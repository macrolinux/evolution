/*
 * e-mail-config-remote-accounts.c
 *
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
 */

#include <config.h>
#include <glib/gi18n-lib.h>

#include <camel/camel.h>
#include <libebackend/e-extension.h>
#include <libedataserver/e-source-backend.h>
#include <libedataserver/e-data-server-util.h>

#include <misc/e-port-entry.h>

#include <mail/e-mail-config-auth-check.h>
#include <mail/e-mail-config-service-page.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_REMOTE_BACKEND \
	(e_mail_config_remote_backend_get_type ())
#define E_MAIL_CONFIG_REMOTE_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_REMOTE_BACKEND, EMailConfigRemoteBackend))
#define E_MAIL_CONFIG_REMOTE_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_REMOTE_BACKEND, EMailConfigRemoteBackendClass))
#define E_IS_MAIL_CONFIG_REMOTE_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_REMOTE_BACKEND))
#define E_IS_MAIL_CONFIG_REMOTE_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_REMOTE_BACKEND))
#define E_MAIL_CONFIG_REMOTE_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_REMOTE_BACKEND, EMailConfigRemoteBackendClass))

typedef struct _EMailConfigRemoteBackend EMailConfigRemoteBackend;
typedef struct _EMailConfigRemoteBackendClass EMailConfigRemoteBackendClass;

typedef EMailConfigRemoteBackend EMailConfigPopBackend;
typedef EMailConfigRemoteBackendClass EMailConfigPopBackendClass;

typedef EMailConfigRemoteBackend EMailConfigNntpBackend;
typedef EMailConfigRemoteBackendClass EMailConfigNntpBackendClass;

typedef EMailConfigRemoteBackend EMailConfigImapBackend;
typedef EMailConfigRemoteBackendClass EMailConfigImapBackendClass;

typedef EMailConfigRemoteBackend EMailConfigImapxBackend;
typedef EMailConfigRemoteBackendClass EMailConfigImapxBackendClass;

struct _EMailConfigRemoteBackend {
	EMailConfigServiceBackend parent;

	GtkWidget *host_entry;		/* not referenced */
	GtkWidget *port_entry;		/* not referenced */
	GtkWidget *user_entry;		/* not referenced */
	GtkWidget *security_combo_box;	/* not referenced */
	GtkWidget *auth_check;		/* not referenced */
};

struct _EMailConfigRemoteBackendClass {
	EMailConfigServiceBackendClass parent_class;
};

/* Forward Declarations */
void		e_mail_config_remote_accounts_register_types
						(GTypeModule *type_module);
GType		e_mail_config_remote_backend_get_type
						(void) G_GNUC_CONST;
GType		e_mail_config_pop_backend_get_type
						(void) G_GNUC_CONST;
GType		e_mail_config_nntp_backend_get_type
						(void) G_GNUC_CONST;
GType		e_mail_config_imap_backend_get_type
						(void) G_GNUC_CONST;
GType		e_mail_config_imapx_backend_get_type
						(void) G_GNUC_CONST;

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailConfigRemoteBackend,
	e_mail_config_remote_backend,
	E_TYPE_MAIL_CONFIG_SERVICE_BACKEND,
	G_TYPE_FLAG_ABSTRACT,
	/* no custom code */)

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigPopBackend,
	e_mail_config_pop_backend,
	E_TYPE_MAIL_CONFIG_REMOTE_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigNntpBackend,
	e_mail_config_nntp_backend,
	E_TYPE_MAIL_CONFIG_REMOTE_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigImapBackend,
	e_mail_config_imap_backend,
	E_TYPE_MAIL_CONFIG_REMOTE_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigImapxBackend,
	e_mail_config_imapx_backend,
	E_TYPE_MAIL_CONFIG_REMOTE_BACKEND)

