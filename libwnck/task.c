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

#include <config.h>

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <glib/gi18n-lib.h>

#include "task.h"
#include "class-group.h"
#include "window-action-menu.h"
#include "private.h"

#define MINI_ICON_SIZE DEFAULT_MINI_ICON_WIDTH
#define TASKLIST_BUTTON_PADDING 4

#define ARROW_SPACE 4
#define ARROW_SIZE 12
#define INDICATOR_SIZE 7


G_DEFINE_TYPE (WnckTask, wnck_task, G_TYPE_OBJECT);

/* Prototype Section */

static void wnck_task_init        (WnckTask      *task);
static void wnck_task_class_init  (WnckTaskClass *klass);
static void wnck_task_finalize    (GObject       *object);
static void wnck_task_stop_glow   (WnckTask *task);


static gboolean wnck_task_get_needs_attention (WnckTask *task);


static char      *wnck_task_get_text (WnckTask *task,
                                      gboolean  icon_text,
                                      gboolean  include_state);
static GdkPixbuf *wnck_task_get_icon (WnckTask *task);
void       wnck_task_state_changed        (WnckWindow      *window,
                                                  WnckWindowState  changed_mask,
                                                  WnckWindowState  new_state,
                                                  gpointer         data);

static void       wnck_task_drag_begin    (GtkWidget          *widget,
                                           GdkDragContext     *context,
                                           WnckTask           *task);
static void       wnck_task_drag_end      (GtkWidget          *widget,
                                           GdkDragContext     *context,
                                           WnckTask           *task);
static void       wnck_task_drag_data_get (GtkWidget          *widget,
                                           GdkDragContext     *context,
                                           GtkSelectionData   *selection_data,
                                           guint               info,
                                           guint               time,
                                           WnckTask           *task);
static void
wnck_tasklist_activate_next_in_class_group (WnckTask *task,
                                            guint32   timestamp);
static void
wnck_task_popup_menu (WnckTask *task,
                      gboolean  action_submenu);
static gint
compare_class_group_tasks (WnckTask *task1, WnckTask *task2);

/* Start of Method definitions */

//TODO: rename to task instead of tasklist
static void
wnck_tasklist_activate_next_in_class_group (WnckTask *task,
                                            guint32   timestamp)
{
  WnckTask *activate_task;
  gboolean  activate_next;
  GList    *l;

  activate_task = NULL;
  activate_next = FALSE;

  l = task->windows;
  while (l)
    {
      WnckTask *task;

      task = WNCK_TASK (l->data);

      if (wnck_window_is_most_recently_activated (task->window))
        activate_next = TRUE;
      else if (activate_next)
        {
          activate_task = task;
          break;
        }

      l = l->next;
    }

  /* no task in this group is active, or only the last one => activate
   * the first task */
  if (!activate_task && task->windows)
    activate_task = WNCK_TASK (task->windows->data);

  if (activate_task)
    {
      task->was_active = FALSE;
      wnck_tasklist_activate_task_window (activate_task, timestamp);
    }
}



static void
wnck_dimm_icon (GdkPixbuf *pixbuf)
{
  int x, y, pixel_stride, row_stride;
  guchar *row, *pixels;
  int w, h;

  g_assert (pixbuf != NULL);

  w = gdk_pixbuf_get_width (pixbuf);
  h = gdk_pixbuf_get_height (pixbuf);

  g_assert (gdk_pixbuf_get_has_alpha (pixbuf));

  pixel_stride = 4;

  row = gdk_pixbuf_get_pixels (pixbuf);
  row_stride = gdk_pixbuf_get_rowstride (pixbuf);

  for (y = 0; y < h; y++)
    {
      pixels = row;

      for (x = 0; x < w; x++)
	{
	  pixels[3] /= 2;

	  pixels += pixel_stride;
	}

      row += row_stride;
    }
}

static GdkPixbuf *
wnck_task_scale_icon (GdkPixbuf *orig, gboolean minimized)
{
  int w, h;
  GdkPixbuf *pixbuf;

  if (!orig)
    return NULL;

  w = gdk_pixbuf_get_width (orig);
  h = gdk_pixbuf_get_height (orig);

  if (h != MINI_ICON_SIZE ||
      !gdk_pixbuf_get_has_alpha (orig))
    {
      double scale;

      pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
			       TRUE,
			       8,
			       MINI_ICON_SIZE * w / (double) h,
			       MINI_ICON_SIZE);

      scale = MINI_ICON_SIZE / (double) gdk_pixbuf_get_height (orig);

      gdk_pixbuf_scale (orig,
			pixbuf,
			0, 0,
			gdk_pixbuf_get_width (pixbuf),
			gdk_pixbuf_get_height (pixbuf),
			0, 0,
			scale, scale,
			GDK_INTERP_HYPER);
    }
  else
    pixbuf = orig;

  if (minimized)
    {
      if (orig == pixbuf)
	pixbuf = gdk_pixbuf_copy (orig);

      wnck_dimm_icon (pixbuf);
    }

  if (orig == pixbuf)
    g_object_ref (pixbuf);

  return pixbuf;
}

static void
wnck_task_maximize_all (GtkMenuItem *menu_item,
  		        gpointer     data)
{
  WnckTask *task = WNCK_TASK (data);
  GList *l;

  l = task->windows;
  while (l)
    {
      WnckTask *child = WNCK_TASK (l->data);
      wnck_window_maximize (child->window);
      l = l->next;
    }
}

static void
wnck_task_unmaximize_all (GtkMenuItem *menu_item,
  		        gpointer     data)
{
  WnckTask *task = WNCK_TASK (data);
  GList *l;

  l = task->windows;
  while (l)
    {
      WnckTask *child = WNCK_TASK (l->data);
      wnck_window_unmaximize (child->window);
      l = l->next;
    }
}

void
wnck_task_position_menu (GtkMenu   *menu,
			 gint      *x,
			 gint      *y,
			 gboolean  *push_in,
			 gpointer   user_data)
{
  GtkWidget *widget = GTK_WIDGET (user_data);
  GdkWindow *window;
  GtkAllocation allocation;
  GtkRequisition requisition;
  gint menu_xpos;
  gint menu_ypos;
  gint pointer_x;
  gint pointer_y;

  gtk_widget_get_preferred_size (GTK_WIDGET (menu), &requisition, NULL);

  window = gtk_widget_get_window (widget);
  gtk_widget_get_allocation (widget, &allocation);

  gdk_window_get_origin (window, &menu_xpos, &menu_ypos);

  menu_xpos += allocation.x;
  menu_ypos += allocation.y;

  if (menu_ypos >  gdk_screen_height () / 2)
    menu_ypos -= requisition.height;
  else
    menu_ypos += allocation.height;

  gtk_widget_get_pointer (widget, &pointer_x, &pointer_y);
  if (requisition.width < pointer_x)
    menu_xpos += MIN (pointer_x, allocation.width - requisition.width);

  *x = menu_xpos;
  *y = menu_ypos;
  *push_in = FALSE;
}

