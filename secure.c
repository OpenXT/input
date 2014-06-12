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

static struct auth_context_t g_auth_context;
static int g_auth_context_present = 0;

static const char *g_auth_status = "none";
static int32_t g_auth_flags = 0;

static void name_hash(char *buf /* size NAME_LEN */, const char *prefix, const char *name)
{
    unsigned char digest[SHA_DIGEST_LENGTH];
    char *ptr;
    SHA_CTX SHA1ctxt;
    int i;

    strcpy(buf, prefix);
    ptr = &buf[strlen(buf)];

    SHA1_Init(&SHA1ctxt);
    SHA1_Update(&SHA1ctxt, name, strlen(name));
    SHA1_Final(digest, &SHA1ctxt);

    for (i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        char c = digest[i];

        sprintf(ptr, "%02hhx", c);
        ptr += 2;
    }

    ptr[0] = '\0';

    info("hash for name %s: %s", name, buf);
}

#define LOCAL_HASH_FILE(fname, user) name_hash(fname, "/mnt/secure/local-hash-", user);
#define REMOTE_HASH_FILE(fname, user) name_hash(fname, "/mnt/secure/remote-hash-", user);
#define USER_NAME(uname, prefix, user) name_hash(uname, prefix, user)

#define LOCAL_USER_PREFIX "localuser-"
#define REMOTE_USER_PREFIX "remoteuser-"

#define NT_HASH_LEN		21
#define NT_HASH_BASE_LEN	16

static size_t utf8_to_utf16le(unsigned char *dest,
                              const unsigned char *src,
                              size_t len)
{
  size_t read = 0, write = 0;
  unsigned char c, c2, c3;
  unsigned short v;

  while ((read < len) && (write < (len*2 - 1)))
  {
    c = src[read++];

    switch (c >> 4)
    {
      case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        // 0xxxxxxx
        v = c;
        break;

      case 12: case 13:
        // 110x xxxx  10xx xxxx
        if (read >= len)
        {
          return write;
        }
        c2 = src[read++];
        v = ((c & 0x1f) << 6) | (c2 & 0x3f);
        break;

      case 14:
        // 1110 xxxx  10xx xxxx  10xx xxxx
        if (read >= (len-1))
        {
          return write;
        }
        c2 = src[read++];
        c3 = src[read++];
        v = ((c & 0x0f) << 12) | ((c2 & 0x3f) << 6) | ((c3 & 0x3f) << 0);
        break;

      default:
        return write;
    }

    dest[write++] = v & 0xff;
    dest[write++] = v >> 8;
  }

  return write;
}

/*
 * Set up nt hashed passwords
 */
static int mk_nt_hash(const char *password, unsigned char *ntbuffer /* 21 bytes */)
{
  size_t len = strlen(password);
  unsigned char *pw = malloc(len*2);
  if(!pw)
    return ENOMEM;

  size_t ulen = utf8_to_utf16le(pw, (const unsigned char *)password, len);

  {
    /* Create NT hashed password. */
    MD4_CTX MD4pw;
    MD4_Init(&MD4pw);
    MD4_Update(&MD4pw, pw, ulen);
    MD4_Final(ntbuffer, &MD4pw);

    memset(ntbuffer + 16, 0, 21 - 16);
  }

  free(pw);
  return 0;
}

/*
 * Set up SHA256 hashed passwords
 */
static int mk_sha256_hash(const char *password, unsigned char *sha256buf /* SHA256_DIGEST_LENGTH bytes*/)
{
  /* Create SHA-256 hashed password. */
  SHA256_CTX SHA256pw;
  SHA256_Init(&SHA256pw);
  SHA256_Update(&SHA256pw, password, strlen(password));
  SHA256_Final(sha256buf, &SHA256pw);
  return 0;
}

void hash_local_user (char *dstbuf, const char *username)
{
    USER_NAME(dstbuf, LOCAL_USER_PREFIX, username);
}

void hash_remote_user(char *dstbuf, const char *username)
{
    USER_NAME(dstbuf, REMOTE_USER_PREFIX, username);
}

