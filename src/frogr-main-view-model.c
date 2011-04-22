/*
 * frogr-main-view-model.c -- Main view model in frogr
 *
 * Copyright (C) 2009-2011 Mario Sanchez Prada
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

#include "frogr-main-view-model.h"

#include "frogr-account.h"

#define TAGS_DELIMITER " "

#define FROGR_MAIN_VIEW_MODEL_GET_PRIVATE(object)               \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object),                       \
                                FROGR_TYPE_MAIN_VIEW_MODEL,     \
                                FrogrMainViewModelPrivate))

G_DEFINE_TYPE (FrogrMainViewModel, frogr_main_view_model, G_TYPE_OBJECT);

/* Private struct */
typedef struct _FrogrMainViewModelPrivate FrogrMainViewModelPrivate;
struct _FrogrMainViewModelPrivate
{
  GSList *pictures_list;
  guint n_pictures;

  GSList *sets_list;
  guint n_sets;

  GSList *groups_list;
  guint n_groups;

  GSList *tags_list;
  guint n_tags;

  gchar* account_description;
};

/* Signals */
enum {
  PICTURE_ADDED,
  PICTURE_REMOVED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* Private API */

static void
_frogr_main_view_model_dispose (GObject* object)
{
  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (object);

  if (priv->pictures_list)
    {
      g_slist_foreach (priv->pictures_list, (GFunc)g_object_unref, NULL);
      g_slist_free (priv->pictures_list);
      priv->pictures_list = NULL;
    }

  if (priv->sets_list)
    {
      g_slist_foreach (priv->sets_list, (GFunc)g_object_unref, NULL);
      g_slist_free (priv->sets_list);
      priv->sets_list = NULL;
    }

  if (priv->groups_list)
    {
      g_slist_foreach (priv->groups_list, (GFunc)g_object_unref, NULL);
      g_slist_free (priv->groups_list);
      priv->groups_list = NULL;
    }

  if (priv->tags_list)
    {
      g_slist_foreach (priv->tags_list, (GFunc)g_free, NULL);
      g_slist_free (priv->tags_list);
      priv->tags_list = NULL;
    }

  G_OBJECT_CLASS (frogr_main_view_model_parent_class)->dispose (object);
}

static void
_frogr_main_view_model_finalize (GObject* object)
{
  FrogrMainViewModelPrivate *priv = FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (object);
  g_free (priv->account_description);
  G_OBJECT_CLASS (frogr_main_view_model_parent_class)->finalize (object);
}

static void
frogr_main_view_model_class_init(FrogrMainViewModelClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS(klass);
  obj_class->dispose = _frogr_main_view_model_dispose;
  obj_class->finalize = _frogr_main_view_model_finalize;

  signals[PICTURE_ADDED] =
    g_signal_new ("picture-added",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, FROGR_TYPE_PICTURE);

  signals[PICTURE_REMOVED] =
    g_signal_new ("picture-removed",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, FROGR_TYPE_PICTURE);

  g_type_class_add_private (obj_class, sizeof (FrogrMainViewModelPrivate));
}

static void
frogr_main_view_model_init (FrogrMainViewModel *self)
{
  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  /* Init private data */
  priv->pictures_list = NULL;
  priv->n_pictures = 0;

  priv->sets_list = NULL;
  priv->n_sets = 0;

  priv->groups_list = NULL;
  priv->n_groups = 0;

  priv->tags_list = NULL;
  priv->n_tags = 0;

  priv->account_description = NULL;
}

/* Public API */

FrogrMainViewModel *
frogr_main_view_model_new (void)
{
  GObject *new = g_object_new(FROGR_TYPE_MAIN_VIEW_MODEL, NULL);
  return FROGR_MAIN_VIEW_MODEL (new);
}

void
frogr_main_view_model_add_picture (FrogrMainViewModel *self,
                                   FrogrPicture *picture)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));
  g_return_if_fail(FROGR_IS_PICTURE (picture));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  g_object_ref (picture);
  priv->pictures_list = g_slist_append (priv->pictures_list, picture);
  priv->n_pictures++;

  g_signal_emit (self, signals[PICTURE_ADDED], 0, picture);
}

void
frogr_main_view_model_remove_picture (FrogrMainViewModel *self,
                                      FrogrPicture *picture)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  priv->pictures_list = g_slist_remove (priv->pictures_list, picture);
  priv->n_pictures--;
  g_object_unref (picture);

  g_signal_emit (self, signals[PICTURE_REMOVED], 0, picture);
}

guint
frogr_main_view_model_n_pictures (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), 0);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->n_pictures;
}

GSList *
frogr_main_view_model_get_pictures (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), NULL);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->pictures_list;
}

