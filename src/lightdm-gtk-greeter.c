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

#include <locale.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <cairo-xlib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>

#ifdef HAVE_LIBINDICATOR
#include <libindicator/indicator-object.h>
#endif

#include "lightdm.h"

static LightDMGreeter *greeter;
static GKeyFile *state;
static gchar *state_filename;
static GtkWindow *login_window, *panel_window;
static GtkButton *login_button, *cancel_button;
static GtkLabel *message_label, *prompt_label;
static GtkWidget *login_box, *prompt_box;
static GtkEntry *prompt_entry;
static GtkComboBox *user_combo;
static GtkComboBox *session_combo;
static GtkComboBox *language_combo;
static gchar *default_font_name, *default_theme_name, *default_icon_theme_name;
static GdkPixbuf *default_background_pixbuf = NULL;
static GdkRGBA *default_background_color = NULL;
static gboolean cancelling = FALSE, prompted = FALSE;


#ifdef HAVE_LIBINDICATOR
static gboolean
entry_scrolled (GtkWidget *menuitem, GdkEventScroll *event, gpointer data)
{
    IndicatorObject      *io;
    IndicatorObjectEntry *entry;

    g_return_val_if_fail (GTK_IS_WIDGET (menuitem), FALSE);

    io = g_object_get_data (G_OBJECT (menuitem), "indicator-custom-object-data");
    entry = g_object_get_data (G_OBJECT (menuitem), "indicator-custom-entry-data");

    g_return_val_if_fail (INDICATOR_IS_OBJECT (io), FALSE);

    g_signal_emit_by_name (io, "scroll", 1, event->direction);
    g_signal_emit_by_name (io, "scroll-entry", entry, 1, event->direction);

    return FALSE;
}

static void
entry_activated (GtkWidget *widget, gpointer user_data)
{
    IndicatorObject      *io;
    IndicatorObjectEntry *entry;

    g_return_if_fail (GTK_IS_WIDGET (widget));

    io = g_object_get_data (G_OBJECT (widget), "indicator-custom-object-data");
    entry = g_object_get_data (G_OBJECT (widget), "indicator-custom-entry-data");

    g_return_if_fail (INDICATOR_IS_OBJECT (io));

    return indicator_object_entry_activate (io, entry, gtk_get_current_event_time ());
}

static GtkWidget*
create_menuitem (IndicatorObject *io, IndicatorObjectEntry *entry)
{
    GtkWidget *box, *menuitem;

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    menuitem = gtk_menu_item_new ();

    gtk_widget_add_events(GTK_WIDGET(menuitem), GDK_SCROLL_MASK);

    g_object_set_data (G_OBJECT (menuitem), "indicator-custom-object-data", io);
    g_object_set_data (G_OBJECT (menuitem), "indicator-custom-entry-data", entry);

    g_signal_connect (G_OBJECT (menuitem), "activate", G_CALLBACK (entry_activated), NULL);
    g_signal_connect (G_OBJECT (menuitem), "scroll-event", G_CALLBACK (entry_scrolled), NULL);

    if (entry->image)
        gtk_box_pack_start (GTK_BOX(box), GTK_WIDGET(entry->image), FALSE, FALSE, 1);

    if (entry->label)
        gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(entry->label), FALSE, FALSE, 1);

    if (entry->menu)
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), GTK_WIDGET (entry->menu));

    gtk_container_add (GTK_CONTAINER (menuitem), box);
    gtk_widget_show (box);

    return menuitem;
}

static void
entry_added (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    GtkWidget *menuitem;

    /* if the menuitem doesn't already exist, create it now */
    menuitem = g_object_get_data (G_OBJECT (entry), "indicator-custom-menuitem-data");
    if (!GTK_IS_WIDGET (menuitem))
    {
        menuitem = create_menuitem (io, entry);
        g_object_set_data (G_OBJECT (entry), "indicator-custom-menuitem-data", GTK_WIDGET (menuitem));
    }

    gtk_widget_show (menuitem);
}

