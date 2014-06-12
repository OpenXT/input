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

#define ABS_MT_PRESSURE         0x3a    /* Pressure on contact area */

#define MOVE_MULT  0.02
#define SCROLL_MULT 0.04
#define PRESSURE_UP_MULT  0.12
#define PRESSURE_DOWN_MULT  0.098
#define NUM_MOVE_HISTORY_REC 4
#define AUTOSCROLL_TIMEOUT_USEC 200000
#define TAP_DRAG_TIMEOUT_USEC 180000
#define TIMEVAL_MAX_USEC 1000000

#define HIST(a) (ts->move_hist[((ts->move_hist_index - (a) + NUM_MOVE_HISTORY_REC) % NUM_MOVE_HISTORY_REC)])

static void wrapper_send_autoscroll_event(int fd, short event , void *opaque);
static int gSlot = -2;

enum tap_state
{
    TS_START = 0,
    TS_TOUCH,
    TS_MOVE,
    TS_SINGLE_TAP_PENDING,
    TS_POSSIBLE_DRAG,
    TS_DRAG
};

enum edge_type
{
    NO_EDGE = 0,
    RIGHT_EDGE = 1,
    BOTTOM_EDGE = 2,
    TOP_EDGE = 4
};

enum scroll_type
{
    SCROLL_UP = 0,
    SCROLL_DOWN
};

struct touchpad_packet
{
    int x;                    /* X position of finger. */
    int y;                    /* X position of finger. */
    int pressure;             /* Finger pressure. */
    int button_mask;          /* Left or right button click. */
    struct timeval timestamp;
    int fingertouching;
};

struct move_history_rec
{
    int x;
    int y;
    struct timeval timestamp;
};

struct scroll_data
{
    int up;
    int down;
    int left;
    int right;
};

struct touchpad_state
{
    int last_x;               /* Last valid value of x - used when packets without an x value are recd. */
    int last_y;               /* Last valid value of y - used when packets without an y value are recd. */
    int last_fingertouching;
    enum tap_state tapstate;
    struct move_history_rec move_hist[NUM_MOVE_HISTORY_REC];
    int move_hist_index;
    int packet_count_move;
    struct scroll_data scrolldata;
    int vert_scroll_on;
    int scroll_y;
    int scroll_packet_count;
    double autoscroll_yspd;
    double autoscroll_y;
    double frac_x;
    double frac_y;

    int touchpad_off;        /* Is the touchpad enabled or disabled. */
};

struct touchpad_limits
{
    int minx;                 /* Minimum valid value of x. */
    int maxx;                 /* Maximum valid value of x. */
    int miny;                 /* Minimum valid value of y. */
    int maxy;                 /* Maximum valid value of y. */
    int min_pressure;         /* Minimum valid value of pressure. */
    int max_pressure;         /* Maximum valid value of pressure. */
    int right_edge;           /* Right edge begins here. */
    int bottom_edge;          /* Bottom edge begins here. */
    int top_edge;             /* Top edge begins here. */
    int tap_move;             /* Minimum distance to move, for a move event to be generated. */

    int no_x;                 /* Used when no x value is received in packet. */
    int no_y;                 /* Used when no y value is received in packet. */
    int no_pressure;          /* Used when no pressure value is received in packet. */

    int scroll_dist_vert;     /* Minimum vertical scroll distance. */
    double speed;

    int is_clickpad;

    /* Maximum x value for left button clicks. */
    int clickpad_left_button_maxx;

    /* True when the clickpad is pressed on the bottom edge. */
    int is_clickpad_pressed;
};

struct touchpad_config
{
    int tap_to_click_enabled;
    int scrolling_enabled;
    int speed;
};

static const int left_button_down = 0x01;
static const int left_button_up = 0x02;
static const int right_button_down = 0x04;
static const int right_button_up = 0x08;
static const int left_button_id = 1;

static struct touchpad_packet tpacket;
static struct touchpad_state  tstate;
static struct touchpad_limits tlimits;
static struct touchpad_config tconfig;
static const int default_diag = 5024;

static struct event autoscroll_event;
static enum scroll_type current_scroll_type;
static int set_timer_only = 1;
static int is_autoscroll_timer_set = 0;

static struct event left_click_event;

/* get configured setting - true/false */
int touchpad_get_tap_to_click_enabled(void)
{
    char buf[16];
    db_read(buf, sizeof(buf), "/touchpad/tap-to-click-enable");
    return strcmp("true", buf) == 0;
}

