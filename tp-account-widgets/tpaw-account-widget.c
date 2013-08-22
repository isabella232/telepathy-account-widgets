/*
 * Copyright (C) 2006-2007 Imendio AB
 * Copyright (C) 2007-2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *          Jonathan Tellier <jonathan.tellier@gmail.com>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include "config.h"
#include "tpaw-account-widget.h"
#include "tpaw-account-widget-private.h"

#include <glib/gi18n-lib.h>
#include <dbus/dbus-protocol.h>

#include "tpaw-account-widget-irc.h"
#include "tpaw-account-widget-private.h"
#include "tpaw-account-widget-sip.h"
#include "tpaw-builder.h"
#include "tpaw-utils.h"

#define DEBUG_FLAG TPAW_DEBUG_ACCOUNT
#include "tpaw-debug.h"

G_DEFINE_TYPE (TpawAccountWidget, tpaw_account_widget, GTK_TYPE_BOX)

typedef enum
{
  NO_SERVICE = 0,
  GTALK_SERVICE,
  FACEBOOK_SERVICE,
  N_SERVICES
} Service;

typedef struct
{
  const gchar *label_username_example;
  gboolean show_advanced;
} ServiceInfo;

static ServiceInfo services_infos[N_SERVICES] = {
    { "label_username_example", TRUE },
    { "label_username_g_example", FALSE },
    { "label_username_f_example", FALSE },
};

struct _TpawAccountWidgetPriv {
  TpawAccountSettings *settings;

  GtkWidget *grid_common_settings;
  GtkWidget *apply_button;
  GtkWidget *cancel_button;
  GtkWidget *entry_password;
  GtkWidget *spinbutton_port;
  GtkWidget *action_area;

  gboolean simple;

  gboolean contains_pending_changes;

  /* Whether the action area was provided or it's an internal one we
   * created ourselves */
  gboolean external_action_area;

  /* An TpawAccountWidget can be used to either create an account or
   * modify it. When we are creating an account, this member is set to TRUE */
  gboolean creating_account;

  /* whether there are any other real accounts. Necessary so we know whether
   * it's safe to dismiss this widget in some cases (eg, whether the Cancel
   * button should be sensitive) */
  gboolean other_accounts_exist;

  /* if TRUE, the GTK+ destroy signal has been fired and so the widgets
   * embedded in this account widget can't be used any more
   * workaround because some async callbacks can be called after the
   * widget has been destroyed */
  gboolean destroyed;

  TpAccountManager *account_manager;

  GtkWidget *param_account_widget;
  GtkWidget *param_password_widget;

  gboolean automatic_change;
  GtkWidget *remember_password_widget;

  /* Used only for IRC accounts */
  TpawIrcNetworkChooser *irc_network_chooser;

  /* Used for 'special' XMPP account having a service associated ensuring that
   * JIDs have a specific suffix; such as Facebook for example */
  gchar *jid_suffix;
};

enum {
  PROP_PROTOCOL = 1,
  PROP_SETTINGS,
  PROP_SIMPLE,
  PROP_CREATING_ACCOUNT,
  PROP_OTHER_ACCOUNTS_EXIST,
  PROP_ACTION_AREA,
};

enum {
  HANDLE_APPLY,
  ACCOUNT_CREATED,
  CANCELLED,
  CLOSE,
  LAST_SIGNAL
};

enum {
  RESPONSE_LAUNCH
};

static guint signals[LAST_SIGNAL] = { 0 };

#define CHANGED_TIMEOUT 300

#define DIGIT             "0-9"
#define DIGITS            "(["DIGIT"]+)"
#define ALPHA             "a-zA-Z"
#define ALPHAS            "(["ALPHA"]+)"
#define ALPHADIGIT        ALPHA DIGIT
#define ALPHADIGITS       "(["ALPHADIGIT"]+)"
#define ALPHADIGITDASH    ALPHA DIGIT "-"
#define ALPHADIGITDASHS   "(["ALPHADIGITDASH"]*)"

#define HOSTNUMBER        "("DIGITS"\\."DIGITS"\\."DIGITS"\\."DIGITS")"
#define TOPLABEL          "("ALPHAS \
                            "| (["ALPHA"]"ALPHADIGITDASHS "["ALPHADIGIT"]))"
#define DOMAINLABEL       "("ALPHADIGITS"|(["ALPHADIGIT"]" ALPHADIGITDASHS \
                                       "["ALPHADIGIT"]))"
#define HOSTNAME          "((" DOMAINLABEL "\\.)+" TOPLABEL ")"
/* Based on http://www.ietf.org/rfc/rfc1738.txt (section 5) */
#define HOST              "("HOSTNAME "|" HOSTNUMBER")"
/* Based on http://www.ietf.org/rfc/rfc0822.txt (appendix D) */
#define EMAIL_LOCALPART   "([^\\(\\)<>@,;:\\\\\"\\[\\]\\s]+)"

/* UIN is digital according to the unofficial specification:
 * http://iserverd.khstu.ru/docum_ext/icqv5.html#CTS
 * 5 digits minimum according to http://en.wikipedia.org/wiki/ICQ#UIN
 * According to an user, we can also provide an email address instead of the
 * ICQ UIN. */
#define ICQ_USER_NAME     "((["DIGIT"]{5,})|"EMAIL_LOCALPART"@"HOST")"

/* Based on http://www.ietf.org/rfc/rfc2812.txt (section 2.3.1) */
#define IRC_SPECIAL       "_\\[\\]{}\\\\|`^"
#define IRC_NICK_NAME     "(["ALPHA IRC_SPECIAL"]["ALPHADIGITDASH IRC_SPECIAL"]*)"

/* Based on http://www.ietf.org/rfc/rfc4622.txt (section 2.2)
 * We just exclude invalid characters to avoid ucschars and other redundant
 * complexity */
#define JABBER_USER_NAME  "([^@:'\"<>&\\s]+)"
/* ID is an email according to the unofficial specification:
 * http://www.hypothetic.org/docs/msn/general/names.php */
#define MSN_USER_NAME     EMAIL_LOCALPART
/* Based on the official help:
 * http://help.yahoo.com/l/us/yahoo/edit/registration/edit-01.html
 * Looks like an email address can be used as well (bgo #655959)
 * */
#define YAHOO_USER_NAME   "(["ALPHA"]["ALPHADIGIT"_\\.]{3,31})|("EMAIL_LOCALPART"@"HOST")"

#define ACCOUNT_REGEX_ICQ      "^"ICQ_USER_NAME"$"
#define ACCOUNT_REGEX_IRC      "^"IRC_NICK_NAME"$"
#define ACCOUNT_REGEX_JABBER   "^"JABBER_USER_NAME"@[^@/]+"
#define ACCOUNT_REGEX_MSN      "^"MSN_USER_NAME"@"HOST"$"
#define ACCOUNT_REGEX_YAHOO    "^"YAHOO_USER_NAME"$"

static void
account_widget_set_control_buttons_sensitivity (TpawAccountWidget *self,
    gboolean sensitive)
{
  /* we hit this case because of the 'other-accounts-exist' property handler
   * being called during init (before constructed()) */
  if (self->priv->apply_button == NULL || self->priv->cancel_button == NULL)
    return;

  gtk_widget_set_sensitive (self->priv->apply_button, sensitive);

  if (sensitive)
    {
      /* We can't grab default if the widget hasn't be packed in a
       * window */
      GtkWidget *window;

      window = gtk_widget_get_toplevel (self->priv->apply_button);
      if (window != NULL &&
          gtk_widget_is_toplevel (window))
        {
          gtk_widget_set_can_default (self->priv->apply_button, TRUE);
          gtk_widget_grab_default (self->priv->apply_button);
        }
    }
}