/* check if the root password is set by peeking into passwd file */
int have_root_password( int *success )
{
    errno = 0;
    struct passwd *p = getpwnam( "root" );
    if (!p) {
        if (errno) {
            perror("getpwnam");
        }
        goto error;
    }
    /* dont have password if the first character of password is '!' */
    if ( strlen(p->pw_passwd) == 0 || p->pw_passwd[0] == '!' ) {
        *success = TRUE;
        return FALSE;
    }
    *success = TRUE;
    /* have password */
    return TRUE;
error:
    warning("failed to check for existence for root password");
    *success = FALSE;
    return TRUE;
}

static int string_maybe_to_file(const char *path, const char *string)
{
    if (!string)
        return TRUE;
    FILE *f = fopen( path, "w" );
    if (!f) {
        warning("failed to open file");
        return FALSE;
    }
    size_t nbytes = strlen(string);
    if ( fwrite(string, 1, nbytes, f ) != nbytes ) {
        warning("failed to write file");
        fclose( f );
        return FALSE;
    }
    fclose( f );
    return TRUE;
}

/** Change root password using sec-change-root-credentials utility. Old password can be given or not,
 *  if not we expect root password is not already set.
 */
int change_root_password(const char *passwd, const char *old_passwd)
{
    pid_t cpid;
    int success;
    
    /* swirly thing */
    auth_status(AUTH_IN_PROGRESS, AUTH_FLAG_LOCAL_STARTED);
    /* don't allow to change root password if it is already set and no old passwd specified */
    if (!old_passwd) {
        int have_p = have_root_password( &success );
        if (!success) {
            goto error;
        } else if (have_p) {
            warning("root password already set, cannot change");
            goto error;
        }
    }
    
    /* write passwords to ramdisk */
    if ( !string_maybe_to_file( "/mnt/secure/pass-new", passwd ) )
        goto error;
    if ( !string_maybe_to_file( "/mnt/secure/pass-old", old_passwd ) )
        goto error;

    int status = sec_change_root_credentials( "/mnt/secure/pass-new", old_passwd ? "/mnt/secure/pass-old" : NULL );
    if (status != 0) {
        warning("failed to change root credentials, error code: %d", status);
        goto error;
    }

    /* not necessary because already done by script, but just in case */
    unlink( "/mnt/secure/pass-new" );
    unlink( "/mnt/secure/pass-old" );

    /* notify xenmgr to clear deferred dom0 password setting */
    com_citrix_xenclient_xenmgr_installer_progress_installstate_ (
        xcbus_conn,
        XENMGR_SERVICE,
        XENMGR_HOST_OBJPATH,
        "dom0-password-set"
    );

    info("SUCCEEDED to change root password.");
    return TRUE;

error:
    warning("FAILED to change root password.");
    /* not necessary because already done by script, but just in case */
    unlink( "/mnt/secure/pass-new" );
    unlink( "/mnt/secure/pass-old" );
    return FALSE;
}

void auth_set_context(const char *user, const char *title, uint32_t flags)
{
    strncpy(g_auth_context.user, user, sizeof(g_auth_context.user));
    strncpy(g_auth_context.title, title, sizeof(g_auth_context.title));
    g_auth_context.flags = flags;
    info("auth_set_context flags=%u\n", flags);
    g_auth_context_present = 1;
    auth_clear_status();
}

void auth_clear_context()
{
    memset(&g_auth_context, 0, sizeof(g_auth_context));
    g_auth_context_present = 0;
}

struct auth_context_t *auth_get_context()
{
    return g_auth_context_present ? &g_auth_context : NULL;
}

void auth_window(int show)
{
    struct domain *uivm = domain_uivm();
    char url_node[64];
    char state_node[64];
    char perm[10];

    sprintf(url_node, "/local/domain/%d/login/url", uivm->domid);
    sprintf(state_node, "/local/domain/%d/login/state", uivm->domid);

    if (show) {
        /* request GUI to popup midori authentication window, ho! */
        xenstore_write("http://1.0.0.0/auth.html", url_node);
        xenstore_write_int(1, state_node);
        sprintf(perm, "n%d", uivm->domid);
        xenstore_chmod (perm, 1, state_node);
    } else {
        /* close midori window */
        xenstore_write_int(0, state_node);
    }
}

int auth_window_shown()
{
    struct domain *uivm = domain_uivm();
    char state_node[64];
    char *v;
    int r = 0;

    sprintf(state_node, "/local/domain/%d/login/state", uivm->domid);

    v = xenstore_read(state_node);

    if (v)
    {
        if (strcmp(v, "3") != 0)
        {
            r = 1;
        }

        free(v);
    }

    return r;
}

