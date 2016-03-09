#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include <gio/gio.h>

#include "config.h"
#include "plugin.h"

#define ICON_BUTTON_TRIM 4

//#define DEBUG_ON
#ifdef DEBUG_ON
#define DEBUG(fmt,args...) printf(fmt,##args)
#else
#define DEBUG
#endif

/* Plug-in global data */

typedef struct {

    GtkWidget *plugin;              /* Back pointer to the widget */
    LXPanel *panel;                 /* Back pointer to panel */
    GtkWidget *tray_icon;           /* Displayed image */
    config_setting_t *settings;     /* Plugin settings */
    GtkWidget *popup;               /* Popup message */
    GtkWidget *menu;                /* Popup menu */
    GtkWidget *empty;               /* Menuitem shown when no devices */
    GHashTable *devices;
    GSList *invalid_devices;
    GVolumeMonitor *monitor;
} EjecterPlugin;

/* Per-device data */

typedef struct {
    EjecterPlugin *plugin;
    GDrive *drive;
    GSList *volumes;
    GSList *mounts;
    GtkWidget *icon;
    GtkWidget *menuitem;
    GtkWidget *label;
    GtkWidget *eject;
    gboolean was_removed;
    gboolean was_ejected;
    gboolean was_mounted;
    uint volume_count;
    uint mount_count;
    char *name;
    char *description;
    char *drive_id;
} EjecterDevice;

/* Prototypes */

static void load_devices (EjecterPlugin *data);
static void verify_devices (EjecterPlugin *ej);
static void handle_volume_added (GtkWidget *widget, GVolume *volume, EjecterPlugin *data);
static void handle_mount_added (GtkWidget *widget, GMount *mount, EjecterPlugin *data);
static EjecterDevice *new_device (GDrive *drive, EjecterPlugin *plugin);
static void new_drive (EjecterPlugin *data, GDrive *drive);
static void new_volume (EjecterPlugin *data, GVolume *volume);
static void new_mount (EjecterPlugin *data, GMount *mount);
static void device_add_volume (EjecterDevice *dev, GVolume *volume);
static void device_add_mount (EjecterDevice *dev, GMount *mount); 
static void handle_mount_unmounted (GtkWidget *widget, gpointer ptr);
static void handle_volume_removed (GtkWidget *widget, gpointer ptr);
static void handle_drive_disconnected (GtkWidget *widget, gpointer ptr);
static void handle_eject_clicked (GtkWidget *widget, gpointer ptr);
static void device_remove (EjecterDevice *dev);
static void eject_done (GObject *source_object, GAsyncResult *res, gpointer ptr);
static void unmount_done (EjecterDevice *dev);
static void remove_done (EjecterDevice *dev);
static void show_message (EjecterPlugin *ej, char *str1, char *str2);
static void hide_message (EjecterPlugin *ej);
static void show_menu (EjecterPlugin *ej);
static void hide_menu (EjecterPlugin *ej);
static void create_menuitem (EjecterDevice *d);
static void set_icon (LXPanel *p, GtkWidget *image, const char *icon, int size);

/* Debug functions */

static void dev_dump (EjecterDevice *dev)
{
    GSList *iter;
    printf ("\nDevice\n");
    if (dev->name) printf ("Device name %s\n", dev->name);
    else printf ("Null device name\n");
    if (dev->drive_id) printf ("Local drive ID %s\n", dev->drive_id);
    else printf ("Null local drive ID\n");
    if (dev->drive)
    {
        if (g_drive_get_identifier (dev->drive, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE)) printf ("Drive ID %s\n", g_drive_get_identifier (dev->drive, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE));
        else printf ("Drive exists; null drive ID\n");

        if (g_drive_get_name (dev->drive)) printf ("Drive name %s\n", g_drive_get_name (dev->drive));
        else printf ("Drive exists; null drive name\n");
    }
    else printf ("Null drive\n");
    printf ("Mounted %d Removed %d Ejected %d\n", dev->was_mounted, dev->was_removed, dev->was_ejected);
    printf ("Volumes %d Mounts %d \n", dev->volume_count, dev->mount_count);
    for (iter = dev->volumes; iter != NULL; iter = g_slist_next (iter))
        printf ("Volume name %s\n", g_volume_get_name (iter->data));
    for (iter = dev->mounts; iter != NULL; iter = g_slist_next (iter))
        printf ("Mount name %s\n", g_mount_get_name (iter->data));
    printf ("\n");
}