/* get configured setting - true/false */
int touchpad_get_scrolling_enabled(void)
{
    char buf[16];
    db_read(buf, sizeof(buf), "/touchpad/scrolling-enable");
    return strcmp("true", buf) == 0;
}

/* get configured setting - int value */
int touchpad_get_speed(void)
{
    char buf[16];
    db_read(buf, sizeof(buf), "/touchpad/speed");
    return strtol(buf, NULL, 10);
}

void touchpad_set_scrolling_enabled(int enabled)
{
    db_write("/touchpad/scrolling-enable", enabled ? "true" : "false");
    touchpad_reread_config();
}

void touchpad_set_tap_to_click_enabled(int enabled)
{
    db_write("/touchpad/tap-to-click-enable", enabled ? "true" : "false");
    touchpad_reread_config();
}

void touchpad_set_speed(int speed)
{
    char buf[16];
    sprintf(buf, "%d", speed);
    db_write("/touchpad/speed", buf);
    touchpad_reread_config();
}


static void reset_touchpad_packet(struct touchpad_packet *tp, struct touchpad_limits *tl)
{
    tp->x = tl->no_x;
    tp->y = tl->no_y;
    tp->pressure = tl->no_pressure;
    tp->button_mask = 0;
    tp->timestamp.tv_sec = 0;
    tp->timestamp.tv_usec = 0;
    tp->fingertouching = 0;
}

static enum edge_type detect_edge(int x, int y, int right_edge, int bottom_edge, int top_edge)
{
    enum edge_type etype = NO_EDGE;

    if (x > right_edge)
        etype |= RIGHT_EDGE;

    if (y > bottom_edge)
    {
        etype |= BOTTOM_EDGE;
    }
    else if (y < top_edge)
    {
        etype |= TOP_EDGE;
    }

    return etype;
}

static int detect_finger(int pressure, struct touchpad_limits *tl)
{
    static int fingertouching = 0;
    int range = tl->max_pressure - tl->min_pressure;

    if (pressure == tl->no_pressure)
        return fingertouching;

    if (!fingertouching)
    {
        if (pressure > (tl->min_pressure + (range * PRESSURE_UP_MULT)))
            fingertouching = 1;
    }
    else
    {
        if (pressure < (tl->min_pressure + (range * PRESSURE_DOWN_MULT)))
            fingertouching = 0;
    }

    return fingertouching;
}

static void populate_input_event(struct input_event *ev, int type, int code, int value)
{
    struct timeval now;

    gettimeofday(&now,NULL);
    ev->time = now;
    ev->type = type;
    ev->code = code;
    ev->value = value;
}

static void event_sync(void)
{
    struct input_event sync_ev;

    populate_input_event(&sync_ev, EV_SYN, SYN_REPORT, 0);

    check_and_inject_event(&sync_ev, gSlot, HID_TYPE_TOUCHPAD);
}

static void event_button_change(int button_id, int value)
{
    struct input_event ev;

    if ((button_id < left_button_id) || (button_id > (BTN_TASK - BTN_LEFT)))
        return;

    populate_input_event(&ev, EV_KEY, button_id + BTN_LEFT - 1, value);
    check_and_inject_event(&ev, gSlot, HID_TYPE_TOUCHPAD);
    
    /* Generate a sync event. */
    event_sync();
}

static void event_relative_move(int dist_x, int dist_y)
{
    struct input_event ev[2];

    if (dist_x != 0)
    {
        populate_input_event(&(ev[0]), EV_REL, REL_X, dist_x);
        check_and_inject_event(&(ev[0]), gSlot, HID_TYPE_TOUCHPAD);
    }

    if (dist_y != 0)
    {
        populate_input_event(&(ev[1]), EV_REL, REL_Y, dist_y);
        check_and_inject_event(&(ev[1]), gSlot, HID_TYPE_TOUCHPAD);
    }

    /* Generate a sync event. */
    if ((dist_x != 0) || (dist_y != 0))
        event_sync();
}

static void event_scroll(enum scroll_type scroll)
{
    struct input_event ev;
    int event_code = -1;
    int value = 0;

    switch (scroll)
    {
        case SCROLL_UP:
            event_code = REL_WHEEL;
            value = 1;
            break;

        case SCROLL_DOWN:
            event_code = REL_WHEEL;
            value = -1;
            break;

        default:
            break;
    }

    populate_input_event(&ev, EV_REL, event_code, value);
    check_and_inject_event(&ev, gSlot, HID_TYPE_TOUCHPAD);

    /* Generate a sync event. */
    event_sync();
}

