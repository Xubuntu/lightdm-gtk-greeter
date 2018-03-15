/*
 * Copyright (C) 2014 - 2018, Sean Davis <smd.seandavis@gmail.com>
 * Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
 * Copyright (C) 2015, Robert Ancell <robert.ancell@canonical.com>
 * Copyright (C) 2015, Simon Steinbeiß <ochosi@shimmerproject.org>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <math.h>
#include <cairo-xlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>
#include <X11/Xatom.h>

#include "greeterbackground.h"
#include "greeterdeprecated.h"

typedef enum
{
    /* Broken/uninitialized configuration */
    BACKGROUND_TYPE_INVALID,
    /* Do not use this monitor */
    BACKGROUND_TYPE_SKIP,
    /* Do not override window background */
    BACKGROUND_TYPE_DEFAULT,
    /* Solid color */
    BACKGROUND_TYPE_COLOR,
    /* Path to image and scaling mode */
    BACKGROUND_TYPE_IMAGE
    /* Maybe other types (e.g. gradient) */
} BackgroundType;

static const gchar* BACKGROUND_TYPE_SKIP_VALUE = "#skip";
static const gchar* BACKGROUND_TYPE_DEFAULT_VALUE = "#default";

typedef enum
{
    /* It is not really useful, used for debugging */
    SCALING_MODE_SOURCE,
    /* Default mode for values without mode prefix */
    SCALING_MODE_ZOOMED,
    SCALING_MODE_STRETCHED
} ScalingMode;

static const gchar* SCALING_MODE_PREFIXES[] = {"#source:", "#zoomed:", "#stretched:", NULL};

typedef gdouble (*TransitionFunction)(gdouble x);
typedef void (*TransitionDraw)(gconstpointer monitor, cairo_t* cr);

/* Background configuration (parsed from background=... option).
   Used to fill <Background> */
typedef struct
{
    BackgroundType type;
    union
    {
        GdkRGBA color;
        struct
        {
            gchar *path;
            ScalingMode mode;
        } image;
    } options;
} BackgroundConfig;

/* Transition configuration
   Used as part of <MonitorConfig> and <Monitor> */
typedef struct
{
    /* Transition duration, in ms */
    glong duration;
    TransitionFunction func;
    /* Function to draw monitor background */
    TransitionDraw draw;
} TransitionConfig;

/* Store monitor configuration */
typedef struct
{
    BackgroundConfig bg;
    gboolean user_bg;
    gboolean laptop;

    TransitionConfig transition;
} MonitorConfig;

/* Actual drawing information attached to monitor.
 * Used to separate configured monitor background and user background. */
typedef struct
{
    gint ref_count;
    BackgroundType type;
    union
    {
        GdkPixbuf* image;
        GdkRGBA color;
    } options;
} Background;

typedef struct
{
    GreeterBackground* object;
    gint number;
    gchar* name;
    GdkRectangle geometry;
    GtkWindow* window;
    gulong window_draw_handler_id;

    /* Configured background */
    Background* background_configured;
    /* Current monitor background: &background_configured or &background_custom
     * Monitors with type = BACKGROUND_TYPE_SKIP have background = NULL */
    Background* background;

    struct
    {
        TransitionConfig config;

        /* Old background, stage == 0.0 */
        Background* from;
        /* New background, stage == 1.0 */
        Background* to;

        guint timer_id;
        gint64 started;
        /* Current stage */
        gdouble stage;
    } transition;
} Monitor;

static const Monitor INVALID_MONITOR_STRUCT = {0};

struct _GreeterBackground
{
	GObject parent_instance;
	struct _GreeterBackgroundPrivate* priv;
};

struct _GreeterBackgroundClass
{
	GObjectClass parent_class;
};

typedef struct _GreeterBackgroundPrivate GreeterBackgroundPrivate;

struct _GreeterBackgroundPrivate
{
    GdkScreen* screen;
    gulong screen_monitors_changed_handler_id;

    /* Widget to display on active monitor */
    GtkWidget* child;
    /* List of groups <GtkAccelGroup*> for greeter screens windows */
    GSList* accel_groups;

    /* Mapping monitor name <gchar*> to its config <MonitorConfig*> */
    GHashTable* configs;

    /* Default config for unlisted monitors */
    MonitorConfig* default_config;

	/* Array of configured monitors for current screen */
    Monitor* monitors;
    gsize monitors_size;

	/* Name => <Monitor*>, "Number" => <Monitor*> */
    GHashTable* monitors_map;

    GList* active_monitors_config;
    const Monitor* active_monitor;
    gboolean active_monitor_change_in_progress;
    gint64 active_monitor_change_last_timestamp;

    /* List of monitors <Monitor*> with user-background=true*/
    GSList* customized_monitors;
    /* Initialized by set_custom_background() */
    BackgroundConfig customized_background;

    /* List of monitors <Monitor*> with laptop=true */
    GSList* laptop_monitors;
    /* DBus proxy to catch lid state changing */
    GDBusProxy* laptop_upower_proxy;
    /* Cached lid state */
    gboolean laptop_lid_closed;

    /* Use cursor position to determinate current active monitor (dynamic) */
    gboolean follow_cursor;
    /* Use cursor position to determinate initial active monitor */
    gboolean follow_cursor_to_init;
};

enum
{
    BACKGROUND_SIGNAL_ACTIVE_MONITOR_CHANGED,
    BACKGROUND_SIGNAL_LAST
};

static guint background_signals[BACKGROUND_SIGNAL_LAST] = {0};

static const gchar* DBUS_UPOWER_NAME                = "org.freedesktop.UPower";
static const gchar* DBUS_UPOWER_PATH                = "/org/freedesktop/UPower";
static const gchar* DBUS_UPOWER_INTERFACE           = "org.freedesktop.UPower";
static const gchar* DBUS_UPOWER_PROP_LID_IS_PRESENT = "LidIsPresent";
static const gchar* DBUS_UPOWER_PROP_LID_IS_CLOSED  = "LidIsClosed";

static const gchar* ACTIVE_MONITOR_CURSOR_TAG       = "#cursor";

G_DEFINE_TYPE_WITH_PRIVATE(GreeterBackground, greeter_background, G_TYPE_OBJECT);

void greeter_background_disconnect                  (GreeterBackground* background);
static gboolean greeter_background_find_monitor_data(GreeterBackground* background,
                                                     GHashTable* table,
                                                     const Monitor* monitor,
                                                     gpointer* data);
static void greeter_background_set_active_monitor   (GreeterBackground* background,
                                                     const Monitor* active);
static void greeter_background_get_cursor_position  (GreeterBackground* background,
                                                     gint* x, gint* y);
static void greeter_background_set_cursor_position  (GreeterBackground* background,
                                                     gint x, gint y);
static void greeter_background_try_init_dbus        (GreeterBackground* background);
static void greeter_background_stop_dbus            (GreeterBackground* background);
static gboolean greeter_background_monitor_enabled  (GreeterBackground* background,
                                                     const Monitor* monitor);
static void greeter_background_dbus_changed_cb      (GDBusProxy* proxy,
                                                     GVariant* changed_properties,
                                                     const gchar* const* invalidated_properties,
                                                     GreeterBackground* background);
