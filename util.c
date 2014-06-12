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

void helper_exec(const char *bin, int domid)
{
    char                buff[12]={0}; 
    struct stat         s;
    pid_t pid;

    if (stat(bin, &s) == -1)
        return;

    snprintf(buff,sizeof(buff)-1,"%d",domid);


    switch ((pid=fork())) { //wait is mopped up by sigchld in main
        case 0:
            execl(bin,bin,buff,(char *) 0);
            error("execl failed for %s",bin);
            _exit(1);
            break;
        case -1:
            error("fork failed");
            break;
    }


    waitpid(pid,NULL,0);
}



    static void
bt (void)
{
#if 0
#if 0
    unsigned int level = 0;
    void *addr;
    Dl_info info;

    for (;;)
    {
        addr = __builtin_return_address (level);
        if (!addr)
            return;
        fprintf (stderr, "%d: %p", level, addr);
        if (dladdr (addr, &info))
        {
            char *base, *offset;

            base = info.dli_saddr;
            offset = addr;

            fprintf (stderr, "(%s %s+0x%x)", info.dli_fname, info.dli_sname,
                    (unsigned int) (offset - base));

        }

        fprintf (stderr, "\n");
        level++;
    }
#else
    void *ba[256];
    Dl_info info;
    int i;

    int n = backtrace (ba, sizeof (ba) / sizeof (ba[0]));
    if (!n)
        return;


    for (i = 0; i < n; ++i)
    {
        fprintf (stderr, "%d: %p", i, ba[i]);
        if (dladdr (ba[i], &info))
        {
            char *base, *offset;

            base = info.dli_saddr;
            offset = ba[i];

            fprintf (stderr, "(%s %s+0x%x)", info.dli_fname, info.dli_sname,
                    (unsigned int) (offset - base));

        }

        fprintf (stderr, "\n");
    }
#endif
#endif
}

    void
message (int flags, const char *file, const char *function, int line,
        const char *fmt, ...)
{
    char buf[1024]={0};
    char *level = NULL;
    va_list ap;
    int len;

    if (flags & MESSAGE_INFO)
    {
        level = "Info";
    }
    else if (flags & MESSAGE_WARNING)
    {
        level = "Warning";
    }
    else if (flags & MESSAGE_ERROR)
    {
        level = "Error";
    }
    else if (flags & MESSAGE_FATAL)
    {
        level = "Fatal";
    }




    va_start (ap, fmt);
    len=vsnprintf(buf,sizeof(buf)-1,fmt,ap);
    va_end (ap);

    if ((len>0) &&(buf[len-1]=='\n')) buf[len-1]=0;

    syslog(LOG_ERR, "%s:%s:%s:%d:%s", level, file, function, line,buf);

    if (flags & MESSAGE_FATAL)
    {
        bt ();
        abort ();
    }
}

void log_dbus_error (const char *file, const char *function, int line, const char *err, const char *fmt, ...)
{
    char buf[128]={0};
    va_list ap;
    int len;
    va_start (ap, fmt);

    len=vsnprintf(buf,sizeof(buf)-1,fmt,ap);
    va_end (ap);

    if ((len>0) &&(buf[len-1]=='\n')) buf[len-1]=0;

    syslog(LOG_ERR, "Info:%s:%s:%d threw error %s : %s", file, function, line,err,buf);
}

