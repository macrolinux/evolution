/* Copyright (C) 2005 Novell Inc. */
/* This file is licensed under the GNU GPL v2 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* We include gi18n-lib.h so that we have strings translated directly for this package */
#include <glib/gi18n-lib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ytnef.h>

#include <camel/camel-mime-part.h>
#include <camel/camel-folder.h>
#include <camel/camel-exception.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-fs.h>

#include <mail/em-format.h>
#include <mail/em-format-hook.h>
#include <mail/em-utils.h>
#include <e-util/e-error.h>
#include <e-util/e-mktemp.h>

int verbose = 0;
int saveRTF = 0;
int saveintermediate = 0;
char *filepath = NULL;
void processTnef(TNEFStruct tnef);
void saveVCalendar(TNEFStruct tnef);
void saveVCard(TNEFStruct tnef);
void saveVTask(TNEFStruct tnef);

void org_gnome_notzed_format_tnef(void *ep, EMFormatHookTarget *t);

void
org_gnome_notzed_format_tnef(void *ep, EMFormatHookTarget *t)
{
	char *tmpdir = NULL, *name = NULL, *cmd;
	CamelStream *out;
	struct dirent *d;
	DIR *dir;
	CamelMultipart *mp;
	CamelMimePart *mainpart;
	CamelDataWrapper *content;
	int len;
	TNEFStruct tnef;

	tmpdir = e_mkdtemp("tnef-attachment-XXXXXX");
	if (tmpdir == NULL)
		return;

	filepath = tmpdir;

	name = g_build_filename(tmpdir, ".evo-attachment.tnef", NULL);

	out = camel_stream_fs_new_with_name(name, O_RDWR|O_CREAT, 0666);
	if (out == NULL)
		goto fail;
	content = camel_medium_get_content_object((CamelMedium *)t->part);
	if (content == NULL)
		goto fail;
	if (camel_data_wrapper_decode_to_stream(content, out) == -1
	    || camel_stream_close(out) == -1) {
		camel_object_unref(out);
 		goto fail;
	}
	camel_object_unref(out);

	/* Extracting the winmail.dat */
        TNEFInitialize(&tnef);
	tnef.Debug = verbose;
        if (TNEFParseFile(name, &tnef) == -1) {
            printf("ERROR processing file\n");
        } 
	processTnef(tnef); 

        TNEFFree(&tnef);
	/* Extraction done */

	dir = opendir(tmpdir);
	if (dir == NULL)
	    goto fail;

	mainpart = camel_mime_part_new();

	mp = camel_multipart_new();
	camel_data_wrapper_set_mime_type((CamelDataWrapper *)mp, "multipart/mixed");
	camel_multipart_set_boundary(mp, NULL);

	camel_medium_set_content_object((CamelMedium *)mainpart, (CamelDataWrapper *)mp);

	while ((d = readdir(dir))) {
		CamelMimePart *part;
		CamelDataWrapper *content;
		CamelStream *stream;
		char *path;
		const char *type;

		if (!strcmp(d->d_name, ".") 
		    || !strcmp(d->d_name, "..") 
		    || !strcmp(d->d_name, ".evo-attachment.tnef"))
		    continue;
		
		path = g_build_filename(tmpdir, d->d_name, NULL);
		
		stream = camel_stream_fs_new_with_name(path, O_RDONLY, 0);
		content = camel_data_wrapper_new();
		camel_data_wrapper_construct_from_stream(content, stream);
		camel_object_unref(stream);

		part = camel_mime_part_new();
		camel_mime_part_set_encoding(part, CAMEL_TRANSFER_ENCODING_BINARY);

		camel_medium_set_content_object((CamelMedium *)part, content);
		camel_object_unref(content);
		
		type = em_utils_snoop_type(part);
		if (type)
		    camel_data_wrapper_set_mime_type((CamelDataWrapper *)part, type);
		
		camel_mime_part_set_filename(part, d->d_name);
		
		g_free(path);
		
		camel_multipart_add_part(mp, part);
	}
	
	closedir(dir);
	
	len = t->format->part_id->len;
	g_string_append_printf(t->format->part_id, ".tnef");

	if (camel_multipart_get_number(mp) > 0)
		em_format_part_as(t->format, t->stream, mainpart, "multipart/mixed");
	else if (t->item->handler.old)
	    t->item->handler.old->handler(t->format, t->stream, t->part, t->item->handler.old);

	g_string_truncate(t->format->part_id, len);
	
	camel_object_unref(mainpart);

	goto ok;
 fail:
	if (t->item->handler.old)
	    t->item->handler.old->handler(t->format, t->stream, t->part, t->item->handler.old);
 ok:
	g_free(name);
	g_free(tmpdir);
}


int e_plugin_lib_enable(EPluginLib *ep, int enable);

int
e_plugin_lib_enable(EPluginLib *ep, int enable)
{
    if (enable) {
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    } 
    return 0;
}