/* Note: the maximum value of timeout_usec that this function can handle
   is 999,999, which is the maximum value of tv_usec in struct timeval. */
static void add_timeout_usec(struct timeval *tv,
                             uint32_t timeout_usec)
{
    uint32_t diff;

    diff = TIMEVAL_MAX_USEC - tv->tv_usec;
    if (diff <= timeout_usec)
    {
        tv->tv_usec = timeout_usec - diff;
        tv->tv_sec += 1;
    }
    else
    {
        tv->tv_usec += timeout_usec;
    }
}

static void delete_left_click_timer(void)
{
    if (event_initialized(&left_click_event))
    {
        evtimer_del(&left_click_event);
    }
}

static void send_left_click_event(void *opaque)
{
    struct touchpad_state *ts = (struct touchpad_state *) opaque;

    const int button_down = 1;
    const int button_up = 0;

    if (ts->tapstate == TS_SINGLE_TAP_PENDING)
    {
        event_button_change(left_button_id, button_down);
        event_button_change(left_button_id, button_up);

        ts->tapstate = TS_START;
    }

    delete_left_click_timer();
}

static void wrapper_send_left_click_event(int fd, short event, void *opaque)
{
    send_left_click_event(opaque);
}

static void set_left_click_timer(struct touchpad_state *ts,
                                 uint32_t timeout_usec)
{
    struct timeval tv = {0, 0};

    if (!event_initialized(&left_click_event))
    {
        evtimer_set(&left_click_event, wrapper_send_left_click_event, ts);
    }

    add_timeout_usec(&tv, timeout_usec);
    evtimer_add(&left_click_event, &tv);
}

static void handle_gestures(struct touchpad_state *ts,
                            struct touchpad_packet *tp,
                            struct touchpad_limits *tl,
                            struct touchpad_config *tc)
{
    static int touch_on_x = 0;
    static int touch_on_y = 0;
    static enum edge_type touch_on_edge = NO_EDGE;
    int button = left_button_id;
    int touch = 0;
    int release = 0;
    int move = 0;
    int dx = 0;
    int dy = 0;

    const int button_down = 1;
    const int button_up = 0;

    touch = (tp->fingertouching) && !(ts->last_fingertouching);
    release = !(tp->fingertouching) && (ts->last_fingertouching);

    if (tp->fingertouching)
    {
        if (touch) 
            move = 0;
        else if ( ((tp->x != tl->no_x) && (abs(tp->x - touch_on_x) >= tl->tap_move)) ||
                  ((tp->y != tl->no_y) && (abs(tp->y - touch_on_y) >= tl->tap_move)) )
            move = 1;
    }

    if (touch)
    {
        if (tp->x != tl->no_x)
            touch_on_x = tp->x;
        else 
            touch_on_x = ts->last_x;

        if (tp->y != tl->no_y)
            touch_on_y = tp->y;
        else 
            touch_on_y = ts->last_y;

        touch_on_edge = detect_edge(touch_on_x, touch_on_y,
                                    tl->right_edge, tl->bottom_edge,
                                    tl->top_edge);
    }

    switch (ts->tapstate)
    {
        case TS_START:
            if (touch)
                ts->tapstate = TS_TOUCH;

            ts->frac_x = 0;
            ts->frac_y = 0;

            break;

        case TS_TOUCH:
            if (move)
            {
                ts->tapstate = TS_MOVE;
            }
            else if(release)
            {
                ts->tapstate = TS_START;

                /* Generate left button click if configured.
                   For clickpads, do not generate tap clicks on the
                   bottom edge, otherwise a press-and-release button
                   click results in a double-click. */
                if ( tc->tap_to_click_enabled &&
                     (!(tl->is_clickpad && (touch_on_edge & BOTTOM_EDGE))) )
                {
                    /* Do not generate the left click immediately, otherwise a
                       double tap and drag gesture results in a double click.
                       The windows and ubuntu drivers seem to check the time
                       diff between 2 left button down events, rather than 2
                       left button up events, for a double click. So in case of
                       double tap and drag, we will not send the first tap. */
                    set_left_click_timer(ts, TAP_DRAG_TIMEOUT_USEC);
                    ts->tapstate = TS_SINGLE_TAP_PENDING;
                }
            }
            break;

        case TS_MOVE:
            if (release)
            {
                ts->tapstate = TS_START;
            }
            break;

        case TS_SINGLE_TAP_PENDING:
            if (touch)
            {
                delete_left_click_timer();

                if ( tc->tap_to_click_enabled &&
                     (!(tl->is_clickpad && (touch_on_edge & BOTTOM_EDGE))) )
                {
                    ts->tapstate = TS_POSSIBLE_DRAG;
                }
                else
                {
                    /* Send the left click that was pending. */
                    event_button_change(button, button_down);
                    event_button_change(button, button_up);

                    ts->tapstate = TS_TOUCH;
                }
            }

            ts->frac_x = 0;
            ts->frac_y = 0;

            break;

        case TS_POSSIBLE_DRAG:
            if (move)
            {
                /* Generate a left button down event.
                   Tap clicks will be enabled if we are here. */
                event_button_change(button, button_down);
                ts->tapstate = TS_DRAG;
            }
            else if(release)
            {
                ts->tapstate = TS_START;

                /* It is not a drag, just a double tap. Generate two left
                   clicks. Tap clicks will be enabled if we are here. */
                event_button_change(button, button_down);
                event_button_change(button, button_up);

                event_button_change(button, button_down);
                event_button_change(button, button_up);
            }

            break;

        case TS_DRAG:
            if (release)
            {
                ts->tapstate = TS_START;

                /* Generate a left button up event.
                   Tap clicks will be enabled if we are here. */
                event_button_change(button, button_up);
            }
            break;

        default:
            break;
    }
}