static void greeter_background_monitors_changed_cb  (GdkScreen* screen,
                                                     GreeterBackground* background);
static void greeter_background_child_destroyed_cb   (GtkWidget* child,
                                                     GreeterBackground* background);

/* struct BackgroundConfig */
static gboolean background_config_initialize        (BackgroundConfig* config,
                                                     const gchar* value);
static void background_config_finalize              (BackgroundConfig* config);
static void background_config_copy                  (const BackgroundConfig* source,
                                                     BackgroundConfig* dest);

/* struct MonitorConfig */
static void monitor_config_free                     (MonitorConfig* config);
/* Copy source config to dest, return dest. Allocate memory if dest == NULL. */
static MonitorConfig* monitor_config_copy           (const MonitorConfig* source,
                                                     MonitorConfig* dest);

/* struct Background */
static Background* background_new                   (const BackgroundConfig* config,
                                                     const Monitor* monitor,
                                                     GHashTable* images_cache);
static Background* background_ref                   (Background* bg);
static void background_unref                        (Background** bg);
static void background_finalize                     (Background* bg);

/* struct Monitor */
static void monitor_finalize                        (Monitor* info);
static void monitor_set_background                  (Monitor* monitor,
                                                     Background* background);
static void monitor_start_transition                (Monitor* monitor,
                                                     Background* from,
                                                     Background* to);
static void monitor_stop_transition                 (Monitor* monitor);
static gboolean monitor_transition_cb               (GtkWidget *widget,
                                                     GdkFrameClock* frame_clock,
                                                     Monitor* monitor);
static void monitor_transition_draw_alpha           (const Monitor* monitor,
                                                     cairo_t* cr);
static void monitor_draw_background                 (const Monitor* monitor,
                                                     const Background* background,
                                                     cairo_t* cr);
static gboolean monitor_window_draw_cb              (GtkWidget* widget,
                                                     cairo_t* cr,
                                                     const Monitor* monitor);
static gboolean monitor_window_enter_notify_cb      (GtkWidget* widget,
                                                     GdkEventCrossing* event,
                                                     const Monitor* monitor);

static GdkPixbuf* scale_image_file                  (const gchar* path,
                                                     ScalingMode mode,
                                                     gint width, gint height,
                                                     GHashTable* cache);
static GdkPixbuf* scale_image                       (GdkPixbuf* source,
                                                     ScalingMode mode,
                                                     gint width, gint height);
static cairo_surface_t* create_root_surface         (GdkScreen* screen);
static void set_root_pixmap_id                      (GdkScreen* screen,
                                                     Display* display,
                                                     Pixmap xpixmap);
static void set_surface_as_root                     (GdkScreen* screen,
                                                     cairo_surface_t* surface);
static gdouble transition_func_linear               (gdouble x);
static gdouble transition_func_ease_in_out          (gdouble x);

/* Implemented in lightdm-gtk-greeter.c */
gpointer greeter_save_focus(GtkWidget* widget);
void greeter_restore_focus(const gpointer saved_data);

static const MonitorConfig DEFAULT_MONITOR_CONFIG =
{
    .bg =
    {
        .type = BACKGROUND_TYPE_COLOR,
        .options =
        {
            .color = {.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}
        }
    },
    .user_bg = TRUE,
    .laptop = FALSE,
    .transition =
    {
        .duration = 500,
        .func = transition_func_ease_in_out,
        .draw = (TransitionDraw)monitor_transition_draw_alpha
    }
};

/* Implementation */

static void
greeter_background_class_init(GreeterBackgroundClass* klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    background_signals[BACKGROUND_SIGNAL_ACTIVE_MONITOR_CHANGED] =
                            g_signal_new("active-monitor-changed",
                                         G_TYPE_FROM_CLASS(gobject_class),
                                         G_SIGNAL_RUN_FIRST,
                                         0, /* class_offset */
                                         NULL /* accumulator */, NULL /* accu_data */,
                                         g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);
}

static void
greeter_background_init(GreeterBackground* self)
{
    GreeterBackgroundPrivate* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, GREETER_BACKGROUND_TYPE, GreeterBackgroundPrivate);
    self->priv = priv;

    priv->screen = NULL;
    priv->screen_monitors_changed_handler_id = 0;
    priv->accel_groups = NULL;

    priv->configs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)monitor_config_free);
    priv->default_config = monitor_config_copy(&DEFAULT_MONITOR_CONFIG, NULL);

    priv->monitors = NULL;
    priv->monitors_size = 0;
    priv->monitors_map = NULL;

    priv->customized_monitors = NULL;
    priv->customized_background.type = BACKGROUND_TYPE_INVALID;
    priv->active_monitors_config = NULL;
    priv->active_monitor = NULL;

    priv->laptop_monitors = NULL;
    priv->laptop_upower_proxy = NULL;
    priv->laptop_lid_closed = FALSE;
}

GreeterBackground*
greeter_background_new(GtkWidget* child)
{
    GreeterBackground *background;

    g_return_val_if_fail(child != NULL, NULL);

    background = GREETER_BACKGROUND(g_object_new(greeter_background_get_type(), NULL));
    background->priv->child = child;
    g_signal_connect(background->priv->child, "destroy", G_CALLBACK(greeter_background_child_destroyed_cb), background);
	return background;
}

void
greeter_background_set_active_monitor_config(GreeterBackground* background,
                                             const gchar* value)
{
    GreeterBackgroundPrivate     *priv;
    gchar                       **iter;
    gchar                       **values;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));

    priv = background->priv;

    g_list_free_full(priv->active_monitors_config, g_free);
    priv->active_monitors_config = NULL;
    priv->active_monitor_change_in_progress = FALSE;
    priv->active_monitor_change_last_timestamp = 0;

    priv->follow_cursor = FALSE;
    priv->follow_cursor_to_init = FALSE;

    if (!value || !*value)
        return;

    values = g_strsplit(value, ";", -1);

    for(iter = values; *iter; ++iter)
    {
        const gchar* tag = *iter;
        if (g_strcmp0(tag, ACTIVE_MONITOR_CURSOR_TAG) == 0)
        {
            priv->follow_cursor = TRUE;
            priv->follow_cursor_to_init = (priv->active_monitors_config == NULL);
        }
        else
            priv->active_monitors_config = g_list_prepend(priv->active_monitors_config, g_strdup(tag));
    }
    g_strfreev(values);

    priv->active_monitors_config = g_list_reverse(priv->active_monitors_config);
}

