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
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>
#include <glib.h>
#if GTK_CHECK_VERSION (3, 0, 0)
#include <gtk/gtkx.h>
#else
#include <gdk/gdkkeysyms.h>
#endif

#ifdef HAVE_LIBINDICATOR
#include <libindicator/indicator-object.h>
#endif

#include <lightdm.h>

#include <src/lightdm-gtk-greeter-ui.h>

static LightDMGreeter *greeter;
static GKeyFile *state;
static gchar *state_filename;

/* Defaults */
static gchar *default_font_name, *default_theme_name, *default_icon_theme_name;
static GdkPixbuf *default_background_pixbuf = NULL;
static GdkPixbuf *background_pixbuf = NULL;

/* Panel Widgets */
static GtkWindow *panel_window;
static GtkWidget *clock_label;
static GtkWidget *menubar;
static GtkMenu *session_menu, *language_menu;
static GtkCheckMenuItem *keyboard_menuitem;

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

/* Current choices */
static gchar *current_session;
static gchar *current_language;

#if GTK_CHECK_VERSION (3, 0, 0)
static GdkRGBA *default_background_color = NULL;
#else
static GdkColor *default_background_color = NULL;
#endif
static gboolean cancelling = FALSE, prompted = FALSE;
#if GTK_CHECK_VERSION (3, 0, 0)
#else
static GdkRegion *window_region = NULL;
#endif

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

const WindowPosition CENTERED_WINDOW_POS = { .x = {50, +1, TRUE, 0}, .y = {50, +1, TRUE, 0} };
WindowPosition main_window_pos;

GdkPixbuf* default_user_pixbuf = NULL;
gchar* default_user_icon = "avatar-default";


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
create_menuitem (IndicatorObject *io, IndicatorObjectEntry *entry, GtkWidget *menubar)
{
    GtkWidget *box, *menuitem;

#if GTK_CHECK_VERSION (3, 0, 0)
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
#else
    box = gtk_hbox_new (FALSE, 0);
#endif
    menuitem = gtk_menu_item_new ();

    gtk_widget_add_events(GTK_WIDGET(menuitem), GDK_SCROLL_MASK);

    g_object_set_data (G_OBJECT (menuitem), "indicator-custom-box-data", box);
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
    gtk_menu_shell_append (GTK_MENU_SHELL (menubar), menuitem);

    return menuitem;
}

static void
entry_added (IndicatorObject *io, IndicatorObjectEntry *entry, gpointer user_data)
{
    GHashTable *menuitem_lookup;
    GtkWidget  *menuitem;

    /* if the menuitem doesn't already exist, create it now */
    menuitem_lookup = g_object_get_data (G_OBJECT (io), "indicator-custom-menuitems-data");
    g_return_if_fail (menuitem_lookup);
    menuitem = g_hash_table_lookup (menuitem_lookup, entry);
    if (!GTK_IS_WIDGET (menuitem))
    {
        menuitem = create_menuitem (io, entry, GTK_WIDGET (user_data));
        g_hash_table_insert (menuitem_lookup, entry, menuitem);
    }

    gtk_widget_show (menuitem);
}

static void
entry_removed_cb (GtkWidget *widget, gpointer userdata)
{
    IndicatorObject *io;
    GHashTable      *menuitem_lookup;
    GtkWidget       *menuitem;
    gpointer         entry;

    io = g_object_get_data (G_OBJECT (widget), "indicator-custom-object-data");
    if (!INDICATOR_IS_OBJECT (io))
        return;

    entry = g_object_get_data (G_OBJECT (widget), "indicator-custom-entry-data");
    if (entry != userdata)
        return;

    menuitem_lookup = g_object_get_data (G_OBJECT (io), "indicator-custom-menuitems-data");
    g_return_if_fail (menuitem_lookup);
    menuitem = g_hash_table_lookup (menuitem_lookup, entry);
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
load_module (const gchar *name, GtkWidget *menubar)
{
    IndicatorObject *io;
    GList           *entries, *lp;
    gchar           *path;

    g_return_val_if_fail (name, FALSE);

    if (!g_str_has_suffix (name, G_MODULE_SUFFIX))
        return FALSE;

    path = g_build_filename (INDICATOR_DIR, name, NULL);
    io = indicator_object_new_from_file (path);
    g_free (path);

    /* used to store/fetch menu entries */
    g_object_set_data_full (G_OBJECT (io), "indicator-custom-menuitems-data",
                            g_hash_table_new (g_direct_hash, g_direct_equal),
                            (GDestroyNotify) g_hash_table_destroy);

    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED,
                      G_CALLBACK (entry_added), menubar);
    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED,
                      G_CALLBACK (entry_removed), menubar);
    g_signal_connect (G_OBJECT (io), INDICATOR_OBJECT_SIGNAL_MENU_SHOW,
                      G_CALLBACK (menu_show), menubar);

    entries = indicator_object_get_entries (io);
    for (lp = entries; lp; lp = g_list_next (lp))
        entry_added (io, lp->data, menubar);

    g_list_free (entries);

    return TRUE;
}
#endif

