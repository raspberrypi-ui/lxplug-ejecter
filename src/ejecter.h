typedef struct {
#ifdef LXPLUG
    LXPanel *panel;                 /* Back pointer to panel */
    config_setting_t *settings;     /* Plugin settings */
#else
    int icon_size;                  /* Variables used under wf-panel */
    gboolean bottom;
    GtkGesture *gesture;
#endif
    GtkWidget *plugin;              /* Back pointer to the widget */
    GtkWidget *tray_icon;           /* Displayed image */
    GtkWidget *popup;               /* Popup message */
    GtkWidget *alignment;           /* Alignment object in popup message */
    GtkWidget *box;                 /* Vbox in popup message */
    GtkWidget *menu;                /* Popup menu */
    GtkWidget *empty;               /* Menuitem shown when no devices */
    GVolumeMonitor *monitor;
    gboolean autohide;
    GList *ejdrives;
    GList *mdrives;
    guint hide_timer;
} EjecterPlugin;