void processTnef(TNEFStruct tnef) { 
    char *astring;
    variableLength *filename;
    variableLength *filedata;
    Attachment *p;
    int RealAttachment;
    int object;
    char ifilename[256];
    int i, count;
    int foundCal=0;

    FILE *fptr;
    
    /* First see if this requires special processing. */
    /* ie: it's a Contact Card, Task, or Meeting request (vCal/vCard) */
    if (tnef.messageClass[0] != 0)  {
        if (strcmp(tnef.messageClass, "IPM.Contact") == 0) {
            saveVCard(tnef );
        }
        if (strcmp(tnef.messageClass, "IPM.Task") == 0) {
            saveVTask(tnef);
        }
        if (strcmp(tnef.messageClass, "IPM.Appointment") == 0) {
            saveVCalendar(tnef);
            foundCal = 1;
        }
    }
    
    if ((filename = MAPIFindUserProp(&(tnef.MapiProperties), 
                        PROP_TAG(PT_STRING8,0x24))) != MAPI_UNDEFINED) {
        if (strcmp(filename->data, "IPM.Appointment") == 0) {
             /* If it's "indicated" twice, we don't want to save 2 calendar entries. */
            if (foundCal == 0) {
                saveVCalendar(tnef);
            }
        }
    }

    if (strcmp(tnef.messageClass, "IPM.Microsoft Mail.Note") == 0) {
        if ((saveRTF == 1) && (tnef.subject.size > 0)) {
            /*  Description */
            if ((filename=MAPIFindProperty(&(tnef.MapiProperties),
					   PROP_TAG(PT_BINARY, PR_RTF_COMPRESSED)))
                    != MAPI_UNDEFINED) {
                int size;
                variableLength buf;
		buf.data="";
		buf.size=0;
                if ((buf.data = DecompressRTF(filename, &(buf.size))) != NULL) {
                    if (filepath == NULL) {
                        sprintf(ifilename, "%s.rtf", tnef.subject.data);
                    } else {
                        sprintf(ifilename, "%s/%s.rtf", filepath, tnef.subject.data);
                    }
                    for(i=0; i<strlen(ifilename); i++) 
                        if (ifilename[i] == ' ') 
                            ifilename[i] = '_';

                    if ((fptr = fopen(ifilename, "wb"))==NULL) {
                        printf("ERROR: Error writing file to disk!");
                    } else {
                        fwrite(buf.data,
                                sizeof(BYTE), 
                                buf.size, 
                                fptr);
                        fclose(fptr);
                    }
                    free(buf.data);
		    buf.data="";
		    buf.size=0;
                }
            } 
	}
    }

    /* Now process each attachment */
    p = tnef.starting_attach.next;
    count = 0;
    while (p != NULL) {
        count++;
        /* Make sure it has a size. */
        if (p->FileData.size > 0) {
            object = 1;           
            
            /* See if the contents are stored as "attached data" */
	    /* Inside the MAPI blocks. */
            if((filedata = MAPIFindProperty(&(p->MAPI), 
                                    PROP_TAG(PT_OBJECT, PR_ATTACH_DATA_OBJ))) 
                    == MAPI_UNDEFINED) {
                if((filedata = MAPIFindProperty(&(p->MAPI), 
                                    PROP_TAG(PT_BINARY, PR_ATTACH_DATA_OBJ))) 
		   == MAPI_UNDEFINED) {
                    /* Nope, standard TNEF stuff. */
                    filedata = &(p->FileData);
                    object = 0;
                }
            }
            /* See if this is an embedded TNEF stream. */
            RealAttachment = 1;
            if (object == 1) {
                /*  This is an "embedded object", so skip the */
                /* 16-byte identifier first. */
                TNEFStruct emb_tnef;
                DWORD signature;
                memcpy(&signature, filedata->data+16, sizeof(DWORD));
                if (TNEFCheckForSignature(signature) == 0) {
                    /* Has a TNEF signature, so process it. */
                    TNEFInitialize(&emb_tnef);
                    emb_tnef.Debug = tnef.Debug;
                    if (TNEFParseMemory(filedata->data+16, 
                            filedata->size-16, &emb_tnef) != -1) {
                        preocessTnef(emb_tnef);
                        RealAttachment = 0;
                    }
                    TNEFFree(&emb_tnef);
                }
            } else {
                TNEFStruct emb_tnef;
                DWORD signature;
                memcpy(&signature, filedata->data, sizeof(DWORD));
                if (TNEFCheckForSignature(signature) == 0) {
                    /* Has a TNEF signature, so process it. */
                    TNEFInitialize(&emb_tnef);
                    emb_tnef.Debug = tnef.Debug;
                    if (TNEFParseMemory(filedata->data, 
                            filedata->size, &emb_tnef) != -1) {
                        preocessTnef(emb_tnef);
                        RealAttachment = 0;
                    }
                    TNEFFree(&emb_tnef);
                }
            }
            if ((RealAttachment == 1) || (saveintermediate == 1)) {
                /* Ok, it's not an embedded stream, so now we */
		/* process it. */
                if ((filename = MAPIFindProperty(&(p->MAPI), 
                                        PROP_TAG(30,0x3707))) 
                        == MAPI_UNDEFINED) {
                    if ((filename = MAPIFindProperty(&(p->MAPI), 
                                        PROP_TAG(30,0x3001))) 
                            == MAPI_UNDEFINED) {
                        filename = &(p->Title);
                    }
                }
                if (filename->size == 1) {
                    filename = (variableLength*)malloc(sizeof(variableLength));
                    filename->size = 20;
                    filename->data = (char*)malloc(20);
                    sprintf(filename->data, "file_%03i.dat", count);
                }
                if (filepath == NULL) {
                    sprintf(ifilename, "%s", filename->data);
                } else {
                    sprintf(ifilename, "%s/%s", filepath, filename->data);
                }
                for(i=0; i<strlen(ifilename); i++) 
                    if (ifilename[i] == ' ') 
                        ifilename[i] = '_';

		if ((fptr = fopen(ifilename, "wb"))==NULL) {
		    printf("ERROR: Error writing file to disk!");
		} else {
		    if (object == 1) {
			fwrite(filedata->data + 16, 
			       sizeof(BYTE), 
			       filedata->size - 16, 
			       fptr);
		    } else {
			fwrite(filedata->data, 
			       sizeof(BYTE), 
			       filedata->size, 
			       fptr);
		    }
		    fclose(fptr);
		} 
            } 
        } /* if size>0 */
        p=p->next;
    } /* while p!= null */
}