static void hash_dump (EjecterPlugin *data)
{
    printf ("\n%d entries in hash table\n", g_hash_table_size (data->devices));
    GList *iter, *keys = g_hash_table_get_keys (data->devices);
    for (iter = keys; iter != NULL; iter = g_list_next (iter))
    {
        printf ("Entry %s\n", iter->data);
        EjecterDevice *dev = g_hash_table_lookup (data->devices, iter->data);
        if (dev) dev_dump (dev);
        else printf ("No device matched\n");
    }
    printf ("\n");
}

/* Adding drives, volumes and mounts */

static void load_devices (EjecterPlugin *data)
{
    GList *iter, *vol_list = g_volume_monitor_get_volumes (data->monitor);

    for (iter = g_list_first (vol_list); iter != NULL; iter = g_list_next (iter))
    {
        GVolume *v = iter->data;
        GDrive *d = g_volume_get_drive (v);
        new_drive (data, d);
        new_volume (data, v);
        GMount *m = g_volume_get_mount (v);
        if (m != NULL) new_mount (data, m);
    }
}

static void verify_devices (EjecterPlugin *ej)
{
    // this is a brutal belt-and-braces sanity check for rogue devices that have failed to eject properly....
    GList *iter, *keys = g_hash_table_get_keys (ej->devices);
    for (iter = keys; iter != NULL; iter = g_list_next (iter))
    {
        EjecterDevice *dev = g_hash_table_lookup (ej->devices, iter->data);
        if (access (dev->drive_id, F_OK) == -1)
        {
            DEBUG ("Removing spurious table entry\n");
            g_hash_table_remove (ej->devices, dev->drive_id);
            g_free (dev);
        }
    }
}

static void handle_volume_added (GtkWidget *widget, GVolume *volume, EjecterPlugin *data)
{
    new_volume (data, volume);
    if (data->menu && gtk_widget_get_visible (data->menu)) show_menu (data);
}

static void handle_mount_added (GtkWidget *widget, GMount *mount, EjecterPlugin *data)
{
    new_mount (data, mount);
    if (data->menu && gtk_widget_get_visible (data->menu)) show_menu (data);
}

static EjecterDevice *new_device (GDrive *drive, EjecterPlugin *plugin)
{
    EjecterDevice *d = g_malloc (sizeof (EjecterDevice));

    d->plugin = plugin;
    d->drive = drive;

    d->was_mounted = FALSE;
    d->was_removed = FALSE;
    d->was_ejected = FALSE;
    d->volumes = NULL;
    d->volume_count = 0;
    d->mounts = NULL;
    d->mount_count = 0;

    d->name = NULL;
    d->description = NULL;

    g_signal_connect (drive, "disconnected", G_CALLBACK (handle_drive_disconnected), d);

    return d;
}

static void new_drive (EjecterPlugin *data, GDrive *drive)
{
    DEBUG ("\nNEW DRIVE\n");

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
    
    EjecterDevice *d = new_device (drive, data);
    d->drive_id = id;
    g_hash_table_insert (data->devices, id, d);
}

static void new_volume (EjecterPlugin *data, GVolume *volume)
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
        new_drive (data, drive);
        dev = g_hash_table_lookup (data->devices, id);
        if (dev == NULL) return;
    }
    device_add_volume (dev, volume);
}

static void new_mount (EjecterPlugin *data, GMount *mount)
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
    DEBUG ("Mount name: %s\n", g_mount_get_name (mount));
      
    EjecterDevice *dev = g_hash_table_lookup (data->devices, id);
    if (dev == NULL) return;
    device_add_mount (dev, mount);
}

static void device_add_volume (EjecterDevice *dev, GVolume *volume)
{
    dev->volumes = g_slist_prepend (dev->volumes, volume);
    dev->volume_count = g_slist_length (dev->volumes);

    g_signal_connect (volume, "removed", G_CALLBACK (handle_volume_removed), dev);
}

static void device_add_mount (EjecterDevice *dev, GMount *mount)
{
    GVolume *volume = g_mount_get_volume (mount);
    if (g_slist_index (dev->volumes, volume) < 0) device_add_volume (dev, volume);

    dev->mounts = g_slist_prepend (dev->mounts, mount);
    dev->mount_count = g_slist_length (dev->mounts);

    dev->was_mounted = TRUE;
    g_signal_connect (mount, "unmounted", G_CALLBACK (handle_mount_unmounted), dev);
}


