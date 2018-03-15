/*
 * Copyright (C) 2015 - 2017, Sean Davis <smd.seandavis@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <glib.h>

#include "greeterconfiguration.h"


static GKeyFile* greeter_config = NULL;
static GKeyFile* state_config = NULL;
static gchar* state_filename = NULL;

static GKeyFile* get_file_for_group (const gchar** group);
static void save_key_file           (GKeyFile* config, const gchar* path);
static gboolean get_int             (GKeyFile* config, const gchar* group, const gchar* key, gint* out);
static gboolean get_bool            (GKeyFile* config, const gchar* group, const gchar* key, gboolean* out);

/* Implementation */

static GList*
append_directory_content(GList* files, const gchar* path)
{
    GError *error = NULL;
    GList  *content = NULL;
    GList  *list_iter = NULL;
    gchar  *full_path = g_build_filename(path, "lightdm", "lightdm-gtk-greeter.conf.d", NULL);
    GDir   *dir = g_dir_open(full_path, 0, &error);

    if(error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning("[Configuration] Failed to read configuration directory '%s': %s", full_path, error->message);
    g_clear_error(&error);

    if(dir)
    {
        const gchar *name;
        while((name = g_dir_read_name(dir)))
        {
            if(!g_str_has_suffix(name, ".conf"))
                continue;
            content = g_list_prepend(content, g_build_filename(full_path, name, NULL));
        }
        g_dir_close(dir);

        if(content)
            content = g_list_sort(content, (GCompareFunc)g_strcmp0);
    }

    content = g_list_append(content, g_build_filename(path, "lightdm", "lightdm-gtk-greeter.conf", NULL));

    for(list_iter = content; list_iter; list_iter = g_list_next(list_iter))
    {
        if(g_file_test(list_iter->data, G_FILE_TEST_IS_REGULAR))
            files = g_list_prepend(files, list_iter->data);
        else
            g_free(list_iter->data);
    }

    g_list_free(content);
    g_free(full_path);
    return files;
}

void
config_init(void)
{
    GKeyFile            *tmp_config = NULL;
    GError              *error = NULL;
    GList               *files = NULL;
    GList               *file_iter = NULL;
    const gchar* const  *dirs;
    gchar               *state_config_dir;
    gchar               *config_path_tmp;
    gchar               *config_path;
    gint                i;

    state_config_dir = g_build_filename(g_get_user_cache_dir(), "lightdm-gtk-greeter", NULL);
    state_filename = g_build_filename(state_config_dir, "state", NULL);
    g_mkdir_with_parents(state_config_dir, 0775);
    g_free(state_config_dir);

    state_config = g_key_file_new();
    g_key_file_load_from_file(state_config, state_filename, G_KEY_FILE_NONE, &error);
    if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning("[Configuration] Failed to load state from %s: %s", state_filename, error->message);
    g_clear_error(&error);

    dirs = g_get_system_data_dirs();
    for(i = 0; dirs[i]; ++i)
        files = append_directory_content(files, dirs[i]);

    dirs = g_get_system_config_dirs();
    for(i = 0; dirs[i]; ++i)
        files = append_directory_content(files, dirs[i]);

    config_path_tmp = g_path_get_dirname(CONFIG_FILE);
    config_path = g_path_get_dirname(config_path_tmp);
    files = append_directory_content(files, config_path);
    g_free(config_path_tmp);
    g_free(config_path);

    files = g_list_reverse(files);

    for(file_iter = files; file_iter; file_iter = g_list_next(file_iter))
    {
        const gchar  *path = file_iter->data;
        gchar       **group_iter = NULL;
        gchar       **groups;

        if(!tmp_config)
            tmp_config = g_key_file_new();

        if(!g_key_file_load_from_file(tmp_config, path, G_KEY_FILE_NONE, &error))
        {
            if(error)
            {
                g_warning("[Configuration] Failed to read file '%s': %s", path, error->message);
                g_clear_error(&error);
            }
            else
                g_warning("[Configuration] Failed to read file '%s'", path);
            continue;
        }
        g_message("[Configuration] Reading file: %s", path);

        if(!greeter_config)
        {
            greeter_config = tmp_config;
            tmp_config = NULL;
            continue;
        }

        groups = g_key_file_get_groups(tmp_config, NULL);
        for(group_iter = groups; *group_iter; ++group_iter)
        {
            gchar **key_iter = NULL;
            gchar **keys = NULL;
            if(**group_iter == '-')
            {
                g_key_file_remove_group(greeter_config, *group_iter + 1, NULL);
                continue;
            }

            keys = g_key_file_get_keys(tmp_config, *group_iter, NULL, NULL);
            for(key_iter = keys; *key_iter; ++key_iter)
            {
                gchar *value = NULL;

                if(**key_iter == '-')
                {
                    g_key_file_remove_key(greeter_config, *group_iter, *key_iter + 1, NULL);
                    continue;
                }

                value = g_key_file_get_value(tmp_config, *group_iter, *key_iter, NULL);
                if(value)
                {
                    g_key_file_set_value(greeter_config, *group_iter, *key_iter, value);
                    g_free(value);
                }
            }
            g_strfreev(keys);
        }
        g_strfreev(groups);
    }
    if (tmp_config)
        g_key_file_unref(tmp_config);
    g_list_free_full(files, g_free);

    if(!greeter_config)
        greeter_config = g_key_file_new();
}

static GKeyFile*
get_file_for_group(const gchar** group)
{
    if(!*group)
        *group = CONFIG_GROUP_DEFAULT;

    if(*group[0] == '/')
    {
        (*group)++;
        return state_config;
    }

    return greeter_config;
}

static void
save_key_file(GKeyFile* config, const gchar* path)
{
    GError* error = NULL;
    gsize data_length = 0;
    gchar* data = g_key_file_to_data(config, &data_length, &error);

    if(error)
    {
        g_warning("[Configuration] Failed to save file: %s", error->message);
        g_clear_error(&error);
    }

    if(data)
    {
        g_file_set_contents(path, data, data_length, &error);
        if(error)
        {
            g_warning("[Configuration] Failed to save file: %s", error->message);
            g_clear_error(&error);
        }
        g_free(data);
    }
}

static gboolean
get_int(GKeyFile* config, const gchar* group, const gchar* key, gint* out)
{
    GError* error = NULL;
    *out = g_key_file_get_integer(config, group, key, &error);
    if(!error)
        return TRUE;
    if(g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
        g_warning("[Configuration] Failed to parse integer value [%s] %s: %s", group, key, error->message);
    g_clear_error(&error);
    return FALSE;
}

static gboolean
get_bool(GKeyFile* config, const gchar* group, const gchar* key, gboolean* out)
{
    GError* error = NULL;
    *out = g_key_file_get_boolean(config, group, key, &error);
    if(!error)
        return TRUE;
    if(g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
        g_warning("[Configuration] Failed to parse boolean value [%s] %s: %s", group, key, error->message);
    g_clear_error(&error);
    return FALSE;
}

gchar**
config_get_groups(const gchar* prefix)
{
    gsize groups_size = 0, i, next;
    gchar** groups = g_key_file_get_groups(greeter_config, &groups_size);

    for(i = next = 0; i < groups_size; ++i)
        if(groups[i] && g_str_has_prefix(groups[i], prefix))
        {
            if(i != next)
            {
                g_free (groups[next]);
                groups[next] = groups[i];
                groups[i] = NULL;
            }
            ++next;
        }

    if(groups)
        groups[next] = NULL;

    return groups;
}

gboolean
config_has_key(const gchar* group, const gchar* key)
{
    GKeyFile* file = get_file_for_group(&group);
    return g_key_file_has_key(file, group, key, NULL);
}

gchar*
config_get_string(const gchar* group, const gchar* key, const gchar* fallback)
{
    GKeyFile* file = get_file_for_group(&group);
    gchar* value = g_key_file_get_value(file, group, key, NULL);
    return value || !fallback ? value : g_strdup(fallback);
}

void
config_set_string(const gchar* group, const gchar* key, const gchar* value)
{
    if(get_file_for_group(&group) != state_config)
    {
        g_warning("[Configuration] %s(%s, %s, '%s')", __func__, group, key, value);
        return;
    }

    g_key_file_set_value(state_config, group, key, value);
    save_key_file(state_config, state_filename);
}

gchar**
config_get_string_list(const gchar* group, const gchar* key, gchar** fallback)
{
    GKeyFile* file = get_file_for_group(&group);
    gchar** value = g_key_file_get_string_list(file, group, key, NULL, NULL);
    return value || !fallback ? value : g_strdupv(fallback);
}

gint
config_get_int(const gchar* group, const gchar* key, gint fallback)
{
    GKeyFile* file = get_file_for_group(&group);
    gint value;
    if(!get_int(file, group, key, &value))
        return fallback;
    return value;
}

void
config_set_int(const gchar* group, const gchar* key, gint value)
{
    if(get_file_for_group(&group) != state_config)
    {
        g_warning("[Configuration] %s(%s, %s, %d)", __func__, group, key, value);
        return;
    }

    g_key_file_set_integer(state_config, group, key, value);
    save_key_file(state_config, state_filename);
}

gboolean
config_get_bool(const gchar* group, const gchar* key, gboolean fallback)
{
    GKeyFile* file = get_file_for_group(&group);
    gboolean value;
    if(!get_bool(file, group, key, &value))
        return fallback;
    return value;
}

void
config_set_bool(const gchar* group, const gchar* key, gboolean value)
{
    if(get_file_for_group(&group) != state_config)
    {
        g_warning("[Configuration] %s(%s, %s, %d)", __func__, group, key, value);
        return;
    }

    g_key_file_set_boolean(state_config, group, key, value);
    save_key_file(state_config, state_filename);
}

gint
config_get_enum(const gchar* group, const gchar* key, gint fallback, const gchar* first_item, ...)
{
    const gchar *item_name;
    GKeyFile    *file;
    gchar       *value;
    gboolean     found = FALSE;
    va_list      var_args;

    if(!first_item)
        return fallback;

    file = get_file_for_group(&group);
    value = g_key_file_get_value(file, group, key, NULL);

    if(!value)
        return fallback;

    va_start(var_args, first_item);

    item_name = first_item;
    while(item_name)
    {
        gint item_value = va_arg(var_args, gint);
        if(g_strcmp0(value, item_name) == 0)
        {
            found = TRUE;
            fallback = item_value;
            break;
        }
        item_name = va_arg(var_args, gchar*);
    }
    va_end(var_args);

    if(!found)
        g_warning("[Configuration] Failed to parse enum value [%s] %s: %s", group, key, value);

    return fallback;
}
