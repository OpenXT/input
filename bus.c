/*
 * Copyright (c) 2014 Citrix Systems, Inc.
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

static DBusConnection *bus_conn = NULL;
static DBusGConnection *gbus_conn = NULL;
xcdbus_conn_t   *xcbus_conn = NULL;


static const char gI[]= "com.citrix.xenclient.input";
static const char BadUuid[]   = "BadUuid";
static const char NoMemory[]  = "NoMemory";
static const char NoSrcId[]   = "NoSrcUuid";
static const char Busy[]      = "Busy";
static const char BadFrame[]      = "BadFrame";
static const char OutOfRange[]      = "OutOfRange";
static const char FailVkbdAttach[] = "FailVkbdAttach";
static const char FailVkbdDetach[] = "FailVkbdDetach";

static const char BadUuid_txt[]   = "The UUID '%s' could not be found.";
static const char NoMemory_divert_txt[] = "Could not create divert info.";
static const char NoSrcId_txt[] = "The UUID of the caller could not be found.  (This method cannot be called from Dom0!)";
static const char FailVkbdAttach_txt[] = "Could not attach Vkbd to dom%u.";
static const char FailVkbdDetach_txt[] = "Could not detach Vkbd from dom%u.";

gboolean
input_daemon_set_slot(InputDaemonObject *this, gint IN_domid, gint IN_slot, GError** err)
{
    struct domain *d = domain_with_domid(IN_domid);
    struct domain *d_slot = domain_with_slot(IN_slot);

    if (!d)
        return FALSE;

    if (IN_slot < 0) {
	    /* no slot swapping if setting to negative */
	    domain_set_slot(d, IN_slot);
	    return TRUE;
    }

    if (d_slot)
        domain_set_slot(d_slot, d->slot);
    domain_set_slot(d, IN_slot);

    return TRUE;
}


gboolean
input_daemon_auth_set_context (InputDaemonObject *this, const char * IN_user, const char * IN_title, GError** err)
{
    auth_set_context(IN_user, IN_title, AUTH_FLAG_NONE);
    return TRUE;
}


gboolean
input_daemon_auth_set_context_flags (InputDaemonObject *this, const char * IN_user, const char * IN_title, const gint IN_flags, GError** err)
{
    auth_set_context(IN_user, IN_title, IN_flags);
    return TRUE;
}

gboolean
input_daemon_auth_begin (InputDaemonObject *this, gboolean *OUT_started, GError** err)
{
    *OUT_started = FALSE;
    switcher_auth_force();
    return TRUE;
}

gboolean
input_daemon_auth_remote_login(InputDaemonObject *this, const char* IN_username, const char* IN_password, GError** err)
{
    auth_remote_login(IN_username, IN_password);
    return TRUE;
}

gboolean
input_daemon_auth_collect_password (InputDaemonObject *this, GError** err)
{
    input_collect_password();
    return TRUE;
}

gboolean
input_daemon_auth_title (InputDaemonObject *this, char ** OUT_title, GError** err)
{
    const char *title = NULL;
    if (auth_get_context()) {
        title = auth_get_context()->title;
    }
    *OUT_title = g_strdup(title ? title : "");
    return TRUE;
}

gboolean
input_daemon_auth_get_context (InputDaemonObject *this, char ** OUT_user, char ** OUT_title, gint* OUT_flags, GError** err)
{
    const struct auth_context_t *ctx = auth_get_context();
    *OUT_user  = g_strdup(ctx ? ctx->user  : "");
    *OUT_title = g_strdup(ctx ? ctx->title : "");
    *OUT_flags = ctx ? ctx->flags : AUTH_FLAG_NONE;
    return TRUE;
}

gboolean
input_daemon_auth_remote_status (InputDaemonObject *this, const gboolean IN_auto_started, const gint IN_status, const char * IN_id, const char * IN_username, const char * IN_recovery_key_file, uint32_t IN_ctx_flags, GError** err)
{
    info("received auth_remote_status");
    info("auth_remote_status parameters: %d, %d, %s, %s, %s, %u", IN_auto_started, IN_status, IN_id, IN_username, IN_recovery_key_file, IN_ctx_flags);
    auth_remote_status(IN_auto_started, IN_status, IN_id, IN_username, IN_recovery_key_file, IN_ctx_flags);
    return TRUE;
}

