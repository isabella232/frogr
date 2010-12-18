 /*
 * fsp-session.c
 *
 * Copyright (C) 2010 Mario Sanchez Prada
 * Authors: Mario Sanchez Prada <msanchez@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 3 of the GNU Lesser General
 * Public License as published by the Free Software Foundation.
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

#include "fsp-session.h"

#include "fsp-data.h"
#include "fsp-error.h"
#include "fsp-flickr-parser.h"
#include "fsp-global-defs.h"
#include "fsp-session-priv.h"
#include "fsp-util.h"

#include <libsoup/soup.h>
#include <string.h>

#define FSP_SESSION_GET_PRIVATE(object)                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object),               \
                                FSP_TYPE_SESSION,       \
                                FspSessionPrivate))

G_DEFINE_TYPE (FspSession, fsp_session, G_TYPE_OBJECT);


/* Private struct */
struct _FspSessionPrivate
{
  gchar *api_key;
  gchar *secret;
  gchar *token;
  gchar *frob;

  FspFlickrParser *parser;
  SoupSession *soup_session;
};


/* Properties */

enum  {
  PROP_0,
  PROP_API_KEY,
  PROP_SECRET,
  PROP_TOKEN
};


/* Prototypes */

static SoupSession *
_get_soup_session                       (FspSession *self);

static void
_get_frob_soup_session_cb               (SoupSession *session,
                                         SoupMessage *msg,
                                         gpointer     data);
static void
_get_auth_token_soup_session_cb         (SoupSession *session,
                                         SoupMessage *msg,
                                         gpointer     data);

/* Private API */

