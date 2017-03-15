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

#include "project.h"

#include "keyboard.h"

#define EVENT_FILES         "/dev/input/event"

#define VALUE_KEY_UP     0
#define VALUE_KEY_DOWN   1
#define VALUE_KEY_REPEAT 2

#define KEY_STATUS_SIZE 256
#define BUTTONS_SIZE 3

#define MIN_CONFIG_MOUSE_SPEED 1
#define MAX_CONFIG_MOUSE_SPEED 10
#define DEFAULT_CONFIG_MOUSE_SPEED 5

static void wrapper_revert_to_auth(int fd, short event, void *opaque);
static void wrapper_input_read(int fd, short event, void *opaque);
static void wrapper_force_timer(int fd, short event, void *opaque);
static void wrapper_input_lock_timer(int fd, short event, void *opaque);
static void send_config(struct domain *d, int slot);
static void broadcast_config(int slot);
static int input_check_secure_mode();
static void dup_mouse_clicks(struct domain* d);
static int check_mouse_keys(struct domain* d, int slot, struct input_event* e);

static struct domain *mouse_dest;

static struct domain *mouse_parent;
static struct domain *keyb_parent;

static struct udev *udev;
static struct udev_monitor *udev_mon;

static int current_grab = 0;

struct timeval global_last_input_event;

int platform_lock_timeout = -1;
static int resistance = -1;

static double mouse_x = 0;
static double mouse_y = 0;
static double mouse_speed = 0;
static double mouse_speed_threshold_1 = 0;
static double mouse_speed_threshold_2 = 0;

struct mousebutton
{
int code;
struct domain* domain;
int slot;
int x;
int y;
};

#define MAX_NUM_PRESSED 5
static struct mousebutton mouse_pressed[MAX_NUM_PRESSED];
static int  num_mouse_pressed;

static uint32_t buttons[BUTTONS_SIZE];


static int mouse_button = 0;

static uint32_t keyb_modbits = 0;

int keyb_waits_for_click = 0;


struct input_binding
{
    int *binding;
    input_binding_cb_t cb;
    input_binding_cb_t force_cb;
    void *opaque;
    int matched;
    int down;
    int force_ticks;
};

#define AUTH_FIELD_USERNAME 0
#define AUTH_FIELD_PASSWORD 1
#define AUTH_FIELD_PASSWORD_CONFIRM 2
#define AUTH_FIELD_PASSWORD_PREVIOUS 3

struct auth_input_state
{
    int cursor;
    char *passwd;
    char *passwd_confirm;
    char *passwd_previous;      /* containing previous password when we request password change */
    char *username;
    int current_field;
};

static struct input_dev
{
    enum input_device_type types[N_DEVS];
    int fds[N_DEVS];
    int grab[N_DEVS];
    int key_status[KEY_STATUS_SIZE];
    struct input_binding *bindings;
    int nbinding;
    int secure_mode;            /* are keys being swallowed by dom0 */
    int collect_password;       /* are we collecting password */
    struct auth_input_state auth;
    struct event device_events[N_DEVS];
} input_dev;

static struct timeval then = { 0, 0 };

static struct event revert_to_auth_event, udev_monitor_event;

static void timeout_start(void)
{
    gettimeofday(&then, NULL);
}

static int timeout_expired(void)
{
    struct timeval now, diff;

    gettimeofday(&now, NULL);
    timersub(&now, &then, &diff);

    return ((diff.tv_sec < 0) || (diff.tv_sec > 4));
}


static void update_grabs(void)
{
    int i = 0;

    for (i = 0; i < N_DEVS; i++)
        if (input_dev.fds[i] != -1 && input_dev.grab[i] != current_grab)
        {
            if (ioctl(input_dev.fds[i], EVIOCGRAB, current_grab) == -1)
                info("grab failed for event%d: %s", i, strerror(errno));
            else
                input_dev.grab[i] = current_grab;
        }
}

static void do_grab(int grab)
{
    info("grab=%d", grab);
    current_grab = grab;
    update_grabs();
}

/* The following defines are only tempory measure - they should come from input.h */

#define ABS_MT_SLOT         0x2f        /* MT slot being modified */
#define ABS_MT_PRESSURE     0x3a        /* Pressure on contact area */
#define ABS_MT_DISTANCE     0x3b        /* Contact hover distance */

typedef enum
{
    nope,
    send_sync,
    send_left,
    send_left_up,
} sim_press;

typedef enum
{
    discared_event,
    send_event,
    events_queued
} inputevent_action;


static inputevent_action demultitouch(struct input_event *e)
{
    static sim_press deferpressed = nope;
    static int draining = 0;
    static int slot = 0;
    static int pressed = 0;
    static int had_slot0 = 0;

    if (draining && deferpressed == nope)
        info("WARNING! draining but not deferpressed!\n");

    if ((slot != 0) || (draining))
    {
        if (!draining)
        {
            if ((e->type == EV_SYN) && (e->code == SYN_REPORT))
            {
                slot = 0;
                had_slot0 = false;
            }

            if ((e->type == EV_ABS) && (e->code == ABS_MT_SLOT))
            {
                slot = e->value;
                if (!slot)
                    had_slot0 = true;
            }
        }

        switch (deferpressed)
        {
        case send_sync:
            e->type = EV_SYN;
            e->code = SYN_REPORT;
            e->value = 0;
            deferpressed = nope;
            draining = 0;
            return send_event;

        case send_left:
            e->type = EV_KEY;
            e->code = BTN_LEFT;
            e->value = 1;
            deferpressed = send_sync;
            return (draining) ? events_queued : send_event;
        case send_left_up:
            e->type = EV_KEY;
            e->code = BTN_LEFT;
            e->value = 0;
            deferpressed = send_sync;
            return (draining) ? events_queued : send_event;
        default:
            return discared_event;
        }
        return discared_event;
    }

    if (e->type == EV_SYN)
    {
        if (e->code == SYN_MT_REPORT)
        {
            e->code = SYN_REPORT;
            slot = 1;
        }
        else if (e->code == SYN_REPORT)
        {
            had_slot0 = false;

            if (deferpressed)
            {
                draining = true;
                return events_queued;
            }
        }
        return send_event;
    }
    if (e->type == EV_ABS)
    {
        switch (e->code)
        {
        case ABS_MT_POSITION_X:
            e->code = ABS_X;
            break;
        case ABS_MT_POSITION_Y:
            e->code = ABS_Y;
            break;
        case ABS_MT_TRACKING_ID:
            if (slot == 0)
            {
                int nowpressed = (e->value != -1);

                if (pressed != nowpressed)
                {
                    pressed = nowpressed;
                    had_slot0 = true;

                    if (pressed)
                        deferpressed = send_left;
                    else
                        deferpressed = send_left_up;

                    return discared_event;
                }
                break;
            }
        case ABS_MT_SLOT:
            slot = e->value;

            if (slot != 0)
            {
                if (had_slot0)
                {
                    e->type = EV_SYN;
                    e->code = SYN_REPORT;
                    e->value = 0;
                    return send_event;
                }
                return discared_event;
            }
        case ABS_MT_TOUCH_MAJOR:
        case ABS_MT_TOUCH_MINOR:
        case ABS_MT_WIDTH_MAJOR:
        case ABS_MT_WIDTH_MINOR:
        case ABS_MT_ORIENTATION:
        case ABS_MT_TOOL_TYPE:
        case ABS_MT_BLOB_ID:

        case ABS_MT_PRESSURE:
        case ABS_MT_DISTANCE:
            had_slot0 = true;
            return discared_event;
        }

        return (slot == 0);
    }
    had_slot0 = true;
    return send_event;
}

static void send_config_reset(struct domain *d, uint8_t slot)
{
    struct msg_input_config_reset msg;
    msg.slot = slot;
    if (d->client)
        input_config_reset(d->client, &msg, sizeof(msg));

    if (d->plugin)
       send_plugin_dev_event(d->plugin, DEV_RESET, slot); 
}


void input_set_focus_change(void)
{
    static struct domain* od=NULL;
    static int osecure=0;
    int secure=input_check_secure_mode();

    if ((secure!=osecure) || (!secure && (get_keyb_dest() != od)))
        {
        osecure=secure;
        od = get_keyb_dest();

        if (secure)
            notify_com_citrix_xenclient_input_keyboard_focus_change(xcbus_conn, SERVICE, OBJ_PATH, "S");
        else
            notify_com_citrix_xenclient_input_keyboard_focus_change(xcbus_conn, SERVICE, OBJ_PATH,
                                                                    get_keyb_dest() ? get_keyb_dest()->uuid : "");
        }
}

/* Our drivers seem to claim capabilities they do not have, in order to satisfy legacy behavior */
/* This code tries to correct for this */

void fixabsbits(uint64_t * bits)
{
    if (*bits & ((uint64_t) 1 << ABS_MT_POSITION_X))
        *bits &= ~(1 << ABS_X);
    if (*bits & ((uint64_t) 1 << ABS_MT_POSITION_Y))
        *bits &= ~(1 << ABS_Y);
}

void fixkeybits(unsigned long *keybits, uint64_t * absbits, int slot)
{
    int key;
    unsigned long *kb;
    unsigned long bit;
    if (input_dev.types[slot] == HID_TYPE_TOUCHPAD)
    { // crear out any keys that don't look like mouse keys
        keybits[2] = 0;
        keybits[0] &= ~ (unsigned long)0x1FF;
    } else

    if ((*absbits & ((uint64_t) 1 << ABS_MT_POSITION_X)) && (*absbits & ((uint64_t) 1 << ABS_MT_POSITION_Y)))
    {
        info("Clearing btn_touch for multitouch device.\n");
        bit = 1 << OFF(key = BTN_TOUCH - BTN_MISC);
        keybits[LONG(key)] &= ~ bit;
    }
}

int relbits_to_absbits(struct domain *d, unsigned long *relbits, uint64_t * absbits)
{

    if ((*relbits & (1 << REL_X)) && (*relbits & (1 << REL_Y)))
    {
        *relbits &= ~((1 << REL_Y) | (1 << REL_X));
        *absbits |= ((1 << ABS_Y) | (1 << ABS_X));
        return 1;
    }
    return 0;
}

static int anyset(unsigned long *keybit)
{
    int i, a;
    a = 0;
    for (i = 0; i < BTN_WORDS; i++)
        a |= keybit[i];
    return a;
}


static void send_config_wrap(struct domain *d, void *o)
{
    int slot = (int) o;
    send_config(d, slot);
    if (d->plugin)
        send_plugin_dev_event(d->plugin, DEV_CONF, slot);
}

static void send_config_reset_wrap(struct domain *d, void *o)
{
    uint8_t slot = (int) o;
    send_config_reset(d, slot);
}

static void broadcast_removed_dev(int slot)
{
    iterate_domains(send_config_reset_wrap, (void *) slot);
}

static void broadcast_config(int slot)
{
    iterate_domains(send_config_wrap, (void *) slot);
}

