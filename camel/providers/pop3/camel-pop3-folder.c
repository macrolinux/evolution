/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-folder.c : class for a pop3 folder */

/* 
 * Authors:
 *   Dan Winship <danw@helixcode.com>
 *
 * Copyright (C) 2000 Helix Code, Inc. (www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "camel-pop3-folder.h"
#include "camel-pop3-store.h"
#include "camel-exception.h"
#include "camel-stream-mem.h"
#include "camel-stream-filter.h"
#include "camel-mime-message.h"
#include "camel-operation.h"

#include <stdlib.h>
#include <string.h>

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(o)))
static CamelFolderClass *parent_class;

static void pop3_finalize (CamelObject *object);

static void pop3_refresh_info (CamelFolder *folder, CamelException *ex);
static void pop3_sync (CamelFolder *folder, gboolean expunge,
		       CamelException *ex);

static gint pop3_get_message_count (CamelFolder *folder);
static GPtrArray *pop3_get_uids (CamelFolder *folder);
static CamelMimeMessage *pop3_get_message (CamelFolder *folder, 
					   const char *uid,
					   CamelException *ex);
static void pop3_set_message_flags (CamelFolder *folder, const char *uid,
				    guint32 flags, guint32 set);

static GPtrArray *parse_listing (int count, char *data);

static void
camel_pop3_folder_class_init (CamelPop3FolderClass *camel_pop3_folder_class)
{
	CamelFolderClass *camel_folder_class =
		CAMEL_FOLDER_CLASS (camel_pop3_folder_class);

	parent_class = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs (camel_folder_get_type ()));

	/* virtual method overload */
	camel_folder_class->refresh_info = pop3_refresh_info;
	camel_folder_class->sync = pop3_sync;

	camel_folder_class->get_message_count = pop3_get_message_count;
	camel_folder_class->get_uids = pop3_get_uids;
	camel_folder_class->free_uids = camel_folder_free_nop;

	camel_folder_class->get_message = pop3_get_message;
	camel_folder_class->set_message_flags = pop3_set_message_flags;
}

static void
camel_pop3_folder_init (gpointer object)
{
	CamelFolder *folder = CAMEL_FOLDER (object);
	CamelPop3Folder *pop3_folder = CAMEL_POP3_FOLDER (object);

	folder->has_summary_capability = FALSE;
	folder->has_search_capability = FALSE;

	pop3_folder->uids = NULL;
	pop3_folder->flags = NULL;
}

CamelType
camel_pop3_folder_get_type (void)
{
	static CamelType camel_pop3_folder_type = CAMEL_INVALID_TYPE;

	if (!camel_pop3_folder_type) {
		camel_pop3_folder_type = camel_type_register (CAMEL_FOLDER_TYPE, "CamelPop3Folder",
							      sizeof (CamelPop3Folder),
							      sizeof (CamelPop3FolderClass),
							      (CamelObjectClassInitFunc) camel_pop3_folder_class_init,
							      NULL,
							      (CamelObjectInitFunc) camel_pop3_folder_init,
							      (CamelObjectFinalizeFunc) pop3_finalize);
	}

	return camel_pop3_folder_type;
}

void
pop3_finalize (CamelObject *object)
{
	CamelPop3Folder *pop3_folder = CAMEL_POP3_FOLDER (object);

	if (pop3_folder->uids)
		camel_folder_free_deep (NULL, pop3_folder->uids);
	if (pop3_folder->flags)
		g_free (pop3_folder->flags);
}

CamelFolder *
camel_pop3_folder_new (CamelStore *parent, CamelException *ex)
{
	CamelFolder *folder;

	folder = CAMEL_FOLDER (camel_object_new (CAMEL_POP3_FOLDER_TYPE));
	camel_folder_construct (folder, parent, "inbox", "inbox");

	/* mt-ok, since we dont have the folder-lock for new() */
	camel_folder_refresh_info (folder, ex);/* mt-ok */
	if (camel_exception_is_set (ex)) {
		camel_object_unref (CAMEL_OBJECT (folder));
		folder = NULL;
	}

	return folder;
}

