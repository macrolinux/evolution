#ifndef _MESSAGE_THREAD_H
#define _MESSAGE_THREAD_H

#include <gnome.h>
#include "message-list.h"

struct _container {
	/* Next must be the first member */
	struct _container *next,
		*parent,
		*child;
	const CamelMessageInfo *message;
	char *root_subject;	/* cached root equivalent subject */
	int re;			/* re version of subject? */
	int order;
};

void mail_do_thread_messages (MessageList *ml, GPtrArray *uids, 
			      gboolean use_camel_uidfree,
			      void (*build) (MessageList *,
					     struct _container *));

#endif /* !_MESSAGE_THREAD_H */