static void
entry_removed_cb (GtkWidget *widget, gpointer userdata)
{
    GtkWidget *menuitem;
    gpointer   entry;

    entry = g_object_get_data (G_OBJECT (widget), "indicator-custom-entry-data");
    if (entry != userdata)
        return;

    menuitem = g_object_get_data (G_OBJECT (entry), "indicator-custom-menuitem-data");
    if (GTK_IS_WIDGET (menuitem))
        gtk_widget_hide (menuitem);

    gtk_widget_destroy (widget);
}

static void
entry_removed (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    gtk_container_foreach (GTK_CONTAINER (user_data), entry_removed_cb, entry);
}

static void
menu_show (IndicatorObject *io, IndicatorObjectEntry *entry, guint32 timestamp, gpointer user_data)
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

static gboolean
load_module (const gchar *name, GtkWidget *menuitem)
{
    IndicatorObject *io;
    GList           *entries, *entry;
    gchar           *path;

    g_return_val_if_fail (name, FALSE);

    if (!g_str_has_suffix (name, G_MODULE_SUFFIX))
        return FALSE;

    path = g_build_filename (INDICATOR_DIR, name, NULL);
    io = indicator_object_new_from_file (path);
    g_free (path);

    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED,
                      G_CALLBACK (entry_added), menuitem);
    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED,
                      G_CALLBACK (entry_removed), menuitem);
    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_MENU_SHOW,
                      G_CALLBACK(menu_show), menuitem);

    entries = indicator_object_get_entries (io);
    for (entry = entries; entry; entry = g_list_next (entry))
        entry_added (io, (IndicatorObjectEntry *) entry->data, menuitem);
    g_list_free (entries);

    return TRUE;
}
#endif

static gchar *
get_session ()
{
    GtkTreeIter iter;
    gchar *session;

    if (!gtk_combo_box_get_active_iter (session_combo, &iter))
        return g_strdup (lightdm_greeter_get_default_session_hint (greeter));

    gtk_tree_model_get (gtk_combo_box_get_model (session_combo), &iter, 1, &session, -1);

    return session;
}