static gchar *
get_session (void)
{
    GList *menu_items, *menu_iter;
    
    /* if the user manually selected a session, use it */
    if (current_session)
        return current_session;

    menu_items = gtk_container_get_children(GTK_CONTAINER(session_menu));
    
    for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next(menu_iter))
    {
        if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_iter->data)))
        {
            return g_strdup(g_object_get_data (G_OBJECT (menu_iter->data), "session-key"));
        }
    }

    return g_strdup (lightdm_greeter_get_default_session_hint (greeter));
}

static void
set_session (const gchar *session)
{
    const gchar *default_session;
    gchar *last_session;
    GList *menu_items, *menu_iter;
    
    menu_items = gtk_container_get_children(GTK_CONTAINER(session_menu));
    
    if (session)
    {
        for (menu_iter = menu_items; menu_iter != NULL; menu_iter = g_list_next(menu_iter))
        {
            gchar *s;
            gboolean matched;
            s = g_strdup(g_object_get_data (G_OBJECT (menu_iter->data), "session-key"));
            matched = g_strcmp0 (s, session) == 0;
            g_free (s);
            if (matched)
            {
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_iter->data), TRUE);
                current_session = g_strdup(session);
                return;
            }
        }
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
    menu_iter = g_list_first(menu_items);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_iter->data), TRUE);
    return;
}

static gchar *
get_language (void)
{
    GList *menu_items, *menu_iter;

    /* if the user manually selected a language, use it */
    if (current_language)
        return current_language;
    
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
                current_language = g_strdup(language);
                return;
            }
        }
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
    /* and disable the session and language comboboxes */
    gtk_widget_set_sensitive (GTK_WIDGET (session_menu), !logged_in);
    gtk_widget_set_sensitive (GTK_WIDGET (language_menu), !logged_in);
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
set_user_image (const gchar *username)
{
    const gchar *path;
    LightDMUser *user;
    GdkPixbuf *image = NULL;
    GError *error = NULL;

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

#if GTK_CHECK_VERSION (3, 0, 0)
/* Use the much simpler fake transparency by drawing the window background with Cairo for Gtk3 */
static gboolean
login_window_expose (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    GtkAllocation *allocation = g_new0 (GtkAllocation, 1);
    GdkRectangle monitor_geometry;
    GdkScreen *screen;

    gtk_widget_get_allocation(widget, allocation);

    screen = gdk_display_get_screen (gdk_display_get_default (), 0);
    gdk_screen_get_monitor_geometry (screen, 0, &monitor_geometry);

    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    gdk_cairo_set_source_pixbuf (cr, background_pixbuf,allocation->width/2-monitor_geometry.width/2, allocation->height/2 - monitor_geometry.height/2);
    cairo_paint (cr);

    g_free (allocation);
    return FALSE;
}

#else
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

gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
password_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
#if GTK_CHECK_VERSION (3, 0, 0)
    if ((event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down) &&
        gtk_widget_get_visible(GTK_WIDGET(user_combo)))
#else
    if ((event->keyval == GDK_Up || event->keyval == GDK_Down) &&
        gtk_widget_get_visible(GTK_WIDGET(user_combo)))
#endif
    {
        gboolean up;
        gboolean available;
        GtkTreeIter iter;
        GtkTreeModel *model = gtk_combo_box_get_model (user_combo);
        #if GTK_CHECK_VERSION (3, 0, 0)
        up = event->keyval == GDK_KEY_Up;
        #else
        up = event->keyval == GDK_Up;
        #endif

        /* Back to username_entry if it is available */
        if (up && gtk_widget_get_visible (GTK_WIDGET (username_entry)) && widget == GTK_WIDGET (password_entry))
        {
            gtk_widget_grab_focus (GTK_WIDGET (username_entry));
            return TRUE;
        }

        if (!gtk_combo_box_get_active_iter (user_combo, &iter))
            return FALSE;

        if (up)
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
#if GTK_CHECK_VERSION (3, 0, 0)
    if (event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_Return)
#else
    if (event->keyval == GDK_Tab || event->keyval == GDK_Return)
#endif
    {
        /* If Shift+Tab, Cycle backwards to previous widget */
        if (event->state & GDK_SHIFT_MASK)
        {
            if (gtk_widget_get_visible(GTK_WIDGET(user_combo)))
            {
                gtk_widget_grab_focus(GTK_WIDGET(user_combo));
            }
            else
            {
                gtk_window_present(panel_window);
                gtk_widget_grab_focus(GTK_WIDGET(menubar));
            }
        }
        else
        {
            gtk_widget_grab_focus(GTK_WIDGET(password_entry));
        }
        return TRUE;
    }
#if GTK_CHECK_VERSION (3, 0, 0)
    else if (event->keyval == GDK_KEY_Up)
#else
    else if (event->keyval == GDK_Up)
#endif
    {
        /* Acts as password_entry */
        return password_key_press_cb (widget, event, user_data);
    }
    return FALSE;
}

gboolean
login_button_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
login_button_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
#if GTK_CHECK_VERSION (3, 0, 0)
    if (event->keyval == GDK_KEY_Tab)
#else
    if (event->keyval == GDK_Tab)
#endif
    {
        if (event->state & GDK_SHIFT_MASK)
            return FALSE;
        gtk_window_present(panel_window);
        gtk_widget_grab_focus(GTK_WIDGET(menubar));

        return TRUE;
    }
    return FALSE;
}