static void send_config(struct domain *d, int slot)
{
    if (!d || d->is_pv_domain)
        return;

    int fd = input_dev.fds[slot];
    int ret = 0;
    int is_touchpad=(input_dev.types[slot] == HID_TYPE_TOUCHPAD);


    unsigned long eventtypes[NBITS(EV_MAX)];

    if ((ret = ioctl(fd, EVIOCGBIT(0, sizeof(eventtypes)), eventtypes)) < 0)
    {
        info("Could not get evbits.\n");
        return;
    }

    struct msg_input_config *msg;
    int absposiblesize = NBITS(ABS_MAX) * sizeof(unsigned long);
    int relposiblesize = NBITS(REL_MAX) * sizeof(unsigned long);
    int evbits = (uint32_t) eventtypes[0];
    int abssize = (evbits & (1 << EV_ABS)) ? absposiblesize : 0;
    int relsize = (evbits & (1 << EV_REL)) || is_touchpad ? relposiblesize : 0;
    int btnsize = (evbits & (1 << EV_KEY)) ? BTN_WORDS * sizeof(unsigned long) : 0;

    unsigned long keybit[NBITS(KEY_OK)];
    unsigned long absbit[NBITS(ABS_MAX)];
    unsigned long relbit[NBITS(REL_MAX)];
    unsigned long *btnbit = &(keybit[NBITS(BTN_MISC)]);
    memset(absbit, 0, absposiblesize);


    if (abssize)
    {
        if (is_touchpad)
        {
            absbit[0] |= ((1 << ABS_Y) | (1 << ABS_X));        
        }
             else  if ((ret = ioctl(fd, EVIOCGBIT(EV_ABS, abssize), absbit)) < 0)
        {
            info("Error: Could not get abs bits.\n");
            abssize = 0;
        }
        else
        {
            fixabsbits((uint64_t *) absbit);
        }
    }

    if (relsize)
    {
        if (is_touchpad)
        {
           relbit[0] = 1 << REL_WHEEL; 
        }
        else if ((ret = ioctl(fd, EVIOCGBIT(EV_REL, relsize), relbit)) < 0)
        {
            info("Error: Could not get rel bits.\n");
            relsize = 0;
        }
        else
        {
            if (relbits_to_absbits(d, relbit, (uint64_t *) absbit))
            {
                info("Making relative mouse absolute.\n");

                abssize = absposiblesize;
            }
            if (relbit[0] == 0)
            {
                info("All rel bits removed.\n");
                relsize = 0;
            }

        }
    }


    /* Ugly fix instead of cast to uint64_t */
    if (absbit[0] == 0 && absbit[1] == 0)
        abssize = 0;


    if (btnsize)
    {
        if ((ret = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit)) < 0)
        {
            info("Error: Could not get button bits.\n");
            btnsize = 0;
        }
        else
        {
            fixkeybits(btnbit, (uint64_t *) absbit, slot);
            int any = anyset(btnbit);
            if (!any)
            {
                info("Keys reported, but not in range.\n");
                btnsize = 0;
            }
        }
    }


    int totalsize = abssize + relsize + btnsize + sizeof(struct msg_input_config) - sizeof(uint32_t);

    msg = alloca(totalsize);

    // Fill in structure

    msg->c.evbits =
        (evbits & 1) | ((abssize) ? (1 << EV_ABS) : 0) | ((relsize) ? (1 << EV_REL) : 0) | ((btnsize) ? (1 << EV_KEY) :
                                                                                            0);
    msg->c.slot = slot;

    uint8_t *payload = (uint8_t *) msg->c.bits;

    if (ioctl(fd, EVIOCGNAME(sizeof(msg->c.name)), msg->c.name) == -1)
    {
        sprintf(msg->c.name, "S:%d T:%d", slot, input_dev.types[slot]);
    }
    msg->c.name[sizeof(msg->c.name) - 1] = 0;
    info("Found device: %s\n", msg->c.name);

    if (abssize)
    {
#ifdef debug
        print_abs_bit_meaning((unsigned long *) absbit);
#endif
        memcpy(payload, absbit, abssize);
        payload += abssize;
    }
    if (relsize)
    {
#ifdef debug
        print_rel_bit_meaning((unsigned long *) relbit);
#endif
        memcpy(payload, relbit, relsize);
        payload += relsize;
    }

    if (btnsize)
    {
#ifdef debug
        print_btn_bit_meaning(btnbit);
#endif
        memcpy(payload, btnbit, btnsize);
    }

    input_config(d->client, msg, totalsize);
}

static void send_slot(struct domain *d)
{
    struct msg_dom0_input_event msg;

    if (NULL == d->client)
        return;

    msg.type = EV_DEV;
    msg.code = DEV_SET;
    msg.value = d->last_devslot;
    dom0_input_event(d->client, &msg, sizeof(msg));
}

static void input_send(struct domain *d, int slot, struct input_event *e)
{
    struct msg_dom0_input_event msg;
    if (!d)
        return;

    if (d->plugin)
        {
        send_plugin_event(d,slot,e);
        return;
        }

    if ((!(d->is_pv_domain))  && (d->last_devslot!=slot))
    {
        d->last_devslot = slot;
        send_slot(d);
    }

#ifdef debug_packets
    if (e) debug_packet(slot, e);
#endif

    inputevent_action ia;

    do
    {
        ia = send_event;

        if (d->is_pv_domain)
        {
            ia = demultitouch(e);
            if (ia == discared_event)
                return;
            xen_vkbd_send_event(d, e);
        }
        else
        {

            msg.type = e->type;
            msg.code = e->code;
            msg.value = e->value;

            if (NULL != d->client)
                dom0_input_event(d->client, &msg, sizeof(msg));
        }
    }
    while (ia == events_queued);
}

void input_domain_gone(struct domain *d)
{
    if (mouse_dest == d)
    {
        info("lost the mouse domain, ditching mouse_events for now");
        mouse_dest = NULL;
    }
    if (get_keyb_dest() == d)
    {
        info("lost the keyboard domain, ditching keyboard_events for now");
        set_keyb_dest(NULL);
    }
    if (keyb_parent == d)
    {
        keyb_parent = NULL;
        set_keyb_dest(NULL);
    }

    if (mouse_parent == d)
    {
        mouse_parent = NULL;
        mouse_dest = NULL;
    }

    int i;
    for (i=0; i<num_mouse_pressed;)
    {
        if (mouse_pressed[i].domain==d)
            {
            num_mouse_pressed--;
            for (;i<num_mouse_pressed; i++)
                memcpy(&mouse_pressed[i], &mouse_pressed[i+1], sizeof(struct mousebutton));
            }
            else
                i++;
    }

}

static void input_keyboard_reset(int reset_mouse_domain)
{
    int i = 0;
    struct input_event e;

    if (get_keyb_dest() == NULL)
        return;

    info("input_keyboard_reset\n");
    for (i = 0; i < KEY_STATUS_SIZE; i++)
        if (input_dev.key_status[i])
        {
            e.type = EV_KEY;
            e.code = i;
            e.value = 0;

            input_dev.key_status[i] = 0;
            input_send(get_keyb_dest(), INPUTSLOT_DEFAULT, &e);

            if (reset_mouse_domain && mouse_dest && (get_keyb_dest() != mouse_dest))
                input_send(mouse_dest, INPUTSLOT_DEFAULT, &e);
        }
}

void divert_domain_gone(struct divert_info_t* dv,struct domain* d)
{
    if (dv->key_domain==d)
        dv->key_domain=NULL;
    if (dv->mouse_domain==d)
        dv->mouse_domain=NULL;
}

void send_keypair(struct keypairs *key, struct domain *d)
{
    struct input_event e;
    int len = d->divert_info->modifers[0];
    uint32_t *m = &d->divert_info->modifers[1];
    int i;

    e.type = EV_KEY;
    e.value = 1;

    for (i = 0; i < len; i++)
    {
        if (key->mod_bits & (1 << i))
        {
            e.code = m[i];
            input_send(d, INPUTSLOT_DEFAULT, &e);
        }
    }
    e.code = key->keycode;
    input_send(d, INPUTSLOT_DEFAULT, &e);

/* Now release */

    e.value = 0;
    input_send(d, INPUTSLOT_DEFAULT, &e);
    for (i = 0; i < len; i++)
    {
        if (key->mod_bits & (1 << i))
        {
            e.code = m[i];
            input_send(d, INPUTSLOT_DEFAULT, &e);
        }
    }

}

int filter_keys(struct input_event *e)
{
    struct divert_info_t *dv;
    if (e->type != EV_KEY)
        return 0;

    if (!keyb_parent)
        return 0;
    dv = keyb_parent->divert_info;
    if (!dv)
    {
        info("Input_server:filter_keys Warning!\n");
        return 0;
    }
    if (!dv->keylist)
        return 0;
    int len = dv->modifers[0];
    uint32_t *m = &dv->modifers[1];
    int i;
// Find modifiers
    for (i = 0; i < len; i++)
    {
        if (m[i] == e->code)
        {
            uint32_t mask = 1 << i;
            if (e->value)
                keyb_modbits |= mask;
            else
                keyb_modbits &= ~mask;
            break;
        }
    }


// Filter action codes
    for (i = 0; i < dv->num_keys; i++)
    {
        struct keypairs *kp = &dv->keylist[i];
        if ((kp->keycode == e->code) && (kp->mod_bits == keyb_modbits))
        {
            if (e->value)
            {
                send_keypair(&dv->keylist[i], keyb_parent);
            }
            return 1;
        }
    }
    return 0;
}

void set_kbd_domain(struct domain *d)
{
    struct divert_info_t *dv = d->divert_info;

    if (dv && (dv->key_domain))
    {
        keyb_parent = d;
        keyb_modbits = 0;
        set_keyb_dest(dv->key_domain);

        keyb_waits_for_click = 1;

    }
    else
    {
        set_keyb_dest(d);
        keyb_modbits = 0;
        keyb_parent = NULL;
        if (dv && (dv->mouse_domain) && (dv->mouse_domain!=d))
            keyb_waits_for_click = 1;
        
    }
}

void sync_mouse_domain(struct domain *d)
{
    if ((mouse_parent == d) || (mouse_dest == d))
        input_set_mouse(d);

    // if mouse pressed, and click was within current frame, repeat click.
    dup_mouse_clicks(d);
}

void sync_kbd_domain(struct domain *d)
{
    if ((keyb_parent == d) || (get_keyb_dest() == d))
        input_set_keyb(d);

}

void set_mouse_domain(struct domain *d)
{
    struct divert_info_t *dv = d->divert_info;

    if (dv && (dv->mouse_domain))
    {
        mouse_dest = dv->mouse_domain;
        mouse_parent = d;
        keyb_waits_for_click = 1;
    }
    else
    {
        mouse_dest = d;
        mouse_parent = NULL;
    }
    // Just te be sure, check key focus anyway
    input_set_focus_change();
}



void input_set_mouse(struct domain *d)
{
    if (keyb_waits_for_click)
    {
        /* We mouse-switched and never clicked in the last VM. */
        if (get_keyb_dest() == d)
            /* We are just back where the keyboard is */
            keyb_waits_for_click = 0;
    }
    else
        keyb_waits_for_click = 1;
    do_grab(1);
    set_mouse_domain(d);
    info("mouse input now directed to domid %d", mouse_dest ? mouse_dest->domid : -1);
}