int auth_begin()
{
    /* fail if no context */
    if (!auth_get_context()) {
        error("auth_begin called, but no context was set");
        return 0;
    }
    if (!domain_uivm()) {
        error("auth_begin called, but no uivm is running");
        return 0;
    }
    /* fail if authentication window already shown (otherwise we end up with multiple
       copies of midori auth running) */
    if (auth_window_shown()) {
        error("auth_begin called, but window was already running");
        return 0;
    }
    info("beginning authentication process");
    /* pop up midori window */
    auth_window(1);
    return 1;
}

int auth_write_remote_hash(const char *fname, const char *password)
{
    unsigned char hash[NT_HASH_LEN];
    FILE *f;
    int written;
    int err;

    err = mk_nt_hash(password, hash);
    if (err != 0)
        return 0;
    f = fopen(fname, "wb");
    if (!f) {
        error("failed to open remote hash file");
        return 0;
    }
    written = fwrite(hash, 1, NT_HASH_LEN, f);
    if (written != NT_HASH_LEN) {
        error("failed to write all hash bytes");
        fclose(f);
        remove(fname);
        return 0;
    }
    fclose(f);
    
    return 1;
}

int auth_write_local_hash(const char *fname, const char *password)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    FILE *f;
    int written;
    int err;

    err = mk_sha256_hash(password, hash);
    if (err != 0)
        return 0;
    f = fopen(fname, "wb");
    if (!f) {
        error("failed to open local hash file");
        return 0;
    }
    written = fwrite(hash, 1, SHA256_DIGEST_LENGTH, f);
    if (written != SHA256_DIGEST_LENGTH) {
        error("failed to write all hash bytes");
        fclose(f);
        remove(fname);
        return 0;
    }
    fclose(f);
    
    return 1;
}

static inline unsigned char hex_map(char c)
{
    switch (c)
    {
        case '0': return 0x0; case '1': return 0x1; case '2': return 0x2; case '3': return 0x3;
        case '4': return 0x4; case '5': return 0x5; case '6': return 0x6; case '7': return 0x7;
        case '8': return 0x8; case '9': return 0x9; case 'a': return 0xa; case 'b': return 0xb;
        case 'c': return 0xc; case 'd': return 0xd; case 'e': return 0xe; case 'f': return 0xf;
        default: return 0xff;
    }
}

static void auth_update_cache(const char *uid_hash,
                              const char *username,
                              int flags)
{
    char s_flags[20] = {0};
    /* Set the platform user to be this user */
    db_write(PLATFORM_USERNAME, username);
    if (flags & AUTH_FLAG_REMOTE_USER) {
        flags = AUTH_FLAG_REMOTE_USER;
    }
    snprintf(s_flags, sizeof(s_flags), "%d", flags);
    db_write(PLATFORM_FLAGS, s_flags);
    /* write tuple for username discovery based on hash */
    user_assoc( uid_hash, username );
}

