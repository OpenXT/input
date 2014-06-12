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

#ifndef XEN_VKBD_H_
#define XEN_VKBD_H_

/* External includes */
#include <xen/io/kbdif.h>
#include <xenbackend.h>
#include <fb2if.h>

/* Structures definitions */
struct xen_vkbd_device
{
    xen_backend_t backend;
    int devid;
    void *page;
    struct event evtchn_event;
};

struct xen_vkbd_backend
{
    xen_backend_t backend;
    int domid;

    /* Device used for keyboard and relative mouse events */
    struct xen_vkbd_device *device;

    /* Device used for absolute mouse events */
    struct xen_vkbd_device *abs_device;
};

#endif /* XEN_VKBD_H_ */
