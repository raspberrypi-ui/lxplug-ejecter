/*============================================================================
Copyright (c) 2018-2025 Raspberry Pi Holdings Ltd.
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
============================================================================*/

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

typedef struct 
{
    GtkWidget *plugin;

#ifdef LXPLUG
    LXPanel *panel;                 /* Back pointer to panel */
    config_setting_t *settings;     /* Plugin settings */
#else
    int icon_size;                  /* Variables used under wf-panel */
    gboolean bottom;
    GtkGesture *gesture;
#endif

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

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

extern void ejecter_init (EjecterPlugin *ej);
extern void ejecter_update_display (EjecterPlugin *ej);
extern gboolean ejecter_control_msg (EjecterPlugin *ej, const char *cmd);
extern void ejecter_destructor (gpointer user_data);

/* End of file */
/*----------------------------------------------------------------------------*/
