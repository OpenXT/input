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

#include "keyboard.h"

static struct event backend_xenstore_event;

static void
xen_vkbd_handle_led(struct input_event *event,
                    struct domain *d)
{
    if (event->type != EV_KEY || event->value == 0)
        return;

    int led_code = 0;

    switch (event->code)
    {
        case KEY_CAPSLOCK:
            led_code = LED_CODE_CAPSLOCK;
            break;
        case KEY_NUMLOCK:
            led_code = LED_CODE_NUMLOCK;
            break;
        case KEY_SCROLLLOCK:
            led_code = LED_CODE_SCROLLLOCK;
            break;
    }

    if (0 == led_code)
        return;

    if ((d->keyboard_led_code & led_code) == led_code)
        d->keyboard_led_code &= (~led_code);
    else
        d->keyboard_led_code |= led_code;

    input_led_code(d->keyboard_led_code, d->domid);
}

void
xen_vkbd_send_event(struct domain *d,
                    struct input_event *event)
{
    xen_vkbd_handle_led(event, d);
    xen_event_send(d->vkbd_backend, event->type, event->code, event->value);
}

/* Backend device operations */

static xen_device_t
xen_vkbd_alloc(xen_backend_t backend, int devid, void *priv)
{
    struct xen_vkbd_backend *back = priv;
    struct xen_vkbd_device *dev;

    dev = malloc(sizeof (*dev));
    memset(dev, 0, sizeof(*dev));

    dev->devid = devid;
    dev->backend = backend;

    if (devid == 0) /* keyboard and relative mouse events device */
        back->device = dev;
    else if (devid == 1) /* absolute mouse events device */
        back->abs_device = dev;
    else
        warning("Unknown vkbd device id #%d\n", devid);

    return dev;
}

static int
xen_vkbd_init(xen_device_t xendev)
{
    struct xen_vkbd_device *dev = xendev;

    if (dev->devid == 1)
        backend_print(dev->backend, dev->devid, "feature-abs-pointer", "%d", 1);

    return 0;
}

static void
xen_vkbd_evtchn_handler(int fb, short event, void *priv)
{
    backend_evtchn_handler(priv);
}

static int
xen_vkbd_connect(xen_device_t xendev)
{
    struct xen_vkbd_device *dev = xendev;
    struct xenkbd_page *page;
    int fd;

    fd = backend_bind_evtchn(dev->backend, dev->devid);
    if (fd < 0)
        return -1;

    event_set(&dev->evtchn_event, fd, EV_READ | EV_PERSIST,
              xen_vkbd_evtchn_handler,
              backend_evtchn_priv(dev->backend, dev->devid));
    event_add(&dev->evtchn_event, NULL);

    dev->page = backend_map_shared_page(dev->backend, dev->devid);
    if (!dev->page)
        return -1;

    return 0;
}


static void
xen_vkbd_disconnect(xen_device_t xendev)
{
    struct xen_vkbd_device *dev = xendev;

    event_del(&dev->evtchn_event);
    backend_unbind_evtchn(dev->backend, dev->devid);

    if (dev->page)
    {
        backend_unmap_shared_page(dev->backend, dev->devid, dev->page);
        dev->page = NULL;
    }
}


static void
xen_vkbd_free(xen_device_t xendev)
{
    struct xen_vkbd_device *dev = xendev;

    xen_vkbd_disconnect(dev);

    // FIXME : Tell xen_vkbd_backend we are gone ?

    free(dev);
}


static struct xen_backend_ops xen_vkbd_backend_ops = {
    xen_vkbd_alloc,
    xen_vkbd_init,
    xen_vkbd_connect,
    xen_vkbd_disconnect,
    NULL,
    NULL,
    NULL,
    xen_vkbd_free
};

/* Backend creation function */

void
xen_vkbd_backend_create(struct domain *d)
{
    struct xen_vkbd_backend *backend;

    backend = malloc(sizeof (*backend));

    backend->domid = d->domid;

    backend->backend = backend_register("vkbd", d->domid, &xen_vkbd_backend_ops, backend);
    if (!backend->backend)
    {
        free(backend);
        return;
    }

    d->vkbd_backend = backend;
}


/* Backend init functions */

static void
xen_backend_handler(int fd, short event, void *priv)
{
    backend_xenstore_handler(priv);
}

void
xen_backend_init(int dom0)
{
    int rc;

    rc = backend_init(dom0);
    if (rc)
    {
        warning("Failed to initialize libxenbackend");
        return;
    }

    event_set(&backend_xenstore_event,
              backend_xenstore_fd(),
              EV_READ | EV_PERSIST,
              xen_backend_handler,
              NULL);
    event_add(&backend_xenstore_event, NULL);
}

void xen_backend_close(void)
{
    /* FIXME: it's not complete */
    event_del(&backend_xenstore_event);
    backend_close();
}