static void
account_widget_set_entry_highlighting (GtkEntry *entry,
    gboolean highlight)
{
  g_return_if_fail (GTK_IS_ENTRY (entry));

  if (highlight)
    {
      GtkStyleContext *style;
      GdkRGBA color;

      style = gtk_widget_get_style_context (GTK_WIDGET (entry));
      gtk_style_context_get_background_color (style, GTK_STATE_FLAG_SELECTED,
          &color);

      /* Here we take the current theme colour and add it to
       * the colour for white and average the two. This
       * gives a colour which is inline with the theme but
       * slightly whiter.
       */
      tpaw_make_color_whiter (&color);

      gtk_widget_override_background_color (GTK_WIDGET (entry), 0, &color);
    }
  else
    {
      gtk_widget_override_background_color (GTK_WIDGET (entry), 0, NULL);
    }
}

static void
account_widget_handle_control_buttons_sensitivity (TpawAccountWidget *self)
{
  gboolean is_valid;

  is_valid = tpaw_account_settings_is_valid (self->priv->settings);

  account_widget_set_control_buttons_sensitivity (self, is_valid);

  g_signal_emit (self, signals[HANDLE_APPLY], 0, is_valid);
}

static void
account_widget_entry_changed_common (TpawAccountWidget *self,
    GtkEntry *entry, gboolean focus)
{
  const gchar *str;
  const gchar *param_name;
  gboolean prev_status;
  gboolean curr_status;

  str = gtk_entry_get_text (entry);
  param_name = g_object_get_data (G_OBJECT (entry), "param_name");
  prev_status = tpaw_account_settings_parameter_is_valid (
      self->priv->settings, param_name);

  if (TPAW_STR_EMPTY (str))
    {
      tpaw_account_settings_unset (self->priv->settings, param_name);

      if (focus)
        {
          gchar *value;

          value = tpaw_account_settings_dup_string (self->priv->settings,
              param_name);

          DEBUG ("Unset %s and restore to %s", param_name, value);
          gtk_entry_set_text (entry, value ? value : "");
          g_free (value);
        }
    }
  else
    {
      DEBUG ("Setting %s to %s", param_name,
          tp_strdiff (param_name, "password") ? str : "***");
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_string (str));
    }

  curr_status = tpaw_account_settings_parameter_is_valid (
      self->priv->settings, param_name);

  if (curr_status != prev_status)
    account_widget_set_entry_highlighting (entry, !curr_status);
}

static void
account_widget_entry_changed_cb (GtkEditable *entry,
    TpawAccountWidget *self)
{
  if (self->priv->automatic_change)
    return;

  account_widget_entry_changed_common (self, GTK_ENTRY (entry), FALSE);
  tpaw_account_widget_changed (self);
}

static void
account_widget_entry_map_cb (GtkEntry *entry,
    TpawAccountWidget *self)
{
  const gchar *param_name;
  gboolean is_valid;

  /* need to initialize input highlighting */
  param_name = g_object_get_data (G_OBJECT (entry), "param_name");
  is_valid = tpaw_account_settings_parameter_is_valid (self->priv->settings,
      param_name);
  account_widget_set_entry_highlighting (entry, !is_valid);
}

static void
account_widget_int_changed_cb (GtkWidget *widget,
    TpawAccountWidget *self)
{
  const gchar *param_name;
  gint value;
  const gchar *signature;

  value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
  param_name = g_object_get_data (G_OBJECT (widget), "param_name");

  signature = tpaw_account_settings_get_dbus_signature (self->priv->settings,
    param_name);
  g_return_if_fail (signature != NULL);

  DEBUG ("Setting %s to %d", param_name, value);

  switch ((int)*signature)
    {
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_int32 (value));
      break;
    case DBUS_TYPE_INT64:
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_int64 (value));
      break;
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_uint32 (value));
      break;
    case DBUS_TYPE_UINT64:
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_uint64 (value));
      break;
    default:
      g_return_if_reached ();
    }

  tpaw_account_widget_changed (self);
}

static void
account_widget_checkbutton_toggled_cb (GtkWidget *widget,
    TpawAccountWidget *self)
{
  gboolean     value;
  gboolean     default_value;
  const gchar *param_name;

  value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
  param_name = g_object_get_data (G_OBJECT (widget), "param_name");

  /* FIXME: This is ugly! checkbox don't have a "not-set" value so we
   * always unset the param and set the value if different from the
   * default value. */
  tpaw_account_settings_unset (self->priv->settings, param_name);
  default_value = tpaw_account_settings_get_boolean (self->priv->settings,
      param_name);

  if (default_value == value)
    {
      DEBUG ("Unset %s and restore to %d", param_name, default_value);
    }
  else
    {
      DEBUG ("Setting %s to %d", param_name, value);
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_boolean (value));
    }

  tpaw_account_widget_changed (self);
}

static void
account_widget_jabber_ssl_toggled_cb (GtkWidget *checkbutton_ssl,
    TpawAccountWidget *self)
{
  gboolean   value;
  gint32       port = 0;

  value = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton_ssl));
  port = tpaw_account_settings_get_uint32 (self->priv->settings, "port");

  if (value)
    {
      if (port == 5222 || port == 0)
        port = 5223;
    }
  else
    {
      if (port == 5223 || port == 0)
        port = 5222;
    }

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->priv->spinbutton_port),
      port);

  self->priv->contains_pending_changes = TRUE;
}

static void
account_widget_combobox_changed_cb (GtkWidget *widget,
    TpawAccountWidget *self)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  const gchar *value;
  GVariant *v;
  const gchar *default_value = NULL;
  const gchar *param_name;

  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
    return;

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
  /* the param value is stored in the first column */
  gtk_tree_model_get (model, &iter, 0, &value, -1);

  param_name = g_object_get_data (G_OBJECT (widget), "param_name");

  v = tpaw_account_settings_dup_default (self->priv->settings, param_name);
  if (v != NULL && g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
    default_value = g_variant_get_string (v, NULL);

  if (!tp_strdiff (value, default_value))
    {
      DEBUG ("Unset %s and restore to %s", param_name, default_value);
      tpaw_account_settings_unset (self->priv->settings, param_name);
    }
  else
    {
      DEBUG ("Setting %s to %s", param_name, value);
      tpaw_account_settings_set (self->priv->settings, param_name,
          g_variant_new_string (value));
    }

  tpaw_account_widget_changed (self);

  tp_clear_pointer (&v, g_variant_unref);
}

static void
clear_icon_released_cb (GtkEntry *entry,
    GtkEntryIconPosition icon_pos,
    GdkEvent *event,
    TpawAccountWidget *self)
{
  const gchar *param_name;

  param_name = g_object_get_data (G_OBJECT (entry), "param_name");

  DEBUG ("Unset %s", param_name);
  tpaw_account_settings_unset (self->priv->settings, param_name);
  gtk_entry_set_text (entry, "");

  tpaw_account_widget_changed (self);
}

static void
password_entry_changed_cb (GtkEditable *entry,
    TpawAccountWidget *self)
{
  const gchar *str;

  str = gtk_entry_get_text (GTK_ENTRY (entry));

  gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
      GTK_ENTRY_ICON_SECONDARY, !TPAW_STR_EMPTY (str));
}

static void
password_entry_activated_cb (GtkEntry *entry,
    TpawAccountWidget *self)
{
    if (gtk_widget_get_sensitive (self->priv->apply_button))
        tpaw_account_widget_apply_and_log_in (self);
}

static void
account_entry_activated_cb (GtkEntry *entry,
    TpawAccountWidget *self)
{
    if (gtk_widget_get_sensitive (self->priv->apply_button))
        tpaw_account_widget_apply_and_log_in (self);
}

