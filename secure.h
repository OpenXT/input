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

#ifndef SECURE_H
#define SECURE_H

/* Authentication flags, which accompany the status */

#define AUTH_FLAG_NONE                                  0

/* status is an error that needs reporting */
#define AUTH_FLAG_ERROR					(1 << 0)

/* user entered wrong password; note that remote users can have this flag set
   if recovery can't be attempted (e.g. the Transmitter is offline) and the
   user isn't yet created locally
   NOTE: this is set in conjunction with the AUTH_NEED_CREDENTIALS status */
#define AUTH_FLAG_LOCAL_PASSWORD_MISMATCH		(1 << 1)

/* NTLM to the Transmitter failed - either invalid user or password (or both) */
#define AUTH_FLAG_REMOTE_USER_OR_PASSWORD_MISMATCH	(1 << 2)

/* key recovery completed successfully */
#define AUTH_FLAG_RECOVERED				(1 << 3)

/* authentication was started automatically by the backend daemon when the
   network came up */
#define AUTH_FLAG_AUTO_STARTED				(1 << 4)

/* NOTE: flags from here indicate failure to talk to the Transmitter */

/* for remote auth, Transmitter is offline so remote auth wasn't attempted */
#define AUTH_FLAG_OFFLINE				(1 << 5)

/* HTTP error code other than 200 or 401 was returned by Transmitter */
#define AUTH_FLAG_HTTP_ERROR				(1 << 6)

/* Couldn't talk to the Transmitter despite network being up */
#define AUTH_FLAG_NETWORK_ERROR				(1 << 7)

/* Internal error in the Transmitter */
#define AUTH_FLAG_REMOTE_INTERNAL_ERROR			(1 << 8)

/* Local authentication was started (set in conjunction with AUTH_IN_PROGRESS status) */
#define AUTH_FLAG_LOCAL_STARTED				(1 << 9)

/* Remote authentication was started (set in conjunction with AUTH_IN_PROGRESS status) */
#define AUTH_FLAG_REMOTE_STARTED			(1 << 10)

/* Not registered with Transmitter so couldn't to remote auth */
#define AUTH_FLAG_NOT_REGISTERED			(1 << 11)

/* Local hash file was missing */
#define AUTH_FLAG_LOCAL_CREDENTIALS_MISSING		(1 << 12)

/* Remote hash file was missing */
#define AUTH_FLAG_REMOTE_CREDENTIALS_MISSING		(1 << 13)

/* User not logged in so can't do recovery */
#define AUTH_FLAG_NOT_LOGGED_IN				(1 << 14)

/* Can't do recovery because different user logged in */
#define AUTH_FLAG_USERID_MISMATCH			(1 << 15)

/* Remote user's password has expired */
#define AUTH_FLAG_REMOTE_PASSWORD_EXPIRED		(1 << 16)

/* Remote user's account is locked */
#define AUTH_FLAG_REMOTE_ACCOUNT_LOCKED			(1 << 17)

/* Remote user's account has been disabled */
#define AUTH_FLAG_REMOTE_ACCOUNT_DISABLED		(1 << 18)

/* Flags used by the UI and input daemon to control the
   authentication event */

#define AUTH_FLAG_LOCK                                  (1 << 0)
#define AUTH_FLAG_CONFIRM_PW                            (1 << 1)
#define AUTH_FLAG_SET_LOCAL_PW                          (1 << 2)
#define AUTH_FLAG_REMOTE_USER                           (1 << 3)
#define AUTH_FLAG_CANNOT_CANCEL                         (1 << 4)
#define AUTH_FLAG_SET_ROOT_PW                           (1 << 5)
#define AUTH_FLAG_CHANGE_LOCAL_PW                       (1 << 6)
#define AUTH_FLAG_USER_HASH                             (1 << 7) /* when we only have a hash and no username in context */

/* Authentication statuses. They are signalled and available via an RPC. */

/* whether the status is a reportable error */
#define IS_NOT_ERROR			0
#define IS_ERROR			1

/* whether the auth window should be hidden for this status */
#define DONT_HIDE_WINDOW		0
#define HIDE_WINDOW			1

/* authentication failed due to an internal error (i.e. something went wrong in XenClient) */
#define AUTH_INTERNAL_ERROR		"internal_error", IS_ERROR, DONT_HIDE_WINDOW

/* changing password failed because no password was given */
#define AUTH_NEED_PASSWORD              "need_password", IS_ERROR, DONT_HIDE_WINDOW

/* user doesn't exist; note that remote users can have this as their 
   auth status if recovery can't be attempted (e.g. the Transmitter is offline)
   and the user isn't yet created locally */
#define AUTH_NOT_EXIST			"not_exist", IS_ERROR, DONT_HIDE_WINDOW

/* recovery failed (recovery key was invalid) */
#define AUTH_RECOVERY_KEY_INVALID	"recovery_key_invalid", IS_ERROR, DONT_HIDE_WINDOW

/* Couldn't get userid for the user. If we couldn't get to the Transmitter and
   there is no username->userid mapping for the user, this error is returned. */ 
#define AUTH_NO_USERID			"no_userid", IS_ERROR, DONT_HIDE_WINDOW

/* User isn't the owner of the device (and wasn't logged on) */
#define AUTH_NOT_DEVICE_OWNER		"not_device_owner", IS_ERROR, DONT_HIDE_WINDOW

