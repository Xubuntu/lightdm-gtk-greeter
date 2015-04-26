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

#define GREETER_BACKGROUND_DEFAULT          "*"

typedef enum
{
    TRANSITION_TYPE_NONE,
    TRANSITION_TYPE_EASE_IN_OUT,
    TRANSITION_TYPE_LINEAR,
    TRANSITION_TYPE_FALLBACK
} TransitionType;

typedef enum
{
    TRANSITION_EFFECT_NONE,
} TransitionEffect;

typedef struct _GreeterBackground           GreeterBackground;
typedef struct _GreeterBackgroundClass      GreeterBackgroundClass;

GType greeter_background_get_type(void) G_GNUC_CONST;

GreeterBackground* greeter_background_new           (GtkWidget* child);
void greeter_background_set_active_monitor_config   (GreeterBackground* background,
                                                     const gchar* value);
void greeter_background_set_monitor_config          (GreeterBackground* background,
                                                     const gchar* name,
                                                     const gchar* bg,
                                                     gint user_bg,
                                                     gint laptop,
                                                     gint transition_duration,
                                                     TransitionType transition_type);
void greeter_background_remove_monitor_config       (GreeterBackground* background,
                                                     const gchar* name);
gchar** greeter_background_get_configured_monitors  (GreeterBackground* background);
void greeter_background_connect                     (GreeterBackground* background,
                                                     GdkScreen* screen);
void greeter_background_set_custom_background       (GreeterBackground* background,
                                                     const gchar* path);
void greeter_background_save_xroot                  (GreeterBackground* background);
const GdkRectangle* greeter_background_get_active_monitor_geometry(GreeterBackground* background);
void greeter_background_add_accel_group             (GreeterBackground* background,
                                                     GtkAccelGroup* group);

G_END_DECLS

#endif // GREETER_BACKGROUND_H