static void
wnck_task_minimize_all (GtkMenuItem *menu_item,
  		        gpointer     data)
{
  WnckTask *task = WNCK_TASK (data);
  GList *l;

  l = task->windows;
  while (l)
    {
      WnckTask *child = WNCK_TASK (l->data);
      wnck_window_minimize (child->window);
      l = l->next;
    }
}

static void
wnck_task_close_all (GtkMenuItem *menu_item,
 		     gpointer     data)
{
  WnckTask *task = WNCK_TASK (data);
  GList *l;

  l = task->windows;
  while (l)
    {
      WnckTask *child = WNCK_TASK (l->data);
      wnck_window_close (child->window, gtk_get_current_event_time ());
      l = l->next;
    }
}

static void
wnck_task_unminimize_all (GtkMenuItem *menu_item,
		          gpointer     data)
{
  WnckTask *task = WNCK_TASK (data);
  GList *l;

  l = task->windows;
  while (l)
    {
      WnckTask *child = WNCK_TASK (l->data);
      /* This is inside an activate callback, so gtk_get_current_event_time()
       * will work.
       */
      wnck_window_unminimize (child->window, gtk_get_current_event_time ());
      l = l->next;
    }
}


static void
wnck_task_stop_glow (WnckTask *task)
{
  /* We stop glowing, but we might still have the task colored,
   * so we don't reset the glow factor */
  if (task->button_glow != 0)
    g_source_remove (task->button_glow);
}

static void
wnck_task_button_toggled (GtkButton *button,
			  WnckTask  *task)
{
  /* Did we really want to change the state of the togglebutton? */
  if (task->really_toggling)
    return;

  /* Undo the toggle */
  task->really_toggling = TRUE;
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
				!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)));
  task->really_toggling = FALSE;

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      wnck_task_popup_menu (task, FALSE);
      break;
    case WNCK_TASK_WINDOW:
      if (task->window == NULL)
	return;

      /* This should only be called by clicking on the task button, so
       * gtk_get_current_event_time() should be fine here...
       */
      wnck_tasklist_activate_task_window (task, gtk_get_current_event_time ());
      break;
    case WNCK_TASK_STARTUP_SEQUENCE:
      break;
    }
}

static char *
wnck_task_get_text (WnckTask *task,
                    gboolean  icon_text,
                    gboolean  include_state)
{
  const char *name;

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      name = wnck_class_group_get_name (task->class_group);
      if (name[0] != 0)
	return g_strdup_printf ("%s (%d)",
				name,
				g_list_length (task->windows));
      else
	return g_strdup_printf ("(%d)",
				g_list_length (task->windows));

    case WNCK_TASK_WINDOW:
      return _wnck_window_get_name_for_display (task->window,
                                                icon_text, include_state);
      break;

    case WNCK_TASK_STARTUP_SEQUENCE:
#ifdef HAVE_STARTUP_NOTIFICATION
      name = sn_startup_sequence_get_description (task->startup_sequence);
      if (name == NULL)
        name = sn_startup_sequence_get_name (task->startup_sequence);
      if (name == NULL)
        name = sn_startup_sequence_get_binary_name (task->startup_sequence);

      return g_strdup (name);
#else
      return NULL;
#endif
      break;
    }

  return NULL;
}

static gboolean
wnck_task_button_glow (WnckTask *task)
{
  GTimeVal tv;
  gdouble now;
  gfloat fade_opacity, loop_time;
  gint fade_max_loops;
  gboolean stopped;

  g_get_current_time (&tv);
  now = (tv.tv_sec * (1.0 * G_USEC_PER_SEC) +
        tv.tv_usec) / G_USEC_PER_SEC;

  if (task->glow_start_time <= G_MINDOUBLE)
    task->glow_start_time = now;

  gtk_widget_style_get (GTK_WIDGET (task->tasklist), "fade-opacity", &fade_opacity,
                                                     "fade-loop-time", &loop_time,
                                                     "fade-max-loops", &fade_max_loops,
                                                     NULL);

  if (task->button_glow == 0)
    {
      /* we're in "has stopped glowing" mode */
      task->glow_factor = fade_opacity * 0.5;
      stopped = TRUE;
    }
  else
    {
      task->glow_factor = fade_opacity * (0.5 -
                                          0.5 * cos ((now - task->glow_start_time) *
                                                     M_PI * 2.0 / loop_time));

      if (now - task->start_needs_attention > loop_time * 1.0 * fade_max_loops)
        stopped = ABS (task->glow_factor - fade_opacity * 0.5) < 0.05;
      else
        stopped = FALSE;
    }

  gtk_widget_queue_draw (task->button);

  if (stopped)
    wnck_task_stop_glow (task);

  return !stopped;
}

static void
wnck_task_clear_glow_start_timeout_id (WnckTask *task)
{
  task->button_glow = 0;
}

static void
wnck_task_queue_glow (WnckTask *task)
{
  if (task->button_glow == 0)
    {
      task->glow_start_time = 0.0;

      /* The animation doesn't speed up or slow down based on the
       * timeout value, but instead will just appear smoother or
       * choppier.
       */
      task->button_glow =
        g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
                            50,
                            (GSourceFunc) wnck_task_button_glow, task,
                            (GDestroyNotify) wnck_task_clear_glow_start_timeout_id);
    }
}

static gboolean
wnck_task_motion_timeout (gpointer data)
{
  WnckWorkspace *ws;
  WnckTask *task = WNCK_TASK (data);

  task->button_activate = 0;

  /* FIXME: THIS IS SICK AND WRONG AND BUGGY.  See the end of
   * http://mail.gnome.org/archives/wm-spec-list/2005-July/msg00032.html
   * There should only be *one* activate call.
   */
  ws = wnck_window_get_workspace (task->window);
  if (ws && ws != wnck_screen_get_active_workspace (wnck_screen_get_default ()))
  {
    wnck_workspace_activate (ws, task->dnd_timestamp);
  }
  wnck_window_activate_transient (task->window, task->dnd_timestamp);

  task->dnd_timestamp = 0;

  return FALSE;
}

static void
wnck_task_reset_glow (WnckTask *task)
{
  wnck_task_stop_glow (task);
  task->glow_factor = 0.0;
}

