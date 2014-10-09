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

/* server.c */
void server_kill_domain(struct domain *d);
int server_init(void);
/* bus.c */
xcdbus_conn_t *xcbus_conn;
gboolean input_daemon_set_slot(InputDaemonObject *this, gint IN_domid, gint IN_slot, GError **err);
gboolean input_daemon_auth_set_context(InputDaemonObject *this, const char *IN_user, const char *IN_title, GError **err);
gboolean input_daemon_auth_set_context_flags(InputDaemonObject *this, const char *IN_user, const char *IN_title, const gint IN_flags, GError **err);
gboolean input_daemon_auth_begin(InputDaemonObject *this, gboolean *OUT_started, GError **err);
gboolean input_daemon_auth_remote_login(InputDaemonObject *this, const char *IN_username, const char *IN_password, GError **err);
gboolean input_daemon_auth_collect_password(InputDaemonObject *this, GError **err);
gboolean input_daemon_auth_title(InputDaemonObject *this, char **OUT_title, GError **err);
gboolean input_daemon_auth_get_context(InputDaemonObject *this, char **OUT_user, char **OUT_title, gint *OUT_flags, GError **err);
gboolean input_daemon_auth_remote_status(InputDaemonObject *this, const gboolean IN_auto_started, const gint IN_status, const char *IN_id, const char *IN_username, const char *IN_recovery_key_file, uint32_t IN_ctx_flags, GError **err);
gboolean input_daemon_auth_get_status(InputDaemonObject *this, const gboolean IN_clear, char **OUT_status, gint *OUT_flags, GError **err);
gboolean input_daemon_auth_create_hash(InputDaemonObject *this, const char *IN_fname, const char *IN_password, GError **err);
gboolean input_daemon_auth_clear_status(InputDaemonObject *this, GError **err);
int is_sec_mountpoint(const char *user_path);
gboolean input_daemon_get_user_keydir(InputDaemonObject *this, const char *IN_user, char **OUT_dir, GError **err);
gboolean input_daemon_get_remote_user_hash(InputDaemonObject *this, const char *IN_user, char **OUT_hash, GError **err);
gboolean input_daemon_auth_rm_platform_user(InputDaemonObject *this, gboolean *OUT_success, char **OUT_error_msg, GError **err);
gboolean input_daemon_get_focus_domid(InputDaemonObject *this, gint *OUT_domid, GError **err);
gboolean input_daemon_switch_focus(InputDaemonObject *this, gint IN_domid, gboolean IN_force, gboolean *OUT_success, GError **err);
gboolean input_daemon_get_platform_user(InputDaemonObject *this, char **OUT_user, gint *OUT_flags, GError **err);
gboolean input_daemon_get_auth_on_boot(InputDaemonObject *this, gboolean *OUT_auth, GError **err);
gboolean input_daemon_set_auth_on_boot(InputDaemonObject *this, const gboolean IN_auth, GError **err);
gboolean input_daemon_touchpad_get(InputDaemonObject *this, const char *IN_prop, char **OUT_value, GError **err);
gboolean input_daemon_touchpad_set(InputDaemonObject *this, const char *IN_prop, const char *IN_value, GError **err);
gboolean input_daemon_get_mouse_speed(InputDaemonObject *this, gint* OUT_value, GError** err);
gboolean input_daemon_set_mouse_speed(InputDaemonObject *this, const gint IN_value, GError** err);
gboolean input_daemon_lock_timeout_set(InputDaemonObject *this, const gint IN_value, GError **err);
gboolean input_daemon_lock_timeout_get(InputDaemonObject *this, gint *OUT_value, GError **err);
gboolean input_daemon_lock(InputDaemonObject *this, const gboolean IN_can_switch_out, GError **err);
gboolean input_daemon_get_kb_layouts(InputDaemonObject *this, char ***OUT_layouts, GError **err);
gboolean input_daemon_get_current_kb_layout(InputDaemonObject *this, char **OUT_layout, GError **err);
gboolean input_daemon_set_current_kb_layout(InputDaemonObject *this, const char *IN_layout, GError **err);
gboolean input_daemon_update_seamless_mouse_settings(InputDaemonObject *this, const char *IN_uuid, GError **err);
gboolean input_daemon_get_idle_time(InputDaemonObject *this, gint *OUT_idleTime, GError **err);
gboolean input_daemon_get_last_input_time(InputDaemonObject *this, gint *OUT_idleTime, GError **err);
gboolean input_daemon_get_lid_state(InputDaemonObject *this, guint *OUT_lidState, GError **err);
void bus_init(void);
void emit_secure_mode(int32_t onoff);
void emit_auth_status(const char *status, int32_t flags);
void emit_auth_remote_start_login(const char *username, uint32_t ctx_flags);
void emit_auth_remote_start_recovery(dbus_bool_t auto_started, const char *id, const char *username, uint32_t ctx_flags);
int db_rm(const char *path);
int db_exists(const char *path);
int db_read(char *buf, int buf_size, const char *path);
int db_write(const char *path, const char *value);
int Modifers[20];
int addmod(uint32_t key, uint32_t *mods);
int check_init_divert_info(struct divert_info_t **dv_in);
void destroy_divert_info(struct divert_info_t **dv);
gboolean input_daemon_divert_mouse_focus(InputDaemonObject *this, const char *IN_uuid, guint IN_sframe_x1, guint IN_sframe_y1, guint IN_sframe_x2, guint IN_sframe_y2, guint IN_dframe_x1, guint IN_dframe_y1, guint IN_dframe_x2, guint IN_dframe_y2, GError **err);
gboolean input_daemon_set_divert_keyboard_filter(InputDaemonObject *this, GArray *IN_key_filter, GError **err);
gboolean input_daemon_divert_keyboard_focus(InputDaemonObject *this, const char *IN_uuid, GError **err);
gboolean input_daemon_stop_mouse_divert(InputDaemonObject *this, GError **err);
gboolean input_daemon_stop_keyboard_divert(InputDaemonObject *this, GError **err);
gboolean input_daemon_touch(InputDaemonObject *this, const char *IN_uuid, GError **err);
gboolean input_daemon_focus_mode(InputDaemonObject *this, gint IN_mode, GError **err);
/* secure_scripts.c */
int sec_check_pass(const char *uname, const char *userpass_fname);
int sec_mount(const char *uname, const char *userpass_fname);
int sec_check_pass_and_mount(const char *user, const char *userpass_fname);
int sec_new_user(const char *uname, const char *userpass_fname, const char *serverpass_fname);
int sec_rm_user(const char *uname);
int sec_change_pass(const char *uname, const char *userpass_fname, const char *serverpass_fname);
int sec_change_recovery(const char *uname, const char *userpass_fname, const char *serverpass_fname);
int sec_check_user(const char *uname);
int sec_change_root_credentials(const char *newpass_fname, const char *oldpass_fname);
/* user.c */
int user_create(const char *hash, const char *name, const char *password_file, const char *recovery_file);
int user_assoc(const char *hash, const char *name);
int user_get_name(const char *hash, char *name);
/* secure.c */
void hash_local_user(char *dstbuf, const char *username);
void hash_remote_user(char *dstbuf, const char *username);
int have_root_password(int *success);
int change_root_password(const char *passwd, const char *old_passwd);
void auth_set_context(const char *user, const char *title, uint32_t flags);
void auth_clear_context(void);
struct auth_context_t *auth_get_context(void);
void auth_window(int show);
int auth_window_shown(void);
int auth_begin(void);
int auth_write_remote_hash(const char *fname, const char *password);
int auth_write_local_hash(const char *fname, const char *password);
void auth_remote_status(int auto_started, int32_t status, const char *id, const char *username, const char *recovery_key_file, uint32_t ctx_flags);
void auth_end(const char *user, const char *password, const char *password_confirm, const char *password_old);
void auth_remote_login(const char *user, const char *password);
void auth_get_status(const char **p_status, int32_t *p_flags);
void auth_clear_status(void);
void auth_status(const char *status, int is_error, int hide_window, int32_t flags);
/* input.c */
int platform_lock_timeout;
int keyb_waits_for_click;
void input_set_focus_change(void);
void fixabsbits(uint64_t *bits);
void fixkeybits(unsigned long *keybits, uint64_t *absbits, int slot);
int abs_to_rel(struct domain *d, int slot, unsigned long *relbits, uint64_t *absbits);
int relbits_to_absbits(struct domain *d, unsigned long *relbits, uint64_t *absbits);
void input_domain_gone(struct domain *d);
void divert_domain_gone(struct divert_info_t *dv, struct domain *d);
void send_keypair(struct keypairs *key, struct domain *d);
int filter_keys(struct input_event *e);
void set_kbd_domain(struct domain *d);
void sync_mouse_domain(struct domain *d);
void sync_kbd_domain(struct domain *d);
void set_mouse_domain(struct domain *d);
void input_set_mouse(struct domain *d);
void input_set_keyb(struct domain *d);
void input_set(struct domain *d);
void input_give_keyboard(struct domain *d);
void input_return_keyboard(struct domain *d);
void input_give_keyboard_from_domain(struct domain *d, struct domain *new_keyb_dest);
void input_return_keyboard_to_domain(struct domain *d, struct domain *prev_keyb_dest);
void turn_numlock_off(void);
int key_status_get(int key);
void save_prev_keyb_domain(struct domain *d);
void restore_prev_keyb_domain(struct domain *d);
void wiggle_ctrl_key(struct domain *d);
int input_inject_seamless_keyboard(struct input_event *e);
int input_inject_seamless_mouse(struct input_event *e);
void input_domain_set_mouse_pos(struct domain *d, int x, int y);
void input_domain_set_mouse(struct domain *d);
void input_set_mouse_pos(int x, int y);
int input_get_mouse_speed(void);
void input_set_mouse_speed(int speed);
int input_get_numlock_restore_on_switch(void);
void input_set_numlock_restore_on_switch(int);
int input_domain_supports_abs(struct domain *d);
void input_domain_handle_resolution_change(struct domain *d, int xres, int yres);
int mouse_outside_frame(void);
void scale_pointer_event(struct input_event *e);
void input_inject(struct input_event *e, int slot, enum input_device_type input_type);
void input_led_code(int led_code, int domid);
void check_and_inject_event(struct input_event *e, int slot, enum input_device_type input_type);
int input_secure(int onoff);
void input_collect_password(void);
void input_add_binding(const int tab[], input_binding_cb_t cb, input_binding_cb_t force_cb, void *opaque);
void udev_mon_handler(void *opaque);
void onstart_sendconfig(struct domain *d);
void input_release(_Bool in_fork);
int input_init(void);
void sock_plugin_sendconfig(struct sock_plugin* plug);
/* domains.c */
void iterate_domains(void (*callback)(struct domain *, void *), void *opaque);
void check_diverts_for_the_dead(struct domain *d);
int domains_count(void);
void domain_set_slot(struct domain *d, int slot);
struct domain *domain_with_domid(int domid);
struct domain *domain_with_slot(int slot);
struct domain *domain_with_uuid(const char *uuid);
struct domain *domain_uivm(void);
struct domain *domain_pvm(void);
int domain_cant_print_screen(struct domain *d);
void domain_gone(struct domain *d);
int get_idle_time(void);
int get_last_input_time(void);
void domain_read_uuid(struct domain *d);
int add_domainstart_callback(void (*callback)(struct domain *));
void handle_switcher_abs(void *priv, struct msg_switcher_abs *msg, size_t msglen);
void switcher_pvm_domid(struct domain *d, uint32_t domid);
void handle_switcher_leds(void *priv, struct msg_switcher_leds *msg, size_t msglen);
void handle_switcher_shutdown(void *priv, struct msg_switcher_shutdown *msg, size_t msglen);
void domain_wake_from_s3(struct domain *d);
void domain_mouse_switch_config(void *opaque);
struct domain *domain_create(dmbus_client_t client, int domid, DeviceType type);
void domain_init(void);
void domain_release(_Bool infork);
/* switch.c */
int switcher_switch_graphic(struct domain *d, int force);
void switcher_unfocus_gpu(void);
int switcher_switch(struct domain *d, int mouse_switch, int force);
void switcher_domain_gone(struct domain *d);
void switcher_s3(struct domain *d);
int switcher_lock(int can_switch_out);
int switcher_auth_force(void);
void switcher_switch_on_mouse(struct input_event *e, int x, int y);
int32_t switcher_get_focus(void);
void switcher_switch_left(void);
void switcher_switch_right(void);
void switcher_init(void);
/* util.c */
void helper_exec(const char *bin, int domid);
void message(int flags, const char *file, const char *function, int line, const char *fmt, ...);
void log_dbus_error(const char *file, const char *function, int line, const char *err, const char *fmt, ...);
/* focus.c */
void focus_expect_death(struct domain *d);
void focus_dont_expect_death(struct domain *d);
void focus_update_domain(struct domain *d);
void focus_domain_gone(struct domain *d);
/* touchpad.c */
int touchpad_get_tap_to_click_enabled(void);
int touchpad_get_scrolling_enabled(void);
int touchpad_get_speed(void);
void touchpad_set_scrolling_enabled(int enabled);
void touchpad_set_tap_to_click_enabled(int enabled);
void touchpad_set_speed(int speed);
void handle_touchpad_event(struct input_event *ev, int slot);
void toggle_touchpad_status(void);
int init_touchpad(int fd);
void touchpad_reread_config(void);
/* keymap.c */
int keycode2ascii(int keycode);
char *get_configured_keymap(void);
int loadkeys(const char *keymap);
void keymap_init(void);
/* usb-tablet.c */
void set_and_inject_event(int slot, struct input_event *ev, int type, int code, int value);
void handle_usb_tablet_event(struct input_event *ev, int slot);
int init_usb_tablet(int fd, int slot, uint8_t subtype);
/* rpcgen/input_daemon_server_obj.c */
void dbus_glib_marshal_input_daemon_BOOLEAN__STRING_STRING_INT_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__INT_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__INT_INT_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__POINTER_POINTER_POINTER_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__INT_BOOLEAN_POINTER_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__BOOLEAN_INT_STRING_STRING_STRING_UINT_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__POINTER_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__STRING_UINT_UINT_UINT_UINT_UINT_UINT_UINT_UINT_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__STRING_POINTER_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__BOXED_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__POINTER_POINTER_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__BOOLEAN_POINTER_POINTER_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__STRING_STRING_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__STRING_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
void dbus_glib_marshal_input_daemon_BOOLEAN__BOOLEAN_POINTER(GClosure *closure, GValue *return_value, guint n_param_values, const GValue *param_values, gpointer invocation_hint, gpointer marshal_data);
const DBusGObjectInfo dbus_glib_input_daemon_object_info;
DBusGObjectInfo dbus_glib_input_daemon_object_info_modified;
GType input_daemon_object_get_type(void);
InputDaemonObject *input_daemon_create_glib_obj(void);
InputDaemonObject *input_daemon_export_dbus(DBusGConnection *conn, const char *path);
/* pm.c */
int host_pmop_in_progress(void);
void pm_init(void);
/* xen_vkbd.c */
void xen_vkbd_send_event(struct domain *d, struct input_event *event);
void xen_vkbd_backend_create(struct domain *d);
void xen_vkbd_backend_release(struct domain *d);
void xen_backend_init(int dom0);
void xen_backend_close(void);
/* xen_event.c */
void xen_event_send(struct xen_vkbd_backend *backend, uint16_t type, uint16_t code, int32_t value);
/* gesture.c */
int position_match(gesture_position a, int x, int y);
int gesture_match(gesture *g, int slot, int x, int y, int push, int *tracking);
void gestures_clean(void);
int gesture_match_move(gesture *g, int slot, int x, int y, int push, int *tracking);
int gesture_handler(int slot, int x, int y, int push);
/* lid.c */
struct lid_switch *lid_switch_public;
void lid_create_switch_event(int fd);
void lid_switch_release(_Bool infork);
/* debug.c */
void print_abs_bit_meaning(unsigned long* inbit);
void debug_packet(int slot, struct input_event *e);
void print_rel_bit_meaning(unsigned long* inbit);
void print_btn_bit_meaning(unsigned long* inbit);
/* encapsulate.c */
void set_keyb_dest(struct domain *d);
struct domain *get_keyb_dest(void);
/* socket.c */
void socket_server_init(void);
void socket_server_close(void);
void send_plugin_event(struct domain *d,int slot, struct input_event *e);
void send_plugin_dev_event(struct sock_plugin* plug, int code, int value);