gboolean
menubar_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
menubar_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
#if GTK_CHECK_VERSION (3, 0, 0)
    if (event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_Escape)
#else
    if (event->keyval == GDK_Tab || event->keyval == GDK_Escape)
#endif
    {
        gtk_menu_shell_cancel(GTK_MENU_SHELL(menubar));
        gtk_widget_grab_focus(GTK_WIDGET(user_combo));
        gtk_window_present(login_window);
        return TRUE;
    }
    return FALSE;
}

gboolean
user_combobox_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
G_MODULE_EXPORT
gboolean
user_combobox_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
#if GTK_CHECK_VERSION (3, 0, 0)
    if (event->keyval == GDK_KEY_Tab)
#else
    if (event->keyval == GDK_Tab)
#endif
    {
        if (event->state & GDK_SHIFT_MASK)
        {
            gtk_window_present(panel_window);
            gtk_widget_grab_focus(GTK_WIDGET(menubar));
        }
        else
        {
            if (gtk_widget_get_visible(GTK_WIDGET(username_entry)))
                gtk_widget_grab_focus(GTK_WIDGET(username_entry));
            else
                gtk_widget_grab_focus(GTK_WIDGET(password_entry));
        }
        return TRUE;
    }
    return FALSE;
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

        if (g_strcmp0 (user, "*other") == 0)
        {
            gtk_widget_show (GTK_WIDGET (username_entry));
            gtk_widget_show (GTK_WIDGET (cancel_button));
        }
        else
        {
            gtk_widget_hide (GTK_WIDGET (username_entry));
            gtk_widget_hide (GTK_WIDGET (cancel_button));
            gtk_widget_grab_focus (GTK_WIDGET (password_entry));
        }

        set_login_button_label (greeter, user);
        set_user_background (user);
        set_user_image (user);
        gtk_widget_set_tooltip_text (GTK_WIDGET (user_combo), user);
        start_authentication (user);
        g_free (user);
    }
}