void
tpaw_account_widget_setup_widget (TpawAccountWidget *self,
    GtkWidget *widget,
    const gchar *param_name)
{
  g_object_set_data_full (G_OBJECT (widget), "param_name",
      g_strdup (param_name), g_free);

  if (GTK_IS_SPIN_BUTTON (widget))
    {
      gint value = 0;
      const gchar *signature;

      signature = tpaw_account_settings_get_dbus_signature (
          self->priv->settings, param_name);
      g_return_if_fail (signature != NULL);

      switch ((int)*signature)
        {
          case DBUS_TYPE_INT16:
          case DBUS_TYPE_INT32:
            value = tpaw_account_settings_get_int32 (self->priv->settings,
              param_name);
            break;
          case DBUS_TYPE_INT64:
            value = tpaw_account_settings_get_int64 (self->priv->settings,
              param_name);
            break;
          case DBUS_TYPE_UINT16:
          case DBUS_TYPE_UINT32:
            value = tpaw_account_settings_get_uint32 (self->priv->settings,
              param_name);
            break;
          case DBUS_TYPE_UINT64:
            value = tpaw_account_settings_get_uint64 (self->priv->settings,
                param_name);
            break;
          default:
            g_return_if_reached ();
        }

      gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value);

      g_signal_connect (widget, "value-changed",
          G_CALLBACK (account_widget_int_changed_cb),
          self);
    }
  else if (GTK_IS_ENTRY (widget))
    {
      gchar *str;

      str = tpaw_account_settings_dup_string (self->priv->settings,
          param_name);
      gtk_entry_set_text (GTK_ENTRY (widget), str ? str : "");

      if (!tp_strdiff (param_name, "account"))
        self->priv->param_account_widget = widget;
      else if (!tp_strdiff (param_name, "password"))
        self->priv->param_password_widget = widget;

      if (strstr (param_name, "password"))
        {
          gtk_entry_set_visibility (GTK_ENTRY (widget), FALSE);

          /* Add 'clear' icon */
          gtk_entry_set_icon_from_icon_name (GTK_ENTRY (widget),
              GTK_ENTRY_ICON_SECONDARY, "edit-clear-symbolic");

          gtk_entry_set_icon_sensitive (GTK_ENTRY (widget),
              GTK_ENTRY_ICON_SECONDARY, !TPAW_STR_EMPTY (str));

          g_signal_connect (widget, "icon-release",
              G_CALLBACK (clear_icon_released_cb), self);
          g_signal_connect (widget, "changed",
              G_CALLBACK (password_entry_changed_cb), self);
          g_signal_connect (widget, "activate",
              G_CALLBACK (password_entry_activated_cb), self);
        }
      else if (strstr (param_name, "account"))
        g_signal_connect (widget, "activate",
            G_CALLBACK (account_entry_activated_cb), self);

      g_signal_connect (widget, "changed",
          G_CALLBACK (account_widget_entry_changed_cb), self);
      g_signal_connect (widget, "map",
          G_CALLBACK (account_widget_entry_map_cb), self);

      g_free (str);
    }
  else if (GTK_IS_TOGGLE_BUTTON (widget))
    {
      gboolean value = FALSE;

      value = tpaw_account_settings_get_boolean (self->priv->settings,
          param_name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), value);

      g_signal_connect (widget, "toggled",
          G_CALLBACK (account_widget_checkbutton_toggled_cb),
          self);
    }
  else if (GTK_IS_COMBO_BOX (widget))
    {
      /* The combo box's model has to contain the param value in its first
       * column (as a string) */
      gchar *str;
      GtkTreeModel *model;
      GtkTreeIter iter;
      gboolean valid;

      str = tpaw_account_settings_dup_string (self->priv->settings,
          param_name);
      model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));

      valid = gtk_tree_model_get_iter_first (model, &iter);
      while (valid)
        {
          gchar *name;

          gtk_tree_model_get (model, &iter, 0, &name, -1);
          if (!tp_strdiff (name, str))
            {
              gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);
              valid = FALSE;
            }
          else
            {
              valid = gtk_tree_model_iter_next (model, &iter);
            }

          g_free (name);
        }

      g_free (str);

      g_signal_connect (widget, "changed",
          G_CALLBACK (account_widget_combobox_changed_cb),
          self);
    }
  else
    {
      DEBUG ("Unknown type of widget for param %s", param_name);
    }

  gtk_widget_set_sensitive (widget,
      tpaw_account_settings_param_is_supported (self->priv->settings,
        param_name));
}

static GHashTable *
build_translated_params (void)
{
  GHashTable *hash;

  hash = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (hash, "account", _("Account"));
  g_hash_table_insert (hash, "password", _("Password"));
  g_hash_table_insert (hash, "server", _("Server"));
  g_hash_table_insert (hash, "port", _("Port"));

  return hash;
}

static gchar *
account_widget_generic_format_param_name (const gchar *param_name)
{
  gchar *str;
  gchar *p;
  static GHashTable *translated_params = NULL;

  g_return_val_if_fail (param_name != NULL, NULL);

  if (G_UNLIKELY (translated_params == NULL))
    translated_params = build_translated_params ();

  /* Translate most common parameters */
  str = g_hash_table_lookup (translated_params, param_name);
  if (str != NULL)
    return g_strdup (str);

  str = g_strdup (param_name);

  if (str && g_ascii_isalpha (str[0]))
    str[0] = g_ascii_toupper (str[0]);

  while ((p = strchr (str, '-')) != NULL)
    {
      if (p[1] != '\0' && g_ascii_isalpha (p[1]))
        {
          p[0] = ' ';
          p[1] = g_ascii_toupper (p[1]);
        }

      p++;
    }

  return str;
}

