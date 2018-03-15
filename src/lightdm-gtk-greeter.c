/*
 * Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
 * Copyright (C) 2011, Gunnar Hjalmarsson <ubuntu@gunnar.cc>
 * Copyright (C) 2012 - 2013, Lionel Le Folgoc <mrpouit@ubuntu.com>
 * Copyright (C) 2012, Julien Lavergne <gilir@ubuntu.com>
 * Copyright (C) 2013 - 2015, Simon Steinbeiß <ochosi@shimmerproject.org>
 * Copyright (C) 2013 - 2018, Sean Davis <smd.seandavis@gmail.com>
 * Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <glib-unix.h>

#include <locale.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <glib.h>
#include <gtk/gtkx.h>
#include <glib/gslist.h>

#ifdef HAVE_LIBINDICATOR
#include <libindicator/indicator-object.h>
#ifdef HAVE_LIBINDICATOR_NG
#include <libindicator/indicator-ng.h>
#endif
#endif

#ifdef HAVE_LIBIDO
/* Some indicators need ido library */
#include <libido/libido.h>
#endif

#ifdef HAVE_LIBXKLAVIER
#include <libxklavier/xklavier.h>
#endif

#include <lightdm.h>

#include "src/greeterconfiguration.h"
#include "src/greetermenubar.h"
#include "src/greeterbackground.h"
#include "src/lightdm-gtk-greeter-ui.h"
#include "src/lightdm-gtk-greeter-css-fallback.h"
#include "src/lightdm-gtk-greeter-css-application.h"


static LightDMGreeter *greeter;

/* List of spawned processes */
static GSList *pids_to_close = NULL;
static GPid spawn_argv_pid (gchar **argv, GSpawnFlags flags, gint *pfd, GError **perror);
#if defined(AT_SPI_COMMAND) || defined(INDICATOR_SERVICES_COMMAND)
static GPid spawn_line_pid (const gchar *line, GSpawnFlags flags, GError **perror);
#endif
static void close_pid (GPid pid, gboolean remove);
static void sigterm_cb (gpointer user_data);

/* Screen window */
static GtkOverlay   *screen_overlay;
static GtkWidget    *screen_overlay_child;

/* Login window */
static GtkWidget    *login_window;
static GtkImage     *user_image;
static GtkComboBox  *user_combo;
static GtkEntry     *username_entry, *password_entry;
static GtkLabel     *message_label;
static GtkInfoBar   *info_bar;
static GtkButton    *cancel_button, *login_button;

/* Panel */
static GtkWidget    *panel_window, *menubar;
static GtkWidget    *power_menuitem, *session_menuitem, *language_menuitem, *a11y_menuitem,
                    *layout_menuitem, *clock_menuitem, *host_menuitem;
static GtkWidget    *suspend_menuitem, *hibernate_menuitem, *restart_menuitem, *shutdown_menuitem;
static GtkWidget    *contrast_menuitem, *font_menuitem, *keyboard_menuitem, *reader_menuitem;
static GtkWidget    *clock_label, *session_badge;
static GtkMenu      *session_menu, *language_menu, *layout_menu;

/* Power window */
static GtkWidget    *power_window;
static GtkButton    *power_ok_button, *power_cancel_button;
static GtkLabel     *power_title, *power_text;
static GtkImage     *power_icon;

static const gchar *DEFAULT_LAYOUT[] = {"~spacer", "~spacer", "~host", "~spacer",
                                        "~session", "~a11y", "~clock", "~power", NULL};

static const gchar *POWER_WINDOW_DATA_LOOP = "power-window-loop";           /* <GMainLoop*> */
static const gchar *POWER_WINDOW_DATA_RESPONSE = "power-window-response";   /* <GtkResponseType> */

static gboolean show_power_prompt (const gchar *action, const gchar* icon, const gchar* title, const gchar* message);
void power_button_clicked_cb (GtkButton *button, gpointer user_data);
gboolean power_window_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);

/* Handling window position */
typedef struct
{
    gint value;
    /* +0 and -0 */
    gint sign;
    /* interpret 'value' as percentage of screen width/height */
    gboolean percentage;
    /* -1: left/top, 0: center, +1: right,bottom */
    gint anchor;
} DimensionPosition;

typedef struct
{
    DimensionPosition x, y;
    /* Flag to use width and height fileds */
    gboolean use_size;
    DimensionPosition width, height;
} WindowPosition;

static WindowPosition* str_to_position (const gchar *str, const WindowPosition *default_value);
/* Function translate user defined coordinates to absolute value */
static gint get_absolute_position (const DimensionPosition *p, gint screen, gint window);
gboolean screen_overlay_get_child_position_cb (GtkWidget *overlay, GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);

static const gchar *WINDOW_DATA_POSITION = "window-position"; /* <WindowPosition*> */

/* Some default positions */
static const WindowPosition WINDOW_POS_CENTER   = {.x = { 50, +1, TRUE,   0}, .y = { 50, +1, TRUE,   0}, .use_size = FALSE};
static const WindowPosition KEYBOARD_POSITION   = {.x = { 50, +1, TRUE,   0}, .y = {  0, -1, FALSE, +1}, .use_size = TRUE,
                                                   .width = {50, 0, TRUE, 0}, .height = {25, 0, TRUE, 0}};

/* Clock */
static gchar *clock_format;
static gboolean clock_timeout_thread (void);

/* Message label */
static gboolean message_label_is_empty (void);
static void set_message_label (LightDMMessageType type, const gchar *text);

/* User image */
static GdkPixbuf *default_user_pixbuf = NULL;
static gchar *default_user_icon = NULL;
static void set_user_image (const gchar *username);

/* External command (keyboard, reader) */
typedef struct
{
    const gchar *name;

    gchar **argv;
    gint argc;

    GPid pid;
    GtkWidget *menu_item;
    GtkWidget *widget;
} MenuCommand;

static MenuCommand *menu_command_parse (const gchar *name, const gchar *value, GtkWidget *menu_item);
static MenuCommand *menu_command_parse_extended (const gchar *name,
                                                 const gchar *value, GtkWidget *menu_item,
                                                 const gchar *xid_app, const gchar *xid_arg);
static gboolean menu_command_run (MenuCommand *command);
static gboolean menu_command_stop (MenuCommand *command);
static void menu_command_terminated_cb (GPid pid, gint status, MenuCommand *command);

static MenuCommand *a11y_keyboard_command;
static MenuCommand *a11y_reader_command;

static void a11y_menuitem_toggled_cb (GtkCheckMenuItem *item, const gchar* name);

/* Session */
static gchar *current_session;
static gboolean is_valid_session (GList* items, const gchar* session);
static gchar* get_session (void);
static void set_session (const gchar *session);
void session_selected_cb (GtkMenuItem *menuitem, gpointer user_data);

/* Sesion language */
static gchar *current_language;
static gchar* get_language (void);
static void set_language (const gchar *language);
void language_selected_cb (GtkMenuItem *menuitem, gpointer user_data);

/* Screensaver values */
static int timeout, interval, prefer_blanking, allow_exposures;

/* Handling monitors backgrounds */
static const gint USER_BACKGROUND_DELAY = 250;
static GreeterBackground *greeter_background;

/* Authentication state */
static gboolean cancelling = FALSE, prompted = FALSE;
static gboolean prompt_active = FALSE, password_prompted = FALSE;

/* Pending questions */
static GSList *pending_questions = NULL;

typedef struct
{
    gboolean is_prompt;
    union
    {
        LightDMMessageType message;
        LightDMPromptType prompt;
    } type;
    gchar *text;
} PAMConversationMessage;

static void pam_message_finalize (PAMConversationMessage *message);
static void process_prompts (LightDMGreeter *greeter);
static void show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type);

/* Panel and indicators */

typedef enum
{
    PANEL_ITEM_INDICATOR,
    PANEL_ITEM_SPACER,
    PANEL_ITEM_SEPARATOR,
    PANEL_ITEM_TEXT
} GreeterPanelItemType;

static const gchar *PANEL_ITEM_STYLE = "panel-item";
static const gchar *PANEL_ITEM_STYLE_HOVERED = "panel-item-hovered";
static const gchar *PANEL_ITEM_STYLES[] =
{
    "panel-item-indicator",
    "panel-item-spacer",
    "panel-item-separator",
    "panel-item-text"
};

static const gchar *PANEL_ITEM_DATA_INDEX = "panel-item-data-index";

#ifdef HAVE_LIBINDICATOR
static const gchar *INDICATOR_ITEM_DATA_OBJECT = "indicator-item-data-object";  /* <IndicatorObject*> */
static const gchar *INDICATOR_ITEM_DATA_ENTRY  = "indicator-item-data-entry";   /* <IndicatorObjectEntry*> */
static const gchar *INDICATOR_ITEM_DATA_BOX    = "indicator-item-data-box";     /* <GtkBox*> */
static const gchar *INDICATOR_DATA_MENUITEMS   = "indicator-data-menuitems";    /* <GHashTable*> */
#endif

static const gchar *LANGUAGE_DATA_CODE = "language-code";   /* <gchar*> e.g. "de_DE.UTF-8" */
static const gchar *SESSION_DATA_KEY = "session-key";       /* <gchar*> session name */

/* Layout indicator */
#ifdef HAVE_LIBXKLAVIER
static XklEngine *xkl_engine;
static const gchar *LAYOUT_DATA_GROUP = "layout-group";     /* <gchar*> */
#else
static const gchar *LAYOUT_DATA_NAME = "layout-name";       /* <gchar*> */
#endif
static const gchar *LAYOUT_DATA_LABEL = "layout-label";     /* <gchar*> e.g. "English (US)" */

static gboolean panel_item_enter_notify_cb (GtkWidget *widget, GdkEvent *event, gpointer enter);
static void panel_add_item (GtkWidget *widget, gint index, GreeterPanelItemType item_type);
static gboolean menu_item_accel_closure_cb (GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval, GdkModifierType modifier, gpointer data);
/* Maybe unnecessary (in future) trick to enable accelerators for hidden/detached menu items */
static void reassign_menu_item_accel (GtkWidget *item);

static void init_indicators (void);

static void layout_selected_cb (GtkCheckMenuItem *menuitem, gpointer user_data);
static void update_layouts_menu (void);
static void update_layouts_menu_state (void);
#ifdef HAVE_LIBXKLAVIER
static void xkl_state_changed_cb (XklEngine *engine, XklEngineStateChange change, gint group, gboolean restore, gpointer user_data);
static void xkl_config_changed_cb (XklEngine *engine, gpointer user_data);
static GdkFilterReturn xkl_xevent_filter (GdkXEvent *xev, GdkEvent *event, gpointer  data);
#endif

/* a11y indicator */
static gchar *default_font_name,
             *default_theme_name,
             *default_icon_theme_name,
             *default_cursor_theme_name;
void a11y_font_cb (GtkCheckMenuItem *item);
void a11y_contrast_cb (GtkCheckMenuItem *item);
void a11y_keyboard_cb (GtkCheckMenuItem *item, gpointer user_data);
void a11y_reader_cb (GtkCheckMenuItem *item, gpointer user_data);

/* Power indicator */
static void power_menu_cb (GtkWidget *menuitem, gpointer userdata);
void suspend_cb (GtkWidget *widget, LightDMGreeter *greeter);
void hibernate_cb (GtkWidget *widget, LightDMGreeter *greeter);
void restart_cb (GtkWidget *widget, LightDMGreeter *greeter);
void shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter);

static void read_monitor_configuration (const gchar *group, const gchar *name);

struct SavedFocusData
{
    GtkWidget *widget;
    gint editable_pos;
};

gpointer greeter_save_focus(GtkWidget* widget);
void greeter_restore_focus(const gpointer saved_data);


static void
read_monitor_configuration (const gchar *group, const gchar *name)
{
    gchar *background;

    g_debug ("[Configuration] Monitor configuration found: '%s'", name);

    background = config_get_string (group, CONFIG_KEY_BACKGROUND, NULL);
    greeter_background_set_monitor_config (greeter_background, name, background,
                                           config_get_bool (group, CONFIG_KEY_USER_BACKGROUND, -1),
                                           config_get_bool (group, CONFIG_KEY_LAPTOP, -1),
                                           config_get_int (group, CONFIG_KEY_T_DURATION, -1),
                                           config_get_enum (group, CONFIG_KEY_T_TYPE,
                                                TRANSITION_TYPE_FALLBACK,
                                                "none",         TRANSITION_TYPE_NONE,
                                                "linear",       TRANSITION_TYPE_LINEAR,
                                                "ease-in-out",  TRANSITION_TYPE_EASE_IN_OUT, NULL));
    g_free (background);
}

gpointer
greeter_save_focus(GtkWidget* widget)
{
    GtkWidget             *window;
    struct SavedFocusData *data;

    window = gtk_widget_get_toplevel(widget);
    if (!GTK_IS_WINDOW (window))
        return NULL;

    data = g_new0 (struct SavedFocusData, 1);
    data->widget = gtk_window_get_focus (GTK_WINDOW (window));
    data->editable_pos = GTK_IS_EDITABLE(data->widget) ? gtk_editable_get_position (GTK_EDITABLE (data->widget)) : -1;

    return data;
}

void
greeter_restore_focus(const gpointer saved_data)
{
    struct SavedFocusData *data = saved_data;

    if (!saved_data || !GTK_IS_WIDGET (data->widget))
        return;

    gtk_widget_grab_focus (data->widget);
    if (GTK_IS_EDITABLE(data->widget) && data->editable_pos > -1)
        gtk_editable_set_position(GTK_EDITABLE(data->widget), data->editable_pos);
}

static void
infobar_revealed_cb_710888 (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    gtk_widget_set_visible (GTK_WIDGET (info_bar), !message_label_is_empty ());
}

