/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
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
#if GTK_CHECK_VERSION (3, 0, 0)
#include <gtk/gtkx.h>
#else
#include <gdk/gdkkeysyms.h>
#endif
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

#include "src/greetermenubar.h"
#include "src/greeterbackground.h"
#include "src/lightdm-gtk-greeter-ui.h"

#if GTK_CHECK_VERSION (3, 0, 0)
#include <src/lightdm-gtk-greeter-css-fallback.h>
#include <src/lightdm-gtk-greeter-css-application.h>
#endif

static LightDMGreeter *greeter;
static GKeyFile *state;
static gchar *state_filename;

/* Defaults */
static gchar *default_font_name, *default_theme_name, *default_icon_theme_name;

/* Panel Widgets */
static GtkWindow *panel_window;
static GtkWidget *menubar;
static GtkWidget *power_menuitem, *session_menuitem, *language_menuitem, *a11y_menuitem,
                 *layout_menuitem, *session_badge;
static GtkWidget *suspend_menuitem, *hibernate_menuitem, *restart_menuitem, *shutdown_menuitem;
static GtkWidget *keyboard_menuitem, *clock_menuitem, *clock_label, *host_menuitem;
static GtkMenu *session_menu, *language_menu, *layout_menu;

/* Login Window Widgets */
static GtkWindow *login_window;
static GtkImage *user_image;
static GtkComboBox *user_combo;
static GtkEntry *username_entry, *password_entry;
static GtkLabel *message_label;
static GtkInfoBar *info_bar;
static GtkButton *cancel_button, *login_button;

static gchar *clock_format;
static gchar **a11y_keyboard_command;
static GPid a11y_kbd_pid = 0;
static GError *a11y_keyboard_error;
static GtkWindow *onboard_window;

/* Pending Questions */
static GSList *pending_questions = NULL;

/* Current choices */
static gchar *current_session;
static gchar *current_language;

/* Screensaver values */
int timeout, interval, prefer_blanking, allow_exposures;

static GreeterBackground *greeter_background;

static gboolean cancelling = FALSE, prompted = FALSE;
static gboolean prompt_active = FALSE, password_prompted = FALSE;
#if GTK_CHECK_VERSION (3, 0, 0)
#else
static GdkRegion *window_region = NULL;
#endif

typedef struct
{
  gboolean is_prompt;
  union {
    LightDMMessageType message;
    LightDMPromptType prompt;
  } type;
  gchar *text;
} PAMConversationMessage;

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
} WindowPosition;

static const WindowPosition WINDOW_POS_CENTER = {.x = {50, +1, TRUE, 0}, .y = {50, +1, TRUE, 0}};
static const WindowPosition WINDOW_POS_TOP_LEFT = {.x = {0, +1, FALSE, -1}, .y = {0, +1, FALSE, -1}};
static WindowPosition main_window_pos;
static WindowPosition panel_window_pos;

static GdkPixbuf* default_user_pixbuf = NULL;
static gchar* default_user_icon = "avatar-default";

static const gchar *LAYOUT_DATA_LABEL = "layout-label";
#ifdef HAVE_LIBXKLAVIER
static const gchar *LAYOUT_DATA_GROUP = "layout-group";
#else
static const gchar *LAYOUT_DATA_NAME = "layout-name";
#endif

#ifdef HAVE_LIBXKLAVIER
static XklEngine *xkl_engine;
#endif

typedef enum
{
    PANEL_ITEM_INDICATOR,
    PANEL_ITEM_SPACER,
    PANEL_ITEM_SEPARATOR,
    PANEL_ITEM_TEXT
} GreeterPanelItemType;

#if GTK_CHECK_VERSION (3, 0, 0)
static const gchar *PANEL_ITEM_STYLE = "panel-item";
static const gchar *PANEL_ITEM_STYLE_HOVERED = "panel-item-hovered";

static const gchar *PANEL_ITEM_STYLES[] = 
{
    "panel-item-indicator",
    "panel-item-spacer",
    "panel-item-separator",
    "panel-item-text"
};
#endif

static const gchar *PANEL_ITEM_DATA_INDEX = "panel-item-data-index";

#ifdef HAVE_LIBINDICATOR
static const gchar *INDICATOR_ITEM_DATA_OBJECT    = "indicator-item-data-object";
static const gchar *INDICATOR_ITEM_DATA_ENTRY     = "indicator-item-data-entry";
static const gchar *INDICATOR_ITEM_DATA_BOX       = "indicator-item-data-box";
static const gchar *INDICATOR_DATA_MENUITEMS      = "indicator-data-menuitems";
#endif

static void
pam_message_finalize (PAMConversationMessage *message)
{
    g_free (message->text);
    g_free (message);
}

#if GTK_CHECK_VERSION (3, 0, 0)
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
#endif

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

    #if GTK_CHECK_VERSION (3, 0, 0)
    gtk_style_context_add_class (gtk_widget_get_style_context (widget), PANEL_ITEM_STYLE);
    gtk_style_context_add_class (gtk_widget_get_style_context (widget), PANEL_ITEM_STYLES[item_type]);
    if (item_type == PANEL_ITEM_INDICATOR)
    {
        g_signal_connect (G_OBJECT (widget), "enter-notify-event",
                          G_CALLBACK (panel_item_enter_notify_cb), GINT_TO_POINTER(TRUE));
        g_signal_connect (G_OBJECT (widget), "leave-notify-event",
                          G_CALLBACK (panel_item_enter_notify_cb), GINT_TO_POINTER(FALSE));
    }
    #endif

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

    g_signal_emit_by_name (io, "scroll", 1, event->direction);
    g_signal_emit_by_name (io, "scroll-entry", entry, 1, event->direction);

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
indicator_entry_create_menuitem (IndicatorObject *io, IndicatorObjectEntry *entry, GtkWidget *menubar)
{
    GtkWidget *box, *menuitem;
    gint index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (io), PANEL_ITEM_DATA_INDEX));

#if GTK_CHECK_VERSION (3, 0, 0)
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
#else
    box = gtk_hbox_new (FALSE, 0);
#endif
    menuitem = gtk_menu_item_new ();

    gtk_widget_add_events(GTK_WIDGET(menuitem), GDK_SCROLL_MASK);

    g_object_set_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_BOX, box);
    g_object_set_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_OBJECT, io);
    g_object_set_data (G_OBJECT (menuitem), INDICATOR_ITEM_DATA_ENTRY, entry);
    g_object_set_data (G_OBJECT (menuitem), PANEL_ITEM_DATA_INDEX, GINT_TO_POINTER (index));

    g_signal_connect (G_OBJECT (menuitem), "activate", G_CALLBACK (indicator_entry_activated_cb), NULL);
    g_signal_connect (G_OBJECT (menuitem), "scroll-event", G_CALLBACK (indicator_entry_scrolled_cb), NULL);

    if (entry->image)
        gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(entry->image), FALSE, FALSE, 1);

    if (entry->label)
        gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(entry->label), FALSE, FALSE, 1);

    if (entry->menu)
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), GTK_WIDGET (entry->menu));

    gtk_container_add (GTK_CONTAINER (menuitem), box);
    gtk_widget_show (box);
    panel_add_item(menuitem, index, PANEL_ITEM_INDICATOR);

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
        for (lp = entries; lp; lp = g_list_next (entry))
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
    g_setenv (key, value, TRUE);

    GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       "org.freedesktop.DBus",
                                                       "/org/freedesktop/DBus",
                                                       "org.freedesktop.DBus",
                                                       NULL, NULL);
    GVariant *result;
    GVariantBuilder *builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
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
    GtkAccelKey key;
    const gchar *accel_path = gtk_menu_item_get_accel_path (GTK_MENU_ITEM (item));

    if (accel_path && gtk_accel_map_lookup_entry (accel_path, &key))
    {
        GClosure *closure = g_cclosure_new (G_CALLBACK (menu_item_accel_closure_cb), item, NULL);
        gtk_accel_group_connect (gtk_menu_get_accel_group (GTK_MENU (gtk_widget_get_parent (item))),
                                 key.accel_key, key.accel_mods, key.accel_flags, closure);
        g_closure_unref (closure);
    }

    GtkWidget* submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (item));
    if(submenu)
        gtk_container_foreach (GTK_CONTAINER (submenu), (GtkCallback)reassign_menu_item_accel, NULL);
}

