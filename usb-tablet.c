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

#define NEW_X_RESOLUTION 32767
#define NEW_Y_RESOLUTION 32767

#define btnleft_newlypressed 2

struct tablet_scaling_params
{
    double x_mult;
    double y_mult;
    int x_offs;
    int y_offs;
    uint8_t subtype;
    uint8_t btnleft;
    int tool;
};

static struct tablet_scaling_params tscale[N_DEVS];
static int ignore_events[N_DEVS];

#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif

#define ABS_MT_TRACKING_ID      0x39
#define MAXSLOTS 3

static int handle_gestures (struct input_event *e)
{
    static int silence = 0;
    static int goSilent = 0;

    static int trackID[MAXSLOTS] = { -1, -1, -1 };
    static int x[MAXSLOTS] = { -1, -1, -1 };
    static int y[MAXSLOTS] = { -1, -1, -1 };

    static int slot = -1;

    static int distance = 0;
    static int track_change = 0;
    int new_slot = -1;

    if ((e->type == EV_SYN) && (e->code == SYN_DROPPED))
    { // Drop state
        distance = 0;
        track_change = 0;
        return 1;
    }


    if ((e->type == EV_ABS) && (e->code == ABS_MT_SLOT))
        new_slot = e->value;


    if (slot < MAXSLOTS)
    {
        if (e->type == EV_ABS)
        {
            switch (e->code)
            {
            case ABS_MT_POSITION_X:
                distance += abs (x[slot] - e->value);
                x[slot] = e->value;
                break;
            case ABS_MT_POSITION_Y:
                distance += abs (y[slot] - e->value);
                y[slot] = e->value;
                break;
            case ABS_MT_TRACKING_ID:
                if ((trackID[slot] < 0) != (e->value < 0))
                {
                    track_change = 1;
                    trackID[slot] = e->value;
                }
                break;
            }
        }



        if (((new_slot != -1) || ((e->type == EV_SYN) && (e->code == SYN_REPORT)))
            && ((slot < MAXSLOTS) && (slot >= 0)))
        {
            if (track_change || (distance > 1))
            {
                goSilent =
                    gesture_handler (slot, x[slot], y[slot], (track_change) ? ((trackID[slot] >= 0) ? 1 : 0) : -1);
            }
            /* reset vars for  next set */
            distance = 0;
            track_change = 0;
        }  /* end new slot or syn_report */
    }
    if (new_slot != -1)
        slot = new_slot;

    if ((e->type == EV_SYN) && (e->code == SYN_REPORT))
        silence = goSilent;

    return (!silence);
} /* endproc */

void set_and_inject_event(int slot,  struct input_event* ev, int type, int code, int value)
{
    ev->type = type;
    ev->code = code;
    ev->value = value;

    check_and_inject_event(ev,slot, HID_TYPE_TABLET);
}