static void 
pop3_refresh_info (CamelFolder *folder, CamelException *ex)
{
	GPtrArray *uids;
	int status, count;
	char *data;
	CamelPop3Folder *pop3_folder = (CamelPop3Folder *) folder;
	CamelPop3Store *pop3_store = CAMEL_POP3_STORE (folder->parent_store);

	camel_operation_start(NULL, _("Retrieving POP summary"));

	status = camel_pop3_command (pop3_store, &data, ex, "STAT");
	if (status != CAMEL_POP3_OK) {
		camel_operation_end(NULL);
		return;
	}

	count = atoi (data);
	g_free (data);

	if (pop3_store->supports_uidl != FALSE) {
		status = camel_pop3_command (pop3_store, NULL, ex, "UIDL");
		switch (status) {
		case CAMEL_POP3_ERR:
			pop3_store->supports_uidl = FALSE;
			break;
		case CAMEL_POP3_FAIL:
			camel_operation_end(NULL);
			return;
		}
	}

	if (pop3_store->supports_uidl == FALSE) {
		int i;

		uids = g_ptr_array_new ();
		g_ptr_array_set_size (uids, count);

		for (i = 0; i < count; i++)
			uids->pdata[i] = g_strdup_printf ("%d", i + 1);
		camel_operation_end(NULL);
	} else {
		data = camel_pop3_command_get_additional_data (pop3_store, 0, ex);
		camel_operation_end(NULL);
		if (camel_exception_is_set (ex))
			return;

		uids = parse_listing (count, data);
		g_free (data);

		if (!uids) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Could not open folder: "
						"message listing was "
						"incomplete."));
			return;
		}
	}

	pop3_folder->uids = uids;
	pop3_folder->flags = g_new0 (guint32, uids->len);
}

static void
pop3_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelPop3Folder *pop3_folder;
	CamelPop3Store *pop3_store;
	int i, status;
	char *resp;

	if (!expunge)
		return;

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (folder->parent_store);

	for (i = 0; i < pop3_folder->uids->len; i++) {
		if (pop3_folder->flags[i] & CAMEL_MESSAGE_DELETED) {
			status = camel_pop3_command (pop3_store, &resp, ex,
						     "DELE %d", i + 1);
			if (status != CAMEL_POP3_OK)
				return;
		}
	}

	camel_pop3_store_expunge (pop3_store, ex);
}


static GPtrArray *
parse_listing (int count, char *data)
{
	GPtrArray *ans;
	char *p;
	int index, len;

	ans = g_ptr_array_new ();
	g_ptr_array_set_size (ans, count);

	p = data;
	while (*p) {
		index = strtoul (p, &p, 10);
		len = strcspn (p, "\n");
		if (index <= count && *p == ' ')
			ans->pdata[index - 1] = g_strndup (p + 1, len - 1);
		p += len;
		if (*p == '\n')
			p++;
	}

	for (index = 0; index < count; index++) {
		if (ans->pdata[index] == NULL) {
			g_ptr_array_free (ans, TRUE);
			return NULL;
		}
	}

	return ans;
}

static int
uid_to_number (CamelPop3Folder *pop3_folder, const char *uid)
{
	int i;

	for (i = 0; i < pop3_folder->uids->len; i++) {
		if (!strcmp (uid, pop3_folder->uids->pdata[i]))
			return i + 1;
	}

	return -1;
}


static CamelMimeMessage *
pop3_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	int status, num, total;
	char *result, *body;
	CamelStream *msgstream;
	CamelMimeMessage *msg;

	num = uid_to_number (CAMEL_POP3_FOLDER (folder), uid);
	if (num == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				      _("No message with uid %s"), uid);
		return NULL;
	}

	camel_operation_start(NULL, _("Retrieving POP message %d"), num);

	status = camel_pop3_command (CAMEL_POP3_STORE (folder->parent_store),
				     &result, ex, "RETR %d", num);
	if (status != CAMEL_POP3_OK) {
		camel_operation_end(NULL);
		return NULL;
	}

	/* this should be "nnn octets" ? */
	if (sscanf(result, "%d", &total) != 1)
		total = 0;

	g_free (result);
	body = camel_pop3_command_get_additional_data (CAMEL_POP3_STORE (folder->parent_store), total, ex);
	if (!body) {
		CamelService *service = CAMEL_SERVICE (folder->parent_store);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not retrieve message from POP "
					"server %s: %s"), service->url->host,
				      camel_exception_get_description (ex));
		camel_operation_end(NULL);
		return NULL;
	}

	msgstream = camel_stream_mem_new_with_buffer (body, strlen (body));
	g_free (body);
	
	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),
						  CAMEL_STREAM (msgstream));

	camel_object_unref (CAMEL_OBJECT (msgstream));

	camel_operation_end(NULL);

	return msg;
}

static void
pop3_set_message_flags (CamelFolder *folder, const char *uid,
			guint32 flags, guint32 set)
{
	CamelPop3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);
	int num;

	num = uid_to_number (pop3_folder, uid);
	if (num == -1)
		return;

	pop3_folder->flags[num - 1] =
		(pop3_folder->flags[num] & ~flags) | (set & flags);
}

static gint
pop3_get_message_count (CamelFolder *folder)
{
	CamelPop3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);

	return pop3_folder->uids->len;
}

static GPtrArray *
pop3_get_uids (CamelFolder *folder)
{
	CamelPop3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);

	return pop3_folder->uids;
}