static void handle_button_clicks(struct touchpad_packet *tp)
{
    int button_id;
    int mask;

    const int button_down = 1;
    const int button_up = 0;
    const int num_buttons = BTN_TASK - BTN_LEFT;

    /* Generate button click events, if any. */
    for (button_id = 1, mask = 1; button_id <= num_buttons; mask = mask << 1, button_id++)
    {
        if (tp->button_mask & mask)
        {
            event_button_change(button_id, button_down);
        }

        mask = mask << 1;

        if (tp->button_mask & mask)
        {
            event_button_change(button_id, button_up);
        }
    }
}

static void store_history(struct touchpad_state *ts, int x, int y, struct timeval timestamp)
{
    int idx = (ts->move_hist_index + 1) % NUM_MOVE_HISTORY_REC;
    ts->move_hist[idx].x = x;
    ts->move_hist[idx].y = y;
    ts->move_hist[idx].timestamp = timestamp;
    ts->move_hist_index = idx;
}

static double estimate_delta(double x0, double x1, double x2, double x3)
{
    return x0 * 0.3 + x1 * 0.1 - x2 * 0.1 - x3 * 0.3;
}

static double estimate_delta_previous(double x0, double x1)
{
    return ((x0 - x1) * 0.4);
}

static void compute_deltas(struct touchpad_state *ts, 
                           struct touchpad_packet *tp, 
                           struct touchpad_limits *tl, 
                           int *delta_x, 
                           int *delta_y)
{
    const int min_packet_count_move = 3;
    int x_value = tp->x;
    int y_value = tp->y;
    double dx = 0;
    double dy = 0;
    double integral = 0;

    if (x_value == tl->no_x)
    {
        x_value = ts->last_x;
    }

    if (y_value == tl->no_y)
    {
        y_value = ts->last_y;
    }

    if ((ts->tapstate == TS_MOVE || ts->tapstate == TS_DRAG) && !(ts->vert_scroll_on))
    {
        ts->packet_count_move++;

        if (ts->packet_count_move > min_packet_count_move)
        {
            dx = estimate_delta(x_value, HIST(0).x, HIST(1).x, HIST(2).x);
            dy = estimate_delta(y_value, HIST(0).y, HIST(1).y, HIST(2).y);
        }
        else if (ts->packet_count_move > 1)
        {
            dx = estimate_delta_previous(x_value, HIST(0).x);
            dy = estimate_delta_previous(y_value, HIST(0).y);
        }

        /* Adjust deltas according to speed. */
        dx = (dx * (tl->speed)) + (ts->frac_x);
        ts->frac_x = modf(dx, &integral);
        dx = integral;

        dy = (dy * (tl->speed)) + (ts->frac_y);
        ts->frac_y = modf(dy, &integral);
        dy = integral;
    }
    else
    {
        ts->packet_count_move = 0;
    }

    *delta_x = (int) dx;
    *delta_y = (int) dy;

    store_history(ts, x_value, y_value, tp->timestamp);
}