static gboolean
wnck_task_button_press_event (GtkWidget	      *widget,
			      GdkEventButton  *event,
			      gpointer         data)
{
  WnckTask *task = WNCK_TASK (data);

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      if (event->button == 2)
        wnck_tasklist_activate_next_in_class_group (task, event->time);
      else
        wnck_task_popup_menu (task,
                              event->button == 3);
      return TRUE;

    case WNCK_TASK_WINDOW:
      if (event->button == 1)
        {
          /* is_most_recently_activated == is_active for click &
	   * sloppy focus methods.  We use the former here because
	   * 'mouse' focus provides a special case.  In that case, no
	   * window will be active, but if a window was the most
	   * recently active one (i.e. user moves mouse straight from
	   * window to tasklist), then we should still minimize it.
           */
          if (wnck_window_is_most_recently_activated (task->window))
            task->was_active = TRUE;
          else
            task->was_active = FALSE;

          return FALSE;
        }
      else if (event->button == 3)
        {
          if (task->action_menu)
            gtk_widget_destroy (task->action_menu);

          g_assert (task->action_menu == NULL);

          task->action_menu = wnck_action_menu_new (task->window);

          g_object_add_weak_pointer (G_OBJECT (task->action_menu),
                                     (void**) &task->action_menu);

          //TODO: hide priv struct of tasklist
          //gtk_menu_set_screen (GTK_MENU (task->action_menu),
                               //_wnck_screen_get_gdk_screen (task->tasklist->priv->screen));

          gtk_widget_show (task->action_menu);
          gtk_menu_popup (GTK_MENU (task->action_menu),
                          NULL, NULL,
                          wnck_task_position_menu, task->button,
                          event->button,
                          gtk_get_current_event_time ());

          g_signal_connect (task->action_menu, "selection-done",
                            G_CALLBACK (gtk_widget_destroy), NULL);

          return TRUE;
        }
      break;
    case WNCK_TASK_STARTUP_SEQUENCE:
      break;
    }

  return FALSE;
}

static void
wnck_task_size_allocated (GtkWidget     *widget,
                          GtkAllocation *allocation,
                          gpointer       data)
{
  WnckTask        *task = WNCK_TASK (data);
  GtkStyleContext *context;
  GtkStateFlags    state;
  GtkBorder        padding;
  int              min_image_width;

  state = gtk_widget_get_state_flags (widget);
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_get_padding (context, state, &padding);

  min_image_width = MINI_ICON_SIZE +
                    padding.left + padding.right +
                    2 * TASKLIST_BUTTON_PADDING;

  if ((allocation->width < min_image_width + 2 * TASKLIST_BUTTON_PADDING) &&
      (allocation->width >= min_image_width)) {
    gtk_widget_show (task->image);
    gtk_widget_hide (task->label);
  } else if (allocation->width < min_image_width) {
    gtk_widget_hide (task->image);
    gtk_widget_show (task->label);
  } else {
    gtk_widget_show (task->image);
    gtk_widget_show (task->label);
  }
}

static void
wnck_task_drag_leave (GtkWidget          *widget,
		      GdkDragContext     *context,
		      guint               time,
		      WnckTask           *task)
{
  if (task->button_activate != 0)
    {
      g_source_remove (task->button_activate);
      task->button_activate = 0;
    }

  gtk_drag_unhighlight (widget);
}

static gboolean
wnck_task_drag_motion (GtkWidget          *widget,
		       GdkDragContext     *context,
		       gint                x,
		       gint                y,
		       guint               time,
		       WnckTask            *task)
{
  if (gtk_drag_dest_find_target (widget, context, NULL))
    {
       gtk_drag_highlight (widget);
#if GTK_CHECK_VERSION(2,21,0)
       gdk_drag_status (context,
                        gdk_drag_context_get_suggested_action (context), time);
#else
       gdk_drag_status (context, context->suggested_action, time);
#endif
		GdkAtom target_type = NULL;
        if(gdk_drag_context_list_targets (context)) {
            /* Choose the best target type */
            target_type = GDK_POINTER_TO_ATOM (
                g_list_nth_data (
                    gdk_drag_context_list_targets (context),
					0
                )
            );
        }
		g_assert(target_type != NULL);
        	
        gtk_drag_get_data (
            widget,
            context,
            target_type,
            time
        );
    } else {
       task->dnd_timestamp = time;

       if (task->button_activate == 0 && task->type == WNCK_TASK_WINDOW)
           task->button_activate = g_timeout_add (WNCK_ACTIVATE_TIMEOUT,
                                                  wnck_task_motion_timeout,
                                                  task);
       gdk_drag_status (context, 0, time);
    }
  return TRUE;
}

static void
wnck_task_drag_begin (GtkWidget          *widget,
		      GdkDragContext     *context,
		      WnckTask           *task)
{
  _wnck_window_set_as_drag_icon (task->window, context,
                                 GTK_WIDGET (task->tasklist));

  task->tasklist->priv->drag_start_time = gtk_get_current_event_time ();
}

static void
wnck_task_drag_end (GtkWidget      *widget,
		    GdkDragContext *context,
		    WnckTask       *task)
{
  task->tasklist->priv->drag_start_time = 0;
}

static void
wnck_task_drag_data_get (GtkWidget          *widget,
		         GdkDragContext     *context,
		         GtkSelectionData   *selection_data,
		         guint               info,
		 	 guint               time,
		         WnckTask           *task)
{
  gulong xid;

  xid = wnck_window_get_xid (task->window);
  gtk_selection_data_set (selection_data,
                          gtk_selection_data_get_target (selection_data),
			  8, (guchar *)&xid, sizeof (gulong));
}

