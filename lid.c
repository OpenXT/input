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

struct lid_switch *lid_switch_public;

struct lid_switch_private
{
    struct lid_switch   public;
    struct event        device_event;
    int                 fd;
    int                 status;
};

static void
wrapper_lid_read(int fd, short unused, void *opaque)
{
    struct lid_switch_private *lid_switch = opaque;;
    int read_sz = 0;
    struct input_event event;

    /* paranoia */
    if (fd != lid_switch->fd)
        return;

    memset(&event, 0, sizeof (event));
    if ((read_sz = read(lid_switch->fd, &event, sizeof (event))) <= 0)
    {
        event_del(&lid_switch->device_event);
        close(fd);
        free(lid_switch);
        return;
    }

    if (event.type == EV_SW && event.value != lid_switch->status)
    {
        xenstore_write(event.value ? "0" : "1", "/pm/lid_state");
        lid_switch->status = event.value;
        notify_com_citrix_xenclient_input_lid_state_changed(xcbus_conn, XCPMD_SERVICE, XCPMD_PATH);
    }
}

static int
lid_get_lid_state(struct lid_switch *lid_public)
{
    struct lid_switch_private *lid_switch = (struct lid_switch_private*) lid_public;
    return !lid_switch->status;
}

void
lid_create_switch_event(int fd)
{
    struct lid_switch_private *lid_switch;
    long bitmask;

    lid_switch = malloc(sizeof (struct lid_switch_private));
    memset(lid_switch, 0x0, sizeof (struct lid_switch_private));

    lid_switch->fd = fd;
    event_set(&lid_switch->device_event, fd, EV_READ | EV_PERSIST,
              wrapper_lid_read, (void*) lid_switch);
    event_add(&lid_switch->device_event, NULL);

    ioctl(fd, EVIOCGSW(sizeof (bitmask)), &bitmask);
    xenstore_write(bitmask ? "0" : "1", "/pm/lid_state");
    lid_switch->status = bitmask;

    lid_switch->public.get_lid_state = lid_get_lid_state;
    lid_switch_public = &lid_switch->public;
}

void lid_switch_release(bool infork)
{
    struct lid_switch_private *lid_switch =
        (struct lid_switch_private *) lid_switch_public;

    (void) infork;

    if (!lid_switch)
        return;

    /* FIXME: For the moment only close file descriptor */
    close(lid_switch->fd);
}
