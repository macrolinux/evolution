/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Damon Chaplin <damon@ximian.com>
 *	    Ettore Perazzoli <ettore@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-ui-util.h>
#include <e-util/e-dialog-utils.h>
#include <e-util/e-icon-factory.h>
#include <e-util/e-print.h>
#include <e-util/e-util-private.h>
#include "dialogs/cal-prefs-dialog.h"
#include "calendar-config.h"
#include "calendar-commands.h"
#include "e-tasks.h"
#include "e-calendar-table.h"
#include "print.h"
#include "tasks-control.h"
#include "evolution-shell-component-utils.h"
#include "e-util/e-menu.h"
#include "e-cal-menu.h"
#include "e-cal-component-preview.h"
#include "e-util/e-menu.h"
#include "itip-utils.h"

#define FIXED_MARGIN                            .05


static void tasks_control_activate_cb		(BonoboControl		*control,
						 gboolean		 activate,
						 gpointer		 user_data);
static void tasks_control_new_task_cmd		(BonoboUIComponent	*uic,
						 gpointer		 data,
						 const char		*path);
static void tasks_control_complete_cmd		(BonoboUIComponent	*uic,
						 gpointer		 data,
						 const char		*path);
static void tasks_control_purge_cmd		(BonoboUIComponent	*uic,
						 gpointer		 data,
						 const char		*path);
static void tasks_control_assign_cmd           (BonoboUIComponent      *uic,
                                                gpointer               data,
                                                const gchar             *path);

static void tasks_control_forward_cmd          (BonoboUIComponent      *uic,
                                                gpointer               data,
                                                const gchar             *path);

struct _tasks_sensitize_item {
	const gchar *command;
	guint32 enable;
};

static void
sensitize_items(BonoboUIComponent *uic, struct _tasks_sensitize_item *items, guint32 mask)
{
	while (items->command) {
		gchar command[32];

		if (strlen(items->command)>=21) {
			g_warning ("Size more than 21: %s\n", items->command);
			continue;
		}

		sprintf(command, "/commands/%s", items->command);

		bonobo_ui_component_set_prop (uic, command, "sensitive",
					      (items->enable & mask) == 0 ? "1" : "0",
					      NULL);
		items++;
	}
}

#define E_CAL_TASKS_PREVIEW_ACTIVE (1<<31)

static struct _tasks_sensitize_item tasks_sensitize_table[] = {
	{ "TasksOpenTask", E_CAL_MENU_SELECT_ONE },
	{ "TasksCut", E_CAL_MENU_SELECT_ANY | E_CAL_MENU_SELECT_EDITABLE | E_CAL_TASKS_PREVIEW_ACTIVE },
	{ "TasksCopy", E_CAL_MENU_SELECT_ANY },
	{ "TasksPaste", E_CAL_MENU_SELECT_EDITABLE | E_CAL_TASKS_PREVIEW_ACTIVE },
	{ "TasksDelete", E_CAL_MENU_SELECT_ANY | E_CAL_MENU_SELECT_EDITABLE },
	{ "TasksMarkComplete", E_CAL_MENU_SELECT_ANY | E_CAL_MENU_SELECT_EDITABLE | E_CAL_MENU_SELECT_NOTCOMPLETE},
	{ "TasksPurge",  E_CAL_MENU_SELECT_EDITABLE },
	{ "TasksAssign", E_CAL_MENU_SELECT_ONE | E_CAL_MENU_SELECT_EDITABLE | E_CAL_MENU_SELECT_ASSIGNABLE },
	{ "TasksForward", E_CAL_MENU_SELECT_ONE },
	{ NULL }
};

/* Sensitizes the UI Component menu/toolbar commands based on the number of
 * selected tasks.
 */
void
tasks_control_sensitize_commands (BonoboControl *control, ETasks *tasks, gint n_selected)
{
	BonoboUIComponent *uic;
	gboolean read_only = TRUE;
	ECal *ecal;
	ECalModel *model;
	ECalMenu *menu;
	ECalMenuTargetSelect *t;
	GPtrArray *events;
	GSList *selected = NULL, *l = NULL;
	ECalendarTable *cal_table;
	GtkWidget *preview;

	uic = bonobo_control_get_ui_component (control);
	g_return_if_fail (uic != NULL);

	if (bonobo_ui_component_get_container (uic) == CORBA_OBJECT_NIL)
		return;

	menu = e_tasks_get_tasks_menu (tasks);
	cal_table = e_tasks_get_calendar_table (tasks);
	model = e_calendar_table_get_model (cal_table);
	events = g_ptr_array_new ();
	selected = e_calendar_table_get_selected (cal_table);

	for (l = selected;l;l = g_slist_next (l)) {
		g_ptr_array_add (events, e_cal_model_copy_component_data ((ECalModelComponent *)l->data));
	}

	g_slist_free (selected);

	t = e_cal_menu_target_new_select (menu, model, events);

	ecal = e_cal_model_get_default_client (model);

	if (ecal)
		e_cal_is_read_only (ecal, &read_only, NULL);

	preview = e_cal_component_preview_get_html (E_CAL_COMPONENT_PREVIEW (e_tasks_get_preview (tasks)));
	if (preview && GTK_WIDGET_VISIBLE (preview) && GTK_WIDGET_HAS_FOCUS (preview))
		t->target.mask = t->target.mask | E_CAL_TASKS_PREVIEW_ACTIVE;
	else
		t->target.mask = t->target.mask & (~E_CAL_TASKS_PREVIEW_ACTIVE);

	sensitize_items (uic, tasks_sensitize_table, t->target.mask);
	e_menu_update_target ((EMenu *)menu, (EMenuTarget *)t);
}