static void
wnck_task_drag_data_received (GtkWidget          *widget,
                              GdkDragContext     *context,
                              gint                x,
                              gint                y,
                              GtkSelectionData   *data,
                              guint               info,
                              guint               time,
                              WnckTask           *target_task)
{
  WnckTasklist *tasklist;
  GList        *l, *windows;
  WnckWindow   *window;
  gulong       *xid;
  guint         new_order, old_order, order;
  WnckWindow   *found_window;

  if ((gtk_selection_data_get_length (data) != sizeof (gulong)) ||
      (gtk_selection_data_get_format (data) != 8))
    {
      gtk_drag_finish (context, FALSE, FALSE, time);
      return;
    }

  tasklist = target_task->tasklist;
  xid = (gulong *) gtk_selection_data_get_data (data);
  found_window = NULL;
  new_order = 0;
  windows = wnck_screen_get_windows (tasklist->priv->screen);

  for (l = windows; l; l = l->next)
    {
       window = WNCK_WINDOW (l->data);
       if (wnck_window_get_xid (window) == *xid)
         {
            old_order = wnck_window_get_sort_order (window);
            new_order = wnck_window_get_sort_order (target_task->window);
            if (old_order < new_order)
              new_order++;
            found_window = window;
            break;
         }
    }

  if (target_task->window == found_window)
    {
      GtkSettings  *settings;
      gint          double_click_time;

      settings = gtk_settings_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (tasklist)));
      double_click_time = 0;
      g_object_get (G_OBJECT (settings),
                    "gtk-double-click-time", &double_click_time,
                    NULL);

      if ((time - tasklist->priv->drag_start_time) < double_click_time)
        {
          wnck_tasklist_activate_task_window (target_task, time);
          gtk_drag_finish (context, TRUE, FALSE, time);
          return;
        }
    }

  if (found_window)
    {
       for (l = windows; l; l = l->next)
         {
            window = WNCK_WINDOW (l->data);
            order = wnck_window_get_sort_order (window);
            if (order >= new_order)
              wnck_window_set_sort_order (window, order + 1);
         }
       wnck_window_set_sort_order (found_window, new_order);

       if (!tasklist->priv->include_all_workspaces &&
           !wnck_window_is_pinned (found_window))
         {
           WnckWorkspace *active_space;
           active_space = wnck_screen_get_active_workspace (tasklist->priv->screen);
           wnck_window_move_to_workspace (found_window, active_space);
         }

       gtk_widget_queue_resize (GTK_WIDGET (tasklist));
    }

    gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
wnck_task_class_name_changed (WnckClassGroup *class_group,
			      gpointer        data)
{
  WnckTask *task = WNCK_TASK (data);

  if (task)
    wnck_task_update_visible_state (task);
}

static void
wnck_task_icon_changed (WnckWindow *window,
			gpointer    data)
{
  WnckTask *task = WNCK_TASK (data);

  if (task)
    wnck_task_update_visible_state (task);
}

static void
wnck_task_name_changed (WnckWindow *window,
			gpointer    data)
{
  WnckTask *task = WNCK_TASK (data);

  if (task)
    wnck_task_update_visible_state (task);
}

static void
wnck_task_class_icon_changed (WnckClassGroup *class_group,
			      gpointer        data)
{
  WnckTask *task = WNCK_TASK (data);

  if (task)
    wnck_task_update_visible_state (task);
}

static gboolean
wnck_task_draw (GtkWidget *widget,
                cairo_t   *cr,
                gpointer   data);

void
wnck_task_create_widgets (WnckTask *task, GtkReliefStyle relief)
{
  GtkWidget *hbox;
  GdkPixbuf *pixbuf;
  char *text;
  GtkCssProvider *provider;
  static const GtkTargetEntry targets[] = {
    { "application/x-wnck-window-id", 0, 0 }
  };

  if (task->type == WNCK_TASK_STARTUP_SEQUENCE)
    task->button = gtk_button_new ();
  else
    task->button = gtk_toggle_button_new ();

  gtk_button_set_relief (GTK_BUTTON (task->button), relief);

  task->button_activate = 0;
  g_object_add_weak_pointer (G_OBJECT (task->button),
                             (void**) &task->button);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider,
                                   "#tasklist-button {\n"
                                   " -GtkWidget-focus-line-width: 0px;\n"
                                   " -GtkWidget-focus-padding: 0px;\n"
                                   "}",
                                   -1, NULL);
  gtk_style_context_add_provider (gtk_widget_get_style_context (task->button),
                                  GTK_STYLE_PROVIDER (provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

  gtk_widget_set_name (task->button,
		       "tasklist-button");

  if (task->type == WNCK_TASK_WINDOW)
    {
      gtk_drag_source_set (GTK_WIDGET (task->button),
                           GDK_BUTTON1_MASK,
                           targets, 1,
                           GDK_ACTION_MOVE);
      gtk_drag_dest_set (GTK_WIDGET (task->button), GTK_DEST_DEFAULT_DROP,
                         targets, 1, GDK_ACTION_MOVE);
    }
  else
    gtk_drag_dest_set (GTK_WIDGET (task->button), 0,
                       NULL, 0, GDK_ACTION_DEFAULT);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  pixbuf = wnck_task_get_icon (task);
  if (pixbuf)
    {
      task->image = gtk_image_new_from_pixbuf (pixbuf);
      g_object_unref (pixbuf);
    }
  else
    task->image = gtk_image_new ();

  gtk_widget_show (task->image);

  text = wnck_task_get_text (task, TRUE, TRUE);
  task->label = gtk_label_new (text);
  gtk_misc_set_alignment (GTK_MISC (task->label), 0.0, 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (task->label),
                          PANGO_ELLIPSIZE_END);

  if (wnck_task_get_needs_attention (task))
    {
      _make_gtk_label_bold ((GTK_LABEL (task->label)));
      wnck_task_queue_glow (task);
    }

  gtk_widget_show (task->label);

  gtk_box_pack_start (GTK_BOX (hbox), task->image, FALSE, FALSE,
		      TASKLIST_BUTTON_PADDING);
  gtk_box_pack_start (GTK_BOX (hbox), task->label, TRUE, TRUE,
		      TASKLIST_BUTTON_PADDING);

  gtk_container_add (GTK_CONTAINER (task->button), hbox);
  gtk_widget_show (hbox);
  g_free (text);

  text = wnck_task_get_text (task, FALSE, FALSE);
  gtk_widget_set_tooltip_text (task->button, text);
  g_free (text);

  /* Set up signals */
  if (GTK_IS_TOGGLE_BUTTON (task->button))
    g_signal_connect_object (G_OBJECT (task->button), "toggled",
                             G_CALLBACK (wnck_task_button_toggled),
                             G_OBJECT (task),
                             0);

  g_signal_connect_object (G_OBJECT (task->button), "size_allocate",
                           G_CALLBACK (wnck_task_size_allocated),
                           G_OBJECT (task),
                           0);

  g_signal_connect_object (G_OBJECT (task->button), "button_press_event",
                           G_CALLBACK (wnck_task_button_press_event),
                           G_OBJECT (task),
                           0);

  g_signal_connect_object (G_OBJECT(task->button), "drag_motion",
                           G_CALLBACK (wnck_task_drag_motion),
                           G_OBJECT (task),
                           0);

  if (task->type == WNCK_TASK_WINDOW)
    {
      g_signal_connect_object (G_OBJECT (task->button), "drag_data_received",
                               G_CALLBACK (wnck_task_drag_data_received),
                               G_OBJECT (task),
                               0);

    }

  g_signal_connect_object (G_OBJECT(task->button), "drag_leave",
                           G_CALLBACK (wnck_task_drag_leave),
                           G_OBJECT (task),
                           0);

  if (task->type == WNCK_TASK_WINDOW) {
      g_signal_connect_object (G_OBJECT(task->button), "drag_data_get",
                               G_CALLBACK (wnck_task_drag_data_get),
                               G_OBJECT (task),
                               0);

      g_signal_connect_object (G_OBJECT(task->button), "drag_begin",
                               G_CALLBACK (wnck_task_drag_begin),
                               G_OBJECT (task),
                               0);

      g_signal_connect_object (G_OBJECT(task->button), "drag_end",
                               G_CALLBACK (wnck_task_drag_end),
                               G_OBJECT (task),
                               0);
  }

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      task->class_name_changed_tag = g_signal_connect (G_OBJECT (task->class_group), "name_changed",
						       G_CALLBACK (wnck_task_class_name_changed), task);
      task->class_icon_changed_tag = g_signal_connect (G_OBJECT (task->class_group), "icon_changed",
						       G_CALLBACK (wnck_task_class_icon_changed), task);
      break;

    case WNCK_TASK_WINDOW:
      task->state_changed_tag = g_signal_connect (G_OBJECT (task->window), "state_changed",
                                                  G_CALLBACK (wnck_task_state_changed), task->tasklist);
      task->icon_changed_tag = g_signal_connect (G_OBJECT (task->window), "icon_changed",
                                                 G_CALLBACK (wnck_task_icon_changed), task);
      task->name_changed_tag = g_signal_connect (G_OBJECT (task->window), "name_changed",
						 G_CALLBACK (wnck_task_name_changed), task);
      break;

    case WNCK_TASK_STARTUP_SEQUENCE:
      break;

    default:
      g_assert_not_reached ();
    }

  g_signal_connect_object (task->button, "draw",
                           G_CALLBACK (wnck_task_draw),
                           G_OBJECT (task),
                           G_CONNECT_AFTER);
}