/* authentication succeeded */
#define AUTH_OK				"ok", IS_NOT_ERROR, HIDE_WINDOW

/* user cancelled out of authentication */
#define AUTH_USER_CANCEL		"user_cancel", IS_NOT_ERROR, HIDE_WINDOW
#define AUTH_USER_CANCEL_DONT_HIDE	"user_cancel", IS_NOT_ERROR, DONT_HIDE_WINDOW

/* authentication has started and is in progress (flags specify local or remote) */
#define AUTH_IN_PROGRESS		"in_progress", IS_NOT_ERROR, DONT_HIDE_WINDOW

/* authentication failed because password mismatch */
#define AUTH_CONFIRM_FAILED             "confirm_failed", IS_ERROR, DONT_HIDE_WINDOW

/* hash file wasn't found - user should be told they need to re-authenticate */
#define AUTH_NEED_CREDENTIALS		"need_credentials", IS_NOT_ERROR, DONT_HIDE_WINDOW


/* Transmitter can't find the recovery key for the user and device pair */
#define AUTH_NO_RECOVERY_KEY		"no_recovery_key", IS_ERROR, DONT_HIDE_WINDOW

/* The ID of synchroniser does not match the cached one */
#define AUTH_SYNCHRONIZER_UID_MISMATCH  "synchronizer_uid_mismatch", IS_ERROR, DONT_HIDE_WINDOW
/* The registration PIN is not valid */
#define AUTH_INVALID_REGISTRATION_PIN   "invalid_registration_pin", IS_ERROR, DONT_HIDE_WINDOW
/* Trying to do double registration */
#define AUTH_ALREADY_REGISTERED         "already_registered", IS_ERROR, DONT_HIDE_WINDOW

/* Reregistration was required and succeeded but had no effect
   (Synchronizer still asking for reregistration) */
#define AUTH_DEVICE_CERT_RENEW_NO_EFFECT "device_cert_renew_no_effect", IS_ERROR, DONT_HIDE_WINDOW

/* CA cert of Synchronizer is untrusted */
#define AUTH_SSL_CACERT_ERROR "ssl_cacert_error", IS_ERROR, DONT_HIDE_WINDOW

/* authentication statuses sent by the backend daemon */
#define REMOTE_AUTH_SYNCHRONIZER_UID_MISMATCH   -11
#define REMOTE_AUTH_ALREADY_REGISTERED          -10
#define REMOTE_AUTH_INVALID_REGISTRATION_PIN    -9
#define REMOTE_AUTH_USERID_MISMATCH		-8
#define REMOTE_AUTH_NOT_LOGGED_IN		-7
#define REMOTE_AUTH_NOT_DEVICE_OWNER		-6
#define REMOTE_AUTH_NOT_REGISTERED		-5
#define REMOTE_AUTH_HTTP_ERROR			-4
#define REMOTE_AUTH_NETWORK_ERROR		-3
#define REMOTE_AUTH_OFFLINE			-2
#define REMOTE_AUTH_INTERNAL_ERROR		-1
#define REMOTE_AUTH_IN_PROGRESS			0
#define REMOTE_AUTH_OK				1
#define REMOTE_AUTH_BAD_USER_OR_PASSWORD	2
#define REMOTE_AUTH_NEED_CREDENTIALS		3
#define REMOTE_AUTH_NO_RECOVERY_KEY		4
#define REMOTE_AUTH_PASSWORD_EXPIRED		5
#define REMOTE_AUTH_ACCOUNT_LOCKED		6
#define REMOTE_AUTH_ACCOUNT_DISABLED		7
#define REMOTE_AUTH_DEVICE_CERT_RENEW_NO_EFFECT	8
#define REMOTE_AUTH_SSL_CACERT_ERROR		9

#define PLATFORM_USERNAME                       "/platform/username"
#define PLATFORM_FLAGS                          "/platform/flags"
#define PLATFORM_AUTH_ON_BOOT                   "/platform/auth_on_boot"
#define PLATFORM_LOCK_TIMEOUT                   "/platform/lock_timeout"

struct auth_context_t {
    char user[64]; /* user ID */
    char title[128]; /* auth session title to show */
    uint32_t flags;
};

#if 0
/* can return null if no context */
struct auth_context_t *auth_get_context();

/* set the context, wipes previous one */
void auth_set_context(const char *user, const char *title, uint32_t flags);

/* begin authentication if auth context is set and not already in progress. shows midori screen.
 * return 1 if started, 0 if not */
int auth_begin();

/* password has been collected, kick off authentication */
void auth_end(const char *user, const char *password, const char *password_confirm, const char *password_old);

/* start remote login  */
void auth_remote_login(const char *user, const char *password);

/* called when backend daemon has a status update for remote auth */
void auth_remote_status(int auto_started, int32_t status, const char *id, const char *username, char *recovery_key_file, uint32_t ctx_flags);

/* emit signal with authentication status for remote and local auth */
void auth_status(const char *status, int is_error, int hide_window, int32_t flags);

/* retrieve latest authentation status */

void auth_get_status(const char **p_status, int32_t *p_flags);

/* clear authentication status */

void auth_clear_status();

/* get the hashed form of username */
void hash_local_user (char *dstbuf, const char *username);
void hash_remote_user(char *dstbuf, const char *username);
#endif

#endif