void input_set_keyb(struct domain *d)
{
        if (get_keyb_dest() != NULL)
        {
            /* Reset the keyboard of the previous domain before changing the
             * keyboard and mouse destination. */
            input_keyboard_reset(0);

            /* Turn numlock off before switching to another domain. Note that this
             * needs to be done after sending the Ctrl key up event, in the case of
             * switching between domains using Ctrl+slot number. This is because
             * pressing numlock does not have any effect when the Ctrl key is
             * pressed. */
            if (!input_get_numlock_restore_on_switch())
                turn_numlock_off();
        }
    keyb_waits_for_click = 0;

    do_grab(1);
    set_kbd_domain(d);
    info("keyboard input now directed to domid %d", get_keyb_dest() ? get_keyb_dest()->domid : -1);
}

void input_set(struct domain *d)
{
    struct timeval now;

        if (get_keyb_dest() != NULL)
        {
            /* Reset the keyboard of the previous domain before changing the
             * keyboard and mouse destination. */
            input_keyboard_reset(1);

            /* Turn numlock off before switching to another domain. Note that this
             * needs to be done after sending the Ctrl key up event, in the case of
             * switching between domains using Ctrl+slot number. This is because
             * pressing numlock does not have any effect when the Ctrl key is
             * pressed. */
            if (!input_get_numlock_restore_on_switch())
                turn_numlock_off();
        }
    keyb_waits_for_click = 0;

    do_grab(1);

    set_mouse_domain(d);
    set_kbd_domain(d);

    if (mouse_dest == get_keyb_dest())
        info("all input now directed to domid:%d abs_enabled:%d", mouse_dest ? mouse_dest->domid : -1,
             mouse_dest->abs_enabled);
    else
    {
        info("keyboard input now directed to domid %d", get_keyb_dest() ? get_keyb_dest()->domid : -1);
        info("mouse input now directed to domid %d", mouse_dest ? mouse_dest->domid : -1);
    }
    gettimeofday(&now, NULL);
    d->last_input_event = now;

}

/* Give the keyboard to domain d. */
void input_give_keyboard(struct domain *d)
{
    if (d == get_keyb_dest())
        return;

    /* When switching between seamless apps shared by different VMs, sometimes
     * the keyboard take command from the second VM is received before the
     * keyboard release command from the first VM. In this case, the keyboard
     * of the first VM will not be reset, so reset it here. */
    if (get_keyb_dest() != mouse_dest)
        input_keyboard_reset(0);

    if (!input_get_numlock_restore_on_switch())
        turn_numlock_off();

    set_keyb_dest(d);
    keyb_parent = NULL;
    keyb_modbits = 0;

    if (get_keyb_dest() != NULL)
    {
        input_led_code(get_keyb_dest()->keyboard_led_code, get_keyb_dest()->domid);
    }

    info("keyboard now directed to domid %d (mouse is on %d)", d ? d->domid : -1, mouse_dest ? mouse_dest->domid : -1);
}

/* Return the keyboard from domain d. */
void input_return_keyboard(struct domain *d)
{
    if (d != get_keyb_dest())
        return;

    if (get_keyb_dest() != mouse_dest)
        input_keyboard_reset(0);

    if (!input_get_numlock_restore_on_switch())
        turn_numlock_off();
    
    set_keyb_dest(mouse_dest);    
    keyb_parent = NULL;
    keyb_modbits = 0;

    if (get_keyb_dest() != NULL)
    {
        input_led_code(get_keyb_dest()->keyboard_led_code, get_keyb_dest()->domid);
    }

    info("keyboard now returned to domid %d (mouse is on %d)",
         get_keyb_dest() ? get_keyb_dest()->domid : -1,
         mouse_dest ? mouse_dest->domid : -1);
}

/* For domain d, give the keyboard to domain new_keyb_dest. */
void input_give_keyboard_from_domain(struct domain *d, struct domain *new_keyb_dest)
{
    if ((d == NULL) || (new_keyb_dest == NULL) || (d == new_keyb_dest))
        return;

    /* If domain d is in focus then give the keyboard to new_keyb_dest */
    if ((mouse_dest != NULL) && (mouse_dest == d))
    {
        input_give_keyboard(new_keyb_dest);
        return;
    }

    /* Domain d is not in focus, store the new keyboard domain for d. */
    d->prev_keyb_domain_ptr = new_keyb_dest;
    d->prev_keyb_domid = new_keyb_dest->domid;
    info("For domain %d, setting keyboard domain to %d\n", d->domid, d->prev_keyb_domid);
}

/* For domain d, release the keyboard from domain prev_keyb_dest
 * and return it to d. */
void input_return_keyboard_to_domain(struct domain *d, struct domain *prev_keyb_dest)
{
    if ((d == NULL) || (prev_keyb_dest == NULL) || (d == prev_keyb_dest))
        return;

    /* If domain d is in focus then release the keyboard from prev_keyb_dest */
    if ((mouse_dest != NULL) && (mouse_dest == d))
    {
        input_return_keyboard(prev_keyb_dest);
        return;
    }

    /* Domain d is not in focus, reset the previous keyboard domain for d, but only
     * if the previous keyboard domain was prev_keyb_dest. */
    if ((d->prev_keyb_domain_ptr == prev_keyb_dest) && (d->prev_keyb_domid == prev_keyb_dest->domid))
    {
        d->prev_keyb_domain_ptr = NULL;
        d->prev_keyb_domid = -1;
        info("For domain %d, release keyboard from domain %d\n", d->domid, prev_keyb_dest->domid);
    }
}

void turn_numlock_off(void)
{
    struct input_event e;

    if (get_keyb_dest() == NULL)
        return;

    if (((get_keyb_dest()->keyboard_led_code) & LED_CODE_NUMLOCK) == LED_CODE_NUMLOCK)
    {
        get_keyb_dest()->keyboard_led_code = ((get_keyb_dest()->keyboard_led_code) & (~LED_CODE_NUMLOCK));

        e.type = EV_KEY;
        e.code = KEY_NUMLOCK;
        e.value = 1;
        input_inject(&e, INPUTSLOT_DEFAULT, HID_TYPE_KEYBOARD);

        e.value = 0;
        input_inject(&e, INPUTSLOT_DEFAULT, HID_TYPE_KEYBOARD);

        input_led_code(get_keyb_dest()->keyboard_led_code, get_keyb_dest()->domid);
    }
}

int key_status_get(int key)
{
    if (key < KEY_STATUS_SIZE)
        return input_dev.key_status[key];

    return -1;
}

void save_prev_keyb_domain(struct domain *d)
{
    if ((get_keyb_dest() == NULL) || (mouse_dest == NULL) || (d == NULL))
        return;

    if (get_keyb_dest() != mouse_dest)
    {
        d->prev_keyb_domain_ptr = get_keyb_dest();
        d->prev_keyb_domid = get_keyb_dest()->domid;
        info("For domain %d, saving previous keyboard domain as %d\n", d->domid, get_keyb_dest()->domid);
    }
    else
    {
        d->prev_keyb_domain_ptr = NULL;
        d->prev_keyb_domid = -1;
    }
}

void restore_prev_keyb_domain(struct domain *d)
{
    if (d == NULL)
        return;

    if ((d->prev_keyb_domain_ptr != NULL) && (d->prev_keyb_domid != -1) &&
        (d->prev_keyb_domain_ptr->domid == d->prev_keyb_domid))
    {
        info("For domain %d, restore keyboard to domain %d\n", d->domid, d->prev_keyb_domain_ptr->domid);
        input_give_keyboard(d->prev_keyb_domain_ptr);
    }
}

static inline int event_is_key(struct input_event *e)
{
    return e->type == EV_KEY && e->code < BTN_MOUSE;
}

static inline int event_is_keyboard(int slot, struct input_event *e)
{   
    static int last_was_key=1;
    if (slot>=0 && input_dev.types[slot] != HID_TYPE_KEYBOARD)
    {
        last_was_key=0;
        return false;
    }
    if ((e->type == EV_KEY && e->code < BTN_MOUSE) || (e->type == EV_MSC && e->code == MSC_SCAN))
    {
        last_was_key=1;
        return true;
    }

    if (e->type==EV_SYN)
        return last_was_key;

    last_was_key=0;
    return false;
} 


static inline int event_is_key_down(struct input_event *e)
{
    if (!event_is_key(e))
        return 0;
    /*value 0 is up, 1 is down, 2 is autorepeat */
    switch (e->value)
    {
    case VALUE_KEY_DOWN:       /*Key down */
        return 1;
    case VALUE_KEY_UP:         /*Key up */
    case VALUE_KEY_REPEAT:     /*Auto repeat */
    default:
        return 0;
    }
}


static inline int event_is_key_up(struct input_event *e)
{
    if (!event_is_key(e))
        return 0;
    /*value 0 is up, 1 is down, 2 is autorepeat */
    switch (e->value)
    {
    case VALUE_KEY_UP:         /*Key up */
        return 1;
    case VALUE_KEY_DOWN:       /*Key down */
    case VALUE_KEY_REPEAT:     /*Auto repeat */
    default:
        return 0;
    }
}


static int code_is_meta_key(int key)
{

    static const int meta_keys[] = {
        KEY_LEFTCTRL,
        KEY_LEFTALT,
        KEY_LEFTMETA,
        KEY_RIGHTCTRL,
        KEY_RIGHTALT,
        KEY_RIGHTMETA,
        KEY_SYSRQ,
        -1,
    };

    const int *keys = meta_keys;
    int i = 0;

    while (*keys != -1)
    {
        if (key == *keys || (key_status_get(*keys) == 1))
            return 1;
        keys++;
    }
    return 0;
}

void wiggle_ctrl_key(struct domain *d)
{
    struct input_event e;

    memset(&e, 0, sizeof(e));
    if (!d)
        return;
    if ((mouse_dest == NULL) || (d == mouse_dest))
        return;


    gettimeofday(&e.time, NULL);

    e.type = EV_KEY;
    e.code = KEY_LEFTCTRL;
    e.value = 1;
    input_send(d, INPUTSLOT_DEFAULT, &e);

    e.type = EV_KEY;
    e.code = KEY_LEFTCTRL;
    e.value = 0;
    input_send(d, INPUTSLOT_DEFAULT, &e);

    info("wiggle_ctrl_key domid:%d\n", d->domid);
}

int input_inject_seamless_keyboard(struct input_event *e)
{
    static const int meta_keys[][4] = {
        {KEY_RIGHTALT, KEY_TAB, -1, -1},
        {KEY_LEFTALT, KEY_TAB, -1, -1},
        {KEY_LEFTCTRL, KEY_ESC, -1, -1},
        {KEY_RIGHTCTRL, KEY_ESC, -1, -1},
        {-1, -1, -1, -1}
    };
    int i, j;

    /* Special case */
    if ((key_status_get(KEY_LEFTMETA) == 1) || (key_status_get(KEY_RIGHTMETA) == 1))
        return 0;

    /* Do not send mouse events to the application sharing VM. */
    if (e->type == EV_REL || e->type == EV_ABS || (e->type == EV_KEY && e->code >= BTN_MOUSE && e->code <= BTN_GEAR_UP))
        return 0;

    if (e->type == EV_KEY)
    {
        for (i = 0; meta_keys[i][0] != -1; i++)
        {
            for (j = 0; j < 4 && meta_keys[i][j] != -1; j++)
            {
                if (meta_keys[i][j + 1] == -1)
                {
                    if (e->code == meta_keys[i][j])
                        return 0;
                    else
                        break;
                }

                if (!key_status_get(meta_keys[i][j]))
                    break;
            }
        }
    }

    return 1;
}