static void
wnck_task_menu_activated (GtkMenuItem *menu_item,
			  gpointer     data)
{
  WnckTask *task = WNCK_TASK (data);

  /* This is an "activate" callback function so gtk_get_current_event_time()
   * will suffice.
   */
  wnck_tasklist_activate_task_window (task, gtk_get_current_event_time ());
}

static void
wnck_task_popup_menu (WnckTask *task,
                      gboolean  action_submenu)
{
  GtkWidget *menu;
  WnckTask *win_task;
  char *text;
  GdkPixbuf *pixbuf;
  GtkWidget *menu_item;
  GtkWidget *image;
  GList *l, *list;

  g_return_if_fail (task->type == WNCK_TASK_CLASS_GROUP);

  if (task->class_group == NULL)
    return;

  if (task->menu == NULL)
    {
      task->menu = gtk_menu_new ();
      g_object_ref_sink (task->menu);
    }

  menu = task->menu;

  /* Remove old menu content */
  list = gtk_container_get_children (GTK_CONTAINER (menu));
  l = list;
  while (l)
    {
      GtkWidget *child = GTK_WIDGET (l->data);
      gtk_container_remove (GTK_CONTAINER (menu), child);
      l = l->next;
    }
  g_list_free (list);

  l = task->windows;
  while (l)
    {
      win_task = WNCK_TASK (l->data);

      text = wnck_task_get_text (win_task, TRUE, TRUE);
      menu_item = gtk_image_menu_item_new_with_label (text);
      g_free (text);

      gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menu_item),
                                                 TRUE);

      if (wnck_task_get_needs_attention (win_task))
        _make_gtk_label_bold (GTK_LABEL (gtk_bin_get_child (GTK_BIN (menu_item))));

      text = wnck_task_get_text (win_task, FALSE, FALSE);
      gtk_widget_set_tooltip_text (menu_item, text);
      g_free (text);

      pixbuf = wnck_task_get_icon (win_task);
      if (pixbuf)
	{
	  image = gtk_image_new_from_pixbuf (pixbuf);
	  gtk_widget_show (image);
	  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item),
					 image);
	  g_object_unref (pixbuf);
	}

      gtk_widget_show (menu_item);

      if (action_submenu)
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item),
                                   wnck_action_menu_new (win_task->window));
      else
        {
          static const GtkTargetEntry targets[] = {
            { "application/x-wnck-window-id", 0, 0 }
          };

          g_signal_connect_object (G_OBJECT (menu_item), "activate",
                                   G_CALLBACK (wnck_task_menu_activated),
                                   G_OBJECT (win_task),
                                   0);


          gtk_drag_source_set (menu_item, GDK_BUTTON1_MASK,
                               targets, 1, GDK_ACTION_MOVE);
          g_signal_connect_object (G_OBJECT(menu_item), "drag_begin",
                                   G_CALLBACK (wnck_task_drag_begin),
                                   G_OBJECT (win_task),
                                   0);
          g_signal_connect_object (G_OBJECT(menu_item), "drag_end",
                                   G_CALLBACK (wnck_task_drag_end),
                                   G_OBJECT (win_task),
                                   0);
          g_signal_connect_object (G_OBJECT(menu_item), "drag_data_get",
                                   G_CALLBACK (wnck_task_drag_data_get),
                                   G_OBJECT (win_task),
                                   0);
        }

      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

      l = l->next;
    }

  /* In case of Right click, show Minimize All, Unminimize All, Close All*/
  if (action_submenu)
    {
      GtkWidget *separator;
      GtkWidget *image;

      separator = gtk_separator_menu_item_new ();
      gtk_widget_show (separator);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), separator);

      menu_item = gtk_image_menu_item_new_with_mnemonic (_("Mi_nimize All"));
      image = gtk_image_new_from_stock (WNCK_STOCK_MINIMIZE, GTK_ICON_SIZE_MENU);
      gtk_widget_show (image);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
      gtk_widget_show (menu_item);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
      g_signal_connect_object (G_OBJECT (menu_item), "activate",
	    		       G_CALLBACK (wnck_task_minimize_all),
			       G_OBJECT (task),
			       0);

      menu_item =  gtk_image_menu_item_new_with_mnemonic (_("Un_minimize All"));
      gtk_widget_show (menu_item);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
      g_signal_connect_object (G_OBJECT (menu_item), "activate",
  			       G_CALLBACK (wnck_task_unminimize_all),
			       G_OBJECT (task),
			       0);

      menu_item = gtk_image_menu_item_new_with_mnemonic (_("Ma_ximize All"));
      image = gtk_image_new_from_stock (WNCK_STOCK_MAXIMIZE, GTK_ICON_SIZE_MENU);
      gtk_widget_show (image);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
      gtk_widget_show (menu_item);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
      g_signal_connect_object (G_OBJECT (menu_item), "activate",
  			       G_CALLBACK (wnck_task_maximize_all),
			       G_OBJECT (task),
			       0);

      menu_item =  gtk_image_menu_item_new_with_mnemonic (_("_Unmaximize All"));
      gtk_widget_show (menu_item);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
      g_signal_connect_object (G_OBJECT (menu_item), "activate",
  			       G_CALLBACK (wnck_task_unmaximize_all),
			       G_OBJECT (task),
			       0);

      separator = gtk_separator_menu_item_new ();
      gtk_widget_show (separator);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), separator);

      menu_item = gtk_image_menu_item_new_with_mnemonic(_("_Close All"));
      image = gtk_image_new_from_stock (WNCK_STOCK_DELETE, GTK_ICON_SIZE_MENU);
      gtk_widget_show (image);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
      gtk_widget_show (menu_item);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
      g_signal_connect_object (G_OBJECT (menu_item), "activate",
			       G_CALLBACK (wnck_task_close_all),
			       G_OBJECT (task),
			       0);
    }

  gtk_menu_set_screen (GTK_MENU (menu),
		       _wnck_screen_get_gdk_screen (task->tasklist->priv->screen));

  gtk_widget_show (menu);
  gtk_menu_popup (GTK_MENU (menu),
		  NULL, NULL,
		  wnck_task_position_menu, task->button,
		  1, gtk_get_current_event_time ());
}




