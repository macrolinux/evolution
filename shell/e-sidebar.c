/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-sidebar.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "e-sidebar.h"

#define E_SIDEBAR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SIDEBAR, ESidebarPrivate))

#define H_PADDING 6
#define V_PADDING 6

#define DEFAULT_TOOLBAR_STYLE	GTK_TOOLBAR_BOTH_HORIZ

struct _ESidebarPrivate {
	GList *proxies;
	gboolean actions_visible;
	gboolean style_set;
	GtkToolbarStyle style;
};

enum {
	PROP_0,
	PROP_ACTIONS_VISIBLE,
	PROP_TOOLBAR_STYLE
};

enum {
	STYLE_CHANGED,
	LAST_SIGNAL
};

static gpointer parent_class;
static guint signals[LAST_SIGNAL];

static int
sidebar_layout_actions (ESidebar *sidebar)
{
	GtkAllocation *allocation = & GTK_WIDGET (sidebar)->allocation;
	gboolean icons_only;
	int num_btns = g_list_length (sidebar->priv->proxies), btns_per_row;
	GList **rows, *p;
	int row_number;
	int max_width = 0, max_height = 0;
	int row_last;
	int x, y;
	int i;

	/*y = allocation->y + allocation->height - V_PADDING - 1;*/
	y = allocation->y + allocation->height - 1;

	if (num_btns == 0)
		return y;

	icons_only = (sidebar->priv->style == GTK_TOOLBAR_ICONS);

	/* Figure out the max width and height */
	for (p = sidebar->priv->proxies; p != NULL; p = p->next) {
		GtkWidget *widget = p->data;
		GtkRequisition requisition;

		gtk_widget_size_request (widget, &requisition);
		max_height = MAX (max_height, requisition.height);
		max_width = MAX (max_width, requisition.width);
	}

	/* Figure out how many rows and columns we'll use. */
	btns_per_row = MAX (1, allocation->width / (max_width + H_PADDING));
	if (!icons_only) {
		/* If using text buttons, we want to try to have a
		 * completely filled-in grid, but if we can't, we want
		 * the odd row to have just a single button.
		 */
		while (num_btns % btns_per_row > 1)
			btns_per_row--;
	}

	/* Assign buttons to rows */
	rows = g_new0 (GList *, num_btns / btns_per_row + 1);

	if (!icons_only && num_btns % btns_per_row != 0) {
		rows [0] = g_list_append (rows [0], sidebar->priv->proxies->data);

		p = sidebar->priv->proxies->next;
		row_number = p ? 1 : 0;
	} else {
		p = sidebar->priv->proxies;
		row_number = 0;
	}

	for (; p != NULL; p = p->next) {
		GtkWidget *widget = p->data;

		if (g_list_length (rows [row_number]) == btns_per_row)
			row_number ++;

		rows [row_number] = g_list_append (rows [row_number], widget);
	}

	row_last = row_number;

	/* Layout the buttons. */
	for (i = row_last; i >= 0; i --) {
		int len, extra_width;

		y -= max_height;
		x = H_PADDING + allocation->x;
		len = g_list_length (rows[i]);
		if (!icons_only)
			extra_width = (allocation->width - (len * max_width ) - (len * H_PADDING)) / len;
		else
			extra_width = 0;
		for (p = rows [i]; p != NULL; p = p->next) {
			GtkAllocation child_allocation;

			child_allocation.x = x;
			child_allocation.y = y;
			child_allocation.width = max_width + extra_width;
			child_allocation.height = max_height;

			gtk_widget_size_allocate (GTK_WIDGET (p->data), &child_allocation);

			x += child_allocation.width + H_PADDING;
		}

		y -= V_PADDING;
	}

	for (i = 0; i <= row_last; i ++)
		g_list_free (rows [i]);
	g_free (rows);

	return y;
}

