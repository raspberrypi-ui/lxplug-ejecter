/*
Copyright (c) 2018 Raspberry Pi (Trading) Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include <gio/gio.h>

#include "plugin.h"

#define DEBUG_ON
#ifdef DEBUG_ON
#define DEBUG(fmt,args...) if(getenv("DEBUG_EJ"))g_message("ej: " fmt,##args)
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
    GtkWidget *alignment;           /* Alignment object in popup message */
    GtkWidget *box;                 /* Vbox in popup message */
    GtkWidget *menu;                /* Popup menu */
    GtkWidget *empty;               /* Menuitem shown when no devices */
    GVolumeMonitor *monitor;
    gboolean autohide;
    GList *ejdrives;
    guint hide_timer;
} EjecterPlugin;

typedef struct {
    EjecterPlugin *ej;
    GDrive *drv;
} CallbackData;

typedef struct {
    GDrive *drv;
    int seq;
} EjectList;

#define HIDE_TIME_MS 5000

/* Prototypes */

static void handle_eject_clicked (GtkWidget *widget, gpointer ptr);
static void eject_done (GObject *source_object, GAsyncResult *res, gpointer ptr);
static void update_icon (EjecterPlugin *ej);
static void show_menu (EjecterPlugin *ej);
static void hide_menu (EjecterPlugin *ej);
static GtkWidget *create_menuitem (EjecterPlugin *ej, GDrive *d);

static void log_eject (EjecterPlugin *ej, GDrive *drive)
{
    EjectList *el;
    el = g_new (EjectList, 1);
    el->drv = drive;
    el->seq = -1;
    ej->ejdrives = g_list_append (ej->ejdrives, el);
}

static gboolean was_ejected (EjecterPlugin *ej, GDrive *drive)
{
    GList *l;
    gboolean ejected = FALSE;
    for (l = ej->ejdrives; l != NULL; l = l->next)
    {
        EjectList *el = (EjectList *) l->data;
        if (el->drv == drive)
        {
            ejected = TRUE;
            if (el->seq != -1) lxpanel_notify_clear (el->seq);
            ej->ejdrives = g_list_remove (ej->ejdrives, el);
            g_free (el);
        }
    }
    return ejected;
}

static void add_seq_for_drive (EjecterPlugin *ej, GDrive *drive, int seq)
{
    GList *l;
    for (l = ej->ejdrives; l != NULL; l = l->next)
    {
        EjectList *el = (EjectList *) l->data;
        if (el->drv == drive)
        {
            el->seq = seq;
            return;
        }
    }
}

