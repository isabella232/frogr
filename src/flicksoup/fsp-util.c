/*
 * fsp-util.c
 *
 * Copyright (C) 2010 Mario Sanchez Prada
 * Authors: Mario Sanchez Prada <msanchez@igalia.com>
 *
 * Some parts of this file were based on source code from the libsoup
 * library libsoup, licensed as LGPLv2 (Copyright 2008 Red Hat, Inc.)
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

#include "fsp-util.h"

#include "fsp-error.h"

#include <stdarg.h>
#include <libsoup/soup.h>

typedef struct
{
  GCancellable        *cancellable;
  gulong               cancellable_id;
} GCancellableData;

static GHashTable *
_get_params_table_from_valist           (const gchar *first_param,
                                         va_list      args)
{
  g_return_val_if_fail (first_param != NULL, NULL);
  g_return_val_if_fail (args != NULL, NULL);

  GHashTable *table = NULL;
  gchar *p, *v;

  table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                 (GDestroyNotify)g_free,
                                 (GDestroyNotify)g_free);

  /* Fill the hash table */
  for (p = (gchar *) first_param; p; p = va_arg (args, gchar*))
    {
      v = va_arg (args, gchar*);

      /* Ignore parameter with no value */
      if (v != NULL)
        g_hash_table_insert (table, g_strdup (p), g_strdup (v));
      else
        g_debug ("Missing value for %s. Ignoring parameter.", p);
    }

  return table;
}

/* This function is based in append_form_encoded() from libsoup's
   SoupForm, licensed as LGPLv2 (Copyright 2008 Red Hat, Inc.) */
static gchar *
_encode_query_value (const char *value)
{
  GString *result = NULL;
  const unsigned char *str = NULL;

  result = g_string_new ("");
  str = (const unsigned char *) value;

  while (*str) {
    if (*str == ' ') {
      g_string_append_c (result, '+');
      str++;
    } else if (!g_ascii_isalnum (*str))
      g_string_append_printf (result, "%%%02X", (int)*str++);
    else
      g_string_append_c (result, *str++);
  }

  return g_string_free (result, FALSE);
}

static gchar *
_get_signed_query_with_params           (const gchar      *api_sig,
                                         GHashTable       *params_table)
{
  g_return_val_if_fail (params_table != NULL, NULL);
  g_return_val_if_fail (api_sig != NULL, NULL);

  GList *keys = NULL;
  gchar *retval = NULL;

  /* Get ownership of the table */
  g_hash_table_ref (params_table);

  /* Get a list of keys */
  keys = g_hash_table_get_keys (params_table);
  if (keys != NULL)
    {
      gchar **url_params_array = NULL;
      GList *k = NULL;
      gint i = 0;

      /* Build gchar** arrays for building the final
         string to be used as the list of GET params */
      url_params_array = g_new0 (gchar*, g_list_length (keys) + 2);

      /* Fill arrays */
      for (k = keys; k; k = g_list_next (k))
        {
          gchar *key = (gchar*) k->data;
          gchar *value = g_hash_table_lookup (params_table, key);
          gchar *actual_value = NULL;

          /* Do not encode basic pairs key-value */
          if (g_strcmp0 (key, "api_key") && g_strcmp0 (key, "auth_token")
              && g_strcmp0 (key, "method") && g_strcmp0 (key, "frob"))
            actual_value = _encode_query_value (value);
          else
            actual_value = g_strdup (value);

          url_params_array[i++] = g_strdup_printf ("%s=%s", key, actual_value);
          g_free (actual_value);
        }

      /* Add those to the params array (space previously reserved) */
      url_params_array[i] = g_strdup_printf ("api_sig=%s", api_sig);

      /* Build the signed query */
      retval = g_strjoinv ("&", url_params_array);

      /* Free */
      g_strfreev (url_params_array);
    }
  g_list_free (keys);
  g_hash_table_unref (params_table);

  return retval;
}

static gboolean
_disconnect_cancellable_on_idle (GCancellableData *clos)
{
  GCancellable *cancellable = NULL;
  gulong cancellable_id = 0;

  /* Get data from closure, and free it */
  cancellable = clos->cancellable;
  cancellable_id = clos->cancellable_id;
  g_slice_free (GCancellableData, clos);

  /* Disconnect from the "cancelled" signal if needed */
  if (cancellable)
    {
      g_cancellable_disconnect (cancellable, cancellable_id);
      g_object_unref (cancellable);
    }

  return FALSE;
}