gboolean
input_daemon_auth_get_status (InputDaemonObject *this, const gboolean IN_clear, char ** OUT_status, gint* OUT_flags, GError** err)
{
    const char *status;
    int32_t flags;
    auth_get_status(&status, &flags);
    *OUT_status = g_strdup(status);
    *OUT_flags  = flags;
    if (IN_clear) {
        auth_clear_status();
    }
    return TRUE;
}

gboolean
input_daemon_auth_create_hash(InputDaemonObject *this, const char *IN_fname, const char *IN_password, GError** err)
{
    auth_write_local_hash(IN_fname, IN_password);
    return TRUE;
}

gboolean
input_daemon_auth_clear_status (InputDaemonObject *this, GError** err)
{
    auth_clear_status();
    return TRUE;
}

int is_sec_mountpoint( const char *user_path )
{
    struct stat sub_stat, super_stat;
    if ( stat( user_path, &sub_stat ) < 0 ) {
        return FALSE;
    }
    if ( stat( "/config/sec", &super_stat ) ) {
        return FALSE;
    }
    return
        ( major(sub_stat.st_dev) != major(super_stat.st_dev) ) ||
        ( minor(sub_stat.st_dev) != minor(super_stat.st_dev) );
}

gboolean
input_daemon_get_user_keydir(InputDaemonObject *this, const char *IN_user, char **OUT_dir, GError** err)
{
    char local_h[256], remote_h[256];
    char local_d[256] = "/config/sec/s-", remote_d[256] = "/config/sec/s-";

    hash_remote_user(remote_h, IN_user);
    strcat( remote_d, remote_h );

    if ( is_sec_mountpoint(remote_d) ) {
        *OUT_dir = g_strdup(remote_d);
        return TRUE;
    }

    hash_local_user(local_h, IN_user);
    strcat( local_d, local_h );
    if ( is_sec_mountpoint(local_d) ) {
        *OUT_dir = g_strdup(local_d);
        return TRUE;
    }

empty:
    *OUT_dir = g_strdup("");
    return TRUE;
}

gboolean
input_daemon_get_remote_user_hash(InputDaemonObject *this, const char *IN_user, char **OUT_hash, GError** err)
{
    char remote_h[256];

    hash_remote_user(remote_h, IN_user);
    *OUT_hash = g_strdup(remote_h);
    return TRUE;
}

gboolean
input_daemon_auth_rm_platform_user(InputDaemonObject *this, gboolean *OUT_success, char* *OUT_error_msg, GError** err)
{
    /* remove platform user, if it is a local user */
    char user[256]      = { 0 };
    char user_hash[256] = { 0 };
    char s_flags[20]    = { 0 };
    int flags;

    info("attempt to remove platform user");

    db_read(user, sizeof(user), PLATFORM_USERNAME);
    db_read(s_flags, sizeof(s_flags), PLATFORM_FLAGS);

    if (strlen( user ) == 0) {
        *OUT_error_msg = g_strdup( "no platform user" );
        goto error;
    }
    /* for now we assume user is named 'local' */
    if (strcmp( user, "local" ) != 0) {
        *OUT_error_msg = g_strdup( "unexpected platform user" );
        goto error;
    }
    errno = 0;
    flags = strtol(s_flags, NULL, 10);
    if (errno != 0) {
        *OUT_error_msg = g_strdup( "failed to read user authentication flags" );
        goto error;
    }
    if (flags & AUTH_FLAG_REMOTE_USER) {
        *OUT_error_msg = g_strdup( "can only remove local users" );
        goto error;
    }

    hash_local_user( user_hash, user );

    int status = sec_rm_user( user_hash );
    if (status != 0) {
        char buf[256];
        sprintf(buf, "sec-rm-user failed with error code %d", status);
        *OUT_error_msg = g_strdup( buf );
        goto error;
    }
    db_rm("/platform");

    info("platform user REMOVED.");
success:
    *OUT_error_msg = g_strdup("");
    *OUT_success = TRUE;
    return TRUE;
error:
    *OUT_success = FALSE;
    return TRUE;
}

gboolean
input_daemon_get_focus_domid (InputDaemonObject *this, gint* OUT_domid, GError** err)
{
    *OUT_domid = switcher_get_focus();
    return TRUE;
}

