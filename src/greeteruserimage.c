/*
 * Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
 * Copyright (C) 2011, Gunnar Hjalmarsson <ubuntu@gunnar.cc>
 * Copyright (C) 2012 - 2013, Lionel Le Folgoc <mrpouit@ubuntu.com>
 * Copyright (C) 2012, Julien Lavergne <gilir@ubuntu.com>
 * Copyright (C) 2013 - 2015, Simon Steinbeiß <ochosi@shimmerproject.org>
 * Copyright (C) 2013 - 2020, Sean Davis <sean@bluesabre.org>
 * Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <lightdm.h>

#include "greeterconfiguration.h"
#include "greeteruserimage.h"

#define USER_IMAGE_SIZE 80

static GdkPixbuf *
round_image (GdkPixbuf *pixbuf)
{
    GdkPixbuf *dest = NULL;
    cairo_surface_t *surface;
    cairo_t *cr;
    gint size;

    size = gdk_pixbuf_get_width (pixbuf);
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
    cr = cairo_create (surface);

    /* Clip a circle */
    cairo_arc (cr, size/2, size/2, size/2, 0, 2 * G_PI);
    cairo_clip (cr);
    cairo_new_path (cr);

    gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
    cairo_paint (cr);

    dest = gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);
    cairo_surface_destroy (surface);
    cairo_destroy (cr);

    return dest;
}

static GdkPixbuf *
get_default_user_image_from_settings (void)
{
    GdkPixbuf *image = NULL;
    gchar *value = NULL;
    GError *error = NULL;

    value = config_get_string (NULL, CONFIG_KEY_DEFAULT_USER_IMAGE, NULL);
    if (!value)
        return NULL;

    if (value[0] == '/')
    {
        image = gdk_pixbuf_new_from_file_at_scale (value,
                                                   USER_IMAGE_SIZE,
                                                   USER_IMAGE_SIZE,
                                                   FALSE,
                                                   &error);

        if (!image)
        {
            g_warning ("Failed to load default user image from path: %s", error->message);
            g_clear_error (&error);
        }
    }
    else if (value[0] == '#')
    {
        image = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                          value + 1,
                                          USER_IMAGE_SIZE,
                                          GTK_ICON_LOOKUP_FORCE_SIZE,
                                          &error);

        if (!image)
        {
            g_warning ("Failed to load default user image from icon theme: %s", error->message);
            g_clear_error (&error);
        }
    }

    g_free (value);

    return image;
}

GdkPixbuf *
get_default_user_image (void)
{
    GdkPixbuf *temp_image = NULL, *image = NULL;
    GError *error = NULL;

    /* If a file is set by preferences, it must be prioritized. */
    image = get_default_user_image_from_settings ();

    /* Fallback to avatar-default icon from theme */
    if (!image)
    {
        image = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                          "avatar-default",
                                          USER_IMAGE_SIZE,
                                          GTK_ICON_LOOKUP_FORCE_SIZE,
                                          &error);

        if (error != NULL)
        {
            g_warning ("Failed to load fallback user image: %s", error->message);
            g_clear_error (&error);
        }
    }

    /* Fallback again to old stock_person icon from theme */
    if (!image)
    {
        image = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                          "stock_person",
                                          USER_IMAGE_SIZE,
                                          GTK_ICON_LOOKUP_FORCE_SIZE,
                                          &error);

        if (error != NULL)
        {
            g_warning ("Failed to load old fallback user image: %s", error->message);
            g_clear_error (&error);
        }
    }

    if (image && config_get_bool (NULL, CONFIG_KEY_ROUND_USER_IMAGE, TRUE))
    {
        temp_image = round_image (image);
        if (temp_image != NULL)
        {
            g_object_unref (image);
            image = temp_image;
        }
    }

    return image;
}

GdkPixbuf *
get_user_image (const gchar *username)
{
    LightDMUser *user = NULL;
    GdkPixbuf *temp_image = NULL, *image = NULL;
    GError *error = NULL;
    const gchar *path;

    user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
    if (user)
    {
        path = lightdm_user_get_image (user);
        if (path)
        {
            image = gdk_pixbuf_new_from_file_at_scale (path,
                                                       USER_IMAGE_SIZE,
                                                       USER_IMAGE_SIZE,
                                                       FALSE,
                                                       &error);
            if (!image)
            {
                g_debug ("Failed to load user image: %s", error->message);
                g_clear_error (&error);
            }
        }
    }

    if (image && config_get_bool (NULL, CONFIG_KEY_ROUND_USER_IMAGE, TRUE))
    {
        temp_image = round_image (image);
        if (temp_image != NULL)
        {
            g_object_unref (image);
            image = temp_image;
        }
    }

    return image;
}