/* Terminating */

static GPid
spawn_argv_pid (gchar **argv, GSpawnFlags flags, gint *pfd, GError **perror)
{
    GPid pid = 0;
    GError *error = NULL;
    gboolean spawned = FALSE;

    if (pfd)
        spawned = g_spawn_async_with_pipes (NULL, argv, NULL, flags, NULL, NULL, &pid, NULL, pfd, NULL, perror);
    else
        spawned = g_spawn_async (NULL, argv, NULL, flags, NULL, NULL, &pid, &error);

    if (spawned)
    {
        pids_to_close = g_slist_prepend (pids_to_close, GINT_TO_POINTER (pid));
        g_debug ("[PIDs] Command executed (#%d): %s", pid, argv[0]);
    }
    else if (perror)
    {
        *perror = error;
    }
    else
    {
        g_warning ("[PIDs] Failed to execute command: %s", argv[0]);
        g_clear_error (&error);
    }

    return pid;
}

#if defined(AT_SPI_COMMAND) || defined(INDICATOR_SERVICES_COMMAND)
static GPid
spawn_line_pid (const gchar *line, GSpawnFlags flags, GError **perror)
{
    gint argc = 0;
    gchar **argv = NULL;
    GError *error = NULL;

    if (g_shell_parse_argv (line, &argc, &argv, &error))
    {
        GPid pid = spawn_argv_pid (argv, flags, NULL, perror);
        g_strfreev (argv);
        return pid;
    }
    else if (!perror && error)
    {
        g_warning ("[PIDs] Failed to parse command line: %s, %s", error->message, line);
        g_clear_error (&error);
    }
    else if (error)
        *perror = error;

    return 0;
}
#endif

static void
close_pid (GPid pid, gboolean remove)
{
    if (!pid)
        return;

    if (remove)
        pids_to_close = g_slist_remove (pids_to_close, GINT_TO_POINTER (pid));

    if (kill (pid, SIGTERM) == 0)
        g_debug ("[PIDs] Process terminated: #%d", pid);
    else
        g_warning ("[PIDs] Failed to terminate process #%d: %s", pid, g_strerror (errno));

    g_spawn_close_pid (pid);
}

static void
sigterm_cb (gpointer user_data)
{
    gboolean is_callback = GPOINTER_TO_INT (user_data);

    if (is_callback)
        g_debug ("SIGTERM received");

    if (pids_to_close)
    {
        g_slist_foreach (pids_to_close, (GFunc)close_pid, GINT_TO_POINTER (FALSE));
        g_slist_free (pids_to_close);
        pids_to_close = NULL;
    }

    if (is_callback)
    {
        gtk_main_quit ();
        #ifdef KILL_ON_SIGTERM
        /* LP: #1445461 */
        g_debug ("Killing greeter with exit()...");
        exit (EXIT_SUCCESS);
        #endif
    }
}

/* Power window */

static gboolean
show_power_prompt (const gchar *action, const gchar* icon, const gchar* title, const gchar* message)
{
    GMainLoop        *loop;
    GtkWidget        *focused;

    gboolean          session_enabled;
    gboolean          language_enabled;
    gboolean          power_present = FALSE;

    gsize             length;
    guint             i;
    gint              logged_in_users = 0;

    gchar           **names;
    GList            *items;
    GList            *item;

    gchar            *new_message = NULL;
    gchar            *dialog_name = NULL;
    gchar            *button_name = NULL;
    GtkResponseType   response;

    /* Do anything only when power indicator is present */
    names = config_get_string_list (NULL, CONFIG_KEY_INDICATORS, NULL);
    if (!names)
       names = (gchar**)DEFAULT_LAYOUT;
    length = g_strv_length (names);
    for (i = 0; i < length; ++i)
    {
        if (g_strcmp0 (names[i], "~power") == 0)
        {   /* Power menu is present */
            power_present = TRUE;
            break;
        }
    }
    if (names && names != (gchar**)DEFAULT_LAYOUT)
        g_strfreev (names);

    if (power_present != TRUE) {
        return FALSE;
    }

    /* Check if there are still users logged in, count them and if so, display a warning */
    items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
    for (item = items; item; item = item->next)
    {
        LightDMUser *user = item->data;
        if (lightdm_user_get_logged_in (user))
            logged_in_users++;
    }

    if (logged_in_users > 0)
    {
        gchar *warning = g_strdup_printf (ngettext ("Warning: There is still %d user logged in.",
                                                    "Warning: There are still %d users logged in.",
                                                    logged_in_users),
                                          logged_in_users);
        message = new_message = g_markup_printf_escaped ("<b>%s</b>\n%s", warning, message);
        g_free (warning);
    }

    dialog_name = g_strconcat (action, "_dialog", NULL);
    button_name = g_strconcat (action, "_button", NULL);

    gtk_widget_set_name (power_window, dialog_name);
    gtk_widget_set_name (GTK_WIDGET (power_ok_button), button_name);
    gtk_button_set_label (power_ok_button, title);
    gtk_label_set_label (power_title, title);
    gtk_label_set_markup (power_text, message);
    gtk_image_set_from_icon_name (power_icon, icon, GTK_ICON_SIZE_DIALOG);

    g_free (button_name);
    g_free (dialog_name);
    g_free (new_message);

    loop = g_main_loop_new (NULL, FALSE);
    g_object_set_data (G_OBJECT (power_window), POWER_WINDOW_DATA_LOOP, loop);
    g_object_set_data (G_OBJECT (power_window), POWER_WINDOW_DATA_RESPONSE, GINT_TO_POINTER (GTK_RESPONSE_CANCEL));

    focused = gtk_window_get_focus (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (screen_overlay))));
    session_enabled = gtk_widget_get_sensitive (session_menuitem);
    language_enabled = gtk_widget_get_sensitive (language_menuitem);

    gtk_widget_set_sensitive (power_menuitem, FALSE);
    gtk_widget_set_sensitive (session_menuitem, FALSE);
    gtk_widget_set_sensitive (language_menuitem, FALSE);
    gtk_widget_hide (login_window);
    gtk_widget_show (power_window);
    gtk_widget_grab_focus (GTK_WIDGET (power_ok_button));

    g_main_loop_run (loop);
    response = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (power_window), POWER_WINDOW_DATA_RESPONSE));
    g_main_loop_unref (loop);

    gtk_widget_hide (power_window);
    gtk_widget_show (login_window);
    gtk_widget_set_sensitive (power_menuitem, TRUE);
    gtk_widget_set_sensitive (session_menuitem, session_enabled);
    gtk_widget_set_sensitive (language_menuitem, language_enabled);

    if (focused)
        gtk_widget_grab_focus (focused);

    return response == GTK_RESPONSE_YES;
}

void
power_button_clicked_cb (GtkButton *button, gpointer user_data)
{
    GMainLoop *loop = g_object_get_data (G_OBJECT (power_window), POWER_WINDOW_DATA_LOOP);
    if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);

    g_object_set_data (G_OBJECT (power_window), POWER_WINDOW_DATA_RESPONSE,
                       GINT_TO_POINTER (button == power_ok_button ? GTK_RESPONSE_YES : GTK_RESPONSE_CANCEL));
}

gboolean
power_window_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        power_button_clicked_cb (power_cancel_button, NULL);
        return TRUE;
    }
    return FALSE;
}

/* Handling window position */

static gboolean
read_position_from_str (const gchar *s, DimensionPosition *x)
{
    DimensionPosition p;
    gchar *end = NULL;
    gchar **parts = g_strsplit (s, ",", 2);
    if (parts[0])
    {
        p.value = g_ascii_strtoll (parts[0], &end, 10);
        p.percentage = end && end[0] == '%';
        p.sign = (p.value < 0 || (p.value == 0 && parts[0][0] == '-')) ? -1 : +1;
        if (p.value < 0)
            p.value *= -1;
        if (g_strcmp0(parts[1], "start") == 0)
            p.anchor = -1;
        else if (g_strcmp0(parts[1], "center") == 0)
            p.anchor = 0;
        else if (g_strcmp0(parts[1], "end") == 0)
            p.anchor = +1;
        else
            p.anchor = p.sign > 0 ? -1 : +1;
        *x = p;
    }
    else
        x = NULL;
    g_strfreev (parts);
    return x != NULL;
}

static WindowPosition*
str_to_position (const gchar *str, const WindowPosition *default_value)
{
    WindowPosition  *pos = g_new0 (WindowPosition, 1);
    gchar           *size_delim;
    gchar           *value;
    gchar           *x;
    gchar           *y;

    *pos = *default_value;

    if (str)
    {
        value = g_strdup (str);
        x = value;
        y = strchr (value, ' ');
        if (y)
            (y++)[0] = '\0';

        if (read_position_from_str (x, &pos->x))
            /* If there is no y-part then y = x */
            if (!y || !read_position_from_str (y, &pos->y))
                pos->y = pos->x;

        size_delim = strchr (y ? y : x, ';');
        if (size_delim)
        {
            x = size_delim + 1;
            if (read_position_from_str (x, &pos->width))
            {
                y = strchr (x, ' ');
                if (y)
                    (y++)[0] = '\0';
                if (!y || !read_position_from_str (y, &pos->height))
                    if (!default_value->use_size)
                        pos->height = pos->width;
                pos->use_size = TRUE;
            }
        }

        g_free (value);
    }

    return pos;
}

static gint
get_absolute_position (const DimensionPosition *p, gint screen, gint window)
{
    gint x = p->percentage ? (screen*p->value)/100 : p->value;
    x = p->sign < 0 ? screen - x : x;
    if (p->anchor > 0)
        x -= window;
    else if (p->anchor == 0)
        x -= window/2;

    if (x < 0)                     /* Offscreen: left/top */
        return 0;
    else if (x + window > screen)  /* Offscreen: right/bottom */
        return screen - window;
    else
        return x;
}

gboolean
screen_overlay_get_child_position_cb (GtkWidget *overlay, GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
    const WindowPosition *pos = g_object_get_data (G_OBJECT (widget), WINDOW_DATA_POSITION);
    gint                  screen_width;
    gint                  screen_height;
    gint                  panel_height;

    if (!pos)
        return FALSE;

    screen_width = gtk_widget_get_allocated_width (overlay);
    screen_height = gtk_widget_get_allocated_height (overlay);

    if (pos->use_size)
    {
        allocation->width = get_absolute_position (&pos->width, screen_width, 0);
        allocation->height = get_absolute_position (&pos->height, screen_height, 0);
    }
    else
    {
        gtk_widget_get_preferred_width (widget, NULL, &allocation->width);
        gtk_widget_get_preferred_height (widget, NULL, &allocation->height);
    }

    allocation->x = get_absolute_position (&pos->x, screen_width, allocation->width);
    allocation->y = get_absolute_position (&pos->y, screen_height, allocation->height);

    /* Do not overlap panel window */
    panel_height = gtk_widget_get_allocated_height (panel_window);
    if (panel_height && gtk_widget_get_visible (panel_window))
    {
        GtkAlign valign = gtk_widget_get_valign (panel_window);
        if (valign == GTK_ALIGN_START && allocation->y < panel_height)
            allocation->y = panel_height;
        else if (valign == GTK_ALIGN_END && screen_height - allocation->y - allocation->height < panel_height)
            allocation->y = screen_height - allocation->height - panel_height;
    }

    return TRUE;
}

/* Clock */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static gboolean
clock_timeout_thread (void)
{
    time_t rawtime;
    struct tm * timeinfo;
    gchar time_str[50];
    gchar *markup;

    time (&rawtime);
    timeinfo = localtime (&rawtime);

    strftime (time_str, 50, clock_format, timeinfo);
    markup = g_markup_printf_escaped ("<b>%s</b>", time_str);
    if (g_strcmp0 (markup, gtk_label_get_label (GTK_LABEL (clock_label))) != 0)
        gtk_label_set_markup (GTK_LABEL (clock_label), markup);
    g_free (markup);

    return TRUE;
}

#pragma GCC diagnostic pop

/* Message label */

static gboolean
message_label_is_empty (void)
{
    return gtk_label_get_text (message_label)[0] == '\0';
}

static void
set_message_label (LightDMMessageType type, const gchar *text)
{
    if (type == LIGHTDM_MESSAGE_TYPE_INFO)
        gtk_info_bar_set_message_type (info_bar, GTK_MESSAGE_INFO);
    else
        gtk_info_bar_set_message_type (info_bar, GTK_MESSAGE_ERROR);
    gtk_label_set_text (message_label, text);
    gtk_widget_set_visible (GTK_WIDGET (info_bar), text && text[0]);
}

/* User image */

static void
set_user_image (const gchar *username)
{
    const gchar *path;
    LightDMUser *user = NULL;
    GdkPixbuf *image = NULL;
    GError *error = NULL;

    if (!gtk_widget_get_visible (GTK_WIDGET (user_image)))
        return;

    if (username)
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);

    if (user)
    {
        path = lightdm_user_get_image (user);
        if (path)
        {
            image = gdk_pixbuf_new_from_file_at_scale (path, 80, 80, FALSE, &error);
            if (image)
            {
                gtk_image_set_from_pixbuf (GTK_IMAGE (user_image), image);
                g_object_unref (image);
                return;
            }
            else
            {
                g_debug ("Failed to load user image: %s", error->message);
                g_clear_error (&error);
            }
        }
    }

    if (default_user_pixbuf)
        gtk_image_set_from_pixbuf (GTK_IMAGE (user_image), default_user_pixbuf);
    else
        gtk_image_set_from_icon_name (GTK_IMAGE (user_image), default_user_icon, GTK_ICON_SIZE_DIALOG);
}

/* MenuCommand */