static void
accounts_widget_generic_setup (TpawAccountWidget *self,
    GtkWidget *grid_common_settings,
    GtkWidget *grid_advanced_settings)
{
  GList *params, *l;
  guint row_common = 0, row_advanced = 0;

  params = tpaw_account_settings_dup_tp_params (self->priv->settings);

  for (l = params; l != NULL; l = g_list_next (l))
    {
      TpConnectionManagerParam *param = l->data;
      GtkWidget       *grid_settings;
      guint           row;
      GtkWidget       *widget = NULL;
      gchar           *param_name_formatted;
      const gchar *dbus_signature;

      if (tp_connection_manager_param_is_required (param))
        {
          grid_settings = grid_common_settings;
          row = row_common++;
        }
      else if (self->priv->simple)
        {
          continue;
        }
      else
        {
          grid_settings = grid_advanced_settings;
          row = row_advanced++;
        }

      param_name_formatted = account_widget_generic_format_param_name (
          tp_connection_manager_param_get_name (param));

      dbus_signature = tp_connection_manager_param_get_dbus_signature (param);

      if (dbus_signature[0] == 's')
        {
          gchar *str;

          str = g_strdup_printf (_("%s"), param_name_formatted);
          widget = gtk_label_new (str);
          gtk_misc_set_alignment (GTK_MISC (widget), 1., 0.5);
          gtk_style_context_add_class (gtk_widget_get_style_context (widget),
              GTK_STYLE_CLASS_DIM_LABEL);
          g_free (str);

          gtk_grid_attach (GTK_GRID (grid_settings),
              widget, 0, row, 1, 1);

          gtk_widget_show (widget);

          widget = gtk_entry_new ();
          g_object_set (widget, "hexpand", TRUE, NULL);
          if (g_strcmp0 (tp_connection_manager_param_get_name (param),
                "account") == 0)
            {
              g_object_set (widget, "width-request", 280, NULL);
              g_signal_connect (widget, "realize",
                  G_CALLBACK (gtk_widget_grab_focus),
                  NULL);
            }

          gtk_grid_attach (GTK_GRID (grid_settings),
              widget, 1, row, 1, 1);

          gtk_widget_show (widget);
        }
      /* int types: ynqiuxt. double type is 'd' */
      else if (dbus_signature[0] == 'y' ||
          dbus_signature[0] == 'n' ||
          dbus_signature[0] == 'q' ||
          dbus_signature[0] == 'i' ||
          dbus_signature[0] == 'u' ||
          dbus_signature[0] == 'x' ||
          dbus_signature[0] == 't' ||
          dbus_signature[0] == 'd')
        {
          gchar   *str = NULL;
          gdouble  minint = 0;
          gdouble  maxint = 0;
          gdouble  step = 1;

          switch (dbus_signature[0])
            {
            case 'y': minint = G_MININT8;  maxint = G_MAXINT8;   break;
            case 'n': minint = G_MININT16; maxint = G_MAXINT16;  break;
            case 'q': minint = 0;          maxint = G_MAXUINT16; break;
            case 'i': minint = G_MININT32; maxint = G_MAXINT32;  break;
            case 'u': minint = 0;          maxint = G_MAXUINT32; break;
            case 'x': minint = G_MININT64; maxint = G_MAXINT64;  break;
            case 't': minint = 0;          maxint = G_MAXUINT64; break;
            case 'd': minint = G_MININT32; maxint = G_MAXINT32;
              step = 0.1; break;
            default: g_assert_not_reached ();
            }

          str = g_strdup_printf (_("%s:"), param_name_formatted);
          widget = gtk_label_new (str);
          gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
          g_free (str);

          gtk_grid_attach (GTK_GRID (grid_settings),
              widget, 0, row, 1, 1);
          gtk_widget_show (widget);

          widget = gtk_spin_button_new_with_range (minint, maxint, step);
          gtk_grid_attach (GTK_GRID (grid_settings),
              widget, 1, row, 1, 1);
          gtk_widget_show (widget);
        }
      else if (dbus_signature[0] == 'b')
        {
          widget = gtk_check_button_new_with_label (param_name_formatted);
          gtk_grid_attach (GTK_GRID (grid_settings),
              widget, 0, row, 2, 1);
          gtk_widget_show (widget);
        }
      else
        {
          DEBUG ("Unknown signature for param %s: %s",
              param_name_formatted, dbus_signature);
        }

      if (widget)
        tpaw_account_widget_setup_widget (self, widget,
            tp_connection_manager_param_get_name (param));

      g_free (param_name_formatted);
    }

  g_list_free_full (params, (GDestroyNotify) tp_connection_manager_param_free);
}

static void
account_widget_handle_params_valist (TpawAccountWidget *self,
    const gchar *first_widget,
    va_list args)
{
  GObject *object;
  const gchar *name;

  for (name = first_widget; name; name = va_arg (args, const gchar *))
    {
      const gchar *param_name;

      param_name = va_arg (args, const gchar *);
      object = gtk_builder_get_object (self->ui_details->gui, name);

      if (!object)
        {
          g_warning ("Builder is missing object '%s'.", name);
          continue;
        }

      tpaw_account_widget_setup_widget (self, GTK_WIDGET (object),
          param_name);
    }
}

static void
account_widget_cancel_clicked_cb (GtkWidget *button,
    TpawAccountWidget *self)
{
  g_signal_emit (self, signals[CANCELLED], 0);
  g_signal_emit (self, signals[CLOSE], 0, GTK_RESPONSE_CANCEL);
}

static void
account_widget_account_enabled_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  TpAccount *account = TP_ACCOUNT (source_object);
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (user_data);

  tp_account_set_enabled_finish (account, res, &error);

  if (error != NULL)
    {
      DEBUG ("Could not enable the account: %s", error->message);
      g_error_free (error);
    }
  else
    {
      tpaw_connect_new_account (account, self->priv->account_manager);
    }

  g_signal_emit (self, signals[CLOSE], 0, GTK_RESPONSE_APPLY);

  /* unref self - part of the workaround */
  g_object_unref (self);
}

static void
account_widget_applied_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  TpAccount *account;
  TpawAccountSettings *settings = TPAW_ACCOUNT_SETTINGS (source_object);
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (user_data);
  gboolean reconnect_required;
  gboolean fire_close = TRUE;

  tpaw_account_settings_apply_finish (settings, res, &reconnect_required,
      &error);

  if (error != NULL)
    {
      DEBUG ("Could not apply changes to account: %s", error->message);
      g_error_free (error);
      return;
    }

  account = tpaw_account_settings_get_account (self->priv->settings);

  if (account != NULL)
    {
      if (self->priv->creating_account)
        {
          /* By default, when an account is created, we enable it. */

          /* workaround to keep self alive during async call */
          g_object_ref (self);

          tp_account_set_enabled_async (account, TRUE,
              account_widget_account_enabled_cb, self);
          g_signal_emit (self, signals[ACCOUNT_CREATED], 0, account);

          /* Will be fired in account_widget_account_enabled_cb */
          fire_close = FALSE;
        }
      else
        {
          /* If the account was offline, we always want to try reconnecting,
           * to give it a chance to connect if the previous params were wrong.
           * tp_account_reconnect_async() won't do anything if the requested
           * presence is offline anyway. */
          if (tp_account_get_connection_status (account, NULL) ==
              TP_CONNECTION_STATUS_DISCONNECTED)
            reconnect_required = TRUE;

          if (reconnect_required && tp_account_is_enabled (account)
              && tp_account_is_enabled (account))
            {
              /* After having applied changes to a user account, we
               * reconnect it if needed. This is done so the new
               * information entered by the user is validated on the server. */
              tp_account_reconnect_async (account, NULL, NULL);
            }
        }
    }

  if (!self->priv->destroyed)
    account_widget_set_control_buttons_sensitivity (self, FALSE);

  self->priv->contains_pending_changes = FALSE;

  if (fire_close)
    {
      /* announce the widget can be closed */
      g_signal_emit (self, signals[CLOSE], 0, GTK_RESPONSE_APPLY);
    }

  /* unref the widget - part of the workaround */
  g_object_unref (self);
}

void
tpaw_account_widget_apply_and_log_in (TpawAccountWidget *self)
{
  gboolean display_name_overridden;

  g_object_get (self->priv->settings,
      "display-name-overridden", &display_name_overridden, NULL);

  if (self->priv->creating_account || !display_name_overridden)
    {
      gchar *display_name;

      /* set default display name for new accounts or update if user didn't
       * manually override it. */
      display_name = tpaw_account_widget_get_default_display_name (self);

      tpaw_account_settings_set_display_name_async (self->priv->settings,
          display_name, NULL, NULL);

      g_free (display_name);
    }

  /* workaround to keep widget alive during async call */
  g_object_ref (self);
  tpaw_account_settings_apply_async (self->priv->settings,
      account_widget_applied_cb, self);
}

static void
account_widget_apply_clicked_cb (GtkWidget *button,
    TpawAccountWidget *self)
{
    tpaw_account_widget_apply_and_log_in (self);
}

static void
account_widget_setup_generic (TpawAccountWidget *self)
{
  GtkWidget *grid_common_settings;
  GtkWidget *grid_advanced_settings;

  grid_common_settings = GTK_WIDGET (gtk_builder_get_object
      (self->ui_details->gui, "grid_common_settings"));
  grid_advanced_settings = GTK_WIDGET (gtk_builder_get_object
      (self->ui_details->gui, "grid_advanced_settings"));

  accounts_widget_generic_setup (self, grid_common_settings,
      grid_advanced_settings);

  g_object_unref (self->ui_details->gui);
}