void login_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
login_cb (GtkWidget *widget)
{
    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), FALSE);
    set_message_label ("");

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
        lightdm_greeter_respond (greeter, gtk_entry_get_text (password_entry));
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

    gtk_widget_set_sensitive (GTK_WIDGET (username_entry), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (password_entry), TRUE);
    gtk_entry_set_text (password_entry, "");
    gtk_entry_set_visibility (password_entry, FALSE);
    if (type == LIGHTDM_PROMPT_TYPE_SECRET) // Password
    {
        gtk_widget_grab_focus (GTK_WIDGET (password_entry));
    }
    else
    {
        if (gtk_widget_get_visible ((GTK_WIDGET (username_entry))))
            gtk_widget_grab_focus (GTK_WIDGET (username_entry));
        else
            gtk_widget_grab_focus (GTK_WIDGET (password_entry));
    }
}

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    set_message_label (text);
}

static const gchar*
get_message_label (void)
{
    return gtk_label_get_text (message_label);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    gtk_entry_set_text (password_entry, "");

    if (cancelling)
    {
        cancel_authentication ();
        return;
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
    return x;
}

static void
center_window (GtkWindow *window, GtkAllocation *unused, const WindowPosition *pos)
{   
    GdkScreen *screen = gtk_window_get_screen (window);
    GtkAllocation allocation;
    GdkRectangle monitor_geometry;

    gdk_screen_get_monitor_geometry (screen, gdk_screen_get_primary_monitor (screen), &monitor_geometry);
    gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
    gtk_window_move (window,
                     monitor_geometry.x + get_absolute_position (&pos->x, monitor_geometry.width, allocation.width),
                     monitor_geometry.y + get_absolute_position (&pos->y, monitor_geometry.height, allocation.height));
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

    /* Prepare the dialog */
    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", action);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
    button = gtk_dialog_add_button(GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_widget_set_name(button, "cancel_button");
    button = gtk_dialog_add_button(GTK_DIALOG (dialog), action, GTK_RESPONSE_OK);
    gtk_widget_set_name(button, button_name);

    /* Add the icon */
    image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG);
    gtk_message_dialog_set_image(GTK_MESSAGE_DIALOG(dialog), image);

    /* Make the dialog themeable and attractive */
    gtk_widget_set_name(dialog, dialog_name);
#if GTK_CHECK_VERSION (3, 0, 0)
    g_signal_connect (G_OBJECT (dialog), "draw", G_CALLBACK (login_window_expose), NULL);
#else
    g_signal_connect (G_OBJECT (dialog), "size-allocate", G_CALLBACK (login_window_size_allocate), NULL);
#endif
    gtk_container_set_border_width(GTK_CONTAINER (dialog), 18);

    /* Hide the login window and show the dialog */
    gtk_widget_hide (GTK_WIDGET (login_window));
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog), NULL, &CENTERED_WINDOW_POS);

    result = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;

    gtk_widget_destroy (dialog);
    gtk_widget_show (GTK_WIDGET (login_window));

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
    gtk_check_menu_item_set_active (keyboard_menuitem, FALSE);
}

void a11y_keyboard_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
a11y_keyboard_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
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
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), FALSE);
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
sigterm_cb (int signum)
{
    exit (0);
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
                    set_login_button_label (greeter, selected_user);
                    set_user_background (selected_user);
                    set_user_image (selected_user);
                    gtk_widget_set_tooltip_text (GTK_WIDGET (user_combo), selected_user);
                    start_authentication (selected_user);
                    break;
                }
            } while (gtk_tree_model_iter_next (model, &iter));
        }
        if (!matched)
        {
            gtk_tree_model_get_iter_first (model, &iter);
            gtk_tree_model_get (model, &iter, 0, &name, -1);
            gtk_combo_box_set_active_iter (user_combo, &iter);
            set_login_button_label (greeter, name);
            set_user_background (name);
            set_user_image (name);
            gtk_widget_set_tooltip_text (GTK_WIDGET (user_combo), name);
            start_authentication (name);
            g_free(name);
        }
        
    }

    g_free (last_user);
}

/* The following code for setting a RetainPermanent background pixmap was taken
   originally from Gnome, with some fixes from MATE. see:
   https://github.com/mate-desktop/mate-desktop/blob/master/libmate-desktop/mate-bg.c */
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

    return surface;
}