gboolean
input_daemon_switch_focus (InputDaemonObject *this, gint IN_domid, gboolean IN_force, gboolean *OUT_success, GError** err)
{
    struct domain *dom;
    *OUT_success = FALSE;
    if (IN_domid == -1) {
        switcher_unfocus_gpu();
        *OUT_success = TRUE;
        return TRUE;
    }

    dom = domain_with_domid(IN_domid);
    if (!dom) {
        warning("Cannot switch to dom%u, this domid is not registered.", IN_domid);
        goto out;
    }
    if (switcher_switch(dom, 0, IN_force) < 0) {
        warning("Cannot switch to dom%u, switcher_switch() failed...", IN_domid);
        goto out;
    }
    // hooray
    *OUT_success = TRUE;
out:
    return TRUE;
}

gboolean
input_daemon_get_platform_user (InputDaemonObject *this, char ** OUT_user, gint* OUT_flags, GError **err)
{
    char user[1024] = "";
    char s_flags[20] = "";
    int flags;

    db_read(user, 1024, PLATFORM_USERNAME);
    db_read(s_flags, 20, PLATFORM_FLAGS);

    *OUT_user  = g_strdup(user);
    *OUT_flags = strtol(s_flags, NULL, 10);

    return TRUE;
}

gboolean
input_daemon_get_auth_on_boot (InputDaemonObject *this, gboolean* OUT_auth, GError** err)
{
    char tmp[1024] = "";

    db_read(tmp, 1024, PLATFORM_AUTH_ON_BOOT);

    *OUT_auth = strtol(tmp, NULL, 10);

    return TRUE;
}

gboolean
input_daemon_set_auth_on_boot (InputDaemonObject *this, const gboolean IN_auth, GError** err)
{
    char tmp[20];

    sprintf(tmp, "%d", IN_auth);
    db_write(PLATFORM_AUTH_ON_BOOT, tmp);

    return TRUE;
}

gboolean
input_daemon_touchpad_get (InputDaemonObject *this, const char * IN_prop, char ** OUT_value, GError** err)
{
    char value[64] = { 0 };

    if (!strcmp(IN_prop, "tap-to-click-enable")) {
        snprintf(value, 8, "%s", touchpad_get_tap_to_click_enabled() ? "true" : "false");
    } else if (!strcmp(IN_prop, "scrolling-enable")) {
        snprintf(value, 8, "%s", touchpad_get_scrolling_enabled() ? "true" : "false");
    } else if (!strcmp(IN_prop, "speed")) {
        snprintf(value, 8, "%d", touchpad_get_speed());
    }

    *OUT_value = g_strdup(value);

    return TRUE;
}

gboolean
input_daemon_touchpad_set (InputDaemonObject *this, const char * IN_prop, const char * IN_value, GError** err)
{
    if (!strcmp(IN_prop, "tap-to-click-enable")) {
        touchpad_set_tap_to_click_enabled( !strcmp(IN_value, "true") );
    } else if (!strcmp(IN_prop, "scrolling-enable")) {
        touchpad_set_scrolling_enabled( !strcmp(IN_value, "true") );
    } else if (!strcmp(IN_prop, "speed")) {
        touchpad_set_speed(strtol(IN_value, NULL, 10));
    }

    return TRUE;
}

gboolean
input_daemon_get_mouse_speed(InputDaemonObject *this, gint* OUT_value, GError** err)
{
    *OUT_value = input_get_mouse_speed();
    return TRUE;
}

gboolean
input_daemon_set_mouse_speed(InputDaemonObject *this, const gint IN_value, GError** err)
{
    input_set_mouse_speed(IN_value);
    return TRUE;
}

gboolean
input_daemon_lock_timeout_set (InputDaemonObject *this, const gint IN_value, GError** err)
{
    char tmp[20];
    extern int platform_lock_timeout;

    platform_lock_timeout = IN_value;
    sprintf(tmp, "%d", IN_value);
    db_write(PLATFORM_LOCK_TIMEOUT, tmp);

    return TRUE;
}

gboolean
input_daemon_lock_timeout_get (InputDaemonObject *this, gint* OUT_value, GError** err)
{
    char tmp[1024] = "";

    db_read(tmp, 1024, PLATFORM_LOCK_TIMEOUT);
    if (*tmp)
        *OUT_value = strtol(tmp, NULL, 10);
    else
        *OUT_value = 0;

    return TRUE;
}