static void
account_widget_settings_ready_cb (TpawAccountSettings *settings,
    GParamSpec *pspec,
    gpointer user_data)
{
  TpawAccountWidget *self = user_data;

  if (tpaw_account_settings_is_ready (self->priv->settings))
    account_widget_setup_generic (self);
}

static GtkWidget *
account_widget_build_generic (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *expander_advanced, *box;

  self->ui_details->gui = tpaw_builder_get_resource (filename,
      "grid_common_settings", &self->priv->grid_common_settings,
      "vbox_generic_settings", &box,
      "expander_advanced_settings", &expander_advanced,
      NULL);

  if (self->priv->simple)
    gtk_widget_hide (expander_advanced);

  g_object_ref (self->ui_details->gui);

  if (tpaw_account_settings_is_ready (self->priv->settings))
    account_widget_setup_generic (self);
  else
    g_signal_connect (self->priv->settings, "notify::ready",
        G_CALLBACK (account_widget_settings_ready_cb), self);

  return box;
}

static GtkWidget *
account_widget_build_salut (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *expander_advanced, *box;

  self->ui_details->gui = tpaw_builder_get_resource (filename,
      "grid_common_settings", &self->priv->grid_common_settings,
      "vbox_salut_settings", &box,
      "expander_advanced_settings", &expander_advanced,
      NULL);

  tpaw_account_widget_handle_params (self,
      "entry_published", "published-name",
      "entry_nickname", "nickname",
      "entry_first_name", "first-name",
      "entry_last_name", "last-name",
      "entry_email", "email",
      "entry_jid", "jid",
      NULL);

  if (self->priv->simple)
    gtk_widget_hide (expander_advanced);

  self->ui_details->default_focus = g_strdup ("entry_first_name");

  return box;
}

static GtkWidget *
account_widget_build_irc (TpawAccountWidget *self,
  const char *filename)
{
  GtkWidget *box;

  tpaw_account_settings_set_regex (self->priv->settings, "account",
      ACCOUNT_REGEX_IRC);

  if (self->priv->simple)
    {
      self->priv->irc_network_chooser = tpaw_account_widget_irc_build_simple
        (self, filename, &box);
    }
  else
    {
      self->priv->irc_network_chooser = tpaw_account_widget_irc_build (self,
          filename, &self->priv->grid_common_settings, &box);
    }

  return box;
}

static GtkWidget *
account_widget_build_sip (TpawAccountWidget *self,
  const char *filename)
{
  GtkWidget *box;

  box = tpaw_account_widget_sip_build (self, filename,
    &self->priv->grid_common_settings);

  if (self->priv->simple)
    {
      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else
    {
      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));
    }

  return box;
}

static GtkWidget *
account_widget_build_msn (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *box;

  tpaw_account_settings_set_regex (self->priv->settings, "account",
      ACCOUNT_REGEX_MSN);

  if (self->priv->simple)
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_msn_simple", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "grid_common_msn_settings", &self->priv->grid_common_settings,
          "vbox_msn_settings", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));
    }

  return box;
}

static void
suffix_id_widget_changed_cb (GtkWidget *entry,
    TpawAccountWidget *self)
{
  gchar *account;

  g_assert (self->priv->jid_suffix != NULL);

  account_widget_entry_changed_common (self, GTK_ENTRY (entry), FALSE);

  account = tpaw_account_settings_dup_string (self->priv->settings,
      "account");

  if (!TPAW_STR_EMPTY (account) &&
      !g_str_has_suffix (account, self->priv->jid_suffix))
    {
      gchar *tmp;

      tmp = g_strdup_printf ("%s%s", account, self->priv->jid_suffix);

      DEBUG ("Change account from '%s' to '%s'", account, tmp);

      tpaw_account_settings_set (self->priv->settings, "account",
          g_variant_new_string (tmp));
      g_free (tmp);
    }

  tpaw_account_widget_changed (self);

  g_free (account);
}

static gchar *
remove_jid_suffix (TpawAccountWidget *self,
    const gchar *str)
{
  g_assert (self->priv->jid_suffix != NULL);

  if (!g_str_has_suffix (str, self->priv->jid_suffix))
    return g_strdup (str);

  return g_strndup (str, strlen (str) - strlen (self->priv->jid_suffix));
}

static void
setup_id_widget_with_suffix (TpawAccountWidget *self,
    GtkWidget *widget,
    const gchar *suffix)
{
  gchar *str = NULL;

  g_object_set_data_full (G_OBJECT (widget), "param_name",
      g_strdup ("account"), g_free);

  g_assert (self->priv->jid_suffix == NULL);
  self->priv->jid_suffix = g_strdup (suffix);

  str = tpaw_account_settings_dup_string (self->priv->settings, "account");
  if (str != NULL)
    {
      gchar *tmp;

      tmp = remove_jid_suffix (self, str);
      gtk_entry_set_text (GTK_ENTRY (widget), tmp);
      g_free (tmp);
      g_free (str);
    }

  self->priv->param_account_widget = widget;

  g_signal_connect (widget, "changed",
      G_CALLBACK (suffix_id_widget_changed_cb), self);
}

static Service
account_widget_get_service (TpawAccountWidget *self)
{
  const gchar *icon_name, *service;

  icon_name = tpaw_account_settings_get_icon_name (self->priv->settings);
  service = tpaw_account_settings_get_service (self->priv->settings);

  /* Previous versions of Tpaw didn't set the Service property on Facebook
   * and gtalk accounts, so we check using the icon name as well. */
  if (!tp_strdiff (icon_name, "im-google-talk") ||
      !tp_strdiff (service, "google-talk"))
    return GTALK_SERVICE;

  if (!tp_strdiff (icon_name, "im-facebook") ||
      !tp_strdiff (service, "facebook"))
    return FACEBOOK_SERVICE;

  return NO_SERVICE;
}

static GtkWidget *
account_widget_build_jabber (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *spinbutton_port;
  GtkWidget *checkbutton_ssl;
  GtkWidget *label_id, *label_password;
  GtkWidget *label_example_fb;
  GtkWidget *label_example;
  GtkWidget *expander_advanced;
  GtkWidget *entry_id;
  GtkWidget *box;
  Service service;

  service = account_widget_get_service (self);

  tpaw_account_settings_set_regex (self->priv->settings, "account",
      ACCOUNT_REGEX_JABBER);

  if (self->priv->simple && service == NO_SERVICE)
    {
      /* Simple widget for XMPP */
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_jabber_simple", &box,
          "label_id_simple", &label_id,
          "label_password_simple", &label_password,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else if (self->priv->simple && service == GTALK_SERVICE)
    {
      /* Simple widget for Google Talk */
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_gtalk_simple", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id_g_simple", "account",
          "entry_password_g_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_g_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_g_simple"));
    }
  else if (self->priv->simple && service == FACEBOOK_SERVICE)
    {
      /* Simple widget for Facebook */
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_fb_simple", &box,
          "entry_id_fb_simple", &entry_id,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_password_fb_simple", "password",
          NULL);

      setup_id_widget_with_suffix (self, entry_id, "@chat.facebook.com");

      self->ui_details->default_focus = g_strdup ("entry_id_fb_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_fb_simple"));
    }
  else
    {
      ServiceInfo info = services_infos[service];

      /* Full widget for XMPP, Google Talk and Facebook*/
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "grid_common_settings", &self->priv->grid_common_settings,
          "vbox_jabber_settings", &box,
          "spinbutton_port", &spinbutton_port,
          "checkbutton_ssl", &checkbutton_ssl,
          "label_username_f_example", &label_example_fb,
          info.label_username_example, &label_example,
          "expander_advanced", &expander_advanced,
          "entry_id", &entry_id,
          "label_id", &label_id,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_password", "password",
          "entry_resource", "resource",
          "entry_server", "server",
          "spinbutton_port", "port",
          "spinbutton_priority", "priority",
          "checkbutton_ssl", "old-ssl",
          "checkbutton_ignore_ssl_errors", "ignore-ssl-errors",
          "checkbutton_encryption", "require-encryption",
          NULL);

      if (service == FACEBOOK_SERVICE)
        {
          gtk_label_set_label (GTK_LABEL (label_id), _("Username:"));

          /* Facebook special case the entry ID widget to hide the
           * "@chat.facebook.com" part */
          setup_id_widget_with_suffix (self, entry_id, "@chat.facebook.com");
        }
      else
        {
          tpaw_account_widget_setup_widget (self, entry_id, "account");
        }

      self->ui_details->default_focus = g_strdup ("entry_id");
      self->priv->spinbutton_port = spinbutton_port;

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));

      g_signal_connect (checkbutton_ssl, "toggled",
          G_CALLBACK (account_widget_jabber_ssl_toggled_cb),
          self);

      if (service == FACEBOOK_SERVICE)
        {
          GtkContainer *parent;
          GList *children;

          /* Removing the label from list of focusable widgets */
          parent = GTK_CONTAINER (gtk_widget_get_parent (label_example_fb));
          children = gtk_container_get_children (parent);
          children = g_list_remove (children, label_example_fb);
          gtk_container_set_focus_chain (parent, children);
          g_list_free (children);
        }

      gtk_widget_show (label_example);

      if (!info.show_advanced)
        gtk_widget_hide (expander_advanced);
    }

  return box;
}

