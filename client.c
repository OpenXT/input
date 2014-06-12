/*
 * Copyright (c) 2010 Citrix Systems, Inc.
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


#define SOCK_PATH "/tmp/input.socket"

static int domid = -1;
static int opt = 0;
static int slot = -1;
static int s;

# define DEBUG(_format_, args...)	\
    fprintf(stderr, "%s:%d(%d-%d) " _format_, __FILE__, __LINE__, domid, slot, ## args)

#define SWITCHER_NONE           'N'
#define SWITCHER_DONE           'D'
#define SWITCHER_ENTER          'E'
#define SWITCHER_LEAVE          'L'
#define SWITCHER_DOMID          'I'
#define SWITCHER_SLOT           'S'
#define SWITCHER_OPT            'O'

void my_send(const char *str)
{
        send(s, str, strlen(str), 0);
}

void random_init(void)
{
    FILE        *fd;
    char        buff[128];

    fd = fopen("/proc/sys/kernel/random/uuid", "r");
    if (!fd)
        return;
    fgets(buff, 128, fd);
    srand(strtol(buff, NULL, 16));
    fclose(fd);
}

int main(int argc, const char *argv[])
{
        int len;
        struct sockaddr_un remote;
        struct input_event *e;
        char   buff[128];
        char    *p = NULL;
        int read_sz;
        int left_over = 0;

        argc = argc;
        slot = strtol(argv[1], NULL, 10);
        domid = strtol(argv[2], NULL, 10);
        opt = strtol(argv[3], NULL, 10);

        if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
                perror("socket");
                exit(1);
        }

        DEBUG("Trying to connect slot=%d,domid=%d,opt=%d\n", slot, domid, opt);

        remote.sun_family = AF_UNIX;
        strcpy(remote.sun_path, SOCK_PATH);
        len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        if (connect(s, (struct sockaddr *)&remote, len) == -1) {
                perror("connect");
                exit(1);
        }

        DEBUG("Conected\n");
        sprintf(buff, "I%d", domid);
        my_send(buff);
        sprintf(buff, "O%d", opt);
        my_send(buff);
        
        p = buff;
        read_sz = 0;
        left_over = 0;
        while (1)
        {
            memmove(buff, p, read_sz - (p - buff));
            if ((read_sz = recv(s, buff + left_over, 128, 0)) <= 0)
                return 1;
            read_sz += left_over;
            left_over = 0;
            p = buff;
            while (left_over == 0 && p - buff < read_sz)
            {
                if (((p - buff) + (int)sizeof (struct input_event)) >= read_sz)
                {
                    left_over = read_sz - (p - buff);
                    break;
                }
                e = (struct input_event *)(p);
                fprintf(stderr, ".");
                fflush(stderr);
                p += sizeof (struct input_event);
            }
        }
        close(s);
        return 0;
}