static MenuCommand*
menu_command_parse (const gchar *name, const gchar *value, GtkWidget *menu_item)
{
    return menu_command_parse_extended (name, value, menu_item, NULL, NULL);
}

static MenuCommand*
menu_command_parse_extended (const gchar *name,
                             const gchar *value,    /* String to parse */
                             GtkWidget   *menu_item,  /* Menu item to connect */
                             const gchar *xid_app,  /* Program that have "xembed" mode support */
                             const gchar *xid_arg)  /* Argument that must be added to command line */
{
    MenuCommand  *command;
    GError       *error = NULL;
    gchar       **argv;
    gint          argc = 0;

    if (!value)
        return NULL;

    if (!g_shell_parse_argv (value, &argc, &argv, &error))
    {
        if (error)
            g_warning ("[Command/%s] Failed to parse command line: %s", name, error->message);
        g_clear_error (&error);
        return NULL;
    }

    command = g_new0 (MenuCommand, 1);
    command->menu_item = menu_item;
    command->argc = argc;
    command->argv = argv;

    if (g_strcmp0 (argv[0], xid_app) == 0)
    {
        gboolean have_xid_arg = FALSE;
        gint i;
        for (i = 1; i < argc; ++i)
            if (g_strcmp0 (argv[i], xid_arg) == 0)
            {
                have_xid_arg = TRUE;
                break;
            }
        if (!have_xid_arg)
        {
            gchar *new_value = g_strdup_printf ("%s %s", value, xid_arg);

            if (g_shell_parse_argv (new_value, &argc, &argv, &error))
            {
                g_strfreev (command->argv);
                command->argc = argc;
                command->argv = argv;
                have_xid_arg = TRUE;
            }
            else
            {
                if (error)
                    g_warning ("[Command/%s] Failed to parse command line: %s", name, error->message);
                g_clear_error (&error);
            }
            g_free (new_value);
        }

        if (have_xid_arg)
        {
            command->widget = gtk_event_box_new ();
            gtk_overlay_add_overlay (screen_overlay, command->widget);
        }
    }
    command->name = name;
    return command;
}

static gboolean
menu_command_run (MenuCommand *command)
{
    GError *error = NULL;

    g_return_val_if_fail (command && g_strv_length (command->argv), FALSE);

    command->pid = 0;

    g_debug ("[Command/%s] Running command", command->name);

    if (command->widget)
    {
        GtkSocket* socket = NULL;
        gint out_fd = 0;
        GPid pid = spawn_argv_pid (command->argv, G_SPAWN_SEARCH_PATH, &out_fd, &error);

        if (pid && out_fd)
        {
            gchar* text = NULL;
            GIOChannel* out_channel = g_io_channel_unix_new (out_fd);
            if (g_io_channel_read_line (out_channel, &text, NULL, NULL, &error) == G_IO_STATUS_NORMAL)
            {
                gchar* end_ptr = NULL;
                gint id;

                text = g_strstrip (text);
                id = g_ascii_strtoll (text, &end_ptr, 0);

                if (id != 0 && end_ptr > text)
                {
                    socket = GTK_SOCKET (gtk_socket_new ());
                    gtk_container_foreach (GTK_CONTAINER (command->widget), (GtkCallback)gtk_widget_destroy, NULL);
                    gtk_container_add (GTK_CONTAINER (command->widget), GTK_WIDGET (socket));
                    gtk_socket_add_id (socket, id);
                    gtk_widget_show_all (GTK_WIDGET (command->widget));

                    command->pid = pid;
                }
                else
                    g_warning ("[Command/%s] Failed to get '%s' socket for: unrecognized output",
                               command->name, command->argv[0]);

                g_free (text);
            }
        }

        if (!command->pid && pid)
            close_pid (pid, TRUE);
    }
    else
    {
        command->pid = spawn_argv_pid (command->argv, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, &error);
        if (command->pid)
            g_child_watch_add (command->pid, (GChildWatchFunc)menu_command_terminated_cb, command);
    }

    if (!command->pid)
    {
        if (error)
            g_warning ("[Command/%s] Failed to run: %s", command->name, error->message);
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (command->menu_item), FALSE);
    }

    g_clear_error (&error);

    return command->pid;
}

static gboolean
menu_command_stop (MenuCommand *command)
{
    g_return_val_if_fail (command, FALSE);

    if (command->pid)
    {
        g_debug ("[Command/%s] Stopping command", command->name);
        close_pid (command->pid, TRUE);
        command->pid = 0;
        if (command->menu_item)
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (command->menu_item), FALSE);
        if (command->widget)
            gtk_widget_hide (command->widget);
    }
    return TRUE;
}

static void
menu_command_terminated_cb (GPid pid, gint status, MenuCommand *command)
{
    menu_command_stop (command);
}

static void
a11y_menuitem_toggled_cb (GtkCheckMenuItem *item, const gchar* name)
{
    config_set_bool (STATE_SECTION_A11Y, name, gtk_check_menu_item_get_active (item));
}

/* Session */

static gboolean
is_valid_session (GList* items, const gchar* session)
{
    for (; items; items = g_list_next (items))
        if (g_strcmp0 (session, lightdm_session_get_key (items->data)) == 0)
            return TRUE;
    return FALSE;
}

static gchar*
get_session (void)
{
    return g_strdup (current_session);
}

static void
set_session (const gchar *session)
{
    gchar *last_session = NULL;
    GList *sessions = lightdm_get_sessions ();

    /* Validation */
    if (!session || !is_valid_session (sessions, session))
    {
        /* previous session */
        last_session = config_get_string (STATE_SECTION_GREETER, STATE_KEY_LAST_SESSION, NULL);
        if (last_session && g_strcmp0 (session, last_session) != 0 &&
            is_valid_session (sessions, last_session))
            session = last_session;
        else
        {
            /* default */
            const gchar* default_session = lightdm_greeter_get_default_session_hint (greeter);
            if (g_strcmp0 (session, default_session) != 0 &&
                is_valid_session (sessions, default_session))
                session = default_session;
            /* first in the sessions list */
            else if (sessions)
                session = lightdm_session_get_key (sessions->data);
            /* give up */
            else
                session = NULL;
        }
    }

    if (gtk_widget_get_visible (session_menuitem))
    {
        GList *menu_iter = NULL;
        GList *menu_items = gtk_container_get_children (GTK_CONTAINER (session_menu));
        if (session)
        {
            for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next (menu_iter))
                if (g_strcmp0 (session, g_object_get_data (G_OBJECT (menu_iter->data), SESSION_DATA_KEY)) == 0)
                {
                    /* Set menuitem-image to session-badge */
                    GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();
                    gchar* session_name = g_ascii_strdown (session, -1);
                    gchar* icon_name = g_strdup_printf ("%s_badge-symbolic", session_name);
                    g_free (session_name);
                    if (gtk_icon_theme_has_icon (icon_theme, icon_name))
                        gtk_image_set_from_icon_name (GTK_IMAGE (session_badge), icon_name, GTK_ICON_SIZE_MENU);
                    else
                        gtk_image_set_from_icon_name (GTK_IMAGE (session_badge), "document-properties-symbolic", GTK_ICON_SIZE_MENU);
                    g_free (icon_name);
                    break;
                }
        }
        if (!menu_iter)
            menu_iter = menu_items;

        if (menu_iter)
	        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_iter->data), TRUE);
    }

    g_free (current_session);
    current_session = g_strdup (session);
    g_free (last_session);
}

void
session_selected_cb (GtkMenuItem *menuitem, gpointer user_data)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
       set_session (g_object_get_data (G_OBJECT (menuitem), SESSION_DATA_KEY));
}


/* Session language */

static gchar*
get_language (void)
{
    GList *menu_items, *menu_iter;

    /* if the user manually selected a language, use it */
    if (current_language)
        return g_strdup (current_language);

    menu_items = gtk_container_get_children (GTK_CONTAINER (language_menu));
    for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next (menu_iter))
    {
        if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menu_iter->data)))
            return g_strdup (g_object_get_data (G_OBJECT (menu_iter->data), LANGUAGE_DATA_CODE));
    }

    return NULL;
}

static void
set_language (const gchar *language)
{
    const gchar *default_language = NULL;
    GList *menu_items, *menu_iter;

    if (!gtk_widget_get_visible (language_menuitem))
    {
        g_free (current_language);
        current_language = g_strdup (language);
        return;
    }

    menu_items = gtk_container_get_children (GTK_CONTAINER (language_menu));

    if (language)
    {
        for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next (menu_iter))
        {
            gchar *s;
            gboolean matched;
            s = g_strdup (g_object_get_data (G_OBJECT (menu_iter->data), LANGUAGE_DATA_CODE));
            matched = g_strcmp0 (s, language) == 0;
            g_free (s);
            if (matched)
            {
                gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_iter->data), TRUE);
                g_free (current_language);
                current_language = g_strdup (language);
                gtk_menu_item_set_label (GTK_MENU_ITEM (language_menuitem),language);
                return;
            }
        }
    }

    /* If failed to find this language, then try the default */
    if (lightdm_get_language ())
    {
        default_language = lightdm_language_get_code (lightdm_get_language ());
        gtk_menu_item_set_label (GTK_MENU_ITEM (language_menuitem), default_language);
    }
    if (default_language && g_strcmp0 (default_language, language) != 0)
        set_language (default_language);
    /* If all else fails, just use the first language from the menu */
    else
    {
        for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next (menu_iter))
        {
            if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menu_iter->data)))
            {
                gtk_menu_item_set_label (GTK_MENU_ITEM (language_menuitem), g_strdup (g_object_get_data (G_OBJECT (menu_iter->data), LANGUAGE_DATA_CODE)));
                break;
            }
        }
    }
}

void
language_selected_cb (GtkMenuItem *menuitem, gpointer user_data)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
    {
       gchar *language = g_object_get_data (G_OBJECT (menuitem), LANGUAGE_DATA_CODE);
       set_language (language);
    }
}

/* Pending questions */

static void
pam_message_finalize (PAMConversationMessage *message)
{
    g_free (message->text);
    g_free (message);
}

static void
process_prompts (LightDMGreeter *ldm)
{
    if (!pending_questions)
        return;

    /* always allow the user to change username again */
    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), TRUE);

    /* Special case: no user selected from list, so PAM asks us for the user
     * via a prompt. For that case, use the username field */
    if (!prompted && pending_questions && !pending_questions->next &&
        ((PAMConversationMessage *) pending_questions->data)->is_prompt &&
        ((PAMConversationMessage *) pending_questions->data)->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET &&
        gtk_widget_get_visible ((GTK_WIDGET (username_entry))) &&
        lightdm_greeter_get_authentication_user (ldm) == NULL)
    {
        prompted = TRUE;
        prompt_active = TRUE;
        gtk_widget_grab_focus (GTK_WIDGET (username_entry));
        gtk_widget_show (GTK_WIDGET (password_entry));
        return;
    }

    while (pending_questions)
    {
        PAMConversationMessage *message = (PAMConversationMessage *) pending_questions->data;
        pending_questions = g_slist_remove (pending_questions, (gconstpointer) message);

        if (!message->is_prompt)
        {
            /* FIXME: this doesn't show multiple messages, but that was
             * already the case before. */
            set_message_label (message->type.message, message->text);
            continue;
        }

        gtk_widget_show (GTK_WIDGET (password_entry));
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        gtk_entry_set_text (password_entry, "");
        gtk_entry_set_visibility (password_entry, message->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET);
        if (message_label_is_empty () && password_prompted)
        {
            /* No message was provided beforehand and this is not the
             * first password prompt, so use the prompt as label,
             * otherwise the user will be completely unclear of what
             * is going on. Actually, the fact that prompt messages are
             * not shown is problematic in general, especially if
             * somebody uses a custom PAM module that wants to ask
             * something different. */
            gchar *str = message->text;
            if (g_str_has_suffix (str, ": "))
                str = g_strndup (str, strlen (str) - 2);
            else if (g_str_has_suffix (str, ":"))
                str = g_strndup (str, strlen (str) - 1);
            set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, str);
            if (str != message->text)
                g_free (str);
        }
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        prompted = TRUE;
        password_prompted = TRUE;
        prompt_active = TRUE;

        /* If we have more stuff after a prompt, assume that other prompts are pending,
         * so stop here. */
        break;
    }
}

/* Panel and indicators */

static gboolean
panel_item_enter_notify_cb (GtkWidget *widget, GdkEvent *event, gpointer enter)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    if (GPOINTER_TO_INT (enter))
        gtk_style_context_add_class (context, PANEL_ITEM_STYLE_HOVERED);
    else
        gtk_style_context_remove_class (context, PANEL_ITEM_STYLE_HOVERED);
    return FALSE;
}

static void
panel_add_item (GtkWidget *widget, gint index, GreeterPanelItemType item_type)
{
    gint insert_pos = 0;
    GList* items = gtk_container_get_children (GTK_CONTAINER (menubar));
    GList* item;
    for (item = items; item; item = item->next)
    {
        if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item->data), PANEL_ITEM_DATA_INDEX)) < index)
            break;
        insert_pos++;
    }
    g_list_free (items);

    gtk_style_context_add_class (gtk_widget_get_style_context (widget), PANEL_ITEM_STYLE);
    gtk_style_context_add_class (gtk_widget_get_style_context (widget), PANEL_ITEM_STYLES[item_type]);
    if (item_type == PANEL_ITEM_INDICATOR)
    {
        g_signal_connect (G_OBJECT (widget), "enter-notify-event",
                          G_CALLBACK (panel_item_enter_notify_cb), GINT_TO_POINTER (TRUE));
        g_signal_connect (G_OBJECT (widget), "leave-notify-event",
                          G_CALLBACK (panel_item_enter_notify_cb), GINT_TO_POINTER (FALSE));
    }

    gtk_widget_show (widget);
    gtk_menu_shell_insert (GTK_MENU_SHELL (menubar), widget, insert_pos);
}