static GtkWidget *
account_widget_build_icq (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *spinbutton_port;
  GtkWidget *box;

  tpaw_account_settings_set_regex (self->priv->settings, "account",
      ACCOUNT_REGEX_ICQ);

  if (self->priv->simple)
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_icq_simple", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_uin_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_uin_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "grid_common_settings", &self->priv->grid_common_settings,
          "vbox_icq_settings", &box,
          "spinbutton_port", &spinbutton_port,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_uin", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          "entry_charset", "charset",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_uin");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));
    }

  return box;
}

static GtkWidget *
account_widget_build_aim (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *spinbutton_port, *box;

  if (self->priv->simple)
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_aim_simple", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_screenname_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_screenname_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "grid_common_settings", &self->priv->grid_common_settings,
          "vbox_aim_settings", &box,
          "spinbutton_port", &spinbutton_port,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_screenname", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_screenname");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));
    }

  return box;
}

static GtkWidget *
account_widget_build_yahoo (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *box;

  tpaw_account_settings_set_regex (self->priv->settings, "account",
      ACCOUNT_REGEX_YAHOO);

  if (self->priv->simple)
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_yahoo_simple", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "grid_common_settings", &self->priv->grid_common_settings,
          "vbox_yahoo_settings", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_locale", "room-list-locale",
          "entry_charset", "charset",
          "spinbutton_port", "port",
          "checkbutton_ignore_invites", "ignore-invites",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));
    }

  return box;
}

static GtkWidget *
account_widget_build_groupwise (TpawAccountWidget *self,
    const char *filename)
{
  GtkWidget *box;

  if (self->priv->simple)
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "vbox_groupwise_simple", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id_simple");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui,
            "remember_password_simple"));
    }
  else
    {
      self->ui_details->gui = tpaw_builder_get_resource (filename,
          "grid_common_groupwise_settings", &self->priv->grid_common_settings,
          "vbox_groupwise_settings", &box,
          NULL);

      tpaw_account_widget_handle_params (self,
          "entry_id", "account",
          "entry_password", "password",
          "entry_server", "server",
          "spinbutton_port", "port",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_id");

      self->priv->remember_password_widget = GTK_WIDGET (
          gtk_builder_get_object (self->ui_details->gui, "remember_password"));
    }

  return box;
}

void
tpaw_account_widget_set_other_accounts_exist (TpawAccountWidget *self,
    gboolean others_exist)
{
  self->priv->other_accounts_exist = others_exist;

  if (self->priv->creating_account)
    account_widget_handle_control_buttons_sensitivity (self);
}

