
#ifndef GREETER_DEPRECATED_H
#define GREETER_DEPRECATED_H

#include <gtk/gtk.h>

gint                  greeter_screen_get_number                   (GdkScreen        *screen);

gint                  greeter_screen_get_width                    (GdkScreen        *screen);

gint                  greeter_screen_get_width_mm                 (GdkScreen        *screen);

gint                  greeter_screen_get_height                   (GdkScreen        *screen);

gint                  greeter_screen_get_height_mm                (GdkScreen        *screen);

gint                  greeter_screen_get_n_monitors               (GdkScreen        *screen);

gchar *               greeter_screen_get_monitor_plug_name        (GdkScreen        *screen,
                                                                   gint              monitor_num);

void                  greeter_screen_get_monitor_geometry         (GdkScreen        *screen,
                                                                   gint              monitor_num,
                                                                   GdkRectangle     *dest);

gint                  greeter_screen_get_primary_monitor          (GdkScreen        *screen);

void                  greeter_widget_reparent                     (GtkWidget        *widget,
                                                                   GtkWidget        *new_parent);

GdkDeviceManager *    greeter_display_get_device_manager          (GdkDisplay       *display);

GdkDevice *           greeter_device_manager_get_client_pointer   (GdkDeviceManager *device_manager);

#endif //GREETER_DEPRECATED_H
