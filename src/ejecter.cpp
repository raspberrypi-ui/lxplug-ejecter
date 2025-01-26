/*============================================================================
Copyright (c) 2024 Raspberry Pi Holdings Ltd.
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

#include <glibmm.h>
#include "ejecter.hpp"

extern "C" {
    WayfireWidget *create () { return new WayfireEjecter; }
    void destroy (WayfireWidget *w) { delete w; }

    static constexpr conf_table_t conf_table[2] = {
        {CONF_BOOL, "autohide", N_("Hide icon when no devices")},
        {CONF_NONE,  NULL,       NULL}
    };
    const conf_table_t *config_params (void) { return conf_table; };
    const char *display_name (void) { return N_("Ejecter"); };
    const char *package_name (void) { return GETTEXT_PACKAGE; };
}

void WayfireEjecter::bar_pos_changed_cb (void)
{
    if ((std::string) bar_pos == "bottom") ej->bottom = TRUE;
    else ej->bottom = FALSE;
}

void WayfireEjecter::icon_size_changed_cb (void)
{
    ej->icon_size = icon_size;
    ejecter_update_display (ej);
}

void WayfireEjecter::command (const char *cmd)
{
    ejecter_control_msg (ej, cmd);
}

bool WayfireEjecter::set_icon (void)
{
    ejecter_update_display (ej);
    return false;
}

void WayfireEjecter::settings_changed_cb (void)
{
    ej->autohide = autohide;
    ejecter_update_display (ej);
}

void WayfireEjecter::init (Gtk::HBox *container)
{
    /* Create the button */
    plugin = std::make_unique <Gtk::Button> ();
    plugin->set_name (PLUGIN_NAME);
    container->pack_start (*plugin, false, false);

    /* Setup structure */
    ej = g_new0 (EjecterPlugin, 1);
    ej->plugin = (GtkWidget *)((*plugin).gobj());
    ej->icon_size = icon_size;
    icon_timer = Glib::signal_idle().connect (sigc::mem_fun (*this, &WayfireEjecter::set_icon));
    bar_pos_changed_cb ();

    /* Initialise the plugin */
    ejecter_init (ej);

    /* Setup callbacks */
    icon_size.set_callback (sigc::mem_fun (*this, &WayfireEjecter::icon_size_changed_cb));
    bar_pos.set_callback (sigc::mem_fun (*this, &WayfireEjecter::bar_pos_changed_cb));

    autohide.set_callback (sigc::mem_fun (*this, &WayfireEjecter::settings_changed_cb));

    settings_changed_cb ();
}

WayfireEjecter::~WayfireEjecter()
{
    icon_timer.disconnect ();
    ejecter_destructor (ej);
}

/* End of file */
/*----------------------------------------------------------------------------*/
