/*
 * frogr-picture-loader.c -- Asynchronous picture loader in frogr
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

#include <gio/gio.h>
#include "frogr-controller.h"
#include "frogr-main-view.h"
#include "frogr-picture.h"
#include "frogr-picture-loader.h"

#define PICTURE_WIDTH 100
#define PICTURE_HEIGHT 100

#define FROGR_PICTURE_LOADER_GET_PRIVATE(object)                \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object),                       \
                                FROGR_TYPE_PICTURE_LOADER,      \
                                FrogrPictureLoaderPrivate))

G_DEFINE_TYPE (FrogrPictureLoader, frogr_picture_loader, G_TYPE_OBJECT);

/* Private struct */
typedef struct _FrogrPictureLoaderPrivate FrogrPictureLoaderPrivate;
struct _FrogrPictureLoaderPrivate
{
  FrogrMainView *mainview;
  GSList *filepaths;
  GSList *current;
  guint index;
  guint n_pictures;

  GFunc picture_loaded_cb;
  GFunc pictures_loaded_cb;
  gpointer object;
};

static const gchar *valid_mimetypes[] = {
  "image/jpg",
  "image/jpeg",
  "image/png",
  "image/bmp",
  "image/gif",
  NULL};

/* Prototypes */

static void _update_status_and_progress (FrogrPictureLoader *self);
static GdkPixbuf *_get_scaled_pixbuf (GdkPixbuf *pixbuf);
static void _load_next_picture (FrogrPictureLoader *self);
static void _load_next_picture_cb (GObject *object,
                                   GAsyncResult *res,
                                   gpointer data);

/* Private API */

static void
_update_status_and_progress (FrogrPictureLoader *self)
{
  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (self);

  gchar *status_text = NULL;
  gchar *progress_bar_text = NULL;

  if (priv->current)
    {
      const gchar *filepath = (const gchar *)priv->current->data;
      gchar *filename = g_path_get_basename (filepath);

      /* Update progress */
      status_text = g_strdup_printf ("Loading '%s'...", filename);
      progress_bar_text = g_strdup_printf ("%d / %d",
                                           priv->index,
                                           priv->n_pictures);
      g_free (filename);
    }

  frogr_main_view_set_status_text (priv->mainview, status_text);
  frogr_main_view_set_progress (priv->mainview,
                                  (double) priv->index / priv->n_pictures,
                                  progress_bar_text);
  /* Free */
  g_free (status_text);
  g_free (progress_bar_text);
}

static GdkPixbuf *
_get_scaled_pixbuf (GdkPixbuf *pixbuf)
{
  GdkPixbuf *scaled_pixbuf = NULL;
  gint width;
  gint height;
  gint new_width;
  gint new_height;

  /* Look for the right side to reduce */
  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  if (width > height)
    {
      new_width = PICTURE_WIDTH;
      new_height = (float)new_width * height / width;
    }
  else
    {
      new_height = PICTURE_HEIGHT;
      new_width = (float)new_height * width / height;
    }

  /* Scale the pixbuf to its best size */
  scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf,
                                           new_width, new_height,
                                           GDK_INTERP_TILES);
  return scaled_pixbuf;
}

static void
_load_next_picture (FrogrPictureLoader *self)
{
  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (self);

  if (priv->current)
    {
      GFile *gfile = NULL;
      GFileInfo *file_info;
      gchar *filepath = (gchar *)priv->current->data;
      const gchar *mime_type;
      gboolean valid_mime = FALSE;
      gint i;

      /* Get file info */
      gfile = g_file_new_for_path (filepath);
      file_info = g_file_query_info (gfile,
                                     G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                     G_FILE_QUERY_INFO_NONE,
                                     NULL,
                                     NULL);
      /* Check mimetype */
      mime_type = g_file_info_get_content_type (file_info);
      for (i = 0; valid_mimetypes[i]; i++)
        {
          if (g_str_equal (valid_mimetypes[i], mime_type))
            {
              valid_mime = TRUE;
              break;
            }
        }

      g_debug ("Adding file %s (%s)\n", filepath, mime_type);
      g_object_unref (file_info);

      /* Asynchronously load the picture if mime is valid */
      if (valid_mime)
        {
          g_file_load_contents_async (gfile,
                                      NULL,
                                      _load_next_picture_cb,
                                      self);
        }
      else
        {
          /* update internal status and check the next picture */
          priv->current = g_slist_next (priv->current);
          priv->index++;
          _load_next_picture (self);
        }
    }
  else
    {
      /* Update status and progress bars */
      _update_status_and_progress (self);

      /* Set proper state */
      frogr_main_view_set_state (priv->mainview, FROGR_STATE_IDLE);

      /* Execute final callback */
      if (priv->pictures_loaded_cb)
        priv->pictures_loaded_cb (priv->object, self);
    }
}