gboolean
input_daemon_lock (InputDaemonObject *this, const gboolean IN_can_switch_out, GError** err)
{
    switcher_lock(IN_can_switch_out);
    return TRUE;
}

gboolean input_daemon_get_kb_layouts(InputDaemonObject *this, char ***OUT_layouts, GError** err)
{
    char **out = NULL;
    FILE *f = NULL;
    char line[256];
    int i = 0;

    out = calloc(KEYMAP_LIST_MAX + 1, sizeof(char *));

    f = fopen(KEYMAP_LIST_FILE, "r");
    if (!f) {
        error("failed to open %s", KEYMAP_LIST_FILE);
        goto error;
    }

    while (fgets(line, sizeof(line), f) && i < KEYMAP_LIST_MAX + 1) {
        char *p, *q;

        p = strchr(line, ':');
        if (!p) {
            error("invalid data in %s", KEYMAP_LIST_FILE);
            goto error;
        }

        *p++ = '\0';

        q = strchr(p, ':');
        if (!q) {
            error("invalid data in %s", KEYMAP_LIST_FILE);
            goto error;
        }

        *q++ = '\0';

        if (strcmp(p, "y") == 0)
            out[i++] = strdup(line);
    }

    fclose(f);

    if (i > KEYMAP_LIST_MAX) {
        error("too many keyboard layouts in %s", KEYMAP_LIST_FILE);
        goto error;
    }

    *OUT_layouts = out;
    return TRUE;

error:
    if (f)
        fclose(f);

    if (out)
        for (i = 0; i < KEYMAP_LIST_MAX + 1; i++)
            if (out[i])
                free(out[i]);

    return FALSE;
}

gboolean input_daemon_get_current_kb_layout(InputDaemonObject *this, char **OUT_layout, GError** err)
{
    *OUT_layout = get_configured_keymap( );
    if (*OUT_layout == NULL) {
        /* dbus likes nulls not */
        *OUT_layout = strdup("");
    }
    return TRUE;
}

gboolean input_daemon_set_current_kb_layout(InputDaemonObject *this, const char *IN_layout, GError** err)
{
    int status = loadkeys(IN_layout);
    if (status < 0) {
        warning("problem setting keymap, error=%d", status);
    }

    return TRUE;
}

gboolean input_daemon_update_seamless_mouse_settings(InputDaemonObject *this, const char *IN_uuid, GError** err)
{
    struct domain *d = domain_with_uuid(IN_uuid);
    if (d) {
        domain_mouse_switch_config(d);
    }
    return TRUE;
}

gboolean
input_daemon_get_idle_time (InputDaemonObject *this, gint* OUT_idleTime, GError** err)
{
    *OUT_idleTime = get_idle_time();
    return TRUE;
}

gboolean
input_daemon_get_lid_state(InputDaemonObject *this, guint* OUT_lidState, GError** err)
{
    *OUT_lidState = lid_switch_public->get_lid_state(lid_switch_public);
    return TRUE;
}

gboolean
input_daemon_get_last_input_time (InputDaemonObject *this, gint* OUT_lastInputTime, GError** err)
{
    *OUT_lastInputTime = get_last_input_time();
    return TRUE;
}

void bus_init()
{
    InputDaemonObject *server_obj = NULL;
    /* have to initialise glib type system */
    g_type_init();
    gbus_conn = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
    if (!gbus_conn) {
        info("no bus");
        exit(1);
    }
    bus_conn = dbus_g_connection_get_connection(gbus_conn);
    xcbus_conn = xcdbus_init_event(SERVICE, gbus_conn);

    if (!xcbus_conn) {
        info("failed to init dbus connection / grab service name");
        exit(1);
    }
    /* export server object */
    server_obj = input_daemon_export_dbus(gbus_conn, OBJ_PATH);
    if (!server_obj) {
        info("failed to export server object");
        exit(1);
    }

    info("initialised dbus succesfully");

    /* since we are talking to dbd, wait for it to appear on bus */
    info("waiting for com.citrix.xenclient.db service..");
    xcdbus_wait_service(xcbus_conn, "com.citrix.xenclient.db");
    info("connected");
}

