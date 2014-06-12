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

#define MAGIC 0xAD9CBCE9

#define EVENT_SIZE sizeof(struct event_record)

#define EV_VM   0x7
#define VM_SEND_TO      0x1
#define VM_TAKE_FROM    0x2
#define VM_ERROR        0x3
struct sock_plugin buffers;

static struct event server_accept_event;
static int server_sock_fd;

static void process_event(struct event_record* r, struct sock_plugin* b);
void send_event(struct sock_plugin* plug, int t, int c, int v);
void kill_connection(struct sock_plugin* buf);
int send_to_plugin(struct sock_plugin* plug,struct event_record* e);

struct event_record* findnext(struct sock_plugin* b)
{
struct event_record* r =  NULL;
int start=b->position;
// Skip junk
while (b->bytes_remaining >= EVENT_SIZE &&
      (r = (struct event_record*) &b->buffer[b->position]) &&
       r->magic != MAGIC)
   {
   b->bytes_remaining--;
   b->position++;
   }

if (start!=b->position)
    info("Warning: Encountered %d bytes of junk.\n", b->position - start);

if (b->bytes_remaining >= EVENT_SIZE)
    {
	b->bytes_remaining-= EVENT_SIZE;
	b->position+=EVENT_SIZE;
	return r;
    }
    else
       return NULL;
}

struct event server_recv_event;

static void wrapper_server_recv(int fd, short event, void *opaque)
{
int n;
struct sock_plugin* buf = (struct sock_plugin*) opaque;
char *b = buf->buffer;

memmove(b, &b[buf->position] , buf->bytes_remaining);
buf->position=0;
do
{
    n=recv(fd, &b[buf->bytes_remaining], buffersize- buf->bytes_remaining,0);
} 
while ((n<0) && (errno==EINTR));

if (n>0)
	{
	struct event_record* r =  NULL;
	buf->bytes_remaining+=n;

	while ((r=findnext(buf))!=NULL)
           {
           process_event(r,buf);
           }
	}
else if (n)
{
    info("Recv failed with %s",strerror(errno));
    kill_connection(buf);
}
else
    {
    kill_connection(buf);
    info("Client disconnected");
    }
}

void kill_connection(struct sock_plugin* buf)
{
    event_del(&server_recv_event);
    close(buf->s);

    if (buf->src)
        {
        buf->src->plugin=NULL;
        buf->src=NULL;
        }
    buf->s=-1;
    free(buf->dev_events);
}

#define E_BADDOMAIN 0x1
#define E_BADCODE   0x2

#define NONE    -1

static void send_error(struct sock_plugin* plug, uint8_t code, uint8_t type, uint8_t domain)
{
struct event_record e;

e.magic= MAGIC;
e.itype=EV_VM;
e.icode=VM_ERROR;
e.ivalue= code << (3*8) | type << (2*8) | domain << (1*8);

if (!send_to_plugin(plug ,&e))
    {
    if (!plug->error)
        plug->error=e.ivalue;
    }
}

static void send_slot(struct sock_plugin* plug, int slot)
{
    struct event_record er;
    er.magic= MAGIC;

    if (plug->dropped)
        {
        er.itype=EV_SYN;
        er.icode=SYN_DROPPED;
        er.ivalue=0;

        plug->dropped = ! send_to_plugin(plug, &er);
        if (plug->dropped)              
            {
            plug->slot_dropped=true;
            return;
            }
        }

    er.magic= MAGIC;
    er.itype=EV_DEV;
    er.icode=DEV_SET;
    er.ivalue=slot;

    if (send_to_plugin(plug, &er))
        {
        plug->slot=slot;

        /* If we prevously droppend the slot number, then we know we also dropped the first packet from that slot,
           hence we should set dropped.  If not, then we've already dealt with dropped, so it should be false. */
        plug->dropped = plug->slot_dropped;
        plug->slot_dropped=false;
        }
    else
        plug->slot_dropped=true;
}

// called from input.c, when devices are added or removed.

void add_queue_dev_event_conf(struct sock_plugin* plug, int slot)
{
struct dev_event* dev_events = plug->dev_events;

int i;
int size = plug->deq_size;
for (i=0; i<size; i++)
   
        if (dev_events[i].slot==slot)
            {            
            dev_events[i].add=true;
            return;
            }

   dev_events = realloc(dev_events, (size+1)*sizeof(struct dev_event));
   dev_events[size].slot=slot;
   dev_events[size].remove=0;
   dev_events[size].add=1;

   plug->deq_size=size+1;
   plug->dev_events=dev_events;
}

