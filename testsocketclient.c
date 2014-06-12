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
#include <stdint.h>
#include <event.h>
#include <linux/input.h>

#define SOCK_PATH "/var/run/input_socket"
#define MAGIC 0xAD9CBCE9
#define EV_DEV      0x6


char typestr[][4] = { "SYN", "KEY", "REL", "ABS", "MSC", "SW " };
char devstr[][45] =
    { "\nChange to device %d\n\n", "\nNew device %d on system!\n\n", "\nDevice %d was removed from the system!\n\n" };

char keys[] =
    { '\0', 'E', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 'B', '\t', 'q', 'w', 'e', 'r', 't', 'y',
    'u', 'i', 'o', 'p', '{', '}',
    'M', 'C', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '~', 'S', '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
	',', '.', '/', '\0', '\0', 'A', ' '
};

struct event_record
{
    uint32_t magic;
    uint16_t itype;
    uint16_t icode;
    uint32_t ivalue;
} __attribute__ ((__packed__));

#define EVENT_SIZE (sizeof(struct event_record))
#define buffersize (EVENT_SIZE*20)
struct buffer_t
{
    char buffer[buffersize];
    unsigned int bytes_remaining;
    int position;
    int s;
    int copy;
    int block;
} buffers;

struct event_record *findnext (struct buffer_t *b);
static void process_event (struct event_record *r, struct buffer_t *b);
static void stop ();
static void recv_callback (int fd, short event, void *opaque)
{
    int n;
    struct buffer_t *buf = &buffers;
    char *b = buf->buffer;

    memmove (b, &b[buf->position], buf->bytes_remaining);
    buf->position = 0;
    n = recv (fd, &b[buf->bytes_remaining], buffersize - buf->bytes_remaining, 0);

    if (n > 0)
    {
	struct event_record *r = NULL;
	buf->bytes_remaining += n;

	while ((r = findnext (buf)) != NULL)
	{
	    process_event (r, buf);
	}
    }
    else if (n)
	printf ("Error %d\n", n);
    else
	stop ();
}

static void process_event (struct event_record *r, struct buffer_t *b);

