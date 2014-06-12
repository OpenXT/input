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


int     main(int argc,char *argv[])
{

    event_init();
    bus_init();
    xenstore_init();
    input_init();
    domain_init();
    server_init();

    switcher_init();

    keymap_init();

    pm_init();

    xen_backend_init(0);

    socket_server_init();

    info ("Dispatching events (Event lib v%s. Method %s)",event_get_version (),event_get_method ());

    event_dispatch();

    socket_server_close();

    return 0;
}