static int local_auth(int remote_ok, int32_t remote_flags, const char *userprefix, const char *id, const char *username, const char *recovery_key_file, char *userpass_fname, uint32_t ctx_flags)
{
    struct stat st;
    int status;
    int got_recovery_key_file = 0;
    int recovery_started = 0;
    char uname[NAME_LEN];
    uint32_t flags = AUTH_FLAG_NONE;

    /* signal local auth is starting */
    auth_status(AUTH_IN_PROGRESS, AUTH_FLAG_LOCAL_STARTED);

    /* check we have the hash file */
    if (stat(userpass_fname, &st) != 0)
    {
        auth_status(AUTH_NEED_CREDENTIALS, remote_flags | AUTH_FLAG_LOCAL_CREDENTIALS_MISSING);
        goto cleanup_userpass;
    }

    /* check we got the user ID */
    if (!(id && id[0]))
    {
        if (remote_ok)
        {
            auth_status(AUTH_INTERNAL_ERROR, remote_flags);
        }
        else
        {

            auth_status(AUTH_NO_USERID, remote_flags);
        }

        goto cleanup_userpass;
    }

    /* check if we've got a recovery key file */
    got_recovery_key_file = recovery_key_file && recovery_key_file[0];

    /* check if the user exists */
    USER_NAME(uname, userprefix, id);
    status = sec_check_user(uname);

    if (status < 0)
    {
        goto error;
    }

    if (status != 0)
    {
        /* user doesn't exist */

        /* check if we don't have a recovery key */
        if ((ctx_flags & AUTH_FLAG_REMOTE_USER) && !got_recovery_key_file)
        {
            if (remote_ok)
            {
                /* user logged in so start recovery */
                emit_auth_remote_start_recovery(remote_flags & AUTH_FLAG_AUTO_STARTED, id, username, ctx_flags);
                /* don't remove local hash file because we'll need it when recovery has finished */
                recovery_started = 1;
                goto skip_cleanup_userpass;
            }

            /* can't do recovery */
            auth_status(AUTH_NOT_EXIST, remote_flags);
            goto cleanup_userpass;
        }

        /* create the user */
        status = user_create(uname, username, userpass_fname, recovery_key_file);

        /* if failed to create user, it's an internal error */
        if (status != 0)
        {
            error("failed to create user %s: %d", id, status);
            goto error;
        }

        /* check if we can mount user's partition */
        status = sec_check_pass_and_mount(uname, userpass_fname);

        /* if we failed to mount, it's an internal error */
        if (status != 0)
        {
            goto error;
        }

        /* Set the platform user to be this user */
        auth_update_cache(uname, username, ctx_flags);
        /* authentication succeeded - we have a local user now */
        auth_status(AUTH_OK, remote_flags);

        goto cleanup_userpass;
    }

    /* check if we can mount user's partition */
    status = sec_check_pass_and_mount(uname, userpass_fname);

    if (status < 0)
    {
        goto error;
    }

    if (status != 0)
    {
        /* can't mount user's partition */

        /* check if we don't have a recovery key */
        if (!got_recovery_key_file)
        {
            if (remote_ok)
            {
                /* user logged in so start recovery */
                emit_auth_remote_start_recovery(remote_flags & AUTH_FLAG_AUTO_STARTED, id, username, ctx_flags);
                /* don't remove local hash file because we'll need it when recovery has finished */
                recovery_started = 1;
                goto skip_cleanup_userpass;
            }

            /* can't do recovery */
            auth_status(AUTH_NEED_CREDENTIALS, remote_flags | AUTH_FLAG_LOCAL_PASSWORD_MISMATCH);
            goto cleanup_userpass;
        }

        /* set the user's password */
        status = sec_change_pass(uname, userpass_fname, recovery_key_file);

        if (status < 0)
        {
            error("failed to change password for user %s: %d", id, status);
            goto error;
        }

        if (status != 0)
        {
            /* recovery failed */
            error("failed to change password for user %s: %d", id, status);
            auth_status(AUTH_RECOVERY_KEY_INVALID, remote_flags);
            goto cleanup_userpass;
        }

        /* check again if we can mount user's partition */
        status = sec_check_pass_and_mount(uname, userpass_fname);

        /* if we still failed to mount, it's an internal error */
        if (status != 0)
        {
            goto error;
        }

        /* authentication succeeded - user's local password has been updated */
        auth_update_cache(uname, username, ctx_flags);
        auth_status(AUTH_OK, remote_flags | AUTH_FLAG_RECOVERED);
        goto cleanup_userpass;
    }

    /* local auth succeeded */
    auth_update_cache(uname, username, ctx_flags);
    auth_status(AUTH_OK, remote_flags);
    goto cleanup_userpass;

error:
    auth_status(AUTH_INTERNAL_ERROR, remote_flags);

cleanup_userpass:
    if (!(remote_flags & AUTH_FLAG_AUTO_STARTED))
    {
        /* remove local hash file auth wasn't started automatically */
        remove(userpass_fname);
    }

skip_cleanup_userpass:
    if (got_recovery_key_file && !recovery_started)
    {
        /* remove recovery key file if recovery wasn't started */
        remove(recovery_key_file);
    }

    return recovery_started;
}

