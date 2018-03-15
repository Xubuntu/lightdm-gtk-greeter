/*
 * Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef GREETER_MENU_BAR_H
#define GREETER_MENU_BAR_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GREETER_MENU_BAR_TYPE            (greeter_menu_bar_get_type())
#define GREETER_MENU_BAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GREETER_MENU_BAR_TYPE, GreeterMenuBar))
#define GREETER_MENU_BAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GREETER_MENU_BAR_TYPE, GreeterMenuBarClass))
#define GREETER_IS_MENU_BAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GREETER_MENU_BAR_TYPE))
#define GREETER_IS_MENU_BAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GREETER_MENU_BAR_TYPE))

typedef struct _GreeterMenuBar       GreeterMenuBar;
typedef struct _GreeterMenuBarClass  GreeterMenuBarClass;

struct _GreeterMenuBar
{
	GtkMenuBar parent_instance;
};

struct _GreeterMenuBarClass
{
	GtkMenuBarClass parent_class;
};

GType greeter_menu_bar_get_type(void) G_GNUC_CONST;
GtkWidget *greeter_menu_bar_new(void);

G_END_DECLS

#endif /* GREETER_MENU_BAR_H */