void
greeter_background_set_monitor_config(GreeterBackground* background,
                                      const gchar* name,
                                      const gchar* bg,
                                      gint user_bg,
                                      gint laptop,
                                      gint transition_duration,
                                      TransitionType transition_type)
{
    GreeterBackgroundPrivate *priv;
    MonitorConfig            *config;
    const MonitorConfig      *FALLBACK;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));

    priv = background->priv;

    config = g_new0(MonitorConfig, 1);

    FALLBACK = (g_strcmp0(name, GREETER_BACKGROUND_DEFAULT) == 0) ? &DEFAULT_MONITOR_CONFIG : priv->default_config;

    if(!background_config_initialize(&config->bg, bg))
        background_config_copy(&FALLBACK->bg, &config->bg);
    config->user_bg = user_bg >= 0 ? user_bg : FALLBACK->user_bg;
    config->laptop = laptop >= 0 ? laptop : FALLBACK->laptop;
    config->transition.duration = transition_duration >= 0 ? transition_duration : FALLBACK->transition.duration;
    config->transition.draw = FALLBACK->transition.draw;

    switch(transition_type)
    {
        case TRANSITION_TYPE_NONE:
            config->transition.func = NULL; break;
        case TRANSITION_TYPE_LINEAR:
            config->transition.func = transition_func_linear; break;
        case TRANSITION_TYPE_EASE_IN_OUT:
            config->transition.func = transition_func_ease_in_out; break;
        case TRANSITION_TYPE_FALLBACK:
        default:
            config->transition.func = FALLBACK->transition.func;
    }

    if(FALLBACK == priv->default_config)
        g_hash_table_insert(priv->configs, g_strdup(name), config);
    else
    {
        if(priv->default_config)
            monitor_config_free(priv->default_config);
        priv->default_config = config;
    }
}

void
greeter_background_remove_monitor_config(GreeterBackground* background,
                                         const gchar* name)
{
    g_return_if_fail(GREETER_IS_BACKGROUND(background));
    g_hash_table_remove(background->priv->configs, name);
}

gchar**
greeter_background_get_configured_monitors(GreeterBackground* background)
{
    GreeterBackgroundPrivate  *priv;
    GHashTableIter             iter;
    gpointer                   key;
    gchar                    **names;
    gint                       n;

    g_return_val_if_fail(GREETER_IS_BACKGROUND(background), NULL);

    priv = background->priv;

    n = g_hash_table_size(priv->configs);
    names = g_new(gchar*, n + 1);
    names[n--] = NULL;

    g_hash_table_iter_init(&iter, priv->configs);
    while(g_hash_table_iter_next(&iter, &key, NULL))
        names[n--] = g_strdup(key);

    return names;
}

void
greeter_background_connect(GreeterBackground* background,
                           GdkScreen* screen)
{
    GreeterBackgroundPrivate *priv;
    GHashTable               *images_cache;
    Monitor                  *first_not_skipped_monitor = NULL;
    cairo_region_t           *screen_region;
    gpointer                  saved_focus = NULL;
    guint                     i;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));
    g_return_if_fail(GDK_IS_SCREEN(screen));

    g_debug("[Background] Connecting to screen: %p (%dx%dpx, %dx%dmm)", screen,
            greeter_screen_get_width(screen), greeter_screen_get_height(screen),
            greeter_screen_get_width_mm(screen), greeter_screen_get_height_mm(screen));

    priv = background->priv;
    saved_focus = NULL;
    if(priv->screen)
    {
        if (priv->active_monitor)
            saved_focus = greeter_save_focus(priv->child);
        greeter_background_disconnect(background);
    }

    priv->screen = screen;
    priv->monitors_size = greeter_screen_get_n_monitors(screen);
    priv->monitors = g_new0(Monitor, priv->monitors_size);
    priv->monitors_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    g_debug("[Background] Monitors found: %" G_GSIZE_FORMAT, priv->monitors_size);

    /* Used to track situation when all monitors marked as "#skip" */
    first_not_skipped_monitor = NULL;

    images_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    screen_region = cairo_region_create();

    for(i = 0; i < priv->monitors_size; ++i)
    {
        const MonitorConfig* config;
        Monitor* monitor = &priv->monitors[i];
        const gchar* printable_name;
        gchar* window_name;
        GSList* item;
        Background* bg = NULL;

        monitor->object = background;
        monitor->name = g_strdup(greeter_screen_get_monitor_plug_name(screen, i));
        monitor->number = i;

        printable_name = monitor->name ? monitor->name : "<unknown>";

        greeter_screen_get_monitor_geometry(screen, i, &monitor->geometry);

        g_debug("[Background] Monitor: %s #%d (%dx%d at %dx%d)%s", printable_name, i,
                monitor->geometry.width, monitor->geometry.height,
                monitor->geometry.x, monitor->geometry.y,
                ((gint)i == greeter_screen_get_primary_monitor(screen)) ? " primary" : "");

        if(!greeter_background_find_monitor_data(background, priv->configs, monitor, (gpointer*)&config))
        {
            g_debug("[Background] No configuration options for monitor %s #%d, using default", printable_name, i);
            config = priv->default_config;
        }

        /* Force last skipped monitor to be active monitor, if there is no other choice */
        if(config->bg.type == BACKGROUND_TYPE_SKIP)
        {
            if(i < priv->monitors_size - 1 || first_not_skipped_monitor)
            {
                g_debug("[Background] Skipping monitor %s #%d", printable_name, i);
                continue;
            }
            g_debug("[Background] Monitor %s #%d can not be skipped, using default configuration for it", printable_name, i);
            if(priv->default_config->bg.type != BACKGROUND_TYPE_SKIP)
                config = priv->default_config;
            else
                config = &DEFAULT_MONITOR_CONFIG;
        }

        /* Simple check to skip fully overlapped monitors.
           Actually, it's can track only monitors in "mirrors" mode. Nothing more. */
        if(cairo_region_contains_rectangle(screen_region, &monitor->geometry) == CAIRO_REGION_OVERLAP_IN)
        {
            g_debug("[Background] Skipping monitor %s #%d, its area is already used by other monitors", printable_name, i);
            continue;
        }
        cairo_region_union_rectangle(screen_region, &monitor->geometry);

        if(!first_not_skipped_monitor)
            first_not_skipped_monitor = monitor;

        monitor->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
        gtk_window_set_type_hint(monitor->window, GDK_WINDOW_TYPE_HINT_DESKTOP);
        gtk_window_set_keep_below(monitor->window, TRUE);
        gtk_window_set_resizable(monitor->window, FALSE);
        gtk_widget_set_app_paintable(GTK_WIDGET(monitor->window), TRUE);
        gtk_window_set_screen(monitor->window, screen);
        gtk_widget_set_size_request(GTK_WIDGET(monitor->window), monitor->geometry.width, monitor->geometry.height);
        gtk_window_move(monitor->window, monitor->geometry.x, monitor->geometry.y);
        gtk_widget_show(GTK_WIDGET(monitor->window));
        monitor->window_draw_handler_id = g_signal_connect(G_OBJECT(monitor->window), "draw",
                                                           G_CALLBACK(monitor_window_draw_cb),
                                                           monitor);

        window_name = monitor->name ? g_strdup_printf("monitor-%s", monitor->name) : g_strdup_printf("monitor-%d", i);
        gtk_widget_set_name(GTK_WIDGET(monitor->window), window_name);
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(monitor->window)), "lightdm-gtk-greeter");
        g_free(window_name);

        for(item = priv->accel_groups; item != NULL; item = g_slist_next(item))
            gtk_window_add_accel_group(monitor->window, item->data);

        g_signal_connect(G_OBJECT(monitor->window), "enter-notify-event",
                         G_CALLBACK(monitor_window_enter_notify_cb), monitor);

        if(config->user_bg)
            priv->customized_monitors = g_slist_prepend(priv->customized_monitors, monitor);

        if(config->laptop)
            priv->laptop_monitors = g_slist_prepend(priv->laptop_monitors, monitor);

        if(config->transition.duration && config->transition.func)
            monitor->transition.config = config->transition;

        monitor->background_configured = background_new(&config->bg, monitor, images_cache);
        if(!monitor->background_configured)
            monitor->background_configured = background_new(&DEFAULT_MONITOR_CONFIG.bg, monitor, images_cache);

        if(config->user_bg && priv->customized_background.type != BACKGROUND_TYPE_INVALID)
            bg = background_new(&priv->customized_background, monitor, images_cache);

        if(bg)
        {
            monitor_set_background(monitor, bg);
            background_unref(&bg);
        }
		else
            monitor_set_background(monitor, monitor->background_configured);

        if(monitor->name)
            g_hash_table_insert(priv->monitors_map, g_strdup(monitor->name), monitor);
        g_hash_table_insert(priv->monitors_map, g_strdup_printf("%d", i), monitor);
    }
    g_hash_table_unref(images_cache);

    if(priv->laptop_monitors && !priv->laptop_upower_proxy)
        greeter_background_try_init_dbus(background);
    else if(!priv->laptop_monitors)
        greeter_background_stop_dbus(background);

    if(priv->follow_cursor_to_init)
    {
        gint x, y;
        greeter_background_get_cursor_position(background, &x, &y);
        for(i = 0; i < priv->monitors_size && !priv->active_monitor; ++i)
        {
            const Monitor* monitor = &priv->monitors[i];
            if(greeter_background_monitor_enabled(background, monitor) &&
               x >= monitor->geometry.x && x < monitor->geometry.x + monitor->geometry.width &&
               y >= monitor->geometry.y && y < monitor->geometry.y + monitor->geometry.height)
            {
                g_debug("[Background] Pointer position will be used to set active monitor: %dx%d", x, y);
                greeter_background_set_active_monitor(background, monitor);
                break;
            }
        }
    }

    if(!priv->active_monitor)
        greeter_background_set_active_monitor(background, NULL);

    if(saved_focus)
    {
        greeter_restore_focus(saved_focus);
        g_free(saved_focus);
    }

    priv->screen_monitors_changed_handler_id = g_signal_connect(G_OBJECT(screen), "monitors-changed",
                                                                G_CALLBACK(greeter_background_monitors_changed_cb),
                                                                background);
}

