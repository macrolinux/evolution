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
 *		Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#include <glib/gstdio.h>

#ifdef G_OS_WIN32
/* Work around namespace clobbage in <windows.h> */
#define DATADIR windows_DATADIR
#include <windows.h>
#undef DATADIR
#undef interface
#endif

#include <libebook/e-book-client.h>
#include <libebook/e-book-query.h>

#include "em-filter-editor.h"

#include <glib/gi18n.h>

#include <gio/gio.h>

#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-tools.h"
#include "e-mail-tag-editor.h"

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-flag.h>
#include <libedataserver/e-proxy.h>
#include "e-util/e-util.h"
#include "e-util/e-util-private.h"
#include "e-util/e-mktemp.h"
#include "e-util/e-account-utils.h"
#include "e-util/e-dialog-utils.h"
#include "e-util/e-alert-dialog.h"
#include "shell/e-shell.h"
#include "widgets/misc/e-attachment.h"

#include "em-utils.h"
#include "em-composer-utils.h"
#include "em-format-quote.h"
#include "em-format-html-print.h"
#include "e-mail-folder-utils.h"
#include "e-mail-session.h"

/* XXX This is a dirty hack on a dirty hack.  We really need
 *     to rework or get rid of the functions that use this. */
extern const gchar *shell_builtin_backend;

/* How many is too many? */
/* Used in em_util_ask_open_many() */
#define TOO_MANY 10

/* drag and drop resulting file naming possibilities */
enum {
	DND_USE_SENT_DATE = 1, /* YYYYMMDDhhmmssms_<title> and use email sent date */
	DND_USE_DND_DATE  = 2,  /*  YYYYMMDDhhmmssms_<title> and drag'drop date */
};

#define d(x)

gboolean
em_utils_ask_open_many (GtkWindow *parent,
                        gint how_many)
{
	gchar *string;
	gboolean proceed;

	if (how_many < TOO_MANY)
		return TRUE;

	string = g_strdup_printf (ngettext (
		/* Translators: This message is shown only for ten or more
		 * messages to be opened.  The %d is replaced with the actual
		 * count of messages. If you need a '%' in your text, then
		 * write it doubled, like '%%'. */
		"Are you sure you want to open %d message at once?",
		"Are you sure you want to open %d messages at once?",
		how_many), how_many);
	proceed = em_utils_prompt_user (
		parent, "prompt-on-open-many",
		"mail:ask-open-many", string, NULL);
	g_free (string);

	return proceed;
}

/**
 * em_utils_prompt_user:
 * @parent: parent window
 * @promptkey: settings key to check if we should prompt the user or not.
 * @tag: e_alert tag.
 *
 * Convenience function to query the user with a Yes/No dialog and a
 * "Do not show this dialog again" checkbox. If the user checks that
 * checkbox, then @promptkey is set to %FALSE, otherwise it is set to
 * %TRUE.
 *
 * Returns %TRUE if the user clicks Yes or %FALSE otherwise.
 **/
gboolean
em_utils_prompt_user (GtkWindow *parent,
                      const gchar *promptkey,
                      const gchar *tag,
                      ...)
{
	GtkWidget *dialog;
	GtkWidget *check = NULL;
	GtkWidget *container;
	va_list ap;
	gint button;
	GSettings *settings;
	EAlert *alert = NULL;

	settings = g_settings_new ("org.gnome.evolution.mail");

	if (promptkey && !g_settings_get_boolean (settings, promptkey)) {
		g_object_unref (settings);
		return TRUE;
	}

	va_start (ap, tag);
	alert = e_alert_new_valist (tag, ap);
	va_end (ap);

	dialog = e_alert_dialog_new (parent, alert);
	g_object_unref (alert);

	container = e_alert_dialog_get_content_area (E_ALERT_DIALOG (dialog));

	if (promptkey) {
		check = gtk_check_button_new_with_mnemonic (
			_("_Do not show this message again"));
		gtk_box_pack_start (
			GTK_BOX (container), check, FALSE, FALSE, 0);
		gtk_widget_show (check);
	}

	button = gtk_dialog_run (GTK_DIALOG (dialog));
	if (promptkey)
		g_settings_set_boolean (
			settings, promptkey,
			!gtk_toggle_button_get_active (
				GTK_TOGGLE_BUTTON (check)));

	gtk_widget_destroy (dialog);

	g_object_unref (settings);

	return button == GTK_RESPONSE_YES;
}

/**
 * em_utils_uids_copy:
 * @uids: array of uids
 *
 * Duplicates the array of uids held by @uids into a new
 * GPtrArray. Use em_utils_uids_free() to free the resultant uid
 * array.
 *
 * Returns a duplicate copy of @uids.
 **/
GPtrArray *
em_utils_uids_copy (GPtrArray *uids)
{
	GPtrArray *copy;
	gint i;

	copy = g_ptr_array_new ();
	g_ptr_array_set_size (copy, uids->len);

	for (i = 0; i < uids->len; i++)
		copy->pdata[i] = g_strdup (uids->pdata[i]);

	return copy;
}

/**
 * em_utils_uids_free:
 * @uids: array of uids
 *
 * Frees the array of uids pointed to by @uids back to the system.
 **/
void
em_utils_uids_free (GPtrArray *uids)
{
	gint i;

	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);

	g_ptr_array_free (uids, TRUE);
}

/* Editing Filters/Search Folders... */

static GtkWidget *filter_editor = NULL;

static void
em_filter_editor_response (GtkWidget *dialog,
                           gint button,
                           gpointer user_data)
{
	EMFilterContext *fc;

	if (button == GTK_RESPONSE_OK) {
		const gchar *config_dir;
		gchar *user;

		config_dir = mail_session_get_config_dir ();
		fc = g_object_get_data ((GObject *) dialog, "context");
		user = g_build_filename (config_dir, "filters.xml", NULL);
		e_rule_context_save ((ERuleContext *) fc, user);
		g_free (user);
	}

	gtk_widget_destroy (dialog);

	filter_editor = NULL;
}

static EMFilterSource em_filter_source_element_names[] = {
	{ "incoming", },
	{ "outgoing", },
	{ NULL }
};

/**
 * em_utils_edit_filters:
 * @session: an #EMailSession
 * @alert_sink: an #EAlertSink
 * @parent_window: a parent #GtkWindow
 *
 * Opens or raises the filters editor dialog so that the user may edit
 * his/her filters. If @parent is non-NULL, then the dialog will be
 * created as a child window of @parent's toplevel window.
 **/
void
em_utils_edit_filters (EMailSession *session,
                       EAlertSink *alert_sink,
                       GtkWindow *parent_window)
{
	const gchar *config_dir;
	gchar *user, *system;
	EMFilterContext *fc;

	g_return_if_fail (E_IS_MAIL_SESSION (session));
	g_return_if_fail (E_IS_ALERT_SINK (alert_sink));

	if (filter_editor) {
		gtk_window_present (GTK_WINDOW (filter_editor));
		return;
	}

	config_dir = mail_session_get_config_dir ();

	fc = em_filter_context_new (session);
	user = g_build_filename (config_dir, "filters.xml", NULL);
	system = g_build_filename (EVOLUTION_PRIVDATADIR, "filtertypes.xml", NULL);
	e_rule_context_load ((ERuleContext *) fc, system, user);
	g_free (user);
	g_free (system);

	if (((ERuleContext *) fc)->error) {
		e_alert_submit (
			alert_sink,
			"mail:filter-load-error",
			((ERuleContext *) fc)->error, NULL);
		return;
	}

	if (em_filter_source_element_names[0].name == NULL) {
		em_filter_source_element_names[0].name = _("Incoming");
		em_filter_source_element_names[1].name = _("Outgoing");
	}

	filter_editor = (GtkWidget *) em_filter_editor_new (
		fc, em_filter_source_element_names);

	if (GTK_IS_WINDOW (parent_window))
		gtk_window_set_transient_for (
			GTK_WINDOW (filter_editor), parent_window);

	gtk_window_set_title (
		GTK_WINDOW (filter_editor), _("Message Filters"));
	g_object_set_data_full (
		G_OBJECT (filter_editor), "context", fc,
		(GDestroyNotify) g_object_unref);
	g_signal_connect (
		filter_editor, "response",
		G_CALLBACK (em_filter_editor_response), NULL);
	gtk_widget_show (GTK_WIDGET (filter_editor));
}