int input_inject_seamless_mouse(struct input_event *e)
{
    static const int meta_keys[][4] = {
        {KEY_LEFTCTRL, KEY_LEFTALT, KEY_DELETE, -1},
        {KEY_RIGHTCTRL, KEY_LEFTALT, KEY_DELETE, -1},
        {KEY_LEFTCTRL, KEY_RIGHTALT, KEY_DELETE, -1},
        {KEY_RIGHTCTRL, KEY_RIGHTALT, KEY_DELETE, -1},
        {-1, -1, -1, -1}
    };
    int i, j;

    if (e->type == EV_KEY)
    {
        for (i = 0; meta_keys[i][0] != -1; i++)
        {
            for (j = 0; j < 4 && meta_keys[i][j] != -1; j++)
            {
                if (meta_keys[i][j + 1] == -1)
                {
                    if (e->code == meta_keys[i][j])
                        return 0;
                    else
                        break;
                }

                if (!key_status_get(meta_keys[i][j]))
                    break;
            }
        }
    }

    if (code_is_meta_key(e->code))
        return 1;

    /* Send mouse events and synchronization events to the application viewing VM. */
    if (e->type == EV_REL ||
        e->type == EV_ABS ||
        (e->type == EV_KEY && e->code >= BTN_MOUSE && e->code <= BTN_GEAR_UP) ||
        (e->type == EV_SYN && e->code == SYN_REPORT))
        return 1;

    return 0;
}

void input_domain_set_mouse_pos(struct domain *d, int x, int y)
{
    struct input_event e;

    if (!d)
        return;
    info("%s: domid:%d x:%d y:%d", __func__, d->domid, x, y);
    e.type = EV_ABS;
    e.code = ABS_X;
    e.value = x;
    input_send(d, INPUTSLOT_DEFAULT, &e);
    e.code = ABS_Y;
    e.value = y;
    input_send(d, INPUTSLOT_DEFAULT, &e);
    e.type = EV_SYN;
    e.code = SYN_REPORT;
    input_send(d, INPUTSLOT_DEFAULT, &e);
}

void input_domain_set_mouse(struct domain *d)
{
    input_domain_set_mouse_pos(d, (int) mouse_x, (int) mouse_y);
}

void input_set_mouse_pos(int x, int y)
{
    mouse_x = x;
    mouse_y = y;
}

static void compute_mouse_speed(void)
{
    const double default_speed = 1.5;
    const double increment = 0.25;
    double mult_threshold_1 = (double) MOUSE_DIV_THRESHOLD_1 / (double) MAX_MOUSE_ABS_X;
    double mult_threshold_2 = (double) MOUSE_DIV_THRESHOLD_2 / (double) MAX_MOUSE_ABS_Y;
    int config_speed = input_get_mouse_speed();

    mouse_speed = default_speed - ((DEFAULT_CONFIG_MOUSE_SPEED - config_speed) * increment);

    mouse_speed_threshold_1 = mult_threshold_1 * mouse_speed;
    mouse_speed_threshold_2 = mult_threshold_2 * mouse_speed;
}

int input_get_mouse_speed(void)
{
    char buf[16];

    int ret = db_read(buf, sizeof(buf), "/mouse/speed");
    if ((ret == 0) || (strlen(buf) == 0))
        sprintf(buf, "%d", DEFAULT_CONFIG_MOUSE_SPEED);

    return strtol(buf, NULL, 10);
}

void input_set_mouse_speed(int speed)
{
    char buf[16];

    if (speed < MIN_CONFIG_MOUSE_SPEED)
        speed = MIN_CONFIG_MOUSE_SPEED;
    else if (speed > MAX_CONFIG_MOUSE_SPEED)
        speed = MAX_CONFIG_MOUSE_SPEED;

    sprintf(buf, "%d", speed);
    db_write("/mouse/speed", buf);
    compute_mouse_speed();
}

int input_get_numlock_restore_on_switch(void)
{
    char buf[16];

    /* Restore numlock state on switch by default. */
    int ret = db_read(buf, sizeof(buf), "/keyboard/numlock-restore-on-switch");
    if ((ret == 0) || (strlen(buf) == 0))
        return 1;

    return (strcmp("true", buf) == 0);
}

void input_set_numlock_restore_on_switch(int restore)
{
    db_write("/keyboard/numlock-restore-on-switch", restore ? "true" : "false");
}

static void force_range(double *x, int b, int e)
{
    if (*x < b)
        *x = b;
    if (*x >= e)
        *x = e;
}

/* Checks if the domain can handle absolute events. */
int input_domain_supports_abs(struct domain *d)
{
    /* UIVM always supports abs. */
    return ((d != NULL) && (d->abs_enabled || (d->slot == 0)));
}

void input_domain_handle_resolution_change(struct domain *d, int xres, int yres)
{
    /* Check if domain has the mouse focus. */
    if (d == NULL || d != mouse_dest)
        return;

    /* Check if domain can handle absolute events. */
    if (!input_domain_supports_abs(d))
        return;

    /* Only update mouse position if desktop dimensions of domain have changed. */
    if ((d->desktop_xres == 0 && d->desktop_yres == 0) ||
        (d->desktop_xres == xres && d->desktop_yres == yres))
        return;

    if (d->desktop_xres != xres)
    {
        mouse_x = mouse_x * (double) d->desktop_xres / (double) xres;
        force_range(&mouse_x, MIN_MOUSE_ABS_X, MAX_MOUSE_ABS_X);
    }

    if (d->desktop_yres != yres)
    {
        mouse_y = mouse_y * (double) d->desktop_yres / (double) yres;
        force_range(&mouse_y, MIN_MOUSE_ABS_Y, MAX_MOUSE_ABS_Y);
    }

    input_domain_set_mouse(d);
}

static double get_mouse_speed_mult(struct input_event *e, enum input_device_type input_type)
{
    double speed_mult = mouse_speed;

    if (input_type != HID_TYPE_MOUSE || e->type != EV_REL || mouse_dest == NULL)
        return 1;

    if (mouse_dest->desktop_xres == 0 && mouse_dest->desktop_yres == 0)
        return speed_mult;

    if (abs(e->value) <= 2)
        speed_mult = mouse_speed_threshold_1;
    else if (abs(e->value) <= 5)
        speed_mult = mouse_speed_threshold_2;

    return speed_mult;
}

static void input_track_mouse_position(struct input_event *e, enum input_device_type input_type)
{
    /* Don't track mouse position if mouse_dest is NULL. */
    if (mouse_dest) {
        int xres = mouse_dest->desktop_xres ? mouse_dest->desktop_xres : DEFAULT_RESOLUTION_X;
        int yres = mouse_dest->desktop_yres ? mouse_dest->desktop_yres : DEFAULT_RESOLUTION_Y;

        if (e->type == EV_REL) {
            switch (e->code) {
            case REL_X:
                if (input_type == HID_TYPE_TOUCHPAD)
                    mouse_x += e->value * (MAX_MOUSE_ABS_X / xres);
                else
                    mouse_x += e->value * mouse_dest->rel_x_mult * get_mouse_speed_mult(e, input_type);

                force_range(&mouse_x, MIN_MOUSE_ABS_X, MAX_MOUSE_ABS_X);
                break;
            case REL_Y:
                if (input_type == HID_TYPE_TOUCHPAD)
                    mouse_y += e->value * (MAX_MOUSE_ABS_Y / yres);
                else
                    mouse_y += e->value * mouse_dest->rel_y_mult * get_mouse_speed_mult(e, input_type);

                force_range(&mouse_y, MIN_MOUSE_ABS_Y, MAX_MOUSE_ABS_Y);
                break;
            }
        }
    }

    if ((e->type==EV_KEY) && ( e->code >= BTN_MISC )  && (e->code < BTN_JOYSTICK))
    {

        int key = e->code - BTN_MISC;
        int word = key / (8 * sizeof(uint32_t));

        if ((word < 0) || (word >= BUTTONS_SIZE))
            return;

        if (e->value)
            {
            buttons[word] |= 1<<key%32;
            if (buttons[word])
                mouse_button |= 1<<word;
            }
        else
            {
            buttons[word] &= ~(1<<key%32);
            if (!buttons[word])
                mouse_button &= ~(1<<word);
            }
    }

}

static void dup_mouse_clicks(struct domain* d)
{
    struct input_event e={.value=1, .type=EV_KEY};
    int i; 
    int pressed= num_mouse_pressed;
    if (!mouse_parent || !mouse_parent->divert_info)
        return;

    struct divert_info_t* dv = mouse_parent->divert_info;
    for (i=0; (i<pressed) && (num_mouse_pressed<MAX_NUM_PRESSED); i++)
    {
        if (mouse_pressed[i].domain!=d)
        {
            // Check code not alreayd duped
            int ii;
            bool ok=true;

            for (ii=0; ii<pressed; ii++)
            {
                if ((mouse_pressed[ii].domain==d) && (mouse_pressed[ii].code == mouse_pressed[i].code))
                {
                    ok=false;
                    break;
                }
            }
            struct mousebutton * mb = &mouse_pressed[i];
            if ( ok && (uint32_t) mb->x > dv->sframe_x1 && (uint32_t) mb->x < dv->sframe_x2 && (uint32_t) mb->y > dv->sframe_y1  && (uint32_t) mb->y < dv->sframe_y1)
                {
                memcpy(&mouse_pressed[num_mouse_pressed], &mouse_pressed[i], sizeof(struct mousebutton));
                mouse_pressed[num_mouse_pressed].domain=d;
                e.code= mouse_pressed[i].code;
                input_send(d, mouse_pressed[i].slot, &e);
                num_mouse_pressed++;
                }
        }
    }

}


static int check_mouse_keys(struct domain* d, int slot, struct input_event* e)
{
if (( e->code < BTN_MISC )  || (e->code >= BTN_JOYSTICK))
    return 0;

if (e->value)
{ // log event, and where it came from

        if (num_mouse_pressed<MAX_NUM_PRESSED)
        {
            struct mousebutton * mb = &mouse_pressed[num_mouse_pressed];
            mb->code=e->code;
            mb->slot=slot;
            mb->domain=d;
            mb->x= (int) mouse_x;
            mb->y= (int) mouse_y;
            num_mouse_pressed++;
        }
    }
    else // send keyup to domain that it lived in.
    {
        int i;
        for (i=0; i<num_mouse_pressed; i++)
        {
            if ((mouse_pressed[i].code==e->code) && (mouse_pressed[i].slot==slot))
            {
                int r=0;
                if (mouse_pressed[i].domain!=d)
                {            
                    input_send(mouse_pressed[i].domain, slot, e);                        
                    r=1;
                }
                num_mouse_pressed--;
                for (;i<num_mouse_pressed; i++)
                    memcpy(&mouse_pressed[i], &mouse_pressed[i+1], sizeof(struct mousebutton));                
                return  r;
            }
        }
    }
    return 0;
}