void auth_remote_status(int auto_started, int32_t status, const char *id, const char *username, const char *recovery_key_file, uint32_t ctx_flags)
{
    char uname[NAME_LEN];
    char fname[NAME_LEN];
    int flags = AUTH_FLAG_NONE;
    int recovery_started = 0;

    if (auto_started)
    {
        flags |= AUTH_FLAG_AUTO_STARTED;
    }
    else
    {
        /* remove remote hash file */
        REMOTE_HASH_FILE(fname, username);
        remove(fname);
    }

    LOCAL_HASH_FILE(fname, username);

    switch (status)
    {
        case REMOTE_AUTH_USERID_MISMATCH:
            auth_status(AUTH_INTERNAL_ERROR, flags | AUTH_FLAG_USERID_MISMATCH);
            break;

        case REMOTE_AUTH_NOT_LOGGED_IN:
            auth_status(AUTH_INTERNAL_ERROR, flags | AUTH_FLAG_NOT_LOGGED_IN);
            break;

        case REMOTE_AUTH_NOT_DEVICE_OWNER:
            auth_status(AUTH_NOT_DEVICE_OWNER, flags);
            break;

        case REMOTE_AUTH_NOT_REGISTERED:
            recovery_started = local_auth(0, flags | AUTH_FLAG_NOT_REGISTERED, REMOTE_USER_PREFIX, id, username, recovery_key_file, fname, ctx_flags);
            break;

        case REMOTE_AUTH_HTTP_ERROR:
            recovery_started = local_auth(0, flags | AUTH_FLAG_HTTP_ERROR, REMOTE_USER_PREFIX, id, username, recovery_key_file, fname, ctx_flags);
            break;

        case REMOTE_AUTH_NETWORK_ERROR:
            recovery_started = local_auth(0, flags | AUTH_FLAG_NETWORK_ERROR, REMOTE_USER_PREFIX, id, username, recovery_key_file, fname, ctx_flags);
            break;

        case REMOTE_AUTH_OFFLINE:
            recovery_started = local_auth(0, flags | AUTH_FLAG_OFFLINE, REMOTE_USER_PREFIX, id, username, recovery_key_file, fname, ctx_flags);
            break;

        case REMOTE_AUTH_INTERNAL_ERROR:
            recovery_started = local_auth(0, flags | AUTH_FLAG_REMOTE_INTERNAL_ERROR, REMOTE_USER_PREFIX, id, username, recovery_key_file, fname, ctx_flags);
            break;

        case REMOTE_AUTH_IN_PROGRESS:
            auth_status(AUTH_IN_PROGRESS, flags | AUTH_FLAG_REMOTE_STARTED);
            break;

        case REMOTE_AUTH_OK:
            recovery_started = local_auth(1, flags, REMOTE_USER_PREFIX, id, username, recovery_key_file, fname, ctx_flags);
            break;

        case REMOTE_AUTH_BAD_USER_OR_PASSWORD:
            auth_status(AUTH_NEED_CREDENTIALS, flags | AUTH_FLAG_REMOTE_USER_OR_PASSWORD_MISMATCH);
            break;

        case REMOTE_AUTH_PASSWORD_EXPIRED:
            auth_status(AUTH_NEED_CREDENTIALS, flags | AUTH_FLAG_REMOTE_PASSWORD_EXPIRED);
            break;

        case REMOTE_AUTH_ACCOUNT_LOCKED:
            auth_status(AUTH_NEED_CREDENTIALS, flags | AUTH_FLAG_REMOTE_ACCOUNT_LOCKED);
            break;

        case REMOTE_AUTH_ACCOUNT_DISABLED:
            auth_status(AUTH_NEED_CREDENTIALS, flags | AUTH_FLAG_REMOTE_ACCOUNT_DISABLED);
            break;
            
        case REMOTE_AUTH_NEED_CREDENTIALS:
            auth_status(AUTH_NEED_CREDENTIALS, flags | AUTH_FLAG_REMOTE_CREDENTIALS_MISSING);
            break;

        case REMOTE_AUTH_NO_RECOVERY_KEY:
            auth_status(AUTH_NO_RECOVERY_KEY, flags);
            break;

        case REMOTE_AUTH_INVALID_REGISTRATION_PIN: auth_status( AUTH_INVALID_REGISTRATION_PIN, flags ); break;
        case REMOTE_AUTH_ALREADY_REGISTERED: auth_status( AUTH_ALREADY_REGISTERED, flags ); break;
        case REMOTE_AUTH_SYNCHRONIZER_UID_MISMATCH: auth_status( AUTH_SYNCHRONIZER_UID_MISMATCH, flags ); break;
        case REMOTE_AUTH_DEVICE_CERT_RENEW_NO_EFFECT: auth_status( AUTH_DEVICE_CERT_RENEW_NO_EFFECT, flags ); break;
        case REMOTE_AUTH_SSL_CACERT_ERROR: auth_status( AUTH_SSL_CACERT_ERROR, flags ); break;

        default:
            error("invalid remote auth status %d", status);
            break;
    }

    if (!recovery_started && !auto_started)
    {
        /* remove local hash file if local_auth didn't already */
        remove(fname);
    }

    if (recovery_key_file && recovery_key_file[0] && !recovery_started)
    {
        /* remove recovery key file if local_auth didn't already */
        remove(recovery_key_file);
    }
}