/*
 * Picked this from e-d-s/libedataserver/e-data.
 * But it allows more characters to occur in filenames, especially
 * when saving attachment.
 */
void
em_filename_make_safe (gchar *string)
{
	gchar *p, *ts;
	gunichar c;
#ifdef G_OS_WIN32
	const gchar *unsafe_chars = "/\":*?<>|\\#";
#else
	const gchar *unsafe_chars = "/#";
#endif

	g_return_if_fail (string != NULL);
	p = string;

	while (p && *p) {
		c = g_utf8_get_char (p);
		ts = p;
		p = g_utf8_next_char (p);
		/* I wonder what this code is supposed to actually
		 * achieve, and whether it does that as currently
		 * written?
		 */
		if (!g_unichar_isprint (c) || ( c < 0xff && strchr (unsafe_chars, c&0xff ))) {
			while (ts < p)
				*ts++ = '_';
		}
	}
}

/* ********************************************************************** */
/* Flag-for-Followup... */

/**
 * em_utils_flag_for_followup:
 * @reader: an #EMailReader
 * @folder: folder containing messages to flag
 * @uids: uids of messages to flag
 *
 * Open the Flag-for-Followup editor for the messages specified by
 * @folder and @uids.
 **/
void
em_utils_flag_for_followup (EMailReader *reader,
                            CamelFolder *folder,
                            GPtrArray *uids)
{
	EShell *shell;
	EMailBackend *backend;
	EShellSettings *shell_settings;
	EShellBackend *shell_backend;
	EMFormatHTML *formatter;
	GtkWidget *editor;
	GtkWindow *window;
	CamelTag *tags;
	gint i;

	g_return_if_fail (E_IS_MAIL_READER (reader));
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);

	window = e_mail_reader_get_window (reader);
	backend = e_mail_reader_get_backend (reader);

	editor = e_mail_tag_editor_new ();
	gtk_window_set_transient_for (GTK_WINDOW (editor), window);

	shell_backend = E_SHELL_BACKEND (backend);
	shell = e_shell_backend_get_shell (shell_backend);
	shell_settings = e_shell_get_shell_settings (shell);

	/* These settings come from the calendar module. */
	g_object_bind_property (
		shell_settings, "cal-use-24-hour-format",
		editor, "use-24-hour-format",
		G_BINDING_SYNC_CREATE);
	g_object_bind_property (
		shell_settings, "cal-week-start-day",
		editor, "week-start-day",
		G_BINDING_SYNC_CREATE);

	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *info;

		info = camel_folder_get_message_info (folder, uids->pdata[i]);

		if (info == NULL)
			continue;

		e_mail_tag_editor_add_message (
			E_MAIL_TAG_EDITOR (editor),
			camel_message_info_from (info),
			camel_message_info_subject (info));

		camel_folder_free_message_info (folder, info);
	}

	/* special-case... */
	if (uids->len == 1) {
		CamelMessageInfo *info;
		const gchar *message_uid;

		message_uid = g_ptr_array_index (uids, 0);
		info = camel_folder_get_message_info (folder, message_uid);
		if (info) {
			tags = (CamelTag *) camel_message_info_user_tags (info);

			if (tags)
				e_mail_tag_editor_set_tag_list (
					E_MAIL_TAG_EDITOR (editor), tags);
			camel_folder_free_message_info (folder, info);
		}
	}

	if (gtk_dialog_run (GTK_DIALOG (editor)) != GTK_RESPONSE_OK)
		goto exit;

	tags = e_mail_tag_editor_get_tag_list (E_MAIL_TAG_EDITOR (editor));
	if (tags == NULL)
		goto exit;

	camel_folder_freeze (folder);
	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *info;
		CamelTag *iter;

		info = camel_folder_get_message_info (folder, uids->pdata[i]);

		if (info == NULL)
			continue;

		for (iter = tags; iter != NULL; iter = iter->next)
			camel_message_info_set_user_tag (
				info, iter->name, iter->value);

		camel_folder_free_message_info (folder, info);
	}

	camel_folder_thaw (folder);
	camel_tag_list_free (&tags);

	formatter = e_mail_reader_get_formatter (reader);
	em_format_queue_redraw (EM_FORMAT (formatter));

exit:
	/* XXX We shouldn't be freeing this. */
	em_utils_uids_free (uids);

	gtk_widget_destroy (GTK_WIDGET (editor));
}

/**
 * em_utils_flag_for_followup_clear:
 * @parent: parent window
 * @folder: folder containing messages to unflag
 * @uids: uids of messages to unflag
 *
 * Clears the Flag-for-Followup flag on the messages referenced by
 * @folder and @uids.
 **/
void
em_utils_flag_for_followup_clear (GtkWindow *parent,
                                  CamelFolder *folder,
                                  GPtrArray *uids)
{
	gint i;

	g_return_if_fail (GTK_IS_WINDOW (parent));
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);

	camel_folder_freeze (folder);
	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *mi = camel_folder_get_message_info (folder, uids->pdata[i]);

		if (mi) {
			camel_message_info_set_user_tag(mi, "follow-up", NULL);
			camel_message_info_set_user_tag(mi, "due-by", NULL);
			camel_message_info_set_user_tag(mi, "completed-on", NULL);
			camel_folder_free_message_info (folder, mi);
		}
	}

	camel_folder_thaw (folder);

	em_utils_uids_free (uids);
}

/**
 * em_utils_flag_for_followup_completed:
 * @parent: parent window
 * @folder: folder containing messages to 'complete'
 * @uids: uids of messages to 'complete'
 *
 * Sets the completed state (and date/time) for each message
 * referenced by @folder and @uids that is marked for
 * Flag-for-Followup.
 **/
void
em_utils_flag_for_followup_completed (GtkWindow *parent,
                                      CamelFolder *folder,
                                      GPtrArray *uids)
{
	gchar *now;
	gint i;

	g_return_if_fail (GTK_IS_WINDOW (parent));
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);

	now = camel_header_format_date (time (NULL), 0);

	camel_folder_freeze (folder);
	for (i = 0; i < uids->len; i++) {
		const gchar *tag;
		CamelMessageInfo *mi = camel_folder_get_message_info (folder, uids->pdata[i]);

		if (mi) {
			tag = camel_message_info_user_tag(mi, "follow-up");
			if (tag && tag[0])
				camel_message_info_set_user_tag(mi, "completed-on", now);
			camel_folder_free_message_info (folder, mi);
		}
	}

	camel_folder_thaw (folder);

	g_free (now);

	em_utils_uids_free (uids);
}

/* This kind of sucks, because for various reasons most callers need to run
 * synchronously in the gui thread, however this could take a long, blocking
 * time to run. */
static gint
em_utils_write_messages_to_stream (CamelFolder *folder,
                                   GPtrArray *uids,
                                   CamelStream *stream)
{
	CamelStream *filtered_stream;
	CamelMimeFilter *from_filter;
	gint i, res = 0;

	from_filter = camel_mime_filter_from_new ();
	filtered_stream = camel_stream_filter_new (stream);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), from_filter);
	g_object_unref (from_filter);

	for (i = 0; i < uids->len; i++) {
		CamelMimeMessage *message;
		gchar *from;

		/* FIXME camel_folder_get_message_sync() may block. */
		message = camel_folder_get_message_sync (
			folder, uids->pdata[i], NULL, NULL);
		if (message == NULL) {
			res = -1;
			break;
		}

		/* We need to flush after each stream write since we are
		 * writing to the same stream. */
		from = camel_mime_message_build_mbox_from (message);

		if (camel_stream_write_string (stream, from, NULL, NULL) == -1
		    || camel_stream_flush (stream, NULL, NULL) == -1
		    || camel_data_wrapper_write_to_stream_sync (
			(CamelDataWrapper *) message, (CamelStream *)
			filtered_stream, NULL, NULL) == -1
		    || camel_stream_flush (
			(CamelStream *) filtered_stream, NULL, NULL) == -1)
			res = -1;

		g_free (from);
		g_object_unref (message);

		if (res == -1)
			break;
	}

	g_object_unref (filtered_stream);

	return res;
}