static void input_convert_rel_to_abs(struct input_event *e)
{
    if (e->type == EV_REL)
    {
        switch (e->code)
        {
        case REL_X:
            e->type = EV_ABS;
            e->code = ABS_X;
            e->value = (int) mouse_x;
            break;
        case REL_Y:
            e->type = EV_ABS;
            e->code = ABS_Y;
            e->value = (int) mouse_y;
            break;
        }
    }
}

static int input_cant_print_screen(struct input_event *e)
{
    return domain_cant_print_screen(mouse_dest) || domain_cant_print_screen(get_keyb_dest());
}

int mouse_outside_frame()
{
    struct divert_info_t *dv = mouse_parent->divert_info;

    if (((uint32_t) mouse_x < dv->sframe_x1) || ((uint32_t) mouse_x > dv->sframe_x2) ||
        ((uint32_t) mouse_y < dv->sframe_y1) || ((uint32_t) mouse_y > dv->sframe_y2))
        return true;
    return false;
}

void scale_pointer_event(struct input_event *e)
{
    struct domain *d = mouse_parent;

    int o, d1, d2;


    if (e->type != EV_ABS)
        return;
    struct divert_info_t *dv = d->divert_info;
    if ((e->code == ABS_X) || (e->code == ABS_MT_POSITION_X))
    {

        o = e->value - dv->sframe_x1;
        d1 = dv->sframe_x2 - dv->sframe_x1;
        d2 = dv->dframe_x2 - dv->dframe_x1;
        e->value = o * d2 / d1 + dv->dframe_x1;
    }
    else if ((e->code == ABS_Y) || (e->code == ABS_MT_POSITION_Y))
    {
        o = e->value - dv->sframe_y1;
        d1 = dv->sframe_y2 - dv->sframe_y1;
        d2 = dv->dframe_y2 - dv->dframe_y1;
        e->value = o * d2 / d1 + dv->dframe_y1;
    }

}

void input_inject(struct input_event *e, int slot, enum input_device_type input_type)
{
    static struct domain* last_d = NULL; 
    static int button_holding=0;

    struct domain *d = mouse_dest;
    int val = 0;

    if (!e)
        return;

    if ((e->type == EV_KEY || e->type == EV_REL || e->type == EV_ABS) && timeout_expired())
    {
        wiggle_ctrl_key(domain_pvm());

        domain_wake_from_s3(mouse_dest);
        domain_wake_from_s3(get_keyb_dest());
        domain_wake_from_s3(domain_pvm());
        timeout_start();
    }


    if (event_is_keyboard(slot,e))
    {
        if (filter_keys(e))
            return;

        if (e->type == EV_KEY && e->code == KEY_SYSRQ)
            if (input_cant_print_screen(e))
            {
                info("mouse_dest:%d keyb_dest:%d, print screen disallow, ignoring event",
                     mouse_dest->domid, get_keyb_dest()->domid);
                return;
            }

        if (e->type == EV_MSC && e->code == MSC_SCAN)
        {
            switch (e->value)
            {
            case 0xAD:
                /* This is the main button of the Samsung Slate 7, */
                /* Handling (switch to uivm) and returning (don't send to the VM, as it does tabs...) */
                switcher_switch(domain_uivm(), 0, 0);
                return;
            case 0xA7:
                ;
                /* This is the rotate button of the Samsung Slate 7, */
                /* We'll want to do something here in the future (maybe
                   enter secure input mode) */
            }
        }
        else if (e->type == EV_KEY && e->code == KEY_SWITCHVIDEOMODE && !(get_keyb_dest()->is_pv_domain))
        {
             /* Send a fake Windows + P keystroke to emulate that key */
             struct input_event win_keystroke = {
             .type = EV_KEY,
             .code = KEY_LEFTMETA,
             .value = e->value
             };

             struct input_event p_keystroke = {
             .type = EV_KEY,
             .code = KEY_P,
             .value = e->value
             };

             input_send(get_keyb_dest(), slot, &win_keystroke);
             input_send(get_keyb_dest(), slot, &p_keystroke);
        }
        if (get_keyb_dest())
            d = get_keyb_dest();
        if (!d)
            return;
    }
    else                        /* not keyboard - mouse? */
    {
        input_track_mouse_position(e, input_type);

        if (!d)
            return;

        if (e->type == EV_REL)
        {
            if (e->code == REL_X)
                val = e->value;
            if (input_domain_supports_abs(mouse_dest))
                input_convert_rel_to_abs(e);
        }

        int click = ((e->type == EV_KEY) && (keyb_waits_for_click &&
                (e->code == BTN_LEFT || e->code == BTN_RIGHT || e->code == BTN_MIDDLE)));
        enum {eUnknown, eChild, eParent} parentchild = eUnknown;
        struct divert_info_t *dv=NULL;

        


        if (mouse_parent)
            {
            dv = mouse_parent->divert_info;

            if ((e->type==EV_SYN) && (e->code==SYN_REPORT) && button_holding==2)
                button_holding=0;
                
            if ((dv->focusmode & CLICKHOLDFOCUS) && (mouse_button))
                button_holding=1;
            else if (button_holding)
                button_holding=2;
            

            if (button_holding)
                {
                if (last_d==mouse_parent)
                    parentchild = eParent;
                else if (last_d==d)
                    parentchild = eChild;
                }
            

            if (parentchild==eUnknown)
                parentchild = mouse_outside_frame()?eParent:eChild;
            
            if (parentchild==eParent)
                d = mouse_parent;
            else
                {
                if (click && (dv->focusmode & KEYFOLLOWMOUSE))
                    {
                    dv->key_domain = d;
                    sync_kbd_domain(mouse_parent);
                    }
                }   
           
            if (e->type==EV_KEY)
                if (check_mouse_keys(d, slot, e))
                    return;

            last_d=d;
            }
        else if (click)
            {
            /* If your keyboard is in another castle, get it back on click */
            input_set_keyb(mouse_dest);
            }

        if (parentchild==eChild)
        {
            if (dv->focusmode & CLONEEVENTS)
                {
                input_send(mouse_parent, slot, e);
                }
            scale_pointer_event(e);

        }
        else
        {

            /* To mouse-switch, last event must be a large-enough X movement */
            /* Unless specified in the db, resistance is 10 */
            if (resistance == -1)
            {
                char buf[16] = { 0 };
                if (db_exists("/switcher/resistance"))
                {
                    db_read(buf, 16, "/switcher/resistance");
                    resistance = strtol(buf, NULL, 0);
                }
                else
                    resistance = 10;
            }
            if (abs(val) > resistance)
                switcher_switch_on_mouse(e, (int) mouse_x, (int) mouse_y);
        }

    } /* end key/mouse */

    /* old seamless */
    if (get_keyb_dest() && mouse_dest && (get_keyb_dest() != mouse_dest) && !keyb_waits_for_click)
    {
        if (input_inject_seamless_keyboard(e))
            input_send(get_keyb_dest(), slot, e);
        if (input_inject_seamless_mouse(e))
            input_send(mouse_dest, slot, e);
        return;
    }
    input_send(d, slot, e);
}

static void input_led(int onoff, int led)
{
    struct input_event ev;
    int i = 0;

    memset(&ev, 0, sizeof(ev));
    for (i = 0; i < N_DEVS; i++)
        if (input_dev.types[i] == HID_TYPE_KEYBOARD)
        {
            ev.type = EV_LED;
            ev.code = led;
            ev.value = onoff;
            write(input_dev.fds[i], &ev, sizeof(struct input_event));
        }
}

#if 0
void input_leds(int onoff)
{
    input_led(onoff, LED_NUML);
    input_led(onoff, LED_CAPSL);
    input_led(onoff, LED_SCROLLL);
}
#endif

void input_led_code(int led_code, int domid)
{
    int onoff = 0;

    if ((get_keyb_dest() != NULL) && (get_keyb_dest()->domid == domid))
    {
        onoff = ((led_code & LED_CODE_SCROLLLOCK) == LED_CODE_SCROLLLOCK) ? 1 : 0;
        input_led(onoff, LED_SCROLLL);

        onoff = ((led_code & LED_CODE_NUMLOCK) == LED_CODE_NUMLOCK) ? 1 : 0;
        input_led(onoff, LED_NUML);

        onoff = ((led_code & LED_CODE_CAPSLOCK) == LED_CODE_CAPSLOCK) ? 1 : 0;
        input_led(onoff, LED_CAPSL);
    }
}

static void check_for_force_binding(void)
{
    struct input_binding *binding = input_dev.bindings;
    int n = input_dev.nbinding;

    while (n--)
    {

        if (binding->down)
            binding->force_ticks++;
        else
            binding->force_ticks = 0;

        if (binding->force_ticks > 6)
        {
            if (binding->force_cb)
                binding->force_cb(binding->opaque);
            binding->force_ticks = 0;
            binding->down = 0;
        }


        binding++;
    }
}

/* run input event against binding queue
 * return 1 if binding was matched and event should be ignored
 * return 0 if binding was not matched and event should be vm injected most likely
 */
static int input_exec_bindings(struct input_event *e)
{
    struct input_binding *binding = input_dev.bindings, *matched = NULL;
    int n = input_dev.nbinding;
    int reset = 0;

    //info("exec_bindings type=%d code=%d value=%d", e->type, e->code, e->value);
    if (event_is_key_up(e))
    {
        while (n--)
        {
            binding->down = 0;
            binding->matched = 0;
            binding++;
        }
    }

    if (!event_is_key_down(e))
    {
        /* dont care a thing about this event uninteresting key down event */
        return 0;
    }

    while (n--)
    {
        /*If the key matches the next in the list, bump the state machine */
        /*along, otherwise back of the line for you boyo */
        if (binding->binding[binding->matched] == e->code)
            binding->matched++;
        else
            binding->matched = 0;

        /*We got to the end of the list and everything matched */
        if (binding->binding[binding->matched] == -1)
        {
            binding->matched = 0;       /*back of the line for next time */
            matched = binding;
        }
        binding++;
    }

    if (matched)
    {
        /*And call the callback */
        matched->down = 1;
        if (matched->cb)
            reset = matched->cb(matched->opaque);

        /*We matched, eat the last keystroke and send key up for all the others */
        if (reset)
            input_keyboard_reset(1);
    }
    return matched != NULL;
}

static void input_exec_bindings_or_inject(struct input_event *e, int slot, enum input_device_type input_type)
{
    if (!input_exec_bindings(e))
    {
        if (input_dev.types[slot] == HID_TYPE_KEYBOARD)
            slot = INPUTSLOT_DEFAULT;
        /* sequence has not matched, inject the event into vm */
        input_inject(e, slot, input_type);
    }
}

