/*
 * Copyright (c) 2014 Citrix Systems, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "project.h"
#include "rpcgen/xenmgr_client.h"

static struct domain *current, *surface_current;

extern int keyb_waits_for_click;

static int
switcher_self_switch_disabled(void)
{
    char c;

    if (db_exists("/switcher/self-switch-disabled"))
    {
        db_read(&c, 1, "/switcher/self-switch-disabled");
        return (c == '1' || c == 'T' || c == 't');
    }
    else
        return 1; /* disabled by default */
}

int
switcher_switch_graphic(struct domain *d, int force)
{
    int rc = TRUE;
    gint domid;

    if (!d)
        return -1;

    if (switcher_self_switch_disabled() &&
        d == surface_current)
        return rc;

    rc = com_citrix_xenclient_surfman_set_visible_
        (xcbus_conn, SURFMAN_SERVICE, SURFMAN_PATH, d->domid, 3000,
         force ? TRUE : FALSE);
    if (rc)
        surface_current = d;
    return rc;
}

void
switcher_unfocus_gpu (void)
{
  /* only unfocus on the gpu, do not touch mouse/kb focus.. */
  surface_current = NULL;
}

int
switcher_switch (struct domain *d, int mouse_switch, int force)
{
  static GArray *domids=NULL;
  int ret = -1;
  static int follow = -1;
  static int switching = 0;

  if (!d) {
    return -1;
  }

  /* this is protection against semiconcurrent switcher_switch invocations. Although this is
   * mostly single threaded program, this function can be enter twice at same time due if another switch
   * request comes while it is waiting for the blocking call to surfman to return. Doom and misery and
   * displayed/keyboard focused vm disreptancy ensues */
  if (switching) {
      warning("switching already in progress");
      return -1;
  }
  switching = 1;

  if (!keyb_waits_for_click)
    {
      /* For an application viewing VM, the keyboard may be directed to a
       * sharing VM. When switching away from the viewing VM, we need to
       * remember the domain that had the keyboard, so it can be restored
       * to this domain when we switch back to the viewing VM. */
      save_prev_keyb_domain (current);
    }

  if (d && d->is_in_s3)
    domain_wake_from_s3 (d);

  if (d->disabled_surface)
      info ("domid:%d disabled_surface:%d", d->domid, d->disabled_surface);

  if (!mouse_switch && input_domain_supports_abs(d))
        input_domain_set_mouse(d);

  if (d->disabled_surface || switcher_switch_graphic(d, force) || d->is_pvm)
  {
        if (current) xenstore_dom_write_int (current->domid, 0, "switcher/have_focus");

        current = d;

	if (follow == -1)
	  {
	      char c;

	      follow = 0;
	      if (db_exists("/switcher/keyboard_follows_mouse"))
	      {
		  db_read(&c, 1, "/switcher/keyboard_follows_mouse");
		  if (c != '0' && c != 'F' && c != 'f')
		      follow = 1;
	      }
	  }

        if (mouse_switch && !follow)
            input_set_mouse(d);
        else
	  {
            input_set(d);

	    /* For an application viewing VM, restore the keyboard to the domain
	     * that had it when we switched away from the VM. */
	    if (!d->divert_info)
            restore_prev_keyb_domain (current);
	  }

        xenstore_dom_write_int (current->domid, 1, "switcher/have_focus");
        xenstore_write_int(current->domid, "/local/domain/0/switcher/focus");
        /* reboots can benefit from focused uuid knowledge.. */
        xenstore_write(current->uuid, "/local/domain/0/switcher/focus-uuid");

        /* Set the keyboard LEDs as per the domain that has the keyboard. */
        if ( (current->prev_keyb_domain_ptr != NULL) && (current->prev_keyb_domid != -1) &&
             (current->prev_keyb_domain_ptr->domid == current->prev_keyb_domid) )
        {
            input_led_code (current->prev_keyb_domain_ptr->keyboard_led_code, current->prev_keyb_domain_ptr->domid);
        }
        else
        {
            input_led_code (current->keyboard_led_code, current->domid);
        }

        ret = 0;
    }

  switching = 0;
  return ret;
}


void
switcher_domain_gone (struct domain *d)
{
    if (!d)
        return;

    if (surface_current && surface_current->domid == d->domid)
    {
        /*Current domain died */
        /*FIXME -- missing code although focus will take care of most of it*/
        surface_current = NULL;
        if (d->sstate != SWITCHER_SHUTDOWN_REBOOT)
            switcher_switch(domain_uivm (), 0, 0);
    }
    else if (current && current->domid == d->domid && d->has_secondary_gpu)
    {
        current = NULL;
        if (d->sstate != SWITCHER_SHUTDOWN_REBOOT)
            switcher_switch(domain_uivm(), 0, 0);
    }
}