static void send_autoscroll_event(void *unused)
{
    struct timeval tv = {0, 0};

    /* Send scroll event. */
    if (!set_timer_only)
    {
        event_scroll(current_scroll_type);
    }

    if (!event_initialized(&autoscroll_event))
    {
        evtimer_set(&autoscroll_event, wrapper_send_autoscroll_event, NULL);
    }   

    add_timeout_usec(&tv, AUTOSCROLL_TIMEOUT_USEC);
    evtimer_add(&autoscroll_event, &tv);
    is_autoscroll_timer_set = 1;
}

static void wrapper_send_autoscroll_event(int fd, short event , void *opaque)
{
    send_autoscroll_event(opaque);
}

static void reset_autoscroll_timer(void)
{
    struct timeval tv = {0, 0};

    if (is_autoscroll_timer_set)
    {
        add_timeout_usec(&tv, AUTOSCROLL_TIMEOUT_USEC);
        evtimer_add(&autoscroll_event, &tv);
    }
}

static void free_autoscroll_timer(void)
{
    if (is_autoscroll_timer_set)
    {
        evtimer_del(&autoscroll_event);
        is_autoscroll_timer_set = 0;
    }
}

static void reset_scroll_data(struct scroll_data *sd)
{
    sd->up = 0;
    sd->down = 0;
    sd->left = 0;
    sd->right = 0;
}

static void start_corner_scrolling(struct touchpad_state *ts,
                                   struct touchpad_packet *tp,
                                   struct touchpad_limits *tl)
{
    struct timeval diff;
    double elapsed_secs;
    double dy;
    int y_value = tp->y;
    double delta = tl->scroll_dist_vert;

    ts->autoscroll_y = 0;

    /* Turn on autoscroll only when we reach a corner while scrolling,
       not when we start scrolling from a corner. */
    if (ts->scroll_packet_count > 3)
    {
        if (tp->y == tl->no_y)
            y_value = ts->last_y;

        timersub(&(HIST(0).timestamp), &(HIST(3).timestamp), &diff);
        elapsed_secs = (diff.tv_sec) + (diff.tv_usec / 1000000.0);
        dy = (HIST(0).y) - (HIST(3).y);
        if ((elapsed_secs > 0) && (delta > 0))
        {
            ts->autoscroll_yspd = (dy / elapsed_secs) / delta;
            ts->autoscroll_y = (y_value - ts->scroll_y) / delta;
        }

        /* Alps touchpads stop sending events soon after the finger stops
           moving, even if it is still pressed. To handle this, we will
           generate scroll events with the help of a timer. */
        set_timer_only = 1;
        send_autoscroll_event(NULL);
        set_timer_only = 0;

       if (y_value < ts->scroll_y)
           current_scroll_type = SCROLL_UP;
       else
           current_scroll_type = SCROLL_DOWN;
    }

    ts->scroll_packet_count = 0;
}

static void stop_corner_scrolling(struct touchpad_state *ts)
{
    ts->autoscroll_yspd = 0;
    ts->scroll_packet_count = 0;
}

static void handle_scrolling(struct touchpad_state *ts, 
                             struct touchpad_packet *tp, 
                             struct touchpad_limits *tl,
                             struct touchpad_config *tc,
                             enum edge_type edge)
{
    int x_value = tp->x;
    int y_value = tp->y;
    int delta;
    int is_corner = 0;
    double dsecs;
    struct timeval diff;

    const double timeout_secs = 0.2;

    struct scroll_data *sd = &(ts->scrolldata);

    if (!(tc->scrolling_enabled))
        return;

    if (tp->x == tl->no_x)
        x_value = ts->last_x;

    if (tp->y == tl->no_y)
        y_value = ts->last_y;

    reset_scroll_data(sd);

    /* Reset the autoscroll timer whenever a packet is received. */
    reset_autoscroll_timer();

    if ((tp->fingertouching) && !(ts->last_fingertouching))
    {
        stop_corner_scrolling(ts);

        if (edge & RIGHT_EDGE)
        {
            ts->vert_scroll_on = 1;
            ts->scroll_y = y_value;
        }
    }

    if (ts->vert_scroll_on && (!(edge & RIGHT_EDGE) || !(tp->fingertouching)))
        ts->vert_scroll_on = 0;

    /* If we were corner scrolling, but no longer in a corner, or raised finger
       then stop corner scrolling. */
    is_corner = ((edge & RIGHT_EDGE) && (edge & (TOP_EDGE | BOTTOM_EDGE)));
    if ((ts->autoscroll_yspd) && (!(tp->fingertouching) || !(is_corner)))
    {
        stop_corner_scrolling(ts);
    }