static gboolean
em_utils_print_messages_to_file	(CamelFolder *folder, 
				 const gchar * uid, 
				 const gchar *filename)
{
	EMFormatHTMLPrint *efhp;
	CamelMimeMessage *message;

	message = camel_folder_get_message_sync (folder, uid, NULL, NULL);
	if (message == NULL)
		return FALSE;

	efhp = em_format_html_print_new (NULL, GTK_PRINT_OPERATION_ACTION_EXPORT);
	efhp->export_filename = g_strdup (filename);
	efhp->async = FALSE;

	em_format_html_print_message (efhp, message, folder, uid);

	g_object_unref (efhp);
	g_object_unref (message);

	return TRUE;
}

/* This kind of sucks, because for various reasons most callers need to run
 * synchronously in the gui thread, however this could take a long, blocking
 * time to run. */
static gint
em_utils_read_messages_from_stream (CamelFolder *folder,
                                    CamelStream *stream)
{
	CamelMimeParser *mp = camel_mime_parser_new ();
	gboolean success = TRUE;

	camel_mime_parser_scan_from (mp, TRUE);
	camel_mime_parser_init_with_stream (mp, stream, NULL);

	while (camel_mime_parser_step (mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM) {
		CamelMimeMessage *msg;
		gboolean success;

		/* NB: de-from filter, once written */
		msg = camel_mime_message_new ();
		if (!camel_mime_part_construct_from_parser_sync (
			(CamelMimePart *) msg, mp, NULL, NULL)) {
			g_object_unref (msg);
			break;
		}

		/* FIXME camel_folder_append_message_sync() may block. */
		success = camel_folder_append_message_sync (
			folder, msg, NULL, NULL, NULL, NULL);
		g_object_unref (msg);

		if (!success)
			break;

		camel_mime_parser_step (mp, NULL, NULL);
	}

	g_object_unref (mp);

	return success ? 0 : -1;
}

/**
 * em_utils_selection_set_mailbox:
 * @data: selection data
 * @folder: folder containign messages to copy into the selection
 * @uids: uids of the messages to copy into the selection
 *
 * Creates a mailbox-format selection.
 * Warning: Could be BIG!
 * Warning: This could block the ui for an extended period.
 **/
void
em_utils_selection_set_mailbox (GtkSelectionData *data,
                                CamelFolder *folder,
                                GPtrArray *uids)
{
	GByteArray *byte_array;
	CamelStream *stream;
	GdkAtom target;

	target = gtk_selection_data_get_target (data);

	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);

	if (em_utils_write_messages_to_stream (folder, uids, stream) == 0)
		gtk_selection_data_set (
			data, target, 8,
			byte_array->data, byte_array->len);

	g_object_unref (stream);
}

/**
 * em_utils_selection_get_mailbox:
 * @selection_data: selection data
 * @folder:
 *
 * Receive a mailbox selection/dnd
 * Warning: Could be BIG!
 * Warning: This could block the ui for an extended period.
 * FIXME: Exceptions?
 **/
void
em_utils_selection_get_mailbox (GtkSelectionData *selection_data,
                                CamelFolder *folder)
{
	CamelStream *stream;
	const guchar *data;
	gint length;

	data = gtk_selection_data_get_data (selection_data);
	length = gtk_selection_data_get_length (selection_data);

	if (data == NULL || length == -1)
		return;

	/* TODO: a stream mem with read-only access to existing data? */
	/* NB: Although copying would let us run this async ... which it should */
	stream = (CamelStream *)
		camel_stream_mem_new_with_buffer ((gchar *) data, length);
	em_utils_read_messages_from_stream (folder, stream);
	g_object_unref (stream);
}

/**
 * em_utils_selection_get_message:
 * @selection_data:
 * @folder:
 *
 * get a message/rfc822 data.
 **/
void
em_utils_selection_get_message (GtkSelectionData *selection_data,
                                CamelFolder *folder)
{
	CamelStream *stream;
	CamelMimeMessage *msg;
	const guchar *data;
	gint length;

	data = gtk_selection_data_get_data (selection_data);
	length = gtk_selection_data_get_length (selection_data);

	if (data == NULL || length == -1)
		return;

	stream = (CamelStream *)
		camel_stream_mem_new_with_buffer ((gchar *) data, length);
	msg = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream_sync (
		(CamelDataWrapper *) msg, stream, NULL, NULL))
		/* FIXME camel_folder_append_message_sync() may block. */
		camel_folder_append_message_sync (
			folder, msg, NULL, NULL, NULL, NULL);
	g_object_unref (msg);
	g_object_unref (stream);
}

/**
 * em_utils_selection_set_uidlist:
 * @selection_data: selection data
 * @folder:
 * @uids:
 *
 * Sets a "x-uid-list" format selection data.
 **/
void
em_utils_selection_set_uidlist (GtkSelectionData *selection_data,
                                CamelFolder *folder,
                                GPtrArray *uids)
{
	GByteArray *array = g_byte_array_new ();
	GdkAtom target;
	gchar *folder_uri;
	gint i;

	/* format: "uri\0uid1\0uid2\0uid3\0...\0uidn\0" */

	folder_uri = e_mail_folder_uri_from_folder (folder);

	g_byte_array_append (
		array, (guchar *) folder_uri, strlen (folder_uri) + 1);

	for (i = 0; i < uids->len; i++)
		g_byte_array_append (array, uids->pdata[i], strlen (uids->pdata[i]) + 1);

	target = gtk_selection_data_get_target (selection_data);
	gtk_selection_data_set (
		selection_data, target, 8, array->data, array->len);
	g_byte_array_free (array, TRUE);

	g_free (folder_uri);
}

/**
 * em_utils_selection_get_uidlist:
 * @data: selection data
 * @session: an #EMailSession
 * @move: do we delete the messages.
 *
 * Convert a uid list into a copy/move operation.
 *
 * Warning: Could take some time to run.
 **/
void
em_utils_selection_get_uidlist (GtkSelectionData *selection_data,
                                EMailSession *session,
                                CamelFolder *dest,
                                gint move,
                                GCancellable *cancellable,
                                GError **error)
{
	/* format: "uri\0uid1\0uid2\0uid3\0...\0uidn" */
	gchar *inptr, *inend;
	GPtrArray *uids;
	CamelFolder *folder;
	const guchar *data;
	gint length;

	g_return_if_fail (selection_data != NULL);
	g_return_if_fail (E_IS_MAIL_SESSION (session));

	data = gtk_selection_data_get_data (selection_data);
	length = gtk_selection_data_get_length (selection_data);

	if (data == NULL || length == -1)
		return;

	uids = g_ptr_array_new ();

	inptr = (gchar *) data;
	inend = (gchar *) (data + length);
	while (inptr < inend) {
		gchar *start = inptr;

		while (inptr < inend && *inptr)
			inptr++;

		if (start > (gchar *) data)
			g_ptr_array_add (uids, g_strndup (start, inptr - start));

		inptr++;
	}

	if (uids->len == 0) {
		g_ptr_array_free (uids, TRUE);
		return;
	}

	/* FIXME e_mail_session_uri_to_folder_sync() may block. */
	folder = e_mail_session_uri_to_folder_sync (
		session, (gchar *) data, 0, cancellable, error);
	if (folder) {
		/* FIXME camel_folder_transfer_messages_to_sync() may block. */
		camel_folder_transfer_messages_to_sync (
			folder, uids, dest, move, NULL, cancellable, error);
		g_object_unref (folder);
	}

	em_utils_uids_free (uids);
}

static gchar *
em_utils_build_export_filename	(CamelFolder *folder, 
				 const gchar * uid, 
				 const gchar * exporttype, 
				 gint exportname, 
				 const gchar * tmpdir)
{
	CamelMessageInfo *info;
	gchar *file, *tmpfile;
	struct tm  *ts;
	gchar datetmp[15];

	/* Try to get the drop filename from the message or folder */
	info = camel_folder_get_message_info(folder, uid);
	if (info) {
		if (camel_message_info_subject (info)) {
			time_t reftime;
			reftime = camel_message_info_date_sent (info);
			if (exportname==DND_USE_DND_DATE) {
				reftime = time(NULL);
			}

			ts = localtime(&reftime);
			strftime(datetmp, 15, "%Y%m%d%H%M%S", ts);

			if (g_ascii_strcasecmp (exporttype, "pdf")==0)
				file = g_strdup_printf ("%s_%s.pdf", datetmp, camel_message_info_subject (info));
			else
				file = g_strdup_printf ("%s_%s", datetmp, camel_message_info_subject(info));

		}
		camel_folder_free_message_info(folder, info);
	} else {
		time_t reftime;
		reftime = time(NULL);
		ts = localtime(&reftime);
		strftime(datetmp, 15, "%Y%m%d%H%M%S", ts);
		if (g_ascii_strcasecmp (exporttype, "pdf")==0)
			file = g_strdup_printf ("%s_Untitled Message.pdf", datetmp);
		else
			file = g_strdup_printf ("%s_Untitled Message", datetmp);

	}

	e_filename_make_safe(file);

	tmpfile = g_build_filename(tmpdir, file, NULL);

	g_free(file);

	return tmpfile;
}

