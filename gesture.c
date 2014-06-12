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

// #define GESTURE_DEBUG

#define SCREEN_X        32767
#define SCREEN_Y        32767

/* Those are the values we actually use */
#define LEFT_X    (SCREEN_X / 4)
#define RIGHT_X   (3*SCREEN_X / 4)
#define MIDDLE_X  (SCREEN_X / 2)

#define TOP_Y     (SCREEN_Y / 3)
#define BOTTEM_Y  (2*SCREEN_Y / 3)
#define MIDDLE_Y  (SCREEN_Y / 2)
/* The deltas for the finger being "close to the center" */
#define DELTA_X   (SCREEN_X / 5)
#define DELTA_Y   (SCREEN_Y / 5)

#define NUM_FINGERS 3
#define MIN_DIST (SCREEN_X/50)
typedef enum
{
    eStartup,
    eRunning,
    eStandBack
} runstate_t;



static runstate_t runstate = eStartup;

struct fingerinfo
{
    int last_x;
    int last_y;
};

static struct fingerinfo fingers[NUM_FINGERS];

static void standback (void)
{
    if (runstate == eRunning)
    {
        runstate = eStandBack;
        info ("Standing back\n");
    }
    else if (runstate == eStandBack)
    {
        runstate = eRunning;
        info ("Standing forward\n");
    }
}

static void to_the_left (void)
{
    switcher_switch_right ();
}

static void to_the_right (void)
{
    switcher_switch_left ();
}

static void to_UI (void)
{
    switcher_switch (domain_uivm (), 0, 0);
}