static void
set_session (const gchar *session)
{
    GtkTreeModel *model = gtk_combo_box_get_model (session_combo);
    GtkTreeIter iter;
    const gchar *default_session;
    gchar *last_session;

    if (session && gtk_tree_model_get_iter_first (model, &iter))
    {
        do
        {
            gchar *s;
            gboolean matched;
            gtk_tree_model_get (model, &iter, 1, &s, -1);
            matched = strcmp (s, session) == 0;
            g_free (s);
            if (matched)
            {
                gtk_combo_box_set_active_iter (session_combo, &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));
    }

    /* If failed to find this session, then try the previous, then the default */
    last_session = g_key_file_get_value (state, "greeter", "last-session", NULL);
    if (last_session && g_strcmp0 (session, last_session) != 0)
    {
        set_session (last_session);
        g_free (last_session);
        return;
    }
    g_free (last_session);
    default_session = lightdm_greeter_get_default_session_hint (greeter);
    if (default_session && g_strcmp0 (session, default_session) != 0)
    {
        set_session (lightdm_greeter_get_default_session_hint (greeter));
        return;
    }

    /* Otherwise just pick the first session */
    gtk_combo_box_set_active (session_combo, 0);
}

static gchar *
get_language ()
{
    GtkTreeIter iter;
    gchar *language;

    if (!gtk_combo_box_get_active_iter (language_combo, &iter))
        return NULL;

    gtk_tree_model_get (gtk_combo_box_get_model (language_combo), &iter, 1, &language, -1);

    return language;
}

static void
set_language (const gchar *language)
{
    GtkTreeModel *model = gtk_combo_box_get_model (language_combo);
    GtkTreeIter iter;
    const gchar *default_language = NULL;

    if (language && gtk_tree_model_get_iter_first (model, &iter))
    {
        do
        {
            gchar *s;
            gboolean matched;
            gtk_tree_model_get (model, &iter, 1, &s, -1);
            matched = strcmp (s, language) == 0;
            g_free (s);
            if (matched)
            {
                gtk_combo_box_set_active_iter (language_combo, &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));
    }

    /* If failed to find this language, then try the default */
    if (lightdm_get_language ())
        default_language = lightdm_language_get_code (lightdm_get_language ());
    if (default_language && g_strcmp0 (default_language, language) != 0)
        set_language (default_language);
}

static void
set_message_label (const gchar *text)
{
    gtk_widget_set_visible (GTK_WIDGET (message_label), strcmp (text, "") != 0);
    gtk_label_set_text (message_label, text);
}

static void
set_login_button_label (const gchar *username)
{
        LightDMUser *user;
        gboolean logged_in = FALSE;

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        /* Show 'Unlock' instead of 'Login' for an already logged in user */
        logged_in = user && lightdm_user_get_logged_in (user);
        if (logged_in)
            gtk_button_set_label (login_button, _("Unlock"));
        else
            gtk_button_set_label (login_button, _("Login"));
        /* and disable the session and language comboboxes */
        gtk_widget_set_sensitive (GTK_WIDGET (session_combo), !logged_in);
        gtk_widget_set_sensitive (GTK_WIDGET (language_combo), !logged_in);
}

static void set_background (GdkPixbuf *new_bg);

static void
set_user_background (const gchar *username)
{
    LightDMUser *user;
    const gchar *path;
    GdkPixbuf *bg = NULL;
    GError *error = NULL;

    user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
    if (user)
    {
        path = lightdm_user_get_background (user);
        if (path)
        {
            bg = gdk_pixbuf_new_from_file (path, &error);
            if (!bg)
            {
                g_warning ("Failed to load user background: %s", error->message);
                g_clear_error (&error);
            }
        }
    }

    set_background (bg);
    if (bg)
        g_object_unref (bg);
}

static void
start_authentication (const gchar *username)
{
    gchar *data;
    gsize data_length;
    GError *error = NULL;

    cancelling = FALSE;
    prompted = FALSE;

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

    if (strcmp (username, "*other") == 0)
    {
        gtk_widget_show (GTK_WIDGET (cancel_button));
        lightdm_greeter_authenticate (greeter, NULL);
    }
    else if (strcmp (username, "*guest") == 0)
    {
        lightdm_greeter_authenticate_as_guest (greeter);
    }
    else
    {
        LightDMUser *user;

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
        {
            set_session (lightdm_user_get_session (user));
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

    /* If in authentication then stop that first */
    cancelling = FALSE;
    if (lightdm_greeter_get_in_authentication (greeter))
    {
        cancelling = TRUE;
        lightdm_greeter_cancel_authentication (greeter);
        set_message_label ("");
    }

    /* Force refreshing the prompt_box for "Other" */
    model = gtk_combo_box_get_model (user_combo);

    if (gtk_combo_box_get_active_iter (user_combo, &iter))
    {
        gchar *user;

        gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &user, -1);
        other = (strcmp (user, "*other") == 0);
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

        if (strcmp (user, "*other") == 0)
            gtk_widget_show (GTK_WIDGET (cancel_button));
        else
            gtk_widget_hide (GTK_WIDGET (cancel_button));

        set_login_button_label (user);
        set_user_background (user);
        start_authentication (user);
        g_free (user);
    }
}

void login_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
login_cb (GtkWidget *widget)
{
    gtk_widget_set_sensitive (GTK_WIDGET (prompt_entry), FALSE);
    set_message_label ("");

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
        lightdm_greeter_respond (greeter, gtk_entry_get_text (prompt_entry));
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
    prompted = TRUE;

    gtk_widget_show (GTK_WIDGET (login_box));
    gtk_label_set_text (prompt_label, dgettext ("Linux-PAM", text));
    gtk_widget_set_sensitive (GTK_WIDGET (prompt_entry), TRUE);
    gtk_entry_set_text (prompt_entry, "");
    gtk_entry_set_visibility (prompt_entry, type != LIGHTDM_PROMPT_TYPE_SECRET);
    gtk_widget_show (GTK_WIDGET (prompt_box));
    gtk_widget_grab_focus (GTK_WIDGET (prompt_entry));
}

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    set_message_label (text);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    gtk_entry_set_text (prompt_entry, "");

    if (cancelling)
    {
        cancel_authentication ();
        return;
    }

    gtk_widget_hide (prompt_box);
    gtk_widget_show (login_box);

    if (lightdm_greeter_get_is_authenticated (greeter))
    {
        if (prompted)
            start_session ();
    }
    else
    {
        if (prompted)
        {
            set_message_label (_("Incorrect password, please try again"));
            start_authentication (lightdm_greeter_get_authentication_user (greeter));
        }
        else
            set_message_label (_("Failed to authenticate"));
    }
}

static void
center_window (GtkWindow *window)
{
    GdkScreen *screen;
    GtkAllocation allocation;
    GdkRectangle monitor_geometry;

    screen = gtk_window_get_screen (window);
    gdk_screen_get_monitor_geometry (screen, gdk_screen_get_primary_monitor (screen), &monitor_geometry);
    gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
    gtk_window_move (window,
                     monitor_geometry.x + (monitor_geometry.width - allocation.width) / 2,
                     monitor_geometry.y + (monitor_geometry.height - allocation.height) / 2);
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

void restart_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
restart_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    GtkWidget *dialog;

    gtk_widget_hide (GTK_WIDGET (login_window));

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", _("Are you sure you want to close all programs and restart the computer?"));
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Return To Login"), FALSE, _("Restart"), TRUE, NULL);
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog));

    if (gtk_dialog_run (GTK_DIALOG (dialog)))
        lightdm_restart (NULL);

    gtk_widget_destroy (dialog);
    gtk_widget_show (GTK_WIDGET (login_window));
}

void shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    GtkWidget *dialog;

    gtk_widget_hide (GTK_WIDGET (login_window));

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", _("Are you sure you want to close all programs and shutdown the computer?"));
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Return To Login"), FALSE, _("Shutdown"), TRUE, NULL);
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog));

    if (gtk_dialog_run (GTK_DIALOG (dialog)))
        lightdm_shutdown (NULL);

    gtk_widget_destroy (dialog);
    gtk_widget_show (GTK_WIDGET (login_window));
}

static void
user_added_cb (LightDMUserList *user_list, LightDMUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_combo_box_get_model (user_combo);

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, lightdm_user_get_name (user),
                        1, lightdm_user_get_display_name (user),
                        2, lightdm_user_get_logged_in (user) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
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
user_changed_cb (LightDMUserList *user_list, LightDMUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!get_user_iter (lightdm_user_get_name (user), &iter))
        return;

    model = gtk_combo_box_get_model (user_combo);

    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, lightdm_user_get_name (user),
                        1, lightdm_user_get_display_name (user),
                        2, lightdm_user_get_logged_in (user) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
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

void a11y_font_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
a11y_font_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
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
        g_object_set (gtk_settings_get_default (), "gtk-font-name", default_font_name, NULL);
}

void a11y_contrast_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
a11y_contrast_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
    {
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", "HighContrastInverse", NULL);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", "HighContrastInverse", NULL);
    }
    else
    {
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", default_theme_name, NULL);
        g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", default_icon_theme_name, NULL);
    }
}

static void
sigterm_cb (int signum)
{
    exit (0);
}

