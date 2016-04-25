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

enum switcher_state
{
    SWITCHER_IDLE = 0,
    SWITCHER_OP_START = 1,
    SWITCHER_OP_COMPLETE = 2,
    SWITCHER_OP_ERROR = 3,
};

#define MOUSE_SWITCH_PREV -2

#define KEYFOLLOWMOUSE  1
#define CLICKHOLDFOCUS  2
#define CLONEEVENTS     4

#define FOCUSMODE_MAX  7

struct timer_t;
struct divert_info_t;

struct domain
{
    bool                    initialised;
    dmbus_client_t          client;
    int                     domid;
    int                     slot;
    int                     keyboard_led_code;
    int                     is_pvm;
    int                     is_in_s3;
    int                     abs_enabled;
    enum switcher_state     enter;
    enum switcher_state     leave;
    enum switcher_state     pre_enter;
    enum switcher_state     post_leave;
    int                     prev_keyb_domid;
    struct domain          *prev_keyb_domain_ptr;
    int                     disabled_surface;
    char                    *uuid;
    int                     sstate;
    struct
    {
        int                 left;
        int                 right;
    }                       mouse_switch;
    int                     vgpu_enabled;
    struct event            server_recv_event;
    struct timeval          last_input_event;
    struct timeval          time_of_s3;
    int                     is_pv_domain;
    struct xen_vkbd_backend *vkbd_backend;
    struct divert_info_t    *divert_info;
    int                     last_devslot;
    int                     has_secondary_gpu;
    struct sock_plugin*      plugin;          
    double                  rel_x_mult;
    double                  rel_y_mult;
    int                     desktop_xres;
    int                     desktop_yres;
    int                     num_active_adapters;
};

struct callbacklist
{
    void (*callback)(struct domain *d);
    struct callbacklist* next;
};

static struct callbacklist* domainstart_callback = NULL;

#define MAX_MODS 20

struct keypairs
{
    uint32_t mod_bits;
    uint32_t keycode;
};

struct divert_info_t
{
    uint32_t modifers[MAX_MODS];
    uint32_t mod_bits;
    struct keypairs* keylist;
    int	num_keys;
    uint32_t focusmode;
    struct domain* key_domain;
    struct domain* mouse_domain;
    
    uint32_t sframe_x1;
    uint32_t sframe_x2;
    uint32_t sframe_y1;
    uint32_t sframe_y2;
    uint32_t dframe_x1;
    uint32_t dframe_x2;
    uint32_t dframe_y1;
    uint32_t dframe_y2;
};

struct event_record
{
uint32_t magic;
uint16_t itype;
uint16_t icode;
uint32_t ivalue;
} __attribute__((__packed__));


#define buffersize (sizeof(struct event_record)*100)

struct dev_event
{
uint8_t slot;
bool remove;
bool add;
};


struct sock_plugin
{
char buffer[buffersize];
unsigned int bytes_remaining;
int position;
struct domain* src;
struct domain* dest;
int s;
int slot;
int recived_slot;
int dropped;
int slot_dropped;

int delayed;
char partialBuffer[sizeof(struct event_record)];

int resetall;
int deq_size;
struct dev_event* dev_events;
uint32_t error;
};