void
greeter_background_disconnect(GreeterBackground* background)
{
    GreeterBackgroundPrivate *priv;
    guint                      i;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));

    priv = background->priv;

    if(priv->screen_monitors_changed_handler_id)
        g_signal_handler_disconnect(priv->screen, priv->screen_monitors_changed_handler_id);
    priv->screen_monitors_changed_handler_id = 0;
    priv->screen = NULL;
    priv->active_monitor = NULL;

    for(i = 0; i < priv->monitors_size; ++i)
        monitor_finalize(&priv->monitors[i]);
    g_free(priv->monitors);
    priv->monitors = NULL;
    priv->monitors_size = 0;

    g_hash_table_unref(priv->monitors_map);
    priv->monitors_map = NULL;
    g_slist_free(priv->customized_monitors);
    priv->customized_monitors = NULL;
    g_slist_free(priv->laptop_monitors);
    priv->laptop_monitors = NULL;
}

/* Moved to separate function to simplify needless and unnecessary syntax expansion in future (regex) */
static gboolean
greeter_background_find_monitor_data(GreeterBackground* background,
                                     GHashTable* table,
                                     const Monitor* monitor,
                                     gpointer* data)
{
    if(!monitor->name || !g_hash_table_lookup_extended(table, monitor->name, NULL, data))
    {
        gchar* num_str = g_strdup_printf("%d", monitor->number);
        gboolean result = g_hash_table_lookup_extended(table, num_str, NULL, data);
        g_free(num_str);
        return result;
    }
    return TRUE;
}

static void
greeter_background_set_active_monitor(GreeterBackground* background,
                                      const Monitor* active)
{
    GreeterBackgroundPrivate* priv = background->priv;
    gint x, y;
    gint64 timestamp;

    if (priv->active_monitor_change_in_progress)
        return;

    /* Prevents infinite signal emmission between two monitors (LP: #1410406, #1509780)
     * There are some rare scenarios when using multiple monitors that cause the greeter
     * to switch back and forth between the monitors indefinitely. By comparing the
     * timestamp at this precision (1/10th of a second), this should no longer be
     * possible.
     */
    if (active != NULL)
    {
        timestamp = floor(g_get_monotonic_time () * 0.00001);
        if (timestamp == priv->active_monitor_change_last_timestamp)
        {
            g_debug("[Background] Preventing infinite monitor loop");
            return;
        }
    }

    priv->active_monitor_change_in_progress = TRUE;
    priv->active_monitor_change_last_timestamp = floor(g_get_monotonic_time () * 0.00001);

    if(active && !active->background)
    {
        if(priv->active_monitor)
            goto active_monitor_change_complete;
        active = NULL;
    }

    /* Auto */
    if(!active)
    {
        /* Normal way: at least one configured active monitor is not disabled */
        GList* iter;
        for(iter = priv->active_monitors_config; iter && !active; iter = g_list_next(iter))
        {
            const Monitor* monitor = g_hash_table_lookup(priv->monitors_map, iter->data);
            if(monitor && monitor->background && greeter_background_monitor_enabled(background, monitor))
                active = monitor;
            if(active)
                g_debug("[Background] Active monitor is not specified, using first enabled monitor from 'active-monitor' list");
        }

        /* All monitors listed in active-monitor-config are disabled (or option is empty) */

        /* Using primary monitor */
        if(!active)
        {
            gint num = greeter_screen_get_primary_monitor(priv->screen);
            if ((guint)num >= priv->monitors_size)
                goto active_monitor_change_complete;

            active = &priv->monitors[num];
            if(!active->background || !greeter_background_monitor_enabled(background, active))
                active = NULL;
            if(active)
                g_debug("[Background] Active monitor is not specified, using primary monitor");
        }

        /* Fallback: first enabled and/or not skipped monitor (screen always have one) */
        if(!active)
        {
            guint i;
            const Monitor* first_not_skipped = NULL;
            for(i = 0; i < priv->monitors_size && !active; ++i)
            {
                const Monitor* monitor = &priv->monitors[i];
                if(!monitor->background)
                    continue;
                if(greeter_background_monitor_enabled(background, monitor))
                    active = monitor;
                if(!first_not_skipped)
                    first_not_skipped = active;
            }
            if(!active)
                active = first_not_skipped;
            if(active)
                g_debug("[Background] Active monitor is not specified, using first enabled monitor");
        }

        if(!active && priv->laptop_monitors)
        {
            active = priv->laptop_monitors->data;
            g_debug("[Background] Active monitor is not specified, using laptop monitor");
        }
    }

    if(!active)
    {
        if(priv->active_monitor)
            g_warning("[Background] Active monitor is not specified, failed to identify. Active monitor stays the same: %s #%d",
                      priv->active_monitor->name, priv->active_monitor->number);
        else
            g_warning("[Background] Active monitor is not specified, failed to identify. Active monitor stays the same: <not defined>");
        goto active_monitor_change_complete;
    }

    if(active == priv->active_monitor)
        goto active_monitor_change_complete;

    priv->active_monitor = active;

    if (priv->active_monitor == NULL)
        goto active_monitor_change_complete;

    if(priv->child)
    {
        GtkWidget* old_parent = gtk_widget_get_parent(priv->child);
        gpointer focus = greeter_save_focus(priv->child);

        if(old_parent)
            greeter_widget_reparent(priv->child, GTK_WIDGET(active->window));
        else
            gtk_container_add(GTK_CONTAINER(active->window), priv->child);

        gtk_window_present(active->window);
        greeter_restore_focus(focus);
        g_free(focus);
    }
    else
        g_warning("[Background] Child widget is destroyed or not defined");

    g_debug("[Background] Active monitor changed to: %s #%d", active->name, active->number);
    g_signal_emit(background, background_signals[BACKGROUND_SIGNAL_ACTIVE_MONITOR_CHANGED], 0);

    greeter_background_get_cursor_position(background, &x, &y);
    /* Do not center cursor if it is already inside active monitor */
    if(x < active->geometry.x || x >= active->geometry.x + active->geometry.width ||
       y < active->geometry.y || y >= active->geometry.y + active->geometry.height)
        greeter_background_set_cursor_position(background,
                                               active->geometry.x + active->geometry.width/2,
                                               active->geometry.y + active->geometry.height/2);

active_monitor_change_complete:
    priv->active_monitor_change_in_progress = FALSE;
    return;
}

