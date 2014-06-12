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

#include <ctype.h>
#include "project.h"

#define NUM_MODIFIER_COLS 2

static int fd = -1;

static int modifiers[][NUM_MODIFIER_COLS] =
{
    {0, 0},
    {KEY_RIGHTSHIFT, KEY_LEFTSHIFT},
    {KEY_RIGHTALT, 0},
    {0, 0},
    {KEY_RIGHTCTRL, KEY_LEFTCTRL},
    {0, 0},
    {0, 0},
    {0, 0},
    {KEY_LEFTALT, 0},
};

static int get_bind(u_char index, u_char table)
{
	struct kbentry ke;

	ke.kb_index = index;
	ke.kb_table = table;
	if (ioctl(fd, KDGKBENT, (unsigned long)&ke))
            return 0;
	return ke.kb_value & 0xff;
}

int keycode2ascii(int keycode)
{
    int i, j;
    int table = 0;
    int value;

    if (fd == -1)
        fd = open("/dev/tty0", O_RDWR);

    for (i = 0; i <= 8; i++)
        for (j = 0; (j < NUM_MODIFIER_COLS) && (modifiers[i][j]); j++)
            if (key_status_get(modifiers[i][j]))
                table |= i;
    value = get_bind(keycode, table);
    return (isprint(value) || (value >= 128 && value <= 255)) ? value : 0;
}

/** Return current keymap from the config file (free the returned string yourself), or NULL */
char *get_configured_keymap( void )
{
    char *rval = NULL;
    FILE *f = NULL;
    char line[256] = { 0 };

    f = fopen(KEYMAP_CONF_FILE, "r");
    if (!f) goto error;
    fgets(line, sizeof(line), f);

    char *text = strstr(line, "KEYBOARD='");
    if (!text) goto error;
    char *quoteend = strstr(text + strlen("KEYBOARD='"), "'");
    if (!quoteend) goto error;

    unsigned long beg = (unsigned long)text + strlen("KEYBOARD='");
    unsigned long len = (unsigned long)quoteend - beg;

    char layout[32] = { 0 };
    strncpy( layout, (char*)beg, MIN(len, sizeof(layout)) );
    rval = strdup( layout );
    goto success;

error:
    rval = NULL;
success:
    if (f) {
        fclose(f);
    }
    return rval;
}

/**
 * For the moment only fd leak hunt is opened, the moment leak hunt is not
 * opened.
 * */
static void leak_hunt(void)
{
    int i;
    int maxfd = sysconf(_SC_OPEN_MAX);
    for (i = 3; i < maxfd; ++i) {
        close(i);
    }
}

/** Change current keymap */
int loadkeys(const char *keymap)
{
    pid_t pid;
    const char *bin = "/usr/bin/loadkeys";
    int status;
    /* invoke loadkeys */
    info("going to call %s with %s", bin, keymap);
    pid = fork();
    if (pid == 0) { /* child */
        leak_hunt();
        execl(bin, bin, keymap, NULL);
        error("execl failed for %s with error %s", bin, strerror (errno));
        exit (1);
    }
    else if (pid == -1) {
        error("failed to fork: %s", strerror (errno));
        return -1;
    }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) {
        return -1;
    }
    int exitstatus = WEXITSTATUS(status);
    if (exitstatus!=0)
        return exitstatus;

#if 0
    /* Silly fork to check that fd was close */
    pid = fork();
    if (pid == 0) { /* child */
        leak_hunt();
        while (1);
        exit(1);
    }
#endif

    /* write keymap to conf file */
    FILE *f = fopen(KEYMAP_CONF_FILE, "w");
    if (f) {
        fprintf(f, "KEYBOARD='%s'\n", keymap);
        fflush( f );
        fsync( fileno(f) );
        fclose( f );
    }


    /* write keymap to xenstore */
    xenstore_write( keymap, "/xenclient/keyboard/layout" );

    /* permission misery */
    char perm[16] = { 0 };
    snprintf(perm, sizeof(perm), "r0");
    xenstore_chmod ( perm, 1, "/xenclient/keyboard/layout" );

    /* success */
    return 0;
}

void keymap_init( )
{
    char *keys = get_configured_keymap( );
    if (keys) {
        info("configuring initial keymap: %s", keys);
        loadkeys( keys );
        free( keys );
    }
}