static int
switch_to_slot (int slot, int mouse_switch, int force)
{
  struct domain *d;

  info ("Received request to switch to slot %d force=%d", slot, force);

#if 0
  if (slot)
    return;
#endif

  d = domain_with_slot (slot);

  if (!d)
    {
      info ("request to switch to absent slot %d", slot);
      return -1;
    }

  info ("request to switch to slot %d force=%d", slot, force);

  return switcher_switch (d, mouse_switch, force);
}

static int
force_go_to_slot (void *slotp)
{
  int slot = (int) slotp;

  if (auth_get_context ()) {
      info ("auth in progress, keyboard switching is blocked");
      return -1;
  }
  switch_to_slot (slot, 0, 1);

  return 0;
}

static int
go_to_slot (void *slotp)
{
  int slot = (int) slotp;

  info("go to slot %d",slot);
  switch_to_slot (slot, 0, 0);

  return 0;
}

static int
go_to_next (void *opaque)
{
  int slot;
  struct domain *d;

  info ("request to switch next slot");
  if (!current)
    return 1;

  if (current == domain_uivm ())
    return 0;

  slot = current->slot;
  info ("current slot is %d", slot);
  slot++;

  for (; slot != current->slot; ++slot)
    {
      slot %= 10;
      d = domain_with_slot (slot);
      if (d && d != domain_uivm ())
        {
          info ("new slot is %d", slot);
          switcher_switch (d, 0, 0);
          return 0;
        }
    }
  return 0;
}

void
switcher_s3 (struct domain *d)
{
  if (d == domain_pvm ())
    return;
  if (d != current)
    return;

  switcher_switch (domain_uivm (), 0, 0);
}

/* lock the screen. Context is constructed from database information. Flags ensure
 * no cancel can happen. Whether switch can happen out of lock screen is specified in the argument */
int switcher_lock(int can_switch_out)
{
    char user[1024];
    char s_flags[20];
    /* make a context ourselves */
    memset (user, 0, sizeof (user));
    memset (s_flags, 0, sizeof (s_flags));

    db_read (user, 1024, PLATFORM_USERNAME);
    if (*user) {
        int flags = AUTH_FLAG_NONE;
        db_read (s_flags, 20, PLATFORM_FLAGS);
        if (*s_flags) {
            flags = strtol (s_flags, NULL, 10);
        }
        if (!can_switch_out) {
            flags |= AUTH_FLAG_LOCK;
        }
        auth_set_context (user, user, flags | AUTH_FLAG_CANNOT_CANCEL);
        return input_secure(1);
    } else {
        warning("lock requested but there is no user");
    }
    return 0;
}

/* start authentication, even if there's no context -> make one then */
int switcher_auth_force()
{
    if (auth_get_context() == NULL) {
        /* if no context, make context using out of database information */
        char user[1024];
        char s_flags[20];

        info("auth requested without context given, making one");

        memset (user, 0, sizeof (user));
        memset (s_flags, 0, sizeof (s_flags));

        db_read (user, 1024, PLATFORM_USERNAME);
        if (*user) {
            int flags = AUTH_FLAG_NONE;
            db_read (s_flags, 20, PLATFORM_FLAGS);
            if (*s_flags) {
                flags = strtol (s_flags, NULL, 10);
            }
            /* we don't really want locking here */
            /* not even if context insisist so, thought it should not */
            flags &= ~AUTH_FLAG_LOCK;
            auth_set_context (user, user,
                              flags | AUTH_FLAG_CANNOT_CANCEL);
            return input_secure(1);
        } else {
            warning("auth requested but there is no user");
        }
        return 0;
    }
    info ("switching to secure mode");
    return input_secure(1);
}

static int report_window_shown()
{
    struct domain *uivm = domain_uivm();
    char state_node[64];
    char *v;
    int r = 0;

    sprintf(state_node, "/local/domain/%d/report/state", uivm->domid);

    v = xenstore_read(state_node);

    if (v)
    {
        if (strcmp(v, "3") != 0)
        {
            r = 1;
        }

        free(v);
    }

    return r;
}