void saveVCard(TNEFStruct tnef) {
    char ifilename[512];
    FILE *fptr;
    variableLength *vl;
    variableLength *pobox, *street, *city, *state, *zip, *country;
    dtr thedate;
    int boolean,index,i;

    if ((vl = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_DISPLAY_NAME))) == MAPI_UNDEFINED) {
        if ((vl=MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_COMPANY_NAME))) == MAPI_UNDEFINED) {
            if (tnef.subject.size > 0) {
                if (filepath == NULL) {
                    sprintf(ifilename, "%s.vcard", tnef.subject.data);
                } else {
                    sprintf(ifilename, "%s/%s.vcard", filepath, tnef.subject.data);
                }
            } else {
                if (filepath == NULL) {
                    sprintf(ifilename, "unknown.vcard");
                } else {
                    sprintf(ifilename, "%s/unknown.vcard", filepath);
                }
            }
        } else {
            if (filepath == NULL) {
                sprintf(ifilename, "%s.vcard", vl->data);
            } else {
                sprintf(ifilename, "%s/%s.vcard", filepath, vl->data);
            }
        }
    } else {
        if (filepath == NULL) {
            sprintf(ifilename, "%s.vcard", vl->data);
        } else {
            sprintf(ifilename, "%s/%s.vcard", filepath, vl->data);
        }
    }
    for(i=0; i<strlen(ifilename); i++) 
        if (ifilename[i] == ' ') 
            ifilename[i] = '_';
    printf("%s\n", ifilename);

    if ((fptr = fopen(ifilename, "wb"))==NULL) {
            printf("Error writing file to disk!");
    } else {
        fprintf(fptr, "BEGIN:VCARD\n");
        fprintf(fptr, "VERSION:2.1\n");
        if (vl != MAPI_UNDEFINED) {
            fprintf(fptr, "FN:%s\n", vl->data);
        }
        fprintProperty(tnef, fptr, PT_STRING8, PR_NICKNAME, "NICKNAME:%s\n");
        fprintUserProp(tnef, fptr, PT_STRING8, 0x8554, "MAILER:Microsoft Outlook %s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_SPOUSE_NAME, "X-EVOLUTION-SPOUSE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_MANAGER_NAME, "X-EVOLUTION-MANAGER:%s\n");       
        fprintProperty(tnef, fptr, PT_STRING8, PR_ASSISTANT, "X-EVOLUTION-ASSISTANT:%s\n");

        /* Organizational */
        if ((vl=MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_COMPANY_NAME))) != MAPI_UNDEFINED) {
            if (vl->size > 0) {
                if ((vl->size == 1) && (vl->data[0] == 0)) {
                } else {
                    fprintf(fptr,"ORG:%s", vl->data);
                    if ((vl=MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_DEPARTMENT_NAME))) != MAPI_UNDEFINED) {
                        fprintf(fptr,";%s", vl->data);
                    }
                    fprintf(fptr, "\n");
                }
            }
        }

        fprintProperty(tnef, fptr, PT_STRING8, PR_OFFICE_LOCATION, "X-EVOLUTION-OFFICE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_TITLE, "TITLE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_PROFESSION, "ROLE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_BODY, "NOTE:%s\n");
        if (tnef.body.size > 0) {
            fprintf(fptr, "NOTE;QUOTED-PRINTABLE:");
            quotedfprint(fptr, &(tnef.body));
            fprintf(fptr,"\n");
        }


        /* Business Address */
        boolean = 0;
        if ((pobox = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_POST_OFFICE_BOX))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((street = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_STREET_ADDRESS))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((city = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_LOCALITY))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((state = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_STATE_OR_PROVINCE))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((zip = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_POSTAL_CODE))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((country = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_COUNTRY))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if (boolean == 1) {
            fprintf(fptr, "ADR;QUOTED-PRINTABLE;WORK:");
            if (pobox != MAPI_UNDEFINED) {
                quotedfprint(fptr, pobox);
            }
            fprintf(fptr, ";;");
            if (street != MAPI_UNDEFINED) {
                quotedfprint(fptr, street);
            }
            fprintf(fptr, ";");
            if (city != MAPI_UNDEFINED) {
                quotedfprint(fptr, city);
            }
            fprintf(fptr, ";");
            if (state != MAPI_UNDEFINED) {
                quotedfprint(fptr, state);
            }
            fprintf(fptr, ";");
            if (zip != MAPI_UNDEFINED) {
                quotedfprint(fptr, zip);
            }
            fprintf(fptr, ";");
            if (country != MAPI_UNDEFINED) {
                quotedfprint(fptr, country);
            }
            fprintf(fptr,"\n");
            if ((vl = MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x801b))) != MAPI_UNDEFINED) {
                fprintf(fptr, "LABEL;QUOTED-PRINTABLE;WORK:");
                quotedfprint(fptr, vl);
                fprintf(fptr,"\n");
            }
        }

        /* Home Address */
        boolean = 0;
        if ((pobox = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_HOME_ADDRESS_POST_OFFICE_BOX))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((street = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_HOME_ADDRESS_STREET))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((city = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_HOME_ADDRESS_CITY))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((state = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_HOME_ADDRESS_STATE_OR_PROVINCE))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((zip = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_HOME_ADDRESS_POSTAL_CODE))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((country = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_HOME_ADDRESS_COUNTRY))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if (boolean == 1) {
            fprintf(fptr, "ADR;QUOTED-PRINTABLE;HOME:");
            if (pobox != MAPI_UNDEFINED) {
                quotedfprint(fptr, pobox);
            }
            fprintf(fptr, ";;");
            if (street != MAPI_UNDEFINED) {
                quotedfprint(fptr, street);
            }
            fprintf(fptr, ";");
            if (city != MAPI_UNDEFINED) {
                quotedfprint(fptr, city);
            }
            fprintf(fptr, ";");
            if (state != MAPI_UNDEFINED) {
                quotedfprint(fptr, state);
            }
            fprintf(fptr, ";");
            if (zip != MAPI_UNDEFINED) {
                quotedfprint(fptr, zip);
            }
            fprintf(fptr, ";");
            if (country != MAPI_UNDEFINED) {
                quotedfprint(fptr, country);
            }
            fprintf(fptr,"\n");
            if ((vl = MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x801a))) != MAPI_UNDEFINED) {
                fprintf(fptr, "LABEL;QUOTED-PRINTABLE;WORK:");
                quotedfprint(fptr, vl);
                fprintf(fptr,"\n");
            }
        }

        /* Other Address */
        boolean = 0;
        if ((pobox = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_OTHER_ADDRESS_POST_OFFICE_BOX))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((street = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_OTHER_ADDRESS_STREET))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((city = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_OTHER_ADDRESS_CITY))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((state = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_OTHER_ADDRESS_STATE_OR_PROVINCE))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((zip = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_OTHER_ADDRESS_POSTAL_CODE))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if ((country = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_OTHER_ADDRESS_COUNTRY))) != MAPI_UNDEFINED) {
            boolean = 1;
        }
        if (boolean == 1) {
            fprintf(fptr, "ADR;QUOTED-PRINTABLE;OTHER:");
            if (pobox != MAPI_UNDEFINED) {
                quotedfprint(fptr, pobox);
            }
            fprintf(fptr, ";;");
            if (street != MAPI_UNDEFINED) {
                quotedfprint(fptr, street);
            }
            fprintf(fptr, ";");
            if (city != MAPI_UNDEFINED) {
                quotedfprint(fptr, city);
            }
            fprintf(fptr, ";");
            if (state != MAPI_UNDEFINED) {
                quotedfprint(fptr, state);
            }
            fprintf(fptr, ";");
            if (zip != MAPI_UNDEFINED) {
                quotedfprint(fptr, zip);
            }
            fprintf(fptr, ";");
            if (country != MAPI_UNDEFINED) {
                quotedfprint(fptr, country);
            }
            fprintf(fptr,"\n");
        }


        fprintProperty(tnef, fptr, PT_STRING8, PR_CALLBACK_TELEPHONE_NUMBER, "TEL;X-EVOLUTION-CALLBACK:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_PRIMARY_TELEPHONE_NUMBER, "TEL;PREF:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_MOBILE_TELEPHONE_NUMBER, "TEL;CELL:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_RADIO_TELEPHONE_NUMBER, "TEL;X-EVOLUTION-RADIO:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_CAR_TELEPHONE_NUMBER, "TEL;CAR:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_OTHER_TELEPHONE_NUMBER, "TEL;VOICE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_PAGER_TELEPHONE_NUMBER, "TEL;PAGER:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_TELEX_NUMBER, "TEL;X-EVOLUTION-TELEX:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_ISDN_NUMBER, "TEL;ISDN:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_HOME2_TELEPHONE_NUMBER, "TEL;HOME:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_TTYTDD_PHONE_NUMBER, "TEL;X-EVOLUTION-TTYTDD:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_HOME_TELEPHONE_NUMBER, "TEL;HOME;VOICE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_ASSISTANT_TELEPHONE_NUMBER, "TEL;X-EVOLUTION-ASSISTANT:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_COMPANY_MAIN_PHONE_NUMBER, "TEL;WORK:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_BUSINESS_TELEPHONE_NUMBER, "TEL;WORK:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_BUSINESS2_TELEPHONE_NUMBER, "TEL;WORK;VOICE:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_PRIMARY_FAX_NUMBER, "TEL;PREF;FAX:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_BUSINESS_FAX_NUMBER, "TEL;WORK;FAX:%s\n");
        fprintProperty(tnef, fptr, PT_STRING8, PR_HOME_FAX_NUMBER, "TEL;HOME;FAX:%s\n");


        /* Email addresses */
        if ((vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x8083))) == MAPI_UNDEFINED) {
            vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x8084));
        }
        if (vl != MAPI_UNDEFINED) {
            if (vl->size > 0) 
                fprintf(fptr, "EMAIL:%s\n", vl->data);
        }
        if ((vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x8093))) == MAPI_UNDEFINED) {
            vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x8094));
        }
        if (vl != MAPI_UNDEFINED) {
            if (vl->size > 0) 
                fprintf(fptr, "EMAIL:%s\n", vl->data);
        }
        if ((vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x80a3))) == MAPI_UNDEFINED) {
            vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x80a4));
        }
        if (vl != MAPI_UNDEFINED) {
            if (vl->size > 0) 
                fprintf(fptr, "EMAIL:%s\n", vl->data);
        }

        fprintProperty(tnef, fptr, PT_STRING8, PR_BUSINESS_HOME_PAGE, "URL:%s\n");
        fprintUserProp(tnef, fptr, PT_STRING8, 0x80d8, "FBURL:%s\n");



        /* Birthday */
        if ((vl=MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_SYSTIME, PR_BIRTHDAY))) != MAPI_UNDEFINED) {
            fprintf(fptr, "BDAY:");
            MAPISysTimetoDTR(vl->data, &thedate);
            fprintf(fptr, "%i-%02i-%02i\n", thedate.wYear, thedate.wMonth, thedate.wDay);
        }

        /* Anniversary */
        if ((vl=MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_SYSTIME, PR_WEDDING_ANNIVERSARY))) != MAPI_UNDEFINED) {
            fprintf(fptr, "X-EVOLUTION-ANNIVERSARY:");
            MAPISysTimetoDTR(vl->data, &thedate);
            fprintf(fptr, "%i-%02i-%02i\n", thedate.wYear, thedate.wMonth, thedate.wDay);
        }
        fprintf(fptr, "END:VCARD\n");
    }
}