static void
greeter_background_get_cursor_position(GreeterBackground* background,
                                       gint* x, gint* y)
{
    GreeterBackgroundPrivate* priv = background->priv;

    GdkDisplay* display = gdk_screen_get_display(priv->screen);
    GdkDeviceManager* device_manager = greeter_display_get_device_manager(display);
    GdkDevice* device = greeter_device_manager_get_client_pointer(device_manager);
    gdk_device_get_position(device, NULL, x, y);
}

static void
greeter_background_set_cursor_position(GreeterBackground* background,
                                       gint x, gint y)
{
    GreeterBackgroundPrivate* priv = background->priv;

    GdkDisplay* display = gdk_screen_get_display(priv->screen);
    GdkDeviceManager* device_manager = greeter_display_get_device_manager(display);
    gdk_device_warp(greeter_device_manager_get_client_pointer(device_manager), priv->screen, x, y);
}

static void
greeter_background_try_init_dbus(GreeterBackground* background)
{
    GreeterBackgroundPrivate *priv;
    GError                   *error = NULL;
    GVariant                 *variant;
    gboolean                  lid_present;

    g_debug("[Background] Creating DBus proxy");

    priv = background->priv;

    if(priv->laptop_upower_proxy)
        greeter_background_stop_dbus(background);

    priv->laptop_upower_proxy = g_dbus_proxy_new_for_bus_sync(
                                            G_BUS_TYPE_SYSTEM,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            NULL,   /* interface info */
                                            DBUS_UPOWER_NAME,
                                            DBUS_UPOWER_PATH,
                                            DBUS_UPOWER_INTERFACE,
                                            NULL,   /* cancellable */
                                            &error);
    if(!priv->laptop_upower_proxy)
    {
        if(error)
            g_warning("[Background] Failed to create dbus proxy: %s", error->message);
        g_clear_error(&error);
        return;
    }

    variant = g_dbus_proxy_get_cached_property(priv->laptop_upower_proxy, DBUS_UPOWER_PROP_LID_IS_PRESENT);
    lid_present = g_variant_get_boolean(variant);
    g_variant_unref(variant);

    g_debug("[Background] UPower.%s property value: %d", DBUS_UPOWER_PROP_LID_IS_PRESENT, lid_present);

    if(!lid_present)
        greeter_background_stop_dbus(background);
    else
    {
        variant = g_dbus_proxy_get_cached_property(priv->laptop_upower_proxy, DBUS_UPOWER_PROP_LID_IS_CLOSED);
        priv->laptop_lid_closed = g_variant_get_boolean(variant);
        g_variant_unref(variant);

        g_signal_connect(priv->laptop_upower_proxy, "g-properties-changed",
                         G_CALLBACK(greeter_background_dbus_changed_cb), background);
    }
}

static void
greeter_background_stop_dbus(GreeterBackground* background)
{
    if(!background->priv->laptop_upower_proxy)
        return;
    g_clear_object(&background->priv->laptop_upower_proxy);
}

static gboolean
greeter_background_monitor_enabled(GreeterBackground* background,
                                   const Monitor* monitor)
{
    GreeterBackgroundPrivate* priv = background->priv;

    if(priv->laptop_upower_proxy && g_slist_find(priv->laptop_monitors, monitor))
        return !priv->laptop_lid_closed;
    return TRUE;
}

static void
greeter_background_dbus_changed_cb(GDBusProxy* proxy,
                                   GVariant* changed_properties,
                                   const gchar* const* invalidated_properties,
                                   GreeterBackground* background)
{
    GreeterBackgroundPrivate *priv;
    GVariant                 *variant;
    gboolean                  new_state;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));

    priv = background->priv;

    variant = g_dbus_proxy_get_cached_property(priv->laptop_upower_proxy, DBUS_UPOWER_PROP_LID_IS_CLOSED);
    new_state = g_variant_get_boolean(variant);
    g_variant_unref(variant);

    if(new_state == priv->laptop_lid_closed)
        return;

    priv->laptop_lid_closed = new_state;
    g_debug("[Background] UPower: lid state changed to '%s'", priv->laptop_lid_closed ? "closed" : "opened");

    if(priv->laptop_monitors)
    {
        if(priv->laptop_lid_closed)
        {
            if(g_slist_find(priv->laptop_monitors, priv->active_monitor))
                greeter_background_set_active_monitor(background, NULL);
        }
        else
        {
            if(!priv->follow_cursor)
                greeter_background_set_active_monitor(background, NULL);
        }
    }
}

static void
greeter_background_monitors_changed_cb(GdkScreen* screen,
                                       GreeterBackground* background)
{
    g_return_if_fail(GREETER_IS_BACKGROUND(background));
    greeter_background_connect(background, screen);
}

static void
greeter_background_child_destroyed_cb(GtkWidget* child,
                                      GreeterBackground* background)
{
    background->priv->child = NULL;
}

void
greeter_background_set_custom_background(GreeterBackground* background,
                                         const gchar* value)
{
    GreeterBackgroundPrivate *priv;
    GHashTable               *images_cache = NULL;
    GSList                   *iter;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));

    priv = background->priv;

    if(priv->customized_background.type != BACKGROUND_TYPE_INVALID)
        background_config_finalize(&priv->customized_background);
    background_config_initialize(&priv->customized_background, value);

    if(!priv->customized_monitors)
        return;

    if(priv->customized_background.type == BACKGROUND_TYPE_IMAGE)
        images_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);

    for(iter = priv->customized_monitors; iter; iter = g_slist_next(iter))
    {
        Monitor *monitor = iter->data;

        /* Old background_custom (if used) will be unrefed in monitor_set_background() */
        Background* bg = NULL;
        if(priv->customized_background.type != BACKGROUND_TYPE_INVALID)
            bg = background_new(&priv->customized_background, monitor, images_cache);
        if(bg)
        {
            monitor_set_background(monitor, bg);
            background_unref(&bg);
        }
        else
            monitor_set_background(monitor, monitor->background_configured);
    }

    if(images_cache)
        g_hash_table_unref(images_cache);
}