static gboolean
_check_errors_on_soup_response           (SoupMessage  *msg,
                                          GError      **error)
{
  g_assert (SOUP_IS_MESSAGE (msg));

  GError *err = NULL;

  /* Check non-succesful SoupMessage's only */
  if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code))
    {
      if (msg->status_code == SOUP_STATUS_CANCELLED)
        err = g_error_new (FSP_ERROR, FSP_ERROR_CANCELLED,
                           "Cancelled by user");
      else if (SOUP_STATUS_IS_CLIENT_ERROR (msg->status_code))
        err = g_error_new (FSP_ERROR, FSP_ERROR_CLIENT_ERROR,
                           "Bad request");
      else if (SOUP_STATUS_IS_SERVER_ERROR (msg->status_code))
        err = g_error_new (FSP_ERROR, FSP_ERROR_SERVER_ERROR,
                           "Server error");
      else
        err = g_error_new (FSP_ERROR, FSP_ERROR_NETWORK_ERROR,
                           "Network error");
    }

  /* Propagate error */
  if (err != NULL)
    g_propagate_error (error, err);

  /* Return result */
  return (err != NULL);
}

static gboolean
_check_async_errors_on_finish           (GObject       *object,
                                         GAsyncResult  *res,
                                         gpointer       source_tag,
                                         GError       **error)
{
  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

  gboolean errors_found = TRUE;

  if (g_simple_async_result_is_valid (res, object, source_tag))
    {
      GSimpleAsyncResult *simple = NULL;

      /* Check error */
      simple = G_SIMPLE_ASYNC_RESULT (res);
      if (!g_simple_async_result_propagate_error (simple, error))
	errors_found = FALSE;
    }
  else
    g_set_error_literal (error, FSP_ERROR, FSP_ERROR_OTHER, "Internal error");

  return errors_found;
}

gchar *
get_api_signature_from_hash_table       (const gchar *shared_secret,
                                         GHashTable  *params_table)
{
  g_return_val_if_fail (shared_secret != NULL, NULL);
  g_return_val_if_fail (params_table != NULL, NULL);

  GList *keys = NULL;
  gchar *api_sig = NULL;

  /* Get ownership of the table */
  g_hash_table_ref (params_table);

  /* Get a list of keys */
  keys = g_hash_table_get_keys (params_table);
  if (keys != NULL)
    {
      gchar **sign_str_array = NULL;
      gchar *sign_str = NULL;
      GList *k = NULL;
      gint i = 0;

      /* Sort the list */
      keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);

      /* Build gchar** arrays for building the signature string */
      sign_str_array = g_new0 (gchar*, (2 * g_list_length (keys)) + 2);

      /* Fill arrays */
      sign_str_array[i++] = g_strdup (shared_secret);
      for (k = keys; k; k = g_list_next (k))
        {
          const gchar *key = (gchar*) k->data;
          const gchar *value = g_hash_table_lookup (params_table, key);

          sign_str_array[i++] = g_strdup (key);
          sign_str_array[i++] = g_strdup (value);
        }
      sign_str_array[i] = NULL;

      /* Get the signature string and calculate the api_sig value */
      sign_str = g_strjoinv (NULL, sign_str_array);
      api_sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sign_str, -1);

      /* Free */
      g_free (sign_str);
      g_strfreev (sign_str_array);
    }

  g_list_free (keys);
  g_hash_table_unref (params_table);

  return api_sig;
}

/**
 * get_signed_query:
 * @shared_secret: secret associated to the Flickr API key being used
 * @first_param: key of the first parameter
 * @...: value for the first parameter, followed optionally by more
 *  key/value parameters pairs, followed by %NULL
 *
 * Gets a signed query part for a given set of pairs
 * key/value. The returned string should be freed with g_free() 
 * when no longer needed.
 *
 * Returns: a newly-allocated @str with the signed query
 */
gchar *
get_signed_query                        (const gchar *shared_secret,
                                         const gchar *first_param,
                                         ... )
{
  g_return_val_if_fail (shared_secret != NULL, NULL);
  g_return_val_if_fail (first_param != NULL, NULL);

  va_list args;
  GHashTable *table = NULL;
  gchar *api_sig = NULL;
  gchar *retval = NULL;

  va_start (args, first_param);

  /* Get the hash table for the params and the API signature from it */
  table = _get_params_table_from_valist (first_param, args);
  api_sig = get_api_signature_from_hash_table (shared_secret, table);

  /* Get the signed URL with the needed params */
  if ((table != NULL) && (api_sig != NULL))
    retval = _get_signed_query_with_params (api_sig, table);

  g_hash_table_unref (table);
  g_free (api_sig);

  va_end (args);

  return retval;
}

