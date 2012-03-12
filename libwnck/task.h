/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * libwnck
 * Copyright (C) Sebastian Geiger 2012 <sbastig@gmx.net>
 * 
 * libwnck is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * libwnck is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 */

#ifndef _TASK_H_
#define _TASK_H_

#include <glib-object.h>
#include <gtk/gtk.h>
#include <libwnck/screen.h>

#include "tasklist.h"
#include "private.h"

G_BEGIN_DECLS

#define WNCK_TYPE_TASK             (wnck_task_get_type ())
#define WNCK_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), WNCK_TYPE_TASK, WnckTask))
#define WNCK_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), WNCK_TYPE_TASK, WnckTaskClass))
#define WNCK_IS_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), WNCK_TYPE_TASK))
#define WNCK_IS_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), WNCK_TYPE_TASK))
#define WNCK_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), WNCK_TYPE_TASK, WnckTaskClass))

typedef struct _WnckTaskClass WnckTaskClass;
typedef struct _WnckTask WnckTask;

struct _WnckTaskClass {
  GObjectClass parent_class;
};

typedef enum {
  WNCK_TASK_CLASS_GROUP,
  WNCK_TASK_WINDOW,
  WNCK_TASK_STARTUP_SEQUENCE
} WnckTaskType;

struct _WnckTask {
  GObject parent_instance;

  WnckTasklist *tasklist; // TODO: do we need a reference to the task list?

  GtkWidget *button;
  GtkWidget *image;
  GtkWidget *label;

  WnckTaskType type;

  WnckClassGroup *class_group;
  WnckWindow *window;
#ifdef HAVE_STARTUP_NOTIFICATION
  SnStartupSequence *startup_sequence;
#endif

  gdouble grouping_score;

  GList *windows; /* List of the WnckTask for the window,
		     if this is a class group */
  guint state_changed_tag;
  guint icon_changed_tag;
  guint name_changed_tag;
  guint class_name_changed_tag;
  guint class_icon_changed_tag;

  /* task menu */
  GtkWidget *menu;
  /* ops menu */
  GtkWidget *action_menu;

  guint really_toggling : 1; /* Set when tasklist really wants
                              * to change the togglebutton state
                              */
  guint was_active : 1;      /* used to fixup activation behavior */

  guint button_activate;

  guint32 dnd_timestamp;

  time_t  start_needs_attention;
  gdouble glow_start_time;
  gdouble glow_factor;

  guint button_glow;

  guint row;
  guint col;
};

GType wnck_task_get_type (void) G_GNUC_CONST;

GList *
wnck_task_get_highest_scored (GList     *ungrouped_class_groups,
			      WnckTask **class_group_task_out);

gint       wnck_task_compare_alphabetically (gconstpointer a, gconstpointer b);
gint       wnck_task_compare (gconstpointer  a, gconstpointer  b);
WnckTask  *wnck_task_new_from_window (WnckTasklist    *tasklist,
                                      WnckWindow      *window);
WnckTask  *wnck_task_new_from_class_group (WnckTasklist    *tasklist,
						 WnckClassGroup  *class_group);
void       wnck_task_state_changed        (WnckWindow      *window,
                                                  WnckWindowState  changed_mask,
                                                  WnckWindowState  new_state,
                                                  gpointer         data);
void       wnck_task_update_visible_state (WnckTask *task);
void       wnck_task_create_widgets (WnckTask *task, GtkReliefStyle relief);
void       wnck_task_position_menu (GtkMenu   *menu,
			 gint      *x,
			 gint      *y,
			 gboolean  *push_in,
			 gpointer   user_data);
void
remove_startup_sequences_for_window (WnckTasklist *tasklist,
                                     WnckWindow   *window);

#ifdef HAVE_STARTUP_NOTIFICATION
WnckTask *wnck_task_new_from_startup_sequence (WnckTasklist      *tasklist,
                                                      SnStartupSequence *sequence);
#endif
void wnck_task_update_visible_state (WnckTask *task);

G_END_DECLS

#endif /* _TASK_H_ */
