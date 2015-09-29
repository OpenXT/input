/*
 * Copyright (c) 2013 Citrix Systems, Inc.
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

static int
xen_event_write_page(struct xen_vkbd_device *dev,
                     union xenkbd_in_event *event)
{
    uint32_t prod;
    struct xenkbd_page *page = dev->page;

    if (!page)
        return -1;

    prod = page->in_prod;
    xen_mb();
    XENKBD_IN_RING_REF(page, prod) = *event;
    xen_wmb();
    page->in_prod = prod + 1;

    return backend_evtchn_notify(dev->backend, dev->devid);
}

/* Send a keyboard (or mouse button) event */
static int
xen_event_send_key(struct xen_vkbd_backend *backend, bool down, int keycode)
{
    union xenkbd_in_event event;

    memset(&event, 0, XENKBD_IN_EVENT_SIZE);
    event.type = XENKBD_TYPE_KEY;
    event.key.pressed = down ? 1 : 0;
    event.key.keycode = keycode;

    return xen_event_write_page(backend->abs_device, &event);
}

/* Send a relative mouse movement */
static int
xen_event_send_motion(struct xen_vkbd_backend *backend, int rel_x, int rel_y, int rel_z)
{
    union xenkbd_in_event event;

    memset(&event, 0, XENKBD_IN_EVENT_SIZE);
    event.type = XENKBD_TYPE_MOTION;
    event.motion.rel_x = rel_x;
    event.motion.rel_y = rel_y;
    event.motion.rel_z = rel_z;

    return xen_event_write_page(backend->device, &event);
}

/* Send an absolute mouse movement */
static int
xen_event_send_position(struct xen_vkbd_backend *backend, int abs_x, int abs_y, int z)
{
    union xenkbd_in_event event;

    memset(&event, 0, XENKBD_IN_EVENT_SIZE);
    event.type = XENKBD_TYPE_POS;
    event.pos.abs_x = abs_x;
    event.pos.abs_y = abs_y;
    event.pos.rel_z = z;

    return xen_event_write_page(backend->abs_device, &event);
}

void
xen_event_send(struct xen_vkbd_backend *backend,
               uint16_t type,
               uint16_t code,
               int32_t value)
{
    static int absolute_x=0, absolute_y=0, absolute_z=0, absolute=0;
    static int relative_x=0, relative_y=0, relative_z=0, relative=0;

    if (type == EV_KEY)
        xen_event_send_key(backend, value, code);

    /* Mouse motion */
    if (type == EV_REL)
    {
        switch (code)
        {
            case REL_X:
                relative_x = value;
                break;
            case REL_Y:
                relative_y = value;
                break;
            case REL_WHEEL:
                relative_z = -value;
                break;
        }
	relative++;
    }

    if (type == EV_ABS)
    {
        switch (code)
        {
            case ABS_X:
                absolute_x = value;
                break;
            case ABS_Y:
                absolute_y = value;
                break;
            case ABS_WHEEL:
                absolute_z = -value;
                break;
        }
	absolute++;
    }

    if (type == EV_SYN && code == SYN_REPORT)
    {
        if (relative)
        {
            xen_event_send_motion(backend, relative_x, relative_y, relative_z);
            relative = relative_x = relative_y = relative_z = 0;
        }
        if (absolute)
        {
            xen_event_send_position(backend, absolute_x, absolute_y, absolute_z);
            absolute = absolute_z = 0;
        }
    }
}