static GdkPixbuf *
wnck_task_get_icon (WnckTask *task)
{
  WnckWindowState state;
  GdkPixbuf *pixbuf;

  pixbuf = NULL;

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      pixbuf = wnck_task_scale_icon (wnck_class_group_get_mini_icon (task->class_group),
				     FALSE);
      break;

    case WNCK_TASK_WINDOW:
      state = wnck_window_get_state (task->window);

      pixbuf =  wnck_task_scale_icon (wnck_window_get_mini_icon (task->window),
				      state & WNCK_WINDOW_STATE_MINIMIZED);
      break;
    case WNCK_TASK_STARTUP_SEQUENCE:
#ifdef HAVE_STARTUP_NOTIFICATION
      if (task->tasklist->priv->icon_loader != NULL)
        {
          const char *icon;

          icon = sn_startup_sequence_get_icon_name (task->startup_sequence);
          if (icon != NULL)
            {
              GdkPixbuf *loaded;

              loaded =  (* task->tasklist->priv->icon_loader) (icon,
                                                               MINI_ICON_SIZE,
                                                               0,
                                                               task->tasklist->priv->icon_loader_data);

              if (loaded != NULL)
                {
                  pixbuf = wnck_task_scale_icon (loaded, FALSE);
                  g_object_unref (G_OBJECT (loaded));
                }
            }
        }

      if (pixbuf == NULL)
        {
          _wnck_get_fallback_icons (NULL, 0, 0,
                                    &pixbuf, MINI_ICON_SIZE, MINI_ICON_SIZE);
        }
#endif
      break;
    }

  return pixbuf;
}

static gboolean
wnck_task_get_needs_attention (WnckTask *task)
{
  GList *l;
  WnckTask *win_task;
  gboolean needs_attention;

  needs_attention = FALSE;

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      task->start_needs_attention = 0;
      l = task->windows;
      while (l)
	{
	  win_task = WNCK_TASK (l->data);

	  if (wnck_window_or_transient_needs_attention (win_task->window))
	    {
	      needs_attention = TRUE;
              task->start_needs_attention = MAX (task->start_needs_attention, _wnck_window_or_transient_get_needs_attention_time (win_task->window));
	      break;
	    }

	  l = l->next;
	}
      break;

    case WNCK_TASK_WINDOW:
      needs_attention =
	wnck_window_or_transient_needs_attention (task->window);
      task->start_needs_attention = _wnck_window_or_transient_get_needs_attention_time (task->window);
      break;

    case WNCK_TASK_STARTUP_SEQUENCE:
      break;
    }

  return needs_attention != FALSE;
}

static gint
compare_class_group_tasks (WnckTask *task1, WnckTask *task2)
{
  const char *name1, *name2;

  name1 = wnck_class_group_get_name (task1->class_group);
  name2 = wnck_class_group_get_name (task2->class_group);

  return g_utf8_collate (name1, name2);
}

void
wnck_task_update_visible_state (WnckTask *task)
{
  GdkPixbuf *pixbuf;
  char *text;

  pixbuf = wnck_task_get_icon (task);
  gtk_image_set_from_pixbuf (GTK_IMAGE (task->image),
			     pixbuf);
  if (pixbuf)
    g_object_unref (pixbuf);

  text = wnck_task_get_text (task, TRUE, TRUE);
  if (text != NULL)
    {
      gtk_label_set_text (GTK_LABEL (task->label), text);
      if (wnck_task_get_needs_attention (task))
        {
          _make_gtk_label_bold ((GTK_LABEL (task->label)));
          wnck_task_queue_glow (task);
        }
      else
        {
          _make_gtk_label_normal ((GTK_LABEL (task->label)));
          wnck_task_reset_glow (task);
        }
      g_free (text);
    }

  text = wnck_task_get_text (task, FALSE, FALSE);
  /* if text is NULL, this unsets the tooltip, which is probably what we'd want
   * to do */
  gtk_widget_set_tooltip_text (task->button, text);
  g_free (text);

  gtk_widget_queue_resize (GTK_WIDGET (task->tasklist));
}

void
wnck_task_state_changed (WnckWindow     *window,
			 WnckWindowState changed_mask,
			 WnckWindowState new_state,
			 gpointer        data)
{
  WnckTasklist *tasklist = WNCK_TASKLIST (data);

  if (changed_mask & WNCK_WINDOW_STATE_SKIP_TASKLIST)
    {
      wnck_tasklist_update_lists  (tasklist);
      gtk_widget_queue_resize (GTK_WIDGET (tasklist));
      return;
    }

  if ((changed_mask & WNCK_WINDOW_STATE_DEMANDS_ATTENTION) ||
      (changed_mask & WNCK_WINDOW_STATE_URGENT))
    {
      WnckWorkspace *active_workspace =
        wnck_screen_get_active_workspace (tasklist->priv->screen);

      if (active_workspace                              &&
          (active_workspace != wnck_window_get_workspace (window) ||
	   (wnck_workspace_is_virtual (active_workspace) &&
	    !wnck_window_is_in_viewport (window, active_workspace))))
        {
          wnck_tasklist_update_lists (tasklist);
          gtk_widget_queue_resize (GTK_WIDGET (tasklist));
        }
    }

  if ((changed_mask & WNCK_WINDOW_STATE_MINIMIZED)         ||
      (changed_mask & WNCK_WINDOW_STATE_DEMANDS_ATTENTION) ||
      (changed_mask & WNCK_WINDOW_STATE_URGENT))
    {
      WnckTask *win_task = NULL;

      /* FIXME: Handle group modal dialogs */
      for (; window && !win_task; window = wnck_window_get_transient (window))
        win_task = g_hash_table_lookup (tasklist->priv->win_hash, window);

      if (win_task)
	{
	  WnckTask *class_group_task;

	  wnck_task_update_visible_state (win_task);

	  class_group_task =
            g_hash_table_lookup (tasklist->priv->class_group_hash,
                                 win_task->class_group);

	  if (class_group_task)
	    wnck_task_update_visible_state (class_group_task);
	}
    }

}