#ifdef HAVE_LIBINDICATOR
static gboolean
indicator_entry_scrolled_cb (GtkWidget *menuitem, GdkEventScroll *event, gpointer data)
{
    IndicatorObject      *io;
    IndicatorObjectEntry *entry;

    g_return_val_if_fail (GTK_IS_WIDGET (menuitem), FALSE);

    io = g_object_get_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_OBJECT);
    entry = g_object_get_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_ENTRY);

    g_return_val_if_fail (INDICATOR_IS_OBJECT (io), FALSE);

    g_signal_emit_by_name(io, INDICATOR_OBJECT_SIGNAL_ENTRY_SCROLLED, entry, 1, event->direction);

    return FALSE;
}

static void
indicator_entry_activated_cb (GtkWidget *widget, gpointer user_data)
{
    IndicatorObject      *io;
    IndicatorObjectEntry *entry;

    g_return_if_fail (GTK_IS_WIDGET (widget));

    io = g_object_get_data (G_OBJECT (widget), INDICATOR_ITEM_DATA_OBJECT);
    entry = g_object_get_data (G_OBJECT (widget), INDICATOR_ITEM_DATA_ENTRY);

    g_return_if_fail (INDICATOR_IS_OBJECT (io));

    return indicator_object_entry_activate (io, entry, gtk_get_current_event_time ());
}

static GtkWidget*
indicator_entry_create_menuitem (IndicatorObject *io, IndicatorObjectEntry *entry, GtkWidget *mbar)
{
    GtkWidget *box, *menuitem;
    gint index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (io), PANEL_ITEM_DATA_INDEX));
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    menuitem = gtk_menu_item_new ();

    gtk_widget_add_events (GTK_WIDGET (menuitem), GDK_SCROLL_MASK);

    g_object_set_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_BOX, box);
    g_object_set_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_OBJECT, io);
    g_object_set_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_ENTRY, entry);
    g_object_set_data (G_OBJECT (menuitem), PANEL_ITEM_DATA_INDEX, GINT_TO_POINTER (index));

    g_signal_connect (G_OBJECT (menuitem), "activate", G_CALLBACK (indicator_entry_activated_cb), NULL);
    g_signal_connect (G_OBJECT (menuitem), "scroll-event", G_CALLBACK (indicator_entry_scrolled_cb), NULL);

    if (entry->image)
        gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (entry->image), FALSE, FALSE, 1);

    if (entry->label)
        gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (entry->label), FALSE, FALSE, 1);

    if (entry->menu)
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), GTK_WIDGET (entry->menu));

    gtk_container_add (GTK_CONTAINER (menuitem), box);
    gtk_widget_show (box);
    panel_add_item (menuitem, index, PANEL_ITEM_INDICATOR);

    return menuitem;
}

static void
indicator_entry_added_cb (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    GHashTable *menuitem_lookup;
    GtkWidget  *menuitem;

    /* if the menuitem doesn't already exist, create it now */
    menuitem_lookup = g_object_get_data (G_OBJECT (io), INDICATOR_DATA_MENUITEMS);
    g_return_if_fail (menuitem_lookup);
    menuitem = g_hash_table_lookup (menuitem_lookup, entry);
    if (!GTK_IS_WIDGET (menuitem))
    {
        menuitem = indicator_entry_create_menuitem (io, entry, GTK_WIDGET (user_data));
        g_hash_table_insert (menuitem_lookup, entry, menuitem);
    }

    gtk_widget_show (menuitem);
}

static void
remove_indicator_entry_cb (GtkWidget *widget, gpointer userdata)
{
    IndicatorObject *io;
    GHashTable      *menuitem_lookup;
    GtkWidget       *menuitem;
    gpointer         entry;

    io = g_object_get_data (G_OBJECT (widget), INDICATOR_ITEM_DATA_OBJECT);
    if (!INDICATOR_IS_OBJECT (io))
        return;

    entry = g_object_get_data (G_OBJECT (widget), INDICATOR_ITEM_DATA_ENTRY);
    if (entry != userdata)
        return;

    menuitem_lookup = g_object_get_data (G_OBJECT (io), INDICATOR_DATA_MENUITEMS);
    g_return_if_fail (menuitem_lookup);
    menuitem = g_hash_table_lookup (menuitem_lookup, entry);
    if (GTK_IS_WIDGET (menuitem))
        gtk_widget_hide (menuitem);

    gtk_widget_destroy (widget);
}

static void
indicator_entry_removed_cb (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    gtk_container_foreach (GTK_CONTAINER (user_data), remove_indicator_entry_cb, entry);
}

static void
indicator_menu_show_cb (IndicatorObject *io, IndicatorObjectEntry *entry, guint32 timestamp, gpointer user_data)
{
    IndicatorObjectEntry *entrydata;
    GtkWidget            *menuitem;
    GList                *entries, *lp;

    menuitem = GTK_WIDGET (user_data);

    if (!entry)
    {
        /* Close any open menus instead of opening one */
        entries = indicator_object_get_entries (io);
        for (lp = entries; lp; lp = g_list_next (lp))
        {
            entrydata = lp->data;
            gtk_menu_popdown (entrydata->menu);
        }
        g_list_free (entries);

        /* And tell the menuitem to exit activation mode too */
        gtk_menu_shell_cancel (GTK_MENU_SHELL (menuitem));
    }
}

static void
greeter_set_env (const gchar* key, const gchar* value)
{
    GDBusProxy      *proxy;
    GVariantBuilder *builder;
    GVariant        *result;

    g_setenv (key, value, TRUE);

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.DBus",
                                           "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus",
                                           NULL, NULL);
    builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add (builder, "{ss}", key, value);
    result = g_dbus_proxy_call_sync (proxy, "UpdateActivationEnvironment", g_variant_new ("(a{ss})", builder),
                                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_variant_unref (result);
    g_variant_builder_unref (builder);
    g_object_unref (proxy);
}
#endif

static gboolean
menu_item_accel_closure_cb (GtkAccelGroup *accel_group,
                            GObject *acceleratable, guint keyval,
                            GdkModifierType modifier, gpointer data)
{
    gtk_menu_item_activate (data);
    return FALSE;
}

/* Maybe unnecessary (in future) trick to enable accelerators for hidden/detached menu items */
static void
reassign_menu_item_accel (GtkWidget *item)
{
    GtkWidget   *submenu;
    GtkAccelKey  key;
    const gchar *accel_path = gtk_menu_item_get_accel_path (GTK_MENU_ITEM (item));

    if (accel_path && gtk_accel_map_lookup_entry (accel_path, &key))
    {
        GClosure *closure = g_cclosure_new (G_CALLBACK (menu_item_accel_closure_cb), item, NULL);
        gtk_accel_group_connect (gtk_menu_get_accel_group (GTK_MENU (gtk_widget_get_parent (item))),
                                 key.accel_key, key.accel_mods, key.accel_flags, closure);
        g_closure_unref (closure);
    }

    submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (item));
    if (submenu)
        gtk_container_foreach (GTK_CONTAINER (submenu), (GtkCallback)reassign_menu_item_accel, NULL);
}

static void
init_indicators (void)
{
    GHashTable       *builtin_items = NULL;
    GHashTableIter    iter;
    gpointer          iter_value;
    gsize             length = 0;
    guint             i;

    #ifdef HAVE_LIBINDICATOR
    IndicatorObject  *io = NULL;
    gboolean          inited = FALSE;
    gchar            *path = NULL;
    #endif

    gchar           **names = config_get_string_list (NULL, CONFIG_KEY_INDICATORS, NULL);

    if (!names)
        names = (gchar**)DEFAULT_LAYOUT;
    length = g_strv_length (names);

    builtin_items = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);

    g_hash_table_insert (builtin_items, g_strdup("~power"), power_menuitem);
    g_hash_table_insert (builtin_items, g_strdup("~session"), session_menuitem);
    g_hash_table_insert (builtin_items, g_strdup("~language"), language_menuitem);
    g_hash_table_insert (builtin_items, g_strdup("~a11y"), a11y_menuitem);
    g_hash_table_insert (builtin_items, g_strdup("~layout"), layout_menuitem);
    g_hash_table_insert (builtin_items, g_strdup("~host"), host_menuitem);
    g_hash_table_insert (builtin_items, g_strdup("~clock"), clock_menuitem);

    g_hash_table_iter_init (&iter, builtin_items);
    while (g_hash_table_iter_next (&iter, NULL, &iter_value))
        gtk_container_remove (GTK_CONTAINER (menubar), iter_value);

    for (i = 0; i < length; ++i)
    {
        if (names[i][0] == '~')
        {   /* Built-in indicators */
            GreeterPanelItemType item_type = PANEL_ITEM_INDICATOR;
            if (g_hash_table_lookup_extended (builtin_items, names[i], NULL, &iter_value))
                g_hash_table_remove (builtin_items, (gconstpointer)names[i]);
            else if (g_strcmp0 (names[i], "~separator") == 0)
            {
                GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
                item_type = PANEL_ITEM_SEPARATOR;
                iter_value = gtk_separator_menu_item_new ();
                gtk_widget_show (separator);
                gtk_container_add (iter_value, separator);
            }
            else if (g_strcmp0 (names[i], "~spacer") == 0)
            {
                item_type = PANEL_ITEM_SPACER;
                iter_value = gtk_separator_menu_item_new ();
                gtk_menu_item_set_label (iter_value, "");
                gtk_widget_set_hexpand (iter_value, TRUE);
            }
            else if (names[i][1] == '~')
            {
                item_type = PANEL_ITEM_TEXT;
                iter_value = gtk_separator_menu_item_new ();
                gtk_menu_item_set_label (iter_value, &names[i][2]);
            }
            else
                continue;

            g_object_set_data (G_OBJECT (iter_value), PANEL_ITEM_DATA_INDEX, GINT_TO_POINTER (i));
            panel_add_item (iter_value, i, item_type);
            continue;
        }

        #ifdef HAVE_LIBINDICATOR
        if (!inited)
        {
            /* Set indicators to run with reduced functionality */
            greeter_set_env ("INDICATOR_GREETER_MODE", "1");
            /* Don't allow virtual file systems? */
            greeter_set_env ("GIO_USE_VFS", "local");
            greeter_set_env ("GVFS_DISABLE_FUSE", "1");
            inited = TRUE;
        }

        if (g_path_is_absolute (names[i]))
        {   /* library with absolute path */
            io = indicator_object_new_from_file (names[i]);
        }
        else if (g_str_has_suffix (names[i], G_MODULE_SUFFIX))
        {   /* library */
            path = g_build_filename (INDICATOR_DIR, names[i], NULL);
            io = indicator_object_new_from_file (path);
        }
        #ifdef HAVE_LIBINDICATOR_NG
        else
        {   /* service file */
            if (strchr (names[i], '.'))
                path = g_strdup_printf ("%s/%s", UNITY_INDICATOR_DIR, names[i]);
            else
                path = g_strdup_printf ("%s/com.canonical.indicator.%s", UNITY_INDICATOR_DIR, names[i]);
            io = INDICATOR_OBJECT (indicator_ng_new_for_profile (path, "desktop_greeter", NULL));
        }
        #endif

        if (io)
        {
            GList *entries, *lp;

            /* used to store/fetch menu entries */
            g_object_set_data_full (G_OBJECT (io), INDICATOR_DATA_MENUITEMS,
                                    g_hash_table_new (g_direct_hash, g_direct_equal),
                                    (GDestroyNotify) g_hash_table_destroy);
            g_object_set_data (G_OBJECT (io), PANEL_ITEM_DATA_INDEX, GINT_TO_POINTER (i));

            g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED,
                              G_CALLBACK (indicator_entry_added_cb), menubar);
            g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED,
                              G_CALLBACK (indicator_entry_removed_cb), menubar);
            g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_MENU_SHOW,
                              G_CALLBACK (indicator_menu_show_cb), menubar);

            entries = indicator_object_get_entries (io);
            for (lp = entries; lp; lp = g_list_next (lp))
                indicator_entry_added_cb (io, lp->data, menubar);
            g_list_free (entries);
        }
        else
        {
            g_warning ("Indicator \"%s\": failed to load", names[i]);
        }

        g_free (path);
        #endif
    }
    if (names && names != (gchar**)DEFAULT_LAYOUT)
        g_strfreev (names);

    if (builtin_items)
    {
        g_hash_table_iter_init (&iter, builtin_items);
        while (g_hash_table_iter_next (&iter, NULL, &iter_value))
        {
            reassign_menu_item_accel (iter_value);
            gtk_widget_hide (iter_value);
        }

        g_hash_table_unref (builtin_items);
    }
}

/* Layout indicator */

static void
layout_selected_cb (GtkCheckMenuItem *menuitem, gpointer user_data)
{
    if (gtk_check_menu_item_get_active (menuitem))
    {
        #ifdef HAVE_LIBXKLAVIER
        gint group = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menuitem), LAYOUT_DATA_GROUP));
        xkl_engine_lock_group (xkl_engine, group);
        #else
        const gchar *name = g_object_get_data (G_OBJECT (menuitem), LAYOUT_DATA_NAME);
        GList *item;
        for (item = lightdm_get_layouts (); item; item = g_list_next (item))
        {
            if (g_strcmp0 (name, lightdm_layout_get_name (item->data)) == 0)
            {
                lightdm_set_layout (item->data);
                gtk_menu_item_set_label (GTK_MENU_ITEM (layout_menuitem),
                                         g_object_get_data (G_OBJECT (menuitem), LAYOUT_DATA_LABEL));
                break;
            }
        }
        #endif
    }
}

