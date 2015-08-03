#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#define GDU_API_IS_SUBJECT_TO_CHANGE
#include <gdu/gdu.h>
#include <gio/gio.h>

#ifdef NOTIFY
#  include <libnotify/notify.h>
#ifndef NOTIFY_CHECK_VERSION
#  define NOTIFY_CHECK_VERSION(a,b,c) 0
#endif
static NotifyNotification *nn;
#endif

#include "config.h"
#include "plugin.h"

#define ICON_BUTTON_TRIM 4

#define DEBUG_ON
#ifdef DEBUG_ON
#define DEBUG(fmt,args...) printf(fmt,##args)
#else
#define DEBUG
#endif

typedef struct {

    GtkWidget *plugin;              /* Back pointer to the widget */
    LXPanel *panel;                 /* Back pointer to panel */
    GtkWidget *tray_icon;           /* Displayed image */
    config_setting_t *settings;     /* Plugin settings */
    GtkWidget *menu;
    GHashTable *devices;
    GSList *invalid_devices;
    GduPool *pool;
    GVolumeMonitor *monitor;
    GNotification *notification;
    GApplication *application;
} EjecterPlugin;


/* Device class... */

/* Data structure */

typedef struct {
    EjecterPlugin *plugin;
    GDrive *drive;
    GduDevice *device;
    GSList *volumes;
    GSList *mounts;
    GtkWidget *icon;
    GtkWidget *menuitem;
    GtkMenu *menu;
    gboolean was_removed;
    gboolean icon_updated;
    uint volume_count;
    uint mount_count;
    char *name;
    char *description;
} EjecterDevice;

/* Prototypes */

static EjecterDevice *device_new (GDrive *drive, GduDevice *device, EjecterPlugin *plugin);
static void device_update_label (EjecterDevice *dev);
static void device_attach (EjecterDevice *dev, GtkMenu *menu);
static void device_remove (EjecterDevice *dev);
static void device_set_mounted (EjecterDevice *dev, gboolean mounted);
static void device_handle_clicked_eject (GtkWidget *widget, gpointer ptr);
static void device_handle_unmounted (GtkWidget *widget, gpointer ptr);
static void device_handle_removed_drive (GtkWidget *widget, gpointer ptr);
static void device_handle_removed_volume (GtkWidget *widget, gpointer ptr);
static void device_add_volume (EjecterDevice *dev, GVolume *volume);
static void device_add_mount (EjecterDevice *dev, GMount *mount); 
static void device_eject_done (GObject *source_object, GAsyncResult *res, gpointer ptr);
static void load_devices (EjecterPlugin *data);
static void manage_drive (EjecterPlugin *data, GDrive *drive);
static void manage_volume (EjecterPlugin *data, GVolume *volume);
static void manage_mount (EjecterPlugin *data, GMount *mount);
static void check_icon (EjecterPlugin *data);
static void handle_manage_mount (GtkWidget *widget, GMount *mount, EjecterPlugin *data);
static void handle_manage_volume (GtkWidget *widget, GVolume *volume, EjecterPlugin *data);
static void handle_unmounted (EjecterDevice *dev);
static void handle_removed (EjecterDevice *dev);


/* Function definitions */

static EjecterDevice *device_new (GDrive *drive, GduDevice *device, EjecterPlugin *plugin)
{
    EjecterDevice *d = malloc (sizeof (EjecterDevice));
    
    d->plugin = plugin;
    
    d->was_removed = FALSE;
    d->icon_updated = FALSE;
    d->volume_count = 0;
    d->mount_count = 0;
    
    d->drive = drive;
    d->device = device;
    
    d->volumes = NULL;
    d->mounts = NULL;
    
    d->menuitem = gtk_image_menu_item_new ();
    gtk_menu_item_set_label (GTK_MENU_ITEM (d->menuitem), "");
    
    d->icon = gtk_image_new_from_gicon (g_drive_get_icon (drive), GTK_ICON_SIZE_BUTTON);
    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (d->menuitem), TRUE);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (d->menuitem), d->icon);
    
    gtk_widget_show_all (d->menuitem);
    g_signal_connect (d->menuitem, "activate", G_CALLBACK(device_handle_clicked_eject), d);
    
    if (gdu_device_is_optical_disc (device)) device_set_mounted (d, TRUE);
    else device_set_mounted (d, FALSE);
    
    g_signal_connect (drive, "disconnected", G_CALLBACK(device_handle_removed_drive), d);
        
    return d;
}