static void add_queue_dev_event_reset(struct sock_plugin* plug, int slot)
{
int size = plug->deq_size;
int i;
struct dev_event* dv;

if (slot==0xFF)
    {
    plug->resetall=true;
    return;
    }

for (i=0; i < size; i++)
        {
        dv = &plug->dev_events[i];

        if (dv->slot==slot)
            { 

            if ( dv->add)
                {
                dv->add =false;
                return;
                }
            dv->remove=true;            
            return;
            }
        }
   dv = realloc(plug->dev_events, (size+1)*sizeof(struct dev_event));
   dv[size].slot=slot;
   dv[size].remove=1;
   dv[size].add=0;
   plug->dev_events=dv;
   plug->deq_size=size+1;
}

static int flush_queued_dev_events(struct sock_plugin* plug)
{

int size = plug->deq_size;
int i;
struct event_record er;

er.magic= MAGIC;
er.itype=EV_DEV;


struct dev_event* dv;
for (i=size-1; i>-1; i--)
        {
        dv = &plug->dev_events[i];
        er.ivalue=dv->slot;
        
        if (dv->remove)
            {
            er.icode=DEV_RESET;
            if(!send_to_plugin(plug, &er))
                return false;
            dv->remove=false;
            }
        if (dv->add)
            {
            er.icode=DEV_CONF;
            if(!send_to_plugin(plug, &er))
                return false;
            dv->add=false;
            }
        plug->deq_size--;
        plug->dev_events=realloc(plug->dev_events, plug->deq_size *sizeof(struct dev_event));    
        }
return true;
}


void send_plugin_dev_event(struct sock_plugin* plug, int code, int value)
{
struct event_record er;

er.magic= MAGIC;
er.itype=EV_DEV;
bool problem=false;

if (plug->resetall)
    {
    er.icode=DEV_RESET;
    er.ivalue=0xFF;
    }
else
    {
    er.icode=code;
    er.ivalue=value;
    }

if ((code==DEV_RESET) && (value==0xFF))
    {
    plug->deq_size=0;
    free(plug->dev_events);
    plug->dev_events=NULL;
    }
else if (plug->deq_size)
    {
    problem = !flush_queued_dev_events(plug);
    }

if (problem || !send_to_plugin(plug, &er))
    {
    switch (code)
        {
        case DEV_CONF: add_queue_dev_event_conf(plug,value);
        break;
        case DEV_RESET: add_queue_dev_event_reset(plug,value);
        break;
        }
    }
else if (plug->resetall)
    {  // If resetall, we just transmitted a reset_all event, so now need to
       // transmit the original code we where invoked to send.
    plug->resetall=false;
    send_plugin_dev_event(plug,code,value);
    }
}


// send_delayed_to_plugin - calledfrom send_to_plugin
// sends remaining of partial sends.

static int send_delayed_to_plugin(struct sock_plugin* plug)
{
int r;
do
    {
    r = send(plug->s, plug->partialBuffer, plug->delayed, MSG_NOSIGNAL | MSG_DONTWAIT);

    if (r>0)
        {
        plug->delayed-=r;
        if (plug->delayed)
            memmove(&plug->partialBuffer, ((char*) ( &plug->partialBuffer)) + r, plug->delayed);
        }
    }
while ( ((r==-1) && (errno==EINTR)) || ((r>0) && (plug->delayed)) );


if (r==-1) 
{
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
        {
        info(strerror(errno));
        kill_connection(plug);
        }
    return false;
}

return true;
}

// sends raw event to plugin.  Partial sends are delt with.

int send_to_plugin(struct sock_plugin* plug,struct event_record* e)
{
int r;
if (plug->delayed)
    {
    r = send_delayed_to_plugin(plug);
    if (!r)
        {
        return false;
        }
    }
do
    {
    r = send(plug->s, e, sizeof(struct event_record), MSG_NOSIGNAL | MSG_DONTWAIT);
    }
while ((r==-1) && (errno==EINTR));

if (r==-1)
    {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        {
         // PACKET LOST;
        return false;
        }
     else
         {
         info(strerror(errno));
         kill_connection(plug);
         }
    } 
else if (r< (int) sizeof(struct event_record)) // partial send
    {
    plug->delayed=sizeof(struct event_record) - r;
    memcpy(&plug->partialBuffer, ((char*)e) + r , plug->delayed);
    send_delayed_to_plugin(plug);
    }
    return true;
}

// sends an event, on the given slot, to the plugin

void send_plugin_event(struct domain *d,int slot, struct input_event *e)
{
struct sock_plugin* plug=d->plugin;
struct event_record er;

er.magic= MAGIC;

if (plug->error)
    {
    er.itype=EV_VM;
    er.icode=VM_ERROR;
    er.ivalue=plug->error;
    if (!send_to_plugin(plug, &er))
        goto cantsend;    
    plug->error=0;
    }

if (plug->resetall)
    {
    er.itype=EV_DEV;
    er.icode=DEV_RESET;
    er.ivalue=0xFF;
    if (!send_to_plugin(plug, &er))
        goto cantsend;
    plug->resetall=false;
    }
   
if (plug->deq_size)
    if (!flush_queued_dev_events(plug))
        goto cantsend;

if (plug->slot!=slot)
    send_slot(plug, slot);

if (plug->dropped)
    {
        er.itype=EV_SYN;
        er.icode=SYN_DROPPED;
        er.ivalue=0;

        plug->dropped = ! send_to_plugin(plug, &er);
        if (plug->dropped)
            return;
    }

er.itype=e->type;
er.icode=e->code;
er.ivalue=e->value;


if (!send_to_plugin(plug, &er))
    {
    plug->dropped=true;
    }
return;

cantsend:

    if (plug->slot!=slot)
        plug->slot_dropped=true;
    else
        plug->dropped=true;
    return;
}