static void
load_user_list ()
{
    const GList *items, *item;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *last_user;
    const gchar *selected_user;

    g_signal_connect (lightdm_user_list_get_instance (), "user-added", G_CALLBACK (user_added_cb), NULL);
    g_signal_connect (lightdm_user_list_get_instance (), "user-changed", G_CALLBACK (user_changed_cb), NULL);
    g_signal_connect (lightdm_user_list_get_instance (), "user-removed", G_CALLBACK (user_removed_cb), NULL);

    model = gtk_combo_box_get_model (user_combo);
    items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
    for (item = items; item; item = item->next)
    {
        LightDMUser *user = item->data;

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, lightdm_user_get_name (user),
                            1, lightdm_user_get_display_name (user),
                            2, lightdm_user_get_logged_in (user) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                            -1);
    }
    if (lightdm_greeter_get_has_guest_account_hint (greeter))
    {
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, "*guest",
                            1, _("Guest Account"),
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

    if (selected_user && gtk_tree_model_get_iter_first (model, &iter))
    {
        do
        {
            gchar *name;
            gboolean matched;
            gtk_tree_model_get (model, &iter, 0, &name, -1);
            matched = strcmp (name, selected_user) == 0;
            g_free (name);
            if (matched)
            {
                gtk_combo_box_set_active_iter (user_combo, &iter);
                set_login_button_label (selected_user);
                set_user_background (selected_user);
                start_authentication (selected_user);
                break;
            }
        } while (gtk_tree_model_iter_next (model, &iter));
    }

    g_free (last_user);
}