void emit_secure_mode(int32_t onoff)
{
    if (!notify_com_citrix_xenclient_input_secure_mode(xcbus_conn, SERVICE, OBJ_PATH, onoff))
    {
        info("failed to signal secure_mode");
    }
}

void emit_auth_status(const char *status, int32_t flags)
{
    if (!notify_com_citrix_xenclient_input_auth_status(xcbus_conn, SERVICE, OBJ_PATH, status, flags))
    {
        info("failed to signal auth_status");
    }
}

void emit_auth_remote_start_login(const char *username, uint32_t ctx_flags)
{
    if (!notify_com_citrix_xenclient_input_auth_remote_start_login(xcbus_conn, SERVICE, OBJ_PATH, username, ctx_flags))
    {
        info("failed to signal auth_remote_start_login");
    }
}

void emit_auth_remote_start_recovery(dbus_bool_t auto_started, const char *id, const char *username, uint32_t ctx_flags)
{
    if (!notify_com_citrix_xenclient_input_auth_remote_start_recovery(xcbus_conn, SERVICE, OBJ_PATH, auto_started, id, username, ctx_flags))
    {
        info("failed to signal auth_remote_start_recovery");
    }
}

int db_rm(const char *path)
{
    return com_citrix_xenclient_db_rm_ ( xcbus_conn, DB_SERVICE, DB_PATH, path );
}

int db_exists(const char *path)
{
    gboolean value = false;

    if (!com_citrix_xenclient_db_exists_ ( xcbus_conn, DB_SERVICE, DB_PATH, path, &value ))
        return false;
    return value;
}

int db_read(char *buf, int buf_size, const char *path)
{
    char *value = NULL;
    /* set to empty string in case of fail */
    if (buf_size > 0)
        *buf = 0;
    if (!com_citrix_xenclient_db_read_ ( xcbus_conn, DB_SERVICE, DB_PATH, path, &value )) {
        return FALSE;
    }
    strncpy( buf, value, buf_size );
    g_free( value );
    return TRUE;
}

int db_write(const char *path, const char *value)
{
    return com_citrix_xenclient_db_write_ ( xcbus_conn, DB_SERVICE, DB_PATH, path, value );
}


#define MAX_MODS 20
int Modifers[MAX_MODS];


int addmod(uint32_t key, uint32_t* mods)
{
int i;
int len = mods[0];
uint32_t* m=&mods[1];

for (i=0; i<len; i++)
   {
   if (m[i]==key)
      return i;
   }
if (i<MAX_MODS)
   {
   m[len]=key;
   mods[0]=len+1;
   return i;
   }
  return -1;
}

int check_init_divert_info(struct divert_info_t** dv_in)
{
if (*dv_in==NULL)
        {
                info("creating input divert_info.\n");
                struct divert_info_t* dv = malloc(sizeof(struct divert_info_t));
                if (dv==NULL)
                {
                    info("Error: Failed to alloc!\n");
                    return -1;
                }
                dv->keylist=NULL;
                dv->key_domain=NULL;
                dv->mouse_domain=NULL;
                dv->num_keys=0;
                dv->modifers[0]=4;
                dv->modifers[1]=KEY_LEFTALT;
                dv->modifers[2]=KEY_RIGHTALT;
                dv->modifers[3]=KEY_LEFTCTRL;
                dv->modifers[4]=KEY_RIGHTCTRL;
                dv->focusmode=0;
                *dv_in=dv;
                return 0;
        }
return 0;
}

void destroy_divert_info(struct divert_info_t** dv)
{
    if (*dv != NULL)
    {
        free((*dv)->keylist);
        free(*dv);
        *dv = NULL;
    }
}

