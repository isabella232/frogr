/*
 * frogr-auth-dialog.c -- Authorization dialog
 *
 * Copyright (C) 2009 Mario Sanchez Prada
 * Authors: Mario Sanchez Prada <msanchez@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 3 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <glib/gi18n.h>
#include "frogr-controller.h"
#include "frogr-auth-dialog.h"

static gchar *unauth_txt =
  N_("You have not properly authorized %s in flickr.\n"
     "Please press the 'Continue' button to authorize %s "
     "and then come back to this screen to complete the process.");

static gchar *auth_txt =
  N_("Press 'Complete' to start using %s once you've "
     "authorized it in your flickr account.");

#define FROGR_AUTH_DIALOG_GET_PRIVATE(object)                   \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object),                       \
                                FROGR_TYPE_AUTH_DIALOG,         \
                                FrogrAuthDialogPrivate))

G_DEFINE_TYPE (FrogrAuthDialog, frogr_auth_dialog, GTK_TYPE_DIALOG);

typedef struct _FrogrAuthDialogPrivate {
  FrogrController *controller;
  GtkWidget *info_label;
  GtkWidget *button;
} FrogrAuthDialogPrivate;


/* Private API */

static void
_frogr_auth_dialog_finalize (GObject *object)
{
  FrogrAuthDialogPrivate *priv = FROGR_AUTH_DIALOG_GET_PRIVATE (object);
  g_object_unref (priv->controller);
  G_OBJECT_CLASS(frogr_auth_dialog_parent_class)->finalize (object);
}

static void
frogr_auth_dialog_class_init (FrogrAuthDialogClass *klass)
{
  GObjectClass *obj_class = (GObjectClass *)klass;
  obj_class->finalize = _frogr_auth_dialog_finalize;
  g_type_class_add_private (obj_class, sizeof (FrogrAuthDialogPrivate));
}

static void
frogr_auth_dialog_init (FrogrAuthDialog *self)
{
  FrogrAuthDialogPrivate *priv = FROGR_AUTH_DIALOG_GET_PRIVATE (self);
  GtkWidget *vbox;
  gchar *aux_string = NULL;

  /* Get controller */
  priv->controller = frogr_controller_get_instance ();

  /* Create widgets */
  priv->button = gtk_dialog_add_button (GTK_DIALOG (self),
                                        _("Continue"),
                                        GTK_RESPONSE_ACCEPT);
  /* Add labels */
  aux_string = g_strdup_printf (_(unauth_txt), PACKAGE_NAME, PACKAGE_NAME);
  priv->info_label = gtk_label_new (aux_string);
  g_free (aux_string);
  gtk_label_set_line_wrap (GTK_LABEL (priv->info_label), TRUE);

#if GTK_CHECK_VERSION (2,14,0)
  vbox = gtk_dialog_get_content_area (GTK_DIALOG (self));
#else
  vbox = GTK_DIALOG (self)->vbox;
#endif
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_box_pack_start (GTK_BOX (vbox), priv->info_label, FALSE, FALSE, 0);

  /* Show the UI */
  gtk_widget_show_all (GTK_WIDGET(self));
}

/* Public API */

FrogrAuthDialog *
frogr_auth_dialog_new (GtkWindow *parent)
{
  GObject *new = g_object_new (FROGR_TYPE_AUTH_DIALOG,
                               "title", _("Authorize Frogr"),
                               "modal", TRUE,
                               "transient-for", parent,
                               "resizable", FALSE,
                               "has-separator", FALSE,
                               NULL);
  return FROGR_AUTH_DIALOG (new);
}

void
frogr_auth_dialog_show (FrogrAuthDialog *self)
{
  FrogrAuthDialogPrivate *priv = FROGR_AUTH_DIALOG_GET_PRIVATE (self);

  if (!frogr_controller_is_authorized (priv->controller))
    {
      gboolean authorizing = FALSE;
      gboolean authorized = FALSE;
      gint response = 0;

      /* Run the dialog */
      do
        {
          response = gtk_dialog_run (GTK_DIALOG (self));
          if (response == GTK_RESPONSE_ACCEPT && !authorizing)
            {
              gchar *aux_string = NULL;

              frogr_controller_open_authorization_url (priv->controller);
              authorizing = TRUE;

              gtk_button_set_label (GTK_BUTTON (priv->button), _("Complete"));
              aux_string = g_strdup_printf (_(auth_txt), PACKAGE_NAME);
              gtk_label_set_text (GTK_LABEL (priv->info_label), aux_string);
              g_free (aux_string);
            }
          else if (response == GTK_RESPONSE_ACCEPT)
            {
              if (frogr_controller_complete_authorization (priv->controller))
                {
                  /* Everything went fine */
                  authorized = TRUE;
                  g_debug ("Authorization successfully completed!");
                }
              else
                {
                  /* An error happened */
                  GtkWidget *msg_dialog;
                  gchar *aux_string = NULL;

                  msg_dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Authorization failed.\n"
                                                         "Please try again"));
                  /* Show and destroy a an error message */
                  gtk_dialog_run (GTK_DIALOG (msg_dialog));
                  gtk_widget_destroy (msg_dialog);

                  /* Restore values if reached */
                  authorizing = FALSE;
                  authorized = FALSE;
                  gtk_button_set_label (GTK_BUTTON (priv->button), _("Continue"));
                  aux_string = g_strdup_printf (_(unauth_txt), PACKAGE_NAME, PACKAGE_NAME);
                  gtk_label_set_text (GTK_LABEL (priv->info_label), aux_string);
                  g_free (aux_string);
                  g_debug ("Authorization not completed");
                }
            }

        } while (!authorized && (response == GTK_RESPONSE_ACCEPT));
    }

  /* Destroy the dialog */
  gtk_widget_destroy (GTK_WIDGET (self));
}