static void
sidebar_set_property (GObject *object,
                      guint property_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACTIONS_VISIBLE:
			e_sidebar_set_actions_visible (
				E_SIDEBAR (object),
				g_value_get_boolean (value));
			return;

		case PROP_TOOLBAR_STYLE:
			e_sidebar_set_style (
				E_SIDEBAR (object),
				g_value_get_enum (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
sidebar_get_property (GObject *object,
                      guint property_id,
                      GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACTIONS_VISIBLE:
			g_value_set_boolean (
				value, e_sidebar_get_actions_visible (
				E_SIDEBAR (object)));
			return;

		case PROP_TOOLBAR_STYLE:
			g_value_set_enum (
				value, e_sidebar_get_style (
				E_SIDEBAR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
sidebar_dispose (GObject *object)
{
	ESidebarPrivate *priv = E_SIDEBAR (object)->priv;

	while (priv->proxies != NULL) {
		GtkWidget *widget = priv->proxies->data;
		gtk_container_remove (GTK_CONTAINER (object), widget);
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
sidebar_size_request (GtkWidget *widget,
                      GtkRequisition *requisition)
{
	ESidebarPrivate *priv;
	GtkWidget *child;
	GList *iter;

	priv = E_SIDEBAR_GET_PRIVATE (widget);
	child = gtk_bin_get_child (GTK_BIN (widget));

	if (child == NULL) {
		requisition->width = 2 * H_PADDING;
		requisition->height = V_PADDING;
	} else {
		gtk_widget_size_request (child, requisition);
	}

	if (!priv->actions_visible)
		return;

	for (iter = priv->proxies; iter != NULL; iter = iter->next) {
		GtkWidget *widget = iter->data;
		GtkRequisition child_requisition;

		gtk_widget_size_request (widget, &child_requisition);

		child_requisition.width += H_PADDING;
		child_requisition.height += V_PADDING;

		requisition->width = MAX (
			requisition->width, child_requisition.width);
		requisition->height += child_requisition.height;
	}
}

static void
sidebar_size_allocate (GtkWidget *widget,
                       GtkAllocation *allocation)
{
	ESidebarPrivate *priv;
	GtkWidget *child;
	gint y;

	priv = E_SIDEBAR_GET_PRIVATE (widget);

	widget->allocation = *allocation;

	if (priv->actions_visible)
		y = sidebar_layout_actions (E_SIDEBAR (widget));
	else
		y = allocation->y + allocation->height;

	child = gtk_bin_get_child (GTK_BIN (widget));

	if (child != NULL) {
		GtkAllocation child_allocation;

		child_allocation.x = allocation->x;
		child_allocation.y = allocation->y;
		child_allocation.width = allocation->width;
		child_allocation.height = y - allocation->y;

		gtk_widget_size_allocate (child, &child_allocation);
	}
}

static void
sidebar_remove (GtkContainer *container,
                GtkWidget *widget)
{
	ESidebarPrivate *priv;
	GList *link;

	priv = E_SIDEBAR_GET_PRIVATE (container);

	/* Look in the internal widgets first. */
	link = g_list_find (priv->proxies, widget);
	if (link != NULL) {
		GtkWidget *widget = link->data;

		gtk_widget_unparent (widget);
		priv->proxies = g_list_delete_link (priv->proxies, link);
		gtk_widget_queue_resize (GTK_WIDGET (container));
		return;
	}

	/* Chain up to parent's remove() method. */
	GTK_CONTAINER_CLASS (parent_class)->remove (container, widget);
}

static void
sidebar_forall (GtkContainer *container,
                gboolean include_internals,
                GtkCallback callback,
                gpointer callback_data)
{
	ESidebarPrivate *priv;

	priv = E_SIDEBAR_GET_PRIVATE (container);

	if (include_internals)
		g_list_foreach (
			priv->proxies, (GFunc) callback, callback_data);

	/* Chain up to parent's forall() method. */
	GTK_CONTAINER_CLASS (parent_class)->forall (
		container, include_internals, callback, callback_data);
}

static void
sidebar_style_changed (ESidebar *sidebar,
                       GtkToolbarStyle style)
{
	if (sidebar->priv->style == style)
		return;

	sidebar->priv->style = style;

	g_list_foreach (
		sidebar->priv->proxies,
		(GFunc) gtk_tool_item_toolbar_reconfigured, NULL);

	gtk_widget_queue_resize (GTK_WIDGET (sidebar));
	g_object_notify (G_OBJECT (sidebar), "toolbar-style");
}

static GtkIconSize
sidebar_get_icon_size (GtkToolShell *shell)
{
	return GTK_ICON_SIZE_LARGE_TOOLBAR;
}

static GtkOrientation
sidebar_get_orientation (GtkToolShell *shell)
{
	return GTK_ORIENTATION_HORIZONTAL;
}

static GtkToolbarStyle
sidebar_get_style (GtkToolShell *shell)
{
	return e_sidebar_get_style (E_SIDEBAR (shell));
}

static GtkReliefStyle
sidebar_get_relief_style (GtkToolShell *shell)
{
	return GTK_RELIEF_NORMAL;
}

static void
sidebar_class_init (ESidebarClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (ESidebarPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = sidebar_set_property;
	object_class->get_property = sidebar_get_property;
	object_class->dispose = sidebar_dispose;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->size_request = sidebar_size_request;
	widget_class->size_allocate = sidebar_size_allocate;

	container_class = GTK_CONTAINER_CLASS (class);
	container_class->remove = sidebar_remove;
	container_class->forall = sidebar_forall;

	class->style_changed = sidebar_style_changed;

	g_object_class_install_property (
		object_class,
		PROP_ACTIONS_VISIBLE,
		g_param_spec_boolean (
			"actions-visible",
			NULL,
			NULL,
			TRUE,
			G_PARAM_CONSTRUCT |
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_TOOLBAR_STYLE,
		g_param_spec_enum (
			"toolbar-style",
			NULL,
			NULL,
			GTK_TYPE_TOOLBAR_STYLE,
			DEFAULT_TOOLBAR_STYLE,
			G_PARAM_CONSTRUCT |
			G_PARAM_READWRITE));

	signals[STYLE_CHANGED] = g_signal_new (
		"style-changed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ESidebarClass, style_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__ENUM,
		G_TYPE_NONE, 1,
		GTK_TYPE_TOOLBAR_STYLE);
}

static void
sidebar_init (ESidebar *sidebar)
{
	sidebar->priv = E_SIDEBAR_GET_PRIVATE (sidebar);

	GTK_WIDGET_SET_FLAGS (sidebar, GTK_NO_WINDOW);
}

static void
sidebar_tool_shell_iface_init (GtkToolShellIface *iface)
{
	iface->get_icon_size = sidebar_get_icon_size;
	iface->get_orientation = sidebar_get_orientation;
	iface->get_style = sidebar_get_style;
	iface->get_relief_style = sidebar_get_relief_style;
}

GType
e_sidebar_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (ESidebarClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) sidebar_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (ESidebar),
			0,     /* n_preallocs */
			(GInstanceInitFunc) sidebar_init,
			NULL   /* value_table */
		};

		static const GInterfaceInfo tool_shell_info = {
			(GInterfaceInitFunc) sidebar_tool_shell_iface_init,
			(GInterfaceFinalizeFunc) NULL,
			NULL   /* interface_data */
		};

		type = g_type_register_static (
			GTK_TYPE_BIN, "ESidebar", &type_info, 0);

		g_type_add_interface_static (
			type, GTK_TYPE_TOOL_SHELL, &tool_shell_info);
	}

	return type;
}

GtkWidget *
e_sidebar_new (void)
{
	return g_object_new (E_TYPE_SIDEBAR, NULL);
}

void
e_sidebar_add_action (ESidebar *sidebar,
                      GtkAction *action)
{
	GtkWidget *widget;

	g_return_if_fail (E_IS_SIDEBAR (sidebar));
	g_return_if_fail (GTK_IS_ACTION (action));

	g_object_ref (action);
	widget = gtk_action_create_tool_item (action);
	gtk_tool_item_set_is_important (GTK_TOOL_ITEM (widget), TRUE);
	gtk_widget_show (widget);

	sidebar->priv->proxies = g_list_append (
		sidebar->priv->proxies, widget);

	gtk_widget_set_parent (widget, GTK_WIDGET (sidebar));
	gtk_widget_queue_resize (GTK_WIDGET (sidebar));
}

gboolean
e_sidebar_get_actions_visible (ESidebar *sidebar)
{
	g_return_val_if_fail (E_IS_SIDEBAR (sidebar), FALSE);

	return sidebar->priv->actions_visible;
}

void
e_sidebar_set_actions_visible (ESidebar *sidebar,
                               gboolean visible)
{
	GList *iter;

	if (sidebar->priv->actions_visible == visible)
		return;

	sidebar->priv->actions_visible = visible;

	for (iter = sidebar->priv->proxies; iter != NULL; iter = iter->next)
		g_object_set (iter->data, "visible", visible, NULL);

	gtk_widget_queue_resize (GTK_WIDGET (sidebar));

	g_object_notify (G_OBJECT (sidebar), "actions-visible");
}

GtkToolbarStyle
e_sidebar_get_style (ESidebar *sidebar)
{
	g_return_val_if_fail (E_IS_SIDEBAR (sidebar), DEFAULT_TOOLBAR_STYLE);

	return sidebar->priv->style;
}

void
e_sidebar_set_style (ESidebar *sidebar,
                     GtkToolbarStyle style)
{
	g_return_if_fail (E_IS_SIDEBAR (sidebar));

	sidebar->priv->style_set = TRUE;
	g_signal_emit (sidebar, signals[STYLE_CHANGED], 0, style);
}

void
e_sidebar_unset_style (ESidebar *sidebar)
{
	GtkSettings *settings;
	GtkToolbarStyle style;

	g_return_if_fail (E_IS_SIDEBAR (sidebar));

	if (!sidebar->priv->style_set)
		return;

	settings = gtk_widget_get_settings (GTK_WIDGET (sidebar));
	g_object_get (settings, "gtk-toolbar-style", &style, NULL);

	if (style == GTK_TOOLBAR_BOTH)
		style = GTK_TOOLBAR_BOTH_HORIZ;

	if (style != sidebar->priv->style)
		g_signal_emit (sidebar, signals[STYLE_CHANGED], 0, style);

	sidebar->priv->style_set = FALSE;
}