static void
#ifdef START_INDICATOR_SERVICES
init_indicators (GKeyFile* config, GPid* indicator_pid, GPid* spi_pid)
#else
init_indicators (GKeyFile* config)
#endif
{
    gchar **names = NULL;
    gsize length = 0;
    guint i;
    GHashTable *builtin_items = NULL;
    GHashTableIter iter;
    gpointer iter_value;
    #ifdef HAVE_LIBINDICATOR
    gboolean inited = FALSE;
    #endif
    gboolean fallback = FALSE;

    const gchar *DEFAULT_LAYOUT[] = {"~host", "~spacer", "~clock", "~spacer",
                                     "~session", "~language", "~a11y", "~power", NULL};

#ifdef START_INDICATOR_SERVICES
    GError *error = NULL;
    gchar *AT_SPI_CMD[] = {"/usr/lib/at-spi2-core/at-spi-bus-launcher", "--launch-immediately", NULL};
    gchar *INDICATORS_CMD[] = {"init", "--user", "--startup-event", "indicator-services-start", NULL};
#endif

    if (g_key_file_has_key (config, "greeter", "indicators", NULL))
    {   /* no option = default list, empty value = empty list */
        names = g_key_file_get_string_list (config, "greeter", "indicators", &length, NULL);
    }
    else if (g_key_file_has_key (config, "greeter", "show-indicators", NULL))
    {   /* fallback mode: no option = empty value = default list */
        names = g_key_file_get_string_list (config, "greeter", "show-indicators", &length, NULL);
        if (length == 0)
            fallback = TRUE;
    }

    if (!names || fallback)
    {
        names = (gchar**)DEFAULT_LAYOUT;
        length = g_strv_length (names);
    }

    builtin_items = g_hash_table_new (g_str_hash, g_str_equal);

    g_hash_table_insert (builtin_items, "~power", power_menuitem);
    g_hash_table_insert (builtin_items, "~session", session_menuitem);
    g_hash_table_insert (builtin_items, "~language", language_menuitem);
    g_hash_table_insert (builtin_items, "~a11y", a11y_menuitem);
    g_hash_table_insert (builtin_items, "~layout", layout_menuitem);
    g_hash_table_insert (builtin_items, "~host", host_menuitem);
    g_hash_table_insert (builtin_items, "~clock", clock_menuitem);

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
                #if GTK_CHECK_VERSION (3, 0, 0)
                GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
                #else
                GtkWidget *separator = gtk_vseparator_new ();
                #endif
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
                #if GTK_CHECK_VERSION (3, 0, 0)
                gtk_widget_set_hexpand (iter_value, TRUE);
                #else
                g_object_set_data(G_OBJECT(iter_value), GREETER_MENU_BAR_EXPAND_PROP, "1");
                #endif
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
        gchar* path = NULL;
        IndicatorObject* io = NULL;

        if (!inited)
        {
            /* Set indicators to run with reduced functionality */
            greeter_set_env ("INDICATOR_GREETER_MODE", "1");
            /* Don't allow virtual file systems? */
            greeter_set_env ("GIO_USE_VFS", "local");
            greeter_set_env ("GVFS_DISABLE_FUSE", "1");

            #ifdef START_INDICATOR_SERVICES
            if (!g_spawn_async (NULL, AT_SPI_CMD, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, spi_pid, &error))
                g_warning ("Failed to run \"at-spi-bus-launcher\": %s", error->message);
            g_clear_error (&error);

            if (!g_spawn_async (NULL, INDICATORS_CMD, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, indicator_pid, &error))
                g_warning ("Failed to run \"indicator-services\": %s", error->message);
            g_clear_error (&error);
            #endif
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

static gboolean
is_valid_session (GList* items, const gchar* session)
{
    for (; items; items = g_list_next (items))
        if (g_strcmp0 (session, lightdm_session_get_key (items->data)) == 0)
            return TRUE;
    return FALSE;
}

static gchar *
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
        last_session = g_key_file_get_value (state, "greeter", "last-session", NULL);
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
            for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next(menu_iter))
                if (g_strcmp0 (session, g_object_get_data (G_OBJECT (menu_iter->data), "session-key")) == 0)
                {
#if GTK_CHECK_VERSION (3, 0, 0)
                    /* Set menuitem-image to session-badge */
                    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
                    gchar* session_name = g_ascii_strdown (session, -1);
                    gchar* icon_name = g_strdup_printf ("%s_badge-symbolic", session_name);
                    g_free (session_name);
                    if (gtk_icon_theme_has_icon(icon_theme, icon_name))
                        gtk_image_set_from_icon_name (GTK_IMAGE(session_badge), icon_name, GTK_ICON_SIZE_MENU);
                    else
                        gtk_image_set_from_icon_name (GTK_IMAGE(session_badge), "document-properties-symbolic", GTK_ICON_SIZE_MENU);
                    g_free (icon_name);
#endif
                    break;
                }
        }
        if (!menu_iter)
            menu_iter = menu_items;
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_iter->data), TRUE);
    }

    g_free (current_session);
    current_session = g_strdup(session);
    g_free (last_session);
}

static gchar *
get_language (void)
{
    GList *menu_items, *menu_iter;

    /* if the user manually selected a language, use it */
    if (current_language)
        return g_strdup (current_language);

    menu_items = gtk_container_get_children(GTK_CONTAINER(language_menu));    
    for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next(menu_iter))
    {
        if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_iter->data)))
        {
            return g_strdup(g_object_get_data (G_OBJECT (menu_iter->data), "language-code"));
        }
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

    menu_items = gtk_container_get_children(GTK_CONTAINER(language_menu));

    if (language)
    {
        for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next(menu_iter))
        {
            gchar *s;
            gboolean matched;
            s = g_strdup(g_object_get_data (G_OBJECT (menu_iter->data), "language-code"));
            matched = g_strcmp0 (s, language) == 0;
            g_free (s);
            if (matched)
            {
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_iter->data), TRUE);
                g_free (current_language);
                current_language = g_strdup(language);
                gtk_menu_item_set_label(GTK_MENU_ITEM(language_menuitem),language);
                return;
            }
        }
    }

    /* If failed to find this language, then try the default */
    if (lightdm_get_language ()) {
        default_language = lightdm_language_get_code (lightdm_get_language ());
        gtk_menu_item_set_label(GTK_MENU_ITEM (language_menuitem), default_language);
    }
    if (default_language && g_strcmp0 (default_language, language) != 0)
        set_language (default_language);
    /* If all else fails, just use the first language from the menu */
    else {
        for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next(menu_iter))
        {
            if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_iter->data)))
                gtk_menu_item_set_label(GTK_MENU_ITEM(language_menuitem), g_strdup(g_object_get_data (G_OBJECT (menu_iter->data), "language-code")));
        }
    }
}

