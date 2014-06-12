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

#ifndef GESTURE_H
#define GESTURE_H

typedef enum gesture_position
{
    eNa_p,
    eGiven_p1,
    eGiven_p2,

    eAbsPosition,
    eLeft,
    eRight,
    eTop,
    eBottom,
    eTop_Left,
    eTop_Right,
    eBottom_Left,
    eBottom_Right
} gesture_position;

enum action_type
{
    eEnd,
    eContact,
    eRelease,
    eMove,
};

typedef enum post_movement
{
    eNa_m,
    eDontMove,
    eMoveAnywhere,

    eMoveLeft,
    eMoveRight,
    eMoveUp,
    eMoveDown,

    eMoveLeftDown,
    eMoveRightDown,
    eMoveLeftUp,
    eMoveRighUp,

    eGiven_m1,
    eGiven_m2,
} post_movement;

struct smallaction_slot
{
    enum action_type action;
    int finger;
    enum gesture_position pos;
    int timel;
    enum post_movement pmove;
};

struct smallaction_list
{
    struct smallaction_slot *actions;
    gesture_position p1;
    gesture_position p2;
    post_movement m1;
    post_movement m2;
    int timelimit;
};

typedef struct gesture
{
    struct smallaction_list *actionlist;
    void (*callback) (void);
    int act;
    int sub_act;
    char name[10];
} gesture;

#endif
