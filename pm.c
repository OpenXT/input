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

/* Input values defined here because RPC tool cannot generate enum values
 * from IDL yet.
 */
#define XCPMD_INPUT_SLEEP          1
#define XCPMD_INPUT_BRIGHTNESSUP   2
#define XCPMD_INPUT_BRIGHTNESSDOWN 3

static int increase_brightness_binding(void *opaque)
{
    com_citrix_xenclient_xcpmd_indicate_input_(xcbus_conn, XCPMD_SERVICE, XCPMD_PATH, XCPMD_INPUT_BRIGHTNESSUP);
    return 0;
}

static int decrease_brightness_binding(void *opaque)
{
    com_citrix_xenclient_xcpmd_indicate_input_(xcbus_conn, XCPMD_SERVICE, XCPMD_PATH, XCPMD_INPUT_BRIGHTNESSDOWN);
    return 0;
}

static int sleep_binding(void *opaque)
{
    com_citrix_xenclient_xcpmd_indicate_input_(xcbus_conn, XCPMD_SERVICE, XCPMD_PATH, XCPMD_INPUT_SLEEP);
    return 0;
}

int host_pmop_in_progress(void)
{
    char *pmact = NULL;
    int r = 0;
    pmact = xenstore_read( "/xenmgr/pm-current-action" );
    r = pmact != NULL;
    free( pmact );
    return r;
}

void pm_init(void)
{
    int brightness_up[]   = { KEY_BRIGHTNESSUP, -1 };   /* 225 */
    int brightness_down[] = { KEY_BRIGHTNESSDOWN, -1 }; /* 224 */
    int sleep[]           = { KEY_SLEEP, -1 };          /* 142 */

    /* install the input bindings */
    input_add_binding(brightness_up, increase_brightness_binding, NULL,(void *) 0);
    input_add_binding(brightness_down, decrease_brightness_binding, NULL,(void *) 0);
    input_add_binding(sleep, sleep_binding, NULL,(void *) 0);

    info("pm: installed brightness and sleep binding handlers.");
}