static void
set_message_label (const gchar *text)
{
    gtk_widget_set_visible (GTK_WIDGET (info_bar), g_strcmp0 (text, "") != 0);
    gtk_label_set_text (message_label, text);
}

static void
set_login_button_label (LightDMGreeter *greeter, const gchar *username)
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
    gtk_widget_set_can_default (GTK_WIDGET (login_button), TRUE);
    gtk_widget_grab_default (GTK_WIDGET (login_button));
    /* and disable the session and language widgets */
    gtk_widget_set_sensitive (GTK_WIDGET (session_menuitem), !logged_in);
    gtk_widget_set_sensitive (GTK_WIDGET (language_menuitem), !logged_in);
}

static void
set_user_background (const gchar *username)
{
    const gchar *path = NULL;
    if (username)
    {
        LightDMUser *user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
            path = lightdm_user_get_background (user);
    }
    greeter_background_set_custom_background(greeter_background, path);
}

static void
set_user_image (const gchar *username)
{
    const gchar *path;
    LightDMUser *user = NULL;
    GdkPixbuf *image = NULL;
    GError *error = NULL;

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
                g_warning ("Failed to load user image: %s", error->message);
                g_clear_error (&error);
            }
        }
    }
    
    if (default_user_pixbuf)
        gtk_image_set_from_pixbuf (GTK_IMAGE (user_image), default_user_pixbuf);
    else
        gtk_image_set_from_icon_name (GTK_IMAGE (user_image), default_user_icon, GTK_ICON_SIZE_DIALOG);
}

/* Function translate user defined coordinates to absolute value */
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

static void
center_window (GtkWindow *window, GtkAllocation *allocation, const WindowPosition *pos)
{   
    const GdkRectangle *monitor_geometry = greeter_background_get_active_monitor_geometry (greeter_background);
    GtkAllocation *new_allocation = NULL;
    if (!allocation)
    {
        allocation = new_allocation = g_new0 (GtkAllocation, 1);
        gtk_widget_get_allocation (GTK_WIDGET (window), new_allocation);
    }
    if (monitor_geometry)
        gtk_window_move (window,
                         monitor_geometry->x + get_absolute_position (&pos->x, monitor_geometry->width, allocation->width),
                         monitor_geometry->y + get_absolute_position (&pos->y, monitor_geometry->height, allocation->height));
    g_free (new_allocation);
}

static void
active_monitor_changed_cb(GreeterBackground* background, gpointer user_data)
{
    const GdkRectangle *monitor_geometry = greeter_background_get_active_monitor_geometry (greeter_background);
    if (monitor_geometry)
    {
        GdkGeometry hints;
        hints.min_width = monitor_geometry->width;
        hints.max_width = monitor_geometry->width;
        hints.min_height = -1;
        hints.max_height = -1;
        gtk_window_set_geometry_hints (panel_window, GTK_WIDGET(panel_window),
                                       &hints, GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
    }
}

#if !GTK_CHECK_VERSION (3, 0, 0)
static GdkRegion *
cairo_region_from_rectangle (gint width, gint height, gint radius)
{
    GdkRegion *region;

    gint x = radius, y = 0;
    gint xChange = 1 - (radius << 1);
    gint yChange = 0;
    gint radiusError = 0;

    GdkRectangle rect;

    rect.x = radius;
    rect.y = radius;
    rect.width = width - radius * 2;
    rect.height = height - radius * 2;

    region = gdk_region_rectangle (&rect);

    while(x >= y)
    {

        rect.x = -x + radius;
        rect.y = -y + radius;
        rect.width = x - radius + width - rect.x;
        rect.height =  y - radius + height - rect.y;

        gdk_region_union_with_rect(region, &rect);

        rect.x = -y + radius;
        rect.y = -x + radius;
        rect.width = y - radius + width - rect.x;
        rect.height =  x - radius + height - rect.y;

        gdk_region_union_with_rect(region, &rect);

        y++;
        radiusError += yChange;
        yChange += 2;
        if(((radiusError << 1) + xChange) > 0)
        {
            x--;
            radiusError += xChange;
            xChange += 2;
        }
   }

   return region;
}

static gboolean
login_window_size_allocate (GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
    gint    radius = 10;

    GdkWindow *window = gtk_widget_get_window (widget);
    if (window_region)
        gdk_region_destroy(window_region);
    window_region = cairo_region_from_rectangle (allocation->width, allocation->height, radius);
    if (window) {
        gdk_window_shape_combine_region(window, window_region, 0, 0);
        gdk_window_input_shape_combine_region(window, window_region, 0, 0);
    }

    return TRUE;
}
#endif

static void
start_authentication (const gchar *username)
{
    gchar *data;
    gsize data_length;
    GError *error = NULL;

    cancelling = FALSE;
    prompted = FALSE;
    password_prompted = FALSE;
    prompt_active = FALSE;

    if (pending_questions)
    {
        g_slist_free_full (pending_questions, (GDestroyNotify) pam_message_finalize);
        pending_questions = NULL;
    }

    g_key_file_set_value (state, "greeter", "last-user", username);
    data = g_key_file_to_data (state, &data_length, &error);
    if (error)
        g_warning ("Failed to save state file: %s", error->message);
    g_clear_error (&error);
    if (data)
    {
        g_file_set_contents (state_filename, data, data_length, &error);
        if (error)
            g_warning ("Failed to save state file: %s", error->message);
        g_clear_error (&error);
    }
    g_free (data);

    if (g_strcmp0 (username, "*other") == 0)
    {
        gtk_widget_show (GTK_WIDGET (username_entry));
        gtk_widget_show (GTK_WIDGET (cancel_button));
        lightdm_greeter_authenticate (greeter, NULL);
    }
    else if (g_strcmp0 (username, "*guest") == 0)
    {
        lightdm_greeter_authenticate_as_guest (greeter);
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

        lightdm_greeter_authenticate (greeter, username);
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
        lightdm_greeter_cancel_authentication (greeter);
        set_message_label ("");
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
    gchar *data;
    gsize data_length;
    GError *error = NULL;

    language = get_language ();
    if (language)
        lightdm_greeter_set_language (greeter, language);
    g_free (language);

    session = get_session ();

    /* Remember last choice */
    g_key_file_set_value (state, "greeter", "last-session", session);

    greeter_background_save_xroot (greeter_background);

    data = g_key_file_to_data (state, &data_length, &error);
    if (error)
        g_warning ("Failed to save state file: %s", error->message);
    g_clear_error (&error);
    if (data)
    {
        g_file_set_contents (state_filename, data, data_length, &error);
        if (error)
            g_warning ("Failed to save state file: %s", error->message);
        g_clear_error (&error);
    }
    g_free (data);

    if (!lightdm_greeter_start_session_sync (greeter, session, NULL))
    {
        set_message_label (_("Failed to start session"));
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
    }
    g_free (session);
}

void
session_selected_cb(GtkMenuItem *menuitem, gpointer user_data);
G_MODULE_EXPORT
void
session_selected_cb(GtkMenuItem *menuitem, gpointer user_data)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem)))
    {
       gchar *session = g_object_get_data (G_OBJECT (menuitem), "session-key");
       set_session(session);
    }
}

