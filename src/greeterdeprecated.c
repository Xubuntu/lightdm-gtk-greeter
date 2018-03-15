/*
 * Copyright (C) 2017 - 2018, Sean Davis <smd.seandavis@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <glib.h>

#include "greeterdeprecated.h"

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

void
greeter_gdk_flush (void)
{
  /* Deprecated GTK ???? */
  gdk_flush ();
}

void
greeter_error_trap_push (void)
{
  /* Deprecated GTK ???? */
  gdk_error_trap_push ();
}

void
greeter_error_trap_pop_ignored (void)
{
  /* Deprecated GTK ???? */
  gdk_error_trap_pop_ignored ();
}

gint
greeter_screen_get_number (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_number (screen);
}

gint
greeter_screen_get_width (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_width (screen);
}

gint
greeter_screen_get_width_mm (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_width_mm (screen);
}

gint
greeter_screen_get_height (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_height (screen);
}

gint
greeter_screen_get_height_mm (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_height_mm (screen);
}

gint
greeter_screen_get_n_monitors (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_n_monitors (screen);
}

gchar *
greeter_screen_get_monitor_plug_name (GdkScreen *screen,
                                      gint       monitor_num)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_monitor_plug_name (screen, monitor_num);
}

void
greeter_screen_get_monitor_geometry (GdkScreen    *screen,
                                     gint          monitor_num,
                                     GdkRectangle *dest)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_monitor_geometry (screen, monitor_num, dest);
}

gint
greeter_screen_get_primary_monitor (GdkScreen *screen)
{
  /* Deprecated GTK 3.22 */
  return gdk_screen_get_primary_monitor (screen);
}

void
greeter_widget_reparent (GtkWidget *widget,
                         GtkWidget *new_parent)
{
  /* Deprecated GTK 3.14 */
  return gtk_widget_reparent (widget, new_parent);
}

GdkDeviceManager *
greeter_display_get_device_manager (GdkDisplay *display)
{
  /* Deprecated GTK 3.20 */
  return gdk_display_get_device_manager (display);
}

GdkDevice *
greeter_device_manager_get_client_pointer (GdkDeviceManager *device_manager)
{
  /* Deprecated GTK 3.20 */
  return gdk_device_manager_get_client_pointer (device_manager);
}

G_GNUC_END_IGNORE_DEPRECATIONS