static void device_update_label (EjecterDevice *dev)
{
    char buffer[1024];
    if (!dev->menuitem) return;
    if (dev->volume_count == 0)
    {
        dev->name = g_drive_get_name (dev->drive);
        dev->description = 0;
        sprintf (buffer, "%s", dev->name);
    }
    else if (dev->volume_count == 1)
    {
        GVolume *v = g_slist_nth_data (dev->volumes, 0);
        dev->name = g_volume_get_name (v);
        dev->description = g_drive_get_name (dev->drive);
        sprintf (buffer, "%s (%s)", dev->name, dev->description);
    }
    else
    {
        dev->name = g_drive_get_name (dev->drive);
        sprintf (buffer, "%s (", dev->name);
        GSList *iter;
        gboolean first = TRUE;
        for (iter = dev->volumes; iter != NULL; iter = g_slist_next (iter))
        {
            GVolume *v = iter->data;
            if (first)
            {
                strcat (buffer, g_volume_get_name (v));
                first = FALSE;
            }
            else
            {
                strcat (buffer, ", ");
                strcat (buffer, g_volume_get_name (v));                
            } 
        }
        strcat (buffer, ")");
    }
    gtk_menu_item_set_label (GTK_MENU_ITEM (dev->menuitem), buffer);
}

static void device_attach (EjecterDevice *dev, GtkMenu *menu)
{
    gtk_menu_append (menu, dev->menuitem);
    dev->menu = menu;
}    
    
static void device_remove (EjecterDevice *dev)
{
    if (dev->was_removed) return;
    gtk_container_remove (GTK_CONTAINER(dev->menu), dev->menuitem);
    gtk_widget_destroy (dev->icon);
    dev->icon = NULL;
    gtk_widget_destroy (dev->menuitem);
    dev->menuitem = NULL;
    dev->was_removed = TRUE;
    handle_removed (dev);
}

static void device_set_mounted (EjecterDevice *dev, gboolean mounted)
{
    gboolean mnt = (mounted || gdu_device_is_optical_disc (dev->device));
    if (dev->menuitem) gtk_widget_set_sensitive (dev->menuitem, mnt);
}

static void device_handle_clicked_eject (GtkWidget *widget, gpointer ptr)
{
    EjecterDevice *d = (EjecterDevice *) ptr;
    GMountOperation *op = gtk_mount_operation_new (NULL);
    if (gdu_device_is_removable (d->device) && g_drive_can_eject (d->drive))
    {
        DEBUG ("Eject: %s\n", g_drive_get_name (d->drive));
        g_drive_eject_with_operation (d->drive, G_MOUNT_UNMOUNT_NONE, op, NULL, device_eject_done, NULL);
    }
    else
    {
        DEBUG ("Unmount: %s\n", g_drive_get_name (d->drive));
        GList *iter;
        for (iter = g_drive_get_volumes (d->drive); iter != NULL; iter = g_list_next (iter))
        {
            GVolume *v = iter->data;
            GMount *m = g_volume_get_mount (v);
            g_mount_eject_with_operation (m, G_MOUNT_UNMOUNT_NONE, op, NULL, device_eject_done, NULL);
        }
    } 
}

static void device_handle_unmounted (GtkWidget *widget, gpointer ptr)
{   
    GMount *mount = G_MOUNT (widget);
    EjecterDevice *dev = (EjecterDevice *) ptr;
    
    DEBUG ("\nDEVICE UNMOUNTED %s\n", dev->name);
    
    dev->mounts = g_slist_remove (dev->mounts, mount);
    dev->mount_count--;
    device_update_label (dev);
    if (dev->mount_count <= 0)
    {
        device_set_mounted (dev, FALSE);
        handle_unmounted (dev);
    }
}   

static void device_handle_removed_drive (GtkWidget *widget, gpointer ptr)
{
    EjecterDevice *dev = (EjecterDevice *) ptr;
        
    DEBUG ("\nDRIVE REMOVED %s\n", dev->name);
        
    device_remove (dev);
}