static void input_keys_status(struct input_event *e)
{
    if (e->type == EV_KEY && e->code < KEY_STATUS_SIZE)
    {
        //info("input_keys_status %d %d\n", e->code, e->value);
        input_dev.key_status[e->code] = ! !e->value;
    }
}

/* broadcast currently focused field - so the UI can sync */
static void auth_notify_current_field()
{
    notify_com_citrix_xenclient_input_focus_auth_field(xcbus_conn, SERVICE, OBJ_PATH, input_dev.auth.current_field);
}

/* broadcast username field - so the UI can sync */
static void auth_notify_username()
{
    notify_com_citrix_xenclient_input_sync_auth_username(xcbus_conn, SERVICE, OBJ_PATH, input_dev.auth.username);
}

static char **auth_get_field_ptr(struct auth_input_state *s, int fieldcode)
{
    switch (fieldcode)
    {
    case AUTH_FIELD_USERNAME:
        return &input_dev.auth.username;
    case AUTH_FIELD_PASSWORD:
        return &input_dev.auth.passwd;
    case AUTH_FIELD_PASSWORD_CONFIRM:
        return &input_dev.auth.passwd_confirm;
    case AUTH_FIELD_PASSWORD_PREVIOUS:
        return &input_dev.auth.passwd_previous;
    }
    return NULL;
}

/* pointer to char data collected for given field */
static char *auth_get_field(struct auth_input_state *s, int fieldcode)
{
    char **pp = auth_get_field_ptr(s, fieldcode);
    return pp ? *pp : NULL;
}

/* return current tabbing order, along with tab order table size, depending on context flags */
static int auth_current_tab_order(int **order)
{
    static int tab_order_case1[] = { AUTH_FIELD_PASSWORD, AUTH_FIELD_USERNAME };
    static int tab_order_case2[] = { AUTH_FIELD_PASSWORD, AUTH_FIELD_PASSWORD_CONFIRM };
    static int tab_order_case3[] = { AUTH_FIELD_PASSWORD };
    static int tab_order_change_local_pass[] =
        { AUTH_FIELD_PASSWORD_PREVIOUS, AUTH_FIELD_PASSWORD, AUTH_FIELD_PASSWORD_CONFIRM };

    struct auth_context_t *ctx = auth_get_context();
    if (!ctx)
    {
        warning("where's my auth context?");
        return 0;
    }

    if (ctx->flags & AUTH_FLAG_CHANGE_LOCAL_PW)
    {
        *order = tab_order_change_local_pass;
        return 3;
    }

    if (ctx->flags & AUTH_FLAG_CONFIRM_PW)
    {
        *order = tab_order_case2;
        return 2;
    }
    else if (ctx->flags & AUTH_FLAG_REMOTE_USER)
    {
        *order = tab_order_case1;
        return 2;
    }
    else
    {
        /* username field is disabled when its local auth, do not allow to tab to it */
        *order = tab_order_case3;
        return 1;
    }
}

/* snap tab index to length of current tab order table */
static int auth_wrap_tabindex(int tabindex)
{
    int *order, len;
    len = auth_current_tab_order(&order);
    if (!len)
    {
        return tabindex;
    }
    while (tabindex < 0)
        tabindex += len;
    while (tabindex >= len)
        tabindex -= len;
    return tabindex;
}

/* figure out tabindex of a field */
static int auth_get_field_tabindex(int fieldcode)
{
    int tab_len;
    int *tab_order;

    tab_len = auth_current_tab_order(&tab_order);
    if (!tab_len)
        return 0;

    int i;
    for (i = 0; i < tab_len; ++i)
    {
        if (tab_order[i] == fieldcode)
        {
            return i;
        }
    }
    warning("where's my tab index?");
    return 0;
}

/* move the field focus forward or backward */
static void auth_tab(int direction)
{
    int *order;
    int tablen = auth_current_tab_order(&order);
    if (!tablen)
    {
        return;
    }
    int tabindex = auth_get_field_tabindex(input_dev.auth.current_field);
    tabindex += direction;
    tabindex = auth_wrap_tabindex(tabindex);
    input_dev.auth.current_field = order[tabindex];

    /* put a cursor after the end */
    char *f = auth_get_field(&input_dev.auth, input_dev.auth.current_field);
    int len = f ? strlen(f) : 0;
    input_dev.auth.cursor = len;
    /* send notification to UI to do the actual on screen focus */
    auth_notify_current_field();
}

/* which field gets focus when auth window opens */
static void auth_figure_initial_focus()
{
    struct auth_context_t *ctx = auth_get_context();
    if (!ctx)
    {
        return;
    }
    if (ctx->flags & AUTH_FLAG_CHANGE_LOCAL_PW)
    {
        input_dev.auth.current_field = AUTH_FIELD_PASSWORD_PREVIOUS;
    }
    else if (ctx->flags & AUTH_FLAG_SET_LOCAL_PW)
    {
        input_dev.auth.current_field = AUTH_FIELD_PASSWORD;
    }
    else if (ctx->flags & AUTH_FLAG_CONFIRM_PW || ctx->flags & AUTH_FLAG_SET_ROOT_PW)
    {
        /* no username field, focus is password */
        input_dev.auth.current_field = AUTH_FIELD_PASSWORD;
    }
    else if (!ctx->user || strlen(ctx->user) == 0)
    {
        /* no username in context, focus is username */
        input_dev.auth.current_field = AUTH_FIELD_USERNAME;
    }
    else
    {
        /* have default username, focus is password */
        input_dev.auth.current_field = AUTH_FIELD_PASSWORD;
    }
}

static void auth_backspace(int fieldcode)
{
    char *field = auth_get_field(&input_dev.auth, fieldcode);
    if (!field)
        return;
    int len = field ? strlen(field) : 0;

    if (len > 0)
    {
        field[len - 1] = 0;
        if (input_dev.auth.cursor > 0)
        {
            --input_dev.auth.cursor;
        }
    }
}

static void auth_collect_key(int fieldcode, int ascii)
{
    char **field_p = NULL, *field = NULL;
    int len = 0, j;
    int cu = input_dev.auth.cursor;
    /* only collect password if so requested */
    if (!input_dev.collect_password)
        return;
    field_p = auth_get_field_ptr(&input_dev.auth, fieldcode);
    if (!field_p)
        return;
    if (*field_p)
    {
        len = strlen(*field_p);
    }
    if (ascii >= 0)
    {
        *field_p = realloc(*field_p, len + 2);
        field = *field_p;
        if (cu > len)
            cu = len;
        for (j = len; j > cu; j--)
            field[j] = field[j - 1];
        field[len + 1] = 0;
        field[j] = ascii;

        ++input_dev.auth.cursor;
    }
    else
    {
        ascii = 256 + ascii;

        *field_p = realloc(*field_p, len + 3);
        field = *field_p;

        if (cu > len)
        {
            cu = len;
        }

        for (j = len + 1; j > cu + 1; j--)
        {
            field[j] = field[j - 2];
        }

        field[len + 2] = 0;

        field[cu] = 0xC0 | ((ascii >> 6) & 0x1F);
        field[cu + 1] = 0x80 | (ascii & 0x3F);

        input_dev.auth.cursor += 2;
    }
}

static void input_clear_passwd(void)
{
    if (input_dev.auth.passwd)
    {
        memset(input_dev.auth.passwd, 0, strlen(input_dev.auth.passwd));
        free(input_dev.auth.passwd);
        input_dev.auth.passwd = NULL;
    }
    if (input_dev.auth.passwd_confirm)
    {
        memset(input_dev.auth.passwd_confirm, 0, strlen(input_dev.auth.passwd_confirm));
        free(input_dev.auth.passwd_confirm);
        input_dev.auth.passwd_confirm = NULL;
    }
    if (input_dev.auth.passwd_previous)
    {
        memset(input_dev.auth.passwd_previous, 0, strlen(input_dev.auth.passwd_previous));
        free(input_dev.auth.passwd_previous);
        input_dev.auth.passwd_previous = NULL;
    }
    if (input_dev.auth.username)
    {
        free(input_dev.auth.username);
        input_dev.auth.username = NULL;
    }

    struct auth_context_t *ctx = auth_get_context();
    if (!ctx)
    {
        return;
    }
    /* the username field is initialised from context */
    if (ctx->flags & AUTH_FLAG_USER_HASH)
    {
        /* the context only contains a hash, recover username from hash */
        char username[NAME_LEN] = { 0 };
        user_get_name(ctx->user, username);
        input_dev.auth.username = strdup(username);
    }
    else
    {
        /* the context contains username directly */
        input_dev.auth.username = strdup(ctx->user);
    }
    input_dev.auth.passwd = strdup("");
    input_dev.auth.passwd_confirm = strdup("");
    input_dev.auth.passwd_previous = strdup("");
    input_dev.auth.cursor = 0;

    /* focus some field */
    auth_figure_initial_focus();

    /* notify ui */
    auth_notify_username();
    auth_notify_current_field();
}

static int input_check_secure_mode()
{
    struct domain *auth_vm = domain_uivm();
    int focused = switcher_get_focus();

    if (!(input_dev.secure_mode))
        return 0;

    /* don't steal keys when auth vm is not on screen */
    if (auth_vm && auth_vm->domid != focused)
    {
        return 0;
    }

    return 1;
}

