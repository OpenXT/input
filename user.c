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
#include "user.h"

int user_create( const char *hash, const char *name, const char *password_file, const char *recovery_file )
{
    int error = sec_new_user( hash, password_file, recovery_file );
    if (error) {
        return error;
    }
    return user_assoc( hash, name );
}

int user_assoc( const char *hash, const char *name )
{
    char path[256] = "/platform/username-map/";
    strncat( path, hash, sizeof(path)-strlen(path) );
    db_write( path, name );
    return 0;
}

int user_get_name( const char *hash, char *name )
{
    char path[256] = "/platform/username-map/";
    strncat( path, hash, sizeof(path)-strlen(path) );
    if (!db_read( name, NAME_LEN, path)) {
        return -1;
    }
    return 0;
}