void
greeter_background_save_xroot(GreeterBackground* background)
{
    GreeterBackgroundPrivate *priv;
    cairo_surface_t          *surface;
    cairo_t                  *cr;
    gsize                     i;

    const GdkRGBA             ROOT_COLOR = {1.0, 1.0, 1.0, 1.0};

    g_return_if_fail(GREETER_IS_BACKGROUND(background));

    priv = background->priv;
    surface = create_root_surface(priv->screen);
    cr = cairo_create(surface);

    gdk_cairo_set_source_rgba(cr, &ROOT_COLOR);
    cairo_paint(cr);

    for(i = 0; i < priv->monitors_size; ++i)
    {
        const Monitor* monitor = &priv->monitors[i];
        if(!monitor->background)
            continue;
        cairo_save(cr);
        cairo_translate(cr, monitor->geometry.x, monitor->geometry.y);
        monitor_draw_background(monitor, monitor->background, cr);
        cairo_restore(cr);
    }
    set_surface_as_root(priv->screen, surface);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

const GdkRectangle*
greeter_background_get_active_monitor_geometry(GreeterBackground* background)
{
    GreeterBackgroundPrivate *priv;

    g_return_val_if_fail(GREETER_IS_BACKGROUND(background), NULL);

    priv = background->priv;

    return priv->active_monitor ? &priv->active_monitor->geometry : NULL;
}

void
greeter_background_add_accel_group(GreeterBackground* background,
                                   GtkAccelGroup* group)
{
    GreeterBackgroundPrivate *priv;

    g_return_if_fail(GREETER_IS_BACKGROUND(background));
    g_return_if_fail(group != NULL);

    priv = background->priv;

    if(priv->monitors)
    {
        guint i;
        for(i = 0; i < priv->monitors_size; ++i)
            if(priv->monitors[i].window)
                gtk_window_add_accel_group(priv->monitors[i].window, group);
    }

    priv->accel_groups = g_slist_append(priv->accel_groups, group);
}

static gboolean
background_config_initialize(BackgroundConfig* config,
                             const gchar* value)
{
    config->type = BACKGROUND_TYPE_INVALID;
    if(!value || strlen(value) == 0)
        return FALSE;
    if(g_strcmp0(value, BACKGROUND_TYPE_SKIP_VALUE) == 0)
        config->type = BACKGROUND_TYPE_SKIP;
    else if(g_strcmp0(value, BACKGROUND_TYPE_DEFAULT_VALUE) == 0)
        config->type = BACKGROUND_TYPE_DEFAULT;
    else if(gdk_rgba_parse(&config->options.color, value))
        config->type = BACKGROUND_TYPE_COLOR;
    else
    {
        const gchar** prefix = SCALING_MODE_PREFIXES;
        while(*prefix && !g_str_has_prefix(value, *prefix))
            ++prefix;

        if(*prefix)
        {
            config->options.image.mode = (ScalingMode)(prefix - SCALING_MODE_PREFIXES);
            value += strlen(*prefix);
        }
        else
            config->options.image.mode = SCALING_MODE_ZOOMED;

        config->options.image.path = g_strdup(value);
        config->type = BACKGROUND_TYPE_IMAGE;
    }
    return TRUE;
}

static void
background_config_finalize(BackgroundConfig* config)
{
    switch(config->type)
    {
        case BACKGROUND_TYPE_IMAGE:
            g_free(config->options.image.path);
            break;
        case BACKGROUND_TYPE_COLOR:
        case BACKGROUND_TYPE_DEFAULT:
        case BACKGROUND_TYPE_SKIP:
            break;
        case BACKGROUND_TYPE_INVALID:
        default:
            g_return_if_reached();
    }

    config->type = BACKGROUND_TYPE_INVALID;
}

static void
background_config_copy(const BackgroundConfig* source,
                       BackgroundConfig* dest)
{
    *dest = *source;

    switch(dest->type)
    {
        case BACKGROUND_TYPE_IMAGE:
            dest->options.image.path = g_strdup(source->options.image.path);
            break;
        case BACKGROUND_TYPE_COLOR:
        case BACKGROUND_TYPE_DEFAULT:
        case BACKGROUND_TYPE_SKIP:
            break;
        case BACKGROUND_TYPE_INVALID:
        default:
            g_return_if_reached();
    }
}

static void
monitor_config_free(MonitorConfig* config)
{
    background_config_finalize(&config->bg);
    g_free(config);
}

static MonitorConfig* monitor_config_copy(const MonitorConfig* source,
                                          MonitorConfig* dest)
{
    if(!dest)
        dest = g_new0(MonitorConfig, 1);
    background_config_copy(&source->bg, &dest->bg);
    dest->user_bg = source->user_bg;
    dest->laptop = source->laptop;
    dest->transition = source->transition;
    return dest;
}

static Background*
background_new(const BackgroundConfig* config,
               const Monitor* monitor,
               GHashTable* images_cache)
{
    Background *result;
    Background  bg = {0};

    switch(config->type)
    {
        case BACKGROUND_TYPE_IMAGE:
            bg.options.image = scale_image_file(config->options.image.path, config->options.image.mode,
                                                monitor->geometry.width, monitor->geometry.height,
                                                images_cache);
            if(!bg.options.image)
            {
                g_warning("[Background] Failed to read wallpaper: %s", config->options.image.path);
                return NULL;
            }
            break;
        case BACKGROUND_TYPE_COLOR:
            bg.options.color = config->options.color;
            break;
        case BACKGROUND_TYPE_DEFAULT:
            break;
        case BACKGROUND_TYPE_SKIP:
        case BACKGROUND_TYPE_INVALID:
        default:
            g_return_val_if_reached(NULL);
    }

    bg.type = config->type;
    bg.ref_count = 1;

    result = g_new(Background, 1);
    *result = bg;
    return result;
}

static Background*
background_ref(Background* bg)
{
    bg->ref_count++;
    return bg;
}

static void
background_unref(Background** bg)
{
    if(!*bg)
        return;
    (*bg)->ref_count--;
    if((*bg)->ref_count == 0)
    {
        background_finalize(*bg);
        *bg = NULL;
    }
}

static void
background_finalize(Background* bg)
{
    switch(bg->type)
    {
        case BACKGROUND_TYPE_IMAGE:
            g_clear_object(&bg->options.image);
            break;
        case BACKGROUND_TYPE_COLOR:
        case BACKGROUND_TYPE_DEFAULT:
            break;
        case BACKGROUND_TYPE_SKIP:
        case BACKGROUND_TYPE_INVALID:
        default:
            g_return_if_reached();
    }

    bg->type = BACKGROUND_TYPE_INVALID;
}

static void
monitor_set_background(Monitor* monitor,
                       Background* background)
{
    if(monitor->background == background)
        return;
    monitor_stop_transition(monitor);

    switch(background->type)
    {
        case BACKGROUND_TYPE_IMAGE:
        case BACKGROUND_TYPE_COLOR:
            gtk_widget_set_app_paintable(GTK_WIDGET(monitor->window), TRUE);
            if(monitor->transition.config.duration > 0 && monitor->background &&
               monitor->background->type != BACKGROUND_TYPE_DEFAULT)
                monitor_start_transition(monitor, monitor->background, background);
            break;
        case BACKGROUND_TYPE_DEFAULT:
            gtk_widget_set_app_paintable(GTK_WIDGET(monitor->window), FALSE);
            break;
        case BACKGROUND_TYPE_SKIP:
        case BACKGROUND_TYPE_INVALID:
        default:
            g_return_if_reached();
    }

    background_unref(&monitor->background);
    monitor->background = background_ref(background);
    gtk_widget_queue_draw(GTK_WIDGET(monitor->window));
}

static void
monitor_start_transition(Monitor* monitor,
                         Background* from,
                         Background* to)
{
    monitor_stop_transition(monitor);

    monitor->transition.from = background_ref(from);
    monitor->transition.to = background_ref(to);

    monitor->transition.started = g_get_monotonic_time();
    monitor->transition.timer_id = gtk_widget_add_tick_callback(GTK_WIDGET(monitor->window),
                                                                (GtkTickCallback)monitor_transition_cb,
                                                                monitor,
                                                                NULL);
    monitor->transition.stage = 0;
}

static void
monitor_stop_transition(Monitor* monitor)
{
    if(!monitor->transition.timer_id)
        return;
    gtk_widget_remove_tick_callback(GTK_WIDGET(monitor->window), monitor->transition.timer_id);
    monitor->transition.timer_id = 0;
    monitor->transition.started = 0;
    monitor->transition.stage = 0;
    background_unref(&monitor->transition.to);
    background_unref(&monitor->transition.from);
}

static gboolean
monitor_transition_cb(GtkWidget *widget,
                      GdkFrameClock* frame_clock,
                      Monitor* monitor)
{
    gint64  span;
    gdouble x;

    if(!monitor->transition.timer_id)
        return G_SOURCE_REMOVE;

    span = g_get_monotonic_time() - monitor->transition.started;
    x = CLAMP(span/monitor->transition.config.duration/1000.0, 0.0, 1.0);
    monitor->transition.stage = monitor->transition.config.func(x);

    if(x >= 1.0)
        monitor_stop_transition(monitor);

    gtk_widget_queue_draw(GTK_WIDGET(monitor->window));
    return x >= 1.0 ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static void
monitor_transition_draw_alpha(const Monitor* monitor,
                              cairo_t* cr)
{
    cairo_pattern_t* alpha_pattern;

    monitor_draw_background(monitor, monitor->transition.from, cr);

    cairo_push_group(cr);
    monitor_draw_background(monitor, monitor->transition.to, cr);
    cairo_pop_group_to_source(cr);

    alpha_pattern = cairo_pattern_create_rgba(0.0, 0.0, 0.0, monitor->transition.stage);
    cairo_mask(cr, alpha_pattern);
    cairo_pattern_destroy(alpha_pattern);
}

static void
monitor_finalize(Monitor* monitor)
{
    if(monitor->transition.config.duration)
    {
        monitor_stop_transition(monitor);
        if(monitor->transition.timer_id)
            g_source_remove(monitor->transition.timer_id);
        monitor->transition.config.duration = 0;
    }

    if(monitor->window_draw_handler_id)
        g_signal_handler_disconnect(monitor->window, monitor->window_draw_handler_id);

    background_unref(&monitor->background_configured);
    background_unref(&monitor->background);

    if(monitor->window)
    {
        GtkWidget* child = gtk_bin_get_child(GTK_BIN(monitor->window));
        if(child) /* remove greeter widget to avoid "destroy" signal */
            gtk_container_remove(GTK_CONTAINER(monitor->window), child);
        gtk_widget_destroy(GTK_WIDGET(monitor->window));
    }

    g_free(monitor->name);

    *monitor = INVALID_MONITOR_STRUCT;
}

static void
monitor_draw_background(const Monitor* monitor,
                        const Background* background,
                        cairo_t* cr)
{
    g_return_if_fail(monitor != NULL);
    g_return_if_fail(background != NULL);

    switch(background->type)
    {
        case BACKGROUND_TYPE_IMAGE:
            if(background->options.image)
            {
                gdk_cairo_set_source_pixbuf(cr, background->options.image, 0, 0);
                cairo_paint(cr);
            }
            break;
        case BACKGROUND_TYPE_COLOR:
            cairo_rectangle(cr, 0, 0, monitor->geometry.width, monitor->geometry.height);
            gdk_cairo_set_source_rgba(cr, &background->options.color);
            cairo_fill(cr);
            break;
        case BACKGROUND_TYPE_DEFAULT:
            break;
        case BACKGROUND_TYPE_SKIP:
        case BACKGROUND_TYPE_INVALID:
        default:
            g_return_if_reached();
    }
}

static gboolean
monitor_window_draw_cb(GtkWidget* widget,
                       cairo_t* cr,
                       const Monitor* monitor)
{
    if(!monitor->background)
        return FALSE;

    if(monitor->transition.started)
        monitor->transition.config.draw(monitor, cr);
    else
        monitor_draw_background(monitor, monitor->background, cr);

    return FALSE;
}

static gboolean
monitor_window_enter_notify_cb(GtkWidget* widget,
                               GdkEventCrossing* event,
                               const Monitor* monitor)
{
    if(monitor->object->priv->active_monitor == monitor)
    {
        GdkWindow *gdkwindow = gtk_widget_get_window (widget);
        Window window = GDK_WINDOW_XID (gdkwindow);
        Display *display = GDK_WINDOW_XDISPLAY (gdkwindow);
        XEvent ev = {0};

        static Atom wm_protocols = None;
        static Atom wm_take_focus = None;

        if (!wm_protocols)
            wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
        if (!wm_take_focus)
            wm_take_focus = XInternAtom(display, "WM_TAKE_FOCUS", False);

        ev.xclient.type = ClientMessage;
        ev.xclient.window = window;
        ev.xclient.message_type = wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm_take_focus;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, window, False, 0L, &ev);
    }
    else if(monitor->object->priv->follow_cursor && greeter_background_monitor_enabled(monitor->object, monitor))
        greeter_background_set_active_monitor(monitor->object, monitor);
    return FALSE;
}

static GdkPixbuf*
scale_image_file(const gchar* path,
                 ScalingMode mode,
                 gint width, gint height,
                 GHashTable* cache)
{
    gchar* key = NULL;
    GdkPixbuf* pixbuf = NULL;

    if(cache)
    {
        key = g_strdup_printf("%s\n%d %dx%d", path, mode, width, height);
        if(g_hash_table_lookup_extended(cache, key, NULL, (gpointer*)&pixbuf))
        {
            g_free(key);
            return GDK_PIXBUF(g_object_ref(pixbuf));
        }
    }

    if(!cache || !g_hash_table_lookup_extended(cache, path, NULL, (gpointer*)&pixbuf))
    {
        GError *error = NULL;
        pixbuf = gdk_pixbuf_new_from_file(path, &error);
        if(error)
        {
            g_warning("[Background] Failed to load background: %s", error->message);
            g_clear_error(&error);
        }
        else if(cache)
            g_hash_table_insert(cache, g_strdup(path), g_object_ref(pixbuf));
    }
    else
        pixbuf = g_object_ref(pixbuf);

    if(pixbuf)
    {
        GdkPixbuf* scaled = scale_image(pixbuf, mode, width, height);
        if(cache)
            g_hash_table_insert(cache, g_strdup(key), g_object_ref(scaled));
        g_object_unref(pixbuf);
        pixbuf = scaled;
    }

    g_free(key);

    return pixbuf;
}

static GdkPixbuf*
scale_image(GdkPixbuf* source,
            ScalingMode mode,
            gint width, gint height)
{
    if(mode == SCALING_MODE_ZOOMED)
    {
        gint offset_x = 0;
        gint offset_y = 0;
        gint p_width = gdk_pixbuf_get_width(source);
        gint p_height = gdk_pixbuf_get_height(source);
        gdouble scale_x = (gdouble)width / p_width;
        gdouble scale_y = (gdouble)height / p_height;
        GdkPixbuf *pixbuf;

        if(scale_x < scale_y)
        {
            scale_x = scale_y;
            offset_x = (width - (p_width * scale_x)) / 2;
        }
        else
        {
            scale_y = scale_x;
            offset_y = (height - (p_height * scale_y)) / 2;
        }

        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE,
                                           gdk_pixbuf_get_bits_per_sample (source),
                                           width, height);
        gdk_pixbuf_composite(source, pixbuf, 0, 0, width, height,
                             offset_x, offset_y, scale_x, scale_y, GDK_INTERP_BILINEAR, 0xFF);
        return pixbuf;
    }
    else if(mode == SCALING_MODE_STRETCHED)
    {
        return gdk_pixbuf_scale_simple(source, width, height, GDK_INTERP_BILINEAR);
    }
    return GDK_PIXBUF(g_object_ref(source));
}

