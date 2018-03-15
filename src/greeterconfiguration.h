/*
 * Copyright (C) 2015 - 2018, Sean Davis <smd.seandavis@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef GREETER_CONFIGURATION_H
#define GREETER_CONFIGURATION_H

#include <glib.h>


#define CONFIG_GROUP_DEFAULT            "greeter"
#define CONFIG_KEY_INDICATORS           "indicators"
#define CONFIG_KEY_DEBUGGING            "allow-debugging"
#define CONFIG_KEY_SCREENSAVER_TIMEOUT  "screensaver-timeout"
#define CONFIG_KEY_THEME                "theme-name"
#define CONFIG_KEY_ICON_THEME           "icon-theme-name"
#define CONFIG_KEY_CURSOR_THEME         "cursor-theme-name"
#define CONFIG_KEY_CURSOR_THEME_SIZE    "cursor-theme-size"
#define CONFIG_KEY_FONT                 "font-name"
#define CONFIG_KEY_DPI                  "xft-dpi"
#define CONFIG_KEY_ANTIALIAS            "xft-antialias"
#define CONFIG_KEY_HINT_STYLE           "xft-hintstyle"
#define CONFIG_KEY_RGBA                 "xft-rgba"
#define CONFIG_KEY_HIDE_USER_IMAGE      "hide-user-image"
#define CONFIG_KEY_DEFAULT_USER_IMAGE   "default-user-image"
#define CONFIG_KEY_KEYBOARD             "keyboard"
#define CONFIG_KEY_READER               "reader"
#define CONFIG_KEY_CLOCK_FORMAT         "clock-format"
#define CONFIG_KEY_ACTIVE_MONITOR       "active-monitor"
#define CONFIG_KEY_POSITION             "position"
#define CONFIG_KEY_PANEL_POSITION       "panel-position"
#define CONFIG_KEY_KEYBOARD_POSITION    "keyboard-position"
#define CONFIG_KEY_A11Y_STATES          "a11y-states"
#define CONFIG_KEY_AT_SPI_ENABLED       "at-spi-enabled"

#define CONFIG_GROUP_MONITOR            "monitor:"
#define CONFIG_KEY_BACKGROUND           "background"
#define CONFIG_KEY_USER_BACKGROUND      "user-background"
#define CONFIG_KEY_LAPTOP               "laptop"
#define CONFIG_KEY_T_TYPE               "transition-type"
#define CONFIG_KEY_T_DURATION           "transition-duration"

#define STATE_SECTION_GREETER           "/greeter"
#define STATE_SECTION_A11Y              "/a11y-states"
#define STATE_KEY_LAST_USER             "last-user"
#define STATE_KEY_LAST_SESSION          "last-session"


void config_init                (void);

gchar** config_get_groups       (const gchar* prefix);
gboolean config_has_key         (const gchar* group, const gchar* key);

gchar* config_get_string        (const gchar* group, const gchar* key, const gchar* fallback);
void config_set_string          (const gchar* group, const gchar* key, const gchar* value);
gchar** config_get_string_list  (const gchar* group, const gchar* key, gchar** fallback);
gint config_get_int             (const gchar* group, const gchar* key, gint fallback);
void config_set_int             (const gchar* group, const gchar* key, gint value);
gboolean config_get_bool        (const gchar* group, const gchar* key, gboolean fallback);
void config_set_bool            (const gchar* group, const gchar* key, gboolean value);
gint config_get_enum            (const gchar* group, const gchar* key, gint fallback, const gchar* first_name, ...) G_GNUC_NULL_TERMINATED;

#endif //GREETER_CONFIGURATION_H
