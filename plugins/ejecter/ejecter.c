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

#define ICON_BUTTON_TRIM 4

//#define DEBUG_ON
#ifdef DEBUG_ON
#define DEBUG(fmt,args...) g_message("ej: " fmt,##args)
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
    gboolean ejecting;
} EjecterPlugin;

typedef struct {
    EjecterPlugin *ej;
    GDrive *drv;
} CallbackData;

/* Prototypes */

static void handle_eject_clicked (GtkWidget *widget, gpointer ptr);
static void eject_done (GObject *source_object, GAsyncResult *res, gpointer ptr);
static void gtk_tooltip_window_style_set (EjecterPlugin *ej);
static void draw_round_rect (cairo_t *cr, gdouble aspect, gdouble x, gdouble y, gdouble corner_radius, gdouble width, gdouble height);
static void fill_background (GtkWidget *widget, cairo_t *cr, GdkColor *bg_color, GdkColor *border_color, guchar alpha);
static void update_shape (EjecterPlugin *ej);
static gboolean gtk_tooltip_paint_window (EjecterPlugin *ej);
static void ejecter_popup_set_position (GtkMenu *menu, gint *px, gint *py, gboolean *push_in, gpointer data);
static gboolean ejecter_mouse_out (GtkWidget * widget, GdkEventButton * event, EjecterPlugin *ej);
static void show_message (EjecterPlugin *ej, char *str1, char *str2);
static void hide_message (EjecterPlugin *ej);
static void update_icon (EjecterPlugin *ej);
static void show_menu (EjecterPlugin *ej);
static void hide_menu (EjecterPlugin *ej);
static GtkWidget *create_menuitem (EjecterPlugin *ej, GDrive *d);
static void set_icon (LXPanel *p, GtkWidget *image, const char *icon, int size);