static void
update_layouts_menu (void)
{
    #ifdef HAVE_LIBXKLAVIER
    XklConfigRegistry *registry;
    XklConfigRec *config;
    XklConfigItem *config_item;
    GSList *menu_group = NULL;
    gint i;

    g_list_free_full (gtk_container_get_children (GTK_CONTAINER (layout_menu)),
                      (GDestroyNotify)gtk_widget_destroy);

    config = xkl_config_rec_new ();
    if (!xkl_config_rec_get_from_server (config, xkl_engine))
    {
        g_object_unref (config);
        g_warning ("Failed to get Xkl configuration from server");
        return;
    }

    config_item = xkl_config_item_new ();
    registry = xkl_config_registry_get_instance (xkl_engine);
    xkl_config_registry_load (registry, FALSE);

    for (i = 0; config->layouts[i] != NULL; ++i)
    {
        const gchar *layout = config->layouts[i] ? config->layouts[i] : "";
        const gchar *variant = config->variants[i] ? config->variants[i] : "";
        gchar *label = strlen (variant) > 0 ? g_strdup_printf ("%s_%s", layout, variant) : g_strdup (layout);

        GtkWidget *menuitem = gtk_radio_menu_item_new (menu_group);
        menu_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menuitem));

        g_snprintf (config_item->name, sizeof (config_item->name), "%s", variant);
        if (xkl_config_registry_find_variant (registry, layout, config_item))
            gtk_menu_item_set_label (GTK_MENU_ITEM (menuitem), config_item->description);
        else
        {
            g_snprintf (config_item->name, sizeof (config_item->name), "%s", layout);
            if (xkl_config_registry_find_layout (registry, config_item))
                gtk_menu_item_set_label (GTK_MENU_ITEM (menuitem), config_item->description);
            else
                gtk_menu_item_set_label (GTK_MENU_ITEM (menuitem), label);
        }

        g_object_set_data_full (G_OBJECT (menuitem), LAYOUT_DATA_LABEL, label, g_free);
        g_object_set_data (G_OBJECT (menuitem), LAYOUT_DATA_GROUP, GINT_TO_POINTER (i));

        g_signal_connect (G_OBJECT (menuitem), "activate", G_CALLBACK (layout_selected_cb), NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (layout_menu), menuitem);
        gtk_widget_show (GTK_WIDGET (menuitem));
    }

    g_object_unref (registry);
    g_object_unref (config_item);
    g_object_unref (config);
    #else
    GSList *menu_group = NULL;
    GList *item;

    g_list_free_full (gtk_container_get_children (GTK_CONTAINER (layout_menu)),
                      (GDestroyNotify)gtk_widget_destroy);

    for (item = lightdm_get_layouts (); item; item = g_list_next (item))
    {
        LightDMLayout *layout = item->data;
        GtkWidget *menuitem = gtk_radio_menu_item_new (menu_group);
        menu_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menuitem));

        g_object_set_data_full (G_OBJECT (menuitem), LAYOUT_DATA_LABEL,
                                g_strdelimit (g_strdup (lightdm_layout_get_name (layout)), "\t ", '_'), g_free);
        g_object_set_data_full (G_OBJECT (menuitem), LAYOUT_DATA_NAME,
                                g_strdup (lightdm_layout_get_name (layout)), g_free);

        g_signal_connect (G_OBJECT (menuitem), "activate", G_CALLBACK (layout_selected_cb), NULL);
        gtk_menu_item_set_label (GTK_MENU_ITEM (menuitem), lightdm_layout_get_description (layout));
        gtk_menu_shell_append (GTK_MENU_SHELL (layout_menu), menuitem);
        gtk_widget_show (GTK_WIDGET (menuitem));
    }
    #endif
}

static void
update_layouts_menu_state (void)
{
#ifdef HAVE_LIBXKLAVIER
    XklState         *state;
    GList            *menu_items;
    GtkCheckMenuItem *menu_item;
#else
    const gchar      *name;
    GList            *menu_items;
    GList            *menu_iter;
    LightDMLayout    *layout;
#endif

#ifdef HAVE_LIBXKLAVIER
    state = xkl_engine_get_current_state (xkl_engine);
    menu_items = gtk_container_get_children (GTK_CONTAINER (layout_menu));
    menu_item = g_list_nth_data (menu_items, state->group);

    if (menu_item)
    {
        gtk_menu_item_set_label (GTK_MENU_ITEM (layout_menuitem),
                                 g_object_get_data (G_OBJECT (menu_item), LAYOUT_DATA_LABEL));
        gtk_check_menu_item_set_active (menu_item, TRUE);
    }
    else
        gtk_menu_item_set_label (GTK_MENU_ITEM (layout_menuitem), "??");
    g_list_free (menu_items);
#else
    layout = lightdm_get_layout ();
    g_return_if_fail (layout != NULL);

    name = lightdm_layout_get_name (layout);
    menu_items = gtk_container_get_children (GTK_CONTAINER (layout_menu));
    for (menu_iter = menu_items; menu_iter; menu_iter = g_list_next (menu_iter))
    {
        if (g_strcmp0 (name, g_object_get_data (G_OBJECT (menu_iter->data), LAYOUT_DATA_NAME)) == 0)
        {
            gtk_check_menu_item_set_active (menu_iter->data, TRUE);
            gtk_menu_item_set_label (GTK_MENU_ITEM (layout_menuitem),
                                     g_object_get_data (G_OBJECT (menu_iter->data), LAYOUT_DATA_LABEL));
            break;
        }
    }
    g_list_free (menu_items);
#endif
}

#ifdef HAVE_LIBXKLAVIER
static void
xkl_state_changed_cb (XklEngine *engine, XklEngineStateChange change, gint group,
                      gboolean restore, gpointer user_data)
{
    if (change == GROUP_CHANGED)
        update_layouts_menu_state ();
}

static void
xkl_config_changed_cb (XklEngine *engine, gpointer user_data)
{
    /* tip: xkl_config_rec_get_from_server() return old settings */
    update_layouts_menu ();
    update_layouts_menu_state ();
}

static GdkFilterReturn
xkl_xevent_filter (GdkXEvent *xev, GdkEvent *event, gpointer  data)
{
    XEvent *xevent = (XEvent *) xev;
    xkl_engine_filter_events (xkl_engine, xevent);
    return GDK_FILTER_CONTINUE;
}
#endif

/* a11y indciator */

void
a11y_font_cb (GtkCheckMenuItem *item)
{
    if (gtk_check_menu_item_get_active (item))
    {
        gchar *font_name, **tokens;
        guint length;

        g_object_get (gtk_settings_get_default (), "gtk-font-name", &font_name, NULL);
        tokens = g_strsplit (font_name, " ", -1);
        length = g_strv_length (tokens);
        if (length > 1)
        {
            gint size = atoi (tokens[length - 1]);
            if (size > 0)
            {
                g_free (tokens[length - 1]);
                tokens[length - 1] = g_strdup_printf ("%d", size + 10);
                g_free (font_name);
                font_name = g_strjoinv (" ", tokens);
            }
        }
        g_strfreev (tokens);

        g_object_set (gtk_settings_get_default (), "gtk-font-name", font_name, NULL);
    }
    else
    {
        g_object_set (gtk_settings_get_default (), "gtk-font-name", default_font_name, NULL);
    }
}

void
a11y_contrast_cb (GtkCheckMenuItem *item)
{
    if (gtk_check_menu_item_get_active (item))
    {
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", "HighContrast", NULL);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", "HighContrast", NULL);
    }
    else
    {
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", default_theme_name, NULL);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", default_icon_theme_name, NULL);
    }
}

void
a11y_keyboard_cb (GtkCheckMenuItem *item, gpointer user_data)
{
    if (gtk_check_menu_item_get_active (item))
        menu_command_run (a11y_keyboard_command);
    else
        menu_command_stop (a11y_keyboard_command);
}

void
a11y_reader_cb (GtkCheckMenuItem *item, gpointer user_data)
{
    if (gtk_check_menu_item_get_active (item))
        menu_command_run (a11y_reader_command);
    else
        menu_command_stop (a11y_reader_command);
}

/* Power indicator */

static void
power_menu_cb (GtkWidget *menuitem, gpointer userdata)
{
    gtk_widget_set_sensitive (suspend_menuitem, lightdm_get_can_suspend ());
    gtk_widget_set_sensitive (hibernate_menuitem, lightdm_get_can_hibernate ());
    gtk_widget_set_sensitive (restart_menuitem, lightdm_get_can_restart ());
    gtk_widget_set_sensitive (shutdown_menuitem, lightdm_get_can_shutdown ());
}

void
suspend_cb (GtkWidget *widget, LightDMGreeter *ldm)
{
    lightdm_suspend (NULL);
}

void
hibernate_cb (GtkWidget *widget, LightDMGreeter *ldm)
{
    lightdm_hibernate (NULL);
}

void
restart_cb (GtkWidget *widget, LightDMGreeter *ldm)
{
    if (show_power_prompt ("restart", "view-refresh-symbolic",
                           _("Restart"),
                           _("Are you sure you want to close all programs and restart the computer?")))
        lightdm_restart (NULL);
}

void
shutdown_cb (GtkWidget *widget, LightDMGreeter *ldm)
{
    if (show_power_prompt ("shutdown", "system-shutdown-symbolic",
                           _("Shut Down"),
                           _("Are you sure you want to close all programs and shut down the computer?")))
        lightdm_shutdown (NULL);
}

static void
set_login_button_label (LightDMGreeter *ldm, const gchar *username)
{
    LightDMUser *user;
    gboolean logged_in = FALSE;

    user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
    if (user)
        logged_in = lightdm_user_get_logged_in (user);
    if (logged_in)
        gtk_button_set_label (login_button, _("Unlock"));
    else
        gtk_button_set_label (login_button, _("Log In"));
    /* and disable the session and language widgets */
    gtk_widget_set_sensitive (GTK_WIDGET (session_menuitem), !logged_in);
    gtk_widget_set_sensitive (GTK_WIDGET (language_menuitem), !logged_in);
}

static guint set_user_background_delayed_id = 0;

static gboolean
set_user_background_delayed_cb (const gchar *value)
{
    greeter_background_set_custom_background (greeter_background, value);
    set_user_background_delayed_id = 0;
    return G_SOURCE_REMOVE;
}

static void
set_user_background (const gchar *user_name)
{
    const gchar *value = NULL;
    if (user_name)
    {
        LightDMUser *user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), user_name);
        if (user)
            value = lightdm_user_get_background (user);
    }

    if (set_user_background_delayed_id)
    {
        g_source_remove (set_user_background_delayed_id);
        set_user_background_delayed_id = 0;
    }

    if (!value)
        greeter_background_set_custom_background (greeter_background, NULL);
    else
    {
        /* Small delay before changing background */
        set_user_background_delayed_id = g_timeout_add_full (G_PRIORITY_DEFAULT, USER_BACKGROUND_DELAY,
                                                             (GSourceFunc)set_user_background_delayed_cb,
                                                             g_strdup (value), g_free);
    }
}

static void
start_authentication (const gchar *username)
{
    cancelling = FALSE;
    prompted = FALSE;
    password_prompted = FALSE;
    prompt_active = FALSE;

    if (pending_questions)
    {
        g_slist_free_full (pending_questions, (GDestroyNotify) pam_message_finalize);
        pending_questions = NULL;
    }

    config_set_string (STATE_SECTION_GREETER, STATE_KEY_LAST_USER, username);

    if (g_strcmp0 (username, "*other") == 0)
    {
        gtk_widget_show (GTK_WIDGET (username_entry));
        gtk_widget_show (GTK_WIDGET (cancel_button));
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_authenticate (greeter, NULL, NULL);
#else
        lightdm_greeter_authenticate (greeter, NULL);
#endif

		if (lightdm_greeter_get_lock_hint (greeter))
		{
			GList * items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
			for (GList * item = items; item; item = item->next)
			{
				LightDMUser *user = item->data;
				if( lightdm_user_get_logged_in (user))
				{
					gtk_entry_set_text (username_entry,lightdm_user_get_name(user));
					break;
				}
			}
		}
    }
    else if (g_strcmp0 (username, "*guest") == 0)
    {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_authenticate_as_guest (greeter, NULL);
#else
        lightdm_greeter_authenticate_as_guest (greeter);
#endif
    }
    else
    {
        LightDMUser *user;

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
        {
            if (!current_session)
                set_session (lightdm_user_get_session (user));
            if (!current_language)
                set_language (lightdm_user_get_language (user));
        }
        else
        {
            set_session (NULL);
            set_language (NULL);
        }
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_authenticate (greeter, username, NULL);
#else
        lightdm_greeter_authenticate (greeter, username);
#endif
    }
}

static void
cancel_authentication (void)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean other = FALSE;

    if (pending_questions)
    {
        g_slist_free_full (pending_questions, (GDestroyNotify) pam_message_finalize);
        pending_questions = NULL;
    }

    /* If in authentication then stop that first */
    cancelling = FALSE;
    if (lightdm_greeter_get_in_authentication (greeter))
    {
        cancelling = TRUE;
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_cancel_authentication (greeter, NULL);
#else
        lightdm_greeter_cancel_authentication (greeter);
#endif
        set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);
    }

    /* Make sure password entry is back to normal */
    gtk_entry_set_visibility (password_entry, FALSE);

    /* Force refreshing the prompt_box for "Other" */
    model = gtk_combo_box_get_model (user_combo);

    if (gtk_combo_box_get_active_iter (user_combo, &iter))
    {
        gchar *user;

        gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &user, -1);
        other = (g_strcmp0 (user, "*other") == 0);
        g_free (user);
    }

    /* Start a new login or return to the user list */
    if (other || lightdm_greeter_get_hide_users_hint (greeter))
        start_authentication ("*other");
    else
        gtk_widget_grab_focus (GTK_WIDGET (user_combo));
}