gboolean
input_daemon_divert_mouse_focus(InputDaemonObject *this, const char* IN_uuid,
                guint IN_sframe_x1, guint IN_sframe_y1, guint IN_sframe_x2, guint IN_sframe_y2,
                guint IN_dframe_x1, guint IN_dframe_y1, guint IN_dframe_x2, guint IN_dframe_y2, GError** err)
{
int32_t domid = xcdbus_get_sender_domid (xcbus_conn);
struct domain* d  = domain_with_domid(domid);

if (!d)
    {
    set_error(err,gI,NoSrcId, NoSrcId_txt);
    return false;;
    }

struct domain* td = domain_with_uuid(IN_uuid);
if (!td)
    {
    set_error(err,gI, BadUuid, BadUuid_txt, IN_uuid);
    return false;
    }

if ((IN_sframe_x1==IN_sframe_x2) || (IN_sframe_y1==IN_sframe_y2))
    {
    set_error(err,gI,BadFrame,"The source frame cannot have an area of zero.");
    return false;
    }

if ((IN_dframe_x1==IN_dframe_x2) || (IN_dframe_y1==IN_dframe_y2))
    {
    set_error(err,gI,BadFrame,"The destination frame cannot have an area of zero.");
    return false;
    }


info("Divert from %d to %d\n", d->domid, td->domid);
if (check_init_divert_info(&d->divert_info))
    {
    set_error(err,gI,NoMemory, NoMemory_divert_txt);
    return false;
    }
struct divert_info_t* dv = d->divert_info;

if (IN_sframe_x1< IN_sframe_x2)
{
dv->sframe_x1=IN_sframe_x1;
dv->sframe_x2=IN_sframe_x2;
}
else
{
dv->sframe_x1=IN_sframe_x2;
dv->sframe_x2=IN_sframe_x1;
}

if (IN_sframe_y1< IN_sframe_y2)
{
dv->sframe_y1=IN_sframe_y1;
dv->sframe_y2=IN_sframe_y2;
}
else
{
dv->sframe_y1=IN_sframe_y2;
dv->sframe_y2=IN_sframe_y1;
}

if (IN_dframe_x1<IN_dframe_x2)
{
dv->dframe_x1=IN_dframe_x1;
dv->dframe_x2=IN_dframe_x2;
}
else
{
dv->dframe_x1=IN_dframe_x2;
dv->dframe_x2=IN_dframe_x1;
}

if (IN_dframe_y1<IN_dframe_y2)
{
dv->dframe_y1=IN_dframe_y1;
dv->dframe_y2=IN_dframe_y2;
}
else
{
dv->dframe_y1=IN_dframe_y2;
dv->dframe_y2=IN_dframe_y1;
}
dv->mouse_domain=td;
sync_mouse_domain(d);
return true;
}


gboolean
input_daemon_set_divert_keyboard_filter(InputDaemonObject *this, GArray* IN_key_filter, GError** err)
{
int32_t domid = xcdbus_get_sender_domid (xcbus_conn);
struct domain* d  = domain_with_domid(domid);
if (!d)
{
    set_error(err,gI, NoSrcId, NoSrcId_txt);
	return false;
}
uint *data = (uint*) IN_key_filter->data;
uint i;
int shortcuts=0;
uint modbits=0;
uint len = IN_key_filter->len;

if (check_init_divert_info(&d->divert_info))
    {
    set_error(err,gI,NoMemory,NoMemory_divert_txt);
    return false;
    }

struct divert_info_t* dv = d->divert_info;


if (dv->key_domain!=NULL)
{
    set_error(err,gI,Busy, "Don't set filter, while filter in use!\n");
	return false;
}

if (dv->keylist!=NULL)
{
        struct keypairs* k = dv->keylist;
        dv->num_keys=0;
        dv->keylist=NULL;
        free(k);
}
dv->keylist = (struct keypairs*) calloc(len, sizeof(struct keypairs) );
dv->num_keys=0;

if (dv->keylist==NULL)
{
        set_error(err,gI,NoMemory, "Failed to create keylist!");
        return false;
}

for (i=0; i< len ; i++ )
{
   if (data[i])
   {
      if ((i+1==len) || data[i+1]==0)
      {
        // Action Key
        dv->keylist[shortcuts].mod_bits=modbits;
        dv->keylist[shortcuts].keycode=data[i];
        modbits=0;
        shortcuts++;
      } else
      {
                // Modifier
        int a = addmod(data[i],dv->modifers);
        modbits|=1<<a;
      }
   }
}
dv->num_keys=shortcuts;

info("Filter set up with %d Shortcuts\n", shortcuts);

return true;
}