unsigned char getRruleCount(unsigned char a, unsigned char b) {
    return ((a << 8) | b);
}

unsigned char getRruleMonthNum(unsigned char a, unsigned char b) {
    switch (a) {
        case 0x00:
            switch (b) {
                case 0x00:
                    /* Jan */
                    return(1);
                case 0xA3:
                    /* May */
                    return(5);
                case 0xAE:
                    /* Nov */
                    return(11);
            }
            break;
        case 0x60:
            switch (b) {
                case 0xAE:
                    /* Feb */
                    return(2);
                case 0x51:
                    /* Jun */
                    return(6);
            }
            break;
        case 0xE0:
            switch (b) {
                case 0x4B:
                    /* Mar */
                    return(3);
                case 0x56:
                    /* Sep */
                    return(9);
            }
            break;
        case 0x40:
            switch (b) {
                case 0xFA:
                    /* Apr */
                    return(4);
            }
            break;
        case 0x20:
            if (b == 0xFA) {
                /* Jul */
                return(7);
            }
            break;
        case 0x80:
            if (b == 0xA8) {
                /* Aug */
                return(8);
            }
            break;
        case 0xA0:
            if (b == 0xFF) {
                /* Oct */
                return(10);
            }
            break;
        case 0xC0:
            if (b == 0x56) {
                return(12);
            }
    }

    /*  Error */
    return(0);
}