/**
 * em_utils_selection_set_urilist:
 * @data:
 * @folder:
 * @uids:
 *
 * Set the selection data @data to a uri which points to a file, which is
 * a berkely mailbox format mailbox.  The file is automatically cleaned
 * up when the application quits.
 **/
void
em_utils_selection_set_urilist (GtkSelectionData *data,
                                CamelFolder *folder,
                                GPtrArray *uids)
{
 	gchar *tmpdir;
	gchar *uri;
 	gint fd;
	GConfClient *client;
	gchar *exporttype;
	gint exportname;
 
 	tmpdir = e_mkdtemp("drag-n-drop-XXXXXX");
 	if (tmpdir == NULL)
 		return;
 
	client = gconf_client_get_default ();
	exporttype = gconf_client_get_string (
		client, "/apps/evolution/mail/save_file_format", NULL);
	if (exporttype == NULL)
		exporttype = g_strdup ("mbox");
	exportname = gconf_client_get_int (
		client, "/apps/evolution/mail/save_name_format", NULL);

	if (g_ascii_strcasecmp (exporttype, "mbox")==0) {
		gchar * file = NULL;
		CamelStream *fstream;

		if(uids->len>1) {
			gchar * tmp = g_strdup_printf(_("Messages from %s"), camel_folder_get_display_name (folder));
			e_filename_make_safe(tmp);
			file = g_build_filename(tmpdir, tmp, NULL);
			g_free(tmp);
		} else {
			file = em_utils_build_export_filename(folder, uids->pdata[0], exporttype, exportname, tmpdir);
 		}
		
		g_free(tmpdir);
		fd = g_open(file, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
		if (fd == -1) {
			g_free(file);
			g_free(exporttype);
			return;
 		}
 
		uri = g_filename_to_uri(file, NULL, NULL);
		fstream = camel_stream_fs_new_with_fd(fd);
		if (fstream) {
			if (em_utils_write_messages_to_stream(folder, uids, fstream) == 0) {
				GdkAtom type;
				/* terminate with \r\n to be compliant with the spec */
				gchar *uri_crlf = g_strconcat(uri, "\r\n", NULL);

				type = gtk_selection_data_get_target (data);
				gtk_selection_data_set(data, type, 8, (guchar *) uri_crlf, strlen(uri_crlf));
				g_free(uri_crlf);
			}
			g_object_unref (fstream);
		} else
			close(fd);
 
		g_free(exporttype);
		g_free(file);
		g_free(uri);
	} else if(g_ascii_strcasecmp (exporttype, "pdf")==0) {
		gchar ** filenames, **uris;
		gint i, uris_count=0;

		filenames = g_new(gchar *, uids->len);
		uris = g_new(gchar *, uids->len + 1);
		for(i=0; i<uids->len; i++) {
			filenames[i] = em_utils_build_export_filename(folder, uids->pdata[i], exporttype, exportname, tmpdir);
			/* validity test */
			fd = g_open(filenames[i], O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
			if (fd == -1) {
				gint j;
				for(j=0; j<=i; j++) {
					g_free(filenames[j]);
				}
				g_free(filenames);
				g_free(uris);
				g_free(tmpdir);
				g_free(exporttype);
				return;
			}
			close(fd);

			/* export */
			if (em_utils_print_messages_to_file (folder, uids->pdata[i], filenames[i])) {
				/* terminate with \r\n to be compliant with the spec */
				uri = g_filename_to_uri(filenames[i], NULL, NULL);
				uris[uris_count++] = g_strconcat(uri, "\r\n", NULL);
				g_free(uri);
			}
 		}
 
		uris[uris_count] = NULL;
		gtk_selection_data_set_uris(data, uris);
 
		g_free(tmpdir);
		for(i=0; i<uids->len; i++) {
			g_free(filenames[i]);
 		}
		g_free(filenames);
		for(i=0; i<uris_count; i++) {
			g_free(uris[i]);
		}
		g_free(uris);
		g_free(exporttype);
 
	} else {
		g_free(tmpdir);
		g_free(exporttype);
	}
}

/**
 * em_utils_selection_set_urilist:
 * @data:
 * @folder:
 * @uids:
 *
 * Get the selection data @data from a uri list which points to a
 * file, which is a berkely mailbox format mailbox.  The file is
 * automatically cleaned up when the application quits.
 **/
void
em_utils_selection_get_urilist (GtkSelectionData *selection_data,
                                CamelFolder *folder)
{
	CamelStream *stream;
	CamelURL *url;
	gint fd, i, res = 0;
	gchar **uris;

	d(printf(" * drop uri list\n"));

	uris = gtk_selection_data_get_uris (selection_data);

	for (i = 0; res == 0 && uris[i]; i++) {
		g_strstrip (uris[i]);
		if (uris[i][0] == '#')
			continue;

		url = camel_url_new (uris[i], NULL);
		if (url == NULL)
			continue;

		if (strcmp(url->protocol, "file") == 0
		    && (fd = g_open (url->path, O_RDONLY | O_BINARY, 0)) != -1) {
			stream = camel_stream_fs_new_with_fd (fd);
			if (stream) {
				res = em_utils_read_messages_from_stream (folder, stream);
				g_object_unref (stream);
			} else
				close (fd);
		}
		camel_url_free (url);
	}

	g_strfreev (uris);
}

/**
 * em_utils_folder_is_templates:
 * @folder: a #CamelFolder
 *
 * Decides if @folder is a Templates folder.
 *
 * Returns %TRUE if this is a Templates folder or %FALSE otherwise.
 **/

gboolean
em_utils_folder_is_templates (CamelFolder *folder)
{
	CamelFolder *local_templates_folder;
	CamelSession *session;
	CamelStore *store;
	EAccountList *account_list;
	EIterator *iterator;
	gchar *folder_uri;
	gboolean is_templates = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	store = camel_folder_get_parent_store (folder);
	session = camel_service_get_session (CAMEL_SERVICE (store));

	local_templates_folder =
		e_mail_session_get_local_folder (
		E_MAIL_SESSION (session), E_MAIL_LOCAL_FOLDER_TEMPLATES);

	if (folder == local_templates_folder)
		return TRUE;

	folder_uri = e_mail_folder_uri_from_folder (folder);

	account_list = e_get_account_list ();
	iterator = e_list_get_iterator (E_LIST (account_list));

	while (!is_templates && e_iterator_is_valid (iterator)) {
		EAccount *account;

		/* XXX EIterator misuses const. */
		account = (EAccount *) e_iterator_get (iterator);

		if (account->templates_folder_uri != NULL)
			is_templates = e_mail_folder_uri_equal (
				session, folder_uri,
				account->templates_folder_uri);

		e_iterator_next (iterator);
	}

	g_object_unref (iterator);
	g_free (folder_uri);

	return is_templates;
}

/**
 * em_utils_folder_is_drafts:
 * @folder: a #CamelFolder
 *
 * Decides if @folder is a Drafts folder.
 *
 * Returns %TRUE if this is a Drafts folder or %FALSE otherwise.
 **/
gboolean
em_utils_folder_is_drafts (CamelFolder *folder)
{
	CamelFolder *local_drafts_folder;
	CamelSession *session;
	CamelStore *store;
	EAccountList *account_list;
	EIterator *iterator;
	gchar *folder_uri;
	gboolean is_drafts = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	store = camel_folder_get_parent_store (folder);
	session = camel_service_get_session (CAMEL_SERVICE (store));

	local_drafts_folder =
		e_mail_session_get_local_folder (
		E_MAIL_SESSION (session), E_MAIL_LOCAL_FOLDER_DRAFTS);

	if (folder == local_drafts_folder)
		return TRUE;

	folder_uri = e_mail_folder_uri_from_folder (folder);

	account_list = e_get_account_list ();
	iterator = e_list_get_iterator (E_LIST (account_list));

	while (!is_drafts && e_iterator_is_valid (iterator)) {
		EAccount *account;

		/* XXX EIterator misuses const. */
		account = (EAccount *) e_iterator_get (iterator);

		if (account->drafts_folder_uri != NULL)
			is_drafts = e_mail_folder_uri_equal (
				session, folder_uri,
				account->drafts_folder_uri);

		e_iterator_next (iterator);
	}

	g_object_unref (iterator);
	g_free (folder_uri);

	return is_drafts;
}

/**
 * em_utils_folder_is_sent:
 * @folder: a #CamelFolder
 *
 * Decides if @folder is a Sent folder.
 *
 * Returns %TRUE if this is a Sent folder or %FALSE otherwise.
 **/
gboolean
em_utils_folder_is_sent (CamelFolder *folder)
{
	CamelFolder *local_sent_folder;
	CamelSession *session;
	CamelStore *store;
	EAccountList *account_list;
	EIterator *iterator;
	gchar *folder_uri;
	gboolean is_sent = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	store = camel_folder_get_parent_store (folder);
	session = camel_service_get_session (CAMEL_SERVICE (store));

	local_sent_folder =
		e_mail_session_get_local_folder (
		E_MAIL_SESSION (session), E_MAIL_LOCAL_FOLDER_SENT);

	if (folder == local_sent_folder)
		return TRUE;

	folder_uri = e_mail_folder_uri_from_folder (folder);

	account_list = e_get_account_list ();
	iterator = e_list_get_iterator (E_LIST (account_list));

	while (!is_sent && e_iterator_is_valid (iterator)) {
		EAccount *account;

		/* XXX EIterator misuses const. */
		account = (EAccount *) e_iterator_get (iterator);

		if (account->sent_folder_uri != NULL)
			is_sent = e_mail_folder_uri_equal (
				session, folder_uri,
				account->sent_folder_uri);

		e_iterator_next (iterator);
	}

	g_object_unref (iterator);
	g_free (folder_uri);

	return is_sent;
}

/**
 * em_utils_folder_is_outbox:
 * @folder: a #CamelFolder
 *
 * Decides if @folder is an Outbox folder.
 *
 * Returns %TRUE if this is an Outbox folder or %FALSE otherwise.
 **/
gboolean
em_utils_folder_is_outbox (CamelFolder *folder)
{
	CamelStore *store;
	CamelSession *session;
	CamelFolder *local_outbox_folder;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	store = camel_folder_get_parent_store (folder);
	session = camel_service_get_session (CAMEL_SERVICE (store));

	local_outbox_folder =
		e_mail_session_get_local_folder (
		E_MAIL_SESSION (session), E_MAIL_LOCAL_FOLDER_OUTBOX);

	return (folder == local_outbox_folder);
}

/* ********************************************************************** */
static EProxy *emu_proxy = NULL;
static GStaticMutex emu_proxy_lock = G_STATIC_MUTEX_INIT;

static gpointer
emu_proxy_setup (gpointer data)
{
	if (!emu_proxy) {
		emu_proxy = e_proxy_new ();
		e_proxy_setup_proxy (emu_proxy);
		/* not necessary to listen for changes here */
	}

	return NULL;
}

EProxy *
em_utils_get_proxy (void)
{
	g_static_mutex_lock (&emu_proxy_lock);

	if (!emu_proxy) {
		mail_call_main (MAIL_CALL_p_p, (MailMainFunc) emu_proxy_setup, NULL);
	}

	g_static_mutex_unlock (&emu_proxy_lock);

	return emu_proxy;
}

/**
 * em_utils_message_to_html:
 * @message:
 * @credits:
 * @flags: EMFormatQuote flags
 * @source:
 * @append: Text to append, can be NULL.
 * @validity_found: if not NULL, then here will be set what validities
 *         had been found during message conversion. Value is a bit OR
 *         of EM_FORMAT_VALIDITY_FOUND_* constants.
 *
 * Convert a message to html, quoting if the @credits attribution
 * string is given.
 *
 * Return value: The html version as a NULL terminated string.
 **/
gchar *
em_utils_message_to_html (CamelMimeMessage *message,
                          const gchar *credits,
                          guint32 flags,
                          EMFormat *source,
                          const gchar *append,
                          guint32 *validity_found)
{
	EMFormatQuote *emfq;
	CamelStream *mem;
	GByteArray *buf;

	buf = g_byte_array_new ();
	mem = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (mem), buf);

	emfq = em_format_quote_new (credits, mem, flags);
	((EMFormat *) emfq)->composer = TRUE;

	if (!source) {
		GSettings *settings;
		gchar *charset;

		/* FIXME We should be getting this from the
		 *       current view, not the global setting. */
		settings = g_settings_new ("org.gnome.evolution.mail");
		charset = g_settings_get_string (settings, "charset");
		em_format_set_default_charset ((EMFormat *) emfq, charset);
		g_object_unref (settings);
		g_free (charset);
	}

	/* FIXME Not passing a GCancellable here. */
	em_format_format_clone (
		EM_FORMAT (emfq), NULL, NULL, message, source, NULL);
	if (validity_found)
		*validity_found = ((EMFormat *)emfq)->validity_found;
	g_object_unref (emfq);

	if (append && *append)
		camel_stream_write_string (mem, append, NULL, NULL);

	camel_stream_write(mem, "", 1, NULL, NULL);
	g_object_unref (mem);

	return (gchar *) g_byte_array_free (buf, FALSE);
}

/* ********************************************************************** */

/**
 * em_utils_empty_trash:
 * @parent: parent window
 * @session: an #EMailSession
 *
 * Empties all Trash folders.
 **/
void
em_utils_empty_trash (GtkWidget *parent,
                      EMailSession *session)
{
	GList *list, *link;

	g_return_if_fail (E_IS_MAIL_SESSION (session));

	if (!em_utils_prompt_user ((GtkWindow *) parent,
		"prompt-on-empty-trash",
		"mail:ask-empty-trash", NULL))
		return;

	list = camel_session_list_services (CAMEL_SESSION (session));

	for (link = list; link != NULL; link = g_list_next (link)) {
		EAccount *account;
		CamelProvider *provider;
		CamelService *service;
		const gchar *uid;

		service = CAMEL_SERVICE (link->data);
		provider = camel_service_get_provider (service);
		uid = camel_service_get_uid (service);

		if (!CAMEL_IS_STORE (service))
			continue;

		if ((provider->flags & CAMEL_PROVIDER_IS_STORAGE) == 0)
			continue;

		account = e_get_account_by_uid (uid);

		/* The local store has no corresponding
		 * EAccount, so skip the enabled check. */
		if (account != NULL) {
			if (!account->enabled)
				continue;
		}

		mail_empty_trash (CAMEL_STORE (service));
	}

	g_list_free (list);
}

/* ********************************************************************** */

/* runs sync, in main thread */
static gpointer
emu_addr_setup (gpointer user_data)
{
	GError *err = NULL;
	ESourceList **psource_list = user_data;

	if (!e_book_client_get_sources (psource_list, &err))
		g_error_free (err);

	return NULL;
}

static void
emu_addr_cancel_stop (gpointer data)
{
	gboolean *stop = data;

	g_return_if_fail (stop != NULL);

	*stop = TRUE;
}

static void
emu_addr_cancel_cancellable (gpointer data)
{
	GCancellable *cancellable = data;

	g_return_if_fail (cancellable != NULL);

	g_cancellable_cancel (cancellable);
}

struct TryOpenEBookStruct {
	GError **error;
	EFlag *flag;
	gboolean result;
};

static void
try_open_book_client_cb (GObject *source_object,
                         GAsyncResult *result,
                         gpointer closure)
{
	EBookClient *book_client = E_BOOK_CLIENT (source_object);
	struct TryOpenEBookStruct *data = (struct TryOpenEBookStruct *) closure;
	GError *error = NULL;

	if (!data)
		return;

	e_client_open_finish (E_CLIENT (book_client), result, &error);

	data->result = error == NULL;

	if (!data->result) {
		g_clear_error (data->error);
		g_propagate_error (data->error, error);
	}

	e_flag_set (data->flag);
}

/*
 * try_open_book_client:
 * Tries to open address book asynchronously, but acts as synchronous.
 * The advantage is it checks periodically whether the camel_operation
 * has been canceled or not, and if so, then stops immediately, with
 * result FALSE. Otherwise returns same as e_client_open()
 */
static gboolean
try_open_book_client (EBookClient *book_client,
                      gboolean only_if_exists,
                      GCancellable *cancellable,
                      GError **error)
{
	struct TryOpenEBookStruct data;
	gboolean canceled = FALSE;
	EFlag *flag = e_flag_new ();

	data.error = error;
	data.flag = flag;
	data.result = FALSE;

	e_client_open (
		E_CLIENT (book_client), only_if_exists,
		cancellable, try_open_book_client_cb, &data);

	while (canceled = g_cancellable_is_cancelled (cancellable),
			!canceled && !e_flag_is_set (flag)) {
		GTimeVal wait;

		g_get_current_time (&wait);
		g_time_val_add (&wait, 250000); /* waits 250ms */

		e_flag_timed_wait (flag, &wait);
	}

	if (canceled) {
		g_cancellable_cancel (cancellable);

		g_clear_error (error);
		g_propagate_error (
			error, e_client_error_create (
			E_CLIENT_ERROR_CANCELLED, NULL));
	}

	e_flag_wait (flag);
	e_flag_free (flag);

	return data.result && (!error || !*error);
}

#define NOT_FOUND_BOOK (GINT_TO_POINTER (1))

G_LOCK_DEFINE_STATIC (contact_cache);

/* key is lowercased contact email; value is EBook pointer
 * (just for comparison) where it comes from */
static GHashTable *contact_cache = NULL;

/* key is source ID; value is pointer to EBook */
static GHashTable *emu_books_hash = NULL;

/* key is source ID; value is same pointer as key; this is hash of
 * broken books, which failed to open for some reason */
static GHashTable *emu_broken_books_hash = NULL;

static ESourceList *emu_books_source_list = NULL;

static gboolean
search_address_in_addressbooks (const gchar *address,
                                gboolean local_only,
                                gboolean (*check_contact) (EContact *contact,
                                                           gpointer user_data),
                                gpointer user_data)
{
	gboolean found = FALSE, stop = FALSE, found_any = FALSE;
	gchar *lowercase_addr;
	gpointer ptr;
	EBookQuery *book_query;
	gchar *query;
	GSList *s, *g, *addr_sources = NULL;
	GHook *hook_cancellable;
	GCancellable *cancellable;

	if (!address || !*address)
		return FALSE;

	G_LOCK (contact_cache);

	if (!emu_books_source_list) {
		mail_call_main (
			MAIL_CALL_p_p, (MailMainFunc)
			emu_addr_setup, &emu_books_source_list);
		emu_books_hash = g_hash_table_new_full (
			g_str_hash, g_str_equal, g_free, g_object_unref);
		emu_broken_books_hash = g_hash_table_new_full (
			g_str_hash, g_str_equal, g_free, NULL);
		contact_cache = g_hash_table_new_full (
			g_str_hash, g_str_equal, g_free, NULL);
	}

	if (!emu_books_source_list) {
		G_UNLOCK (contact_cache);
		return FALSE;
	}

	lowercase_addr = g_utf8_strdown (address, -1);
	ptr = g_hash_table_lookup (contact_cache, lowercase_addr);
	if (ptr != NULL && (check_contact == NULL || ptr == NOT_FOUND_BOOK)) {
		g_free (lowercase_addr);
		G_UNLOCK (contact_cache);
		return ptr != NOT_FOUND_BOOK;
	}

	book_query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_IS, address);
	query = e_book_query_to_string (book_query);
	e_book_query_unref (book_query);

	for (g = e_source_list_peek_groups (emu_books_source_list);
			g; g = g_slist_next (g)) {
		ESourceGroup *group = g->data;

		if (!group)
			continue;

		if (local_only && !(e_source_group_peek_base_uri (group) &&
			g_str_has_prefix (
			e_source_group_peek_base_uri (group), "local:")))
			continue;

		for (s = e_source_group_peek_sources (group); s; s = g_slist_next (s)) {
			ESource *source = s->data;
			const gchar *completion = e_source_get_property (source, "completion");

			if (completion && g_ascii_strcasecmp (completion, "true") == 0) {
				addr_sources = g_slist_prepend (addr_sources, g_object_ref (source));
			}
		}
	}

	cancellable = g_cancellable_new ();
	hook_cancellable = mail_cancel_hook_add (emu_addr_cancel_cancellable, cancellable);

	for (s = addr_sources; !stop && !found && s; s = g_slist_next (s)) {
		ESource *source = s->data;
		GSList *contacts;
		EBookClient *book_client = NULL;
		GHook *hook_stop;
		gboolean cached_book = FALSE;
		const gchar *display_name;
		const gchar *uid;
		GError *err = NULL;

		uid = e_source_peek_uid (source);
		display_name = e_source_peek_name (source);

		/* failed to load this book last time, skip it now */
		if (g_hash_table_lookup (emu_broken_books_hash, uid) != NULL) {
			d(printf ("%s: skipping broken book '%s'\n",
				G_STRFUNC, display_name));
			continue;
		}

		d(printf(" checking '%s'\n", e_source_get_uri(source)));

		hook_stop = mail_cancel_hook_add (emu_addr_cancel_stop, &stop);

		book_client = g_hash_table_lookup (emu_books_hash, uid);
		if (!book_client) {
			book_client = e_book_client_new (source, &err);

			if (book_client == NULL) {
				if (err && (g_error_matches (err, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
				    g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))) {
					stop = TRUE;
				} else if (err) {
					gchar *source_uid;

					source_uid = g_strdup (uid);

					g_hash_table_insert (
						emu_broken_books_hash,
						source_uid, source_uid);

					g_warning (
						"%s: Unable to create addressbook '%s': %s",
						G_STRFUNC,
						display_name,
						err->message);
				}
				g_clear_error (&err);
			} else if (!stop && !try_open_book_client (book_client, TRUE, cancellable, &err)) {
				g_object_unref (book_client);
				book_client = NULL;

				if (err && (g_error_matches (err, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
				    g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))) {
					stop = TRUE;
				} else if (err) {
					gchar *source_uid;

					source_uid = g_strdup (uid);

					g_hash_table_insert (
						emu_broken_books_hash,
						source_uid, source_uid);

					g_warning (
						"%s: Unable to open addressbook '%s': %s",
						G_STRFUNC,
						display_name,
						err->message);
				}
				g_clear_error (&err);
			}
		} else {
			cached_book = TRUE;
		}

		if (book_client && !stop && e_book_client_get_contacts_sync (book_client, query, &contacts, cancellable, &err)) {
			if (contacts != NULL) {
				if (!found_any) {
					g_hash_table_insert (contact_cache, g_strdup (lowercase_addr), book_client);
				}
				found_any = TRUE;

				if (check_contact) {
					GSList *l;

					for (l = contacts; l && !found; l = l->next) {
						EContact *contact = l->data;

						found = check_contact (contact, user_data);
					}
				} else {
					found = TRUE;
				}

				g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
				g_slist_free (contacts);
			}
		} else if (book_client) {
			stop = stop || (err &&
			    (g_error_matches (err, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
			     g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)));
			if (err && !stop) {
				gchar *source_uid = g_strdup (uid);

				g_hash_table_insert (emu_broken_books_hash, source_uid, source_uid);

				g_warning (
					"%s: Can't get contacts from '%s': %s",
					G_STRFUNC,
					display_name,
					err->message);
			}
			g_clear_error (&err);
		}

		mail_cancel_hook_remove (hook_stop);

		if (stop && !cached_book && book_client) {
			g_object_unref (book_client);
		} else if (!stop && book_client && !cached_book) {
			g_hash_table_insert (
				emu_books_hash, g_strdup (uid), book_client);
		}
	}

	mail_cancel_hook_remove (hook_cancellable);
	g_object_unref (cancellable);

	g_slist_free_full (addr_sources, (GDestroyNotify) g_object_unref);

	g_free (query);

	if (!found_any) {
		g_hash_table_insert (contact_cache, lowercase_addr, NOT_FOUND_BOOK);
		lowercase_addr = NULL;
	}

	G_UNLOCK (contact_cache);

	g_free (lowercase_addr);

	return found_any;
}