static void
do_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (object);

  switch (prop_id)
    {
    case PROP_SETTINGS:
      self->priv->settings = g_value_dup_object (value);
      break;
    case PROP_SIMPLE:
      self->priv->simple = g_value_get_boolean (value);
      break;
    case PROP_CREATING_ACCOUNT:
      self->priv->creating_account = g_value_get_boolean (value);
      break;
    case PROP_OTHER_ACCOUNTS_EXIST:
      tpaw_account_widget_set_other_accounts_exist (
          TPAW_ACCOUNT_WIDGET (object), g_value_get_boolean (value));
      break;
    case PROP_ACTION_AREA:
      self->priv->action_area = g_value_get_object (value);
      if (self->priv->action_area != NULL)
        {
          g_object_ref_sink (self->priv->action_area);
          self->priv->external_action_area = TRUE;
        }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
do_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (object);

  switch (prop_id)
    {
    case PROP_PROTOCOL:
      g_value_set_string (value,
        tpaw_account_settings_get_protocol (self->priv->settings));
      break;
    case PROP_SETTINGS:
      g_value_set_object (value, self->priv->settings);
      break;
    case PROP_SIMPLE:
      g_value_set_boolean (value, self->priv->simple);
      break;
    case PROP_CREATING_ACCOUNT:
      g_value_set_boolean (value, self->priv->creating_account);
      break;
    case PROP_OTHER_ACCOUNTS_EXIST:
      g_value_set_boolean (value, self->priv->other_accounts_exist);
      break;
    case PROP_ACTION_AREA:
      g_value_set_object (value, self->priv->action_area);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
set_apply_button (TpawAccountWidget *self)
{
  GtkWidget *image;

  /* We can't use the stock button as its accelerator ('A') clashes with the
   * Add button. */
  gtk_button_set_use_stock (GTK_BUTTON (self->priv->apply_button), FALSE);

  gtk_button_set_label (GTK_BUTTON (self->priv->apply_button), _("A_pply"));
  gtk_button_set_use_underline (GTK_BUTTON (self->priv->apply_button), TRUE);

  image = gtk_image_new_from_stock (GTK_STOCK_APPLY, GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (self->priv->apply_button), image);
}

static void
presence_changed_cb (TpAccountManager *manager,
    TpConnectionPresenceType state,
    const gchar *status,
    const gchar *message,
    TpawAccountWidget *self)
{
  if (self->priv->destroyed)
    return;

  if (self->priv->apply_button == NULL)
    /* This button doesn't exist in 'simple' mode */
    return;

  if (state > TP_CONNECTION_PRESENCE_TYPE_OFFLINE &&
      self->priv->creating_account)
    {
      /* We are online and creating a new account, display a Login button */
      GtkWidget *image;

      gtk_button_set_use_stock (GTK_BUTTON (self->priv->apply_button), FALSE);
      gtk_button_set_label (GTK_BUTTON (self->priv->apply_button),
          _("L_og in"));

      image = gtk_image_new_from_stock (GTK_STOCK_CONNECT,
          GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image (GTK_BUTTON (self->priv->apply_button), image);
    }
  else
    {
      /* We are offline or modifying an existing account, display
       * a Save button */
      set_apply_button (self);
    }
}

static void
account_manager_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (user_data);
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;
  TpConnectionPresenceType state;

  if (!tp_proxy_prepare_finish (account_manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      goto out;
    }

  state = tp_account_manager_get_most_available_presence (account_manager, NULL,
      NULL);

  /* simulate a presence change so the apply button will be changed
   * if needed */
  presence_changed_cb (account_manager, state, NULL, NULL, self);

out:
  g_object_unref (self);
}

#define WIDGET(cm, proto) \
  { #cm, #proto, TPAW_ACCOUNT_WIDGETS_RESOURCES_PREFIX "/tpaw-account-widget-"#proto".ui", \
    account_widget_build_##proto }

static void
remember_password_toggled_cb (GtkToggleButton *button,
    TpawAccountWidget *self)
{
  tpaw_account_settings_set_remember_password (self->priv->settings,
      gtk_toggle_button_get_active (button));

  if (!self->priv->automatic_change)
    tpaw_account_widget_changed (self);
}

static void
account_settings_password_retrieved_cb (GObject *object,
    gpointer user_data)
{
  TpawAccountWidget *self = user_data;
  gchar *password;

  password = tpaw_account_settings_dup_string (
      self->priv->settings, "password");

  /* We have to do this so that when we call gtk_entry_set_text,
   * the ::changed callback doesn't think the user made the
   * change. This is also used in remember_password_toggled_cb. */
  self->priv->automatic_change = TRUE;

  if (password != NULL)
    {
      gtk_entry_set_text (GTK_ENTRY (self->priv->param_password_widget),
          password);
    }

  gtk_toggle_button_set_active (
      GTK_TOGGLE_BUTTON (self->priv->remember_password_widget),
      !TPAW_STR_EMPTY (password));

  self->priv->automatic_change = FALSE;

  g_free (password);
}

static void
do_constructed (GObject *obj)
{
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (obj);
  const gchar *display_name, *default_display_name;
  guint i = 0;
  struct {
    const gchar *cm_name;
    const gchar *protocol;
    const char *file;
    GtkWidget * (*func)(TpawAccountWidget *self, const gchar *filename);
  } widgets [] = {
    { "salut", "local-xmpp", TPAW_ACCOUNT_WIDGETS_RESOURCES_PREFIX "/tpaw-account-widget-local-xmpp.ui",
        account_widget_build_salut },
    WIDGET (gabble, jabber),
    WIDGET (haze, msn),
    WIDGET (haze, icq),
    WIDGET (haze, aim),
    WIDGET (haze, yahoo),
    WIDGET (haze, groupwise),
    WIDGET (idle, irc),
    WIDGET (sofiasip, sip),
  };
  const gchar *protocol, *cm_name;
  GtkWidget *box;

  cm_name = tpaw_account_settings_get_cm (self->priv->settings);
  protocol = tpaw_account_settings_get_protocol (self->priv->settings);

  for (i = 0 ; i < G_N_ELEMENTS (widgets); i++)
    {
      if (!tp_strdiff (widgets[i].cm_name, cm_name) &&
          !tp_strdiff (widgets[i].protocol, protocol))
        {
          box = widgets[i].func (self, widgets[i].file);
          break;
        }
    }

  if (i == G_N_ELEMENTS (widgets))
    {
      gchar *filename = TPAW_ACCOUNT_WIDGETS_RESOURCES_PREFIX "/tpaw-account-widget-generic.ui";
      box = account_widget_build_generic (self, filename);
    }

  gtk_container_add (GTK_CONTAINER (self), box);

  /* handle default focus */
  if (self->ui_details->default_focus != NULL)
    {
      GObject *default_focus_entry;

      default_focus_entry = gtk_builder_get_object
        (self->ui_details->gui, self->ui_details->default_focus);
      g_signal_connect (default_focus_entry, "realize",
          G_CALLBACK (gtk_widget_grab_focus),
          NULL);
    }

  /* remember password */
  if (self->priv->param_password_widget != NULL
      && self->priv->remember_password_widget != NULL
      && tpaw_account_settings_supports_sasl (self->priv->settings))
    {
      if (self->priv->simple)
        {
          gtk_toggle_button_set_active (
              GTK_TOGGLE_BUTTON (self->priv->remember_password_widget), TRUE);
        }
      else
        {
          gchar *password;

          password = tpaw_account_settings_dup_string (self->priv->settings,
              "password");

          /* FIXME: we should enable this checkbox only if the password is
           * stored for good in the password storage, not only for the session
           * (bgo #683571) */
          gtk_toggle_button_set_active (
              GTK_TOGGLE_BUTTON (self->priv->remember_password_widget),
              !TPAW_STR_EMPTY (password));

          /* The password might not have been retrieved from the
           * keyring yet. We should update the remember password
           * toggle button and the password entry when/if it is. */
          tp_g_signal_connect_object (self->priv->settings,
              "password-retrieved",
              G_CALLBACK (account_settings_password_retrieved_cb), self, 0);

          g_free (password);
        }

      g_signal_connect (self->priv->remember_password_widget, "toggled",
          G_CALLBACK (remember_password_toggled_cb), self);

      self->priv->automatic_change = TRUE;
      remember_password_toggled_cb (
          GTK_TOGGLE_BUTTON (self->priv->remember_password_widget), self);
      self->priv->automatic_change = FALSE;
    }
  else if (self->priv->remember_password_widget != NULL
      && !tpaw_account_settings_supports_sasl (self->priv->settings))
    {
      gtk_widget_set_visible (self->priv->remember_password_widget, FALSE);
      tpaw_account_settings_set_remember_password (self->priv->settings,
          TRUE);
    }

  /* dup and init the account-manager */
  self->priv->account_manager = tp_account_manager_dup ();

  g_object_ref (self);
  tp_proxy_prepare_async (self->priv->account_manager, NULL,
      account_manager_ready_cb, self);

  if (!self->priv->external_action_area)
    {
      g_assert (self->priv->action_area == NULL);
      self->priv->action_area = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_button_box_set_layout (GTK_BUTTON_BOX (self->priv->action_area),
          GTK_BUTTONBOX_END);
      /* Hard code the default spacing as we cannot easily get this property
       * as the widget is not in a GtkDialog yet (and it could end up packed
       * in a non-GtkDialog window anyway */
      gtk_box_set_spacing (GTK_BOX (self->priv->action_area), 6);
      /* If the action area is set by the user of this class then we keep a
       * reference, so we do the same here */
      g_object_ref_sink (self->priv->action_area);
    }
  else
    {
      g_assert (self->priv->action_area != NULL);
    }

  self->priv->cancel_button = gtk_button_new_from_stock (GTK_STOCK_CLOSE);

  self->priv->apply_button = gtk_button_new ();
  set_apply_button (self);

  /* We'll change this button to a "Log in" one if we are creating a new
   * account and are connected. */
  tp_g_signal_connect_object (self->priv->account_manager,
      "most-available-presence-changed",
      G_CALLBACK (presence_changed_cb), obj, 0);

  gtk_box_pack_end (GTK_BOX (self->priv->action_area),
      self->priv->cancel_button, TRUE, TRUE, 3);
  gtk_box_pack_end (GTK_BOX (self->priv->action_area),
      self->priv->apply_button, TRUE, TRUE, 3);

  if (!self->priv->external_action_area)
    gtk_box_pack_end (GTK_BOX (self), self->priv->action_area, FALSE,
        FALSE, 3);

  g_signal_connect (self->priv->cancel_button, "clicked",
      G_CALLBACK (account_widget_cancel_clicked_cb),
      self);
  g_signal_connect (self->priv->apply_button, "clicked",
      G_CALLBACK (account_widget_apply_clicked_cb),
      self);
  gtk_widget_show_all (self->priv->action_area);

  if (self->priv->creating_account)
    /* When creating an account, the user might have nothing to enter.
     * That means that no control interaction might occur,
     * so we update the control button sensitivity manually.
     */
    account_widget_handle_control_buttons_sensitivity (self);
  else
    account_widget_set_control_buttons_sensitivity (self, FALSE);

  g_clear_object (&self->ui_details->gui);

  display_name = tpaw_account_settings_get_display_name (
      self->priv->settings);
  default_display_name = tpaw_account_widget_get_default_display_name (self);

  if (tp_strdiff (display_name, default_display_name) &&
      !self->priv->creating_account)
    {
      /* The display name of the account is not the one that we'd assign by
       * default; assume that the user changed it manually */
      g_object_set (self->priv->settings, "display-name-overridden", TRUE,
          NULL);
    }
}

static void
do_dispose (GObject *obj)
{
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (obj);

  g_clear_object (&self->priv->settings);
  g_clear_object (&self->priv->account_manager);
  g_clear_object (&self->priv->action_area);

  if (G_OBJECT_CLASS (tpaw_account_widget_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (tpaw_account_widget_parent_class)->dispose (obj);
}

static void
do_finalize (GObject *obj)
{
  TpawAccountWidget *self = TPAW_ACCOUNT_WIDGET (obj);

  g_free (self->ui_details->default_focus);
  g_slice_free (TpawAccountWidgetUIDetails, self->ui_details);

  g_free (self->priv->jid_suffix);

  if (G_OBJECT_CLASS (tpaw_account_widget_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (tpaw_account_widget_parent_class)->finalize (obj);
}

static void
tpaw_account_widget_class_init (TpawAccountWidgetClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  oclass->get_property = do_get_property;
  oclass->set_property = do_set_property;
  oclass->constructed = do_constructed;
  oclass->dispose = do_dispose;
  oclass->finalize = do_finalize;

  param_spec = g_param_spec_string ("protocol",
      "protocol", "The protocol of the account",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_PROTOCOL, param_spec);

  param_spec = g_param_spec_object ("settings",
      "settings", "The settings of the account",
      TPAW_TYPE_ACCOUNT_SETTINGS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_SETTINGS, param_spec);

  param_spec = g_param_spec_boolean ("simple",
      "simple", "Whether the account widget is a simple or an advanced one",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_SIMPLE, param_spec);

  param_spec = g_param_spec_boolean ("creating-account",
      "creating-account",
      "TRUE if we're creating an account, FALSE if we're modifying it",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_CREATING_ACCOUNT, param_spec);

  param_spec = g_param_spec_boolean ("other-accounts-exist",
      "other-accounts-exist",
      "TRUE if there are any other accounts (even if this isn't yet saved)",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_OTHER_ACCOUNTS_EXIST,
                  param_spec);

  param_spec = g_param_spec_object ("action-area",
      "action-area",
      "The widget where to pack the action buttons (or NULL)",
      GTK_TYPE_BOX,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_ACTION_AREA,
                  param_spec);

  signals[HANDLE_APPLY] =
    g_signal_new ("handle-apply", G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
        g_cclosure_marshal_generic,
        G_TYPE_NONE,
        1, G_TYPE_BOOLEAN);

  /* This signal is emitted when an account has been created and enabled. */
  signals[ACCOUNT_CREATED] =
      g_signal_new ("account-created", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST, 0, NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE,
          1, G_TYPE_OBJECT);

  signals[CANCELLED] =
      g_signal_new ("cancelled", G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST, 0, NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE,
          0);

  signals[CLOSE] =
    g_signal_new ("close", G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
        g_cclosure_marshal_VOID__INT,
        G_TYPE_NONE,
        1, G_TYPE_INT);

  g_type_class_add_private (klass, sizeof (TpawAccountWidgetPriv));
}

static void
tpaw_account_widget_init (TpawAccountWidget *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self), TPAW_TYPE_ACCOUNT_WIDGET,
        TpawAccountWidgetPriv);

  self->ui_details = g_slice_new0 (TpawAccountWidgetUIDetails);
}

/* public methods */

void
tpaw_account_widget_discard_pending_changes (TpawAccountWidget *self)
{
  tpaw_account_settings_discard_changes (self->priv->settings);
  self->priv->contains_pending_changes = FALSE;
}

gboolean
tpaw_account_widget_contains_pending_changes (TpawAccountWidget *self)
{
  return self->priv->contains_pending_changes;
}

void
tpaw_account_widget_handle_params (TpawAccountWidget *self,
    const gchar *first_widget,
    ...)
{
  va_list args;

  va_start (args, first_widget);
  account_widget_handle_params_valist (self, first_widget, args);
  va_end (args);
}

TpawAccountWidget *
tpaw_account_widget_new_for_protocol (TpawAccountSettings *settings,
    GtkBox *action_area,
    gboolean simple)
{
  g_return_val_if_fail (TPAW_IS_ACCOUNT_SETTINGS (settings), NULL);

  return g_object_new (TPAW_TYPE_ACCOUNT_WIDGET,
        "orientation", GTK_ORIENTATION_VERTICAL,
        "settings", settings,
        "simple", simple,
        "creating-account",
          tpaw_account_settings_get_account (settings) == NULL,
        "action-area", action_area,
        NULL);
}

gchar *
tpaw_account_widget_get_default_display_name (TpawAccountWidget *self)
{
  gchar *login_id;
  const gchar *protocol, *p;
  gchar *default_display_name;
  Service service;

  login_id = tpaw_account_settings_dup_string (self->priv->settings,
      "account");
  protocol = tpaw_account_settings_get_protocol (self->priv->settings);
  service = account_widget_get_service (self);

  if (login_id != NULL)
    {
      /* TODO: this should be done in tpaw-account-widget-irc */
      if (!tp_strdiff (protocol, "irc"))
        {
          TpawIrcNetwork *network;

          network = tpaw_irc_network_chooser_get_network (
              self->priv->irc_network_chooser);
          g_assert (network != NULL);

          /* To translators: The first parameter is the login id and the
           * second one is the network. The resulting string will be something
           * like: "MyUserName on freenode".
           * You should reverse the order of these arguments if the
           * server should come before the login id in your locale.*/
          default_display_name = g_strdup_printf (_("%1$s on %2$s"),
              login_id, tpaw_irc_network_get_name (network));
        }
      else if (service == FACEBOOK_SERVICE && self->priv->jid_suffix != NULL)
        {
          gchar *tmp;

          tmp = remove_jid_suffix (self, login_id);
          default_display_name = g_strdup_printf ("Facebook (%s)", tmp);
          g_free (tmp);
        }
      else
        {
          default_display_name = g_strdup (login_id);
        }

      return default_display_name;
    }

  if ((p = tpaw_protocol_name_to_display_name (protocol)) != NULL)
    protocol = p;

  if (protocol != NULL)
    {
      /* To translators: The parameter is the protocol name. The resulting
       * string will be something like: "Jabber Account" */
      default_display_name = g_strdup_printf (_("%s Account"), protocol);
    }
  else
    {
      default_display_name = g_strdup (_("New account"));
    }

  g_free (login_id);

  return default_display_name;
}

/* Used by subclass to indicate that widget contains pending changes */
void
tpaw_account_widget_changed (TpawAccountWidget *self)
{
  account_widget_handle_control_buttons_sensitivity (self);
  self->priv->contains_pending_changes = TRUE;
}

void
tpaw_account_widget_set_account_param (TpawAccountWidget *self,
    const gchar *account)
{
  if (self->priv->param_account_widget == NULL)
    return;

  gtk_entry_set_text (GTK_ENTRY (self->priv->param_account_widget), account);
}

void
tpaw_account_widget_set_password_param (TpawAccountWidget *self,
    const gchar *account)
{
  if (self->priv->param_password_widget == NULL)
    return;

  gtk_entry_set_text (GTK_ENTRY (self->priv->param_password_widget), account);
}

TpawAccountSettings *
tpaw_account_widget_get_settings (TpawAccountWidget *self)
{
  return self->priv->settings;
}

void
tpaw_account_widget_hide_buttons (TpawAccountWidget *self)
{
  gtk_widget_hide (self->priv->action_area);
}