void
perform_async_request                   (SoupSession         *soup_session,
                                         const gchar         *url,
                                         SoupSessionCallback  request_cb,
                                         GObject             *source_object,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             source_tag,
                                         gpointer             data)
{
  g_return_if_fail (SOUP_IS_SESSION (soup_session));
  g_return_if_fail (url != NULL);
  g_return_if_fail (request_cb != NULL);
  g_return_if_fail (callback != NULL);

  AsyncRequestData *clos = NULL;
  SoupMessage *msg = NULL;

  /* Build and queue the message */
  msg = soup_message_new (SOUP_METHOD_GET, url);

  /* Save important data for the callback */
  clos = g_slice_new0 (AsyncRequestData);
  clos->object = source_object;
  clos->soup_session = soup_session;
  clos->soup_message = msg;
  clos->cancellable = cancellable;
  clos->callback = callback;
  clos->source_tag = source_tag;
  clos->data = data;

  /* Connect to the "cancelled" signal thread safely */
  if (clos->cancellable)
    {
      clos->cancellable_id =
        g_cancellable_connect (clos->cancellable,
                               G_CALLBACK (soup_session_cancelled_cb),
                               clos,
                               NULL);
    }

  /* Queue the message */
  soup_session_queue_message (soup_session, msg, request_cb, clos);

  g_debug ("\nRequested URL:\n%s\n", url);
}

void
soup_session_cancelled_cb               (GCancellable *cancellable,
                                         gpointer      data)
{
  AsyncRequestData *clos = NULL;
  GObject *object = NULL;
  SoupSession *session = NULL;
  SoupMessage *message = NULL;

  clos = (AsyncRequestData *) data;

  /* Get data from closure, and free it */
  object = clos->object;
  session = clos->soup_session;
  message = clos->soup_message;

  soup_session_cancel_message (session, message, SOUP_STATUS_CANCELLED);

  g_debug ("Remote request cancelled!");
}

void
handle_soup_response                    (SoupMessage   *msg,
                                         FspParserFunc  parserFunc,
                                         gpointer       data)
{
  g_assert (SOUP_IS_MESSAGE (msg));
  g_assert (parserFunc != NULL);
  g_assert (data != NULL);

  FspParser *parser = NULL;
  AsyncRequestData *clos = NULL;
  gpointer result = NULL;
  GError *err = NULL;
  gchar *response_str = NULL;
  gulong response_len = 0;

  parser = fsp_parser_get_instance ();
  clos = (AsyncRequestData *) data;

  response_str = g_strndup (msg->response_body->data, msg->response_body->length);
  response_len = (ulong) msg->response_body->length;
  if (response_str)
    g_debug ("\nResponse got:\n%s\n", response_str);

  /* Get value from response */
  if (!_check_errors_on_soup_response (msg, &err))
    result = parserFunc (parser, response_str, response_len, &err);

  /* Build response and call async callback */
  build_async_result_and_complete (clos, result, err);

  g_free (response_str);
}

void
build_async_result_and_complete         (AsyncRequestData *clos,
                                         gpointer    result,
                                         GError     *error)
{
  g_assert (clos != NULL);

  GSimpleAsyncResult *res = NULL;
  GObject *object = NULL;
  GCancellableData *cancellable_data = NULL;
  GCancellable *cancellable = NULL;
  gulong cancellable_id = 0;
  GAsyncReadyCallback  callback = NULL;
  gpointer source_tag;
  gpointer data;

  /* Get data from closure, and free it */
  object = clos->object;
  cancellable = clos->cancellable;
  cancellable_id = clos->cancellable_id;
  callback = clos->callback;
  source_tag = clos->source_tag;
  data = clos->data;
  g_slice_free (AsyncRequestData, clos);

  /* Make sure the "cancelled" signal gets disconnected in another
     iteration of the main loop to avoid a dead-lock with itself */
  cancellable_data = g_slice_new0 (GCancellableData);
  cancellable_data->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
  cancellable_data->cancellable_id = cancellable_id;

  g_idle_add ((GSourceFunc) _disconnect_cancellable_on_idle, cancellable_data);

  /* Build response and call async callback */
  res = g_simple_async_result_new (object, callback,
                                   data, source_tag);

  /* Return the given value or an error otherwise */
  if (error != NULL)
    g_simple_async_result_set_from_error (res, error);
  else
    g_simple_async_result_set_op_res_gpointer (res, result, NULL);

  /* Execute the callback */
  g_simple_async_result_complete_in_idle (res);
}

gpointer
finish_async_request                    (GObject       *object,
                                         GAsyncResult  *res,
                                         gpointer       source_tag,
                                         GError       **error)
{
  g_return_val_if_fail (G_IS_OBJECT (object), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);

  gpointer retval = NULL;

  /* Check for errors */
  if (!_check_async_errors_on_finish (object, res, source_tag, error))
    {
      GSimpleAsyncResult *simple = NULL;
      gpointer result = NULL;

      /* Get result */
      simple = G_SIMPLE_ASYNC_RESULT (res);
      result = g_simple_async_result_get_op_res_gpointer (simple);
      if (result != NULL)
        retval = result;
      else
        g_set_error_literal (error, FSP_ERROR, FSP_ERROR_OTHER,
                             "Internal error");
    }

  return retval;
}