static void handle_mount_in (GtkWidget *widget, GMount *mount, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("MOUNT ADDED %s", g_mount_get_name (mount));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_mount_out (GtkWidget *widget, GMount *mount, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("MOUNT REMOVED %s", g_mount_get_name (mount));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_mount_pre (GtkWidget *widget, GMount *mount, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("MOUNT PREUNMOUNT %s", g_mount_get_name (mount));
    log_eject (ej, g_mount_get_drive (mount));
}

static void handle_volume_in (GtkWidget *widget, GVolume *vol, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("VOLUME ADDED %s", g_volume_get_name (vol));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_volume_out (GtkWidget *widget, GVolume *vol, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("VOLUME REMOVED %s", g_volume_get_name (vol));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_drive_in (GtkWidget *widget, GDrive *drive, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("DRIVE ADDED %s", g_drive_get_name (drive));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_drive_out (GtkWidget *widget, GDrive *drive, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("DRIVE REMOVED %s", g_drive_get_name (drive));

    if (!was_ejected (ej, drive))
        lxpanel_notify (ej->panel, _("Drive was removed without ejecting\nPlease use menu to eject before removal"));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_eject_clicked (GtkWidget *widget, gpointer data)
{
    CallbackData *dt = (CallbackData *) data;
    EjecterPlugin *ej = dt->ej;
    GDrive *drv = dt->drv;
    DEBUG ("EJECT %s", g_drive_get_name (drv));

    g_drive_eject_with_operation (drv, G_MOUNT_UNMOUNT_NONE, NULL, NULL, eject_done, ej);
}

static void eject_done (GObject *source_object, GAsyncResult *res, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    GDrive *drv = (GDrive *) source_object;
    char *buffer;
    GError *err = NULL;

    g_drive_eject_with_operation_finish (drv, res, &err);

    if (err == NULL)
    {
        DEBUG ("EJECT COMPLETE");
        buffer = g_strdup_printf (_("%s has been ejected\nIt is now safe to remove the device"), g_drive_get_name (drv));
        add_seq_for_drive (ej, drv, lxpanel_notify (ej->panel, buffer));
    }
    else
    {
        DEBUG ("EJECT FAILED");
        buffer = g_strdup_printf (_("Failed to eject %s\n%s"), g_drive_get_name (drv), err->message);
        lxpanel_notify (ej->panel, buffer);
    }
    g_free (buffer);
}


/* Ejecter functions */

static gboolean is_drive_mounted (GDrive *d)
{
    GList *viter, *vols = g_drive_get_volumes (d);

    for (viter = vols; viter != NULL; viter = g_list_next (viter))
    {
        if (g_volume_get_mount ((GVolume *) viter->data) != NULL) return TRUE;
    }
    return FALSE;
}

static void update_icon (EjecterPlugin *ej)
{
    if (ej->autohide)
    {
        /* loop through all devices, checking for mounted volumes */
        GList *driter, *drives = g_volume_monitor_get_connected_drives (ej->monitor);

        for (driter = drives; driter != NULL; driter = g_list_next (driter))
        {
            GDrive *drv = (GDrive *) driter->data;
            if (is_drive_mounted (drv))
            {
                gtk_widget_show_all (ej->plugin);
                gtk_widget_set_sensitive (ej->plugin, TRUE);
                return;
            }
        }
        gtk_widget_hide (ej->plugin);
        gtk_widget_set_sensitive (ej->plugin, FALSE);
    }
}

static void show_menu (EjecterPlugin *ej)
{
    hide_menu (ej);

    ej->menu = gtk_menu_new ();
    gtk_menu_set_reserve_toggle_size (GTK_MENU (ej->menu), FALSE);

    /* loop through all devices, creating menu items for them */
    GList *driter, *drives = g_volume_monitor_get_connected_drives (ej->monitor);
    int count = 0;

    for (driter = drives; driter != NULL; driter = g_list_next (driter))
    {
        GDrive *drv = (GDrive *) driter->data;
        if (is_drive_mounted (drv))
        {
            GtkWidget *item = create_menuitem (ej, drv);
            CallbackData *dt = g_new0 (CallbackData, 1);
            dt->ej = ej;
            dt->drv = drv;
            g_signal_connect (item, "activate", G_CALLBACK (handle_eject_clicked), dt);
            gtk_menu_shell_append (GTK_MENU_SHELL (ej->menu), item);
            count++;
        }
    }

    if (count)
    {
        gtk_widget_show_all (ej->menu);
        gtk_menu_popup_at_widget (GTK_MENU (ej->menu), ej->plugin, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
    }
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

static GtkWidget *create_menuitem (EjecterPlugin *ej, GDrive *d)
{
    char buffer[1024];
    GList *vols;
    GVolume *v;
    GtkWidget *item, *icon, *eject;

    vols = g_drive_get_volumes (d);

    sprintf (buffer, "%s (", g_drive_get_name (d));
    GList *iter;
    gboolean first = TRUE;
    for (iter = vols; iter != NULL; iter = g_list_next (iter))
    {
        v = (GVolume *) iter->data;
        if (g_volume_get_name (v))
        {
            if (first) first = FALSE;
            else strcat (buffer, ", ");
            strcat (buffer, g_volume_get_name (v));
        }
    }
    strcat (buffer, ")");
    icon = gtk_image_new_from_gicon (g_drive_get_icon (d), GTK_ICON_SIZE_BUTTON);

    item = lxpanel_plugin_new_menu_item (ej->panel, buffer, 40, NULL);
    lxpanel_plugin_update_menu_icon (item, icon);

    eject = gtk_image_new ();
    lxpanel_plugin_set_menu_icon (ej->panel, eject, "media-eject");
    lxpanel_plugin_append_menu_icon (item, eject);

    gtk_widget_show_all (item);

    return item;
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

    lxpanel_plugin_set_taskbar_icon (panel, ej->tray_icon, "media-eject");
    update_icon (ej);
}

/* Handler for control message */
static gboolean ejecter_control_msg (GtkWidget *plugin, const char *cmd)
{
    EjecterPlugin *ej = lxpanel_plugin_get_data (plugin);

    DEBUG ("Eject command device %s\n", cmd);

    /* Loop through all drives until we find the one matching the supplied device */
    GList *iter, *drives = g_volume_monitor_get_connected_drives (ej->monitor);
    for (iter = drives; iter != NULL; iter = g_list_next (iter))
    {
        GDrive *d = iter->data;
        char *id = g_drive_get_identifier (d, "unix-device");

        if (!g_strcmp0 (id, cmd)) 
        {
            DEBUG ("EXTERNAL EJECT %s", g_drive_get_name (d));
            log_eject (ej, d);
        }
        g_free (id);
    }
    g_list_free_full (drives, g_object_unref);
    return TRUE;
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
    EjecterPlugin *ej = g_new0 (EjecterPlugin, 1);
    int val;

#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

    /* Allocate top level widget and set into plugin widget pointer. */
    ej->panel = panel;
    ej->settings = settings;
    ej->plugin = gtk_button_new ();
    lxpanel_plugin_set_data (ej->plugin, ej, ejecter_destructor);

    /* Allocate icon as a child of top level */
    ej->tray_icon = gtk_image_new ();
    gtk_container_add (GTK_CONTAINER (ej->plugin), ej->tray_icon);
    lxpanel_plugin_set_taskbar_icon (panel, ej->tray_icon, "media-eject");
    gtk_widget_set_tooltip_text (ej->tray_icon, _("Select a drive in menu to eject safely"));

    /* Set up button */
    gtk_button_set_relief (GTK_BUTTON (ej->plugin), GTK_RELIEF_NONE);

    /* Set up variables */
    if (config_setting_lookup_int (settings, "AutoHide", &val))
    {
        if (val == 1) ej->autohide = TRUE;
        else ej->autohide = FALSE;
    }
    else ej->autohide = FALSE;

    ej->popup = NULL;
    ej->menu = NULL;

    ej->hide_timer = 0;

    /* Get volume monitor and connect to events */
    ej->monitor = g_volume_monitor_get ();
    g_signal_connect (ej->monitor, "volume-added", G_CALLBACK (handle_volume_in), ej);
    g_signal_connect (ej->monitor, "volume-removed", G_CALLBACK (handle_volume_out), ej);
    g_signal_connect (ej->monitor, "mount-added", G_CALLBACK (handle_mount_in), ej);
    g_signal_connect (ej->monitor, "mount-removed", G_CALLBACK (handle_mount_out), ej);
    g_signal_connect (ej->monitor, "mount-pre-unmount", G_CALLBACK (handle_mount_pre), ej);
    g_signal_connect (ej->monitor, "drive-connected", G_CALLBACK (handle_drive_in), ej);
    g_signal_connect (ej->monitor, "drive-disconnected", G_CALLBACK (handle_drive_out), ej);

    /* Show the widget and return. */
    gtk_widget_show_all (ej->plugin);
    return ej->plugin;
}

static gboolean ejecter_apply_configuration (gpointer user_data)
{
    EjecterPlugin *ej = lxpanel_plugin_get_data ((GtkWidget *) user_data);

    config_group_set_int (ej->settings, "AutoHide", ej->autohide);
    if (ej->autohide) update_icon (ej);
    else gtk_widget_show_all (ej->plugin);
    return FALSE;
}

static GtkWidget *ejecter_configure (LXPanel *panel, GtkWidget *p)
{
    EjecterPlugin *ej = lxpanel_plugin_get_data (p);

    return lxpanel_generic_config_dlg(_("Ejecter"), panel,
        ejecter_apply_configuration, p,
        _("Hide icon when no devices"), &ej->autohide, CONF_TYPE_BOOL,
        NULL);
}

FM_DEFINE_MODULE (lxpanel_gtk, ejecter)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Ejecter"),
    .description = N_("Ejects mounted drives"),
    .new_instance = ejecter_constructor,
    .reconfigure = ejecter_configuration_changed,
    .button_press_event = ejecter_button_press_event,
    .config = ejecter_configure,
    .control = ejecter_control_msg,
    .gettext_package = GETTEXT_PACKAGE
};