/* Removing drives, volumes and mounts */

static void handle_mount_unmounted (GtkWidget *widget, gpointer ptr)
{
    GMount *mount = G_MOUNT (widget);
    EjecterDevice *dev = (EjecterDevice *) ptr;
    EjecterPlugin *data = dev->plugin;

    DEBUG ("\nDEVICE UNMOUNTED %s\n", dev->name);

    if (!dev) return;
    if (!mount) return;

    if (dev->mount_count && dev->mounts)
    {
        dev->mounts = g_slist_remove (dev->mounts, mount);
        dev->mount_count = g_slist_length (dev->mounts);
    }

    if (dev->mount_count == 0)
    {
        dev->was_mounted = FALSE;
        if (!dev->was_ejected)   // deal with occasional lack of device_handle_drive_disconnected
        {
            dev->was_ejected = TRUE; //!!!!
            if (dev->volume_count && dev->volumes)
            {
                GSList *iter;
                for (iter = dev->volumes; iter != NULL; iter = g_slist_next (iter))
                {
                    GVolume *v = iter->data;
                    g_signal_handlers_disconnect_matched (v, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK (handle_volume_removed), NULL);
                }
                g_slist_free (dev->volumes);
                dev->volume_count = 0;
            }
            device_remove (dev);
        }
        unmount_done (dev);
    }
    if (data->menu && gtk_widget_get_visible (data->menu)) show_menu (data);
}

static void handle_volume_removed (GtkWidget *widget, gpointer ptr)
{
    GVolume *volume = G_VOLUME (widget);
    EjecterDevice *dev = (EjecterDevice *) ptr;
    EjecterPlugin *data = dev->plugin;

    DEBUG ("\nVOLUME REMOVED %s\n", g_volume_get_name (volume));

    if (!dev) return;
    if (!volume) return;

    if (dev->volume_count && dev->volumes)
    {
        dev->volumes = g_slist_remove (dev->volumes, volume);
        dev->volume_count = g_slist_length (dev->volumes);
    }

    if (dev->volume_count == 0 && !dev->was_ejected)
    {
		dev->was_ejected = TRUE;
		device_remove (dev);
	}
    if (data->menu && gtk_widget_get_visible (data->menu)) show_menu (data);
}

static void handle_drive_disconnected (GtkWidget *widget, gpointer ptr)
{
    EjecterDevice *dev = (EjecterDevice *) ptr;
    EjecterPlugin *data = dev->plugin;

	DEBUG ("\nDRIVE REMOVED %s\n", dev->name);

    if (dev->mount_count == 0) dev->was_ejected = TRUE; //!!!!

    device_remove (dev);
    if (data->menu && gtk_widget_get_visible (data->menu)) show_menu (data);
}

static void handle_eject_clicked (GtkWidget *widget, gpointer ptr)
{
    EjecterDevice *d = (EjecterDevice *) ptr;
    GMountOperation *op = gtk_mount_operation_new (NULL);
    d->was_ejected = TRUE;
    if (g_drive_is_media_removable (d->drive) && g_drive_can_eject (d->drive))
    {
        DEBUG ("Eject: %s\n", g_drive_get_name (d->drive));
        g_drive_eject_with_operation (d->drive, G_MOUNT_UNMOUNT_NONE, op, NULL, eject_done, d);
    }
    else
    {
        DEBUG ("Unmount: %s\n", g_drive_get_name (d->drive));
        GList *iter;
        for (iter = g_drive_get_volumes (d->drive); iter != NULL; iter = g_list_next (iter))
        {
            GVolume *v = iter->data;
            GMount *m = g_volume_get_mount (v);
            g_mount_eject_with_operation (m, G_MOUNT_UNMOUNT_NONE, op, NULL, eject_done, d);
        }
    }
}

static void device_remove (EjecterDevice *dev)
{
    if (!dev) return;
    if (dev->was_removed) return;
    g_signal_handlers_disconnect_matched (dev->drive, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK (handle_drive_disconnected), NULL);
    dev->was_removed = TRUE;
    remove_done (dev);
}

