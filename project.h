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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <math.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <linux/types.h>
#include <linux/input.h>
#include <linux/kd.h>
//#include <linux/keyboard.h>

#include <dlfcn.h>
#include <pwd.h>
#include <time.h>
#include <dirent.h>
#include <syslog.h>


#include <openssl/md4.h>
#include <openssl/sha.h>

#include <libudev.h>
#include <glib.h>
#include <dbus/dbus-glib.h>
#include <stdint.h>
#include <xenctrl.h>
#include <xenstore.h>

#include <xcdbus.h>
#include <libdmbus.h>

#include <event.h>

#include <xcxenstore.h>

#include "rpcgen/db_client.h"
#include "rpcgen/surfman_client.h"  
#include "rpcgen/xenmgr_client.h"
#include "rpcgen/xenmgr_host_client.h"
#include "rpcgen/xcpmd_client.h"
#include "rpcgen/input_daemon_server_obj.h"
//#include "input_daemon_server_marshall.h"

#define KEYMAP_CONF_FILE "/config/keyboard.conf"
#define KEYMAP_LIST_FILE "/usr/share/xenclient/keyboards"
#define KEYMAP_LIST_MAX 1024

#include "input.h"
#include "timer.h"

#include "xc_input_socket_protocol.h"

#include "switch.h"
#include "util.h"
#include "bus.h"
#include "secure.h"
#include "user.h"
#include "gesture.h"
#include "xen_vkbd.h"
#include "lid.h"

#define NDOMAIN_MAX 20
#define LONG_BITS (sizeof(long) * 8)
#define NBITS(x) (((x) + LONG_BITS - 1) / LONG_BITS)
#define OFF(x)   ((x) % LONG_BITS)
#define LONG(x)  ((x) / LONG_BITS)
#define TEST_BIT(bit, array) (array[LONG(bit)] & (1 << OFF(bit)))
#define ARRAY_LEN(arr) (sizeof (arr) / sizeof ((arr)[0]))

#define BTN_WORDS 3
#define ABS_WORDS 2

#define SUBTYPE_NONE       0
#define SUBTYPE_TABLET     1
#define SUBTYPE_STYLUS     2
#define SUBTYPE_MONOTOUCH  3
#define SUBTYPE_MULTITOUCH 4

#define MIN_MOUSE_ABS_X 0
#define MAX_MOUSE_ABS_X 0x7fff
#define MIN_MOUSE_ABS_Y 0
#define MAX_MOUSE_ABS_Y 0x7fff

/* The default resolution to use when the VM doesn't report any */
#define DEFAULT_RESOLUTION_X	1920
#define DEFAULT_RESOLUTION_Y	1080

/* A dividend at which pixel precision works well when converting relative
   events to absolute. We have two thresholds, one for slow moves, the other
   for slightly faster moves. */
#define MOUSE_DIV_THRESHOLD_1 6000
#define MOUSE_DIV_THRESHOLD_2 10000

enum input_device_type
{
    HID_TYPE_KEYBOARD = 1,
    HID_TYPE_MOUSE,
    HID_TYPE_TOUCHPAD,
    HID_TYPE_TABLET,
    HID_TYPE_THINKPAD_ACPI
};

//#define debug 1
//#define debug_packets 1

#include "prototypes.h"