void
language_selected_cb(GtkMenuItem *menuitem, gpointer user_data);
G_MODULE_EXPORT
void
language_selected_cb(GtkMenuItem *menuitem, gpointer user_data)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem)))
    {
       gchar *language = g_object_get_data (G_OBJECT (menuitem), "language-code");
       set_language(language);
    }
}

static void
power_menu_cb (GtkWidget *menuitem, gpointer userdata)
{
    gtk_widget_set_sensitive (suspend_menuitem, lightdm_get_can_suspend());
    gtk_widget_set_sensitive (hibernate_menuitem, lightdm_get_can_hibernate());
    gtk_widget_set_sensitive (restart_menuitem, lightdm_get_can_restart());
    gtk_widget_set_sensitive (shutdown_menuitem, lightdm_get_can_shutdown());
}

gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if ((event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down) &&
        gtk_widget_get_visible(GTK_WIDGET(user_combo)))
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
        {
            #if GTK_CHECK_VERSION (3, 0, 0)
            available = gtk_tree_model_iter_previous (model, &iter);
            #else
            GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
            available = gtk_tree_path_prev (path);
            if (available)
                available = gtk_tree_model_get_iter (model, &iter, path);
            gtk_tree_path_free (path);
            #endif
        }
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
    if (!g_strcmp0(gtk_entry_get_text(username_entry), "") == 0)
        start_authentication(gtk_entry_get_text(username_entry));
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
    else if (event->keyval == GDK_KEY_Return)
    {
        gtk_widget_grab_focus(GTK_WIDGET(password_entry));
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
        gtk_window_present (login_window);
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

static void set_displayed_user (LightDMGreeter *greeter, gchar *username)
{
    gchar *user_tooltip;
    LightDMUser *user;

    if (g_strcmp0 (username, "*other") == 0)
    {
        gtk_widget_show (GTK_WIDGET (username_entry));
        gtk_widget_show (GTK_WIDGET (cancel_button));
        user_tooltip = g_strdup(_("Other"));
    }
    else
    {
        gtk_widget_hide (GTK_WIDGET (username_entry));
        gtk_widget_hide (GTK_WIDGET (cancel_button));
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        user_tooltip = g_strdup(username);
    }

    if (g_strcmp0 (username, "*guest") == 0)
    {
        user_tooltip = g_strdup(_("Guest Session"));
    }

    set_login_button_label (greeter, username);
    set_user_background (username);
    set_user_image (username);
    user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
    if (user)
    {
        set_language (lightdm_user_get_language (user));
        set_session (lightdm_user_get_session (user));
    }
    else
        set_language (lightdm_language_get_code (lightdm_get_language ()));
    gtk_widget_set_tooltip_text (GTK_WIDGET (user_combo), user_tooltip);
    start_authentication (username);
    g_free (user_tooltip);
}

void user_combobox_active_changed_cb (GtkComboBox *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
user_combobox_active_changed_cb (GtkComboBox *widget, LightDMGreeter *greeter)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_combo_box_get_model (user_combo);

    if (gtk_combo_box_get_active_iter (user_combo, &iter))
    {
        gchar *user;

        gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &user, -1);

        set_displayed_user(greeter, user);

        g_free (user);
    }
    set_message_label ("");
}

static const gchar*
get_message_label (void)
{
    return gtk_label_get_text (message_label);
}

static void
process_prompts (LightDMGreeter *greeter)
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
        lightdm_greeter_get_authentication_user (greeter) == NULL)
    {
        prompted = TRUE;
        prompt_active = TRUE;
        gtk_widget_grab_focus (GTK_WIDGET (username_entry));
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
            set_message_label (message->text);
            continue;
        }

        gtk_entry_set_text (password_entry, "");
        gtk_entry_set_visibility (password_entry, message->type.prompt != LIGHTDM_PROMPT_TYPE_SECRET);
        if (get_message_label()[0] == 0 && password_prompted)
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
            set_message_label (str);
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

void login_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
login_cb (GtkWidget *widget)
{
    /* Reset to default screensaver values */
    if (lightdm_greeter_get_lock_hint (greeter))
        XSetScreenSaver(gdk_x11_display_get_xdisplay(gdk_display_get_default ()), timeout, interval, prefer_blanking, allow_exposures);        

    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), FALSE);
    set_message_label ("");
    prompt_active = FALSE;

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
    {
        lightdm_greeter_respond (greeter, gtk_entry_get_text (password_entry));
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

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
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
        process_prompts (greeter);
}

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
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
        process_prompts (greeter);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
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

    if (lightdm_greeter_get_is_authenticated (greeter))
    {
        if (prompted)
            start_session ();
    }
    else
    {
        if (prompted)
        {
            if (get_message_label()[0] == 0)
                set_message_label (_("Incorrect password, please try again"));
            start_authentication (lightdm_greeter_get_authentication_user (greeter));
        }
        else
            set_message_label (_("Failed to authenticate"));
    }
}

void suspend_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
suspend_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    lightdm_suspend (NULL);
}

void hibernate_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
hibernate_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    lightdm_hibernate (NULL);
}

static gboolean
show_power_prompt (const gchar* action, const gchar* message, const gchar* icon,
                   const gchar* dialog_name, const gchar* button_name)
{
    GtkWidget *dialog;
    GtkWidget *image;
    GtkWidget *button;
    gboolean   result;
    const GList *items, *item;
    gint logged_in_users = 0;
    gchar *warning;
    
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
        if (logged_in_users > 1)
            warning = g_strdup_printf (_("Warning: There are still %d users logged in."), logged_in_users);
        else
            warning = g_strdup_printf (_("Warning: There is still %d user logged in."), logged_in_users);
        message = g_markup_printf_escaped ("<b>%s</b>\n%s", warning, message);
        g_free (warning);
    }

    /* Prepare the dialog */
    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", action);
    gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(dialog), "%s", message);        
    button = gtk_dialog_add_button(GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_widget_set_name(button, "cancel_button");
    button = gtk_dialog_add_button(GTK_DIALOG (dialog), action, GTK_RESPONSE_OK);
    gtk_widget_set_name(button, button_name);

    /* Add the icon */
    image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG);
    gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(dialog), image);

    /* Make the dialog themeable and attractive */
#if GTK_CHECK_VERSION (3, 0, 0)
    gtk_style_context_add_class(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(GTK_WIDGET(dialog))), "lightdm-gtk-greeter");
#endif
    gtk_widget_set_name(dialog, dialog_name);
    gtk_container_set_border_width(GTK_CONTAINER (dialog), 18);
#if !GTK_CHECK_VERSION (3, 0, 0)
    g_signal_connect (G_OBJECT (dialog), "size-allocate", G_CALLBACK (login_window_size_allocate), NULL);