/* Sets the "ESETROOT_PMAP_ID" property to later be used to free the pixmap,
*/
static void
set_root_pixmap_id (GdkScreen *screen,
                         Display *display,
                         Pixmap xpixmap)
{
    Window xroot = RootWindow (display, gdk_screen_get_number (screen));
    char *atom_names[] = {"_XROOTPMAP_ID", "ESETROOT_PMAP_ID"};
    Atom atoms[G_N_ELEMENTS(atom_names)] = {0};

    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char *data_root, *data_esetroot;

    /* Get atoms for both properties in an array, only if they exist.
     * This method is to avoid multiple round-trips to Xserver
     */
    if (XInternAtoms (display, atom_names, G_N_ELEMENTS(atom_names), True, atoms) &&
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

                gdk_error_trap_push ();
                if (xrootpmap && xrootpmap == esetrootpmap) {
                    XKillClient (display, xrootpmap);
                }
                if (esetrootpmap && esetrootpmap != xrootpmap) {
                    XKillClient (display, esetrootpmap);
                }

                XSync (display, False);
#if GTK_CHECK_VERSION (3, 0, 0)
                gdk_error_trap_pop_ignored ();
#else
                gdk_error_trap_pop ();
#endif

            }
        }
    }

    /* Get atoms for both properties in an array, create them if needed.
     * This method is to avoid multiple round-trips to Xserver
     */
    if (!XInternAtoms (display, atom_names, G_N_ELEMENTS(atom_names), False, atoms) ||
        atoms[0] == None || atoms[1] == None) {
        g_warning("Could not create atoms needed to set root pixmap id/properties.\n");
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
set_surface_as_root (GdkScreen *screen, cairo_surface_t *surface)
{
    g_return_if_fail (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_XLIB);

    /* Desktop background pixmap should be created from dummy X client since most
     * applications will try to kill it with XKillClient later when changing pixmap
     */
    Display *display = GDK_DISPLAY_XDISPLAY (gdk_screen_get_display (screen));
    Pixmap pixmap_id = cairo_xlib_surface_get_drawable (surface);
    Window xroot = RootWindow (display, gdk_screen_get_number (screen));

    XGrabServer (display);

    XSetWindowBackgroundPixmap (display, xroot, pixmap_id);
    set_root_pixmap_id (screen, display, pixmap_id);
    XClearWindow (display, xroot);

    XFlush (display);
    XUngrabServer (display);
}

static void
set_background (GdkPixbuf *new_bg)
{
    GdkRectangle monitor_geometry;
    GdkPixbuf *bg = NULL;
    gint i, p_height, p_width, height, width;
    gdouble scale;

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
                p_width = gdk_pixbuf_get_width(bg);
                p_height = gdk_pixbuf_get_height(bg);

                scale = (double)monitor_geometry.width/p_width;
                height = p_height * scale;
                width = monitor_geometry.width;

                if (height < monitor_geometry.height)
                {
                    scale = (double)monitor_geometry.height/p_height;
                    height = monitor_geometry.height;
                    width = p_width * scale;
                }

                GdkPixbuf *p = gdk_pixbuf_scale_simple (bg, width,
                                                        height, GDK_INTERP_BILINEAR);
                if (width > monitor_geometry.width)
                {
                    GdkPixbuf *tmp = gdk_pixbuf_new_subpixbuf(p, (width-monitor_geometry.width)/2, 0, monitor_geometry.width, monitor_geometry.height);
                    g_object_unref (p);
                    p = tmp;
                }
                if (!gdk_pixbuf_get_has_alpha (p))
                {
                    GdkPixbuf *tmp = gdk_pixbuf_add_alpha (p, FALSE, 255, 255, 255);
                    g_object_unref (p);
                    p = tmp;
                }
                gdk_cairo_set_source_pixbuf (c, p, monitor_geometry.x, monitor_geometry.y);
                /* Make the background pixbuf globally accessible so it can be reused for fake transparency */
                if (background_pixbuf)
                    g_object_unref(background_pixbuf);                
                background_pixbuf = p;
            }
            else
#if GTK_CHECK_VERSION (3, 0, 0)
                gdk_cairo_set_source_rgba (c, default_background_color);
#else
                gdk_cairo_set_source_color (c, default_background_color);
#endif
            cairo_paint (c);
        }

        cairo_destroy (c);

        /* Refresh background */
        gdk_flush ();
        set_surface_as_root(screen, surface);
        cairo_surface_destroy(surface);
    }
}