static gboolean
switcher_status_report_enabled(void)
{
    gboolean enabled = TRUE;

    if (!property_get_com_citrix_xenclient_xenmgr_config_ui_switcher_status_report_enabled_(
                    xcbus_conn, "com.citrix.xenclient.xenmgr", "/", &enabled)) {
        warning("failed to get status report property!");
        return TRUE;
    }

    return enabled;
}

static int switcher_status_report (void *opaque)
{
  char path[256];
  struct domain *d = domain_uivm();
  if (!d)
    return 0;
  if (auth_window_shown() || report_window_shown())
    return 0;
  if (!switcher_status_report_enabled())
    return 0;

  sprintf(path, "/local/domain/%d/report/state", d->domid);
  xenstore_write_int(1, path);
  sprintf(path, "/local/domain/%d/report/url", d->domid);
  xenstore_write("http://1.0.0.0/create_report.html", path);
  return 0;
}

enum video_adapter
{
    VIDEO_ADAPTER_DOMAIN_GONE,
    VIDEO_ADAPTER_DEFAULT,
    VIDEO_ADAPTER_INTEL,
    VIDEO_ADAPTER_OTHER
};

static enum video_adapter
switcher_get_slot_type(int slot)
{
    struct domain *d;
    char display_node[64];
    char *v;
    int type;

    d = domain_with_slot(slot);

    /* This should never happen if we got the good slot from domain_mouse_switch_config */
    if (!d)
        return VIDEO_ADAPTER_DOMAIN_GONE;

    sprintf(display_node, "/local/domain/%d/display/activeAdapter/0", d->domid);
    v = xenstore_read(display_node);
    if (!v)
      return VIDEO_ADAPTER_DEFAULT;

    info("XXXX Adapter for %d : %s", slot, v);

    /* The default adapter is named "OpenXT Xen Display Driver" */
    if ((*v == '\0') || (strcasestr(v, "openxt")))
      type = VIDEO_ADAPTER_DEFAULT; /* Default */
    else if (strcasestr(v, "intel"))
      type = VIDEO_ADAPTER_DEFAULT; /* Intel */
    else
      type = VIDEO_ADAPTER_OTHER; /* Other */

    free(v);

    return type;
}

static int
switcher_switch_makes_sense(int from_slot, int to_slot)
{
    enum video_adapter from, to;

    from = switcher_get_slot_type(from_slot);
    to   = switcher_get_slot_type(to_slot);

    /* Weird switches... */
    if (from == VIDEO_ADAPTER_DOMAIN_GONE ||
        to == VIDEO_ADAPTER_DOMAIN_GONE ||
        (from == VIDEO_ADAPTER_INTEL && to == VIDEO_ADAPTER_INTEL) || /* Switch from Intel to Intel */
	(from == VIDEO_ADAPTER_INTEL && to == VIDEO_ADAPTER_DEFAULT) || /* Switch from Intel to Default */
	(from == VIDEO_ADAPTER_DEFAULT && to == VIDEO_ADAPTER_INTEL) || /* Switch from Default to Intel */
	(from == VIDEO_ADAPTER_DEFAULT && to == VIDEO_ADAPTER_DEFAULT))   /* Switch from Default to Default */
      return 0;

    /* All the other cases look valid... */
    return 1;
}

void
switcher_switch_on_mouse(struct input_event *e, int x, int y)
{
    int slot = -1;
    int mouse_pos_x;
    char c;

    if (e->type != EV_ABS)
        return;
    if (!current)
        return;

    if (!input_domain_supports_abs(current))
        return;


    if ((x > MIN_MOUSE_ABS_X) && (x < MAX_MOUSE_ABS_X))
        return;

    if (db_exists("/switcher/enabled"))
        {
        char c;

        db_read(&c, 1, "/switcher/enabled");
        if (c == '0' || c == 'F' || c == 'f')
            return;
        }

    domain_mouse_switch_config(current);

    if (x == 0)
    {
        slot = current->mouse_switch.left;
        if (slot == -1)
            return;
        info("at the left of %d %d", current->slot, slot);
        if (!switcher_switch_makes_sense(current->slot, slot))
	        {
            info("mouse-switching from slot %d to slot %d doesn't seem to make sense, aborting.",
		    current->slot, slot);
            return;
	        }
         mouse_pos_x = MAX_MOUSE_ABS_X - 1;
    } else if (x == MAX_MOUSE_ABS_X)
    {
        slot = current->mouse_switch.right;
        if (slot == -1)
            return;
        info("at the right of %d %d", current->slot, slot);
	    if (!switcher_switch_makes_sense(current->slot, slot))
	    {
	        info("mouse-switching from slot %d to slot %d doesn't seem to make sense, aborting.",
            current->slot, slot);
            return;
	    }
        mouse_pos_x = MIN_MOUSE_ABS_X + 1;
    }

    if (slot == MOUSE_SWITCH_PREV)
    {
        if (!surface_current)
            return;
        slot = surface_current->slot;
    }
    if (slot >= 0)
      {
        /* Move the pointer to the bottom-left corner before switching */
        input_domain_set_mouse_pos(current, MIN_MOUSE_ABS_X, MAX_MOUSE_ABS_Y);
        if (switch_to_slot(slot, 1, 0) >= 0)
          {
            struct domain *d = domain_with_slot(slot);
            /* Move x to the opposite side */
            input_set_mouse_pos(mouse_pos_x, y);
            /* Refresh the mouse position in the guest, as y has changed */
            input_domain_set_mouse(d);
          }
      }
}