gboolean
em_utils_in_addressbook (CamelInternetAddress *iaddr,
                         gboolean local_only)
{
	const gchar *addr;

	/* TODO: check all addresses? */
	if (iaddr == NULL || !camel_internet_address_get (iaddr, 0, NULL, &addr))
		return FALSE;

	return search_address_in_addressbooks (addr, local_only, NULL, NULL);
}

static gboolean
extract_photo_data (EContact *contact,
                    gpointer user_data)
{
	EContactPhoto **photo = user_data;

	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (user_data != NULL, FALSE);

	*photo = e_contact_get (contact, E_CONTACT_PHOTO);
	if (!*photo)
		*photo = e_contact_get (contact, E_CONTACT_LOGO);

	return *photo != NULL;
}

typedef struct _PhotoInfo {
	gchar *address;
	EContactPhoto *photo;
} PhotoInfo;

static void
emu_free_photo_info (PhotoInfo *pi)
{
	if (!pi)
		return;

	if (pi->address)
		g_free (pi->address);
	if (pi->photo)
		e_contact_photo_free (pi->photo);
	g_free (pi);
}

G_LOCK_DEFINE_STATIC (photos_cache);
static GSList *photos_cache = NULL; /* list of PhotoInfo-s */

CamelMimePart *
em_utils_contact_photo (CamelInternetAddress *cia,
                        gboolean local_only)
{
	const gchar *addr = NULL;
	CamelMimePart *part = NULL;
	EContactPhoto *photo = NULL;
	GSList *p, *first_not_null = NULL;
	gint count_not_null = 0;

	if (cia == NULL || !camel_internet_address_get (cia, 0, NULL, &addr) || !addr) {
		return NULL;
	}

	G_LOCK (photos_cache);

	/* search a cache first */
	for (p = photos_cache; p; p = p->next) {
		PhotoInfo *pi = p->data;

		if (!pi)
			continue;

		if (pi->photo) {
			if (!first_not_null)
				first_not_null = p;
			count_not_null++;
		}

		if (g_ascii_strcasecmp (addr, pi->address) == 0) {
			photo = pi->photo;
			break;
		}
	}

	/* !p means the address had not been found in the cache */
	if (!p && search_address_in_addressbooks (
			addr, local_only, extract_photo_data, &photo)) {
		PhotoInfo *pi;

		if (photo && photo->type != E_CONTACT_PHOTO_TYPE_INLINED) {
			e_contact_photo_free (photo);
			photo = NULL;
		}

		/* keep only up to 10 photos in memory */
		if (photo && count_not_null >= 10 && first_not_null) {
			pi = first_not_null->data;

			photos_cache = g_slist_remove (photos_cache, pi);

			emu_free_photo_info (pi);
		}

		pi = g_new0 (PhotoInfo, 1);
		pi->address = g_strdup (addr);
		pi->photo = photo;

		photos_cache = g_slist_append (photos_cache, pi);
	}

	/* some photo found, use it */
	if (photo) {
		/* Form a mime part out of the photo */
		part = camel_mime_part_new ();
		camel_mime_part_set_content (part,
				    (const gchar *) photo->data.inlined.data,
				    photo->data.inlined.length, "image/jpeg");
	}

	G_UNLOCK (photos_cache);

	return part;
}