    /* If we are no longer in a corner, or raised finger then free the
       autoscroll timer. Note that yspd may have been zero when we were
       corner scrolling. */
    if (!(tp->fingertouching) || !(is_corner))
    {
        free_autoscroll_timer();
    }

    /* If hitting a corner (top or bottom) while vertical scrolling is on,
       start corner scrolling. */
    if ((ts->vert_scroll_on) && is_corner && (ts->autoscroll_yspd == 0))
    {
        start_corner_scrolling(ts, tp, tl);
    }

    if (ts->vert_scroll_on)
    {
        (ts->scroll_packet_count)++;
    }

    if (ts->vert_scroll_on)
    {
        delta = tl->scroll_dist_vert;
        if (delta > 0)
        {
            while ((y_value - ts->scroll_y) > delta)
            {
                (sd->down)++;
                ts->scroll_y += delta;
            }

            while ((y_value - ts->scroll_y) < -delta)
            {
                (sd->up)++;
                ts->scroll_y -= delta;
            }
        }
    }

    if (ts->autoscroll_yspd)
    {
        timersub(&(tp->timestamp), &(HIST(0).timestamp), &diff);
        dsecs = (diff.tv_sec) + (diff.tv_usec / 1000000.0);

        /* If dsecs is not less than timeout_secs, an autoscroll event will
           already have been generated by the timer, so do not generate
           it again. */
        if (dsecs < timeout_secs)
        {
            ts->autoscroll_y += (ts->autoscroll_yspd * dsecs);

            while (ts->autoscroll_y > 1.0)
            {
                sd->down++;
                ts->autoscroll_y -= 1.0;
            }

            while (ts->autoscroll_y < -1.0)
            {
                sd->up++;
                ts->autoscroll_y += 1.0;
            }
        }
    }

    /* Generate scroll events. */
    while ((sd->up)-- > 0)
    {
        event_scroll(SCROLL_UP);
    }
    while ((sd->down)-- > 0)
    {
        event_scroll(SCROLL_DOWN);
    }
}

static void save_previous_packet_values(struct touchpad_state *ts,
                                        struct touchpad_packet *tp,
                                        struct touchpad_limits *tl)
{
    ts->last_fingertouching = tp->fingertouching;

    if (tp->x != tl->no_x)
    {
        ts->last_x = tp->x;
    }

    if (tp->y != tl->no_y)
    {
        ts->last_y = tp->y;
    }
}

static void handle_clickpad(struct touchpad_state *ts,
                            struct touchpad_packet *tp,
                            struct touchpad_limits *tl,
                            enum edge_type edge)
{
    static int button_down_x = -1;

    if (edge & BOTTOM_EDGE)
    {
        /* clickpad reports only left button, and we need to fake
           the right button depending on the touch position. */
        if (tp->button_mask & left_button_down)
        {
            tl->is_clickpad_pressed = 1;
            if (tp->x != tl->no_x)
                button_down_x = tp->x;
            else
                button_down_x = ts->last_x;

            if (button_down_x > tl->clickpad_left_button_maxx)
                tp->button_mask = (tp->button_mask & (!left_button_down)) | right_button_down;
        }
        else if (tp->button_mask & left_button_up)
        {
            tl->is_clickpad_pressed = 0;
            if (button_down_x > tl->clickpad_left_button_maxx)
                tp->button_mask = (tp->button_mask & (!left_button_up)) | right_button_up;

            button_down_x = -1;
        }

        handle_button_clicks(tp);
    }
}

static void process_packet(struct touchpad_state *ts, 
                           struct touchpad_packet *tp, 
                           struct touchpad_limits *tl, 
                           struct touchpad_config *tc)
{
    int x_value = tp->x;
    int y_value = tp->y;
    int dx;
    int dy;
    enum edge_type edge;

    if (tl->is_clickpad == 0)
        handle_button_clicks(tp);

    /* If the touchpad has been disabled, only generate button clicks. Do not
       handle moves, scrolling etc. */
    if (ts->touchpad_off == 1)
        return;

    if (tp->x == tl->no_x)
        x_value = ts->last_x;

    if (tp->y == tl->no_y)
        y_value = ts->last_y;

    edge = detect_edge(x_value, y_value, tl->right_edge, tl->bottom_edge, tl->top_edge);

    tp->fingertouching = detect_finger(tp->pressure, tl);

    if (tl->is_clickpad)
        handle_clickpad(ts, tp, tl, edge);

    handle_gestures(ts, tp, tl, tc);

    handle_scrolling(ts, tp, tl, tc, edge);

    compute_deltas(ts, tp, tl, &dx, &dy);