static gboolean
clock_timeout_thread (void)
{
    time_t rawtime;
    struct tm * timeinfo;
    gchar time_str[50];
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    
    strftime(time_str, 50, clock_format, timeinfo);
    gtk_label_set_markup( GTK_LABEL(clock_label), g_strdup_printf("<b>%s</b>", time_str) );
    
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
    if (xevent->type == MapNotify)
    {
        Window xwin = xevent->xmap.window;
        Window keyboard_xid = 0;
        GdkDisplay* display = gdk_x11_lookup_xdisplay (xevent->xmap.display);
        GdkWindow* win = gdk_x11_window_foreign_new_for_display (display, xwin);
        GdkWindow* keyboard_win = onboard_window ? gtk_widget_get_window (GTK_WIDGET (onboard_window)) : NULL;

        /* Check to see if this window is our onboard window, since we don't want to focus it. */
        if (keyboard_win)
#if GTK_CHECK_VERSION (3, 0, 0)
                keyboard_xid = gdk_x11_window_get_xid (keyboard_win);
#else
                keyboard_xid = gdk_x11_drawable_get_xid (keyboard_win);
#endif
            
        if (xwin != keyboard_xid && gdk_window_get_type_hint (win) != GDK_WINDOW_TYPE_HINT_NOTIFICATION)
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
                gdk_window_raise (gtk_widget_get_window (GTK_WIDGET (onboard_window)));
        }
    }
    return GDK_FILTER_CONTINUE;
}

int
main (int argc, char **argv)
{
    GKeyFile *config;
    GdkRectangle monitor_geometry;
    GtkBuilder *builder;
    const GList *items, *item;
    GtkCellRenderer *renderer;
    GtkWidget *menuitem, *image, *infobar_compat, *content_area;
    gchar *value, *state_dir;
#if GTK_CHECK_VERSION (3, 0, 0)
    GdkRGBA background_color;
    GtkIconTheme *icon_theme;
#else
    GdkColor background_color;
#endif
    GError *error = NULL;
#ifdef HAVE_LIBINDICATOR
    gchar **whitelist;
    GDir *dir;
    gsize length = 0;
    guint indicators_loaded = 0, i;
#endif

    /* Disable global menus */
    g_unsetenv ("UBUNTU_MENUPROXY");

    /* Initialize i18n */
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    signal (SIGTERM, sigterm_cb);

#if GTK_CHECK_VERSION (3, 0, 0)
#else
    /* init threads */
    gdk_threads_init();
#endif

    /* init gtk */
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
#if GTK_CHECK_VERSION (3, 0, 0)
    if (!gdk_rgba_parse (&background_color, value))
#else
    if (!gdk_color_parse (value, &background_color))
#endif
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
#if GTK_CHECK_VERSION (3, 0, 0)
        default_background_color = gdk_rgba_copy (&background_color);
#else
        default_background_color = gdk_color_copy (&background_color);
#endif
    }
    g_free (value);

    /* Force the screen to remain blank in case the session was just locked to reduce VT-switching flickering and to make the greeter behave a bit more like a screensaver than a mere unlock-dialog */
    if (lightdm_greeter_get_lock_hint (greeter))
        XForceScreenSaver(gdk_x11_display_get_xdisplay(gdk_display_get_default ()),ScreenSaverActive);

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
    gtk_style_context_add_class( GTK_STYLE_CONTEXT(gtk_widget_get_style_context(GTK_WIDGET(panel_window))), GTK_STYLE_CLASS_MENUBAR);
#endif
    gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "hostname_label")), lightdm_get_hostname ());
    session_menu = GTK_MENU(gtk_builder_get_object (builder, "session_menu"));
    language_menu = GTK_MENU(gtk_builder_get_object (builder, "language_menu"));
    clock_label = GTK_WIDGET(gtk_builder_get_object (builder, "clock_label"));
    menubar = GTK_WIDGET (gtk_builder_get_object (builder, "menubar"));
    
    keyboard_menuitem = GTK_CHECK_MENU_ITEM (gtk_builder_get_object (builder, "keyboard_menuitem"));

    /* Login window */
    login_window = GTK_WINDOW (gtk_builder_get_object (builder, "login_window"));
    user_image = GTK_IMAGE (gtk_builder_get_object (builder, "user_image"));
    user_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "user_combobox"));
    username_entry = GTK_ENTRY (gtk_builder_get_object (builder, "username_entry"));
    password_entry = GTK_ENTRY (gtk_builder_get_object (builder, "password_entry"));
    
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