static void
start_session (void)
{
    gchar *language;
    gchar *session;

    language = get_language ();
    if (language)
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_set_language (greeter, language, NULL);
#else
        lightdm_greeter_set_language (greeter, language);
#endif
    g_free (language);

    session = get_session ();

    /* Remember last choice */
    config_set_string (STATE_SECTION_GREETER, STATE_KEY_LAST_SESSION, session);

    greeter_background_save_xroot (greeter_background);

    if (!lightdm_greeter_start_session_sync (greeter, session, NULL))
    {
        set_message_label (LIGHTDM_MESSAGE_TYPE_ERROR, _("Failed to start session"));
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
    }
    g_free (session);
}

gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if ((event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down) &&
        gtk_widget_get_visible (GTK_WIDGET (user_combo)))
    {
        gboolean available;
        GtkTreeIter iter;
        GtkTreeModel *model = gtk_combo_box_get_model (user_combo);

        /* Back to username_entry if it is available */
        if (event->keyval == GDK_KEY_Up &&
            gtk_widget_get_visible (GTK_WIDGET (username_entry)) && widget == GTK_WIDGET (password_entry))
        {
            gtk_widget_grab_focus (GTK_WIDGET (username_entry));
            return TRUE;
        }

        if (!gtk_combo_box_get_active_iter (user_combo, &iter))
            return FALSE;

        if (event->keyval == GDK_KEY_Up)
            available = gtk_tree_model_iter_previous (model, &iter);
        else
            available = gtk_tree_model_iter_next (model, &iter);

        if (available)
            gtk_combo_box_set_active_iter (user_combo, &iter);

        return TRUE;
    }
    return FALSE;
}

gboolean
username_focus_out_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
username_focus_out_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    if (!g_strcmp0(gtk_entry_get_text (username_entry), "") == 0)
        start_authentication (gtk_entry_get_text (username_entry));
    return FALSE;
}

gboolean
username_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
username_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    /* Acts as password_entry */
    if (event->keyval == GDK_KEY_Up)
        return password_key_press_cb (widget, event, user_data);
    /* Enter activates the password entry */
    else if (event->keyval == GDK_KEY_Return && gtk_widget_get_visible (GTK_WIDGET (password_entry)))
    {
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        return TRUE;
    }
    else
        return FALSE;
}

gboolean
menubar_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
menubar_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    switch (event->keyval)
    {
    case GDK_KEY_Tab: case GDK_KEY_Escape:
    case GDK_KEY_Super_L: case GDK_KEY_Super_R:
    case GDK_KEY_F9: case GDK_KEY_F10:
    case GDK_KEY_F11: case GDK_KEY_F12:
        gtk_menu_shell_cancel (GTK_MENU_SHELL (menubar));
        return TRUE;
    default:
        return FALSE;
    };
}

gboolean
login_window_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
login_window_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    GtkWidget *item = NULL;

    if (event->keyval == GDK_KEY_F9)
        item = session_menuitem;
    else if (event->keyval == GDK_KEY_F10)
        item = language_menuitem;
    else if (event->keyval == GDK_KEY_F11)
        item = a11y_menuitem;
    else if (event->keyval == GDK_KEY_F12)
        item = power_menuitem;
    else if (event->keyval != GDK_KEY_Escape &&
             event->keyval != GDK_KEY_Super_L &&
             event->keyval != GDK_KEY_Super_R)
        return FALSE;

    if (GTK_IS_MENU_ITEM (item) && gtk_widget_is_sensitive (item) && gtk_widget_get_visible (item))
        gtk_menu_shell_select_item (GTK_MENU_SHELL (menubar), item);
    else
        gtk_menu_shell_select_first (GTK_MENU_SHELL (menubar), TRUE);
    return TRUE;
}

static void
set_displayed_user (LightDMGreeter *ldm, const gchar *username)
{
    gchar *user_tooltip;
    LightDMUser *user;

    if (g_strcmp0 (username, "*other") == 0)
    {
        gtk_widget_show (GTK_WIDGET (username_entry));
        gtk_widget_show (GTK_WIDGET (cancel_button));
        user_tooltip = g_strdup (_("Other"));
    }
    else
    {
        gtk_widget_hide (GTK_WIDGET (username_entry));
        gtk_widget_hide (GTK_WIDGET (cancel_button));
        user_tooltip = g_strdup (username);
    }

    /* At this moment we do not have information about possible prompts
     * for current user (except *guest). So, password_entry.visible changed in:
     *   auth_complete_cb
     *   process_prompts
     *   and here - for *guest */

    if (g_strcmp0 (username, "*guest") == 0)
    {
        user_tooltip = g_strdup (_("Guest Session"));
        gtk_widget_hide (GTK_WIDGET (password_entry));
        gtk_widget_grab_focus (GTK_WIDGET (user_combo));
    }

    set_login_button_label (ldm, username);
    set_user_background (username);
    set_user_image (username);
    user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
    if (user)
    {
        set_language (lightdm_user_get_language (user));
        set_session (lightdm_user_get_session (user));
    }
    else
    {
        set_language (lightdm_language_get_code (lightdm_get_language ()));
        set_session (lightdm_greeter_get_default_session_hint (ldm));
    }
    gtk_widget_set_tooltip_text (GTK_WIDGET (user_combo), user_tooltip);
    start_authentication (username);
    g_free (user_tooltip);
}

void user_combobox_active_changed_cb (GtkComboBox *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
user_combobox_active_changed_cb (GtkComboBox *widget, LightDMGreeter *ldm)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_combo_box_get_model (user_combo);

    if (gtk_combo_box_get_active_iter (user_combo, &iter))
    {
        gchar *user;

        gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &user, -1);

        set_displayed_user (ldm, user);

        g_free (user);
    }
    set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);
}

void login_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
login_cb (GtkWidget *widget)
{
    /* Reset to default screensaver values */
    if (lightdm_greeter_get_lock_hint (greeter))
        XSetScreenSaver (gdk_x11_display_get_xdisplay (gdk_display_get_default ()), timeout, interval, prefer_blanking, allow_exposures);

    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), FALSE);
    set_message_label (LIGHTDM_MESSAGE_TYPE_INFO, NULL);
    prompt_active = FALSE;

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
    {
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
        lightdm_greeter_respond (greeter, gtk_entry_get_text (password_entry), NULL);
#else
        lightdm_greeter_respond (greeter, gtk_entry_get_text (password_entry));
#endif
        /* If we have questions pending, then we continue processing
         * those, until we are done. (Otherwise, authentication will
         * not complete.) */
        if (pending_questions)
            process_prompts (greeter);
    }
    else
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
}

void cancel_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
cancel_cb (GtkWidget *widget)
{
    cancel_authentication ();
}

gboolean
user_combo_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
user_combo_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if (event->keyval == GDK_KEY_Return)
    {
        if (gtk_widget_get_visible (GTK_WIDGET (username_entry)))
            gtk_widget_grab_focus (GTK_WIDGET (username_entry));
        else if (gtk_widget_get_visible (GTK_WIDGET (password_entry)))
            gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        else
            login_cb (GTK_WIDGET (login_button));
        return TRUE;
    }
    return FALSE;
}

static void
show_prompt_cb (LightDMGreeter *ldm, const gchar *text, LightDMPromptType type)
{
    PAMConversationMessage *message_obj = g_new (PAMConversationMessage, 1);
    if (message_obj)
    {
        message_obj->is_prompt = TRUE;
        message_obj->type.prompt = type;
        message_obj->text = g_strdup (text);
        pending_questions = g_slist_append (pending_questions, message_obj);
    }

    if (!prompt_active)
        process_prompts (ldm);
}

static void
show_message_cb (LightDMGreeter *ldm, const gchar *text, LightDMMessageType type)
{
    PAMConversationMessage *message_obj = g_new (PAMConversationMessage, 1);
    if (message_obj)
    {
        message_obj->is_prompt = FALSE;
        message_obj->type.message = type;
        message_obj->text = g_strdup (text);
        pending_questions = g_slist_append (pending_questions, message_obj);
    }

    if (!prompt_active)
        process_prompts (ldm);
}

static void
timed_autologin_cb (LightDMGreeter *ldm)
{
    /* Don't trigger autologin if user locks screen with light-locker (thanks to Andrew P.). */
    if (!lightdm_greeter_get_lock_hint (ldm))
    {
        /* Select autologin-session if configured */
        if (lightdm_greeter_get_hint (greeter, "autologin-session"))
        {
            set_session (lightdm_greeter_get_hint (greeter, "autologin-session"));
        }

        if (lightdm_greeter_get_is_authenticated (ldm))
        {
            /* Configured autologin user may be already selected in user list. */
            if (lightdm_greeter_get_authentication_user (ldm))
                /* Selected user matches configured autologin-user option. */
                start_session ();
            else if (lightdm_greeter_get_autologin_guest_hint (ldm))
                /* "Guest session" is selected and autologin-guest is enabled. */
                start_session ();
            else if (lightdm_greeter_get_autologin_user_hint (ldm))
            {
                /* "Guest session" is selected, but autologin-user is configured. */
                start_authentication (lightdm_greeter_get_autologin_user_hint (ldm));
                prompted = TRUE;
            }
        }
        else
#ifdef HAVE_LIBLIGHTDMGOBJECT_1_19_2
            lightdm_greeter_authenticate_autologin (ldm, NULL);
#else
            lightdm_greeter_authenticate_autologin (ldm);
#endif
    }
}

static void
authentication_complete_cb (LightDMGreeter *ldm)
{
    prompt_active = FALSE;
    gtk_entry_set_text (password_entry, "");

    if (cancelling)
    {
        cancel_authentication ();
        return;
    }

    if (pending_questions)
    {
        g_slist_free_full (pending_questions, (GDestroyNotify) pam_message_finalize);
        pending_questions = NULL;
    }

    if (lightdm_greeter_get_is_authenticated (ldm))
    {
        if (prompted)
            start_session ();
        else
        {
            gtk_widget_hide (GTK_WIDGET (password_entry));
            gtk_widget_grab_focus (GTK_WIDGET (user_combo));
        }
    }
    else
    {
        /* If an error message is already printed we do not print it this statement
         * The error message probably comes from the PAM module that has a better knowledge
         * of the failure. */
        gboolean have_pam_error = !message_label_is_empty () &&
                                  gtk_info_bar_get_message_type (info_bar) != GTK_MESSAGE_ERROR;
        if (prompted)
        {
            if (!have_pam_error)
                set_message_label (LIGHTDM_MESSAGE_TYPE_ERROR, _("Incorrect password, please try again"));
            start_authentication (lightdm_greeter_get_authentication_user (ldm));
        }
        else
        {
            g_warning ("Failed to authenticate");
            if (!have_pam_error)
                set_message_label (LIGHTDM_MESSAGE_TYPE_ERROR, _("Failed to authenticate"));
        }
    }
}

static void
user_added_cb (LightDMUserList *user_list, LightDMUser *user, LightDMGreeter *ldm)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean logged_in = FALSE;

    model = gtk_combo_box_get_model (user_combo);

    logged_in = lightdm_user_get_logged_in (user);

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, lightdm_user_get_name (user),
                        1, lightdm_user_get_display_name (user),
                        2, logged_in ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                        -1);
}

static gboolean
get_user_iter (const gchar *username, GtkTreeIter *iter)
{
    GtkTreeModel *model;

    model = gtk_combo_box_get_model (user_combo);

    if (!gtk_tree_model_get_iter_first (model, iter))
        return FALSE;
    do
    {
        gchar *name;
        gboolean matched;

        gtk_tree_model_get (model, iter, 0, &name, -1);
        matched = g_strcmp0 (name, username) == 0;
        g_free (name);
        if (matched)
            return TRUE;
    } while (gtk_tree_model_iter_next (model, iter));

    return FALSE;
}

static void
user_changed_cb (LightDMUserList *user_list, LightDMUser *user, LightDMGreeter *ldm)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean logged_in = FALSE;

    if (!get_user_iter (lightdm_user_get_name (user), &iter))
        return;
    logged_in = lightdm_user_get_logged_in (user);

    model = gtk_combo_box_get_model (user_combo);

    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, lightdm_user_get_name (user),
                        1, lightdm_user_get_display_name (user),
                        2, logged_in ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                        -1);
}

static void
user_removed_cb (LightDMUserList *user_list, LightDMUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!get_user_iter (lightdm_user_get_name (user), &iter))
        return;

    model = gtk_combo_box_get_model (user_combo);
    gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