    /* Generate move events. */
    if (dx || dy)
    {
        /* Do not generate move events if the clickpad is pressed on the bottom edge. */
        if (!(tl->is_clickpad_pressed && (edge & BOTTOM_EDGE)))
        {
            event_relative_move(dx, dy);
        }
    }

    /* Save the values of some state variables. */
    save_previous_packet_values(ts, tp, tl);
}

void handle_touchpad_event(struct input_event *ev,int slot)
{
    static int sync_recd = 1;
    gSlot=slot;

    if (sync_recd == 1)
    {
        reset_touchpad_packet(&tpacket, &tlimits);
        sync_recd = 0;
    }

    if ((ev->type == EV_SYN) && (ev->code == SYN_REPORT))
    {
        /* Packet is complete, now process it. */
        tpacket.timestamp = ev->time;
        sync_recd = 1;
        process_packet(&tstate, &tpacket, &tlimits, &tconfig);
    }
    else if (ev->type == EV_ABS)
    {
        if ((ev->code == ABS_X) || (ev->code == ABS_MT_POSITION_X))
            tpacket.x = ev->value;
        else if ((ev->code == ABS_Y) || (ev->code == ABS_MT_POSITION_Y))
            tpacket.y = ev->value;
        else if ((ev->code == ABS_PRESSURE) || (ev->code ==ABS_MT_PRESSURE))
            tpacket.pressure = ev->value;

    }
    else if (ev->type == EV_KEY)
    {
     
        if ((ev->code >=BTN_LEFT) && (ev->code <=BTN_TASK))
        {       
            int bitpair = (ev->code - BTN_LEFT) << 1;
            tpacket.button_mask |= 1 << ( bitpair + (ev->value?0:1));
        }
    }
}

static void init_touchpad_state(struct touchpad_state *ts)
{
    ts->last_x = 0;
    ts->last_y = 0;
    ts->last_fingertouching = 0;
    ts->tapstate = TS_START;
    ts->move_hist_index = -1;
    ts->packet_count_move = 0;
    ts->vert_scroll_on = 0;
    ts->scroll_y = 0;
    ts->scroll_packet_count = 0;
    ts->autoscroll_yspd = 0;
    ts->autoscroll_y = 0;
    ts->frac_x = 0;
    ts->frac_y = 0;
    ts->touchpad_off = 0;

    reset_scroll_data(&(ts->scrolldata));
}

static void disable_touchpad(void)
{
    tstate.touchpad_off = 1;
}

static void enable_touchpad(void)
{
    init_touchpad_state(&tstate);
}

static int disable_touchpad_cb(void *opaque)
{
    disable_touchpad();
    return 0;
}

static int enable_touchpad_cb(void *opaque)
{
    enable_touchpad();
    return 0;
}

void toggle_touchpad_status(void)
{
    if (tstate.touchpad_off == 1)
        enable_touchpad();
    else
        disable_touchpad();
}

/* For the HP8440p, keyboard events are generated when the touchpad on/off
 * button is pressed, so handle those.
 */
static void add_touchpad_bindings(void)
{
    {
        int touchpad_off[] = { KEY_PROG2, -1 };
        input_add_binding (touchpad_off, disable_touchpad_cb, NULL, (void *) 0);
    }

    {
        int touchpad_on[] = { KEY_HELP, -1 };
        input_add_binding (touchpad_on, enable_touchpad_cb, NULL, (void *) 0);
    }
}

static void compute_touchpad_speed(struct touchpad_config *tc, struct touchpad_limits *tl)
{
    const double default_speed = 0.15;
    const double min_speed = 0.1;
    const double max_speed = 0.9;
    const int default_config_speed = 5;
    const int min_config_speed = 1;
    const int max_config_speed = 10;

    double units;
    int width;
    int height;
    int diag;

    width = abs(tl->maxx - tl->minx);
    height = abs(tl->maxy - tl->miny);
    diag = sqrt(width * width + height * height);

    tl->speed = default_speed;

    if ((diag != default_diag) && (diag > 0))
    {
        tl->speed = (default_speed * default_diag) / (double)diag;

        if (tl->speed > max_speed)
            tl->speed = max_speed;

        if (tl->speed < min_speed)
            tl->speed = min_speed;
    }


    /* Adjust the speed according to the configuration. */
    if ((tc->speed < min_config_speed) || (tc->speed > max_config_speed))
    {
        info("touchpad speed out of range, setting it to %d", default_config_speed);
        tc->speed = default_config_speed;
    }

    if (tc->speed == default_config_speed)
        return;

    units = ((tl->speed) / (double)default_config_speed);

    if (tc->speed < default_config_speed)
    {
        tl->speed = tl->speed - (units * (default_config_speed - tc->speed)) ;
    }
    else
    {
        tl->speed = tl->speed + (units * (tc->speed - default_config_speed)) ;
    }
}

