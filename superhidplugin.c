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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdint.h>
#include <event.h>
#include <linux/input.h>
#include <fcntl.h>

#define SOCK_PATH "/var/run/input_socket"
#define MAGIC 0xAD9CBCE9

#define EVENT_SIZE (sizeof(struct event_record))
#define buffersize (EVENT_SIZE*20)

/* Report IDs for the various devices */
#define REPORT_ID_KEYBOARD      0x01
#define REPORT_ID_MOUSE         0x02
#define REPORT_ID_TABLET        0x03
#define REPORT_ID_MULTITOUCH    0x04
#define REPORT_ID_STYLUS        0x05
#define REPORT_ID_PUCK          0x06
#define REPORT_ID_FINGER        0x07
#define REPORT_ID_MT_MAX_COUNT  0x10
#define REPORT_ID_CONFIG        0x11
#define REPORT_ID_INVALID       0xff

/* Shouldn't that be defined somewhere already? */
#define ABS_MT_SLOT		0x2f
#define EV_DEV			0x06
#define DEV_SET			0x01

/* All the following is specific to the superhid digitizer */
#define TIP_SWITCH	0x01
#define IN_RANGE	0x02
#define DATA_VALID	0x04
#define FINGER_1	0x08
#define FINGER_2	0x10
#define FINGER_3	0x18

#define LOW_X	0
#define HIGH_X	0xFFF
#define LOW_Y	0
#define HIGH_Y	0xFFF

/* This is actually 8, but we don't want to segv if input_server sends 10 */
#define MAX_FINGERS	10

struct event_record
{
    uint32_t magic;
    uint16_t itype;
    uint16_t icode;
    uint32_t ivalue;
} __attribute__ ((__packed__));

struct buffer_t
{
    char buffer[buffersize];
    unsigned int bytes_remaining;
    int position;
    int s;
    int copy;
    int block;
} buffers;

struct superhid_report
{
    uint8_t report_id;
    uint8_t misc;
    uint16_t x;
    uint16_t y;
} __attribute__ ((__packed__));

int hid_fd;
struct event recv_event;

static void stop ()
{
    event_del (&recv_event);
}

/* Some OSes swap the x,y coordinates for some reason... */
static uint16_t swap_bytes(uint16_t n)
{
  uint16_t res;

  res = ((n << 8) & 0xFF00) + (n >> 8);

  return res;
}

/* This function simulates a touchscreen from a mouse. */
/* Useful to debug the driver without a tablet handy! */
static void process_relative_event(uint16_t itype, uint16_t icode, uint32_t ivalue,
				   char left, char middle, char right)
{
  struct superhid_report report;
  static uint16_t x = LOW_X;
  static uint16_t y = LOW_Y;

  if (itype != EV_REL && itype != EV_KEY)
    return;

  if (itype == EV_REL && icode == ABS_X && x + ivalue > LOW_X && x + ivalue < HIGH_X)
    x += ivalue;

  if (itype == EV_REL && icode == ABS_Y && y + ivalue > LOW_Y && y + ivalue < HIGH_Y)
    y += ivalue;

  report.report_id = REPORT_ID_MULTITOUCH;
  report.misc = 0;
  report.misc |= FINGER_1;
  if (left)
  report.misc |= TIP_SWITCH;
  report.misc |= IN_RANGE;
  report.misc |= DATA_VALID;
  /* report.x = swap_bytes(x); */
  /* report.y = swap_bytes(y); */
  report.x = x;
  report.y = y;

  write(hid_fd, &report, 6);

  if (middle)
    {
      report.misc |= TIP_SWITCH;
      report.misc &= 0xF7;
      report.misc |= FINGER_2;
      report.x = report.x + 50;
      write(hid_fd, &report, 6);
    }

  if (right)
    {
      report.misc |= TIP_SWITCH;
      report.misc &= 0xE7;
      report.misc |= FINGER_3;
      if (middle)
	report.x = report.x + 50;
      else
	report.x = report.x + 100;
      write(hid_fd, &report, 6);
    }
}

static void process_absolute_event(uint16_t itype, uint16_t icode, uint32_t ivalue)
{
  static struct superhid_report report[MAX_FINGERS] = { 0 };
  static int finger = 0;
  int i;
  static char just_syned = 0;

  /* Initialize the report array */
  if (report[finger].report_id == 0)
    {
      for (i = 0; i < MAX_FINGERS; ++i)
	{
	  report[i].report_id = REPORT_ID_MULTITOUCH;
	  report[i].misc = 0;
	  /* TODO: use that flag(s) properly :) */
	  report[i].misc |= IN_RANGE;
	  report[i].misc |= DATA_VALID;
	  /* Setting the finger ID */
	  report[i].misc |= (i << 3) & 0xF8;
	  /* Finger touching by default? */
	  /* report[i].misc |= TIP_SWITCH; */
	}
    }

  switch (itype)
    {
    case EV_ABS:
      switch (icode)
	{
	/* case ABS_X: */
	case ABS_MT_POSITION_X:
	  report[finger].x = ivalue >> 3;
	  break;
	/* case ABS_Y: */
	case ABS_MT_POSITION_Y:
	  report[finger].y = ivalue >> 3;
	  break;
	case ABS_MT_SLOT:
	  /* We force a SYN_REPORT on ABS_MT_SLOT, because the device is serial. */
	  /* However, we don't want to send twice the same event for nothing... */
	  if (!just_syned)
	    write(hid_fd, &(report[finger]), 6);
	  finger = ivalue;
	  break;
	case ABS_MT_TRACKING_ID:
	  if (ivalue == 0xFFFFFFFF)
	    report[finger].misc &= ~TIP_SWITCH;
	  else
	    report[finger].misc |= TIP_SWITCH;
	default:
	  break;
	}
      break;
    case EV_KEY:
      switch (icode)
	{
	default:
	  break;
	}
      break;
    case EV_SYN:
      switch (icode)
	{
	case SYN_REPORT:
	  write(hid_fd, &(report[finger]), 6);
	  just_syned = 1;
	  /* re-init */
	  /* Nothing to do? */
	  return;
	  break;
	default:
	  break;
	}
    default:
      break;
    }

  just_syned = 0;
}