char * getRruleDayname(unsigned char a) {
    static char daystring[25];

    *daystring = 0;

    if (a & 0x01) {
        strcat(daystring, "SU,");
    }
    if (a & 0x02) {
        strcat(daystring, "MO,");
    }
    if (a & 0x04) {
        strcat(daystring, "TU,");
    }
    if (a & 0x08) {
        strcat(daystring, "WE,");
    }
    if (a & 0x10) {
        strcat(daystring, "TH,");
    }
    if (a & 0x20) {
        strcat(daystring, "FR,");
    }
    if (a & 0x40) {
        strcat(daystring, "SA,");
    }

    if (strlen(daystring)) {
        daystring[strlen(daystring) - 1] = 0;
    }

    return(daystring);
}

void printRrule(FILE *fptr, char *recur_data, int size, TNEFStruct TNEF) {
    variableLength *filename;

    if (size < 0x1F) {
        return;
    }

    fprintf(fptr, "RRULE:FREQ=");

    if (recur_data[0x04] == 0x0A) {
        fprintf(fptr, "DAILY");

        if (recur_data[0x16] == 0x23 || recur_data[0x16] == 0x22 ||
                recur_data[0x16] == 0x21) {
            if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties),
                    PROP_TAG(PT_I2, 0x0011))) != MAPI_UNDEFINED) {
                fprintf(fptr, ";INTERVAL=%d", *(filename->data));
            }
            if (recur_data[0x16] == 0x22 || recur_data[0x16] == 0x21) {
                fprintf(fptr, ";COUNT=%d", 
                    getRruleCount(recur_data[0x1B], recur_data[0x1A]));
            }
        } else if (recur_data[0x16] == 0x3E) {
            fprintf(fptr, ";BYDAY=MO,TU,WE,TH,FR");
            if (recur_data[0x1A] == 0x22 || recur_data[0x1A] == 0x21) {
                fprintf(fptr, ";COUNT=%d", 
                    getRruleCount(recur_data[0x1F], recur_data[0x1E]));
            }
        }
    } else if (recur_data[0x04] == 0x0B) {
        fprintf(fptr, "WEEKLY;INTERVAL=%d;BYDAY=%s", 
            recur_data[0x0E], getRruleDayname(recur_data[0x16]));
        if (recur_data[0x1A] == 0x22 || recur_data[0x1A] == 0x21) {
            fprintf(fptr, ";COUNT=%d", 
                getRruleCount(recur_data[0x1F], recur_data[0x1E]));
        }
    } else if (recur_data[0x04] == 0x0C) {
        fprintf(fptr, "MONTHLY");
        if (recur_data[0x06] == 0x02) {
            fprintf(fptr, ";INTERVAL=%d;BYMONTHDAY=%d", recur_data[0x0E],
                recur_data[0x16]);
            if (recur_data[0x1A] == 0x22 || recur_data[0x1A] == 0x21) {
                fprintf(fptr, ";COUNT=%d", getRruleCount(recur_data[0x1F], 
                    recur_data[0x1E]));
            }
        } else if (recur_data[0x06] == 0x03) {
            fprintf(fptr, ";BYDAY=%s;BYSETPOS=%d;INTERVAL=%d", 
                getRruleDayname(recur_data[0x16]),
                recur_data[0x1A] == 0x05 ? -1 : recur_data[0x1A],
                recur_data[0x0E]);
            if (recur_data[0x1E] == 0x22 || recur_data[0x1E] == 0x21) {
                fprintf(fptr, ";COUNT=%d", getRruleCount(recur_data[0x23],
                    recur_data[0x22]));
            }
        }
    } else if (recur_data[0x04] == 0x0D) {
        fprintf(fptr, "YEARLY;BYMONTH=%d",
                getRruleMonthNum(recur_data[0x0A], recur_data[0x0B]));
        if (recur_data[0x06] == 0x02) {
            fprintf(fptr, ";BYMONTHDAY=%d", recur_data[0x16]);
        } else if (recur_data[0x06] == 0x03) {
            fprintf(fptr, ";BYDAY=%s;BYSETPOS=%d",
                getRruleDayname(recur_data[0x16]),
                recur_data[0x1A] == 0x05 ? -1 : recur_data[0x1A]);
        }
        if (recur_data[0x1E] == 0x22 || recur_data[0x1E] == 0x21) {
            fprintf(fptr, ";COUNT=%d", getRruleCount(recur_data[0x23],
                recur_data[0x22]));
        }
    }
    fprintf(fptr, "\n");
}