/**
 * Change 'local' user password. This shall create the 'local' user if it does not exist.
 * The recovery key will be set to be equal to the password.
 * return TRUE/FALSE depending on success.
 */
static int change_or_create_local_password(const char *user, const char *password_new, const char *password_old)
{
    char password_fname[NAME_LEN] = { 0 };
    char oldpassword_fname[NAME_LEN] = { 0 } ;
    char uname[NAME_LEN] = { 0 };
    int success = TRUE; /* lets be optimistic */

    /* signal local auth is starting */
    auth_status(AUTH_IN_PROGRESS, AUTH_FLAG_LOCAL_STARTED);
    /* write local password hash to ramdisk file */
    LOCAL_HASH_FILE( password_fname, user );
    if (!auth_write_local_hash(password_fname, password_new)) {
        error("failed to hash user %s password", user);
        auth_status(AUTH_INTERNAL_ERROR, 0);
        goto error;
    }
    /* write old password hash to ramdisk file */
    if (password_old) {
        strcpy(oldpassword_fname, password_fname);
        strcat(oldpassword_fname, "-old");
        if (!auth_write_local_hash( oldpassword_fname, password_old)) {
            error("failed to hash user %s previous password", user);
            auth_status(AUTH_INTERNAL_ERROR, 0);
            goto error;
        }
    }

    /* check if the user exists */
    USER_NAME( uname, LOCAL_USER_PREFIX, user );
    int status = sec_check_user( uname );
    if (status < 0) {
        error("failed to check for user %s existence", user);
        auth_status(AUTH_INTERNAL_ERROR, 0);
        goto error;
    }

    if (status != 0) {
        /* doesn't exist, create!
         * Use the same file for purposes of password and recovery */
        status = user_create(uname, user, password_fname, password_fname);
        if (status != 0) {
            error("failed to create local user %s, status=%d", user, status);
            auth_status(AUTH_INTERNAL_ERROR, 0);
            goto error;
        }

    } else {
        /* exists, we need to use the previous password as recovery key to change the pass */
        status = sec_change_pass(uname, password_fname, oldpassword_fname);
        if (status < 0) {
            error("failed to change password for local user %s, status=%d", user, status);
            auth_status(AUTH_INTERNAL_ERROR, 0);
            goto error;
        }
        if (status != 0) {
            error("failed to validate the recovery key for local user %s, status=%d", user, status);
            /* password mismatch between old password (recovery key) and typed one */
            auth_status(AUTH_RECOVERY_KEY_INVALID, 0);
            goto error;
        }
        /* update recovery key to match new password */
        status = sec_change_recovery(uname, password_fname, password_fname);
        if (status != 0) {
            error("failed to change recovery key for local user %s, status=%d", user, status);
            auth_status(AUTH_INTERNAL_ERROR, 0);
            goto error;
        }
    }

    /* it worked somehow, mount partition */
    status = sec_check_pass_and_mount(uname, password_fname);
    if (status != 0) {
        error("failed to mount user %s partition, status=%d!", user, status);
        auth_status(AUTH_INTERNAL_ERROR, 0);
        goto error;
    }
    /* set the platform user to be this user */
    auth_update_cache(uname, user, 0);
    auth_status( AUTH_OK, 0 );

    info("Local password has been chaged.");

    goto cleanup;

error:
    success = FALSE;

cleanup:
    remove(password_fname);
    if (password_old) {
        remove(oldpassword_fname);
    }
    return success;
}