static gboolean
wnck_task_draw (GtkWidget *widget,
                cairo_t   *cr,
                gpointer   data)
{
  int x, y;
  WnckTask *task;
  GtkStyleContext *context;
  GtkStateFlags state;
  GtkBorder padding;
  GtkWidget    *tasklist_widget;
  gint width, height;
  gboolean overlay_rect;
  gint arrow_width;
  gint arrow_height;
  GdkRGBA color;

  task = WNCK_TASK (data);

  switch (task->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      context = gtk_widget_get_style_context (widget);

      gtk_style_context_get_padding (context, gtk_widget_get_state_flags (widget), &padding);
      state = (task->tasklist->priv->active_class_group == task) ?
              GTK_STATE_FLAG_ACTIVE : GTK_STATE_FLAG_NORMAL;
      gtk_style_context_get_color (context, state, &color);

      x = gtk_widget_get_allocated_width (widget) -
          (gtk_container_get_border_width (GTK_CONTAINER (widget)) + padding.right + ARROW_SIZE);
      y = gtk_widget_get_allocated_height (widget) / 2;

      arrow_width = INDICATOR_SIZE + ((INDICATOR_SIZE % 2) - 1);
      arrow_height = arrow_width / 2 + 1;
      x += (ARROW_SIZE - arrow_width) / 2;
      y -= (2 * arrow_height + ARROW_SPACE) / 2;

      cairo_save (cr);
      gdk_cairo_set_source_rgba (cr, &color);

      /* Up arrow */
      cairo_move_to (cr, x, y + arrow_height);
      cairo_line_to (cr, x + arrow_width / 2., y);
      cairo_line_to (cr, x + arrow_width, y + arrow_height);
      cairo_close_path (cr);
      cairo_fill (cr);

      /* Down arrow */
      y += arrow_height + ARROW_SPACE;
      cairo_move_to (cr, x, y);
      cairo_line_to (cr, x + arrow_width, y);
      cairo_line_to (cr, x + arrow_width / 2., y + arrow_height);
      cairo_close_path (cr);
      cairo_fill (cr);

      cairo_restore (cr);

      break;

    case WNCK_TASK_WINDOW:
    case WNCK_TASK_STARTUP_SEQUENCE:
      break;
    }

  if (task->glow_factor == 0.0)
    return FALSE;

  /* push a translucent overlay to paint to, so we can blend later */
  cairo_push_group_with_content (cr, CAIRO_CONTENT_COLOR_ALPHA);

  width = gtk_widget_get_allocated_width (task->button);
  height = gtk_widget_get_allocated_height (task->button);

  tasklist_widget = GTK_WIDGET (task->tasklist);

  context = gtk_widget_get_style_context (task->button);

  /* first draw the button */
  gtk_widget_style_get (tasklist_widget, "fade-overlay-rect", &overlay_rect, NULL);
  if (overlay_rect)
    {
      GdkRGBA bg_color;

      /* Draw a rectangle with selected background color */
      gtk_style_context_get_background_color (context, GTK_STATE_FLAG_SELECTED, &bg_color);
      gdk_cairo_set_source_rgba (cr, &bg_color);
      cairo_paint (cr);
    }
  else
    {
      gtk_style_context_save (context);
      gtk_style_context_set_state (context, GTK_STATE_FLAG_SELECTED);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_BUTTON);

      cairo_save (cr);
      gtk_render_background (context, cr, 0, 0, width, height);
      gtk_render_frame (context, cr, 0, 0, width, height);
      cairo_restore (cr);

      gtk_style_context_restore (context);
    }

  /* then the contents */
  gtk_container_propagate_draw (GTK_CONTAINER (task->button),
                                gtk_bin_get_child (GTK_BIN (task->button)),
                                cr);
  /* finally blend it */
  cairo_pop_group_to_source (cr);
  cairo_paint_with_alpha (cr, task->glow_factor);

  return FALSE;
}

gint
wnck_task_compare_alphabetically (gconstpointer a,
                                  gconstpointer b)
{
  char *text1;
  char *text2;
  gint  result;

  text1 = wnck_task_get_text (WNCK_TASK (a), TRUE, FALSE);
  text2 = wnck_task_get_text (WNCK_TASK (b), TRUE, FALSE);

  result= g_utf8_collate (text1, text2);

  g_free (text1);
  g_free (text2);

  return result;
}

gint
wnck_task_compare (gconstpointer  a,
		   gconstpointer  b)
{
  WnckTask *task1 = WNCK_TASK (a);
  WnckTask *task2 = WNCK_TASK (b);
  gint pos1 = 0, pos2 = 0;

  switch (task1->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      if (task2->type == WNCK_TASK_CLASS_GROUP)
	return compare_class_group_tasks (task1, task2);
      else
	return -1; /* Sort groups before everything else */

    case WNCK_TASK_WINDOW:
      pos1 = wnck_window_get_sort_order (task1->window);
      break;
    case WNCK_TASK_STARTUP_SEQUENCE:
      pos1 = G_MAXINT; /* startup sequences are sorted at the end. */
      break;           /* Changing this will break scrolling.      */
    }

  switch (task2->type)
    {
    case WNCK_TASK_CLASS_GROUP:
      if (task1->type == WNCK_TASK_CLASS_GROUP)
	return compare_class_group_tasks (task1, task2);
      else
	return 1; /* Sort groups before everything else */

    case WNCK_TASK_WINDOW:
      pos2 = wnck_window_get_sort_order (task2->window);
      break;
    case WNCK_TASK_STARTUP_SEQUENCE:
      pos2 = G_MAXINT;
      break;
    }

  if (pos1 < pos2)
    return -1;
  else if (pos1 > pos2)
    return 1;
  else
    return 0; /* should only happen if there's multiple processes being
               * started, and then who cares about sort order... */
}