struct event_record *findnext (struct buffer_t *b)
{
    struct event_record *r = NULL;
    int start = b->position;
// Skip junk
    while (b->bytes_remaining >= EVENT_SIZE &&
	   (r = (struct event_record *) &b->buffer[b->position]) && r->magic != MAGIC)
    {
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

show_code (struct event_record * r)
{
    if (r->itype == EV_DEV)
    {
	if ((r->icode < 4) && (r->icode > 0))
	    printf (devstr[r->icode - 1], r->ivalue);
	else
	    printf ("Invalid EV_DEV code %x, value %x\n", r->icode, r->ivalue);
    }
    else
    {
	printf ("event %d (%s), %d %d\n", r->itype, (r->itype < 6) ? typestr[r->itype] : "???", r->icode, r->ivalue);
	if ((r->itype == EV_SYN) && (r->icode == SYN_REPORT))
	    printf ("\n");
    }

}


static void process_event (struct event_record *r, struct buffer_t *b)
{
    if (r->itype == 0x7)
    {
	if (r->icode == 0x3)
	{
	    printf ("Error %d, %d, %d\n", (r->ivalue & 0xFF000000) >> (8 * 3), (r->ivalue & 0x00FF0000) >> (8 * 2),
		    (r->ivalue & 0x0000FF00) >> (8 * 1));
	}
	else
	    printf ("unexpected event %d, %d %d\n", r->itype, r->icode, r->ivalue);
    }
    else
    {
	show_code (r);
	if (b->copy)
	{
	    if (send (b->s, r, sizeof (struct event_record), 0) == -1)
	    {
		perror ("send");
		exit (1);
	    }

	}
    }
}

void set_domain (int s, int d)
{
    struct event_record e;

    e.magic = MAGIC;
    e.itype = 7;
    e.icode = 0x1;
    e.ivalue = d;

    if (send (s, &e, sizeof (struct event_record), 0) == -1)
    {
	perror ("send");
	exit (1);
    }
}



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

void send_string (int s, char *str)
{
    struct event_record e;
    int key;
    int i;
    int v;
    e.magic = MAGIC;
    e.itype = EV_KEY;

    for (i = 0; (i < 255) && (str[i] != 0); i++)
	for (key = 0; key < sizeof (keys); key++)
	    if (str[i] == keys[key])
	    {
		e.icode = key;
		for (v = 1; v >= 0; v--)
		{
		    e.ivalue = v;
		    if (send (s, &e, sizeof (struct event_record), 0) == -1)
		    {
			perror ("send");
			exit (1);
		    }
		}
		break;
	    }
}

void send_event (int s, int v)
{
    struct event_record e;

    e.magic = MAGIC;
    e.itype = 5;
    e.icode = v;
    e.ivalue = 1;

    if (send (s, &e, sizeof (struct event_record), 0) == -1)
    {
	perror ("send");
	exit (1);
    }


}

#define NUM_COMMANDS 9
const char commands[NUM_COMMANDS][8] = { "exit", "send", "blow", "suck", "copy", "stop", "block", "unblock","help" };

enum command_ids
{
    eExit = 0,
    eSend,
    eBlow,
    eSuck,
    eCopy,
    eStop,
    eBlock,
    eUnblock,
    eHelp,
    eMax
};

#define eNone eMax

struct event recv_event;
struct event recv_stdin;

static void stop ()
{
    event_del (&recv_event);
    event_del (&recv_stdin);
}

void getcommand (char *str, int len, char **com, char **arg)
{
    int i;
    int leading = 1;

    *arg = NULL;
    *com = str;

    str[len] = '\0';

    if (len == 0)
	return;
    if (str[len - 1] == '\n')
    {
	len--;
	str[len] = '\0';
    }

    for (i = 0; i < len; i++)
    {
	if (str[i] == ' ')
	{
	    if (leading)
	    {
		*com = &str[i + 1];
	    }
	    else
	    {
		str[i] = '\0';
		*arg = &str[i + 1];
		break;
	    }

	}
	else
	    leading = 0;
    }


}

enum command_ids conv_com (char *str)
{
    enum command_ids com;
    printf ("%s: ", str);

    for (com = eExit; com < eMax; com++)
	if (strcmp (str, commands[com]) == 0)
	    break;
    return com;
}

void do_command (enum command_ids com, char *arg, int s)
{

    switch (com)
    {
    case eSend:

	if (!arg)
	    printf ("Error - Bad Input!");
	else
	{
	    send_string (s, arg);
	    printf ("By your command.");
	}
	break;

    case eBlow:
	{
	    int d = strtol (arg, NULL, 0);

	    if (errno)
		printf ("Error - Bad domain number.");
	    else
	    {
		set_domain (s, d);
		printf ("Right you are!");
	    }

	}
	break;

    case eSuck:
	{
	    int d = strtol (arg, NULL, 0);

	    if (errno)
		printf ("Error - Bad domain number.");
	    else
	    {
		printf ("Puckering up!");
		suck (s, d);
	    }

	}
	break;
    case eCopy:
	buffers.copy = 1;
	printf ("will do!");
	break;
    case eStop:
	buffers.copy = 0;
	printf ("ok");
	break;
    case eExit:
	stop ();
	break;
    case eBlock:
	if (!buffers.block)
	{
	    buffers.block = 1;
	    printf ("That'll be popular!");
	    event_del (&recv_event);
	}
	else
	    printf ("We're already doing that.");
	break;
    case eUnblock:
	if (buffers.block)
	{
	    buffers.block = 0;
	    printf ("Stand back!");
	    event_add (&recv_event, NULL);
	}
	else
	    printf ("We wern't blocked!");
	break;
    case eHelp:
        printf("Command available:\n suck <d>\t- Requests all events for domain <d>\n blow <d>\t- Sends events to domain <d>\n copy\t\t- All input should be copied to output\n stop\t\t- Don't copy\n send <text>\t- Send text as keypresses. Only lowercase for alpha keys, M = enter, B = backspace\n block\t\t- Disable reading - block the socket.\n unblock\t- Re-enable reading\n help\t\t- This text.\n exit\t\t- Exit this client.\n");
    break;
    default:
	printf ("Hu?");
    }
    printf ("\n>\n");

}

static void stdin_callback (int fd, short event, void *opaque)
{

    int *s = (int *) opaque;

    char str[100];
    char *command;
    char *arg;
    enum command_ids com;

    int n;
    n = read (fd, &str, sizeof (str), 0);

    if (n > 0)
    {
	getcommand (str, n, &command, &arg);
	com = conv_com (command);

	do_command (com, arg, *s);

    }
    else if (n)
    {
	printf ("Error %d\n", n);
	exit (1);
    }
    else
	stop ();
}


int main (void)
{
    int s, t, len;
    struct sockaddr_un remote;
    char str[100];

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

    event_init ();

    event_set (&recv_event, s, EV_READ | EV_PERSIST, recv_callback, NULL);
    event_add (&recv_event, NULL);

    event_set (&recv_stdin, 0, EV_READ | EV_PERSIST, stdin_callback, &s);
    event_add (&recv_stdin, NULL);

    event_dispatch ();

    close (s);

    return 0;
}