static void
load_user_list (void)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;

    const GList  *items;
    const GList  *item;

    const gchar  *selected_user;
    gchar        *last_user;
    gboolean      logged_in = FALSE;

    g_signal_connect (lightdm_user_list_get_instance (), "user-added", G_CALLBACK (user_added_cb), greeter);
    g_signal_connect (lightdm_user_list_get_instance (), "user-changed", G_CALLBACK (user_changed_cb), greeter);
    g_signal_connect (lightdm_user_list_get_instance (), "user-removed", G_CALLBACK (user_removed_cb), NULL);
    model = gtk_combo_box_get_model (user_combo);
    items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
    for (item = items; item; item = item->next)
    {
        LightDMUser *user = item->data;
        logged_in = lightdm_user_get_logged_in (user);

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, lightdm_user_get_name (user),
                            1, lightdm_user_get_display_name (user),
                            2, logged_in ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                            -1);
    }
    if (lightdm_greeter_get_has_guest_account_hint (greeter))
    {
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, "*guest",
                            1, _("Guest Session"),
                            2, PANGO_WEIGHT_NORMAL,
                            -1);
    }

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, "*other",
                        1, _("Other..."),
                        2, PANGO_WEIGHT_NORMAL,
                        -1);

    last_user = config_get_string (STATE_SECTION_GREETER, STATE_KEY_LAST_USER, NULL);

    if (lightdm_greeter_get_select_user_hint (greeter))
        selected_user = lightdm_greeter_get_select_user_hint (greeter);
    else if (lightdm_greeter_get_select_guest_hint (greeter))
        selected_user = "*guest";
    else if (last_user)
        selected_user = last_user;
    else
        selected_user = NULL;

    if (gtk_tree_model_get_iter_first (model, &iter))
    {
        gchar *name;
        gboolean matched = FALSE;

        if (selected_user)
        {
            do
            {
                gtk_tree_model_get (model, &iter, 0, &name, -1);
                matched = g_strcmp0 (name, selected_user) == 0;
                g_free (name);
                if (matched)
                {
                    gtk_combo_box_set_active_iter (user_combo, &iter);
                    set_displayed_user (greeter, selected_user);
                    break;
                }
            } while (gtk_tree_model_iter_next (model, &iter));
        }
        if (!matched)
        {
            gtk_tree_model_get_iter_first (model, &iter);
            gtk_tree_model_get (model, &iter, 0, &name, -1);
            gtk_combo_box_set_active_iter (user_combo, &iter);
            set_displayed_user (greeter, name);
            g_free (name);
        }
    }

    g_free (last_user);
}

static GdkFilterReturn
wm_window_filter (GdkXEvent *gxevent, GdkEvent *event, gpointer  data)
{
    XEvent *xevent = (XEvent*)gxevent;
    if (xevent->type == MapNotify)
    {
        GdkDisplay *display = gdk_x11_lookup_xdisplay (xevent->xmap.display);
        GdkWindow *win = gdk_x11_window_foreign_new_for_display (display, xevent->xmap.window);
        GdkWindowTypeHint win_type = gdk_window_get_type_hint (win);

        if (win_type != GDK_WINDOW_TYPE_HINT_COMBO &&
            win_type != GDK_WINDOW_TYPE_HINT_TOOLTIP &&
            win_type != GDK_WINDOW_TYPE_HINT_NOTIFICATION)
        /*
        if (win_type == GDK_WINDOW_TYPE_HINT_DESKTOP ||
            win_type == GDK_WINDOW_TYPE_HINT_DIALOG)
        */
            gdk_window_focus (win, GDK_CURRENT_TIME);
    }
    else if (xevent->type == UnmapNotify)
    {
        Window xwin;
        int revert_to = RevertToNone;

        XGetInputFocus (xevent->xunmap.display, &xwin, &revert_to);
        if (revert_to == RevertToNone)
            gdk_window_lower (gtk_widget_get_window (gtk_widget_get_toplevel (GTK_WIDGET (screen_overlay))));
    }

    return GDK_FILTER_CONTINUE;
}