static void
fsp_session_set_property                (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  FspSession *self = FSP_SESSION (object);
  FspSessionPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_API_KEY:
      g_free (priv->api_key);
      priv->api_key = g_value_dup_string (value);
      break;
    case PROP_SECRET:
      g_free (priv->secret);
      priv->secret = g_value_dup_string (value);
      break;
    case PROP_TOKEN:
      fsp_session_set_token (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
fsp_session_get_property                (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  FspSession *self = FSP_SESSION (object);

  switch (prop_id)
    {
    case PROP_API_KEY:
      g_value_set_string (value, self->priv->api_key);
      break;
    case PROP_SECRET:
      g_value_set_string (value, self->priv->secret);
      break;
    case PROP_TOKEN:
      g_value_set_string (value, self->priv->token);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
fsp_session_dispose                     (GObject* object)
{
  FspSession *self = FSP_SESSION (object);

  /* Unref objects */
  if (self->priv->parser)
    {
      g_object_unref (self->priv->parser);
      self->priv->parser = NULL;
    }

  /* Unref object */
  if (self->priv->soup_session)
    {
      g_object_unref (self->priv->soup_session);
      self->priv->soup_session = NULL;
    }

  /* Call superclass */
  G_OBJECT_CLASS (fsp_session_parent_class)->dispose(object);
}

static void
fsp_session_finalize                    (GObject* object)
{
  FspSession *self = FSP_SESSION (object);

  /* Free memory */
  g_free (self->priv->api_key);
  g_free (self->priv->secret);
  g_free (self->priv->token);

  /* Call superclass */
  G_OBJECT_CLASS (fsp_session_parent_class)->finalize(object);
}

static void
fsp_session_class_init                  (FspSessionClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS(klass);
  GParamSpec *pspec = NULL;

  /* Install GOBject methods */
  obj_class->set_property = fsp_session_set_property;
  obj_class->get_property = fsp_session_get_property;
  obj_class->dispose = fsp_session_dispose;
  obj_class->finalize = fsp_session_finalize;

  /* Install properties */
  pspec = g_param_spec_string ("api-key", "api-key", "API key", NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (obj_class, PROP_API_KEY, pspec);

  pspec = g_param_spec_string ("secret", "secret", "Shared secret", NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (obj_class, PROP_SECRET, pspec);

  pspec = g_param_spec_string ("token", "token", "Authorized token", NULL,
                               G_PARAM_READWRITE);
  g_object_class_install_property (obj_class, PROP_TOKEN, pspec);

  /* Add private */
  g_type_class_add_private (obj_class, sizeof (FspSessionPrivate));
}

static void
fsp_session_init                        (FspSession *self)
{
  self->priv = FSP_SESSION_GET_PRIVATE (self);

  self->priv->api_key = NULL;
  self->priv->secret = NULL;
  self->priv->token = NULL;
  self->priv->frob = NULL;

  self->priv->parser = fsp_flickr_parser_get_instance ();
  self->priv->soup_session = soup_session_async_new ();
}

static SoupSession *
_get_soup_session                       (FspSession *self)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), NULL);
  return self->priv->soup_session;
}

static void
_get_frob_soup_session_cb               (SoupSession *session,
                                         SoupMessage *msg,
                                         gpointer data)
{
  g_assert (SOUP_IS_SESSION (session));
  g_assert (SOUP_IS_MESSAGE (msg));
  g_assert (data != NULL);

  GAsyncData *clos = NULL;
  FspSession *self = NULL;
  gchar *frob = NULL;
  GError *err = NULL;

  /* Get needed data from closure */
  clos = (GAsyncData *) data;
  self = FSP_SESSION (clos->object);

  /* Get value from response */
  if (!check_errors_on_soup_response (msg, &err))
    {
      frob = fsp_flickr_parser_get_frob (self->priv->parser,
                                         msg->response_body->data,
                                         (int) msg->response_body->length,
                                         &err);
    }

  /* Build response and call async callback */
  build_async_result_and_complete (clos, (gpointer) frob, err);
}

static void
_get_auth_token_soup_session_cb         (SoupSession *session,
                                         SoupMessage *msg,
                                         gpointer data)
{
  g_assert (SOUP_IS_SESSION (session));
  g_assert (SOUP_IS_MESSAGE (msg));
  g_assert (data != NULL);

  GAsyncData *clos = NULL;
  FspSession *self = NULL;
  FspDataAuthToken *auth_token = NULL;
  GError *err = NULL;

  /* Get needed data from closure */
  clos = (GAsyncData *) data;
  self = FSP_SESSION (clos->object);

  /* Get value from response */
  if (!check_errors_on_soup_response (msg, &err))
    {
      auth_token =
        fsp_flickr_parser_get_auth_token (self->priv->parser,
                                          msg->response_body->data,
                                          (int) msg->response_body->length,
                                          &err);
    }

  /* Build response and call async callback */
  build_async_result_and_complete (clos, (gpointer) auth_token, err);
}

/* from fsp-session-priv.h */
SoupSession *
fsp_session_get_soup_session            (FspSession *self)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), NULL);

  return _get_soup_session (self);
}

/* Public API */

FspSession *
fsp_session_new                         (const gchar *api_key,
                                         const gchar *secret,
                                         const gchar *token)
{
  g_return_val_if_fail (api_key != NULL, NULL);
  g_return_val_if_fail (secret != NULL, NULL);

  GObject *object = g_object_new(FSP_TYPE_SESSION,
                                 "api-key", api_key,
                                 "secret", secret,
                                 "token", token,
                                 NULL);
  return FSP_SESSION (object);
}

const gchar *
fsp_session_get_api_key                 (FspSession *self)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), NULL);

  return self->priv->api_key;
}

const gchar *
fsp_session_get_secret                  (FspSession *self)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), NULL);

  return self->priv->secret;
}

const gchar *
fsp_session_get_token                   (FspSession *self)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), NULL);

  return self->priv->token;
}

void
fsp_session_set_token                   (FspSession  *self,
                                         const gchar *token)
{
  g_return_if_fail (FSP_IS_SESSION (self));

  g_free (self->priv->token);
  self->priv->token = g_strdup (token);
}

/* Get authorization URL */

void
fsp_session_get_auth_url_async          (FspSession          *self,
                                         GCancellable        *c,
                                         GAsyncReadyCallback  cb,
                                         gpointer             data)
{
  g_return_if_fail (FSP_IS_SESSION (self));

  FspSessionPrivate *priv = self->priv;
  gchar *url = NULL;
  gchar *signed_query = NULL;

  /* Build the signed url */
  signed_query = get_signed_query (priv->secret,
                                   "method", "flickr.auth.getFrob",
                                   "api_key", priv->api_key,
                                   NULL);
  url = g_strdup_printf ("%s/?%s", FLICKR_API_BASE_URL, signed_query);
  g_free (signed_query);

  /* Perform the async request */
  perform_async_request (priv->soup_session, url,
                         _get_frob_soup_session_cb, G_OBJECT (self),
                         c, cb, fsp_session_get_auth_url_async, data);

  g_free (url);
}