#endif
    greeter_background_add_subwindow(greeter_background, GTK_WINDOW (dialog));
    /* Hide the login window and show the dialog */
    gtk_widget_hide (GTK_WIDGET (login_window));
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog), NULL, &WINDOW_POS_CENTER);

    result = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

    greeter_background_remove_subwindow(greeter_background, GTK_WINDOW (dialog));
    gtk_widget_destroy (dialog);
    gtk_widget_show (GTK_WIDGET (login_window));
    gtk_widget_queue_resize(GTK_WIDGET (login_window));

    return result;
}

void restart_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
restart_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    if (show_power_prompt(_("Restart"), _("Are you sure you want to close all programs and restart the computer?"),
                          #if GTK_CHECK_VERSION (3, 0, 0)
                          "view-refresh-symbolic",
                          #else
                          "view-refresh",
                          #endif
                          "restart_dialog", "restart_button"))
        lightdm_restart (NULL);
}

void shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    if (show_power_prompt(_("Shut Down"), _("Are you sure you want to close all programs and shut down the computer?"),
                          #if GTK_CHECK_VERSION (3, 0, 0)
                          "system-shutdown-symbolic",
                          #else
                          "system-shutdown",
                          #endif
                          "shutdown_dialog", "shutdown_button"))
        lightdm_shutdown (NULL);
}

static void
user_added_cb (LightDMUserList *user_list, LightDMUser *user, LightDMGreeter *greeter)
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
user_changed_cb (LightDMUserList *user_list, LightDMUser *user, LightDMGreeter *greeter)
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

void a11y_font_cb (GtkCheckMenuItem *item);
G_MODULE_EXPORT
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

void a11y_contrast_cb (GtkCheckMenuItem *item);
G_MODULE_EXPORT
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

static void
keyboard_terminated_cb (GPid pid, gint status, gpointer user_data)
{
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (keyboard_menuitem), FALSE);
}

void a11y_keyboard_cb (GtkCheckMenuItem *item);
G_MODULE_EXPORT
void
a11y_keyboard_cb (GtkCheckMenuItem *item)
{
    if (gtk_check_menu_item_get_active (item))
    {
        gboolean spawned = FALSE;
        if (onboard_window)
        {
            GtkSocket* socket = NULL;
            gint out_fd = 0;

            if (g_spawn_async_with_pipes (NULL, a11y_keyboard_command, NULL, G_SPAWN_SEARCH_PATH,
                                          NULL, NULL, &a11y_kbd_pid, NULL, &out_fd, NULL,
                                          &a11y_keyboard_error))
            {
                gchar* text = NULL;
                GIOChannel* out_channel = g_io_channel_unix_new (out_fd);
                if (g_io_channel_read_line(out_channel, &text, NULL, NULL, &a11y_keyboard_error) == G_IO_STATUS_NORMAL)
                {
                    gchar* end_ptr = NULL;

                    text = g_strstrip (text);
                    gint id = g_ascii_strtoll (text, &end_ptr, 0);

                    if (id != 0 && end_ptr > text)
                    {
                        socket = GTK_SOCKET (gtk_socket_new ());
                        gtk_container_add (GTK_CONTAINER (onboard_window), GTK_WIDGET (socket));
                        gtk_socket_add_id (socket, id);
                        gtk_widget_show_all (GTK_WIDGET (onboard_window));
                        spawned = TRUE;
                    }
                    else
                        g_debug ("onboard keyboard command error : 'unrecognized output'");

                    g_free(text);
                }
            }
        }
        else
        {
            spawned = g_spawn_async (NULL, a11y_keyboard_command, NULL,
                                     G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                     NULL, NULL, &a11y_kbd_pid, &a11y_keyboard_error);
            if (spawned)
                g_child_watch_add (a11y_kbd_pid, keyboard_terminated_cb, NULL);
        }

        if(!spawned)
        {
            if (a11y_keyboard_error)
                g_debug ("a11y keyboard command error : '%s'", a11y_keyboard_error->message);
            a11y_kbd_pid = 0;
            g_clear_error(&a11y_keyboard_error);
            gtk_check_menu_item_set_active (item, FALSE);
        }
    }
    else
    {
        if (a11y_kbd_pid != 0)
        {
            kill (a11y_kbd_pid, SIGTERM);
            g_spawn_close_pid (a11y_kbd_pid);
            a11y_kbd_pid = 0;
            if (onboard_window)
                gtk_widget_hide (GTK_WIDGET(onboard_window));
        }
    }
}

static void
load_user_list (void)
{
    const GList *items, *item;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *last_user;
    const gchar *selected_user;
    gboolean logged_in = FALSE;

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

    last_user = g_key_file_get_value (state, "greeter", "last-user", NULL);

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
                    name = g_strdup(selected_user);
                    set_displayed_user(greeter, name);
                    g_free(name);
                    break;
                }
            } while (gtk_tree_model_iter_next (model, &iter));
        }
        if (!matched)
        {
            gtk_tree_model_get_iter_first (model, &iter);
            gtk_tree_model_get (model, &iter, 0, &name, -1);
            gtk_combo_box_set_active_iter (user_combo, &iter);
            set_displayed_user(greeter, name);
            g_free(name);
        }
        
    }

    g_free (last_user);
}

static gboolean
clock_timeout_thread (void)
{
    time_t rawtime;
    struct tm * timeinfo;
    gchar time_str[50];
    gchar *markup;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    
    strftime(time_str, 50, clock_format, timeinfo);
    markup = g_markup_printf_escaped("<b>%s</b>", time_str);
    if (g_strcmp0(markup, gtk_label_get_label (GTK_LABEL (clock_label))) != 0)
        gtk_label_set_markup (GTK_LABEL (clock_label), markup);
    g_free(markup);
    
    return TRUE;
}

static gboolean
read_position_from_str (const gchar *s, DimensionPosition *x)
{
    DimensionPosition p;
    gchar *end = NULL;
    gchar **parts = g_strsplit(s, ",", 2);
    if (parts[0])
    {
        p.value = g_ascii_strtoll(parts[0], &end, 10);
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

static GdkFilterReturn
focus_upon_map (GdkXEvent *gxevent, GdkEvent *event, gpointer  data)
{
    XEvent* xevent = (XEvent*)gxevent;
    GdkWindow* keyboard_win = onboard_window ? gtk_widget_get_window (GTK_WIDGET (onboard_window)) : NULL;
    if (xevent->type == MapNotify)
    {
        Window xwin = xevent->xmap.window;
        Window keyboard_xid = 0;
        GdkDisplay* display = gdk_x11_lookup_xdisplay (xevent->xmap.display);
        GdkWindow* win = gdk_x11_window_foreign_new_for_display (display, xwin);
        GdkWindowTypeHint win_type = gdk_window_get_type_hint (win);

        /* Check to see if this window is our onboard window, since we don't want to focus it. */
        if (keyboard_win)
#if GTK_CHECK_VERSION (3, 0, 0)
                keyboard_xid = gdk_x11_window_get_xid (keyboard_win);
#else
                keyboard_xid = gdk_x11_drawable_get_xid (keyboard_win);
#endif

        if (xwin != keyboard_xid
            && win_type != GDK_WINDOW_TYPE_HINT_TOOLTIP
            && win_type != GDK_WINDOW_TYPE_HINT_NOTIFICATION)
        {
            gdk_window_focus (win, GDK_CURRENT_TIME);
            /* Make sure to keep keyboard above */
            if (onboard_window)
            {
                if (keyboard_win)
                    gdk_window_raise (keyboard_win);
            }
        }
    }
    else if (xevent->type == UnmapNotify)
    {
        Window xwin;
        int revert_to;
        XGetInputFocus (xevent->xunmap.display, &xwin, &revert_to);

        if (revert_to == RevertToNone)
        {
            gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (login_window)), GDK_CURRENT_TIME);
            /* Make sure to keep keyboard above */
            if (onboard_window)
            {
                if (keyboard_win)
                    gdk_window_raise (keyboard_win);
            }
        }
    }
    return GDK_FILTER_CONTINUE;
}