static void handle_mount_in (GtkWidget *widget, GMount *mount, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("MOUNT ADDED");

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_mount_out (GtkWidget *widget, GMount *mount, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("MOUNT REMOVED");

    if (ej->ejecting == FALSE)
        show_message (ej, _("Drive was removed without ejecting"), _("Please use menu to eject before removal"));

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_volume_in (GtkWidget *widget, GVolume *vol, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("VOLUME ADDED");

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_volume_out (GtkWidget *widget, GVolume *vol, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("VOLUME REMOVED");

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_drive_in (GtkWidget *widget, GDrive *drive, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("DRIVE ADDED");

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_drive_out (GtkWidget *widget, GDrive *drive, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    DEBUG ("DRIVE REMOVED");

    hide_message (ej);

    if (ej->menu && gtk_widget_get_visible (ej->menu)) show_menu (ej);
    update_icon (ej);
}

static void handle_eject_clicked (GtkWidget *widget, gpointer data)
{
    CallbackData *dt = (CallbackData *) data;
    EjecterPlugin *ej = dt->ej;
    GDrive *drv = dt->drv;
    DEBUG ("EJECT %s", g_drive_get_name (drv));

    ej->ejecting = TRUE;
    g_drive_eject_with_operation (drv, G_MOUNT_UNMOUNT_NONE, NULL, NULL, eject_done, ej);
}

static void eject_done (GObject *source_object, GAsyncResult *res, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    GDrive *drv = (GDrive *) source_object;
    char buffer[128];
    DEBUG ("EJECT COMPLETE");

    ej->ejecting = FALSE;
    g_drive_eject_with_operation_finish (drv, res, NULL);
    sprintf (buffer, _("%s has been ejected"), g_drive_get_name (drv));
    show_message (ej, buffer, _("It is now safe to remove the device"));
}

/* The functions below are a copy of those in GTK+2.0's gtktooltip.c, as for some reason, you cannot */
/* manually cause a tooltip to appear with a simple function call. I have no idea why not... */

static void on_screen_changed (GtkWidget *window)
{
	GdkScreen *screen;
	GdkColormap *cmap;

	screen = gtk_widget_get_screen (window);
	cmap = NULL;
	if (gdk_screen_is_composited (screen)) cmap = gdk_screen_get_rgba_colormap (screen);
	if (cmap == NULL) cmap = gdk_screen_get_rgb_colormap (screen);

	gtk_widget_set_colormap (window, cmap);
}

static void gtk_tooltip_window_style_set (EjecterPlugin *ej)
{
    gtk_alignment_set_padding (GTK_ALIGNMENT (ej->alignment), ej->popup->style->ythickness, ej->popup->style->ythickness,
        ej->popup->style->xthickness, ej->popup->style->xthickness);
    gtk_box_set_spacing (GTK_BOX (ej->box), ej->popup->style->xthickness);
    gtk_widget_queue_draw (ej->popup);
}

static void draw_round_rect (cairo_t *cr, gdouble aspect, gdouble x, gdouble y, gdouble corner_radius, gdouble width, gdouble height)
{
    gdouble radius = corner_radius / aspect;
    cairo_move_to (cr, x + radius, y);
    cairo_line_to (cr, x + width - radius, y);
    cairo_arc (cr, x + width - radius, y + radius, radius, -90.0f * G_PI / 180.0f, 0.0f * G_PI / 180.0f);
    cairo_line_to (cr, x + width, y + height - radius);
    cairo_arc (cr, x + width - radius, y + height - radius, radius, 0.0f * G_PI / 180.0f, 90.0f * G_PI / 180.0f);
    cairo_line_to (cr, x + radius, y + height);
    cairo_arc (cr, x + radius, y + height - radius, radius,  90.0f * G_PI / 180.0f, 180.0f * G_PI / 180.0f);
    cairo_line_to (cr, x, y + radius);
    cairo_arc (cr, x + radius, y + radius, radius, 180.0f * G_PI / 180.0f, 270.0f * G_PI / 180.0f);
    cairo_close_path (cr);
}

static void fill_background (GtkWidget *widget, cairo_t *cr, GdkColor *bg_color, GdkColor *border_color, guchar alpha)
{
    gint tooltip_radius;

    if (!gtk_widget_is_composited (widget)) alpha = 255;

    gtk_widget_style_get (widget, "tooltip-radius", &tooltip_radius,  NULL);

    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

    draw_round_rect (cr, 1.0, 0.5, 0.5, tooltip_radius, widget->allocation.width - 1,  widget->allocation.height - 1);

    cairo_set_source_rgba (cr, (float) bg_color->red / 65535.0, (float) bg_color->green / 65535.0, (float) bg_color->blue / 65535.0, (float) alpha / 255.0);
    cairo_fill_preserve (cr);

    cairo_set_source_rgba (cr, (float) border_color->red / 65535.0, (float) border_color->green / 65535.0, (float) border_color->blue / 65535.0, (float) alpha / 255.0);
    cairo_set_line_width (cr, 1.0);
    cairo_stroke (cr);
}

static void update_shape (EjecterPlugin *ej)
{
    GdkBitmap *mask;
    cairo_t *cr;
    gint width, height, tooltip_radius;

    gtk_widget_style_get (ej->popup, "tooltip-radius", &tooltip_radius, NULL);

    if (tooltip_radius == 0 || gtk_widget_is_composited (ej->popup))
    {
        gtk_widget_shape_combine_mask (ej->popup, NULL, 0, 0);
        return;
    }

    gtk_window_get_size (GTK_WINDOW (ej->popup), &width, &height);
    mask = (GdkBitmap *) gdk_pixmap_new (NULL, width, height, 1);
    cr = gdk_cairo_create (mask);

    fill_background (ej->popup, cr, &ej->popup->style->black, &ej->popup->style->black, 255);
    gtk_widget_shape_combine_mask (ej->popup, mask, 0, 0);

    cairo_destroy (cr);
    g_object_unref (mask);
}

static gboolean gtk_tooltip_paint_window (EjecterPlugin *ej)
{
    guchar tooltip_alpha;
    gint tooltip_radius;

    gtk_widget_style_get (ej->popup, "tooltip-alpha", &tooltip_alpha, "tooltip-radius", &tooltip_radius, NULL);

    if (tooltip_alpha != 255 || tooltip_radius != 0)
    {
        cairo_t *cr = gdk_cairo_create (ej->popup->window);
        fill_background (ej->popup, cr, &ej->popup->style->bg [GTK_STATE_NORMAL], &ej->popup->style->bg [GTK_STATE_SELECTED], tooltip_alpha);
        cairo_destroy (cr);
        update_shape (ej);
    }
    else gtk_paint_flat_box (ej->popup->style, ej->popup->window, GTK_STATE_NORMAL, GTK_SHADOW_OUT, NULL, ej->popup, "tooltip",
        0, 0, ej->popup->allocation.width, ej->popup->allocation.height);

    return FALSE;
}

/* Ejecter functions */

static void ejecter_popup_set_position (GtkMenu *menu, gint *px, gint *py, gboolean *push_in, gpointer data)
{
    EjecterPlugin *ej = (EjecterPlugin *) data;
    /* Determine the coordinates. */
    lxpanel_plugin_popup_set_position_helper (ej->panel, ej->plugin, GTK_WIDGET(menu), px, py);
    *push_in = TRUE;
}

static gboolean ejecter_mouse_out (GtkWidget * widget, GdkEventButton * event, EjecterPlugin *ej)
{
    gtk_widget_hide (ej->popup);
    gdk_pointer_ungrab (GDK_CURRENT_TIME);
    return FALSE;
}

static void show_message (EjecterPlugin *ej, char *str1, char *str2)
{
    GtkWidget *item;
    gint x, y;

    hide_menu (ej);
    hide_message (ej);

    ej->popup = gtk_window_new (GTK_WINDOW_POPUP);
    on_screen_changed (ej->popup);
    gtk_window_set_type_hint (GTK_WINDOW (ej->popup), GDK_WINDOW_TYPE_HINT_TOOLTIP);
    gtk_widget_set_app_paintable (ej->popup, TRUE);
    gtk_window_set_resizable (GTK_WINDOW (ej->popup), FALSE);
    gtk_widget_set_name (ej->popup, "gtk-tooltip");

    ej->alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
    gtk_container_add (GTK_CONTAINER (ej->popup), ej->alignment);
    gtk_widget_show (ej->alignment);

    g_signal_connect_swapped (ej->popup, "style-set", G_CALLBACK (gtk_tooltip_window_style_set), ej);
    g_signal_connect_swapped (ej->popup, "expose-event", G_CALLBACK (gtk_tooltip_paint_window), ej);

    ej->box = gtk_vbox_new (FALSE, ej->popup->style->xthickness);
    gtk_container_add (GTK_CONTAINER (ej->alignment), ej->box);

    item = gtk_label_new (str1);
    gtk_box_pack_start (GTK_BOX (ej->box), item, FALSE, FALSE, 0);
    item = gtk_label_new (str2);
    gtk_box_pack_start (GTK_BOX (ej->box), item, FALSE, FALSE, 0);

    gtk_widget_show_all (ej->popup);
    gtk_widget_hide (ej->popup);
    lxpanel_plugin_popup_set_position_helper (ej->panel, ej->tray_icon, ej->popup, &x, &y);
    gdk_window_move (gtk_widget_get_window (ej->popup), x, y);
    gtk_window_present (GTK_WINDOW (ej->popup));
    gdk_pointer_grab (gtk_widget_get_window (ej->popup), TRUE, GDK_BUTTON_PRESS_MASK, NULL, NULL, GDK_CURRENT_TIME);
    g_signal_connect (G_OBJECT(ej->popup), "button-press-event", G_CALLBACK (ejecter_mouse_out), ej);
}

static void hide_message (EjecterPlugin *ej)
{
    if (ej->popup)
    {
		gtk_widget_destroy (ej->popup);
		ej->popup = NULL;
	}
}

static gboolean is_drive_mounted (GDrive *d)
{
    GList *viter, *vols = g_drive_get_volumes (d);
    int volume_count = g_list_length (vols);
    if (volume_count)
    {
        for (viter = vols; viter != NULL; viter = g_list_next (viter))
        {
            if (g_volume_get_mount ((GVolume *) viter->data) != NULL) return TRUE;
        }
    }
    return FALSE;
}

static void update_icon (EjecterPlugin *ej)
{
    if (ej->autohide)
    {
        /* loop through all devices, checking for mounted volumes */
        GList *driter, *drives = g_volume_monitor_get_connected_drives (ej->monitor);
        int count = 0;

        for (driter = drives; driter != NULL; driter = g_list_next (driter))
        {
            GDrive *drv = (GDrive *) driter->data;
            if (is_drive_mounted (drv))
            {
                gtk_widget_show_all (ej->plugin);
                return;
            }
        }
        gtk_widget_hide_all (ej->plugin);
    }
}

static void show_menu (EjecterPlugin *ej)
{
    hide_message (ej);
    hide_menu (ej);

    ej->menu = gtk_menu_new ();

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
            gtk_menu_append (ej->menu, item);
            count++;
        }
    }

    if (count)
    {
        gtk_widget_show_all (ej->menu);
        gtk_menu_popup (GTK_MENU (ej->menu), NULL, NULL, ejecter_popup_set_position, ej, 1, gtk_get_current_event_time ());
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
    GtkWidget *item, *icon, *box, *label, *eject;
    int volume_count;

    vols = g_drive_get_volumes (d);
    volume_count = g_list_length (vols);

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
    icon = gtk_image_new_from_gicon (g_volume_get_icon (v), GTK_ICON_SIZE_BUTTON);

    item = gtk_image_menu_item_new ();
    box = gtk_hbox_new (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (item), box);

    label = gtk_label_new (NULL);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_text (GTK_LABEL (label), buffer);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

    eject = gtk_image_new ();
    set_icon (ej->panel, eject, "media-eject", 16);
    gtk_box_pack_start (GTK_BOX (box), eject, FALSE, FALSE, 0);

    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), icon);

    gtk_widget_show_all (item);

    return item;
}

static void set_icon (LXPanel *p, GtkWidget *image, const char *icon, int size)
{
    GdkPixbuf *pixbuf;
    if (size == 0) size = panel_get_icon_size (p) - ICON_BUTTON_TRIM;
    if (gtk_icon_theme_has_icon (panel_get_icon_theme (p), icon))
    {
        GtkIconInfo *info = gtk_icon_theme_lookup_icon (panel_get_icon_theme (p), icon, size, GTK_ICON_LOOKUP_FORCE_SIZE);
        pixbuf = gtk_icon_info_load_icon (info, NULL);
        gtk_icon_info_free (info);
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

#ifdef ENABLE_NLS
    textdomain ( GETTEXT_PACKAGE );
#endif
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
    int val;
    
#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

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

    if (config_setting_lookup_int (settings, "AutoHide", &val))
    {
        if (val == 1) ej->autohide = TRUE;
        else ej->autohide = FALSE;
    }
    else ej->autohide = FALSE;

    ej->popup = NULL;
    ej->menu = NULL;
    ej->ejecting = FALSE;

    g_signal_connect (ej->monitor, "volume_added", G_CALLBACK (handle_volume_in), ej);
    g_signal_connect (ej->monitor, "volume_removed", G_CALLBACK (handle_volume_out), ej);
    g_signal_connect (ej->monitor, "mount_added", G_CALLBACK (handle_mount_in), ej);
    g_signal_connect (ej->monitor, "mount_removed", G_CALLBACK (handle_mount_out), ej);
    g_signal_connect (ej->monitor, "drive_connected", G_CALLBACK (handle_drive_in), ej);
    g_signal_connect (ej->monitor, "drive_disconnected", G_CALLBACK (handle_drive_out), ej);

    /* Show the widget, and return. */
    gtk_widget_show_all (p);
    update_icon (ej);
    return p;
}

static gboolean ejecter_apply_configuration (gpointer user_data)
{
    EjecterPlugin *ej = lxpanel_plugin_get_data ((GtkWidget *) user_data);

    config_group_set_int (ej->settings, "AutoHide", ej->autohide);
    if (ej->autohide) update_icon (ej);
    else  gtk_widget_show_all (ej->plugin);
}

static GtkWidget *ejecter_configure (LXPanel *panel, GtkWidget *p)
{
    EjecterPlugin *ej = lxpanel_plugin_get_data (p);
    return lxpanel_generic_config_dlg(_("Ejecter"), panel,
        ejecter_apply_configuration, p,
        _("Hide icon when no devices"), &ej->autohide, CONF_TYPE_BOOL,
        NULL);
}

FM_DEFINE_MODULE(lxpanel_gtk, ejecter)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Ejecter"),
    .description = N_("Ejects mounted drives"),
    .new_instance = ejecter_constructor,
    .reconfigure = ejecter_configuration_changed,
    .button_press_event = ejecter_button_press_event,
    .config = ejecter_configure,
    .gettext_package = GETTEXT_PACKAGE
};