gchar *
fsp_session_get_auth_url_finish         (FspSession    *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);

  gchar *frob = NULL;
  gchar *auth_url = NULL;

  /* Check for errors */
  if (!check_async_errors_on_finish (G_OBJECT (self), res,
                                     fsp_session_get_auth_url_async, error))
    {
      GSimpleAsyncResult *simple = NULL;
      gpointer result = NULL;

      /* Get result */
      simple = G_SIMPLE_ASYNC_RESULT (res);
      result = g_simple_async_result_get_op_res_gpointer (simple);
      if (result != NULL)
        frob = (gchar *) result;
      else
        g_set_error_literal (error, FSP_ERROR, FSP_ERROR_OTHER,
                             "Internal error");
    }

  /* Build the auth URL from the frob */
  if (frob != NULL)
    {
      FspSessionPrivate *priv = self->priv;
      gchar *signed_query = NULL;

      /* Save the frob */
      priv->frob = frob;

      /* Build the authorization url */
      signed_query = get_signed_query (priv->secret,
                                       "api_key", priv->api_key,
                                       "perms", "write",
                                       "frob", priv->frob,
                                       NULL);
      auth_url = g_strdup_printf ("http://flickr.com/services/auth/?%s",
                                  signed_query);
      g_free (signed_query);
    }

  return auth_url;
}

/* Complete authorization */

void
fsp_session_complete_auth_async         (FspSession          *self,
                                         GCancellable        *c,
                                         GAsyncReadyCallback  cb,
                                         gpointer             data)
{
  g_return_if_fail (FSP_IS_SESSION (self));
  g_return_if_fail (cb != NULL);

  FspSessionPrivate *priv = self->priv;
  gchar *signed_query = NULL;
  gchar *url = NULL;

  if (priv->frob != NULL)
    {
      /* Build the signed url */
      signed_query = get_signed_query (priv->secret,
                                       "method", "flickr.auth.getToken",
                                       "api_key", priv->api_key,
                                       "frob", priv->frob,
                                       NULL);
      url = g_strdup_printf ("%s/?%s", FLICKR_API_BASE_URL, signed_query);
      g_free (signed_query);

      /* Perform the async request */
      perform_async_request (priv->soup_session, url,
                             _get_auth_token_soup_session_cb, G_OBJECT (self), 
                             c, cb, fsp_session_complete_auth_async, data);

      g_free (url);
    }
  else
    {
      GError *err = NULL;

      /* Build and report error */
      err = g_error_new (FSP_ERROR, FSP_ERROR_NOFROB, "No frob defined");
      g_simple_async_report_gerror_in_idle (G_OBJECT (self),
                                            cb, data, err);
    }
}

gboolean
fsp_session_complete_auth_finish        (FspSession    *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
  g_return_val_if_fail (FSP_IS_SESSION (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

  FspDataAuthToken *auth_token = NULL;

  /* Check for errors */
  if (!check_async_errors_on_finish (G_OBJECT (self), res,
                                     fsp_session_complete_auth_async, error))
    {
      GSimpleAsyncResult *simple = NULL;
      gpointer result = NULL;

      /* Get result */
      simple = G_SIMPLE_ASYNC_RESULT (res);
      result = g_simple_async_result_get_op_res_gpointer (simple);
      if (result != NULL)
        auth_token = FSP_DATA_AUTH_TOKEN (result);
      else
        g_set_error_literal (error, FSP_ERROR, FSP_ERROR_OTHER,
                             "Internal error");
    }

  /* Complete the authorization saving the token if present */
  if (auth_token != NULL)
    {
      fsp_session_set_token (self, auth_token->token);
      fsp_data_free (FSP_DATA (auth_token));
      return TRUE;
    }

  return FALSE;
}