static void device_handle_removed_volume (GtkWidget *widget, gpointer ptr)
{
    GVolume *volume = G_VOLUME (widget);
    EjecterDevice *dev = (EjecterDevice *) ptr;

    DEBUG ("\nVOLUME REMOVED %s\n", g_volume_get_name (volume));
    
    dev->volumes = g_slist_remove (dev->volumes, volume);
    dev->volume_count--;
    device_update_label (dev);
    
    //if (dev->volume_count == 0) device_remove (dev);
}   

static void device_add_volume (EjecterDevice *dev, GVolume *volume)
{
    dev->volumes = g_slist_prepend (dev->volumes, volume);
    dev->volume_count++;
    g_signal_connect (volume, "removed", G_CALLBACK (device_handle_removed_volume), dev);
    if (!dev->icon_updated)
    {
        gtk_image_set_from_gicon (GTK_IMAGE (dev->icon), g_volume_get_icon (volume), GTK_ICON_SIZE_BUTTON);
        dev->icon_updated = TRUE;
    }
    device_update_label (dev);
}

static void device_add_mount (EjecterDevice *dev, GMount *mount)    
{
    GVolume *volume = g_mount_get_volume (mount);
    if (g_slist_index (dev->volumes, volume) < 0) device_add_volume (dev, volume);
    dev->mounts = g_slist_prepend (dev->mounts, mount);
    dev->mount_count++;
    device_set_mounted (dev, TRUE);
    g_signal_connect (mount, "unmounted", G_CALLBACK(device_handle_unmounted), dev);
    device_update_label (dev);
}    

static void device_eject_done (GObject *source_object, GAsyncResult *res, gpointer ptr)
{
    DEBUG ("\nEJECT COMPLETE\n");
}

/* Ejecter functions */

static void load_devices (EjecterPlugin *data)
{
    GList *iter, *vol_list = g_volume_monitor_get_volumes (data->monitor);
    
    for (iter = g_list_first (vol_list); iter != NULL; iter = g_list_next (iter))
    {
        GVolume *v = iter->data;
        GDrive *d = g_volume_get_drive (v);
        manage_drive (data, d);
        manage_volume (data, v);
        GMount *m = g_volume_get_mount (v);
        if (m != NULL) manage_mount (data, m);
    }
    check_icon (data);
}

