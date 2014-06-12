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

typedef int (*input_binding_cb_t)(void *);
#define N_DEVS              64
#define INPUTSLOT_DEFAULT  -1
#define INPUTSLOT_INVALID  -2

#define EV_DEV      0x6
#define DEV_SET     0x1
#define DEV_CONF    0x2
#define DEV_RESET   0x3

#ifndef SYN_DROPPED
# define SYN_DROPPED 0x3
#endif