static void process_event(struct event_record* r, struct sock_plugin* plug)
{
  if (r->itype == EV_VM)
    {
    struct domain* d;
    switch(r->icode)
    {
    case VM_SEND_TO:
        if ( !( plug->dest=domain_with_domid(r->ivalue)) )
            {
            send_error(plug, E_BADDOMAIN, r->icode , r->ivalue);
            }
         else if (plug->dest->is_pv_domain)
            {
            send_error(plug, E_BADDOMAIN, r->icode , r->ivalue);
            plug->dest=NULL;
            }
        else
            { // Do we have the right dev slot?
            int devslot=plug->recived_slot;
            if ((devslot!=INPUTSLOT_INVALID) && (plug->dest->last_devslot!=devslot))
                {
                send_event(plug, EV_DEV,DEV_SET, devslot);
                plug->dest->last_devslot=devslot;
                }
            }
    break;

    case VM_TAKE_FROM:
        if (plug->src)
            plug->src->plugin=NULL;

        d=domain_with_domid(r->ivalue);
        if (!d)
           {
           send_error(plug, E_BADDOMAIN, r->icode , r->ivalue);
           }
        else
           {
                d->plugin=plug;
                plug->src=d;

                sock_plugin_sendconfig(plug);
           }       
    break;

    default:
        send_error(plug,E_BADCODE, r->icode, 0);
    }

    }
  else if (plug->dest)
        send_event(plug, r->itype, r->icode, r->ivalue);
  else
    send_error(plug, E_BADDOMAIN, 0 , 0xff); 
}

void send_event(struct sock_plugin* plug, int t, int c, int v)
{
    struct msg_dom0_input_event msg;
    struct domain* d = plug->dest;

    if ((NULL==d) || (NULL == d->client)) 
        return;

    if (t==EV_DEV)
    {  
        if(c==DEV_SET)
            {
            plug->recived_slot=v;
            d->last_devslot=v;
            }
          else // We don't want config events progated, since the VM will already know about it.
            return;
    }

    msg.type = t;
    msg.code = c;
    msg.value = v;
    dom0_input_event(d->client, &msg, sizeof(msg));
}

static void server_accept(int fd, short event, void *opaque)
  {
  int s = (int)opaque;
  int s2;
  socklen_t t;
  struct sockaddr_un remote;


  t = sizeof (remote);
  if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1)
  {
    info("Accept failed with %s",strerror(errno));
    return;
  }
  if (buffers.s==-1)
    {
      buffers.src=NULL;
      buffers.dest=NULL;
      buffers.slot=INPUTSLOT_INVALID;
      buffers.bytes_remaining=0;
      buffers.position=0;
      buffers.recived_slot=INPUTSLOT_INVALID;
      buffers.dropped=false;
      buffers.slot_dropped=false;
      buffers.error=0;

      buffers.resetall=0;
      buffers.deq_size=0;
      buffers.dev_events=NULL;
      buffers.delayed=0;
      buffers.s=s2;

      info("Accepting client for Unix domain socket.");
      event_set (&server_recv_event, s2,  EV_READ | EV_PERSIST,  wrapper_server_recv,  (void*)&buffers);
      event_add (&server_recv_event, NULL);
    }
    else
    {
    info("Second connection not allowed\n");
    close(s2);
    }
}

void socket_server_init(void)
{
    int len;
    struct sockaddr_un local;
    const char sock_path[]="/var/run/input_socket";

    info("Opening Unix domain socket in %s",sock_path);
    buffers.s=-1;

    if ((server_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        info("socket failed with %s",strerror(errno));
        return;
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, sock_path);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(server_sock_fd, (struct sockaddr *)&local, len) == -1) {
        info("bind failed with %s",strerror(errno));
        close(server_sock_fd);
        return;
    }

    if (listen(server_sock_fd, 5) == -1) {
        info("listen failed with %s",strerror(errno));
        close(server_sock_fd);
        return;
    }

   event_set (&server_accept_event, server_sock_fd, EV_READ | EV_PERSIST, server_accept, (void*)server_sock_fd);
   event_add (&server_accept_event, NULL);
}

void socket_server_close()
   {
   close(server_sock_fd);
   free(buffers.dev_events);
   }