/* The following code for setting a RetainPermanent background pixmap was taken
   originally from Gnome, with some fixes from MATE. see:
   https://github.com/mate-desktop/mate-desktop/blob/master/libmate-desktop/mate-bg.c */
static cairo_surface_t*
create_root_surface(GdkScreen* screen)
{
    gint number, width, height;
    Display *display;
    Pixmap pixmap;
    cairo_surface_t *surface;

    number = greeter_screen_get_number (screen);
    width = greeter_screen_get_width (screen);
    height = greeter_screen_get_height (screen);

    /* Open a new connection so with Retain Permanent so the pixmap remains when the greeter quits */
    greeter_gdk_flush ();
    display = XOpenDisplay (gdk_display_get_name (gdk_screen_get_display (screen)));
    if (!display)
    {
        g_warning("[Background] Failed to create root pixmap");
        return NULL;
    }

    XSetCloseDownMode (display, RetainPermanent);
    pixmap = XCreatePixmap (display, RootWindow (display, number), width, height, DefaultDepth (display, number));
    XCloseDisplay (display);

    /* Convert into a Cairo surface */
    surface = cairo_xlib_surface_create (GDK_SCREEN_XDISPLAY (screen),
                                         pixmap,
                                         GDK_VISUAL_XVISUAL (gdk_screen_get_system_visual (screen)),
                                         width, height);

    return surface;
}

