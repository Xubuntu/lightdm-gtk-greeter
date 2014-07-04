#ifndef GREETER_BACKGROUND_H
#define GREETER_BACKGROUND_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GREETER_BACKGROUND_TYPE             (greeter_background_get_type())
#define GREETER_BACKGROUND(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GREETER_BACKGROUND_TYPE, GreeterBackground))
#define GREETER_BACKGROUND_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GREETER_BACKGROUND_TYPE, GreeterBackgroundClass))
#define GREETER_IS_BACKGROUND(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GREETER_BACKGROUND_TYPE))
#define GREETER_IS_BACKGROUND_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GREETER_BACKGROUND_TYPE))

typedef struct _GreeterBackground           GreeterBackground;
typedef struct _GreeterBackgroundClass      GreeterBackgroundClass;
typedef struct _GreeterBackgroundPrivate    GreeterBackgroundPrivate;

struct _GreeterBackground
{
	GObject parent_instance;
	struct _GreeterBackgroundPrivate* priv;
};

struct _GreeterBackgroundClass
{
	GObjectClass parent_class;
};

GType greeter_background_get_type(void) G_GNUC_CONST;

GreeterBackground* greeter_background_new           (void);
void greeter_background_set_background_config       (GreeterBackground* background,
                                                     const gchar* value);
void greeter_background_set_custom_background_config(GreeterBackground* background,
                                                     const gchar* value);
void greeter_background_set_active_monitor_config   (GreeterBackground* background,
                                                     const gchar* value);
void greeter_background_set_lid_monitor_config      (GreeterBackground* background,
                                                     const gchar* value);
void greeter_background_connect                     (GreeterBackground* background,
                                                     GdkScreen* screen);
void greeter_background_add_subwindow               (GreeterBackground* background,
                                                     GtkWindow* window);
void greeter_background_remove_subwindow            (GreeterBackground* background,
                                                     GtkWindow* window);
void greeter_background_set_custom_background       (GreeterBackground* background,
                                                     const gchar* path);
void greeter_background_save_xroot                  (GreeterBackground* background);
const GdkRectangle* greeter_background_get_active_monitor_geometry(GreeterBackground* background);

G_END_DECLS

#endif // GREETER_BACKGROUND_H