void auth_end(const char *user, const char *password, const char *password_confirm, const char *password_old)
{
    char fname[NAME_LEN];
    int is_local = 0;
    struct auth_context_t *ctx = auth_get_context();

    if (!auth_window_shown())
    {
        goto error;
    }

    if (!ctx)
    {
        goto error;
    }

    if (ctx->flags & AUTH_FLAG_CONFIRM_PW)
    {
        if (!password_confirm || strcmp(password, password_confirm))
        {
            auth_status(AUTH_CONFIRM_FAILED, 0);
            return;
        }
    }
    
    /* DO STUFF IF WE ARE CHANGING DOM0 PASSWORD */
    if (ctx->flags & AUTH_FLAG_SET_ROOT_PW) {
        /* password needs to be nonempty */
        if (!password || strlen(password) == 0) {
            auth_status(AUTH_NEED_PASSWORD, 0);
            return;
        }
        if (!change_root_password(password, NULL)) {
            /* we somehow failed to change the root password, why!? */
            goto error;
        } else {
            auth_status(AUTH_OK, 0);
            /* bye */
            return;
        }
    }

    if (!user || strlen(user) == 0)
    {
        auth_status(AUTH_NEED_CREDENTIALS, 0);
        return;
    }

    /* DO STUFF IF WE ARE CHANGING LOCAL USER PASSWORD */
    if (ctx->flags & AUTH_FLAG_SET_LOCAL_PW || ctx->flags & AUTH_FLAG_CHANGE_LOCAL_PW) {
        if (!change_or_create_local_password(user, password, password_old)) {
            error("failed to change or create local password!");
        }
        return;
    }

    is_local = (ctx->flags & AUTH_FLAG_REMOTE_USER) == 0;
    /* write local password hash to ramdisk file */
    LOCAL_HASH_FILE(fname, user);
    if (!auth_write_local_hash(fname, password))
    {
        goto error;
    }

    if (is_local)
    {
        /* do local auth, pass in remote_ok=0 to indicate remote auth wasn't attempted */
        local_auth(0, AUTH_FLAG_NONE, LOCAL_USER_PREFIX, user, user, "", fname, ctx->flags);
    }
    else
    {
        /* write remote password hash to ramdisk file */
        REMOTE_HASH_FILE(fname, user);
        if (!auth_write_remote_hash(fname, password))
        {
            goto error;
        }

        /* signal remote auth is starting */
        auth_status(AUTH_IN_PROGRESS, AUTH_FLAG_REMOTE_STARTED);

        /* notify backend daemon that the password hash is ready */
        emit_auth_remote_start_login(user, ctx->flags);
    }

    return;

error:
    auth_status(AUTH_INTERNAL_ERROR, 0);
}

void auth_remote_login(const char *user, const char *password)
{
    char fname[NAME_LEN];
    struct auth_context_t *ctx = auth_get_context();

    if (!ctx)
    {
        goto error;
    }

    if ((ctx->flags & AUTH_FLAG_REMOTE_USER) == 0)
    {
        goto error;
    }

    /* write local password hash to ramdisk file */
    LOCAL_HASH_FILE(fname, user);
    if (!auth_write_local_hash(fname, password))
    {
        goto error;
    }

    /* write remote password hash to ramdisk file */
    REMOTE_HASH_FILE(fname, user);
    if (!auth_write_remote_hash(fname, password))
    {
         goto error;
    }

    /* signal remote auth is starting */
    auth_status(AUTH_IN_PROGRESS, AUTH_FLAG_REMOTE_STARTED);

    /* notify backend daemon that the password hash is ready */
    emit_auth_remote_start_login(user, ctx->flags);

    return;

error:
    auth_status(AUTH_INTERNAL_ERROR, 0);
}

void auth_get_status(const char **p_status, int32_t *p_flags)
{
    *p_status = g_auth_status;
    *p_flags = g_auth_flags;
}

void auth_clear_status()
{
    g_auth_status = "none";
    g_auth_flags = 0;
}

void auth_status(const char *status, int is_error, int hide_window, int32_t flags)
{
    if (hide_window)
    {
        auth_clear_context();
        auth_window(0);
        input_secure(0);
    }

    if (is_error)
    {
        flags |= AUTH_FLAG_ERROR;
    }

    g_auth_status = status;
    g_auth_flags = flags;

    info("sending auth_status %s, %d", status, flags);
    emit_auth_status(status, flags);
}