/* return 1 means that we drop the event */
static int input_secure_mode(struct input_event *e, enum input_device_type input_type)
{
    char c;
    struct auth_context_t *ctx = auth_get_context();
    int locked;

    if (!input_check_secure_mode())
        return 0;    

    if (e->type == EV_REL || e->type == EV_ABS || (e->type == EV_SYN && e->code == SYN_REPORT))
        return 0;

    /* in the new scheme we TRUST people, we let them switch off the authentication screen
     * and use other sequence keys. No longer we're obsessively paranoid.
     * Note to self: we are still paranoid if we are using lock screen. We don't want peeps
     * to poke around vms when lock screen is up */
    locked = ctx && ctx->flags & AUTH_FLAG_LOCK;
    if (!locked && input_exec_bindings(e))
    {
        /* sequence was matched, return 1 to not inject it */
        return 1;
    }

    if (e->type == EV_KEY && e->code < BTN_MOUSE)
    {
#if 0
        info("ev keycode:%d value:%d LEFTCTRL:%d\n", e->code, e->value, key_status_get(KEY_LEFTCTRL));
#endif

        if (e->value == VALUE_KEY_DOWN)
        {
            switch (e->code)
            {
            case KEY_ESC:
                /* signal auth cancel and clear password */
                if (ctx->flags & AUTH_FLAG_CANNOT_CANCEL)
                    auth_status(AUTH_USER_CANCEL_DONT_HIDE, 0);
                else
                    auth_status(AUTH_USER_CANCEL, 0);
                input_clear_passwd();
                break;
            case KEY_TAB:
                /* reverse or forward tab depending on shift key */
                if ((key_status_get(KEY_RIGHTSHIFT) == 1) || (key_status_get(KEY_LEFTSHIFT) == 1))
                {
                    auth_tab(-1);
                }
                else
                {
                    auth_tab(1);
                }
                break;
            case KEY_BACKSPACE:
                /* chop last character off */
                auth_backspace(input_dev.auth.current_field);
                break;
            case KEY_ENTER:
                /* and try to auth */
                auth_end(input_dev.auth.username,
                         input_dev.auth.passwd ? input_dev.auth.passwd : "",
                         input_dev.auth.passwd_confirm, input_dev.auth.passwd_previous);
                input_clear_passwd();
                break;
            default:
                if ((c = keycode2ascii(e->code)))
                {
                    auth_collect_key(input_dev.auth.current_field, c);
                }
                break;
            }
        }
        /* eat tabs because tabbing is done inside input daemon */
        if (e->code == KEY_TAB)
            return 1;
        if (keycode2ascii(e->code) || e->code == KEY_BACKSPACE)
        {
            /* username keys get injected, others get replaced by blanks */
            if (keycode2ascii(e->code) && input_dev.auth.current_field != AUTH_FIELD_USERNAME)
            {
                e->code = KEY_SPACE;
            }
            /* convert key down to key down & up straight away */
            if (e->value == VALUE_KEY_DOWN)
            {
                /* plop key down */
                e->value = VALUE_KEY_DOWN;
                input_inject(e, INPUTSLOT_DEFAULT, input_type);
                /* plop key up in rapid succession */
                e->value = VALUE_KEY_UP;
                input_inject(e, INPUTSLOT_DEFAULT, input_type);
            }
        }
        else
        {
            /* Propagate modifier keys to UIVM directly (if username field) */
            if (input_dev.auth.current_field == AUTH_FIELD_USERNAME)
            {
                if (e->code == KEY_LEFTSHIFT || e->code == KEY_RIGHTSHIFT ||
                    e->code == KEY_LEFTCTRL || e->code == KEY_RIGHTCTRL ||
                    e->code == KEY_LEFTALT || e->code == KEY_RIGHTALT)
                {
                    input_inject(e, INPUTSLOT_DEFAULT, input_type);
                }
            }
        }
    }
    /* eat event */
    return 1;
}

static void handle_thinkpad_acpi_event(struct input_event *ev)
{
    /* We do not send thinkpad acpi events to the guests, we could if required. */
    if (ev->type == EV_KEY && ev->code == KEY_FN_F8 && ev->value == VALUE_KEY_DOWN)
    {
        toggle_touchpad_status();
    }
}

static void force_timer(void *opaque)
{
    static struct event timer_event;
    static int init = 1;
    struct timeval tv;

    if (init)
    {
        evtimer_set(&timer_event, wrapper_force_timer, (void *) 1);
        init = 0;
    }

    check_for_force_binding();

    timerclear(&tv);
    tv.tv_sec = 1;
    evtimer_add(&timer_event, &tv);
}


static void input_lock_timer(void *opaque)
{
    static struct event lock_event;
    static int timeout = 0;
    struct timeval tv = { 0, 0 };

    if (lock_event.ev_flags == 0)
        event_set(&lock_event, -1, EV_TIMEOUT | EV_PERSIST, wrapper_input_lock_timer, (void *) 1);

    if ((int) opaque == 0)
        timeout = 0;
    if (platform_lock_timeout == -1)
    {
        char tmp[20] = "";
        db_read(tmp, 20, PLATFORM_LOCK_TIMEOUT);
        platform_lock_timeout = 0;
        if (*tmp)
            platform_lock_timeout = strtol(tmp, NULL, 10);
    }
    if (platform_lock_timeout > 0 && timeout >= platform_lock_timeout)
    {
        timeout = 0;
        switcher_lock(0);
    }
    if ((int) opaque == 1)
        timeout += 5;
    tv.tv_sec += 5;
    evtimer_add(&lock_event, &tv);
}

static void wrapper_input_lock_timer(int fd, short event, void *opaque)
{
    input_lock_timer(opaque);
}

void check_and_inject_event(struct input_event *e, int slot, enum input_device_type input_type)
{
    int inject = 1;

    if (e == NULL)
        return;

    inject = !input_secure_mode(e, input_type);

    if (inject)
        input_inject(e, slot, input_type);
}


static void input_read(void *opaque)
{
    int slot = (int) opaque;
    struct input_event event[64];
    unsigned int i = 0;
    int read_sz = 0;
    int inject = 1;
    int32_t focused;

    memset(event, 0, sizeof(event));
    if ((read_sz = read(input_dev.fds[slot], event, sizeof(event))) <= 0)
    {
        info("read failed from event%d (returned %d) Dropping device.", slot, read_sz);
        event_del(&input_dev.device_events[slot]);
        close(input_dev.fds[slot]);
        input_dev.fds[slot] = -1;
        broadcast_removed_dev(slot);
        return;
    }

    input_lock_timer((void *) 0);

    for (i = 0; i < read_sz / (sizeof(struct input_event)); i++)
    {
        input_keys_status(&event[i]);

        enum input_device_type input_type = input_dev.types[slot];

        inject = !input_secure_mode(&event[i], input_type);

        if (input_type == HID_TYPE_TOUCHPAD)
            handle_touchpad_event(&event[i], slot);
        else if (input_type == HID_TYPE_TABLET)
            handle_usb_tablet_event(&event[i], slot);
        else if (inject)
        {
            if (input_type == HID_TYPE_THINKPAD_ACPI)
                handle_thinkpad_acpi_event(&event[i]);
            else
                input_exec_bindings_or_inject(&event[i], slot, input_type);
        }
    }

    gettimeofday(&global_last_input_event, NULL);

    /*update last_input_event time */
    focused = switcher_get_focus();
    if (focused > 0)
    {
        struct domain *current = domain_with_domid(focused);
        if (current)
        {
            current->last_input_event = global_last_input_event;
        }
    }
}

/* revert screen to authentication vm */
static void revert_to_auth(void *opaque)
{
    int32_t focused;
    struct domain *auth_vm = domain_uivm();
    if (!auth_vm)
    {
        return;
    }
    focused = switcher_get_focus();
    if (focused > 0 && focused != auth_vm->domid)
    {
        /* dont try to switch if its pvm and tools are not loaded; otherwise massive
         * lag incurs waiting for blanker response which never comes */
        int try_switch = 1;
        struct domain *current = domain_with_domid(focused);
        if (current && current->is_pvm)
        {
            char *addons = xenstore_dom_read(current->domid, "attr/PVAddons/Installed");
            if (!addons || addons[0] != '1')
            {
                try_switch = 0;
            }
            free(addons);
        }
        if (try_switch)
        {
            switcher_switch(auth_vm, 0, 0);
        }
    }
    /* keep checking if secure mode is on */
    if (input_dev.secure_mode)
    {
        struct timeval tv = { 0, 0 };
        tv.tv_sec += 1;
        evtimer_add(&revert_to_auth_event, &tv);
    }
}

static void wrapper_revert_to_auth(int fd, short event, void *opaque)
{
    revert_to_auth(opaque);
}

int input_secure(int onoff)
{
    if (onoff != input_dev.secure_mode)
    {
        input_dev.secure_mode = onoff;
        input_set_focus_change();
        input_clear_passwd();

        /* notify dbus */
        emit_secure_mode(onoff);

        if (onoff)
        {
            /* when entering secure mode, turn off password collection for starters. it will get
             * turned on later, when UI focuses the GUI control */
            input_dev.collect_password = 0;
            /* bring password collection window */
            int began = auth_begin();
            if (!began)
            {
                info("authentication has not began despite attempt");
                return 0;
            }
            else
            {
                struct timeval tv = { 0, 0 };
                /* in the new scheme, we have no bussiness chaining people to the authentication screen,
                 * we trust them completely. We need not to force authentication screen!
                 * However, we still force authentication screen when the lock screen is used */
                struct auth_context_t *ctx = auth_get_context();
                if (ctx && ctx->flags & AUTH_FLAG_LOCK)
                {
                    event_set(&revert_to_auth_event, -1, EV_TIMEOUT | EV_PERSIST, wrapper_revert_to_auth, NULL);

                    if (revert_to_auth_event.ev_flags == 0)
                    {
                        return 0;
                    }
                    tv.tv_sec += 1;
                    evtimer_add(&revert_to_auth_event, &tv);
                }
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }
    return 0;
}

void input_collect_password()
{
    if (input_dev.secure_mode)
    {
        input_dev.collect_password = 1;
        /* send notif about currently focused field so UI can sync */
        auth_notify_current_field();
        auth_notify_username();
    }
}


static int find_keyboard_device(int fd, unsigned int bustype, const char* name)
{
    unsigned long eventtypes[NBITS(EV_MAX)];
    unsigned long keybits[NBITS(KEY_CAPSLOCK)];
    const int minkey=40;  /* This number is a bit arbitary, but minimal keyboard would need at least this many keys  */

    int i;
    int count=0;
    int ret;

    if (strcasestr(name, "keyboard"))
        return HID_TYPE_KEYBOARD;
    /* Not every keyboard has keyboard in its name */
    if (bustype != BUS_USB)
        return 0;

    if ((ret = ioctl(fd, EVIOCGBIT(0, sizeof(eventtypes)), eventtypes)) < 0)
        return ret;
    
    if (!TEST_BIT(EV_KEY, eventtypes))
        return 0;  /* This proves nothing - but should be checked*/

    if ((ret = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits)) < 0)
        return ret;

    for (i=1; i<KEY_CAPSLOCK; i++)
    {
        if (TEST_BIT(i, keybits))
            count++;
    }
    if (count>=minkey) /* Should allow keyboards with a few keys missing, but should have majority */
        return HID_TYPE_KEYBOARD;

    return 0;

}

static int find_pointer_device_type(int fd, unsigned int bustype, uint8_t * subtype)
{
    unsigned long eventtypes[NBITS(EV_MAX)];
    unsigned long abslimits[NBITS(ABS_MAX)];
    unsigned long keybits[NBITS(KEY_OK)];
    *subtype = SUBTYPE_NONE;
    int ret = 0;

    memset(eventtypes, 0, sizeof(eventtypes));
    memset(abslimits, 0, sizeof(abslimits));
    memset(keybits, 0, sizeof(abslimits));

    if ((ret = ioctl(fd, EVIOCGBIT(0, sizeof(eventtypes)), eventtypes)) < 0)
        return ret;

    /* Check if the device supports absolute events. */
    if (!TEST_BIT(EV_ABS, eventtypes))
        return HID_TYPE_MOUSE;

    /* USB mice also support absolute events, so we need to perform
       additional checks to determine if the device is a touchpad, or tablet. */
    if (!TEST_BIT(EV_SYN, eventtypes) || !TEST_BIT(EV_KEY, eventtypes))
        return HID_TYPE_MOUSE;

    if ((ret = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abslimits)), abslimits)) < 0)
        return ret;

    if ((ret = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits)) < 0)
        return ret;

    if (TEST_BIT(ABS_X, abslimits) && TEST_BIT(ABS_Y, abslimits))
    {
/* Touchpads can be USB!  BTN_TOOL_FINGER can be used to tell its a touchpad.  However, by correctly 
identifying that the wacom bamboo touchpad is a touchpad, it stops it working.  So for now its better to work wrongly */

/*        if (((bustype == BUS_I8042) || (TEST_BIT(BTN_TOOL_FINGER, keybits))) && */
        if (((bustype == BUS_I8042)) && (TEST_BIT(ABS_PRESSURE, abslimits)))
            return HID_TYPE_TOUCHPAD;

        if ((bustype == BUS_USB) || (bustype == BUS_RS232) || bustype == BUS_I2C)
        {
            if (TEST_BIT(BTN_TOOL_PEN, keybits))
            {
                *subtype = SUBTYPE_STYLUS;
            }
            if (TEST_BIT(BTN_TOOL_FINGER, keybits))
            {
                *subtype = SUBTYPE_MONOTOUCH;
            }
        }

        return HID_TYPE_TABLET;
    }

    return HID_TYPE_MOUSE;
}