#if GTK_CHECK_VERSION (3, 0, 0)
    g_signal_connect (G_OBJECT (login_window), "draw", G_CALLBACK (login_window_expose), NULL);
#else
    g_signal_connect (G_OBJECT (login_window), "size-allocate", G_CALLBACK (login_window_size_allocate), NULL);
#endif
    
    /* To maintain compatability with GTK+2, set special properties here */
#if GTK_CHECK_VERSION (3, 0, 0)
    gtk_box_set_child_packing(GTK_BOX(content_area), GTK_WIDGET(message_label), TRUE, TRUE, 0, GTK_PACK_START);
    gtk_window_set_has_resize_grip(GTK_WINDOW(panel_window), FALSE);
    gtk_widget_set_margin_top(GTK_WIDGET(user_combo), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(password_entry), 12);
    gtk_entry_set_placeholder_text(password_entry, _("Enter your password"));
    gtk_entry_set_placeholder_text(username_entry, _("Enter your username"));
    icon_theme = gtk_icon_theme_get_default();
    
#else
    gtk_widget_set_tooltip_text(GTK_WIDGET(password_entry), _("Enter your password"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(username_entry), _("Enter your username"));
#endif

    /* Glade can't handle custom menuitems, so set them up manually */
#ifdef HAVE_LIBINDICATOR
    /* whitelisted indicator modules to show */
    whitelist = g_key_file_get_string_list (config, "greeter", "show-indicators", &length, NULL);
    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "menubar"));
    /* load indicators */
    if (g_file_test (INDICATOR_DIR, (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)))
    {
        const gchar *name;
        dir = g_dir_open (INDICATOR_DIR, 0, NULL);

        while ((name = g_dir_read_name (dir)))
        {
            gboolean match = FALSE;
            for (i = 0; i < length; ++i)
                if ((match = (g_strcmp0 (name, whitelist[i]) == 0)))
                    break;

            if (G_LIKELY (!match))
            {
                g_debug ("Ignoring module (not in whitelist): %s", name);
                continue;
            }

            if (load_module (name, menuitem))
                ++indicators_loaded;
        }

        g_dir_close (dir);
    }

    if (length > 0)
        g_strfreev (whitelist);

    if (indicators_loaded > 0)
    {
        gtk_widget_set_can_focus (menuitem, TRUE);
        gtk_widget_show (menuitem);
    }
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

    /* Clock */
    gtk_widget_set_visible(GTK_WIDGET(clock_label),
                           g_key_file_get_boolean (config, "greeter", "show-clock", NULL));
    clock_format = g_key_file_get_value (config, "greeter", "clock-format", NULL);
    if (!clock_format)
        clock_format = "%a, %H:%M";

    /* Session menu */
    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "session_menuitem"));
#if GTK_CHECK_VERSION (3, 0, 0)
    if (gtk_icon_theme_has_icon(icon_theme, "document-properties-symbolic"))
        image = gtk_image_new_from_icon_name ("document-properties-symbolic", GTK_ICON_SIZE_MENU);
    else
        image = gtk_image_new_from_icon_name ("document-properties", GTK_ICON_SIZE_MENU);
#else
    image = gtk_image_new_from_icon_name ("document-properties", GTK_ICON_SIZE_MENU);
#endif
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (menuitem), image);
    gtk_widget_show (GTK_WIDGET (menuitem));
    
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

    /* Language menu */
    if (g_key_file_get_boolean (config, "greeter", "show-language-selector", NULL))
    {
        menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "language_menuitem"));
#if GTK_CHECK_VERSION (3, 0, 0)
        if (gtk_icon_theme_has_icon(icon_theme, "preferences-desktop-locale-symbolic"))
            image = gtk_image_new_from_icon_name ("preferences-desktop-locale-symbolic", GTK_ICON_SIZE_MENU);
        else
            image = gtk_image_new_from_icon_name ("preferences-desktop-locale", GTK_ICON_SIZE_MENU);