static void eject_done (GObject *source_object, GAsyncResult *res, gpointer ptr)
{
    DEBUG ("\nEJECT COMPLETE\n");

    EjecterDevice *dev = (EjecterDevice *) ptr;

    // final backup check in case unmount calls didn't happen...
    if (!dev) return;

    dev->was_mounted = FALSE;
    if (dev->mount_count && dev->mounts)
    {
        g_slist_free (dev->mounts);
        dev->mounts = NULL;
        dev->mount_count = 0;
    }
    else return;

    DEBUG ("Backup eject called\n");
    unmount_done (dev);
}

static void unmount_done (EjecterDevice *dev)
{
    if (!dev->was_removed)
    {
        DEBUG ("\nUNMOUNTED %s\n", dev->name);

        char buffer[128];
        sprintf (buffer, _("%s has been ejected"), dev->name);
        show_message (dev->plugin, buffer, _("It is now safe to remove the device"));
    }
}

static void remove_done (EjecterDevice *dev)
{
    DEBUG ("\nREMOVED %s\n", dev->name);

    if (!dev) return;

    EjecterPlugin *data = dev->plugin;
    if (dev->drive_id) g_hash_table_remove (data->devices, dev->drive_id);

    GSList *iter;
    for (iter = dev->mounts; iter != NULL; iter = g_slist_next (iter))
    {
        GMount *m = iter->data;
        g_signal_handlers_disconnect_matched (m, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK (handle_mount_unmounted), NULL);
    }
    for (iter = dev->volumes; iter != NULL; iter = g_slist_next (iter))
    {
        GVolume *v = iter->data;
        g_signal_handlers_disconnect_matched (v, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK (handle_volume_removed), NULL);
    }

    if (!dev->was_ejected)
    {
        char buffer[128];
		sprintf (buffer, _("%s was removed without ejecting"), dev->name);
        show_message (dev->plugin, buffer, _("Please use menu to eject before removal"));
    }
    else hide_message (dev->plugin);

    g_free (dev);
}

/* Ejecter functions */

static void ejecter_popup_set_position (GtkMenu *menu, gint *px, gint *py, gboolean *push_in, gpointer data)
{
    EjecterPlugin * ej = (EjecterPlugin *) data;
    /* Determine the coordinates. */
    lxpanel_plugin_popup_set_position_helper(ej->panel, ej->plugin, GTK_WIDGET(menu), px, py);
    *push_in = TRUE;
}

static void show_message (EjecterPlugin *ej, char *str1, char *str2)
{
    GtkWidget *item;

    hide_menu (ej);
    hide_message (ej);

    ej->popup = gtk_menu_new ();
    item = gtk_menu_item_new_with_label (str1);
    gtk_widget_set_sensitive (item, FALSE);
    gtk_menu_append (GTK_MENU (ej->popup), item);

    item = gtk_menu_item_new_with_label (str2);
    gtk_widget_set_sensitive (item, FALSE);
    gtk_menu_append (GTK_MENU (ej->popup), item);

    gtk_widget_show_all (ej->popup);
    gtk_menu_popup (GTK_MENU (ej->popup), NULL, NULL, ejecter_popup_set_position, ej, 1, gtk_get_current_event_time ());
}

static void hide_message (EjecterPlugin *ej)
{
    if (ej->popup)
    {
		gtk_menu_popdown (GTK_MENU (ej->popup));
		gtk_widget_destroy (ej->popup);
		ej->popup = NULL;
	}
}

static void show_menu (EjecterPlugin *ej)
{
    hide_message (ej);
    hide_menu (ej);

    verify_devices (ej);
    ej->menu = gtk_menu_new ();

    /* Loop through all devices, creating menuitems for them */
    int count = 0;
    GList *iter, *keys = g_hash_table_get_keys (ej->devices);
    for (iter = keys; iter != NULL; iter = g_list_next (iter))
    {
        EjecterDevice *dev = g_hash_table_lookup (ej->devices, iter->data);
        create_menuitem (dev);
        gtk_menu_append (ej->menu, dev->menuitem);
        count++;
    }

    /* If no devices were found, create the "empty" menuitem */
    if (!count)
    {
        ej->empty = gtk_menu_item_new_with_label (_("No ejectable devices"));
        gtk_widget_set_sensitive (ej->empty, FALSE);
        gtk_menu_append (ej->menu, ej->empty);
    }

    gtk_widget_show_all (ej->menu);
    gtk_menu_popup (GTK_MENU (ej->menu), NULL, NULL, ejecter_popup_set_position, ej, 1, gtk_get_current_event_time ());
}