static int device_is_thinkpad_acpi(unsigned int bustype, const char *name)
{
    return ((bustype == BUS_HOST) && (strcasestr(name, "thinkpad extra buttons")));
}

static int device_is_acpi_video(unsigned int bustype, const char *name)
{
    return ((bustype == BUS_HOST) && (strcasestr(name, "video bus")));
}

static int device_is_displaylink_touchpanel(unsigned int bustype, const char *name)
{
    return ((bustype == BUS_USB) && (strcasestr(name, "e2i technology, inc. usb touchpanel")));
}

static int device_is_lid_switch(unsigned int bustype, const char *name)
{
    return ((bustype == BUS_HOST) && (strcasestr(name, "lid switch")));
}

static bool bus_is_supported(unsigned int bus)
{
    unsigned int buses[] = {
        BUS_I8042, BUS_USB, BUS_RS232, BUS_I2C,
    };
    unsigned int i;

    for (i = 0; i < ARRAY_LEN(buses); ++i)
        if (buses[i] == bus)
            return true;
    return false;
}

static bool devices_blacklist(unsigned int bustype, const char *name)
{
    if (device_is_acpi_video(bustype, name))
        return true;
    if (device_is_displaylink_touchpanel(bustype, name))
        return true;

    return false;
}

static int consider_device(int slot)
{
    int fd = input_dev.fds[slot];
    int ret = 0;

    char name[128] = { 0 };
    struct input_id id;

    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) == -1)
        return -1;
    name[sizeof(name) - 1] = 0;

    if (ioctl(fd, EVIOCGID, &id) == -1)
        return -1;

    if (!bus_is_supported(id.bustype))
        return -1;
    if (devices_blacklist(id.bustype, name))
        return -1;

    if (!ioctl(fd, EVIOCGRAB, current_grab))
        input_dev.grab[slot] = current_grab;

    if (device_is_thinkpad_acpi(id.bustype, name))
    {
        info("event%d added thinkpad acpi device on fd %d (%s)", slot, fd, name);
        input_dev.types[slot] = HID_TYPE_THINKPAD_ACPI;

        event_set(&input_dev.device_events[slot], fd, EV_READ | EV_PERSIST, wrapper_input_read, (void *) slot);
        event_add(&input_dev.device_events[slot], NULL);
        return 0;
    }

    if (device_is_lid_switch(id.bustype, name))
    {
        info("event%d added lid switch on fd %d (%s)", slot, fd, name);
        lid_create_switch_event(fd);
        input_dev.fds[slot] = -1;
        return 0;
    }

    ret = find_keyboard_device(fd, id.bustype, name);
    if (ret == HID_TYPE_KEYBOARD)
    {
        info("event%d added keyboard on fd %d (%s)", slot, fd, name);
        input_dev.types[slot] = HID_TYPE_KEYBOARD;

        event_set(&input_dev.device_events[slot], fd, EV_READ | EV_PERSIST, wrapper_input_read, (void *) slot);
        event_add(&input_dev.device_events[slot], NULL);

        /* Set the keyboard LEDs. */
        if (get_keyb_dest() != NULL)
        {
            input_led_code(get_keyb_dest()->keyboard_led_code, get_keyb_dest()->domid);
        }

        return 0;
    }
    else if (ret<0)
    {
        info("Unable to determine whether device %s is a keyboard", name);
        return -1;
    }

    uint8_t subtype;
    /* Check if device is a touchpad, usb tablet or mouse. */
    ret = find_pointer_device_type(fd, id.bustype, &subtype);
    if (ret < 0)
    {
        info("Unable to determine whether device %s is a touchpad, usb tablet or mouse", name);
        return -1;
    }
    else if (ret == HID_TYPE_TOUCHPAD)
    {
        if (init_touchpad(fd) < 0)
            return -1;

        info("event%d added touchpad on fd %d (%s)", slot, fd, name);
        input_dev.types[slot] = HID_TYPE_TOUCHPAD;
    }
    else if (ret == HID_TYPE_TABLET)
    {
        if (init_usb_tablet(fd, slot, subtype) < 0)
            return -1;

        info("event%d added usb tablet on fd %d (%s)", slot, fd, name);
        input_dev.types[slot] = HID_TYPE_TABLET;
    }
    else                        /* HID_TYPE_MOUSE */
    {
        info("event%d added mouse on fd %d (%s)", slot, fd, name);
        input_dev.types[slot] = HID_TYPE_MOUSE;
    }

    broadcast_config(slot);
    event_set(&input_dev.device_events[slot], fd, EV_READ | EV_PERSIST, wrapper_input_read, (void *) slot);

    event_add(&input_dev.device_events[slot], NULL);

    return 0;
}

static void wrapper_input_read(int fd, short event, void *opaque)
{
    input_read(opaque);
}

static void wrapper_force_timer(int fd, short event, void *opaque)
{
    force_timer(opaque);
}

static void input_scan(void *unused)
{
    char name[128];
    int i;
    struct timeval tv;


    for (i = 0; i < N_DEVS; i++)
    {
        /* Do we already have this device open and working */
        if (input_dev.fds[i] >= 0)
            continue;

        sprintf(name, EVENT_FILES "%d", i);
        /* Check to see if we can open it */
        input_dev.fds[i] = open(name, O_RDWR);
        if (input_dev.fds[i] < 0)
            continue;

        /* Switch to NBIO */
        {
            long arg = 0;
            arg = fcntl(input_dev.fds[i], F_GETFL, arg);
            arg |= O_NONBLOCK;
            fcntl(input_dev.fds[i], F_SETFL, arg);
        }

        if (consider_device(i))
        {
            close(input_dev.fds[i]);
            input_dev.fds[i] = -1;
            continue;
        }
    }

#if 0
    if (!scan_timer)
        scan_timer = set_new_timer(input_scan, NULL);
    gettimeofday(&tv, NULL);
    tv.tv_sec += 5;
    set_timer(scan_timer, &tv);
#endif
}

void input_add_binding(const int tab[], input_binding_cb_t cb, input_binding_cb_t force_cb, void *opaque)
{
    int len = 0;
    int i = input_dev.nbinding++;
    input_dev.bindings = realloc(input_dev.bindings, input_dev.nbinding * sizeof(struct input_binding));


    while (tab[len] != -1)
        len++;
    len++;
    len *= sizeof(int);

    input_dev.bindings[i].binding = malloc(len);
    memcpy(input_dev.bindings[i].binding, tab, len);

    input_dev.bindings[i].cb = cb;
    input_dev.bindings[i].force_cb = force_cb;
    input_dev.bindings[i].opaque = opaque;
    input_dev.bindings[i].matched = 0;
    input_dev.bindings[i].down = 0;
    input_dev.bindings[i].force_ticks = 0;
}

void udev_mon_handler(void *opaque)
{
    struct udev_device *dev;
    const char *devnode;
    dev = udev_monitor_receive_device(udev_mon);
    /* TODO: Make input_scan only consider the added/removed device,
       for now just trigger a scan for all add events */
    devnode = udev_device_get_devnode(dev);
    if (devnode && !strcmp("add", udev_device_get_action(dev)))
    {
        fprintf(stderr, "Add device event received for %s\n", devnode);
        if (strstr(devnode, "event"))
        {
            fprintf(stderr, "Initiating scan for %s\n", devnode);
            input_scan(NULL);
        }
    }
}

static void wrapper_udev_mon_handler(int fd, short event, void *opaque)
{
    udev_mon_handler(opaque);
}

void onstart_sendconfig(struct domain *d)
{
    /* Send all devices */
    int slot;

    send_config_reset(d, (uint8_t) 0xFF);

    for (slot = 0; slot < N_DEVS; slot++)
        if ((input_dev.types[slot] > HID_TYPE_KEYBOARD) && (input_dev.types[slot] < HID_TYPE_THINKPAD_ACPI))
        {
            send_config(d, slot);
        }
    send_slot(d);
}

void sock_plugin_sendconfig(struct sock_plugin* plug)
{
    int slot;
    send_plugin_dev_event(plug, DEV_RESET, (uint8_t)0xFF);

    for (slot = 0; slot < N_DEVS; slot++)
        if ((input_dev.types[slot] > HID_TYPE_KEYBOARD) && (input_dev.types[slot] < HID_TYPE_THINKPAD_ACPI))
        {
            send_plugin_dev_event(plug, DEV_CONF, slot);
        }
}

void input_release(bool in_fork)
{
    int i;

    (void) in_fork;

    /* FIXME: For the moment only close file descriptors */

    for (i = 0; i < N_DEVS; i++)
    {
        if (input_dev.fds[i] != -1)
        {
            close(input_dev.fds[i]);
            input_dev.fds[i] = -1;
        }
    }

    if (udev)
    {
        event_del(&udev_monitor_event);
        if (udev_mon)
            udev_monitor_unref(udev_mon);
        udev_mon = NULL;
        udev_unref(udev);
    }
    udev = NULL;
}

int input_init(void)
{
    int i;
    int fd;

    gettimeofday(&global_last_input_event, NULL);

    if (add_domainstart_callback(onstart_sendconfig))
    {
        info("Error: Can't create domainstart_callback!\n");
        return -1;
    }

    memset(&input_dev, 0, sizeof(input_dev));
    for (i = 0; i < N_DEVS; i++)
        input_dev.fds[i] = -1;

    memset(buttons, 0, sizeof(buttons));

    /* Set the mouse to the centre of the screen. */
    mouse_x = (MAX_MOUSE_ABS_X - MIN_MOUSE_ABS_X) / 2;
    mouse_y = (MAX_MOUSE_ABS_Y - MIN_MOUSE_ABS_Y) / 2;
    compute_mouse_speed();

    input_scan(NULL);

    /* Create the udev object */
    udev = udev_new();
    if (!udev)
    {
        info("Can't create udev\n");
        return -1;
    }

    udev_mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "input", NULL);
    udev_monitor_enable_receiving(udev_mon);

    /* Get the file descriptor (fd) for the monitor.
       This fd will get passed to select() */
    fd = udev_monitor_get_fd(udev_mon);

    event_set(&udev_monitor_event, fd, EV_READ | EV_PERSIST, wrapper_udev_mon_handler, NULL);
    event_add(&udev_monitor_event, NULL);

    force_timer(NULL);

    return 1;
}
