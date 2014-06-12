/*
 * Copyright (c) 2012 Citrix Systems, Inc.
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

#define DEATH_WINDOW_LENGTH 30

#define NSLOTS 11

struct slot
{
  int domid;
  struct timeval death_window;
  int pvm;
};

static struct slot slots[NSLOTS] = {};

void
focus_expect_death (struct domain *d)
{
  if (!d)
    return;
  if (d->slot < 0 || d->slot >= NSLOTS)
    return;

  gettimeofday (&slots[d->slot].death_window, NULL);
}

void
focus_dont_expect_death (struct domain *d)
{
  if (!d)
    return;
  if (d->slot < 0 || d->slot >= NSLOTS)
    return;

  slots[d->slot].death_window.tv_sec = 0;
}


static int
expecting_death (int slot)
{
  struct timeval now, diff;

  gettimeofday (&now, NULL);
  timersub (&now, &slots[slot].death_window, &diff);

  if (diff.tv_sec < 0)
    return 0;
  if (diff.tv_sec > DEATH_WINDOW_LENGTH)
    return 0;

  return 1;
}

static void
focus_initial_switch(struct domain *d)
{
    struct timeval now;
    /* switch to VM if its either a PVM or matches the focus uuid */
    char *focus_uuid = NULL;
    focus_uuid = xenstore_read("/local/domain/0/switcher/focus-uuid");
    if (d->is_pvm || (focus_uuid && d->uuid && !strcmp(focus_uuid, d->uuid))) {
        info("focus switch on domain creation to uuid=%s domid=%d", d->uuid, d->domid);
        switcher_switch(d, 0, 0);
    }
    free( focus_uuid );
    gettimeofday(&now,NULL);
    d->last_input_event = now;

}

void
focus_update_domain (struct domain *d)
{

  if (!d)
    return;
  if (d->slot < 0 || d->slot >= NSLOTS)
    return;

  if ((slots[d->slot].domid != d->domid) || (slots[d->slot].pvm != d->is_pvm))
    {
      info
        ("new incumbent for slot %d, last_domid=%d new_domid=%d last_was_pvm=%d, new_is_pvm=%d",
         d->slot, slots[d->slot].domid, d->domid, slots[d->slot].pvm,
         d->is_pvm);
    }

  /*Cancel any pending reboot timers */
  focus_dont_expect_death (d);

  /*Update the pvm status: XXX note that this will be wrong for a just connected VM, but we don't care */
  slots[d->slot].pvm = d->is_pvm;

  focus_initial_switch(d);
}

void
focus_domain_gone (struct domain *d)
{
  if (!d->is_pvm)
    return;
  info ("*** Trouble brewing - a non focused pvm died ***");

  if ((d->slot < 0) || (d->slot >= NSLOTS))
    return;

  if (expecting_death (d->slot))
    {
      info ("- but it's rebooting so that's ok");
      return;
    }

  switcher_switch (domain_uivm (), 0, 0);
}