static cairo_surface_t *
create_root_surface (GdkScreen *screen)
{
    gint number, width, height;
    Display *display;
    Pixmap pixmap;
    cairo_surface_t *surface;

    number = gdk_screen_get_number (screen);
    width = gdk_screen_get_width (screen);
    height = gdk_screen_get_height (screen);

    /* Open a new connection so with Retain Permanent so the pixmap remains when the greeter quits */
    gdk_flush ();
    display = XOpenDisplay (gdk_display_get_name (gdk_screen_get_display (screen)));
    if (!display)
    {
        g_warning ("Failed to create root pixmap");
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

    /* Use this pixmap for the background */
    XSetWindowBackgroundPixmap (GDK_SCREEN_XDISPLAY (screen),
                                RootWindow (GDK_SCREEN_XDISPLAY (screen), number),
                                cairo_xlib_surface_get_drawable (surface));


    return surface;
}

static void
set_background (GdkPixbuf *new_bg)
{
    GdkRectangle monitor_geometry;
    GdkPixbuf *bg = NULL;
    gint i;

    if (new_bg)
        bg = new_bg;
    else
        bg = default_background_pixbuf;

    /* Set the background */
    for (i = 0; i < gdk_display_get_n_screens (gdk_display_get_default ()); i++)
    {
        GdkScreen *screen;
        cairo_surface_t *surface;
        cairo_t *c;
        int monitor;

        screen = gdk_display_get_screen (gdk_display_get_default (), i);
        surface = create_root_surface (screen);
        c = cairo_create (surface);

        for (monitor = 0; monitor < gdk_screen_get_n_monitors (screen); monitor++)
        {
            gdk_screen_get_monitor_geometry (screen, monitor, &monitor_geometry);

            if (bg)
            {
                GdkPixbuf *p = gdk_pixbuf_scale_simple (bg, monitor_geometry.width,
                                                        monitor_geometry.height, GDK_INTERP_BILINEAR);
                gdk_cairo_set_source_pixbuf (c, p, monitor_geometry.x, monitor_geometry.y);
                g_object_unref (p);
            }
            else
                gdk_cairo_set_source_rgba (c, default_background_color);
            cairo_paint (c);
        }

        cairo_destroy (c);

        /* Refresh background */
        gdk_flush ();
        XClearWindow (GDK_SCREEN_XDISPLAY (screen), RootWindow (GDK_SCREEN_XDISPLAY (screen), i));
    }
}

int
main (int argc, char **argv)
{
    GKeyFile *config;
    GdkRectangle monitor_geometry;
    GtkBuilder *builder;
    GtkTreeModel *model;
    const GList *items, *item;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    GtkWidget *menuitem, *hbox, *image;
    gchar *value, *state_dir;
    GdkRGBA background_color;
    GError *error = NULL;
#ifdef HAVE_LIBINDICATOR
    GDir *dir;
    guint indicators_loaded = 0;
#endif

    /* Disable global menus */
    g_unsetenv ("UBUNTU_MENUPROXY");

    /* Initialize i18n */
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    signal (SIGTERM, sigterm_cb);

    gtk_init (&argc, &argv);

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

    /* Load background */
    value = g_key_file_get_value (config, "greeter", "background", NULL);
    if (!value)
        value = g_strdup ("#000000");
    if (!gdk_rgba_parse (&background_color, value))
    {
        gchar *path;
        GError *error = NULL;

        if (g_path_is_absolute (value))
            path = g_strdup (value);
        else
            path = g_build_filename (GREETER_DATA_DIR, value, NULL);

        g_debug ("Loading background %s", path);
        default_background_pixbuf = gdk_pixbuf_new_from_file (path, &error);
        if (!default_background_pixbuf)
           g_warning ("Failed to load background: %s", error->message);
        g_clear_error (&error);
        g_free (path);
    }
    else
    {
        g_debug ("Using background color %s", value);
        default_background_color = gdk_rgba_copy (&background_color);
    }
    g_free (value);

    /* Set the background */
    set_background (NULL);

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
    g_object_get (gtk_settings_get_default (), "gtk-font-name", &default_font_name, NULL);  
    value = g_key_file_get_value (config, "greeter", "xft-dpi", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", (int) (1024 * atof (value)), NULL);
    value = g_key_file_get_value (config, "greeter", "xft-antialias", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-antialias", strcmp (value, "true") == 0, NULL);
    g_free (value);
    value = g_key_file_get_value (config, "greeter", "xft-hintstyle", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-hintstyle", value, NULL);
    g_free (value);
    value = g_key_file_get_value (config, "greeter", "xft-rgba", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-rgba", value, NULL);
    g_free (value);

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_file (builder, GREETER_DATA_DIR "/greeter.ui", &error))
    {
        g_warning ("Error loading UI: %s", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    login_window = GTK_WINDOW (gtk_builder_get_object (builder, "login_window"));
    login_box = GTK_WIDGET (gtk_builder_get_object (builder, "login_box"));
    login_button = GTK_BUTTON (gtk_builder_get_object (builder, "login_button"));
    cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "cancel_button"));
    prompt_box = GTK_WIDGET (gtk_builder_get_object (builder, "prompt_box"));
    prompt_label = GTK_LABEL (gtk_builder_get_object (builder, "prompt_label"));
    prompt_entry = GTK_ENTRY (gtk_builder_get_object (builder, "prompt_entry"));
    message_label = GTK_LABEL (gtk_builder_get_object (builder, "message_label"));
    user_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "user_combobox"));
    session_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "session_combobox"));
    language_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "language_combobox"));  
    panel_window = GTK_WINDOW (gtk_builder_get_object (builder, "panel_window"));

    /* Load logo */
    value = g_key_file_get_value (config, "greeter", "logo", NULL);
    if (value)
    {
        gchar *path;
        GtkImage *logo = GTK_IMAGE (gtk_builder_get_object (builder, "logo"));
        GdkPixbuf *logo_pixbuf = NULL;
        GError *error = NULL;

        if (g_path_is_absolute (value))
            path = g_strdup (value);
        else
            path = g_build_filename (GREETER_DATA_DIR, value, NULL);

        g_debug ("Loading logo %s", path);
        logo_pixbuf = gdk_pixbuf_new_from_file (path, &error);
        if (logo_pixbuf)
            gtk_image_set_from_pixbuf (logo, logo_pixbuf);
        else
           g_warning ("Failed to load logo: %s", error->message);
        g_clear_error (&error);
        g_object_unref (logo_pixbuf);
        g_free (path);
        g_free (value);
    }

    gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "hostname_label")), lightdm_get_hostname ());

    /* Glade can't handle custom menuitems, so set them up manually */