void saveVCalendar(TNEFStruct TNEF) {
    char ifilename[256];
    variableLength *filename;
    char *charptr, *charptr2;
    FILE *fptr;
    int index;
    DDWORD *ddword_ptr;
    DDWORD ddword_val;
    dtr thedate;

    if (filepath == NULL) {
        sprintf(ifilename, "calendar.vcf");
    } else {
        sprintf(ifilename, "%s/calendar.vcf", filepath);
    }
    printf("%s\n", ifilename);

    if ((fptr = fopen(ifilename, "wb"))==NULL) {
            printf("Error writing file to disk!");
    } else {
        fprintf(fptr, "BEGIN:VCALENDAR\n");
        if (TNEF.messageClass[0] != 0) {
            charptr2=TNEF.messageClass;
            charptr=charptr2;
            while (*charptr != 0) {
                if (*charptr == '.') {
                    charptr2 = charptr;
                }
                charptr++;
            }
            if (strcmp(charptr2, ".MtgCncl") == 0) {
                fprintf(fptr, "METHOD:CANCEL\n");
            } else {
                fprintf(fptr, "METHOD:REQUEST\n");
            }
        } else {
            fprintf(fptr, "METHOD:REQUEST\n");
        }
        fprintf(fptr, "VERSION:2.0\n");
        fprintf(fptr, "BEGIN:VEVENT\n");

	/* UID 
	   After alot of comparisons, I'm reasonably sure this is totally 
	   wrong.  But it's not really necessary. */
	
	/* I think it only exists to connect future modification entries to 
	   this entry. so as long as it's incorrectly interpreted the same way 
	   every time, it should be ok :) */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_BINARY, 0x3))) == MAPI_UNDEFINED) {
            if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                            PROP_TAG(PT_BINARY, 0x23))) == MAPI_UNDEFINED) {
                filename = NULL;
            }
        }
        if (filename!=NULL) {
            fprintf(fptr, "UID:");
            for(index=0;index<filename->size;index++) {
                fprintf(fptr,"%02X", (unsigned char)filename->data[index]);
            }
            fprintf(fptr,"\n");
        }

        /* Sequence */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_LONG, 0x8201))) != MAPI_UNDEFINED) {
            ddword_ptr = (DDWORD*)filename->data;
            fprintf(fptr, "SEQUENCE:%i\n", (int) *ddword_ptr);
        }
        if ((filename=MAPIFindProperty(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_BINARY, PR_SENDER_SEARCH_KEY))) 
                != MAPI_UNDEFINED) {
            charptr = filename->data;
            charptr2 = strstr(charptr, ":");
            if (charptr2 == NULL) 
                charptr2 = charptr;
            else
                charptr2++;
            fprintf(fptr, "ORGANIZER;CN=\"%s\":MAILTO:%s\n", 
                    charptr2, charptr2);
        }

        /* Required Attendees */
        if ((filename = MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_STRING8, 0x823b))) != MAPI_UNDEFINED) {
	    /* We have a list of required participants, so 
	       write them out. */
            if (filename->size > 1) {
                charptr = filename->data-1;
                charptr2=strstr(charptr+1, ";");
                while (charptr != NULL) {
                    charptr++;
                    charptr2 = strstr(charptr, ";");
                    if (charptr2 != NULL) {
                        *charptr2 = 0;
                    }
                    while (*charptr == ' ') 
                        charptr++;
                    fprintf(fptr, "ATTENDEE;PARTSTAT=NEEDS-ACTION;");
                    fprintf(fptr, "ROLE=REQ-PARTICIPANT;RSVP=TRUE;");
                    fprintf(fptr, "CN=\"%s\":MAILTO:%s\n",
                                charptr, charptr);
                    charptr = charptr2;
                }
            }
            /* Optional attendees */
            if ((filename = MAPIFindUserProp(&(TNEF.MapiProperties), 
                            PROP_TAG(PT_STRING8, 0x823c))) != MAPI_UNDEFINED) {
                    /* The list of optional participants */
                if (filename->size > 1) {
                    charptr = filename->data-1;
                    charptr2=strstr(charptr+1, ";");
                    while (charptr != NULL) {
                        charptr++;
                        charptr2 = strstr(charptr, ";");
                        if (charptr2 != NULL) {
                            *charptr2 = 0;
                        }
                        while (*charptr == ' ') 
                            charptr++;
                        fprintf(fptr, "ATTENDEE;PARTSTAT=NEEDS-ACTION;");
                        fprintf(fptr, "ROLE=OPT-PARTICIPANT;RSVP=TRUE;");
                        fprintf(fptr, "CN=\"%s\":MAILTO:%s\n",
                                charptr, charptr);
                        charptr = charptr2;
                    }
                }
            }
        } else if ((filename = MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_STRING8, 0x8238))) != MAPI_UNDEFINED) {
            if (filename->size > 1) {
                charptr = filename->data-1;
                charptr2=strstr(charptr+1, ";");
                while (charptr != NULL) {
                    charptr++;
                    charptr2 = strstr(charptr, ";");
                    if (charptr2 != NULL) {
                        *charptr2 = 0;
                    }
                    while (*charptr == ' ') 
                        charptr++;
                    fprintf(fptr, "ATTENDEE;PARTSTAT=NEEDS-ACTION;");
                    fprintf(fptr, "ROLE=REQ-PARTICIPANT;RSVP=TRUE;");
                    fprintf(fptr, "CN=\"%s\":MAILTO:%s\n",
                                charptr, charptr);
                    charptr = charptr2;
                }
            }

        }
        /* Summary */
        filename = NULL;
        if((filename=MAPIFindProperty(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_STRING8, PR_CONVERSATION_TOPIC))) 
                != MAPI_UNDEFINED) {
            fprintf(fptr, "SUMMARY:");
            cstylefprint(fptr, filename);
            fprintf(fptr, "\n");
        }

        /* Description */
        if ((filename=MAPIFindProperty(&(TNEF.MapiProperties),
                                PROP_TAG(PT_BINARY, PR_RTF_COMPRESSED)))
                != MAPI_UNDEFINED) {
            variableLength buf;
            if ((buf.data = DecompressRTF(filename, &(buf.size))) != NULL) {
                fprintf(fptr, "DESCRIPTION:");
                printRtf(fptr, &buf);
                free(buf.data);
            }
            
        } 

        /* Location */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_STRING8, 0x0002))) == MAPI_UNDEFINED) {
            if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                            PROP_TAG(PT_STRING8, 0x8208))) == MAPI_UNDEFINED) {
                filename = NULL;
            }
        }
        if (filename != NULL) {
            fprintf(fptr,"LOCATION: %s\n", filename->data);
        }
        /* Date Start */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_SYSTIME, 0x820d))) == MAPI_UNDEFINED) {
            if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                            PROP_TAG(PT_SYSTIME, 0x8516))) == MAPI_UNDEFINED) {
                filename=NULL;
            }
        }
        if (filename != NULL) {
            fprintf(fptr, "DTSTART:");
            MAPISysTimetoDTR(filename->data, &thedate);
            fprintf(fptr,"%04i%02i%02iT%02i%02i%02iZ\n", 
                    thedate.wYear, thedate.wMonth, thedate.wDay,
                    thedate.wHour, thedate.wMinute, thedate.wSecond);
        }
        /* Date End */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_SYSTIME, 0x820e))) == MAPI_UNDEFINED) {
            if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                            PROP_TAG(PT_SYSTIME, 0x8517))) == MAPI_UNDEFINED) {
                filename=NULL;
            }
        }
        if (filename != NULL) {
            fprintf(fptr, "DTEND:");
            MAPISysTimetoDTR(filename->data, &thedate);
            fprintf(fptr,"%04i%02i%02iT%02i%02i%02iZ\n", 
                    thedate.wYear, thedate.wMonth, thedate.wDay,
                    thedate.wHour, thedate.wMinute, thedate.wSecond);
        }
        /* Date Stamp */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_SYSTIME, 0x8202))) != MAPI_UNDEFINED) {
            fprintf(fptr, "CREATED:");
            MAPISysTimetoDTR(filename->data, &thedate);
            fprintf(fptr,"%04i%02i%02iT%02i%02i%02iZ\n", 
                    thedate.wYear, thedate.wMonth, thedate.wDay,
                    thedate.wHour, thedate.wMinute, thedate.wSecond);
        }
        /* Class */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_BOOLEAN, 0x8506))) != MAPI_UNDEFINED) {
            ddword_ptr = (DDWORD*)filename->data;
            ddword_val = SwapDDWord((BYTE*)ddword_ptr);
            fprintf(fptr, "CLASS:" );
            if (*ddword_ptr == 1) {
                fprintf(fptr,"PRIVATE\n");
            } else {
                fprintf(fptr,"PUBLIC\n");
            }
        }
        /* Recurrence */
        filename = NULL;
        if ((filename=MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_BINARY, 0x8216))) != MAPI_UNDEFINED) {
            PrintRrule(fptr, filename->data, filename->size, TNEF);
        }

        /* Wrap it up */
        fprintf(fptr, "END:VEVENT\n");
        fprintf(fptr, "END:VCALENDAR\n");
        fclose(fptr);
    }
}