static void
debug_log_handler (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{
    gchar *new_domain = NULL;
    if (log_level == G_LOG_LEVEL_DEBUG)
    {
        log_level = G_LOG_LEVEL_MESSAGE;
        if (log_domain)
            log_domain = new_domain = g_strdup_printf ("DEBUG/%s", log_domain);
        else
            log_domain = "DEBUG";
    }
    g_log_default_handler (log_domain, log_level, message, user_data);
    g_free (new_domain);
}

int
main (int argc, char **argv)
{
    GtkBuilder      *builder;
    GdkWindow       *root_window;
    GtkWidget       *image;

    GError          *error = NULL;

    const GList     *items;
    const GList     *item;
    GList           *menubar_items;

    gchar          **config_groups;
    gchar          **config_group;
    gchar          **a11y_states;

    gchar           *value;

    GtkCssProvider  *css_provider;
    GdkRGBA          lightdm_gtk_greeter_override_defaults;
    guint            fallback_css_priority = GTK_STYLE_PROVIDER_PRIORITY_APPLICATION;
    GtkIconTheme    *icon_theme;

    /* Prevent memory from being swapped out, as we are dealing with passwords */
    mlockall (MCL_CURRENT | MCL_FUTURE);

    g_message ("Starting %s (%s, %s)", PACKAGE_STRING, __DATE__, __TIME__);

    /* Disable global menus */
    g_unsetenv ("UBUNTU_MENUPROXY");

    /* LP: #1024482 */
    g_setenv ("GDK_CORE_DEVICE_EVENTS", "1", TRUE);

    /* Initialize i18n */
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    g_unix_signal_add (SIGTERM, (GSourceFunc)sigterm_cb, /* is_callback */ GINT_TO_POINTER (TRUE));

    default_user_icon = g_strdup("avatar-default");

    config_init ();

    if (config_get_bool (NULL, CONFIG_KEY_DEBUGGING, FALSE))
        g_log_set_default_handler (debug_log_handler, NULL);

    /* init gtk */
    gtk_init (&argc, &argv);

    /* Disabling GtkInspector shortcuts.
       It is still possible to run GtkInspector with GTK_DEBUG=interactive.
       Assume that user knows what he's doing. */
    if (!config_get_bool (NULL, CONFIG_KEY_DEBUGGING, FALSE))
    {
        GtkWidget *fake_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        GtkBindingSet *set = gtk_binding_set_by_class (G_OBJECT_GET_CLASS (fake_window));
        GtkBindingEntry *entry = NULL;
        GtkBindingSignal *signals = NULL;
        GSList *bindings = NULL;
        GSList *iter;

        for (entry = set->entries; entry; entry = entry->set_next)
        {
            for (signals = entry->signals; signals; signals = signals->next)
            {
                if (g_strcmp0 (signals->signal_name, "enable-debugging") == 0)
                {
                    bindings = g_slist_prepend (bindings, entry);
                    break;
                }
            }
        }

        for (iter = bindings; iter; iter = g_slist_next (iter))
        {
            entry = iter->data;
            gtk_binding_entry_remove (set, entry->keyval, entry->modifiers);
        }

        g_slist_free (bindings);
        gtk_widget_destroy (fake_window);
    }

#ifdef HAVE_LIBIDO
    g_debug ("Initializing IDO library");
    ido_init ();
#endif

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (timed_autologin_cb), NULL);
    if (!lightdm_greeter_connect_sync (greeter, NULL))
        return EXIT_FAILURE;

    /* Set default cursor */
    gdk_window_set_cursor (gdk_get_default_root_window (), gdk_cursor_new_for_display (gdk_display_get_default (), GDK_LEFT_PTR));

    /* Make the greeter behave a bit more like a screensaver if used as un/lock-screen by blanking the screen */
    if (lightdm_greeter_get_lock_hint (greeter))
    {
        Display *display = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
        XGetScreenSaver (display, &timeout, &interval, &prefer_blanking, &allow_exposures);
        XForceScreenSaver (display, ScreenSaverActive);
        XSetScreenSaver (display, config_get_int (NULL, CONFIG_KEY_SCREENSAVER_TIMEOUT, 60), 0,
                         ScreenSaverActive, DefaultExposures);
    }

    /* Set GTK+ settings */
    value = config_get_string (NULL, CONFIG_KEY_THEME, NULL);
    if (value)
    {
        g_debug ("[Configuration] Changing GTK+ theme to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-theme-name", &default_theme_name, NULL);
    g_debug ("[Configuration] GTK+ theme: '%s'", default_theme_name);

    value = config_get_string (NULL, CONFIG_KEY_ICON_THEME, NULL);
    if (value)
    {
        g_debug ("[Configuration] Changing icons theme to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-icon-theme-name", &default_icon_theme_name, NULL);
    g_debug ("[Configuration] Icons theme: '%s'", default_icon_theme_name);

    value = config_get_string (NULL, CONFIG_KEY_CURSOR_THEME, NULL);
    if (value)
    {
        g_debug ("[Configuration] Changing cursor theme to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-cursor-theme-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-cursor-theme-name", &default_cursor_theme_name, NULL);
    g_debug ("[Configuration] Cursor theme: '%s'", default_cursor_theme_name);

    if (config_has_key(NULL, CONFIG_KEY_CURSOR_THEME_SIZE))
    {
        g_object_set (gtk_settings_get_default (), "gtk-cursor-theme-size", config_get_int (NULL, CONFIG_KEY_CURSOR_THEME_SIZE, 16), NULL);
    }

    value = config_get_string (NULL, CONFIG_KEY_FONT, "Sans 10");
    if (value)
    {
        g_debug ("[Configuration] Changing font to '%s'", value);
        g_object_set (gtk_settings_get_default (), "gtk-font-name", value, NULL);
        g_free (value);
    }
    g_object_get (gtk_settings_get_default (), "gtk-font-name", &default_font_name, NULL);
    g_debug ("[Configuration] Font: '%s'", default_font_name);

    if (config_has_key (NULL, CONFIG_KEY_DPI))
        g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", 1024*config_get_int (NULL, CONFIG_KEY_DPI, 96), NULL);

    if (config_has_key (NULL, CONFIG_KEY_ANTIALIAS))
        g_object_set (gtk_settings_get_default (), "gtk-xft-antialias", config_get_bool (NULL, CONFIG_KEY_ANTIALIAS, FALSE), NULL);

    value = config_get_string (NULL, CONFIG_KEY_HINT_STYLE, NULL);
    if (value)
    {
        g_object_set (gtk_settings_get_default (), "gtk-xft-hintstyle", value, NULL);
        g_free (value);
    }

    value = config_get_string (NULL, CONFIG_KEY_RGBA, NULL);
    if (value)
    {
        g_object_set (gtk_settings_get_default (), "gtk-xft-rgba", value, NULL);
        g_free (value);
    }

    #ifdef AT_SPI_COMMAND
    if (config_has_key (NULL, CONFIG_KEY_AT_SPI_ENABLED) && config_get_bool (NULL, CONFIG_KEY_AT_SPI_ENABLED, TRUE) == FALSE)
    {
        // AT_SPI is user-disabled
    }
    else
    {
        spawn_line_pid (AT_SPI_COMMAND, G_SPAWN_SEARCH_PATH, NULL);
    }
    #endif

    #ifdef INDICATOR_SERVICES_COMMAND
    spawn_line_pid (INDICATOR_SERVICES_COMMAND, G_SPAWN_SEARCH_PATH, NULL);
    #endif

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_string (builder, lightdm_gtk_greeter_ui,
                                      lightdm_gtk_greeter_ui_length, &error))
    {
        g_warning ("Error loading UI: %s", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    /* Screen window */
    screen_overlay = GTK_OVERLAY (gtk_builder_get_object (builder, "screen_overlay"));
    screen_overlay_child = GTK_WIDGET (gtk_builder_get_object (builder, "screen_overlay_child"));

    /* Login window */
    login_window = GTK_WIDGET (gtk_builder_get_object (builder, "login_window"));
    user_image = GTK_IMAGE (gtk_builder_get_object (builder, "user_image"));
    user_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "user_combobox"));
    username_entry = GTK_ENTRY (gtk_builder_get_object (builder, "username_entry"));
    password_entry = GTK_ENTRY (gtk_builder_get_object (builder, "password_entry"));
    info_bar = GTK_INFO_BAR (gtk_builder_get_object (builder, "greeter_infobar"));
    message_label = GTK_LABEL (gtk_builder_get_object (builder, "message_label"));
    cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "cancel_button"));
    login_button = GTK_BUTTON (gtk_builder_get_object (builder, "login_button"));

    /* Panel window*/
    panel_window = GTK_WIDGET (gtk_builder_get_object (builder, "panel_window"));
    menubar = GTK_WIDGET (gtk_builder_get_object (builder, "menubar"));
    session_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "session_menuitem"));
    session_menu = GTK_MENU (gtk_builder_get_object (builder, "session_menu"));
    language_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "language_menuitem"));
    language_menu = GTK_MENU (gtk_builder_get_object (builder, "language_menu"));
    a11y_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "a11y_menuitem"));
    contrast_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "high_contrast_menuitem"));
    font_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "large_font_menuitem"));
    keyboard_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "keyboard_menuitem"));
    reader_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "reader_menuitem"));
    power_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "power_menuitem"));
    layout_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "layout_menuitem"));
    layout_menu = GTK_MENU (gtk_builder_get_object (builder, "layout_menu"));
    clock_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "clock_menuitem"));
    host_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "host_menuitem"));

    /* Power dialog */
    power_window = GTK_WIDGET (gtk_builder_get_object (builder, "power_window"));
    power_ok_button = GTK_BUTTON (gtk_builder_get_object (builder, "power_ok_button"));
    power_cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "power_cancel_button"));
    power_title = GTK_LABEL (gtk_builder_get_object (builder, "power_title"));
    power_text = GTK_LABEL (gtk_builder_get_object (builder, "power_text"));
    power_icon = GTK_IMAGE (gtk_builder_get_object (builder, "power_icon"));

    gtk_overlay_add_overlay (screen_overlay, login_window);
    gtk_overlay_add_overlay (screen_overlay, panel_window);
    gtk_overlay_add_overlay (screen_overlay, power_window);

    gtk_accel_map_add_entry ("<Login>/a11y/font", GDK_KEY_F1, 0);
    gtk_accel_map_add_entry ("<Login>/a11y/contrast", GDK_KEY_F2, 0);
    gtk_accel_map_add_entry ("<Login>/a11y/keyboard", GDK_KEY_F3, 0);
    gtk_accel_map_add_entry ("<Login>/a11y/reader", GDK_KEY_F4, 0);
    gtk_accel_map_add_entry ("<Login>/power/shutdown", GDK_KEY_F4, GDK_MOD1_MASK);

    init_indicators ();

    /* https://bugzilla.gnome.org/show_bug.cgi?id=710888
       > GtkInfoBar not shown after calling gtk_widget_show
       Assume they will fix it someday. */
    if (gtk_get_major_version () == 3 && gtk_get_minor_version () < 18)
    {
        GList *children = gtk_container_get_children (GTK_CONTAINER (info_bar));
        if (g_list_length (children) == 1 && GTK_IS_REVEALER (children->data))
            g_signal_connect_after(children->data, "notify::child-revealed", (GCallback)infobar_revealed_cb_710888, NULL);
        g_list_free (children);
    }

    /* Hide empty panel */
    menubar_items = gtk_container_get_children (GTK_CONTAINER (menubar));
    if (!menubar_items)
        gtk_widget_hide (GTK_WIDGET (panel_window));
    else
        g_list_free (menubar_items);

    if (config_get_bool (NULL, CONFIG_KEY_HIDE_USER_IMAGE, FALSE))
    {
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "user_image_border")));
        gtk_widget_hide (GTK_WIDGET (user_image));  /* Hide to mark image is disabled */
        gtk_widget_set_size_request (GTK_WIDGET (user_combo), 250, -1);
    }
    else
    {
        value = config_get_string (NULL, CONFIG_KEY_DEFAULT_USER_IMAGE, NULL);
        if (value)
        {
            if (value[0] == '#')
            {
                g_free (default_user_icon);
                default_user_icon = g_strdup (value + 1);
            }
            else
            {
                default_user_pixbuf = gdk_pixbuf_new_from_file_at_scale (value, 80, 80, FALSE, &error);
                if (!default_user_pixbuf)
                {
                    g_warning ("Failed to load default user image: %s", error->message);
                    g_clear_error (&error);
                }
            }
            g_free (value);
        }
    }

    icon_theme = gtk_icon_theme_get_default ();

    /* Session menu */
    if (gtk_widget_get_visible (session_menuitem))
    {
        GSList *sessions = NULL;

        if (gtk_icon_theme_has_icon (icon_theme, "document-properties-symbolic"))
            session_badge = gtk_image_new_from_icon_name ("document-properties-symbolic", GTK_ICON_SIZE_MENU);
        else
            session_badge = gtk_image_new_from_icon_name ("document-properties", GTK_ICON_SIZE_MENU);
        gtk_widget_show (session_badge);
        gtk_container_add (GTK_CONTAINER (session_menuitem), session_badge);

        items = lightdm_get_sessions ();
        for (item = items; item; item = item->next)
        {
            LightDMSession *session = item->data;
            GtkWidget *radiomenuitem;

            radiomenuitem = gtk_radio_menu_item_new_with_label (sessions, lightdm_session_get_name (session));
            g_object_set_data (G_OBJECT (radiomenuitem), SESSION_DATA_KEY, (gpointer) lightdm_session_get_key (session));
            sessions = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radiomenuitem));
            g_signal_connect (G_OBJECT (radiomenuitem), "activate", G_CALLBACK (session_selected_cb), NULL);
            gtk_menu_shell_append (GTK_MENU_SHELL (session_menu), radiomenuitem);
            gtk_widget_show (GTK_WIDGET (radiomenuitem));
        }
        set_session (NULL);
    }

    /* Language menu */
    if (gtk_widget_get_visible (language_menuitem))
    {
        GSList *languages = NULL;
        gchar *modifier;
        items = lightdm_get_languages ();
        for (item = items; item; item = item->next)
        {
            LightDMLanguage *language = item->data;
            const gchar *country, *code;
            gchar *label;
            GtkWidget *radiomenuitem;

            country = lightdm_language_get_territory (language);
            if (country)
                label = g_strdup_printf ("%s - %s", lightdm_language_get_name (language), country);
            else
                label = g_strdup (lightdm_language_get_name (language));

            code = lightdm_language_get_code (language);
            modifier = strchr (code, '@');
            if (modifier != NULL)
            {
                gchar *label_new = g_strdup_printf ("%s [%s]", label, modifier+1);
                g_free (label);
                label = label_new;
            }

            radiomenuitem = gtk_radio_menu_item_new_with_label (languages, label);
            g_object_set_data (G_OBJECT (radiomenuitem), LANGUAGE_DATA_CODE, (gpointer) code);
            languages = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radiomenuitem));
            g_signal_connect (G_OBJECT (radiomenuitem), "activate", G_CALLBACK (language_selected_cb), NULL);
            gtk_menu_shell_append (GTK_MENU_SHELL (language_menu), radiomenuitem);
            gtk_widget_show (GTK_WIDGET (radiomenuitem));
        }
        set_language (NULL);
    }

    /* a11y menu */
    if (gtk_widget_get_visible (a11y_menuitem))
    {
        if (gtk_icon_theme_has_icon (icon_theme, "preferences-desktop-accessibility-symbolic"))
            image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility-symbolic", GTK_ICON_SIZE_MENU);
        else
            image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_MENU);
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (a11y_menuitem), image);
    }

    value = config_get_string (NULL, CONFIG_KEY_KEYBOARD, NULL);
    if (value)
    {
        a11y_keyboard_command = menu_command_parse_extended ("keyboard", value, keyboard_menuitem, "onboard", "--xid");
        g_free (value);
    }
    gtk_widget_set_visible (keyboard_menuitem, a11y_keyboard_command != NULL);



    value = config_get_string (NULL, CONFIG_KEY_READER, NULL);
    if (value)
    {
        a11y_reader_command = menu_command_parse ("reader", value, reader_menuitem);
        g_free (value);
    }
    gtk_widget_set_visible (reader_menuitem, a11y_reader_command != NULL);

    /* Power menu */
    if (gtk_widget_get_visible (power_menuitem))
    {
        if (gtk_icon_theme_has_icon (icon_theme, "system-shutdown-symbolic"))
            image = gtk_image_new_from_icon_name ("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
        else
            image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (power_menuitem), image);

        suspend_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "suspend_menuitem")));
        hibernate_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "hibernate_menuitem")));
        restart_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "restart_menuitem")));
        shutdown_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "shutdown_menuitem")));

        g_signal_connect (G_OBJECT (power_menuitem),"activate", G_CALLBACK (power_menu_cb), NULL);
    }

    /* Layout menu */
    if (gtk_widget_get_visible (layout_menuitem))
    {
        #ifdef HAVE_LIBXKLAVIER
        xkl_engine = xkl_engine_get_instance (XOpenDisplay (NULL));
        if (xkl_engine)
        {
            xkl_engine_start_listen (xkl_engine, XKLL_TRACK_KEYBOARD_STATE);
            g_signal_connect (xkl_engine, "X-state-changed",
                              G_CALLBACK (xkl_state_changed_cb), NULL);
            g_signal_connect (xkl_engine, "X-config-changed",
                              G_CALLBACK (xkl_config_changed_cb), NULL);
            gdk_window_add_filter (NULL, (GdkFilterFunc) xkl_xevent_filter, NULL);

            /* refresh */
            XklConfigRec *config_rec = xkl_config_rec_new ();
            if (xkl_config_rec_get_from_server (config_rec, xkl_engine))
                xkl_config_rec_activate (config_rec, xkl_engine);
            g_object_unref (config_rec);
        }
        else
        {
            g_warning ("Failed to get XklEngine instance");
            gtk_widget_hide (layout_menuitem);
        }
        #endif
        update_layouts_menu ();
        update_layouts_menu_state ();
    }

    /* Host label */
    if (gtk_widget_get_visible (host_menuitem))
        gtk_menu_item_set_label (GTK_MENU_ITEM (host_menuitem), lightdm_get_hostname ());

    /* Clock label */
    if (gtk_widget_get_visible (clock_menuitem))
    {
        gtk_menu_item_set_label (GTK_MENU_ITEM (clock_menuitem), "");
        clock_label = gtk_bin_get_child (GTK_BIN (clock_menuitem));
        clock_format = config_get_string (NULL, CONFIG_KEY_CLOCK_FORMAT, "%a, %H:%M");
        clock_timeout_thread ();
        gdk_threads_add_timeout (1000, (GSourceFunc) clock_timeout_thread, NULL);
    }

    /* A bit of CSS */
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (css_provider, lightdm_gtk_greeter_css_application, lightdm_gtk_greeter_css_application_length, NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    css_provider = gtk_css_provider_new ();
    if (gtk_style_context_lookup_color (gtk_widget_get_style_context (GTK_WIDGET (login_window)),
                                        "lightdm-gtk-greeter-override-defaults",
                                        &lightdm_gtk_greeter_override_defaults))
        fallback_css_priority = GTK_STYLE_PROVIDER_PRIORITY_FALLBACK;
    gtk_css_provider_load_from_data (css_provider, lightdm_gtk_greeter_css_fallback, lightdm_gtk_greeter_css_fallback_length, NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (css_provider),
                                               fallback_css_priority);

    /* Background */
    greeter_background = greeter_background_new (GTK_WIDGET (screen_overlay));

    value = config_get_string (NULL, CONFIG_KEY_ACTIVE_MONITOR, NULL);
    greeter_background_set_active_monitor_config (greeter_background, value ? value : "#cursor");
    g_free (value);

    read_monitor_configuration (CONFIG_GROUP_DEFAULT, GREETER_BACKGROUND_DEFAULT);

    config_groups = config_get_groups (CONFIG_GROUP_MONITOR);
    for (config_group = config_groups; *config_group; ++config_group)
    {
        const gchar *name = *config_group + sizeof (CONFIG_GROUP_MONITOR);
        while (*name && g_ascii_isspace (*name))
            ++name;

        read_monitor_configuration (*config_group, name);
    }
    g_strfreev (config_groups);

    greeter_background_add_accel_group (greeter_background, GTK_ACCEL_GROUP (gtk_builder_get_object (builder, "a11y_accelgroup")));
    greeter_background_add_accel_group (greeter_background, GTK_ACCEL_GROUP (gtk_builder_get_object (builder, "power_accelgroup")));

    greeter_background_connect (greeter_background, gdk_screen_get_default ());

    if (lightdm_greeter_get_hide_users_hint (greeter))
    {
        set_user_image (NULL);
        start_authentication ("*other");
    }
    else
    {
        load_user_list ();
        gtk_widget_hide (GTK_WIDGET (cancel_button));
        gtk_widget_show (GTK_WIDGET (user_combo));
    }

    /* Windows positions */
    value = config_get_string (NULL, CONFIG_KEY_POSITION, NULL);
    g_object_set_data_full (G_OBJECT (login_window), WINDOW_DATA_POSITION, str_to_position (value, &WINDOW_POS_CENTER), g_free);
    g_free (value);


    gtk_widget_set_valign (panel_window, config_get_enum (NULL, CONFIG_KEY_PANEL_POSITION, GTK_ALIGN_START,
                                                          "bottom", GTK_ALIGN_END,
                                                          "top", GTK_ALIGN_START, NULL));

    if (a11y_keyboard_command)
    {
        value = config_get_string (NULL, CONFIG_KEY_KEYBOARD_POSITION, NULL);
        g_object_set_data_full (G_OBJECT (a11y_keyboard_command->widget), WINDOW_DATA_POSITION, str_to_position (value, &KEYBOARD_POSITION), g_free);
        g_free (value);
    }

    gtk_builder_connect_signals (builder, greeter);

    a11y_states = config_get_string_list (NULL, CONFIG_KEY_A11Y_STATES, NULL);
    if (a11y_states && *a11y_states)
    {
        gpointer a11y_item;
        gchar **values_iter;

        GHashTable *a11y_items = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);
        g_hash_table_insert (a11y_items, g_strdup("contrast"), contrast_menuitem);
        g_hash_table_insert (a11y_items, g_strdup("font"), font_menuitem);
        g_hash_table_insert (a11y_items, g_strdup("keyboard"), keyboard_menuitem);
        g_hash_table_insert (a11y_items, g_strdup("reader"), reader_menuitem);

        for (values_iter = a11y_states; *values_iter; ++values_iter)
        {
            value = *values_iter;
            switch (value[0])
            {
            case '-':
                continue;
            case '+':
                if (g_hash_table_lookup_extended (a11y_items, &value[1], NULL, &a11y_item) &&
                    gtk_widget_get_visible (GTK_WIDGET (a11y_item)))
                        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (a11y_item), TRUE);
                break;
            default:
                if (g_hash_table_lookup_extended (a11y_items, value, NULL, &a11y_item) &&
                    gtk_widget_get_visible (GTK_WIDGET (a11y_item)))
                {
                    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (a11y_item),
                                                    config_get_bool (STATE_SECTION_A11Y, value, FALSE));
                    g_signal_connect (G_OBJECT (a11y_item), "toggled", G_CALLBACK (a11y_menuitem_toggled_cb), g_strdup (value));
                }
            }
        }
        g_hash_table_unref (a11y_items);
    }
    g_strfreev (a11y_states);

    /* There is no window manager, so we need to implement some of its functionality */
    root_window = gdk_get_default_root_window ();
    gdk_window_set_events (root_window, gdk_window_get_events (root_window) | GDK_SUBSTRUCTURE_MASK);
    gdk_window_add_filter (root_window, wm_window_filter, NULL);

    gtk_widget_show (GTK_WIDGET (screen_overlay));

    g_debug ("Run Gtk loop...");
    gtk_main ();
    g_debug ("Gtk loop exits");

    sigterm_cb (/* is_callback */ GINT_TO_POINTER (FALSE));

    {
        Display *display = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
        int screen = XDefaultScreen (display);
        Window w = RootWindow (display, screen);
        Atom id = XInternAtom (display, "AT_SPI_BUS", True);
        if (id != None)
            {
                XDeleteProperty (display, w, id);
                XSync (display, FALSE);
            }
    }

    return EXIT_SUCCESS;
}