/* list of email addresses (strings) to remove from local cache of photos and
 * contacts, but only if the photo doesn't exist or is an not-found contact */
void
emu_remove_from_mail_cache (const GSList *addresses)
{
	const GSList *a;
	GSList *p;
	CamelInternetAddress *cia;

	cia = camel_internet_address_new ();

	for (a = addresses; a; a = a->next) {
		const gchar *addr = NULL;

		if (!a->data)
			continue;

		if (camel_address_decode ((CamelAddress *) cia, a->data) != -1 &&
		    camel_internet_address_get (cia, 0, NULL, &addr) && addr) {
			gchar *lowercase_addr = g_utf8_strdown (addr, -1);

			G_LOCK (contact_cache);
			if (g_hash_table_lookup (contact_cache, lowercase_addr) == NOT_FOUND_BOOK)
				g_hash_table_remove (contact_cache, lowercase_addr);
			G_UNLOCK (contact_cache);

			g_free (lowercase_addr);

			G_LOCK (photos_cache);
			for (p = photos_cache; p; p = p->next) {
				PhotoInfo *pi = p->data;

				if (pi && !pi->photo && g_ascii_strcasecmp (pi->address, addr) == 0) {
					photos_cache = g_slist_remove (photos_cache, pi);
					emu_free_photo_info (pi);
					break;
				}
			}
			G_UNLOCK (photos_cache);
		}
	}

	g_object_unref (cia);
}