void handle_usb_tablet_event (struct input_event *ev, int slot)
{
    static int pen_inrange=0;

    struct input_event new_ev;
    new_ev.time = ev->time;
    new_ev.type = ev->type;
    new_ev.code = ev->code;
    new_ev.value = ev->value;

    struct tablet_scaling_params* t = &(tscale[slot]);

    if (new_ev.type == EV_ABS)
    {
	if (!pen_inrange || (t->subtype!=SUBTYPE_MONOTOUCH))
        {
        if ((new_ev.code == ABS_X) || (new_ev.code == ABS_MT_POSITION_X))
            new_ev.value = floor ( (new_ev.value - t->x_offs) * t->x_mult);
        else if ((new_ev.code == ABS_Y) || (new_ev.code == ABS_MT_POSITION_Y))
            new_ev.value = floor ( (new_ev.value - t->y_offs) * t->y_mult);
	}
	else
  	   return;
    }
    else if (new_ev.type == EV_KEY)
    {        
	/* Translate tablet codes into mouse codes, so it can be understood by drivers */
        switch (new_ev.code)
        {
        case BTN_TOOL_PEN:
            if (new_ev.value)
                t->tool = BTN_LEFT;
            pen_inrange=new_ev.value;
            break;
        case BTN_TOOL_FINGER:
            if (new_ev.value)
                t->tool = BTN_TOOL_FINGER;
            break;
        case BTN_TOOL_RUBBER:
            if (new_ev.value)
                t->tool = BTN_RIGHT;
            pen_inrange=new_ev.value;
            break;

        case BTN_TOUCH:
	    if (t->tool == BTN_TOOL_FINGER)
	    {
		if (new_ev.value)
			{	
			if ((t->btnleft==0) && (!pen_inrange))
				t->btnleft=btnleft_newlypressed;
            break;
			}
		else if (t->btnleft)
			{
			new_ev.code=BTN_LEFT;
			t->btnleft=0;
			}
		else
            break;
            }
	    else
	            new_ev.code = t->tool;
            break;
        case BTN_STYLUS:
            new_ev.code = BTN_MIDDLE;
            break;
        }
    }


    if (new_ev.type == EV_SYN && new_ev.code == SYN_REPORT)
    {
        if (t->btnleft==btnleft_newlypressed)
	{
	t->btnleft=1;
	check_and_inject_event (&new_ev, slot, HID_TYPE_TABLET);
        set_and_inject_event(slot, &new_ev, EV_KEY, BTN_LEFT,1);
	set_and_inject_event(slot, &new_ev, EV_SYN, SYN_REPORT,0);
	return;
	}
    }

    // Check for SYN_DROPPED.  Indicates buffer overrun, so no point forwarding any more of packet until next syn_report
    // Note that as any packets before the syn_dropped have already been transmitted, syn_dropped must still be transmitted
    // so that client knows to ignore them.

    if (new_ev.type == EV_SYN && new_ev.code == SYN_DROPPED)
    {
        ignore_events[slot] = 1;
        info ("Ignoring events from event%d until next packet begins", slot);
    }
    else if (ignore_events[slot]) 
    {
	if  (new_ev.type == EV_SYN && new_ev.code == SYN_REPORT)
	    {
            ignore_events[slot] = 0;
            info ("End of dropped packet from event%d", slot);
	    }
	else
	   return;
    }

    if ((new_ev.type == EV_KEY) && (new_ev.code != ev->code)) 
    { /* we want to send bot the mouse version and pen version */
        check_and_inject_event (ev, slot, HID_TYPE_TABLET);
    }

    if (handle_gestures (&new_ev))
        check_and_inject_event (&new_ev, slot, HID_TYPE_TABLET);
}

int init_usb_tablet (int fd, int slot, uint8_t subtype)
{
    struct input_absinfo absinfo_x;
    struct input_absinfo absinfo_y;

    int ret = 0;
    int i;
    int diff;


    struct input_id id;
    struct tablet_scaling_params* t = &(tscale[slot]);
    ignore_events[slot] = 0;
 
    t->subtype = subtype;
    t->btnleft=0;
    t->tool=0;

    if (ioctl (fd, EVIOCGID, &id) == -1)
        return -1;

    if ((id.vendor==0x56a) && (id.product==0xed) && (subtype==SUBTYPE_MONOTOUCH)) // device lies
	{
	   absinfo_x.minimum=380;
	   absinfo_x.maximum=3820;
           absinfo_y.minimum=290;
	   absinfo_y.maximum=3620;
	} else
	{
	    if ((ret = ioctl (fd, EVIOCGABS (ABS_X), &absinfo_x)) < 0)
	        return ret;

	    if ((ret = ioctl (fd, EVIOCGABS (ABS_Y), &absinfo_y)) < 0)
        	return ret;
	}

    t->x_offs = absinfo_x.minimum;
    diff = absinfo_x.maximum - absinfo_x.minimum;

    if (diff != 0)
        t->x_mult = ((double) NEW_X_RESOLUTION) / ((double) diff);
    else
        return -1;

    t->y_offs = absinfo_y.minimum;
    diff = absinfo_y.maximum - absinfo_y.minimum;

    if (diff != 0)
        t->y_mult = ((double) NEW_Y_RESOLUTION) / ((double) diff);
    else
        return -1;

    return 0;
}