/* Sets the "ESETROOT_PMAP_ID" property to later be used to free the pixmap */
static void
set_root_pixmap_id(GdkScreen* screen,
                   Display* display,
                   Pixmap xpixmap)
{

    Window xroot = RootWindow (display, greeter_screen_get_number (screen));
    const char *atom_names[] = {"_XROOTPMAP_ID", "ESETROOT_PMAP_ID"};
    Atom atoms[G_N_ELEMENTS(atom_names)] = {0};

    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char *data_root, *data_esetroot;

    /* Get atoms for both properties in an array, only if they exist.
     * This method is to avoid multiple round-trips to Xserver
     */
    if (XInternAtoms (display, (char **)atom_names, G_N_ELEMENTS(atom_names), True, atoms) &&
        atoms[0] != None && atoms[1] != None)
    {

        XGetWindowProperty (display, xroot, atoms[0], 0L, 1L, False, AnyPropertyType,
                            &type, &format, &nitems, &after, &data_root);
        if (data_root && type == XA_PIXMAP && format == 32 && nitems == 1)
        {
            XGetWindowProperty (display, xroot, atoms[1], 0L, 1L, False, AnyPropertyType,
                                &type, &format, &nitems, &after, &data_esetroot);
            if (data_esetroot && type == XA_PIXMAP && format == 32 && nitems == 1)
            {
                Pixmap xrootpmap = *((Pixmap *) data_root);
                Pixmap esetrootpmap = *((Pixmap *) data_esetroot);
                XFree (data_root);
                XFree (data_esetroot);

                greeter_error_trap_push ();
                if (xrootpmap && xrootpmap == esetrootpmap) {
                    XKillClient (display, xrootpmap);
                }
                if (esetrootpmap && esetrootpmap != xrootpmap) {
                    XKillClient (display, esetrootpmap);
                }

                XSync (display, False);
                greeter_error_trap_pop_ignored ();
            }
        }
    }

    /* Get atoms for both properties in an array, create them if needed.
     * This method is to avoid multiple round-trips to Xserver
     */
    if (!XInternAtoms (display, (char **)atom_names, G_N_ELEMENTS(atom_names), False, atoms) ||
        atoms[0] == None || atoms[1] == None) {
        g_warning("[Background] Could not create atoms needed to set root pixmap id/properties.\n");
        return;
    }

    /* Set new _XROOTMAP_ID and ESETROOT_PMAP_ID properties */
    XChangeProperty (display, xroot, atoms[0], XA_PIXMAP, 32,
                     PropModeReplace, (unsigned char *) &xpixmap, 1);

    XChangeProperty (display, xroot, atoms[1], XA_PIXMAP, 32,
                     PropModeReplace, (unsigned char *) &xpixmap, 1);
}

/**
* set_surface_as_root:
* @screen: the #GdkScreen to change root background on
* @surface: the #cairo_surface_t to set root background from.
* Must be an xlib surface backing a pixmap.
*
* Set the root pixmap, and properties pointing to it. We
* do this atomically with a server grab to make sure that
* we won't leak the pixmap if somebody else it setting
* it at the same time. (This assumes that they follow the
* same conventions we do). @surface should come from a call
* to create_root_surface().
**/
static void
set_surface_as_root(GdkScreen* screen,
                    cairo_surface_t* surface)
{
    Display *display;
    Pixmap   pixmap_id;
    Window   xroot;

    g_return_if_fail(cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_XLIB);

    /* Desktop background pixmap should be created from dummy X client since most
     * applications will try to kill it with XKillClient later when changing pixmap
     */
    display = GDK_DISPLAY_XDISPLAY (gdk_screen_get_display (screen));
    pixmap_id = cairo_xlib_surface_get_drawable (surface);
    xroot = RootWindow (display, greeter_screen_get_number(screen));

    XGrabServer (display);

    XSetWindowBackgroundPixmap (display, xroot, pixmap_id);
    set_root_pixmap_id (screen, display, pixmap_id);
    XClearWindow (display, xroot);

    XFlush (display);
    XUngrabServer (display);
}

static gdouble
transition_func_linear(gdouble x)
{
    return x;
}

static gdouble
transition_func_ease_in_out(gdouble x)
{
    return (1 - cos(M_PI*x))/2;
}