#ifdef GESTURE_DEBUG
char action_type_name[][10] = { "end", "Contact", "Release", "Move" };
char guesture_position_name[][15] =
    { "N/A", "P1", "P2", "!AP!", "Left", "Right", "Top", "Bottom", "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
char guesture_movement_name[][10] =
    { "N/A", "dontmove", "anything", "left", "right", "up", "down", "LeftDown", "RightDown", "LeftUp", "RighUp", "M1", "M2" };
#endif



int position_match (gesture_position a, int x, int y)
{
    switch (a)
    {
    case eLeft:
        /* x is on the left part-screen,   y is close to the middle */
        return (x < LEFT_X && y < MIDDLE_Y + DELTA_Y && y > MIDDLE_Y - DELTA_Y);
    case eRight:
        /* x is on the right part-screen,  y is close to the middle */
        return (x > RIGHT_X && y < MIDDLE_Y + DELTA_Y && y > MIDDLE_Y - DELTA_Y);
    case eTop:
        /* y is on the top part-screen,    x is close to the middle */
        return (y < TOP_Y && x < MIDDLE_X + DELTA_X && x > MIDDLE_X - DELTA_X);
    case eBottom:
        /* y is on the bottom part-screen, x is close to the middle */
        return (y > BOTTEM_Y && x < MIDDLE_X + DELTA_X && x > MIDDLE_X - DELTA_X);

    case eTop_Left:
        /* x is on the left part-screen,   y is on the top part-screen */
        return (x < LEFT_X && y < TOP_Y);
    case eTop_Right:
        /* x is on the right part-screen,  y is on the top part-screen */
        return (x > RIGHT_X && y < TOP_Y);
    case eBottom_Left:
        /* x is on the left part-screen,   y is on the bottom part-screen */
        return (x < LEFT_X && y > BOTTEM_Y);
    case eBottom_Right:
        /* x is on the right part-screen,   y is on the bottom part-screen */
        return (x > RIGHT_X && y > BOTTEM_Y);
    default:
        warning ("Unhandle gesture: %d", a);
    };

    return 0;
};

#define ANY -1
#define NONE -1

#define ENDACTION         { .action = eEnd,        .finger = ANY,        .pos = eNa_p,                        .timel =  NONE,        .pmove = eNa_m}
#define ENDACTIONLIST        {.actions = NULL}

static struct smallaction_slot singledrag_actions[] = {
    {.action = eContact, .finger = 0,  .pos = eGiven_p1, .timel = NONE,.pmove = eNa_m},
    {.action = eRelease, .finger = 0,  .pos = eGiven_p2, .timel = NONE,.pmove = eGiven_m1}
};

static struct smallaction_slot twotap_actions[] = {
    {.action = eContact, .finger = 0,  .pos = eGiven_p1,.timel = NONE,.pmove = eDontMove},
    {.action = eContact, .finger = 1,  .pos = eGiven_p1,.timel = 1,   .pmove = eDontMove},
    {.action = eRelease, .finger = ANY,.pos = eNa_p,    .timel = 5,   .pmove = eNa_m},
    {.action = eRelease, .finger = ANY,.pos = eNa_p,    .timel = 1,   .pmove = eNa_m},
    ENDACTION
};

static struct smallaction_slot twodrag_actions[] = {
    {.action = eContact, .finger = 0,  .pos = eGiven_p1, .timel = NONE,.pmove = eNa_m},
    {.action = eContact, .finger = 1,  .pos = eGiven_p1, .timel = 1,   .pmove = eNa_m},
    {.action = eRelease, .finger = ANY,.pos = eGiven_p2, .timel = 5,   .pmove = eGiven_m1},
    {.action = eRelease, .finger = ANY,.pos = eGiven_p2, .timel = 1,   .pmove = eGiven_m1},
    ENDACTION
};

/* switch vm actions */
static struct smallaction_list goleft_actions[] = {
    {.actions = twodrag_actions,.p1 = eRight,.p2 = eLeft,.m1 = eMoveLeft, eNa_m,.timelimit = 10},
    ENDACTIONLIST
};
static gesture goleft = {.actionlist = goleft_actions,.callback = to_the_left,.name = "GoLeft" };

static struct smallaction_list goright_actions[] = {
    {.actions = twodrag_actions,.p1 = eLeft,.p2 = eRight,.m1 = eMoveRight, eNa_m,.timelimit = 10},
    ENDACTIONLIST
};
static gesture goright = {.actionlist = goright_actions,.callback = to_the_right,.name = "GoRight" };


static struct smallaction_list goup_actions[] = {
    {.actions = twodrag_actions,.p1 = eBottom,.p2 = eTop,.m1 = eMoveUp, eNa_m,.timelimit = 10},
    ENDACTIONLIST
};
static gesture goup = {.actionlist = goup_actions,.callback = to_UI,.name = "GoUp" };


/* Cross action */
static struct smallaction_list crossL_actions[] = {
    {.actions = singledrag_actions,.p1 = eTop_Left,.p2 = eBottom_Right,.m1 = eMoveRightDown, eNa_m,.timelimit = 10},
    {.actions = singledrag_actions,.p1 = eTop_Right,.p2 = eBottom_Left,.m1 = eMoveLeftDown, eNa_m,.timelimit = 10},
    ENDACTIONLIST
};

static struct smallaction_list crossR_actions[] = {
    {.actions = singledrag_actions,.p1 = eTop_Right,.p2 = eBottom_Left,.m1 = eMoveLeftDown, eNa_m,.timelimit = 10},
    {.actions = singledrag_actions,.p1 = eTop_Left,.p2 = eBottom_Right,.m1 = eMoveRightDown, eNa_m,.timelimit = 10},
    ENDACTIONLIST
};
static gesture crossL = {.actionlist = crossL_actions,.callback = standback,.name = "CrossL" };
static gesture crossR = {.actionlist = crossR_actions,.callback = standback,.name = "CrossR" };

static gesture *gestures[] = { &goleft, &goright, &goup, &crossL, &crossR, NULL };
static gesture *standback_gestures[] = { &crossL, &crossR, NULL };


int gesture_match (gesture * g, int slot, int x, int y, int push, int *tracking)
{
    struct smallaction_list *sa_list;
    struct smallaction_slot *current;

    sa_list = &(g->actionlist[g->act]);
    current = &(sa_list->actions[g->sub_act]);

#ifdef GESTURE_DEBUG
    char dbgmessage[100];
#endif

    gesture_position pos = current->pos;
    if (pos == eGiven_p1)
        pos = sa_list->p1;
    if (pos == eGiven_p2)
        pos = sa_list->p2;

#ifdef GESTURE_DEBUG
    post_movement pmove = current->pmove;
    if (pmove == eGiven_m1)
        pmove = sa_list->m1;
    if (pmove == eGiven_m2)
        pmove = sa_list->m2;


    sprintf (dbgmessage, "%s: #%d/%d: %s on finger %d to the %s before %ds alowing %s movment before.", g->name, g->act,
             g->sub_act, action_type_name[current->action], current->finger, guesture_position_name[pos],
             current->timel, guesture_movement_name[pmove]);
#endif

    bool success = false;

    switch (current->action)
    {
    case eContact:
        success = (push && position_match (pos, x, y));
        break;
    case eRelease:
        success = (!push && position_match (pos, x, y));
        break;

    default:
        info ("Bad Action!\n");
    }

    if (success)
    {
#ifdef GESTURE_DEBUG
        info ("%s Found!\n", dbgmessage);
#endif
        g->sub_act++;
        fingers[slot].last_x = x;
        fingers[slot].last_y = y;
    }
    else
    {
        if ((g->sub_act > 0) || (g->act > 0))
        {
#ifdef GESTURE_DEBUG
            info ("%s Fail!\n", dbgmessage);
#endif
            g->sub_act = 0;
            g->act = 0;
        }
    }

    if ((sa_list->actions[g->sub_act]).action == eEnd)
    {
        g->act++;
        g->sub_act = 0;
        sa_list = &(g->actionlist[g->act]);

        if (sa_list->actions == NULL)
        {
#ifdef GESTURE_DEBUG
            info ("Guesture recognise!\n");
#endif
            g->act = 0;
            g->callback ();
            return true;
        }
    }
    if ((g->sub_act > 1) || (g->act > 0))
    {
        (*tracking)++;
    }

    return false;
}


void gestures_clean ()
{
    unsigned int i;

    for (i = 0; gestures[i]; ++i)
    {
        gestures[i]->act = 0;
        gestures[i]->sub_act = 0;
    };
}

int gesture_match_move (gesture * g, int slot, int x, int y, int push, int *tracking)
{

    struct smallaction_list *sa_list;
    struct smallaction_slot *current;

    sa_list = &(g->actionlist[g->act]);
    current = &(sa_list->actions[g->sub_act]);
    post_movement pmove = current->pmove;

    int fail = 0;
    int dx = x - fingers[slot].last_x;
    int dy = y - fingers[slot].last_y;

    if (!g->act && !g->sub_act)
        return false;

    if ((abs (dx) < MIN_DIST) && (abs (dy) < MIN_DIST))
    {
        goto dontcheck;
    }

    if ((pmove == eNa_m) || (pmove == eMoveAnywhere))
    {
        goto dontcheck;
    }

    if (pmove == eGiven_m1)
        pmove = sa_list->m1;
    if (pmove == eGiven_m2)
        pmove = sa_list->m2;

    switch (pmove)
    {
    case eDontMove:        fail++;
        break;

    case eMoveLeft:        if ((dx > 0) || (-dx < abs (dy)))  fail++;
        break;
    case eMoveRight:        if ((dx < 0) || ( dx < abs (dy)))  fail++;
        break;
    case eMoveUp:        if ((dy > 0) || (-dy < abs (dx)))  fail++;
        break;
    case eMoveDown:        if ((dy < 0) || ( dy < abs (dx)))  fail++;
        break;

    case eMoveLeftDown:        if ((dx > 0) || (dy < 0))          fail++;
        break;
    case eMoveRightDown:if ((dx < 0) || (dy < 0))          fail++;
        break;
    case eMoveLeftUp:        if ((dx > 0) || (dy > 0))          fail++;
        break;
    case eMoveRighUp:        if ((dx < 0) || (dy > 0))          fail++;
        break;
    default:
        info ("Unexpected Movement restriction type %d!\n", pmove);
    }

    if (fail)
    {
#ifdef GESTURE_DEBUG
        info ("%s: %d/%d Fail on wrong motion!\n", g->name, g->act, g->sub_act);
#endif
        g->sub_act = 0;
        g->act = 0;
        return false;
    }
    else
    {
        fingers[slot].last_x = x;
        fingers[slot].last_y = y;
    }

dontcheck:

    if ((g->sub_act > 1) || (g->act > 0))
    {
        (*tracking)++;
    }

    return false;
}


int gesture_handler (int slot, int x, int y, int push)
{
    int (*gesture_matcher) (gesture *, int, int, int, int, int *);
    gesture **glist;
 unsigned int i;
    int tracking = 0;

    gesture_matcher = (push == -1) ? gesture_match_move : gesture_match;

    if (runstate == eStartup)
    {
        gestures_clean ();
        runstate = eRunning;
    }

    glist = (runstate == eStandBack) ? standback_gestures : gestures;

    for (i = 0; glist[i]; ++i)
    {
        if (gesture_matcher (glist[i], slot, x, y, push, &tracking))
        {
            /* we've got a match on gestures[i], let's clear all
               statuses and leave */
            gestures_clean ();
            return 0;
        }

    };
    return tracking;
}