void
emu_remove_from_mail_cache_1 (const gchar *address)
{
	GSList *l;

	g_return_if_fail (address != NULL);

	l = g_slist_append (NULL, (gpointer) address);

	emu_remove_from_mail_cache (l);

	g_slist_free (l);
}

/* frees all data created by call of em_utils_in_addressbook() or
 * em_utils_contact_photo() */
void
emu_free_mail_cache (void)
{
	G_LOCK (contact_cache);

	if (emu_books_hash) {
		g_hash_table_destroy (emu_books_hash);
		emu_books_hash = NULL;
	}

	if (emu_broken_books_hash) {
		g_hash_table_destroy (emu_broken_books_hash);
		emu_broken_books_hash = NULL;
	}

	if (emu_books_source_list) {
		g_object_unref (emu_books_source_list);
		emu_books_source_list = NULL;
	}

	if (contact_cache) {
		g_hash_table_destroy (contact_cache);
		contact_cache = NULL;
	}

	G_UNLOCK (contact_cache);

	G_LOCK (photos_cache);

	g_slist_foreach (photos_cache, (GFunc) emu_free_photo_info, NULL);
	g_slist_free (photos_cache);
	photos_cache = NULL;

	G_UNLOCK (photos_cache);
}

void
em_utils_clear_get_password_canceled_accounts_flag (void)
{
	EAccountList *account_list;
	EIterator *iterator;

	account_list = e_get_account_list ();

	for (iterator = e_list_get_iterator (E_LIST (account_list));
	     e_iterator_is_valid (iterator);
	     e_iterator_next (iterator)) {
		EAccount *account = (EAccount *) e_iterator_get (iterator);

		if (account && account->source)
			account->source->get_password_canceled = FALSE;

		if (account && account->transport)
			account->transport->get_password_canceled = FALSE;
	}

	g_object_unref (iterator);
}

gchar *
em_utils_url_unescape_amp (const gchar *url)
{
	gchar *buff;
	gint i, j, amps;

	if (!url)
		return NULL;

	amps = 0;
	for (i = 0; url[i]; i++) {
		if (url [i] == '&' && strncmp (url + i, "&amp;", 5) == 0)
			amps++;
	}

	buff = g_strdup (url);

	if (!amps)
		return buff;

	for (i = 0, j = 0; url[i]; i++, j++) {
		buff[j] = url[i];

		if (url [i] == '&' && strncmp (url + i, "&amp;", 5) == 0)
			i += 4;
	}
	buff[j] = 0;

	return buff;
}

static EAccount *
guess_account_from_folder (CamelFolder *folder)
{
	CamelStore *store;
	const gchar *uid;

	store = camel_folder_get_parent_store (folder);
	uid = camel_service_get_uid (CAMEL_SERVICE (store));

	return e_get_account_by_uid (uid);
}

static EAccount *
guess_account_from_message (CamelMimeMessage *message)
{
	const gchar *uid;

	uid = camel_mime_message_get_source (message);

	return (uid != NULL) ? e_get_account_by_uid (uid) : NULL;
}