/* Layout functions and callbacks */

static void
layout_selected_cb(GtkCheckMenuItem *menuitem, gpointer user_data)
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
    XklState *state = xkl_engine_get_current_state (xkl_engine);
    GList *menu_items = gtk_container_get_children (GTK_CONTAINER (layout_menu));
    GtkCheckMenuItem *menu_item = g_list_nth_data (menu_items, state->group);

    if (menu_item)
    {
        gtk_menu_item_set_label (GTK_MENU_ITEM (layout_menuitem),
                                 g_object_get_data (G_OBJECT (menu_item), LAYOUT_DATA_LABEL));
        gtk_check_menu_item_set_active(menu_item, TRUE);
    }
    else
        gtk_menu_item_set_label (GTK_MENU_ITEM (layout_menuitem), "??");
    g_list_free (menu_items);
    #else
    LightDMLayout *layout = lightdm_get_layout ();
    g_return_if_fail (layout != NULL);

    const gchar *name = lightdm_layout_get_name (layout);
    GList *menu_items = gtk_container_get_children (GTK_CONTAINER (layout_menu));
    GList *menu_iter;
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

int
main (int argc, char **argv)
{
    GKeyFile *config;
    GdkRectangle monitor_geometry;
    GtkBuilder *builder;
    const GList *items, *item;
    GtkCellRenderer *renderer;
    GtkWidget *image, *infobar_compat, *content_area;
    gchar *value, *state_dir;
#if GTK_CHECK_VERSION (3, 0, 0)
    GtkIconTheme *icon_theme;
    GtkCssProvider *css_provider;
#endif
    GError *error = NULL;

    Display* display;

    #ifdef START_INDICATOR_SERVICES
    GPid indicator_pid = 0, spi_pid = 0;
    #endif

    /* Prevent memory from being swapped out, as we are dealing with passwords */
    mlockall (MCL_CURRENT | MCL_FUTURE);

    /* Disable global menus */
    g_unsetenv ("UBUNTU_MENUPROXY");

    /* Initialize i18n */
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    g_unix_signal_add(SIGTERM, (GSourceFunc)gtk_main_quit, NULL);

#if GTK_CHECK_VERSION (3, 0, 0)
#else
    /* init threads */
    gdk_threads_init();
#endif

    /* init gtk */
    gtk_init (&argc, &argv);
    
#ifdef HAVE_LIBIDO
    ido_init ();
#endif

    config = g_key_file_new ();
    g_key_file_load_from_file (config, CONFIG_FILE, G_KEY_FILE_NONE, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load configuration from %s: %s\n", CONFIG_FILE, error->message);
    g_clear_error (&error);

    state_dir = g_build_filename (g_get_user_cache_dir (), "lightdm-gtk-greeter", NULL);
    g_mkdir_with_parents (state_dir, 0775);
    state_filename = g_build_filename (state_dir, "state", NULL);
    g_free (state_dir);

    state = g_key_file_new ();
    g_key_file_load_from_file (state, state_filename, G_KEY_FILE_NONE, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load state from %s: %s\n", state_filename, error->message);
    g_clear_error (&error);

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (lightdm_greeter_authenticate_autologin), NULL);
    if (!lightdm_greeter_connect_sync (greeter, NULL))
        return EXIT_FAILURE;

    /* Set default cursor */
    gdk_window_set_cursor (gdk_get_default_root_window (), gdk_cursor_new (GDK_LEFT_PTR));

    /* Make the greeter behave a bit more like a screensaver if used as un/lock-screen by blanking the screen */
    gchar* end_ptr = NULL;
    int screensaver_timeout = 60;
    value = g_key_file_get_value (config, "greeter", "screensaver-timeout", NULL);
    if (value)
        screensaver_timeout = g_ascii_strtoll (value, &end_ptr, 0);
    g_free (value);
    
    display = gdk_x11_display_get_xdisplay(gdk_display_get_default ());
    if (lightdm_greeter_get_lock_hint (greeter)) {
        XGetScreenSaver(display, &timeout, &interval, &prefer_blanking, &allow_exposures);
        XForceScreenSaver(display, ScreenSaverActive);
        XSetScreenSaver(display, screensaver_timeout, 0, ScreenSaverActive, DefaultExposures);
    }

    /* Set GTK+ settings */
    value = g_key_file_get_value (config, "greeter", "theme-name", NULL);
    if (value)
    {
        g_debug ("Using Gtk+ theme %s", value);
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", value, NULL);
    }
    g_free (value);
    g_object_get (gtk_settings_get_default (), "gtk-theme-name", &default_theme_name, NULL);
    g_debug ("Default Gtk+ theme is '%s'", default_theme_name);

    value = g_key_file_get_value (config, "greeter", "icon-theme-name", NULL);
    if (value)
    {
        g_debug ("Using icon theme %s", value);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", value, NULL);
    }
    g_free (value);
    g_object_get (gtk_settings_get_default (), "gtk-icon-theme-name", &default_icon_theme_name, NULL);
    g_debug ("Default theme is '%s'", default_icon_theme_name);

    value = g_key_file_get_value (config, "greeter", "font-name", NULL);
    if (value)
    {
        g_debug ("Using font %s", value);
        g_object_set (gtk_settings_get_default (), "gtk-font-name", value, NULL);
    }
    else
    {
        value = g_strdup("Sans 10");
        g_object_set (gtk_settings_get_default (), "gtk-font-name", value, NULL);
    }
    g_object_get (gtk_settings_get_default (), "gtk-font-name", &default_font_name, NULL);  
    value = g_key_file_get_value (config, "greeter", "xft-dpi", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", (int) (1024 * atof (value)), NULL);
    value = g_key_file_get_value (config, "greeter", "xft-antialias", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-antialias", g_strcmp0 (value, "true") == 0, NULL);
    g_free (value);
    value = g_key_file_get_value (config, "greeter", "xft-hintstyle", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-hintstyle", value, NULL);
    g_free (value);
    value = g_key_file_get_value (config, "greeter", "xft-rgba", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-rgba", value, NULL);
    g_free (value);
    
    /* Get a11y on screen keyboard command*/
    gint argp;
    value = g_key_file_get_value (config, "greeter", "keyboard", NULL);
    g_debug ("a11y keyboard command is '%s'", value);
    /* Set NULL to blank to avoid warnings */
    if (!value) { value = g_strdup(""); }
    g_shell_parse_argv (value, &argp, &a11y_keyboard_command, NULL);
    g_free (value);

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_string (builder, lightdm_gtk_greeter_ui,
                                      lightdm_gtk_greeter_ui_length, &error))
    {
        g_warning ("Error loading UI: %s", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);
    
    /* Panel */
    panel_window = GTK_WINDOW (gtk_builder_get_object (builder, "panel_window"));
#if GTK_CHECK_VERSION (3, 0, 0)
    gtk_style_context_add_class(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(GTK_WIDGET(panel_window))), "lightdm-gtk-greeter");
    gtk_style_context_add_class(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(GTK_WIDGET(panel_window))), GTK_STYLE_CLASS_MENUBAR);
#endif
    menubar = GTK_WIDGET (gtk_builder_get_object (builder, "menubar"));

    /* Login window */
    login_window = GTK_WINDOW (gtk_builder_get_object (builder, "login_window"));
    user_image = GTK_IMAGE (gtk_builder_get_object (builder, "user_image"));
    user_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "user_combobox"));
    username_entry = GTK_ENTRY (gtk_builder_get_object (builder, "username_entry"));
    password_entry = GTK_ENTRY (gtk_builder_get_object (builder, "password_entry"));

#if !GTK_CHECK_VERSION (3, 0, 0)
    g_signal_connect (G_OBJECT (login_window), "size-allocate", G_CALLBACK (login_window_size_allocate), NULL);
#endif

    /* Add InfoBar via code for GTK+2 compatability */
    infobar_compat = GTK_WIDGET(gtk_builder_get_object(builder, "infobar_compat"));
    info_bar = GTK_INFO_BAR (gtk_info_bar_new());
    gtk_info_bar_set_message_type(info_bar, GTK_MESSAGE_ERROR);
    gtk_widget_set_name(GTK_WIDGET(info_bar), "greeter_infobar");
    content_area = gtk_info_bar_get_content_area(info_bar);

    message_label = GTK_LABEL (gtk_builder_get_object (builder, "message_label"));
    g_object_ref(message_label);
    gtk_container_remove(GTK_CONTAINER(infobar_compat), GTK_WIDGET(message_label));
    gtk_container_add(GTK_CONTAINER(content_area), GTK_WIDGET(message_label));
    g_object_unref(message_label);

    gtk_container_add(GTK_CONTAINER(infobar_compat), GTK_WIDGET(info_bar));

    cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "cancel_button"));
    login_button = GTK_BUTTON (gtk_builder_get_object (builder, "login_button"));

    /* To maintain compatability with GTK+2, set special properties here */