void saveVTask(TNEFStruct tnef) {
    variableLength *vl;
    variableLength *filename;
    int index,i;
    char ifilename[256];
    char *charptr, *charptr2;
    dtr thedate;
    FILE *fptr;
    DDWORD *ddword_ptr;
    DDWORD ddword_val;
    
    vl = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_CONVERSATION_TOPIC));

    if (vl == MAPI_UNDEFINED) {
        return;
    }

    index = strlen(vl->data);
    while (vl->data[index] == ' ') 
            vl->data[index--] = 0;

    if (filepath == NULL) {
        sprintf(ifilename, "%s.vcf", vl->data);
    } else {
        sprintf(ifilename, "%s/%s.vcf", filepath, vl->data);
    }
    for(i=0; i<strlen(ifilename); i++) 
        if (ifilename[i] == ' ') 
            ifilename[i] = '_';
    printf("%s\n", ifilename);

    if ((fptr = fopen(ifilename, "wb"))==NULL) {
            printf("Error writing file to disk!");
    } else {
        fprintf(fptr, "BEGIN:VCALENDAR\n");
        fprintf(fptr, "VERSION:2.0\n");
        fprintf(fptr, "METHOD:PUBLISH\n");
        filename = NULL;

        fprintf(fptr, "BEGIN:VTODO\n");
        if (tnef.messageID[0] != 0) {
            fprintf(fptr,"UID:%s\n", tnef.messageID);
        }
        filename = MAPIFindUserProp(&(tnef.MapiProperties), \
                        PROP_TAG(PT_STRING8, 0x8122));
        if (filename != MAPI_UNDEFINED) {
            fprintf(fptr, "ORGANIZER:%s\n", filename->data);
        }
                 

        if ((filename = MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, PR_DISPLAY_TO))) != MAPI_UNDEFINED) {
            filename = MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(PT_STRING8, 0x811f));
        }
        if ((filename != MAPI_UNDEFINED) && (filename->size > 1)) {
            charptr = filename->data-1;
            charptr2=strstr(charptr+1, ";");
            while (charptr != NULL) {
                charptr++;
                charptr2 = strstr(charptr, ";");
                if (charptr2 != NULL) {
                    *charptr2 = 0;
                }
                while (*charptr == ' ') 
                    charptr++;
                fprintf(fptr, "ATTENDEE;CN=%s;ROLE=REQ-PARTICIPANT:%s\n", charptr, charptr);
                charptr = charptr2;
            }
        }

        if (tnef.subject.size > 0) {
            fprintf(fptr,"SUMMARY:");
            cstylefprint(fptr,&(tnef.subject));
            fprintf(fptr,"\n");
        }

        if (tnef.body.size > 0) {
            fprintf(fptr,"DESCRIPTION:");
            cstylefprint(fptr,&(tnef.body));
            fprintf(fptr,"\n");
        }

        filename = MAPIFindProperty(&(tnef.MapiProperties), \
                    PROP_TAG(PT_SYSTIME, PR_CREATION_TIME));
        if (filename != MAPI_UNDEFINED) {
            fprintf(fptr, "DTSTAMP:");
            MAPISysTimetoDTR(filename->data, &thedate);
            fprintf(fptr,"%04i%02i%02iT%02i%02i%02iZ\n", 
                    thedate.wYear, thedate.wMonth, thedate.wDay,
                    thedate.wHour, thedate.wMinute, thedate.wSecond);
        }

        filename = MAPIFindUserProp(&(tnef.MapiProperties), \
                    PROP_TAG(PT_SYSTIME, 0x8517));
        if (filename != MAPI_UNDEFINED) {
            fprintf(fptr, "DUE:");
            MAPISysTimetoDTR(filename->data, &thedate);
            fprintf(fptr,"%04i%02i%02iT%02i%02i%02iZ\n", 
                    thedate.wYear, thedate.wMonth, thedate.wDay,
                    thedate.wHour, thedate.wMinute, thedate.wSecond);
        }
        filename = MAPIFindProperty(&(tnef.MapiProperties), \
                    PROP_TAG(PT_SYSTIME, PR_LAST_MODIFICATION_TIME));
        if (filename != MAPI_UNDEFINED) {
            fprintf(fptr, "LAST-MODIFIED:");
            MAPISysTimetoDTR(filename->data, &thedate);
            fprintf(fptr,"%04i%02i%02iT%02i%02i%02iZ\n", 
                    thedate.wYear, thedate.wMonth, thedate.wDay,
                    thedate.wHour, thedate.wMinute, thedate.wSecond);
        }
        /* Class */
        filename = MAPIFindUserProp(&(tnef.MapiProperties), \
                        PROP_TAG(PT_BOOLEAN, 0x8506));
        if (filename != MAPI_UNDEFINED) {
            ddword_ptr = (DDWORD*)filename->data;
            ddword_val = SwapDDWord((BYTE*)ddword_ptr);
            fprintf(fptr, "CLASS:" );
            if (*ddword_ptr == 1) {
                fprintf(fptr,"PRIVATE\n");
            } else {
                fprintf(fptr,"PUBLIC\n");
            }
        }
        fprintf(fptr, "END:VTODO\n");
        fprintf(fptr, "END:VCALENDAR\n");
        fclose(fptr);
    }

}