#ifdef HAVE_LIBINDICATOR
    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "indicator_menuitem"));
    /* load indicators */
    if (g_file_test (INDICATOR_DIR, (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)))
    {
        const gchar *name;
        dir = g_dir_open (INDICATOR_DIR, 0, NULL);

        while ((name = g_dir_read_name (dir)))
            if (load_module (name, menuitem))
                ++indicators_loaded;

        g_dir_close (dir);
    }

    if (indicators_loaded > 0)
    {
        gtk_widget_set_can_focus (menuitem, TRUE);
        gtk_widget_show (menuitem);
    }
#endif

    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "power_menuitem"));
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show (hbox);
    gtk_container_add (GTK_CONTAINER (menuitem), hbox);
    image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
    gtk_widget_show (image);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "a11y_menuitem"));
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_show (hbox);
    gtk_container_add (GTK_CONTAINER (menuitem), hbox);
    image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_MENU);
    gtk_widget_show (image);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

    if (!lightdm_get_can_suspend ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "suspend_menuitem")));
    if (!lightdm_get_can_hibernate ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "hibernate_menuitem")));
    if (!lightdm_get_can_restart ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "restart_menuitem")));
    if (!lightdm_get_can_shutdown ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "shutdown_menuitem")));

    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (session_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (session_combo), renderer, "text", 0);
    model = gtk_combo_box_get_model (session_combo);
    items = lightdm_get_sessions ();
    for (item = items; item; item = item->next)
    {
        LightDMSession *session = item->data;

        gtk_widget_show (GTK_WIDGET (session_combo));
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, lightdm_session_get_name (session),
                            1, lightdm_session_get_key (session),
                            -1);
    }
    set_session (NULL);

    if (g_key_file_get_boolean (config, "greeter", "show-language-selector", NULL))
    {
        gtk_widget_show (GTK_WIDGET (language_combo));

        renderer = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (language_combo), renderer, TRUE);
        gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (language_combo), renderer, "text", 0);
        model = gtk_combo_box_get_model (language_combo);
        items = lightdm_get_languages ();
        for (item = items; item; item = item->next)
        {
            LightDMLanguage *language = item->data;
            const gchar *country, *code;
            gchar *label;

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

            gtk_widget_show (GTK_WIDGET (language_combo));
            gtk_list_store_append (GTK_LIST_STORE (model), &iter);
            gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                                0, label,
                                1, code,
                                -1);
            g_free (label);
        }
        set_language (NULL);
    }

    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (user_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "text", 1);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "weight", 2);

    if (lightdm_greeter_get_hide_users_hint (greeter))
        start_authentication ("*other");
    else
    {
        load_user_list ();
        gtk_widget_hide (GTK_WIDGET (cancel_button));
        gtk_widget_show (GTK_WIDGET (user_combo));
    } 

    gtk_builder_connect_signals(builder, greeter);

    gtk_widget_show (GTK_WIDGET (login_window));
    center_window (login_window);
    g_signal_connect (GTK_WIDGET (login_window), "size-allocate", G_CALLBACK (center_window), login_window);

    gtk_widget_show (GTK_WIDGET (panel_window));
    GtkAllocation allocation;
    gtk_widget_get_allocation (GTK_WIDGET (panel_window), &allocation);
    gdk_screen_get_monitor_geometry (gdk_screen_get_default (), gdk_screen_get_primary_monitor (gdk_screen_get_default ()), &monitor_geometry);
    gtk_window_resize (panel_window, monitor_geometry.width, allocation.height);
    gtk_window_move (panel_window, monitor_geometry.x, monitor_geometry.y);

    gtk_widget_show (GTK_WIDGET (login_window));
    gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (login_window)), GDK_CURRENT_TIME);

    gtk_main ();

    if (default_background_pixbuf)
        g_object_unref (default_background_pixbuf);
    if (default_background_color)
        gdk_rgba_free ( default_background_color);

    return EXIT_SUCCESS;
}
