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

int sec_check_pass(const char *uname, const char *userpass_fname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-check-pass";
    int status;

    info("going to call %s with %s, %s", bin, uname, userpass_fname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, userpass_fname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

int sec_mount(const char *uname, const char *userpass_fname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-mount";
    int status;

    info("going to call %s with %s, %s", bin, uname, userpass_fname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, userpass_fname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

int sec_check_pass_and_mount(const char *user, const char *userpass_fname)
{
    int status;

    /* test if password is ok */
    status = sec_check_pass(user, userpass_fname);
    if (status != 0) {
        error("check_pass for user '%s' failed: %d", user, status);
        return status;
    }

    /* try to mount key partition */
    status = sec_mount(user, userpass_fname);
    if (status != 0) {
        error("try_mount for user '%s' failed: %d", user, status);
        return status;
    }

    return 0;
}

int sec_new_user(const char *uname, const char *userpass_fname, const char *serverpass_fname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-new-user";
    int status;

    info("going to call %s with %s, %s, %s", bin, uname, userpass_fname, serverpass_fname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, userpass_fname, serverpass_fname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

int sec_rm_user(const char *uname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-rm-user";
    int status;

    info("going to call %s with %s", bin, uname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}


int sec_change_pass(const char *uname, const char *userpass_fname, const char *serverpass_fname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-change-pass";
    int status;

    info("going to call %s with %s, %s, %s", bin, uname, userpass_fname, serverpass_fname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, userpass_fname, serverpass_fname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

int sec_change_recovery(const char *uname, const char *userpass_fname, const char *serverpass_fname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-change-recovery";
    int status;

    info("going to call %s with %s, %s, %s", bin, uname, userpass_fname, serverpass_fname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, userpass_fname, serverpass_fname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

int sec_check_user(const char *uname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-check-user";
    int status;

    info("going to call %s with %s", bin, uname);

    switch (pid=fork()) {
    case 0:
        execl(bin, bin, uname, (char*)NULL);
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

/** oldpass_fname can be NULL if root password is currently not set */
int sec_change_root_credentials(const char *newpass_fname, const char *oldpass_fname)
{
    pid_t pid;
    const char *bin = "/usr/bin/sec-change-root-credentials";
    int status;

    info("going to call %s", bin);

    switch (pid=fork()) {
    case 0:
        if ( oldpass_fname != NULL ) {
            execl( bin, bin, newpass_fname, oldpass_fname, NULL );
        } else {
            execl( bin, bin, newpass_fname, NULL );
        }
        error("execl failed for %s", bin);
        _exit(1);
    case -1:
        error("fork failed");
        return -1;
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}


