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
 *		Michael Meeks <michael@ximian.com>
 *	Ettore Perazzoli <ettore@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-dialog-utils.h"

#include <errno.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include <gconf/gconf-client.h>
#include <glib/gi18n.h>

#include "e-util/e-util.h"
#include "e-util/e-error.h"


/**
 * e_notice:
 * @parent: the dialog's parent window, or %NULL
 * @type: the type of dialog (%GTK_MESSAGE_INFO, %GTK_MESSAGE_WARNING,
 * or %GTK_MESSAGE_ERROR)
 * @format: printf-style format string, followed by arguments
 *
 * Convenience function to show a dialog with a message and an "OK"
 * button.
 **/
void
e_notice (gpointer parent, GtkMessageType type, const gchar *format, ...)
{
	GtkWidget *dialog;
	va_list args;
	gchar *str;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	dialog = gtk_message_dialog_new (NULL,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 type,
					 GTK_BUTTONS_OK,
					 "%s",
					 str);
	va_end (args);
	g_free (str);

	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

gchar *
e_file_dialog_save (GtkWindow *parent,
                    const gchar *title,
                    const gchar *fname)
{
	GtkWidget *dialog;
	gchar *filename = NULL;
	gchar *uri;

	g_return_val_if_fail (GTK_IS_WINDOW (parent), NULL);

	dialog = e_file_get_save_filesel (
		parent, title, fname, GTK_FILE_CHOOSER_ACTION_SAVE);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_OK)
		goto exit;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));

	if (e_file_can_save (GTK_WINDOW (dialog), uri)) {
		e_file_update_save_path (
			gtk_file_chooser_get_current_folder_uri (
			GTK_FILE_CHOOSER (dialog)), TRUE);
		filename = uri;  /* FIXME This looks wrong. */
	}

exit:
	gtk_widget_destroy (dialog);

	return filename;
}

/**
 * e_file_get_save_filesel:
 * @parent: parent window
 * @title: dialog title
 * @name: filename; already in a proper form (suitable for file system)
 * @action: action for dialog
 *
 * Creates a save dialog, using the saved directory from gconf.   The dialog has
 * no signals connected and is not shown.
 **/
GtkWidget *
e_file_get_save_filesel (GtkWindow *parent,
                         const gchar *title,
                         const gchar *name,
                         GtkFileChooserAction action)
{
	GtkWidget *filesel;
	gchar *uri;

	g_return_val_if_fail (GTK_IS_WINDOW (parent), NULL);

	filesel = gtk_file_chooser_dialog_new (
		title, parent, action,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		(action == GTK_FILE_CHOOSER_ACTION_OPEN) ?
		GTK_STOCK_OPEN : GTK_STOCK_SAVE, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (filesel), GTK_RESPONSE_OK);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (filesel), FALSE);

	uri = e_file_get_save_path ();

	gtk_file_chooser_set_current_folder_uri (
		GTK_FILE_CHOOSER (filesel), uri);

	if (name && name[0])
		gtk_file_chooser_set_current_name (
			GTK_FILE_CHOOSER (filesel), name);

	g_free (uri);

	return filesel;
}

/**
 * e_file_can_save:
 *
 * Return TRUE if the URI can be saved to, FALSE otherwise.  It checks local
 * files to see if they're regular and can be accessed.  If the file exists and
 * is writable, it pops up a dialog asking the user if they want to overwrite
 * it.  Returns the users choice.
 **/
gboolean
e_file_can_save(GtkWindow *parent, const gchar *uri)
{
	struct stat st;
	gchar *path;
	gboolean res;

	if (!uri || uri[0] == 0)
		return FALSE;

	/* Assume remote files are writable; too costly to check */
	if (!e_file_check_local(uri))
		return TRUE;

	path = g_filename_from_uri (uri, NULL, NULL);
	if (!path)
		return FALSE;

	/* make sure we can actually save to it... */
	if (g_stat (path, &st) != -1 && !S_ISREG (st.st_mode)) {
		g_free(path);
		return FALSE;
	}

	res = TRUE;
	if (g_access (path, F_OK) == 0) {
		if (g_access (path, W_OK) != 0) { e_error_run(parent, "mail:no-save-path", path, g_strerror(errno), NULL);
			g_free(path);
			return FALSE;
		}

		res = e_error_run(parent, E_ERROR_ASK_FILE_EXISTS_OVERWRITE, path, NULL) == GTK_RESPONSE_OK;

	}

	g_free(path);
	return res;
}

gboolean
e_file_check_local (const gchar *name)
{
	gchar *uri;

	uri = g_filename_to_uri (name, NULL, NULL);
	if (uri) {
		g_free(uri);
		return TRUE;
	}

	return FALSE;
}