static void
_load_next_picture_cb (GObject *object,
                       GAsyncResult *res,
                       gpointer data)
{
  FrogrPictureLoader *self = FROGR_PICTURE_LOADER (data);;
  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (self);

  FrogrPicture *fpicture = NULL;
  GFile *file = G_FILE (object);
  GError *error = NULL;
  gchar *contents;
  gsize length;

  if (g_file_load_contents_finish (file, res, &contents, &length, NULL, &error))
    {
      GdkPixbufLoader *pixbuf_loader = gdk_pixbuf_loader_new ();

      if (gdk_pixbuf_loader_write (pixbuf_loader,
                                   (const guchar *)contents,
                                   length,
                                   &error))
        {
          GdkPixbuf *pixbuf;
          GdkPixbuf *s_pixbuf;
          gchar *filepath;
          gchar *filename;

          /* Gather needed information */
          filepath = g_file_get_path (file);
          filename = g_file_get_basename (file);
          gdk_pixbuf_loader_close (pixbuf_loader, NULL);
          pixbuf = gdk_pixbuf_loader_get_pixbuf (pixbuf_loader);

          /* Get (scaled) pixbuf */
          s_pixbuf = _get_scaled_pixbuf (pixbuf);

          /* Build the FrogrPicture and set pixbuf */
          fpicture = frogr_picture_new (filepath, filename, FALSE);
          frogr_picture_set_pixbuf (fpicture, s_pixbuf);

          /* Free */
          g_object_unref (s_pixbuf);
          g_free (filepath);
          g_free (filename);
        }
      else
        {
          /* Not able to write pixbuf */
          g_warning ("Not able to write pixbuf: %s",
                     error->message);
          g_error_free (error);
        }

      g_object_unref (pixbuf_loader);
    }
  else
    {
      /* Not able to load contents */
      gchar *filename = g_file_get_basename (file);
      g_warning ("Not able to read contents from %s: %s",
                 filename,
                 error->message);
      g_error_free (error);
      g_free (filename);
    }

  /* Update internal status */
  priv->current = g_slist_next (priv->current);
  priv->index++;

  /* Update status and progress bars */
  _update_status_and_progress (self);

  /* Execute 'picture-loaded' callback */
  if (priv->picture_loaded_cb && fpicture)
    priv->picture_loaded_cb (priv->object, fpicture);

  /* Free memory */
  g_free (contents);
  if (fpicture != NULL)
    g_object_unref (fpicture);

  /* Go for the next picture */
  _load_next_picture (self);
}

static void
_frogr_picture_loader_finalize (GObject* object)
{
  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (object);

  /* Free */
  g_object_unref (priv->mainview);
  g_slist_foreach (priv->filepaths, (GFunc)g_free, NULL);
  g_slist_free (priv->filepaths);

  G_OBJECT_CLASS (frogr_picture_loader_parent_class)->finalize(object);
}

static void
frogr_picture_loader_class_init(FrogrPictureLoaderClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS(klass);
  obj_class->finalize = _frogr_picture_loader_finalize;
  g_type_class_add_private (obj_class, sizeof (FrogrPictureLoaderPrivate));
}

static void
frogr_picture_loader_init (FrogrPictureLoader *self)
{
  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (self);

  FrogrController *controller = NULL;

  /* Init private data */

  /* We need the controller to get the main window */
  controller = frogr_controller_get_instance ();
  priv->mainview = frogr_controller_get_main_view (controller);
  g_object_unref (controller);

  /* Init the rest of private data */
  priv->filepaths = NULL;
  priv->current = NULL;
  priv->index = -1;
  priv->n_pictures = 0;
}

/* Public API */

FrogrPictureLoader *
frogr_picture_loader_new (GSList *filepaths,
                          GFunc picture_loaded_cb,
                          GFunc pictures_loaded_cb,
                          gpointer object)
{
  FrogrPictureLoader *self =
    FROGR_PICTURE_LOADER (g_object_new(FROGR_TYPE_PICTURE_LOADER, NULL));

  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (self);

  /* Internal data */
  priv->filepaths = g_slist_copy (filepaths);
  priv->current = priv->filepaths;
  priv->index = 0;
  priv->n_pictures = g_slist_length (priv->filepaths);

  /* Callback data */
  priv->picture_loaded_cb = picture_loaded_cb;
  priv->pictures_loaded_cb = pictures_loaded_cb;
  priv->object = object;

  return self;
}

void
frogr_picture_loader_load (FrogrPictureLoader *self)
{
  g_return_if_fail (FROGR_IS_PICTURE_LOADER (self));

  FrogrPictureLoaderPrivate *priv =
    FROGR_PICTURE_LOADER_GET_PRIVATE (self);

  /* Check first whether there's something to load */
  if (priv->filepaths == NULL)
    return;

  /* Set proper state */
  frogr_main_view_set_state (priv->mainview, FROGR_STATE_LOADING);

  /* Update status and progress bars */
  _update_status_and_progress (self);

  /* Trigger the asynchronous process */
  _load_next_picture (self);
}