#if GTK_CHECK_VERSION (3, 0, 0)
    gtk_style_context_add_class(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(GTK_WIDGET(login_window))), "lightdm-gtk-greeter");
    gtk_box_set_child_packing(GTK_BOX(content_area), GTK_WIDGET(message_label), TRUE, TRUE, 0, GTK_PACK_START);
    gtk_window_set_has_resize_grip(GTK_WINDOW(panel_window), FALSE);
    gtk_widget_set_margin_top(GTK_WIDGET(user_combo), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(password_entry), 12);
    gtk_entry_set_placeholder_text(password_entry, _("Enter your password"));
    gtk_entry_set_placeholder_text(username_entry, _("Enter your username"));
    icon_theme = gtk_icon_theme_get_default();
#else
    gtk_container_set_border_width (GTK_CONTAINER(gtk_builder_get_object (builder, "vbox2")), 18);
    gtk_container_set_border_width (GTK_CONTAINER(gtk_builder_get_object (builder, "content_frame")), 14);
    gtk_container_set_border_width (GTK_CONTAINER(gtk_builder_get_object (builder, "buttonbox_frame")), 8);
    gtk_widget_set_tooltip_text(GTK_WIDGET(password_entry), _("Enter your password"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(username_entry), _("Enter your username"));
#endif

    /* Indicators */
    session_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "session_menuitem"));
    session_menu = GTK_MENU(gtk_builder_get_object (builder, "session_menu"));
    language_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "language_menuitem"));
    language_menu = GTK_MENU(gtk_builder_get_object (builder, "language_menu"));
    a11y_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "a11y_menuitem"));
    power_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "power_menuitem"));
    layout_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "layout_menuitem"));
    layout_menu = GTK_MENU(gtk_builder_get_object (builder, "layout_menu"));
    keyboard_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "keyboard_menuitem"));
    clock_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "clock_menuitem"));
    host_menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "host_menuitem"));

    gtk_accel_map_add_entry ("<Login>/a11y/font", GDK_KEY_F1, 0);
    gtk_accel_map_add_entry ("<Login>/a11y/contrast", GDK_KEY_F2, 0);
    gtk_accel_map_add_entry ("<Login>/a11y/keyboard", GDK_KEY_F3, 0);
    gtk_accel_map_add_entry ("<Login>/power/shutdown", GDK_KEY_F4, GDK_MOD1_MASK);

#ifdef START_INDICATOR_SERVICES
    init_indicators (config, &indicator_pid, &spi_pid);
#else
    init_indicators (config);