int init_touchpad(int fd)
{
    const double default_edge_mult = 0.17;
    const double edge_mult_low_res = 0.20;
    const double clickpad_bottom_edge_mult = 0.20;
    const double clickpad_left_button_maxx_mult = 0.50;

    struct input_absinfo absinfo;
    int ret = 0;
    int width;
    int height;
    int diag;
    double edge_width;
    double edge_mult = default_edge_mult;
    unsigned long keybits[NBITS(KEY_MAX)];

    struct stat stat_buf;
    const char *disable_touchpad_file = "/config/disable-touchpad";

    /* Check if the touchpad needs to be disabled. */
    if (stat(disable_touchpad_file, &stat_buf) == 0)
    {
        info("Disabling touchpad due to presence of file %s", disable_touchpad_file);
        return -1;
    }

    memset(keybits, 0, sizeof(keybits));

    if ((ret = ioctl(fd, EVIOCGABS(ABS_X), &absinfo)) < 0)
        return ret;
    tlimits.minx = absinfo.minimum;
    tlimits.maxx = absinfo.maximum;

    if ((ret = ioctl(fd, EVIOCGABS(ABS_Y), &absinfo)) < 0)
        return ret;
    tlimits.miny = absinfo.minimum;
    tlimits.maxy = absinfo.maximum;

    if ((ret = ioctl(fd, EVIOCGABS(ABS_PRESSURE), &absinfo)) < 0)
        return ret;
    tlimits.min_pressure = absinfo.minimum;
    tlimits.max_pressure = absinfo.maximum;

    if ((ret = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits)) < 0)
        return ret;

    /* Clickpads only report left button. */
    if (TEST_BIT(BTN_LEFT, keybits) &&
        !TEST_BIT(BTN_RIGHT, keybits) &&
        !TEST_BIT(BTN_MIDDLE, keybits))
    {
        tlimits.is_clickpad = 1;
    }
    else
    {
        tlimits.is_clickpad = 0;
    }
    tlimits.is_clickpad_pressed = 0;

    /* Calculate minimum distance for a move event to be generated. */
    width = abs(tlimits.maxx - tlimits.minx);
    height = abs(tlimits.maxy - tlimits.miny);
    diag = sqrt(width * width + height * height);
    tlimits.tap_move = diag * MOVE_MULT;

    /* Calculate edge widths. */
    if (diag < default_diag)
    {
        edge_mult = edge_mult_low_res;
    }

    edge_width = width * edge_mult;
    tlimits.right_edge = tlimits.maxx - edge_width;

    edge_width = height * edge_mult;
    tlimits.bottom_edge = tlimits.maxy - edge_width;
    tlimits.top_edge = tlimits.miny + edge_width;

    if (tlimits.is_clickpad)
    {
        edge_width = height * clickpad_bottom_edge_mult;
        tlimits.bottom_edge = tlimits.maxy - edge_width;

        edge_width = width * clickpad_left_button_maxx_mult;
        tlimits.clickpad_left_button_maxx = tlimits.minx + edge_width;
    }
    else
    {
        tlimits.clickpad_left_button_maxx = tlimits.minx - 1;
    }

    /* Calculate the minimum horizontal and vertical scroll distance. */
    tlimits.scroll_dist_vert = diag * SCROLL_MULT;

    /* If the event has no x, y or pressure values, use these instead. */
    tlimits.no_x = tlimits.minx - 1;
    tlimits.no_y = tlimits.miny - 1;
    tlimits.no_pressure = tlimits.min_pressure - 1;

    /* Initialise the touchpad state. */
    init_touchpad_state(&tstate);

    /* Get the touchpad configuration. */
    touchpad_reread_config();

    add_touchpad_bindings();

    if (tlimits.is_clickpad)
    {
        info("device on fd %d is a clickpad", fd);
    }

    return 0;
}

void touchpad_reread_config(void)
{
    tconfig.tap_to_click_enabled = touchpad_get_tap_to_click_enabled();
    tconfig.scrolling_enabled = touchpad_get_scrolling_enabled();
    tconfig.speed = touchpad_get_speed();
    compute_touchpad_speed(&tconfig, &tlimits);
}