#else
        image = gtk_image_new_from_icon_name ("preferences-desktop-locale", GTK_ICON_SIZE_MENU);
#endif
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (menuitem), image);
        
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

            gtk_widget_show (GTK_WIDGET (menuitem));
            radiomenuitem = gtk_radio_menu_item_new_with_label (languages, label);
            g_object_set_data (G_OBJECT (radiomenuitem), "language-code", (gpointer) code);
            languages = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radiomenuitem));
            g_signal_connect(G_OBJECT(radiomenuitem), "activate", G_CALLBACK(language_selected_cb), NULL);
            gtk_menu_shell_append (GTK_MENU_SHELL(language_menu), radiomenuitem);
            gtk_widget_show (GTK_WIDGET (radiomenuitem));
        }
        set_language (NULL);
    }
    else
    {
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "language_menuitem")));
    }
    
    /* a11y menu */
    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "a11y_menuitem"));
#if GTK_CHECK_VERSION (3, 0, 0)
    if (gtk_icon_theme_has_icon(icon_theme, "preferences-desktop-accessibility-symbolic"))
        image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility-symbolic", GTK_ICON_SIZE_MENU);
    else
        image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_MENU);
#else
    image = gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_MENU);
#endif
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (menuitem), image);
    
    /* Power menu */
    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "power_menuitem"));
#if GTK_CHECK_VERSION (3, 0, 0)
    if (gtk_icon_theme_has_icon(icon_theme, "system-shutdown-symbolic"))
        image = gtk_image_new_from_icon_name ("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
    else
        image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
#else
    image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
#endif
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (menuitem), image);
    
    if (!lightdm_get_can_suspend ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "suspend_menuitem")));
    if (!lightdm_get_can_hibernate ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "hibernate_menuitem")));
    if (!lightdm_get_can_restart ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "restart_menuitem")));
    if (!lightdm_get_can_shutdown ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "shutdown_menuitem")));

    /* Users combobox */
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (user_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "text", 1);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "weight", 2);

    if (lightdm_greeter_get_hide_users_hint (greeter))
    {
        /* Set the background to default */
        set_background (NULL);
        start_authentication ("*other");
    }
    else
    {
        /* This also sets the background to user's */
        load_user_list ();
        gtk_widget_hide (GTK_WIDGET (cancel_button));
        gtk_widget_show (GTK_WIDGET (user_combo));
    }

    /* Window position */
    /* Default: x-center, y-center */
    main_window_pos = CENTERED_WINDOW_POS;
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
    
    gtk_builder_connect_signals(builder, greeter);

    gtk_widget_show (GTK_WIDGET (login_window));
    center_window (login_window,  NULL, &main_window_pos);
    g_signal_connect (GTK_WIDGET (login_window), "size-allocate", G_CALLBACK (center_window), &main_window_pos);

    gtk_widget_show (GTK_WIDGET (panel_window));
    GtkAllocation allocation;
    gtk_widget_get_allocation (GTK_WIDGET (panel_window), &allocation);
    gdk_screen_get_monitor_geometry (gdk_screen_get_default (), gdk_screen_get_primary_monitor (gdk_screen_get_default ()), &monitor_geometry);
    gtk_window_resize (panel_window, monitor_geometry.width, allocation.height);
    gtk_window_move (panel_window, monitor_geometry.x, monitor_geometry.y);

    gtk_widget_show (GTK_WIDGET (login_window));
    gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (login_window)), GDK_CURRENT_TIME);
    
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (keyboard_menuitem), FALSE);
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
        
        gtk_widget_show (GTK_WIDGET (keyboard_menuitem));
    }
    else
    {
        gtk_widget_hide (GTK_WIDGET (keyboard_menuitem));
    }
    
    gdk_threads_add_timeout( 100, (GSourceFunc) clock_timeout_thread, NULL );

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

    if (background_pixbuf)
        g_object_unref (background_pixbuf);
    if (default_background_pixbuf)
        g_object_unref (default_background_pixbuf);
    if (default_background_color)
#if GTK_CHECK_VERSION (3, 0, 0)
        gdk_rgba_free (default_background_color);
#else
        gdk_color_free (default_background_color);
#endif

    return EXIT_SUCCESS;
}