void fprintProperty(TNEFStruct tnef, FILE *fptr, DWORD proptype, DWORD propid, char text[]) {
    variableLength *vl;
    if ((vl=MAPIFindProperty(&(tnef.MapiProperties), PROP_TAG(proptype, propid))) != MAPI_UNDEFINED) { 
        if (vl->size > 0)  
            if ((vl->size == 1) && (vl->data[0] == 0)) {
            } else { 
                fprintf(fptr, text, vl->data); 
            } 
    }
}

void fprintUserProp(TNEFStruct tnef, FILE *fptr, DWORD proptype, DWORD propid, char text[]) {
    variableLength *vl;
    if ((vl=MAPIFindUserProp(&(tnef.MapiProperties), PROP_TAG(proptype, propid))) != MAPI_UNDEFINED) { 
        if (vl->size > 0)  
            if ((vl->size == 1) && (vl->data[0] == 0)) {
            } else { 
                fprintf(fptr, text, vl->data); 
            } 
    }
}

void quotedfprint(FILE *fptr, variableLength *vl) {
    int index;

    for (index=0;index<vl->size-1; index++) { 
        if (vl->data[index] == '\n') { 
            fprintf(fptr, "=0A"); 
        } else if (vl->data[index] == '\r') { 
        } else { 
            fprintf(fptr, "%c", vl->data[index]); 
        } 
    }
}

void cstylefprint(FILE *fptr, variableLength *vl) {
    int index;

    for (index=0;index<vl->size-1; index++) { 
        if (vl->data[index] == '\n') { 
            fprintf(fptr, "\\n"); 
        } else if (vl->data[index] == '\r') { 
            /* Print nothing. */
        } else if (vl->data[index] == ';') {
            fprintf(fptr, "\\;");
        } else if (vl->data[index] == ',') {
            fprintf(fptr, "\\,");
        } else if (vl->data[index] == '\\') {
            fprintf(fptr, "\\");
        } else { 
            fprintf(fptr, "%c", vl->data[index]); 
        } 
    }
}

void printRtf(FILE *fptr, variableLength *vl) {
    int index;
    char *byte;
    int brace_ct;
    int key;

    key = 0;
    brace_ct = 0;

    for(index = 0, byte=vl->data; index < vl->size; index++, byte++) {
        if (*byte == '}') {
            brace_ct--;
            key = 0;
            continue;
        }
        if (*byte == '{') {
            brace_ct++;
            continue;
        }
        if (*byte == '\\') {
            key = 1;
        }
        if (isspace(*byte)) {
            key = 0;
        }
        if ((brace_ct == 1) && (key == 0)) {
            if (*byte == '\n') { 
                fprintf(fptr, "\\n"); 
            } else if (*byte == '\r') { 
		/* Print nothing. */
            } else if (*byte == ';') {
                fprintf(fptr, "\\;");
            } else if (*byte == ',') {
                fprintf(fptr, "\\,");
            } else if (*byte == '\\') {
                fprintf(fptr, "\\");
            } else { 
                fprintf(fptr, "%c", *byte);
            } 
        }
    }
    fprintf(fptr, "\n");
}