int32_t
switcher_get_focus ()
{
  if (!current)
    return 0;
  return current->domid;
}

#if 0
static void
switcher_watch_ac (const char *path, void *opaque)
{
  int ac = 0;
  char *tmp;

  if (!(tmp = xenstore_read (path)))
    return;
  ac = strtol (tmp, NULL, 10);
  free (tmp);
  // Force a switch to the current vm
  focused_slot != -1 &&// when we plug the power cable out
  if (ac == 0)
    switcher_switch (current, 0, 0);
}
#endif

static int
switcher_auth_or_lock_wrapper(void *opaque)
{
    if (auth_get_context() != NULL) {
        return switcher_auth_force();
    } else {
        return switcher_lock(0);
    }
}

void
switcher_switch_left(void)
{
  go_to_slot((void*)(current->slot - 1));
}

void
switcher_switch_right(void)
{
  go_to_slot((void*)(current->slot + 1));
}

void
switcher_init (void)
{
  int i;
  struct stat unused;

#if 0
  xenstore_watch (switcher_watch_ac, NULL, "/pm/ac_adapter");
#endif

  for (i = 0; i < 10; i++)
    {
      int left[] = { KEY_LEFTCTRL, i ? KEY_1 + (i - 1) : KEY_0, -1 };
      int right[] = { KEY_RIGHTCTRL, i ? KEY_1 + (i - 1) : KEY_0, -1 };
      input_add_binding (left, go_to_slot, force_go_to_slot, (void *) i);
      input_add_binding (right, go_to_slot, force_go_to_slot, (void *) i);
    }

  {
    int windows_alt[] = { KEY_LEFTMETA, KEY_LEFTALT, -1 };
    input_add_binding (windows_alt, go_to_next, NULL, (void *) 0);
  }

  {
    int ctrl_alt_backspace1[] =
      { KEY_LEFTCTRL, KEY_LEFTALT, KEY_BACKSPACE, -1 };
    int ctrl_alt_backspace2[] =
      { KEY_LEFTALT, KEY_LEFTCTRL, KEY_BACKSPACE, -1 };
    int ctrl_alt_backspace3[] =
      { KEY_RIGHTCTRL, KEY_RIGHTALT, KEY_BACKSPACE, -1 };
    int ctrl_alt_backspace4[] =
      { KEY_RIGHTALT, KEY_RIGHTCTRL, KEY_BACKSPACE, -1 };
    int ctrl_alt_r1[] =
      { KEY_LEFTCTRL, KEY_LEFTALT, KEY_R, -1 };
    int ctrl_alt_r2[] =
      { KEY_LEFTALT, KEY_LEFTCTRL, KEY_R, -1 };
    int ctrl_alt_r3[] =
      { KEY_RIGHTCTRL, KEY_RIGHTALT, KEY_R, -1 };
    int ctrl_alt_r4[] =
      { KEY_RIGHTALT, KEY_RIGHTCTRL, KEY_R, -1 };
    input_add_binding (ctrl_alt_backspace1, switcher_auth_or_lock_wrapper, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_backspace2, switcher_auth_or_lock_wrapper, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_backspace3, switcher_auth_or_lock_wrapper, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_backspace4, switcher_auth_or_lock_wrapper, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_r1, switcher_status_report, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_r2, switcher_status_report, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_r3, switcher_status_report, NULL,
                       (void *) 0);
    input_add_binding (ctrl_alt_r4, switcher_status_report, NULL,
                       (void *) 0);
  }
}