static void hide_menu (EjecterPlugin *ej)
{
    if (ej->menu)
    {
		gtk_menu_popdown (GTK_MENU (ej->menu));
		gtk_widget_destroy (ej->menu);
		ej->menu = NULL;
	}
}

static void create_menuitem (EjecterDevice *d)
{
    char buffer[1024];
    GVolume *v;
    d->menuitem = gtk_image_menu_item_new ();

    GtkWidget *box = gtk_hbox_new (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (d->menuitem), box);

    d->label = gtk_label_new (NULL);
    gtk_label_set_max_width_chars (GTK_LABEL (d->label), 40);
    gtk_label_set_ellipsize (GTK_LABEL (d->label), PANGO_ELLIPSIZE_END);
    if (d->volume_count == 0)
    {
        d->name = g_drive_get_name (d->drive);
        d->description = 0;
        sprintf (buffer, "%s", d->name);
        d->icon = gtk_image_new_from_gicon (g_drive_get_icon (d->drive), GTK_ICON_SIZE_BUTTON);
    }
    else if (d->volume_count == 1)
    {
        v = g_slist_nth_data (d->volumes, 0);
        d->name = g_drive_get_name (d->drive);
        d->description = g_volume_get_name (v);
        sprintf (buffer, "%s (%s)", d->name, d->description);
        d->icon = gtk_image_new_from_gicon (g_volume_get_icon (v), GTK_ICON_SIZE_BUTTON);
    }
    else
    {
        d->name = g_drive_get_name (d->drive);
        sprintf (buffer, "%s (", d->name);
        GSList *iter;
        gboolean first = TRUE;
        for (iter = d->volumes; iter != NULL; iter = g_slist_next (iter))
        {
            v = iter->data;
            if (first) first = FALSE;
            else strcat (buffer, ", ");
            if (g_volume_get_name (v)) strcat (buffer, g_volume_get_name (v));
        }
        strcat (buffer, ")");
        d->icon = gtk_image_new_from_gicon (g_volume_get_icon (v), GTK_ICON_SIZE_BUTTON);
    }
    gtk_label_set_text (GTK_LABEL (d->label), buffer);
    gtk_misc_set_alignment (GTK_MISC (d->label), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX (box), d->label, TRUE, TRUE, 0);

    d->eject = gtk_image_new_from_icon_name ("media-eject", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start (GTK_BOX (box), d->eject, FALSE, FALSE, 0);

    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (d->menuitem), TRUE);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (d->menuitem), d->icon);
    gtk_widget_set_sensitive (d->menuitem, d->was_mounted);

    gtk_widget_show_all (d->menuitem);
    g_signal_connect (d->menuitem, "activate", G_CALLBACK (handle_eject_clicked), d);
}

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

/* Handler for menu button click */
static gboolean ejecter_button_press_event (GtkWidget *widget, GdkEventButton *event, LXPanel *panel)
{
    EjecterPlugin * ej = lxpanel_plugin_get_data (widget);

    /* Show or hide the popup menu on left-click */
    if (event->button == 1)
    {
        show_menu (ej);
        return TRUE;
    }
    else return FALSE;
}

/* Handler for system config changed message from panel */
static void ejecter_configuration_changed (LXPanel *panel, GtkWidget *p)
{
    EjecterPlugin * ej = lxpanel_plugin_get_data (p);

    set_icon (panel, ej->tray_icon, "media-eject", 0);
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
    gtk_widget_set_tooltip_text (ej->tray_icon, _("Select a drive in menu to eject safely"));
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
    ej->monitor = g_volume_monitor_get ();

    ej->devices = g_hash_table_new (g_str_hash, g_str_equal);
    ej->invalid_devices = NULL;
    
    /* Read in current devices */
    load_devices (ej);

    ej->popup = NULL;
    ej->menu = NULL;

    g_signal_connect (ej->monitor, "volume_added", G_CALLBACK (handle_volume_added), ej);
    g_signal_connect (ej->monitor, "mount_added", G_CALLBACK (handle_mount_added), ej);

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