static void manage_drive (EjecterPlugin *data, GDrive *drive)
{
    DEBUG ("\nNEW DEVICE\n");
        
    if (drive == NULL)
    {
        DEBUG ("Drive passed was null, skipping\n");
        return;
    }
    char *id = g_drive_get_identifier (drive, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    if (id == NULL)
    {
        DEBUG ("No id\n");
        return;
    }
    if (g_slist_index (data->invalid_devices, id) != -1)
    {
        DEBUG ("Skipped drive (in invalid list)\n");
        return;
    }
    if (g_hash_table_lookup (data->devices, id) != NULL)
    {
        DEBUG ("Skipped drive (already in list)\n");
        return;
    }
    DEBUG ("Drive id: %s\n", id);
    
    GduDevice *gdu_dev = gdu_pool_get_by_device_file (data->pool, id);
    if (gdu_device_is_system_internal (gdu_dev))
    {
        DEBUG ("Device is internal: skip\n");
        data->invalid_devices = g_slist_append (data->invalid_devices, id);
        return;
    }
    EjecterDevice *d = device_new (drive, gdu_dev, data);
    g_hash_table_insert (data->devices, id, d);
    device_attach (d, GTK_MENU (data->menu));
    check_icon (data);
}

static void manage_volume (EjecterPlugin *data, GVolume *volume)
{
    DEBUG ("\nNEW VOLUME\n");   
    if (volume == NULL)
    {
        DEBUG ("Volume was null, skipping\n");
        return;
    }
    GDrive *drive = g_volume_get_drive (volume);
    if (drive == NULL)
    {
        DEBUG ("Volume did not have a drive, skipping\n");
        return;
    }
    char *id = g_drive_get_identifier (drive, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    if (id == NULL) return;
    if (g_slist_index (data->invalid_devices, id) != -1) return;
    
    DEBUG ("Drive id: %s\n", id);
    DEBUG ("Volume name: %s\n", g_volume_get_name (volume));
        
    EjecterDevice *dev = g_hash_table_lookup (data->devices, id);
    if (dev == NULL) 
    {
        manage_drive (data, drive);
        return;
    }
    device_add_volume (dev, volume);
    check_icon (data);
}

static void manage_mount (EjecterPlugin *data, GMount *mount)
{
    DEBUG ("\nNEW MOUNT\n");
    if (mount == NULL)
    {
        DEBUG ("Mount was null, skipping\n");
        return;
    }
    GDrive *drive = g_mount_get_drive (mount);
    if (drive == NULL)
    {
        DEBUG ("Mount did not have a drive, skipping\n");
        return;
    }
    char *id = g_drive_get_identifier (drive, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    if (id == NULL) return;
    
    DEBUG ("Mount id: %s\n", id);
    DEBUG ("Mount name: %s\n", g_mount_get_name(mount));  
      
    EjecterDevice *dev = g_hash_table_lookup (data->devices, id);
    if (dev == NULL) return;
    device_add_mount (dev, mount);
    check_icon (data);
}

static void check_icon (EjecterPlugin *data)
{
    int num = g_hash_table_size (data->devices);
    if (num <= 0)
    {
        if (gtk_widget_get_visible (data->menu)) gtk_menu_popdown (GTK_MENU (data->menu));
        if (data->application && data->notification)
            g_application_withdraw_notification (data->application, "media-eject");
    }
//        if (num > 0) {
//            this.indicator.set_status(AppIndicator.Status.ACTIVE);
//        } else {
//            try { if (this.notification != null) this.notification.close(); }
//            catch (GLib.Error error) { report_error(error); };
//            this.indicator.set_status(AppIndicator.Status.PASSIVE);
//        }
}

static void handle_manage_volume (GtkWidget *widget, GVolume *volume, EjecterPlugin *data)
{
    manage_volume (data, volume);
}

static void handle_manage_mount (GtkWidget *widget, GMount *mount, EjecterPlugin *data)
{
    manage_mount (data, mount);
}

static void handle_unmounted (EjecterDevice *dev)
{
    if (!dev->was_removed)
    {
        DEBUG ("\nUNMOUNTED %s\n", dev->name);
    
        char buffer[128];
        sprintf (buffer, "%s can be removed", dev->name);
        dev->plugin->notification = g_notification_new (buffer);
        g_notification_set_body (dev->plugin->notification, "It is now possible to safely remove the device.");
        g_application_send_notification (dev->plugin->application, "media-eject", dev->plugin->notification);
    }
    
    //    this.notification.update(_("%s can be removed").printf(device.name),
    //                             _("It's now possible to safely remove the device."), 
    //                             "media-eject");
    //    try { this.notification.show(); }
    //    catch (GLib.Error error) { report_error(error); };
}

static void handle_removed (EjecterDevice *dev)
{
    DEBUG ("\nREMOVED %s\n", dev->name);
    
    EjecterPlugin *data = dev->plugin;
    char *id = g_drive_get_identifier (dev->drive, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    g_hash_table_remove (data->devices, id);
    check_icon (data);
    if (dev->plugin->application && dev->plugin->notification)
        g_application_withdraw_notification (dev->plugin->application, "media-eject");
    
    //                this.indicator.set_menu(this.menu);
    //                try { this.notification.close(); }
    //                catch (GLib.Error error) { report_error(error); };
}

/* Plugin functions */

static void set_icon (LXPanel *p, GtkWidget *image, const char *icon, int size)
{
    GdkPixbuf *pixbuf;
    if (size == 0) size = panel_get_icon_size (p) - ICON_BUTTON_TRIM;
    if (gtk_icon_theme_has_icon (panel_get_icon_theme (p), icon))
    {
        pixbuf = gtk_icon_theme_load_icon (panel_get_icon_theme (p), icon, size, 0, NULL);
        if (pixbuf != NULL)
        {
            gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
            g_object_unref (pixbuf);
            return;
        }
    }
    else
    {
        char path[256];
        sprintf (path, "%s/images/%s.png", PACKAGE_DATA_DIR, icon);
        pixbuf = gdk_pixbuf_new_from_file_at_scale (path, size, size, TRUE, NULL);
        if (pixbuf != NULL)
        {
            gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
            g_object_unref (pixbuf);
        }
    }
}

static void ejecter_popup_set_position (GtkMenu * menu, gint * px, gint * py, gboolean * push_in, gpointer data)
{
    EjecterPlugin * ej = (EjecterPlugin *) data;

    /* Determine the coordinates. */
    lxpanel_plugin_popup_set_position_helper(ej->panel, ej->plugin, GTK_WIDGET(menu), px, py);
    *push_in = TRUE;
}

/* Handler for menu button click */
static gboolean ejecter_button_press_event (GtkWidget *widget, GdkEventButton *event, LXPanel *panel)
{
    EjecterPlugin * ej = lxpanel_plugin_get_data (widget);

    /* Show or hide the popup menu on left-click */
    if (event->button == 1)
    {
        if (ej->menu) 
        {
            gtk_widget_show_all (GTK_WIDGET (ej->menu));
            gtk_menu_popup (GTK_MENU (ej->menu), NULL, NULL, ejecter_popup_set_position, ej, 1, gtk_get_current_event_time ());
        }
        return TRUE;
    }
    else return FALSE;
}

/* Handler for system config changed message from panel */
static void ejecter_configuration_changed (LXPanel *panel, GtkWidget *p)
{
    EjecterPlugin * ej = lxpanel_plugin_get_data (p);
    //!!!! Do some stuff here....
}

/* Plugin destructor. */
static void ejecter_destructor (gpointer user_data)
{
    EjecterPlugin * ej = (EjecterPlugin *) user_data;

    /* Deallocate memory */
    g_free (ej);
}

/* Plugin constructor. */
static GtkWidget *ejecter_constructor (LXPanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context */
    EjecterPlugin * ej = g_new0 (EjecterPlugin, 1);
    GtkWidget *p;
    
    setlocale (LC_ALL, "");
    bindtextdomain (PACKAGE, NULL);
    bind_textdomain_codeset (PACKAGE, "UTF-8");
    textdomain (PACKAGE);

    ej->tray_icon = gtk_image_new ();
    set_icon (panel, ej->tray_icon, "media-eject", 0);
    gtk_widget_set_tooltip_text (ej->tray_icon, _("This is the eject plugin ..."));
    gtk_widget_set_visible (ej->tray_icon, TRUE);

    /* Allocate top level widget and set into Plugin widget pointer. */
    ej->panel = panel;
    ej->plugin = p = gtk_button_new ();
    gtk_button_set_relief (GTK_BUTTON (ej->plugin), GTK_RELIEF_NONE);
    g_signal_connect (ej->plugin, "button-press-event", G_CALLBACK(ejecter_button_press_event), NULL);
    ej->settings = settings;
    lxpanel_plugin_set_data (p, ej, ejecter_destructor);
    gtk_widget_add_events (p, GDK_BUTTON_PRESS_MASK);

    /* Allocate icon as a child of top level */
    gtk_container_add (GTK_CONTAINER(p), ej->tray_icon);
    
    /* Initialise data structures */
    ej->pool = gdu_pool_new ();
    ej->monitor = g_volume_monitor_get ();

    ej->devices = g_hash_table_new (g_str_hash, g_str_equal);
    ej->invalid_devices = NULL;
    
    /* Register application for notifications */
    ej->application = g_application_new (NULL, 0);
    g_application_register (ej->application, NULL, NULL);

    /* Create and populate the menu */
    ej->menu = gtk_menu_new ();
    
    GtkWidget *item = gtk_image_menu_item_new_with_label ("Eject removable media");
    gtk_widget_set_sensitive (item, FALSE);
    gtk_menu_append (ej->menu, item);
    
    item = gtk_separator_menu_item_new ();
    gtk_menu_append (ej->menu, item);
    
    load_devices (ej);
    
    g_signal_connect (ej->monitor, "volume_added", G_CALLBACK (handle_manage_volume), ej);
    g_signal_connect (ej->monitor, "mount_added", G_CALLBACK (handle_manage_mount), ej);

    /* Show the widget, and return. */
    gtk_widget_show_all (p);
    return p;
}

FM_DEFINE_MODULE(lxpanel_gtk, ejecter)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Ejecter"),
    .description = N_("Ejects mounted drives"),
    .new_instance = ejecter_constructor,
    .reconfigure = ejecter_configuration_changed,
    .button_press_event = ejecter_button_press_event
};