static void
wnck_task_init (WnckTask *task) {
  /* TODO: Add initialization code here */
  task->tasklist = NULL;

  task->button = NULL;
  task->image = NULL;
  task->label = NULL;

  task->type = WNCK_TASK_WINDOW;

  task->class_group = NULL;
  task->window = NULL;
#ifdef HAVE_STARTUP_NOTIFICATION
  task->startup_sequence = NULL;
#endif

  task->grouping_score = 0;

  task->windows = NULL;

  task->state_changed_tag = 0;
  task->icon_changed_tag = 0;
  task->name_changed_tag = 0;
  task->class_name_changed_tag = 0;
  task->class_icon_changed_tag = 0;

  task->menu = NULL;
  task->action_menu = NULL;

  task->really_toggling = FALSE;

  task->was_active = FALSE;

  task->button_activate = 0;

  task->dnd_timestamp = 0;

  task->start_needs_attention = 0;
  task->glow_start_time = 0.0;
  task->glow_factor = 0.0;

  task->button_glow = 0;

  task->row = 0;
  task->col = 0;
}

GList *
wnck_task_get_highest_scored (GList     *ungrouped_class_groups,
			      WnckTask **class_group_task_out)
{
  WnckTask *class_group_task;
  WnckTask *best_task = NULL;
  double max_score = -1000000000.0; /* Large negative score */
  GList *l;

  l = ungrouped_class_groups;
  while (l != NULL)
    {
      class_group_task = WNCK_TASK (l->data);

      if (class_group_task->grouping_score >= max_score)
	{
	  max_score = class_group_task->grouping_score;
	  best_task = class_group_task;
	}

      l = l->next;
    }

  *class_group_task_out = best_task;

  return g_list_remove (ungrouped_class_groups, best_task);
}


static void
wnck_task_finalize (GObject *object) {
  /* TODO: Add deinitalization code here */
  WnckTask *task = WNCK_TASK (object);

  if (task->tasklist->priv->active_task == task)
    wnck_tasklist_change_active_task (task->tasklist, NULL);

  if (task->button)
    {
      g_object_remove_weak_pointer (G_OBJECT (task->button),
                                    (void**) &task->button);
      gtk_widget_destroy (task->button);
      task->button = NULL;
      task->image = NULL;
      task->label = NULL;
    }

#ifdef HAVE_STARTUP_NOTIFICATION
  if (task->startup_sequence)
    {
      sn_startup_sequence_unref (task->startup_sequence);
      task->startup_sequence = NULL;
    }
#endif

  g_list_free (task->windows);
  task->windows = NULL;

  if (task->state_changed_tag != 0)
    {
      g_signal_handler_disconnect (task->window,
				   task->state_changed_tag);
      task->state_changed_tag = 0;
    }

  if (task->icon_changed_tag != 0)
    {
      g_signal_handler_disconnect (task->window,
				   task->icon_changed_tag);
      task->icon_changed_tag = 0;
    }

  if (task->name_changed_tag != 0)
    {
      g_signal_handler_disconnect (task->window,
				   task->name_changed_tag);
      task->name_changed_tag = 0;
    }

  if (task->class_name_changed_tag != 0)
    {
      g_signal_handler_disconnect (task->class_group,
				   task->class_name_changed_tag);
      task->class_name_changed_tag = 0;
    }

  if (task->class_icon_changed_tag != 0)
    {
      g_signal_handler_disconnect (task->class_group,
				   task->class_icon_changed_tag);
      task->class_icon_changed_tag = 0;
    }

  if (task->class_group)
    {
      g_object_unref (task->class_group);
      task->class_group = NULL;
    }

  if (task->window)
    {
      g_object_unref (task->window);
      task->window = NULL;
    }

  if (task->menu)
    {
      gtk_widget_destroy (task->menu);
      task->menu = NULL;
    }

  if (task->action_menu)
    {
      g_object_remove_weak_pointer (G_OBJECT (task->action_menu),
                                    (void**) &task->action_menu);
      gtk_widget_destroy (task->action_menu);
      task->action_menu = NULL;
    }

  if (task->button_activate != 0)
    {
      g_source_remove (task->button_activate);
      task->button_activate = 0;
    }

  wnck_task_stop_glow (task);

  G_OBJECT_CLASS (wnck_task_parent_class)->finalize (object);
}

static void wnck_task_class_init (WnckTaskClass *klass) {
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = wnck_task_finalize;
}

WnckTask *wnck_task_new_from_window (
  WnckTasklist *tasklist,
  WnckWindow   *window)
{
  WnckTask *task;

  task = g_object_new (WNCK_TYPE_TASK, NULL);

  task->type = WNCK_TASK_WINDOW;
  task->window = g_object_ref (window);
  task->class_group = g_object_ref (wnck_window_get_class_group (window));
  task->tasklist = tasklist;

  wnck_task_create_widgets (task, tasklist->priv->relief);

  remove_startup_sequences_for_window (tasklist, window);

  return task;
}

WnckTask *wnck_task_new_from_class_group (
  WnckTasklist   *tasklist,
  WnckClassGroup *class_group)
{
  WnckTask *task;

  task = g_object_new (WNCK_TYPE_TASK, NULL);

  task->type = WNCK_TASK_CLASS_GROUP;
  task->window = NULL;
  task->class_group = g_object_ref (class_group);
  task->tasklist = tasklist;

  wnck_task_create_widgets (task, tasklist->priv->relief);

  return task;
}

void
remove_startup_sequences_for_window (WnckTasklist *tasklist,
                                     WnckWindow   *window)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  const char *win_id;
  GList *tmp;

  win_id = _wnck_window_get_startup_id (window);
  if (win_id == NULL)
    return;

  tmp = tasklist->priv->startup_sequences;
  while (tmp != NULL)
    {
      WnckTask *task = tmp->data;
      GList *next = tmp->next;
      const char *task_id;

      g_assert (task->type == WNCK_TASK_STARTUP_SEQUENCE);

      task_id = sn_startup_sequence_get_id (task->startup_sequence);

      if (task_id && strcmp (task_id, win_id) == 0)
        gtk_widget_destroy (task->button);

      tmp = next;
    }
#else
  ; /* nothing */
#endif
}

#ifdef HAVE_STARTUP_NOTIFICATION
WnckTask*
wnck_task_new_from_startup_sequence (WnckTasklist      *tasklist,
                                     SnStartupSequence *sequence)
{
  WnckTask *task;

  task = g_object_new (WNCK_TYPE_TASK, NULL);

  task->type = WNCK_TASK_STARTUP_SEQUENCE;
  task->window = NULL;
  task->class_group = NULL;
  task->startup_sequence = sequence;
  sn_startup_sequence_ref (task->startup_sequence);
  task->tasklist = tasklist;

  wnck_task_create_widgets (task, tasklist->priv->relief);

  return task;
}

#endif /* HAVE_STARTUP_NOTIFICATION */