static void process_event (struct event_record *r, struct buffer_t *b)
{
  uint16_t itype;
  uint16_t icode;
  uint32_t ivalue;
  static int dev_set;
  static char left = 0;
  static char middle = 0;
  static char right = 0;

  itype = r->itype;
  icode = r->icode;
  ivalue = r->ivalue;

  if (itype == EV_KEY && icode == BTN_LEFT)
    left = !!ivalue;
  if (itype == EV_KEY && icode == BTN_MIDDLE)
    middle = !!ivalue;
  if (itype == EV_KEY && icode == BTN_RIGHT)
    right = !!ivalue;

  /* Uncomment the following line to emulate a touchscreen from a mouse */
  /* process_relative_event(itype, icode, ivalue, left, middle, right); */

  if (itype == EV_DEV && icode == DEV_SET)
    {
      dev_set = ivalue;
      printf("DEV_SET %d\n", dev_set);
    }

  if (dev_set != 6)
    return;

  process_absolute_event(itype, icode, ivalue);
}

struct event_record *findnext (struct buffer_t *b)
{
    struct event_record *r = NULL;
    int start = b->position;

    /* Skip junk */
    while (b->bytes_remaining >= EVENT_SIZE &&
	   (r = (struct event_record *) &b->buffer[b->position]) && r->magic != MAGIC)
    {
      printf("SKIPPED!\n");
	b->bytes_remaining--;
	b->position++;
    }

    if (start != b->position)
	printf ("Warning: Encountered %d bytes of junk.\n", b->position - start);

    if (b->bytes_remaining >= EVENT_SIZE)
    {
	b->bytes_remaining -= EVENT_SIZE;
	b->position += EVENT_SIZE;
	return r;
    }
    else
	return NULL;
}

static void recv_callback (int fd, short event, void *opaque)
{
    int n;
    struct buffer_t *buf = &buffers;
    char *b = buf->buffer;

    memmove (b, &b[buf->position], buf->bytes_remaining);
    buf->position = 0;
    size_t nbytes = 0;
    n = recv (fd, &b[buf->bytes_remaining], buffersize - buf->bytes_remaining, 0);

    if (n > 0)
    {
	struct event_record *r = NULL;
	buf->bytes_remaining += n;

	while ((r = findnext (buf)) != NULL)
	  process_event (r, buf);
    }
    else if (n)
	printf ("Error %d\n", n);
    else
      stop();
}

/* This function tells input_server to send us the events for the domain */
void suck (int s, int d)
{
    struct event_record e;

    e.magic = MAGIC;
    e.itype = 7;
    e.icode = 0x2;
    e.ivalue = d;

    if (send (s, &e, sizeof (struct event_record), 0) == -1)
    {
	perror ("send");
	exit (1);
    }
}

int main (int argc, char** argv)
{
    int s, t, len;
    struct sockaddr_un remote;
    char str[100];
    pthread_t output_thread_var;

    if (argc != 2)
      {
	printf("Usage: superhidplugin <domid>\n");
	exit(1);
      }

    /* Trying to connect to input_server to get events */
    if ((s = socket (AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
	perror ("socket");
	exit (1);
    }

    printf ("Trying to connect...\n");

    remote.sun_family = AF_UNIX;
    strcpy (remote.sun_path, SOCK_PATH);
    len = strlen (remote.sun_path) + sizeof (remote.sun_family);
    if (connect (s, (struct sockaddr *) &remote, len) == -1)
    {
	perror ("connect");
	exit (1);
    }

    printf ("Connected.\n");

    buffers.bytes_remaining = 0;
    buffers.position = 0;
    buffers.copy = 0;
    buffers.block = 0;
    buffers.s = s;

    /* Now connect to the superhid device */
    hid_fd = open("/dev/hidg0", O_RDWR, 0666);
    if (hid_fd == -1)
      {
	perror("/dev/hidg0");
	exit(1);
      }

    event_init ();

    event_set (&recv_event, s, EV_READ | EV_PERSIST, recv_callback, NULL);
    event_add (&recv_event, NULL);

    suck (s, strtol(argv[1], NULL, 0));

    event_dispatch ();

    close (s);

    return 0;
}