gboolean
input_daemon_divert_keyboard_focus(InputDaemonObject *this, const char* IN_uuid, GError** err)
{
int32_t domid = xcdbus_get_sender_domid (xcbus_conn);
struct domain* d  = domain_with_domid(domid);
struct domain* td = domain_with_uuid(IN_uuid);
if (!d)
    {
    set_error(err,gI,NoSrcId, NoSrcId_txt);
    return false;;
    }

if (!td)
    {
    set_error(err,gI, BadUuid, BadUuid_txt, IN_uuid);
	return false;
    }

info("divert from %d to %d\n", d->domid, td->domid);
if (check_init_divert_info(&d->divert_info))
    {
    set_error(err,gI,NoMemory,NoMemory_divert_txt);
    return false;
    }


struct divert_info_t* dv = d->divert_info;

dv->key_domain = td;
sync_kbd_domain(d);

return true;
}


gboolean input_daemon_stop_mouse_divert(InputDaemonObject *this, GError** err)
{
int32_t domid = xcdbus_get_sender_domid (xcbus_conn);
struct domain* d  = domain_with_domid(domid);
if (d && d->divert_info)
    d->divert_info->mouse_domain=NULL;
sync_mouse_domain(d);
return true;
}

gboolean input_daemon_stop_keyboard_divert(InputDaemonObject *this, GError** err)
{
int32_t domid = xcdbus_get_sender_domid (xcbus_conn);
struct domain* d  = domain_with_domid(domid);
if (d && d->divert_info)
	d->divert_info->key_domain=NULL;
sync_kbd_domain(d);
return true;
}

gboolean
input_daemon_touch(InputDaemonObject *this, const char* IN_uuid, GError** err)
{
struct domain* td = domain_with_uuid(IN_uuid);
if (!td)
{
    set_error(err,gI, BadUuid, BadUuid_txt,IN_uuid );
    return false;
}
wiggle_ctrl_key(td);
return true;
}

gboolean
input_daemon_focus_mode(InputDaemonObject *this, gint IN_mode, GError** err)
{
int32_t domid = xcdbus_get_sender_domid (xcbus_conn);
struct domain* d  = domain_with_domid(domid);
if (!d)
    {
    set_error(err,gI,NoSrcId, NoSrcId_txt);
    return false;;
    }

if (check_init_divert_info(&d->divert_info))
    {
    set_error(err,gI,NoMemory,"Could not create divert info.");
    return false;
    }
uint8_t mode = (uint8_t) IN_mode;
if (mode > FOCUSMODE_MAX)
    {
    set_error(err,gI,OutOfRange,"The value should be 0 or 1.");
    return false;
    }

d->divert_info->focusmode=mode;
return true;
}

gboolean
input_daemon_property_get_numlock_restore_on_switch(InputDaemonObject *this, gboolean *OUT_value, GError **err)
{
    *OUT_value = input_get_numlock_restore_on_switch();
    return TRUE;
}

gboolean
input_daemon_property_set_numlock_restore_on_switch(InputDaemonObject *this, gboolean IN_value, GError **err)
{
    input_set_numlock_restore_on_switch(IN_value);
    return TRUE;
}

gboolean
input_daemon_attach_vkbd(InputDaemonObject *this, gint IN_domid, GError **err)
{
    struct domain *d;


    d = domain_with_domid(IN_domid);
    if (d == NULL) {
        d = domain_new(IN_domid);
        if (d == NULL)
            goto fail;
        if (domain_setup(d))
            goto fail_and_free_domain;
        if (domain_set_pvm(d, false))
            goto fail_and_free_domain;
    }
    if (domain_attach_vkbd(d))
        return FALSE;

    /* Update the slots[] global with what we just configured for this domain.
     * This will send input to the vkbd frontend with little sanity checks.
     * XXX: This is a tad convoluted.
     */
    focus_update_domain(d);

    return TRUE;

fail_and_free_domain:
    domain_release(d);
fail:
    set_error(err, gI, FailVkbdAttach, FailVkbdAttach_txt, IN_domid);
    return FALSE;
}


gboolean
input_daemon_detach_vkbd(InputDaemonObject *this, gint IN_domid, GError** err)
{
    struct domain *d;

    d = domain_with_domid(IN_domid);
    if (d == NULL) {
        warning("%s: Could not detach VKBD from dom%u. Dom%u does not exist.",
                __func__, IN_domid, IN_domid);
        set_error(err, gI, FailVkbdDetach, FailVkbdDetach_txt, IN_domid);
        return FALSE;
    }
    domain_detach_vkbd(d);

    return TRUE;
}