static void
mail_config_remote_backend_insert_widgets (EMailConfigServiceBackend *backend,
                                           GtkBox *parent)
{
	EMailConfigRemoteBackend *remote_backend;
	CamelProvider *provider;
	CamelSettings *settings;
	ESource *source;
	ESourceBackend *extension;
	EMailConfigServicePage *page;
	EMailConfigServicePageClass *class;
	GtkLabel *label;
	GtkWidget *widget;
	GtkWidget *container;
	const gchar *backend_name;
	const gchar *extension_name;
	const gchar *text;
	gchar *markup;

	remote_backend = E_MAIL_CONFIG_REMOTE_BACKEND (backend);

	page = e_mail_config_service_backend_get_page (backend);
	source = e_mail_config_service_backend_get_source (backend);
	settings = e_mail_config_service_backend_get_settings (backend);

	class = E_MAIL_CONFIG_SERVICE_PAGE_GET_CLASS (page);
	extension_name = class->extension_name;
	extension = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_get_backend_name (extension);

	text = _("Configuration");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_label_new_with_mnemonic (_("_Server:"));
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 0, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 0, 1, 1);
	remote_backend->host_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_label_new_with_mnemonic (_("_Port:"));
	gtk_grid_attach (GTK_GRID (container), widget, 2, 0, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = e_port_entry_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (container), widget, 3, 0, 1, 1);
	remote_backend->port_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_label_new_with_mnemonic (_("User_name:"));
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 1, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 1, 3, 1);
	remote_backend->user_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	text = _("Security");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_widget_set_margin_top (widget, 6);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_label_new_with_mnemonic (_("Encryption _method:"));
	gtk_grid_attach (GTK_GRID (container), widget, 0, 0, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	/* The IDs correspond to the CamelNetworkSecurityMethod enum. */
	widget = gtk_combo_box_text_new ();
	gtk_combo_box_text_append (
		GTK_COMBO_BOX_TEXT (widget),
		"none",
		_("No encryption"));
	gtk_combo_box_text_append (
		GTK_COMBO_BOX_TEXT (widget),
		"starttls-on-standard-port",
		_("STARTTLS after connecting"));
	gtk_combo_box_text_append (
		GTK_COMBO_BOX_TEXT (widget),
		"ssl-on-alternate-port",
		_("SSL on a dedicated port"));
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 0, 1, 1);
	remote_backend->security_combo_box = widget;  /* do not reference */
	gtk_widget_show (widget);

	provider = camel_provider_get (backend_name, NULL);
	if (provider != NULL && provider->port_entries != NULL)
		e_port_entry_set_camel_entries (
			E_PORT_ENTRY (remote_backend->port_entry),
			provider->port_entries);

	text = _("Authentication");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_widget_set_margin_top (widget, 6);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = e_mail_config_auth_check_new (backend);
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	remote_backend->auth_check = widget;  /* do not reference */
	gtk_widget_show (widget);

	g_object_bind_property (
		settings, "host",
		remote_backend->host_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	g_object_bind_property_full (
		settings, "security-method",
		remote_backend->security_combo_box, "active-id",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		e_binding_transform_enum_value_to_nick,
		e_binding_transform_enum_nick_to_value,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property (
		settings, "port",
		remote_backend->port_entry, "port",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		settings, "security-method",
		remote_backend->port_entry, "security-method",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		settings, "user",
		remote_backend->user_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	/* Don't use G_BINDING_SYNC_CREATE here since the widget
	 * chooses its initial mechanism more intelligently than
	 * a simple property binding would. */
	g_object_bind_property (
		settings, "auth-mechanism",
		remote_backend->auth_check, "active-mechanism",
		G_BINDING_BIDIRECTIONAL);
}

static gboolean
mail_config_remote_backend_check_complete (EMailConfigServiceBackend *backend)
{
	EMailConfigRemoteBackend *remote_backend;
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	EPortEntry *port_entry;
	const gchar *host;
	const gchar *user;

	remote_backend = E_MAIL_CONFIG_REMOTE_BACKEND (backend);

	settings = e_mail_config_service_backend_get_settings (backend);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_get_host (network_settings);
	user = camel_network_settings_get_user (network_settings);

	if (host == NULL || *host == '\0')
		return FALSE;

	port_entry = E_PORT_ENTRY (remote_backend->port_entry);
	if (!e_port_entry_is_valid (port_entry))
		return FALSE;

	if (user == NULL || *user == '\0')
		return FALSE;

	return TRUE;
}

static void
mail_config_remote_backend_commit_changes (EMailConfigServiceBackend *backend)
{
	/* All CamelNetworkSettings properties are already up-to-date,
	 * and these properties are bound to ESourceExtension properties,
	 * so nothing to do here. */
}

static void
e_mail_config_remote_backend_class_init (EMailConfigRemoteBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->insert_widgets = mail_config_remote_backend_insert_widgets;
	backend_class->check_complete = mail_config_remote_backend_check_complete;
	backend_class->commit_changes = mail_config_remote_backend_commit_changes;
}

static void
e_mail_config_remote_backend_class_finalize (EMailConfigRemoteBackendClass *class)
{
}

static void
e_mail_config_remote_backend_init (EMailConfigRemoteBackend *backend)
{
}

static gboolean
mail_config_pop_backend_auto_configure (EMailConfigServiceBackend *backend,
                                        EMailAutoconfig *autoconfig)
{
	ESource *source;

	source = e_mail_config_service_backend_get_source (backend);

	return e_mail_autoconfig_set_pop3_details (autoconfig, source);
}

static void
e_mail_config_pop_backend_class_init (EMailConfigRemoteBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "pop";
	backend_class->auto_configure = mail_config_pop_backend_auto_configure;
}

static void
e_mail_config_pop_backend_class_finalize (EMailConfigRemoteBackendClass *class)
{
}

static void
e_mail_config_pop_backend_init (EMailConfigRemoteBackend *backend)
{
}

static void
e_mail_config_nntp_backend_class_init (EMailConfigRemoteBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "nntp";
}

static void
e_mail_config_nntp_backend_class_finalize (EMailConfigRemoteBackendClass *class)
{
}

static void
e_mail_config_nntp_backend_init (EMailConfigRemoteBackend *backend)
{
}

static gboolean
mail_config_imap_backend_auto_configure (EMailConfigServiceBackend *backend,
                                         EMailAutoconfig *autoconfig)
{
	ESource *source;

	source = e_mail_config_service_backend_get_source (backend);

	return e_mail_autoconfig_set_imap_details (autoconfig, source);
}

static void
e_mail_config_imap_backend_class_init (EMailConfigRemoteBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "imap";
	backend_class->auto_configure = mail_config_imap_backend_auto_configure;
}

static void
e_mail_config_imap_backend_class_finalize (EMailConfigRemoteBackendClass *class)
{
}

static void
e_mail_config_imap_backend_init (EMailConfigRemoteBackend *backend)
{
}

static gboolean
mail_config_imapx_backend_auto_configure (EMailConfigServiceBackend *backend,
                                          EMailAutoconfig *autoconfig)
{
	ESource *source;

	source = e_mail_config_service_backend_get_source (backend);

	return e_mail_autoconfig_set_imap_details (autoconfig, source);
}

static void
e_mail_config_imapx_backend_class_init (EMailConfigRemoteBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "imapx";
	backend_class->auto_configure = mail_config_imapx_backend_auto_configure;
}

static void
e_mail_config_imapx_backend_class_finalize (EMailConfigRemoteBackendClass *class)
{
}

static void
e_mail_config_imapx_backend_init (EMailConfigRemoteBackend *backend)
{
}

void
e_mail_config_remote_accounts_register_types (GTypeModule *type_module)
{
	/* Abstract base type */
	e_mail_config_remote_backend_register_type (type_module);

	/* Concrete sub-types */
	e_mail_config_pop_backend_register_type (type_module);
	e_mail_config_nntp_backend_register_type (type_module);
	e_mail_config_imap_backend_register_type (type_module);
	e_mail_config_imapx_backend_register_type (type_module);
}