GHashTable *
em_utils_generate_account_hash (void)
{
	GHashTable *account_hash;
	EAccount *account, *def;
	EAccountList *account_list;
	EIterator *iterator;

	account_list = e_get_account_list ();
	account_hash = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);

	def = e_get_default_account ();

	iterator = e_list_get_iterator (E_LIST (account_list));

	while (e_iterator_is_valid (iterator)) {
		account = (EAccount *) e_iterator_get (iterator);

		if (account->id->address) {
			EAccount *acnt;

			/* Accounts with identical email addresses that are
			 * enabled take precedence over the accounts that
			 * aren't. If all accounts with matching email
			 * addresses are disabled, then the first one in
			 * the list takes precedence. The default account
			 * always takes precedence no matter what. */
			acnt = g_hash_table_lookup (
				account_hash, account->id->address);
			if (acnt && acnt != def && !acnt->enabled && account->enabled) {
				g_hash_table_remove (
					account_hash, acnt->id->address);
				acnt = NULL;
			}

			if (!acnt)
				g_hash_table_insert (
					account_hash, (gchar *)
					account->id->address,
					(gpointer) account);
		}

		e_iterator_next (iterator);
	}

	g_object_unref (iterator);

	/* The default account has to be there if none
	 * of the enabled accounts are present. */
	if (g_hash_table_size (account_hash) == 0 && def && def->id->address)
		g_hash_table_insert (
			account_hash, (gchar *)
			def->id->address,
			(gpointer) def);

	return account_hash;
}

EAccount *
em_utils_guess_account (CamelMimeMessage *message,
                        CamelFolder *folder)
{
	EAccount *account = NULL;

	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);

	if (folder != NULL)
		g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* check for newsgroup header */
	if (folder != NULL
	    && camel_medium_get_header (CAMEL_MEDIUM (message), "Newsgroups"))
		account = guess_account_from_folder (folder);

	/* check for source folder */
	if (account == NULL && folder != NULL)
		account = guess_account_from_folder (folder);

	/* then message source */
	if (account == NULL)
		account = guess_account_from_message (message);

	return account;
}

EAccount *
em_utils_guess_account_with_recipients (CamelMimeMessage *message,
                                        CamelFolder *folder)
{
	EAccount *account = NULL;
	EAccountList *account_list;
	GHashTable *recipients;
	EIterator *iterator;
	CamelInternetAddress *addr;
	const gchar *type;
	const gchar *key;

	/* This policy is subject to debate and tweaking,
	 * but please also document the rational here. */

	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);

	/* Build a set of email addresses in which to test for membership.
	 * Only the keys matter here; the values just need to be non-NULL. */
	recipients = g_hash_table_new (g_str_hash, g_str_equal);

	type = CAMEL_RECIPIENT_TYPE_TO;
	addr = camel_mime_message_get_recipients (message, type);
	if (addr != NULL) {
		gint index = 0;

		while (camel_internet_address_get (addr, index++, NULL, &key))
			g_hash_table_insert (
				recipients, (gpointer) key,
				GINT_TO_POINTER (1));
	}

	type = CAMEL_RECIPIENT_TYPE_CC;
	addr = camel_mime_message_get_recipients (message, type);
	if (addr != NULL) {
		gint index = 0;

		while (camel_internet_address_get (addr, index++, NULL, &key))
			g_hash_table_insert (
				recipients, (gpointer) key,
				GINT_TO_POINTER (1));
	}

	/* First Preference: We were given a folder that maps to an
	 * enabled account, and that account's email address appears
	 * in the list of To: or Cc: recipients. */

	if (folder != NULL)
		account = guess_account_from_folder (folder);

	if (account == NULL || !account->enabled)
		goto second_preference;

	if ((key = account->id->address) == NULL)
		goto second_preference;

	if (g_hash_table_lookup (recipients, key) != NULL)
		goto exit;

second_preference:

	/* Second Preference: Choose any enabled account whose email
	 * address appears in the list to To: or Cc: recipients. */

	account_list = e_get_account_list ();
	iterator = e_list_get_iterator (E_LIST (account_list));

	while (e_iterator_is_valid (iterator)) {
		account = (EAccount *) e_iterator_get (iterator);
		e_iterator_next (iterator);

		if (account == NULL || !account->enabled)
			continue;

		if ((key = account->id->address) == NULL)
			continue;

		if (g_hash_table_lookup (recipients, key) != NULL) {
			g_object_unref (iterator);
			goto exit;
		}
	}
	g_object_unref (iterator);

	/* Last Preference: Defer to em_utils_guess_account(). */
	account = em_utils_guess_account (message, folder);

exit:
	g_hash_table_destroy (recipients);

	return account;
}

void
emu_restore_folder_tree_state (EMFolderTree *folder_tree)
{
	EShell *shell;
	EShellBackend *backend;
	GKeyFile *key_file;
	const gchar *config_dir;
	gchar *filename;

	g_return_if_fail (folder_tree != NULL);
	g_return_if_fail (EM_IS_FOLDER_TREE (folder_tree));

	shell = e_shell_get_default ();
	backend = e_shell_get_backend_by_name (shell, "mail");
	g_return_if_fail (backend != NULL);

	config_dir = e_shell_backend_get_config_dir (backend);
	g_return_if_fail (config_dir != NULL);

	filename = g_build_filename (config_dir, "state.ini", NULL);

	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, filename, 0, NULL);
	g_free (filename);

	em_folder_tree_restore_state (folder_tree, key_file);

	g_key_file_free (key_file);
}

/* Returns TRUE if CamelURL points to a local mbox file. */
gboolean
em_utils_is_local_delivery_mbox_file (CamelURL *url)
{
	g_return_val_if_fail (url != NULL, FALSE);

	return g_str_equal (url->protocol, "mbox") &&
		(url->path != NULL) &&
		g_file_test (url->path, G_FILE_TEST_EXISTS) &&
		!g_file_test (url->path, G_FILE_TEST_IS_DIR);
}

static void
cancel_service_connect_cb (GCancellable *cancellable,
                           CamelService *service)
{
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	camel_service_cancel_connect (service);
}

gboolean
em_utils_connect_service_sync (CamelService *service,
                               GCancellable *cancellable,
                               GError **error)
{
	gboolean res;
	gulong handler_id = 0;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	if (cancellable != NULL)
		handler_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (cancel_service_connect_cb),
			service, NULL);

	res = camel_service_connect_sync (service, error);

	if (handler_id)
		g_cancellable_disconnect (cancellable, handler_id);

	return res;
}

gboolean
em_utils_disconnect_service_sync (CamelService *service,
                                  gboolean clean,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gboolean res;
	gulong handler_id = 0;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	if (cancellable != NULL)
		handler_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (cancel_service_connect_cb),
			service, NULL);

	res = camel_service_disconnect_sync (service, clean, error);

	if (handler_id)
		g_cancellable_disconnect (cancellable, handler_id);

	return res;
}

static gboolean
check_prefix (const gchar *subject,
	      const gchar *prefix,
	      gint *skip_len)
{
	gint plen;

	g_return_val_if_fail (subject != NULL, FALSE);
	g_return_val_if_fail (prefix != NULL, FALSE);
	g_return_val_if_fail (*prefix, FALSE);
	g_return_val_if_fail (skip_len != NULL, FALSE);

	plen = strlen (prefix);
	if (g_ascii_strncasecmp (subject, prefix, plen) != 0)
		return FALSE;

	if (g_ascii_strncasecmp (subject + plen, ": ", 2) == 0) {
		*skip_len = plen + 2;
		return TRUE;
	}

	if (g_ascii_strncasecmp (subject + plen, " : ", 3) == 0) {
		*skip_len = plen + 3;
		return TRUE;
	}

	return FALSE;
}

gboolean
em_utils_is_re_in_subject (EShell *shell,
			   const gchar *subject,
			   gint *skip_len)
{
	EShellSettings *shell_settings;
	gchar *prefixes, **prefixes_strv;
	gboolean res;
	gint ii;

	g_return_val_if_fail (shell != NULL, FALSE);
	g_return_val_if_fail (subject != NULL, FALSE);
	g_return_val_if_fail (skip_len != NULL, FALSE);

	*skip_len = -1;

	if (strlen (subject) < 3)
		return FALSE;

	if (check_prefix (subject, "Re", skip_len))
		return TRUE;

	shell_settings = e_shell_get_shell_settings (shell);
	prefixes = e_shell_settings_get_string (shell_settings, "composer-localized-re");
	if (!prefixes || !*prefixes) {
		g_free (prefixes);
		return FALSE;
	}

	prefixes_strv = g_strsplit (prefixes, ",", -1);
	g_free (prefixes);

	if (!prefixes_strv)
		return FALSE;

	res = FALSE;

	for (ii = 0; !res && prefixes_strv[ii]; ii++) {
		const gchar *prefix = prefixes_strv[ii];

		if (*prefix)
			res = check_prefix (subject, prefix, skip_len);
	}

	g_strfreev (prefixes_strv);

	return res;
}