#endif

    value = g_key_file_get_value (config, "greeter", "default-user-image", NULL);
    if (value)
    {
        if (value[0] == '#')
            default_user_icon = g_strdup (value + 1);
        else
        {
            default_user_pixbuf = gdk_pixbuf_new_from_file (value, &error);
            if (!default_user_pixbuf)
            {
                g_warning ("Failed to load default user image: %s", error->message);
                g_clear_error (&error);
            }
        }
        g_free (value);
    }

    /* Session menu */
    if (gtk_widget_get_visible (session_menuitem))
    {
#if GTK_CHECK_VERSION (3, 0, 0)
        if (gtk_icon_theme_has_icon(icon_theme, "document-properties-symbolic"))
            session_badge = gtk_image_new_from_icon_name ("document-properties-symbolic", GTK_ICON_SIZE_MENU);
        else
            session_badge = gtk_image_new_from_icon_name ("document-properties", GTK_ICON_SIZE_MENU);
#else
        session_badge = gtk_image_new_from_icon_name ("document-properties", GTK_ICON_SIZE_MENU);
#endif
        gtk_widget_show (session_badge);
        gtk_container_add (GTK_CONTAINER (session_menuitem), session_badge);

        items = lightdm_get_sessions ();
        GSList *sessions = NULL;
        for (item = items; item; item = item->next)
        {
            LightDMSession *session = item->data;
            GtkWidget *radiomenuitem;

            radiomenuitem = gtk_radio_menu_item_new_with_label (sessions, lightdm_session_get_name (session));
            g_object_set_data (G_OBJECT (radiomenuitem), "session-key", (gpointer) lightdm_session_get_key (session));
            sessions = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radiomenuitem));
            g_signal_connect(G_OBJECT(radiomenuitem), "activate", G_CALLBACK(session_selected_cb), NULL);
            gtk_menu_shell_append (GTK_MENU_SHELL(session_menu), radiomenuitem);
            gtk_widget_show (GTK_WIDGET (radiomenuitem));
        }
        set_session (NULL);
    }

    /* Language menu */
    if (gtk_widget_get_visible (language_menuitem))
    {
        items = lightdm_get_languages ();
        GSList *languages = NULL;
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
            gchar *modifier = strchr (code, '@');
            if (modifier != NULL)
            {
                gchar *label_new = g_strdup_printf ("%s [%s]", label, modifier+1);
                g_free (label);
                label = label_new;
            }

            radiomenuitem = gtk_radio_menu_item_new_with_label (languages, label);
            g_object_set_data (G_OBJECT (radiomenuitem), "language-code", (gpointer) code);
            languages = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radiomenuitem));
            g_signal_connect(G_OBJECT(radiomenuitem), "activate", G_CALLBACK(language_selected_cb), NULL);
            gtk_menu_shell_append (GTK_MENU_SHELL(language_menu), radiomenuitem);
            gtk_widget_show (GTK_WIDGET (radiomenuitem));
        }
        set_language (NULL);
    }

    /* a11y menu */
    if (gtk_widget_get_visible (a11y_menuitem))
    {
    #if GTK_CHECK_VERSION (3, 0, 0)
        if (gtk_icon_theme_has_icon(icon_theme, "preferences-desktop-accessibility-symbolic"))
            image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility-symbolic", GTK_ICON_SIZE_MENU);
        else
            image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_MENU);
    #else
        image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_MENU);
    #endif
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (a11y_menuitem), image);
    }

    /* Power menu */
    if (gtk_widget_get_visible (power_menuitem))
    {
#if GTK_CHECK_VERSION (3, 0, 0)
        if (gtk_icon_theme_has_icon(icon_theme, "system-shutdown-symbolic"))
            image = gtk_image_new_from_icon_name ("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
        else
            image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
#else
        image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
#endif
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (power_menuitem), image);

        suspend_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "suspend_menuitem")));
        hibernate_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "hibernate_menuitem")));
        restart_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "restart_menuitem")));
        shutdown_menuitem = (GTK_WIDGET (gtk_builder_get_object (builder, "shutdown_menuitem")));

        g_signal_connect (G_OBJECT (power_menuitem),"activate", G_CALLBACK(power_menu_cb), NULL);
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
        clock_format = g_key_file_get_value (config, "greeter", "clock-format", NULL);
        if (!clock_format)
            clock_format = "%a, %H:%M";
        clock_timeout_thread ();
        gdk_threads_add_timeout (1000, (GSourceFunc) clock_timeout_thread, NULL);
    }

    #if GTK_CHECK_VERSION (3, 0, 0)
    /* A bit of CSS */
    GdkRGBA lightdm_gtk_greeter_override_defaults;
    css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (css_provider, lightdm_gtk_greeter_css_application, lightdm_gtk_greeter_css_application_length, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default (), GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    css_provider = gtk_css_provider_new ();
    guint fallback_css_priority = GTK_STYLE_PROVIDER_PRIORITY_APPLICATION;
    if (gtk_style_context_lookup_color (gtk_widget_get_style_context (GTK_WIDGET (login_window)),
                                        "lightdm-gtk-greeter-override-defaults",
                                        &lightdm_gtk_greeter_override_defaults))
        fallback_css_priority = GTK_STYLE_PROVIDER_PRIORITY_FALLBACK;
    gtk_css_provider_load_from_data (css_provider, lightdm_gtk_greeter_css_fallback, lightdm_gtk_greeter_css_fallback_length, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default (), GTK_STYLE_PROVIDER (css_provider),
                                              fallback_css_priority);
    #endif

    /* Background */
    greeter_background = greeter_background_new ();
    g_signal_connect (G_OBJECT (greeter_background), "active-monitor-changed", G_CALLBACK(active_monitor_changed_cb), NULL);

    value = g_key_file_get_value (config, "greeter", "background", NULL);
    greeter_background_set_background_config (greeter_background, value);
    g_free (value);

    value = g_key_file_get_value (config, "greeter", "user-background", NULL);
    greeter_background_set_custom_background_config (greeter_background, value);
    g_free (value);

    value = g_key_file_get_value (config, "greeter", "active-monitor", NULL);
    greeter_background_set_active_monitor_config (greeter_background, value);
    g_free (value);

    value = g_key_file_get_value (config, "greeter", "lid-monitor", NULL);
    greeter_background_set_lid_monitor_config (greeter_background, value ? value : "#cursor");
    g_free (value);

    greeter_background_add_subwindow (greeter_background, login_window);
    greeter_background_add_subwindow (greeter_background, panel_window);
    greeter_background_connect (greeter_background, gdk_screen_get_default ());

    /* Users combobox */
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (user_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "text", 1);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "weight", 2);

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
    main_window_pos = WINDOW_POS_CENTER;
    value = g_key_file_get_value (config, "greeter", "position", NULL);
    if (value)
    {
        gchar *x = value;
        gchar *y = strchr(value, ' ');
        if (y)
            (y++)[0] = '\0';
        
        if (read_position_from_str (x, &main_window_pos.x))
            /* If there is no y-part then y = x */
            if (!y || !read_position_from_str (y, &main_window_pos.y))
                main_window_pos.y = main_window_pos.x;

        g_free (value);
    }
    panel_window_pos = WINDOW_POS_TOP_LEFT;

    gtk_builder_connect_signals(builder, greeter);

    gtk_widget_show (GTK_WIDGET (panel_window));
    center_window (panel_window, NULL, &panel_window_pos);
    g_signal_connect (GTK_WIDGET (panel_window), "size-allocate", G_CALLBACK (center_window), &panel_window_pos);

    gtk_widget_show (GTK_WIDGET (login_window));
    center_window (login_window, NULL, &main_window_pos);
    g_signal_connect (GTK_WIDGET (login_window), "size-allocate", G_CALLBACK (center_window), &main_window_pos);
    gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (login_window)), GDK_CURRENT_TIME);

    if (a11y_keyboard_command)
    {
        /* If command is onboard, position the application at the bottom-center of the screen */
        if (g_strcmp0(a11y_keyboard_command[0], "onboard") == 0)
        {
            gint argp;
            value = "onboard --xid";
            g_debug ("a11y keyboard command is now '%s'", value);
            g_shell_parse_argv (value, &argp, &a11y_keyboard_command, NULL);
            onboard_window = GTK_WINDOW (gtk_window_new(GTK_WINDOW_TOPLEVEL));
            gtk_widget_set_size_request (GTK_WIDGET (onboard_window), 605, 205);
            gtk_window_move (onboard_window, (monitor_geometry.width - 605)/2, monitor_geometry.height - 205);
        }
    }
    gtk_widget_set_sensitive (keyboard_menuitem, a11y_keyboard_command != NULL);
    gtk_widget_set_visible (keyboard_menuitem, a11y_keyboard_command != NULL);

    /* focus fix (source: unity-greeter) */
    GdkWindow* root_window = gdk_get_default_root_window ();
    gdk_window_set_events (root_window, gdk_window_get_events (root_window) | GDK_SUBSTRUCTURE_MASK);
    gdk_window_add_filter (root_window, focus_upon_map, NULL);

#if GTK_CHECK_VERSION (3, 0, 0)
#else
    gdk_threads_enter();
#endif
    gtk_main ();
#if GTK_CHECK_VERSION (3, 0, 0)
#else
    gdk_threads_leave();
#endif

#ifdef START_INDICATOR_SERVICES
    if (indicator_pid)
    {
		kill (indicator_pid, SIGTERM);
		waitpid (indicator_pid, NULL, 0);
    }

    if (spi_pid)
    {
		kill (spi_pid, SIGTERM);
		waitpid (spi_pid, NULL, 0);
    }
#endif

    return EXIT_SUCCESS;
}
