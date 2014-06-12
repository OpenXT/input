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

extern void handle_switcher_abs(void *priv, struct msg_switcher_abs *msg, size_t msglen);
extern void handle_switcher_leds(void *priv, struct msg_switcher_leds *msg, size_t msglen);
extern void handle_switcher_shutdown(void *priv, struct msg_switcher_shutdown *msg, size_t msglen);

void server_kill_domain(struct domain *d)
{
    event_del(&(d->server_recv_event));
    /* close(d->socket); */
    domain_gone(d);
}

static struct dmbus_rpc_ops input_rpc_ops = {
  .switcher_abs = handle_switcher_abs,
  .switcher_leds = handle_switcher_leds,
  .switcher_shutdown = handle_switcher_shutdown
};

static struct event rpc_connect_event;

static void
rpc_handler (int fd, short event, void *priv)
{
  struct domain *d = priv;

  dmbus_handle_events(d->client);
}

static int
rpc_connect (dmbus_client_t client,
             int domain,
             DeviceType type,
             int dm_domain,
             int fd,
             struct dmbus_rpc_ops **ops,
             void **priv)
{
  struct domain *d;
  info("input_server: DM connected. domid %d device type %d\n", domain, type);

  switch (type)
    {
    case DEVICE_TYPE_INPUT:
    case DEVICE_TYPE_INPUT_PVM:
      d = domain_create(client, domain, type);
      if (!d) {
        info("input_server: Failed to create domain.\n");
        return -1;
      }
      *ops = &input_rpc_ops;
      break;
    default:
      info("input_server: Bad device type %d\n", type);
      return -1;
    }

  event_set (&d->server_recv_event, fd, EV_READ | EV_PERSIST, rpc_handler, d);
  event_add (&d->server_recv_event, NULL);
  *priv = d;

  return 0;
}

static void
rpc_disconnect (dmbus_client_t client, void *priv)
{
  struct domain *d = priv;

  info("input_server: DM %d disconnected\n", d->domid);

  event_del (&d->server_recv_event);

  server_kill_domain (d);
}

static struct dmbus_service_ops service_ops = {
  .connect = rpc_connect,
  .disconnect = rpc_disconnect,
};

int
server_init (void)
{
  int fd;

  info("input_server: dmbus_init\n");

  fd = dmbus_init (DMBUS_SERVICE_INPUT, &service_ops);
  if (fd == -1)
    {
      info("input_server: dmbus_init failed\n");
      error ("Failed to initialize dmbus");
      return fd;
    }

  event_set (&rpc_connect_event, fd, EV_READ | EV_PERSIST,
             (void *) dmbus_handle_connect, NULL);
  event_add (&rpc_connect_event, NULL);

  return 0;
}