void
frogr_main_view_model_add_set (FrogrMainViewModel *self,
                               FrogrPhotoSet *set)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));
  g_return_if_fail(FROGR_IS_SET (set));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  /* When adding one by one we prepend always to keep the order */
  priv->sets_list = g_slist_prepend (priv->sets_list, set);
  g_object_ref (set);

  priv->n_sets++;
}

void
frogr_main_view_model_remove_set (FrogrMainViewModel *self,
                                  FrogrPhotoSet *set)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  priv->sets_list = g_slist_remove (priv->sets_list, set);
  priv->n_sets--;
  g_object_unref (set);
}

void
frogr_main_view_model_remove_all_sets (FrogrMainViewModel *self)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  g_slist_foreach (priv->sets_list, (GFunc)g_object_unref, NULL);
  g_slist_free (priv->sets_list);

  priv->sets_list = NULL;
  priv->n_sets = 0;
}

guint
frogr_main_view_model_n_sets (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), 0);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->n_sets;
}

GSList *
frogr_main_view_model_get_sets (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), NULL);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->sets_list;
}

void
frogr_main_view_model_set_sets (FrogrMainViewModel *self,
                                GSList *sets_list)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv = NULL;
  FrogrPicture *picture = NULL;
  GSList *item = NULL;

  frogr_main_view_model_remove_all_sets (self);

  priv = FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);
  priv->sets_list = sets_list;
  priv->n_sets = g_slist_length (sets_list);

  /* Remove all the sets attached to every picture */
  for (item = priv->pictures_list; item; item = g_slist_next (item))
    {
      picture = FROGR_PICTURE (item->data);
      frogr_picture_remove_sets (picture);
    }
}

void
frogr_main_view_model_add_group (FrogrMainViewModel *self,
                                 FrogrGroup *group)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));
  g_return_if_fail(FROGR_IS_GROUP (group));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  g_object_ref (group);
  priv->groups_list = g_slist_append (priv->groups_list, group);
  priv->n_groups++;
}

void
frogr_main_view_model_remove_group (FrogrMainViewModel *self,
                                    FrogrGroup *group)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  priv->groups_list = g_slist_remove (priv->groups_list, group);
  priv->n_groups--;
  g_object_unref (group);
}

void
frogr_main_view_model_remove_all_groups (FrogrMainViewModel *self)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  g_slist_foreach (priv->groups_list, (GFunc)g_object_unref, NULL);
  g_slist_free (priv->groups_list);

  priv->groups_list = NULL;
  priv->n_groups = 0;
}

guint
frogr_main_view_model_n_groups (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), 0);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->n_groups;
}

GSList *
frogr_main_view_model_get_groups (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), NULL);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->groups_list;
}

void
frogr_main_view_model_set_groups (FrogrMainViewModel *self,
                                  GSList *groups_list)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv = NULL;
  FrogrPicture *picture = NULL;
  GSList *item = NULL;

  frogr_main_view_model_remove_all_groups (self);

  priv = FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);
  priv->groups_list = groups_list;
  priv->n_groups = g_slist_length (groups_list);

  /* Remove all the groups attached to every picture */
  for (item = priv->pictures_list; item; item = g_slist_next (item))
    {
      picture = FROGR_PICTURE (item->data);
      frogr_picture_remove_groups (picture);
    }
}

GSList *
frogr_main_view_model_get_tags_list (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), NULL);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->tags_list;
}

void
frogr_main_view_model_set_tags_list (FrogrMainViewModel *self, GSList *tags_list)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv = NULL;

  frogr_main_view_model_remove_all_tags (self);

  priv = FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);
  priv->tags_list = tags_list;
  priv->n_tags = g_slist_length (tags_list);
}

void
frogr_main_view_model_remove_all_tags (FrogrMainViewModel *self)
{
  g_return_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  g_slist_foreach (priv->tags_list, (GFunc)g_free, NULL);
  g_slist_free (priv->tags_list);

  priv->tags_list = NULL;
  priv->n_tags = 0;
}

guint
frogr_main_view_model_n_tags (FrogrMainViewModel *self)
{
  g_return_val_if_fail(FROGR_IS_MAIN_VIEW_MODEL (self), 0);

  FrogrMainViewModelPrivate *priv =
    FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);

  return priv->n_tags;
}

const gchar *
frogr_main_view_model_get_account_description (FrogrMainViewModel *self)
{
  g_return_val_if_fail (FROGR_IS_MAIN_VIEW_MODEL (self), NULL);

  FrogrMainViewModelPrivate *priv = FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);
  return priv->account_description;
}

void
frogr_main_view_model_set_account_description (FrogrMainViewModel *self,
                                               const gchar *description)
{
  g_return_if_fail (FROGR_IS_MAIN_VIEW_MODEL (self));

  FrogrMainViewModelPrivate *priv = FROGR_MAIN_VIEW_MODEL_GET_PRIVATE (self);
  if (priv->account_description)
    g_free (priv->account_description);

  priv->account_description = g_strdup (description);
}