/* Callback used when the selection in the table changes */
static void
selection_changed_cb (ETasks *tasks, gint n_selected, gpointer data)
{
	BonoboControl *control;

	control = BONOBO_CONTROL (data);

	tasks_control_sensitize_commands (control, tasks, n_selected);
}

static gboolean
tasks_control_focus_changed (GtkWidget *widget, GdkEventFocus *event, struct focus_changed_data *fc_data)
{
	g_return_val_if_fail (fc_data != NULL, FALSE);

	tasks_control_sensitize_commands (fc_data->control, fc_data->tasks, -1);

	return FALSE;
}

static BonoboUIVerb verbs [] = {
	BONOBO_UI_VERB ("TasksNewTask", tasks_control_new_task_cmd),
	BONOBO_UI_VERB ("TasksMarkComplete", tasks_control_complete_cmd),
	BONOBO_UI_VERB ("TasksPurge", tasks_control_purge_cmd),
	BONOBO_UI_VERB ("TasksAssign", tasks_control_assign_cmd),
        BONOBO_UI_VERB ("TasksForward", tasks_control_forward_cmd),
	BONOBO_UI_VERB_END
};

void
tasks_control_activate (BonoboControl *control, ETasks *tasks)
{
	Bonobo_UIContainer remote_uih;
	BonoboUIComponent *uic;
	gint n_selected;
	ECalendarTable *cal_table;
	ETable *etable;
	gboolean state;
	gchar *xmlfile;

	uic = bonobo_control_get_ui_component (control);
	g_return_if_fail (uic != NULL);

	remote_uih = bonobo_control_get_remote_ui_container (control, NULL);
	bonobo_ui_component_set_container (uic, remote_uih, NULL);
	bonobo_object_release_unref (remote_uih, NULL);

	e_tasks_set_ui_component (tasks, uic);

	bonobo_ui_component_add_verb_list_with_data (uic, verbs, tasks);

	bonobo_ui_component_freeze (uic, NULL);

	xmlfile = g_build_filename (EVOLUTION_UIDIR,
				    "evolution-tasks.xml",
				    NULL);
	bonobo_ui_util_set_ui (uic, PREFIX,
			       xmlfile,
			       "evolution-tasks",
			       NULL);
	g_free (xmlfile);

	e_tasks_setup_view_menus (tasks, uic);

	/* Signals from the tasks widget; also sensitize the menu items as appropriate */

	g_signal_connect (tasks, "selection_changed", G_CALLBACK (selection_changed_cb), control);

	e_menu_activate ((EMenu *)e_tasks_get_tasks_menu (tasks), uic, 1);
	cal_table = e_tasks_get_calendar_table (tasks);
	etable = e_calendar_table_get_table (cal_table);
	n_selected = e_table_selected_count (etable);

	tasks_control_sensitize_commands (control, tasks, n_selected);

	state = calendar_config_get_preview_state();

	bonobo_ui_component_thaw (uic, NULL);

	bonobo_ui_component_add_listener(uic, "ViewPreview", tasks_control_view_preview, tasks);
	bonobo_ui_component_set_prop(uic, "/commands/ViewPreview", "state", state?"1":"0", NULL);
}


void
tasks_control_deactivate (BonoboControl *control, ETasks *tasks)
{
	BonoboUIComponent *uic = bonobo_control_get_ui_component (control);

	g_return_if_fail (uic != NULL);

	e_menu_activate ((EMenu *)e_tasks_get_tasks_menu (tasks), uic, 0);
	e_tasks_set_ui_component (tasks, NULL);

	e_tasks_discard_view_menus (tasks);

	/* Stop monitoring the "selection_changed" signal */
	g_signal_handlers_disconnect_matched (tasks, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, control);

	bonobo_ui_component_rm (uic, "/", NULL);
	bonobo_ui_component_unset_container (uic, NULL);
}

static void
tasks_control_new_task_cmd		(BonoboUIComponent	*uic,
					 gpointer		 data,
					 const char		*path)
{
	ETasks *tasks;

	tasks = E_TASKS (data);
	e_tasks_new_task (tasks);
}

static void
tasks_control_complete_cmd		(BonoboUIComponent	*uic,
					 gpointer		 data,
					 const char		*path)
{
	ETasks *tasks;

	bonobo_ui_component_set_prop (uic, "/commands/TasksMarkComplete", "sensitive",
					      "0",
					      NULL);
	tasks = E_TASKS (data);
	e_tasks_complete_selected (tasks);
}

static gboolean
confirm_purge (ETasks *tasks)
{
	GtkWidget *dialog, *checkbox, *parent;
	gint button;

	if (!calendar_config_get_confirm_purge ())
		return TRUE;

	parent = gtk_widget_get_toplevel (GTK_WIDGET (tasks));
	dialog = gtk_message_dialog_new (
		(GtkWindow *)parent,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_WARNING,
		GTK_BUTTONS_YES_NO,
		"%s",
		_("This operation will permanently erase all tasks marked as completed. If you continue, you will not be able to recover these tasks.\n\nReally erase these tasks?"));
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_NO);

	checkbox = gtk_check_button_new_with_label (_("Do not ask me again."));
	gtk_widget_show (checkbox);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), checkbox, TRUE, TRUE, 6);

	button = gtk_dialog_run (GTK_DIALOG (dialog));
	if (button == GTK_RESPONSE_YES && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)))
		calendar_config_set_confirm_purge (FALSE);
	gtk_widget_destroy (dialog);

	return button == GTK_RESPONSE_YES ? TRUE : FALSE;
}

static void
tasks_control_purge_cmd	(BonoboUIComponent	*uic,
			 gpointer		 data,
			 const char		*path)
{
	ETasks *tasks;

	tasks = E_TASKS (data);
	if (confirm_purge (tasks))
	    e_tasks_delete_completed (tasks);
}
