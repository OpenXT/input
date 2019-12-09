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

#define DOMAIN_ACTIVE_ADAPTER_NODE "display/activeAdapter"

extern struct timeval global_last_input_event;

static struct domain domains[NDOMAIN_MAX];

static xc_interface *xc_handle = NULL;
static bool destroy_in_fork = false;

void domain_print(const struct domain *d)
{
    if (!d->initialised)
        info ("[not initialised]");
    else
        info("dom%u<->slot-%u, "
            "%sconnected dmbus-client, "
            "is %sPV, is %spvm, is %sin s3, "
            "ABS %sable, "
            "desktop:%dx%d & relative:%dx%d, %d active adapters.",
            d->domid, d->slot,
            d->client ? "" : "dis",
            d->is_pv_domain ? "" : "not ",
            d->is_pvm ? "" : "not ",
            d->is_in_s3 ? "" : "not ",
            d->abs_enabled ? "en" : "dis",
            d->desktop_xres, d->desktop_yres,
            d->rel_x_mult, d->rel_y_mult,
            d->num_active_adapters);
}

void domains_print(void)
{
    int i;

    for (i = 0; i < NDOMAIN_MAX; ++i)
        domain_print(&domains[i]);
}

void iterate_domains(void (*callback)(struct domain *,void*), void* opaque)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; i++)
    {
        if (domains[i].initialised)
            callback(&domains[i], opaque);
    }
}

void check_diverts_for_the_dead(struct domain* d)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; ++i)
    {
        if (domains[i].divert_info != NULL)
            divert_domain_gone(domains[i].divert_info, d);
    }
}

int domains_count(void)
{
    int         i, n = 0;

    for (i = 0; i < NDOMAIN_MAX; i++)
        if (domains[i].initialised)
            n++;
    return n;
}

void
domain_set_slot(struct domain *d, int slot)
{
    d->slot = slot;
    xenstore_dom_write_int(d->domid, d->slot, "switcher/slot");
}

static struct domain *empty_domain()
{
    int i;

    for (i = 0; i < NDOMAIN_MAX; ++i)
        if (!domains[i].initialised)
            return &domains[i];

    return NULL;
}

struct domain *domain_with_domid(int domid)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; ++i) {
        if (domains[i].initialised &&
            domains[i].domid == domid)
            return &domains[i];
    }
    return NULL;
}

struct domain * domain_with_slot(int slot)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; ++i) {
        if (domains[i].initialised &&
            domains[i].slot == slot)
            return &domains[i];
    }
    return NULL;
}


struct domain * domain_with_slot_and_not_domid(int slot, int domid)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; ++i) {
        if (domains[i].initialised &&
            domains[i].slot == slot &&
            domains[i].domid != domid)
            return &domains[i];
    }
    return NULL;
}

struct domain *domain_with_uuid(const char *uuid)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; ++i) {
        if (domains[i].initialised &&
            domains[i].uuid &&
            !strcmp(domains[i].uuid, uuid))
            return &domains[i];
    }
    return NULL;
}

struct domain * domain_uivm(void)
{
    return domain_with_slot(0);
}


struct domain * domain_pvm(void)
{
    int i;
    for (i = 0; i < NDOMAIN_MAX; ++i) {
        if (domains[i].initialised &&
            domains[i].is_pvm)
            return &domains[i];
    }
    return NULL;
}


static void reset_prev_keyb_domain(struct domain *d)
{
    int i = 0;

    if ((d == NULL) || (d->domid == -1))
        return;

    for (i = 0; i < NDOMAIN_MAX; i++)
    {
        if (domains[i].initialised && (domains[i].prev_keyb_domid == d->domid))
        {
            info("reset previous keyboard domain for domain %d\n", domains[i].domid);
            domains[i].prev_keyb_domain_ptr = NULL;
            domains[i].prev_keyb_domid = -1;
        }
    }
}

int domain_cant_print_screen(struct domain *d)
{
    char tmp[1024];
    char path[1024];
    int rc = 0;

    if (d)
    {
        sprintf(path, "/vm/%s/policies/print-screen-disallowed", d->uuid);
        rc = db_read(tmp, 1024, path) && strcmp(tmp, "true") == 0;
        info("print-screen path:%s value:%s disallow:%d", path, tmp, rc);
    }
    return rc;
}

static char**
domain_get_whitelist_drivers()
{
    static char *driver[64] = { 0 };
    static int driver_read = 0;

    char buf[256] = { 0 }, *beg, *end;
    unsigned int dri = 0;

    if (driver_read)
        return driver;

    if (!db_read(buf, sizeof (buf), "/display-driver-whitelist")) {
        warning("error reading display driver whitelist!");
        return NULL;
    }

    beg = buf;
    end = strchr(beg, ',');

    while (*beg && dri < sizeof (driver)-1) {
        int sz =
            (end != NULL)
              ? (end-beg)
              : (int)strlen(beg);
        if (sz > 0) {
            driver[dri] = strndup(beg, sz);
            ++dri;
        }

        if (!end)
            break;
        else {
            beg = end + 1;
            end = strchr(beg, ',');
        }
    }
    driver[dri] = NULL;
    driver_read = 1;

    dri = 0;
    while (driver[dri]) {
        info("display driver whitelist: %d = %s", dri, driver[dri]);
        ++dri;
    }

    return driver;
}

static void
domain_active_adapter_node_watch(const char *path, void *opaque)
{
    struct domain *d = opaque;
    char *buff = NULL;
    int i, disabled_surface = 0;
    char **whitelist_drivers = NULL;

    buff = xenstore_dom_read(d->domid, "switcher/have_gpu");
    if (buff && strlen(buff) && strtol(buff, NULL, 0) == 1)
        goto out;

    whitelist_drivers = domain_get_whitelist_drivers();

    buff = xenstore_read("%s", path);
    if (!buff || strlen(buff) == 0)
        goto out;

    for (i = 0; whitelist_drivers[i]; ++i)
    {
        if (strcasestr(buff, whitelist_drivers[i]))
            break;
    }

    if (whitelist_drivers[i] == NULL)
    {
        /* Only disable the surface if it's not handled as a surfman VGPU
         * Take care of calling surfman only one */
        if (d->vgpu_enabled == -1)
            com_citrix_xenclient_surfman_has_vgpu_(xcbus_conn, SURFMAN_SERVICE,
                                                   SURFMAN_PATH, d->domid,
                                                   &d->vgpu_enabled);
        if (!d->vgpu_enabled)
            disabled_surface = 1;
    }

    if (d->disabled_surface != disabled_surface)
    {
        d->disabled_surface = disabled_surface;
        if (d->disabled_surface)
        {
            /* Write "" to xenstore so the blanker will pic the best
             * resolution (nvidia pass through scenario). */
            xenstore_dom_write(d->domid, "", "switcher/display_size");
        }

        switcher_switch_graphic(disabled_surface ? domain_uivm() : 0, 0);
    }

out:
    free(buff);
}


/*
 * The blanker in the VM will write the number of adapters of the VM
 * to the display/activeAdapter node.
 * Then, we set a watch on each of this adapters
 */
static void
domain_active_adapter_watch(const char *path, void *opaque)
{
    struct domain *d = opaque;
    char *buff = NULL, *endptr, node_name[33];
    long int val = 0;
    int i;

    buff = xenstore_dom_read(d->domid, DOMAIN_ACTIVE_ADAPTER_NODE);
    if (!buff || strlen(buff) == 0)
    {
        info("No active adapter node for domain %d", d->domid);
        return;
    }

    val = strtol(buff, &endptr, 10);

    // no active adapters for this domain yet
    if (endptr == buff)
        return;

    // graphics driver unloading for a SVM with a secondary GPU
    if (0 == val && d->has_secondary_gpu)
        switcher_switch_graphic(d, 0);

    info("domain %d has %d active adapter(s)", d->domid, val);
    d->num_active_adapters = val;
    for (i = 0; i < val; ++i)
    {
        sprintf(node_name, "%s/%d", DOMAIN_ACTIVE_ADAPTER_NODE, i);
        info ("watching node %s\n", node_name);
        if (!xenstore_dom_watch(d->domid, domain_active_adapter_node_watch, d, "%s", node_name))
                warning("failed to install xenstore watch! %s", node_name);
    }
}


static void domain_surface_disabled_detect_init(struct domain *d)
{
    char *buff = NULL;
    char perm_buff[16];

    d->vgpu_enabled = -1;

    // create node only if it does not exist yet
    buff = xenstore_dom_read(d->domid, DOMAIN_ACTIVE_ADAPTER_NODE);
    if (!buff)
    {
        sprintf(perm_buff, "n0 b%d", d->domid);
        // apparently the lib requires the permissions to be separated by 0
        perm_buff[2] = 0;
        xenstore_dom_write(d->domid, "", DOMAIN_ACTIVE_ADAPTER_NODE);
        xenstore_dom_chmod(d->domid, perm_buff, 2, DOMAIN_ACTIVE_ADAPTER_NODE);
    }
    else
        free(buff);

    if (!xenstore_dom_watch(d->domid, domain_active_adapter_watch, d, DOMAIN_ACTIVE_ADAPTER_NODE))
            warning("failed to install xenstore watch! %s", DOMAIN_ACTIVE_ADAPTER_NODE);

}

static void release_xs_watches(struct domain *d)
{
    int i;
    xenstore_dom_watch(d->domid, NULL, NULL, DOMAIN_ACTIVE_ADAPTER_NODE);
    /* arbitrary assumption of maximum of 8 active adapters per domain */
    for (i = 0; i < d->num_active_adapters; ++i) {
        char node[256];
        snprintf(node, sizeof(node), "%s/%d", DOMAIN_ACTIVE_ADAPTER_NODE, i);
        xenstore_dom_watch(d->domid, NULL, NULL, "%s", node);
    }
    xenstore_dom_watch(d->domid, NULL, NULL, "power-state");
    xenstore_dom_watch(d->domid, NULL, NULL, "switcher/command");
    xenstore_dom_watch(d->domid, NULL, NULL, "attr/desktopDimensions");
}

/**
 * FIXME: It's really ugly but I have no solution ...
 * if dmbus_disconnect_client is called to release fd
 **/

void domain_gone(struct domain *d)
{
    xc_dominfo_t info;
    int ret;

    if (!d)
        return;

    if (!destroy_in_fork)
        info("cleaning up domain %d",d->domid);
    else
        info ("cleaning up in fork domain %d", d->domid);

    if (d->vkbd_backend) {
        xen_vkbd_backend_release(d);
    }

    release_xs_watches(d);

    if (d->is_pvm && d->sstate != 0)
        focus_domain_gone(d);

    if (!destroy_in_fork)
        reset_prev_keyb_domain(d);

    free(d->uuid);
    destroy_divert_info(&d->divert_info);
    d->client = NULL;
    d->initialised = false;
    d->slot = -1;
    d->is_pvm = 0;
    d->prev_keyb_domain_ptr = NULL;
    d->prev_keyb_domid = -1;
        
    if (!destroy_in_fork)
    {
        input_domain_gone(d);
        switcher_domain_gone(d);
    }
    check_diverts_for_the_dead(d);
}

#if 0
static void switcher_led_timer(void *opaque)
{
    static struct timer_t       *t = NULL;
    struct timeval              tv;
    static int                  onoff = 0;

    opaque = opaque;

    if  (onoff == 1 && (!mouse_grab_domain || !keyboard_grab_domain ||
                mouse_grab_domain == keyboard_grab_domain))
    {
        if (t)
        {
            free_timer(t);
            t = NULL;
        }
        return;
    }

    input_leds(onoff);
    onoff = !onoff;

    if (t == NULL)
        t = set_new_timer(switcher_led_timer, NULL);

    gettimeofday(&tv, NULL);
    tv.tv_usec += 500 * 1000;

    if (tv.tv_usec > 1000000) {
        tv.tv_usec -= 1000000;
        tv.tv_sec++;
    }

    set_timer(t, &tv);
}
#endif

static void domain_power_state(const char *path, void *opaque)
{
    struct domain *d = opaque;
    char *tmp = 0;
    int state = 0;

    if (!d)
    {
        error("No domain in switcher_power_state!\n");
        return;
    }

    tmp = xenstore_read("%s", path);
    if (!tmp || strlen(tmp) == 0) {
        free(tmp);
        return;
    }
    state = strtol(tmp, NULL, 10);
    free(tmp);
    info("domain id=%d power-state now %d",d->domid,state);

    if (state == 3)
    {
        info("domain id=%d entered s3",d->domid);
        d->is_in_s3 = 1;
        gettimeofday(&d->time_of_s3, NULL);

        /* If a domain goes to sleep, then reset its previous keyboard domain
           so that on wakeup, the domain itself will have the keyboard. */
        info("reset previous keyboard domain for domain %d\n", d->domid);
        d->prev_keyb_domain_ptr = NULL;
        d->prev_keyb_domid = -1;

        /* If a domain goes to sleep, then reset the previous keyboard domain
           for all domains that have given the keyboard to this domain. */
        reset_prev_keyb_domain(d);

        switcher_s3(d);


    }
}

int get_idle_time()
{
    struct timeval now;
    int i, latest_input_activity=0, sleeping_vm_count=0, guest_vm_count=0, uivm_domid = -1;
    struct domain *uivm = domain_uivm();

    if (uivm != NULL)
        uivm_domid = uivm->domid;

    /* Calculate latest_input_activity considering all domains except uivm */
    for (i = 0; i < NDOMAIN_MAX; i++)
    {
            if (domains[i].initialised && (domains[i].domid != uivm_domid))
            {
                    guest_vm_count++;
                    if(domains[i].is_in_s3)
                    {
                        domains[i].last_input_event = domains[i].time_of_s3;
                        sleeping_vm_count++;
                    }

                    latest_input_activity = MAX(latest_input_activity,domains[i].last_input_event.tv_sec);
            }
    }

    /* Check if all guest vms are asleep or only the uivm is running. */
    if (sleeping_vm_count == guest_vm_count)
    {
       /* Dont crash if there is no uivm running. */
        if (uivm != NULL)
            latest_input_activity = MAX(latest_input_activity,uivm->last_input_event.tv_sec);
        gettimeofday(&now,NULL);
        return (now.tv_sec - latest_input_activity);
    }
    else
        return 0;
}

int get_last_input_time()
{
    struct timeval now;

    gettimeofday(&now,NULL);
    return (now.tv_sec - global_last_input_event.tv_sec);
}


#if 0
static void domain_slot_watch(const char *path, void *opaque)
{
    struct domain       *d = opaque;
    struct domain       *d_slot;
    int                 slot;
    char                *tmp;

    path = path;
    if (!(tmp = xenstore_dom_read(d->domid, "switcher/slot")))
        return;
    slot = strtol(tmp, NULL, 10);
    free(tmp);

    if ((d_slot = domain_with_slot(slot))
    {
        error("There is already a domain on this slot, %d\n", d_slot->domid);
        return;
    }

    d->slot = slot;
    info("Domain %d is on slot %d\n", d->domid, slot);
}
#endif


static void domain_command(const char *path, void *opaque)
{
    struct domain       *d = opaque;
    char                *tmp;
    int                 slot;
    int                 domid;

    tmp = xenstore_read("%s", path);
    if (!tmp)
        return;

    if (!*tmp)  {
        free(tmp);
        return;
    }

    info("Command \"%s\", domid %d\n", tmp, d->domid);

    if (strcmp(tmp, "switch") == 0)
    {
        switcher_switch(d, 0, 0);
    }
    else if (sscanf(tmp, "switch slot %d", &slot) == 1)
    {
        struct domain   *s = domain_with_slot(slot);

        if (!s)
        {
            error("slot %d doesn't exist", slot);
            return;
        }
        info("try to switch to slot %d", slot);
        switcher_switch(s, 0, 0);
    }
    else if (sscanf(tmp, "switch domid %d", &domid) == 1)
    {
        struct domain   *s = domain_with_domid(domid);
        if (!s)
        {
            error("domain %d doesn't exist", domid);
            return;
        }
        switcher_switch(s, 0, 0);
    }
    else if (strcmp("keyboard take", tmp) == 0)
    {
        input_give_keyboard(d);
    }
    else if (sscanf(tmp, "keyboard take %d", &domid) == 1)
    {
        struct domain *src_domain = domain_with_domid(domid);
        if (!src_domain)
        {
            error("domain %d doesn't exist", domid);
            return;
        }
        input_give_keyboard_from_domain(src_domain, d);
    }
    else if (strcmp("keyboard release", tmp) == 0)
    {
        input_return_keyboard(d);
    }
    else if (sscanf(tmp, "keyboard release %d", &domid) == 1)
    {
        struct domain *dest_domain = domain_with_domid(domid);
        if (!dest_domain)
        {
            error("domain %d doesn't exist", domid);
            return;
        }
        input_return_keyboard_to_domain(dest_domain, d);
    }

    xenstore_write("", "%s", path);
    free(tmp);
}

void
domain_read_uuid(struct domain *d)
{
    char *tmp;

    if (!(tmp = xenstore_dom_read(d->domid, "vm")))
        return;
    d->uuid = xenstore_read("%s/uuid", tmp);
    free(tmp);
}

static void
domain_read_is_pv_domain(struct domain *d)
{
    xc_dominfo_t info;
    int ret;

    d->is_pv_domain = 0;

    ret = xc_domain_getinfo(xc_handle, d->domid, 1, &info);
    if (ret != 1) {
        warning("xc_domain_getinfo() failed (%s).", strerror(errno));
        return;
    }
    if (info.domid != (uint32_t) d->domid) {
        warning("xc_domain_getinfo() reports a different domid (input:%d vs getinfo:%d).", d->domid, info.domid);
        return;
    }

    /* For input-daemon, PVHv2 and PV guests are the same (vkbd, no controller emulation).
     * This second test is the only way we have on 4.12 to differenciate
     * through xc_domain_getinfo between HVM and PVHv2.
     * Also, it is true on x86 only, LAPIC emulation is hard set by libxl
     * (libxl_x86.c) and PVHv2 can only request LAPIC emulation, so no other
     * flag can be set (xen/arch/x86/domain.c:arch_domain_create). */
    d->is_pv_domain =
           !info.hvm
        || (info.hvm && (info.arch_config.emulation_flags == XEN_X86_EMU_LAPIC));
}

static void
domain_read_has_secondary_gpu(struct domain *d)
{
    char gpu_db_str[128];
    char gpu;
    int rc = 0;

    d->has_secondary_gpu = 0;

    if (d->is_pvm)
        return;

    sprintf(gpu_db_str, "/vm/%s/gpu", d->uuid);
    rc = db_read(&gpu, 1, gpu_db_str);

    if (rc != TRUE)
        return;

    d->has_secondary_gpu = (gpu != 0);
}

static int
domain_read_slot(struct domain *d)
{
    char slot_str[10];
    char path[128];
    int rc = 0;

    sprintf(path, "/vm/%s/slot", d->uuid);
    rc = db_read(slot_str, 1, path);

    if (rc != TRUE)
        return -1;

    return strtol(slot_str, NULL, 10);
}

int add_domainstart_callback(void (*callback)(struct domain *))
{
    struct callbacklist* newitem;
    newitem = (struct callbacklist*) malloc(sizeof(struct callbacklist));
    if (newitem)
    {
        newitem->next=domainstart_callback;
        newitem->callback=callback;
        domainstart_callback=newitem;
        return 0;
    }
    return -1;
}


static void domain_calculate_abs_scaling(const char *path, void *opaque)
{
    struct domain *d = opaque;
    char *buff = NULL;
    int xres = 0, yres = 0;

    if (d == NULL || path == NULL)
        return;

    d->rel_x_mult = MAX_MOUSE_ABS_X / DEFAULT_RESOLUTION_X;
    d->rel_y_mult = MAX_MOUSE_ABS_Y / DEFAULT_RESOLUTION_Y;

    buff = xenstore_read("%s", path);
    if (!buff || strlen(buff) == 0)
    {
        info("No desktopDimensions node for domain %d", d->domid);
        /* These might have been previously set, so need to clear the value. */
        d->desktop_xres = d->desktop_yres = 0;
        free(buff);
        return;
    }

    if ((sscanf(buff, "%d %d", &xres, &yres) != 2) || (xres <= 0) || (yres <= 0))
    {
        info("Invalid desktopDimensions node for domain %d, value is %s", d->domid, buff);
        /* These might have been previously set, so need to clear the value. */
        d->desktop_xres = d->desktop_yres = 0;
        free(buff);
        return;
    }

    info("Found valid desktopDimensions node for domain %d, xres is %d, yres is %d", d->domid, xres, yres);
    free(buff);

    d->rel_x_mult = (double) MAX_MOUSE_ABS_X / (double) xres;
    d->rel_y_mult = (double) MAX_MOUSE_ABS_Y / (double) yres;

    /* If the desktop dimensions have changed, need to adjust the mouse position. */
    input_domain_handle_resolution_change(d, xres, yres);

    d->desktop_xres = xres;
    d->desktop_yres = yres;
}


/**
 *  Returns a non-zero value if this slot is occuped by a left-over domain.
 *
 */
static int slot_occupied_by_dead_domain(int slot, int domid)
{
    int dying, ret;
    xc_dominfo_t info;
    char * reported_domid;

    struct domain * d;
    //If we've been passed the special slot -1, vacuously return false;
    //this is an indication that a domain has no real slot (and thus can't
    //occupy a slot).
    if(slot == -1) {
        return 0;
    }

    d = domain_with_slot_and_not_domid(slot, domid);

    //Some other domain, likely dead, occupies this slot, deal with it
    if (d) {
        ret = xc_domain_getinfo(xc_handle, d->domid, 1, &info);
        //Xen couldn't find the domain or the domain info it returned is not
        //the domain we asked for, so it has NO idea about it.
        if(ret != 1 || (int)info.domid != d->domid) {
            return 1;
        }
    }

    d = domain_with_slot(slot);
    //If no domain occupies this slot, it can't be occupied by a dead one.
    if(!d) {
        return 0;
    }

    //Ask xen for information about the domain...
    ret = xc_domain_getinfo(xc_handle, d->domid, 1, &info);

    //If the domain doesn't exist, according to Xen, we've stumbled
    //upon a left-over dead record!
    if(ret != 1) {
        return 1;
    }

    //Otherwise, we'll have to employ an ugly heuristic to see if
    //the toolstack has tried (and failed!) to destroy the VM.
    //
    //If you see a way to improve this heuristic, by all means, do!
    //Current heuristic:
    //
    //- If the domain is marked as /dying/, we've started the process
    //  of killing it; and
    //- If the domain's record has been purged from the Xenstore, its
    //  cleanup is either done or no longer possible.
    //
    //If both conditions are met, we've found a dead-in-all-but-name-VM.
    //
    reported_domid = xenstore_dom_read(d->domid, "domid");

    //If we've obtained a domid from the xenstore, this domain still has
    //a xenstore record, and likely is not yet dead.
    if(reported_domid) {
      free(reported_domid);
      return 0;
    }

    //Otherwise, we'll return the dying status-- as this will tell us
    //if the domain is all-but-dead, as defined above.
    return info.dying;
}


static void switcher_domid(struct domain *d, uint32_t domid)
{
  char perm[8];
  char path[64];
  char perms[128];
  char *tmp = 0;
  int stubdom_domid;
  int slot;
  struct domain       *d_pvm;

  if (domain_with_domid(domid))
    {
      error("domain %d already exists", domid);
      return;
    }

  d->domid = domid;

  domain_read_uuid(d);
  domain_read_is_pv_domain(d);

  slot = domain_read_slot(d);

  // Ensures that no "zombie" domains are taking up valuable switcher slots.
  // Ideally, this shouldn't be necessary-- but in development environments
  // it's possible for interesting things to happen (e.g. for a developer to
  // destroy a stub-domain without giving us notice of the termination.)

  // This safeguard function isn't strictly neccessary, but it's lightweight
  // and prevents some awful behavior (including huge delays) if developers do
  // manage to do fun things like kernel panic their stubdomains.
  if(slot_occupied_by_dead_domain(slot, domid))
  {
      warning("slot %d is held by a dead domain; cleaning up", slot);
      domain_gone(domain_with_slot_and_not_domid(slot, domid));
  }

  if (domain_with_slot(slot) || (slot == -1))
  {
      error("slot %d already taken (wanted by domain %d)",slot,domid);
      return;
  }
  d->slot = slot;

  info("New domain %d (slot %d)", domid, slot);

  /* init xenstore nodes and permissions for midori secure window and status report */
  if (slot == 0)
  {
      xenstore_dom_write(domid, "http://1.0.0.0/auth.html", "login/url");
      xenstore_dom_write(domid, "3", "login/state");

      sprintf(perm, "n%d", domid);

      sprintf(path, "/local/domain/%d/report/state", domid);
      xenstore_write_int(3, "%s", path);
      xenstore_chmod(perm, 1, "%s", path);

      sprintf(path, "/local/domain/%d/report/url", domid);
      xenstore_write("http://1.0.0.0/create_report.html", "%s", path);
      xenstore_chmod (perm, 1, "%s", path);
  }

  xenstore_dom_write(domid, "", "switcher/command");
  sprintf(perms, "r%d", domid);
  xenstore_dom_chmod(domid, perms, 1, "switcher/command");
  if (!xenstore_dom_watch(domid, domain_command, d, "switcher/command"))
          warning("failed to install xenstore watch! switcher/command");


  d->rel_x_mult = MAX_MOUSE_ABS_X / DEFAULT_RESOLUTION_X;
  d->rel_y_mult = MAX_MOUSE_ABS_Y / DEFAULT_RESOLUTION_Y;
  d->desktop_xres = d->desktop_yres = 0;
  if (!xenstore_dom_watch(d->domid, domain_calculate_abs_scaling, d, "attr/desktopDimensions"))
          warning("failed to install xenstore watch! attr/desktopDimensions");

  d->is_pvm = 0;
  d->keyboard_led_code = 0;
  xenstore_dom_write_int(d->domid, 0, "switcher/have_gpu");

  if (!xenstore_dom_watch(domid, domain_power_state, d, "power-state"))
          warning("failed to install xenstore watch! power-state");
  domain_power_state("power-state",d);

  d_pvm = domain_pvm();
  domain_surface_disabled_detect_init(d);

  xen_vkbd_backend_create(d);
  domain_read_has_secondary_gpu(d);

  domain_mouse_switch_config(d);

  focus_update_domain(d);

/* call our callbacks*/
  struct callbacklist* c = domainstart_callback;
  while (c)
  {
     c->callback(d);
     c = c->next;
  }

  /* we are ready to receive switch commands for this domain */
  xenstore_dom_write(domid, "true", "switcher/ready");
}

void handle_switcher_abs(void *priv, struct msg_switcher_abs *msg, size_t msglen)
{
  struct domain *d = priv;

  d->abs_enabled = msg->enabled;
  info("PV mouse driver reports abs support is %s for domid:%d", (d->abs_enabled)?"on":"off",d->domid);
}

void switcher_pvm_domid(struct domain *d, uint32_t domid)
{
  switcher_domid(d, domid);

  info("Domain %d is a pvm", d->domid);
  d->is_pvm = 1;
  xenstore_dom_write_int(d->domid, 1, "switcher/have_gpu");

  focus_update_domain(d);
  info("pvm connected, switching to pvm");
  switcher_switch(d, 0, 0);
}

void handle_switcher_leds(void *priv, struct msg_switcher_leds *msg, size_t msglen)
{
  struct domain *d = priv;

  d->keyboard_led_code = msg->led_code;
  input_led_code(msg->led_code, d->domid);
}

void handle_switcher_shutdown(void *priv, struct msg_switcher_shutdown *msg, size_t msglen)
{
  struct domain *d = priv;
  int state = msg->reason;

  info("Domain %d, slot %d, is_pvm %d, reports shutdown code %d",
       d->domid,d->slot,d->is_pvm,state);

  d->sstate = state;
  switch (state) {
      case SWITCHER_SHUTDOWN_REBOOT:
          /*The pvm is rebooting, we need to block
            execution of the init_raster code for a bit*/
          focus_expect_death(d);
          break;
      case SWITCHER_SHUTDOWN_S3:
      case SWITCHER_SHUTDOWN_S4:
      case SWITCHER_SHUTDOWN_S5:
          /*The pvm is dieing, we need */
          /*to activate the raster code */
          /*in the switch*/
          focus_dont_expect_death(d);
          break;
  }

  warning("not reached");
}

static void send_wakeup(struct domain *d)
{
    struct msg_input_wakeup msg;
    if (d->client)
        input_wakeup(d->client, &msg, sizeof(msg));
}

void domain_wake_from_s3(struct domain *d)
{
    unsigned long s_state = 0;
    int handle;

    if (!d)
        return;
    if (!d->is_in_s3) return;
    if (host_pmop_in_progress()) {
        info("NOT resuming domain %d from S3 - host power operation in progress");
        return;
    }

    info("Resuming domain %d from S3", d->domid);

    if (xc_handle != NULL)
    {
        xc_get_hvm_param(xc_handle, d->domid, HVM_PARAM_ACPI_S_STATE, &s_state);
        if (s_state == 3)
            xc_set_hvm_param(xc_handle, d->domid, HVM_PARAM_ACPI_S_STATE, 0);
        d->is_in_s3 = 0;
        d->sstate = 5;
        send_wakeup(d);
    } else {
        error("Failed to open xen control interface");
    }

    // Waking up a PVM from S3 will trigger the PVM guest driver to re-initialize
    // the graphic device. Therefore, we might as well switch directly to it since
    // it is displayable until we find a way to recover the device once put in S3.
    if (d->is_pvm) {
        switcher_switch(d, 0, 0);
    }
}

/*
** Gather the info from the database into the domain struct
** do that every 1 seconds so we can change setting on runtime.
**
** /switcher/<slot>/left|right <slot>|prev
*/
void domain_mouse_switch_config(void *opaque)
{
    struct domain *d = opaque;
    char *obj_path = NULL;
    GError *error = NULL;
    GValue value1 = G_VALUE_INIT;
    GValue value2 = G_VALUE_INIT;
    char *str = NULL;

    int values[2] = {-1, -1};

    if (!com_citrix_xenclient_xenmgr_find_vm_by_uuid_ ( xcbus_conn, "com.citrix.xenclient.xenmgr", "/", d->uuid, &obj_path )) {
        return;
    }
    /* #!$?! we really should autogen these */
    DBusGProxy *vm_proxy = xcdbus_get_proxy(xcbus_conn, "com.citrix.xenclient.xenmgr", obj_path, "org.freedesktop.DBus.Properties");
    if (!dbus_g_proxy_call(
            vm_proxy, "Get", &error,
            G_TYPE_STRING, "com.citrix.xenclient.xenmgr.vm", G_TYPE_STRING, "seamless-mouse-left", DBUS_TYPE_INVALID,
            G_TYPE_VALUE, &value1, DBUS_TYPE_INVALID ))
    {
        return;
    }

    if (G_VALUE_HOLDS(&value1, G_TYPE_INT)) {
        // slot
        values[0] = g_value_get_int(&value1);
    }

    if (!dbus_g_proxy_call(
            vm_proxy, "Get", &error,
            G_TYPE_STRING, "com.citrix.xenclient.xenmgr.vm", G_TYPE_STRING, "seamless-mouse-right", DBUS_TYPE_INVALID,
            G_TYPE_VALUE, &value2, DBUS_TYPE_INVALID ))
    {
        return;
    }

    if (G_VALUE_HOLDS(&value2, G_TYPE_INT)) {
        values[1] = g_value_get_int(&value2);
    }

    info ("configuring seamless mouse for uuid=%s left=%d right=%d", d->uuid, values[0], values[1]);
    d->mouse_switch.left = values[0];
    d->mouse_switch.right = values[1];
}


struct domain *domain_create(dmbus_client_t client, int domid, DeviceType type)
{
    struct domain *d=empty_domain();
    if (!d)  {
        error("too many domains");
        return d;
    }

    memset( d, 0, sizeof(struct domain) );
    d->domid=-1;
    d->slot=-1;
    d->is_in_s3=0;
    d->prev_keyb_domain_ptr = NULL;
    d->prev_keyb_domid = -1;
    d->client=client;
    d->initialised = true;
    d->sstate = 5;
    d->mouse_switch.left = -1;
    d->mouse_switch.right = -1;
    d->divert_info = NULL;
    d->plugin=NULL;
    d->last_devslot=INPUTSLOT_INVALID;
    if (DEVICE_TYPE_INPUT == type)
        switcher_domid(d, domid);
    else if (DEVICE_TYPE_INPUT_PVM == type)
        switcher_pvm_domid(d, domid);

    return d;
}

void domain_init(struct domain *d, int domid)
{
    memset(d, 0, sizeof (*d));

    d->initialised = false;
    d->domid = domid;
    d->slot = -1;

    d->mouse_switch.left = -1;
    d->mouse_switch.right = -1;

    d->last_devslot = INPUTSLOT_INVALID;

    d->rel_x_mult = MAX_MOUSE_ABS_X / DEFAULT_RESOLUTION_X;
    d->rel_y_mult = MAX_MOUSE_ABS_Y / DEFAULT_RESOLUTION_Y;

    /* Unknown... */
    d->sstate = 5;
    d->prev_keyb_domid = -1;
}

struct domain *domain_new(int domid)
{
    struct domain *d;

    /* Enforce domain unicity. */
    if (domain_with_domid(domid)) {
        error("%s: Could not create new dom%d already exists.", __func__, domid);
        return NULL;
    }
    d = empty_domain();
    if (d == NULL) {
        error("%s: Could not create new dom%d, maximum capacity reached.", __func__, domid);
        return NULL;
    }
    domain_init(d, domid);
    return d;
}

void domain_release(struct domain *d)
{
    domain_detach_vkbd(d);
    release_xs_watches(d);

    /* TODO: These have deep entanglement with the behaviour of input. */
    reset_prev_keyb_domain(d);
    free(d->uuid);
    destroy_divert_info(&d->divert_info);

    /* TODO: These have deep entanglement with the behaviour of input. */
    d->client = NULL;
    d->initialised = false;
    d->slot = -1; /* XXX: Waaaat?! */
    d->is_pvm = 0;
    d->prev_keyb_domain_ptr = NULL;
    d->prev_keyb_domid = -1;

    input_domain_gone(d);
    switcher_domain_gone(d);
    check_diverts_for_the_dead(d);

    d->initialised = false;
}

int domain_assign_slot(struct domain *d)
{
    int slot;
    char perm[128];

    /* Slot is set by the toolstack in Xenstore. */
    slot = domain_read_slot(d);
    if (slot < 0 || slot >= NDOMAIN_MAX) {
        error("%s: Slot %d is invalid.", __func__, slot);
        return -EINVAL;
    }
    /* Salvage forgotten slots from dead-domains. */
    if (slot_occupied_by_dead_domain(slot, d->domid)) {
        warning("%s: Slot %d is held by a dead domain, clean-up.",
                __func__, slot);
        domain_gone(domain_with_slot_and_not_domid(slot, d->domid));
    }
    /* Domain<->Slot has to be unique. */
    if (domain_with_slot(slot) || (slot < 0)) {
        error("%s: Slot %d is already attributed to dom%u.",
              __func__, d->domid, slot);
        return -EEXIST;
    }
    d->slot = slot;

    /* Setup Xenstore node for slot switch.
     * XXX: This is a bit weird, is there still (incentive to) support domains
     *      being able to switch to another slot?
     */
    xenstore_dom_write(d->domid, "", "switcher/command");
    sprintf(perm, "r%d", d->domid);
    xenstore_dom_chmod(d->domid, perm, 1, "switcher/command");
    if (!xenstore_dom_watch(d->domid, domain_command, d, "switcher/command"))
        warning("%s: Could not setup xenstore watch on switcher/command", __func__);

    return 0;
}

int domain_setup(struct domain *d)
{
    int rc;

    /* Allocate/Store the domain UUID in /struct domain/. */
    domain_read_uuid(d);
    /* xc_getphysinfo to check if this domain is PV or HVM. */
    domain_read_is_pv_domain(d);
    /* Assign the slot passed by the toostack through Xenstore to this domain.
     *  - Slot read from /vm/<uuid>/slot (uuid needs to be known beforehand)
     *  - Also setup relevant nodes in XenStore to manage that slot.
     */
    rc = domain_assign_slot(d);
    if (rc)
        return rc;

    /* Slot 0 (UIVM) special case.
     * XXX: Stuff in here might be deprecated... */
    if (d->slot == 0) {
        char path[128], perm[128];

        xenstore_dom_write(d->domid, "http://1.0.0.0/auth.html", "login/url");
        xenstore_dom_write(d->domid, "3", "login/state");

        sprintf(perm, "n%d", d->domid);
        sprintf(path, "/local/domain/%d/report/state", d->domid);
        xenstore_write_int(3, "%s", path);
        xenstore_chmod(perm, 1, "%s", path);

        sprintf(path, "/local/domain/%d/report/url", d->domid);
        xenstore_write("http://1.0.0.0/create_report.html", "%s", path);
        xenstore_chmod(perm, 1, "%s", path);
    }

    /* Watch on node attr/desktopDimensions for resize events?
     * XXX: Isn't that HVM/Windows only ? */
    if (!xenstore_dom_watch(d->domid, domain_calculate_abs_scaling, d,
                            "attr/desktopDimensions"))
        warning("%s: Could not setup xenstore watch on switcher/command."
                " Slot will be static.", __func__);

    /* Handle PM events.
     * Setup watch on Xenstore node <dompath>/power-state. */
    if (!xenstore_dom_watch(d->domid, domain_power_state, d, "power-state"))
        warning("%s: Could not setup xenstore watch on power-state."
                " Power-management event will not be handled properly.",
                __func__);
    domain_power_state("power-state", d);

    /* Initialise display position for mouse switching.
     * XXX: Uses <dompath>/switcher/<slot>/{left,right} and talks with
     *      xenvm over dbus. */
    domain_mouse_switch_config(d);

    d->initialised = true;
    return 0;
}

int domain_set_pvm(struct domain *d, bool is_pvm)
{
    assert(d != NULL);
    d->is_pvm = is_pvm;
    xenstore_dom_write_int(d->domid, d->is_pvm, "switcher/have_gpu");
    return 0;
}

int domain_attach_vkbd(struct domain *d)
{
    assert(d != NULL);

    if (!d->is_pv_domain) {
        error("%s: Could not attach VKBD to dom%u. Domain must be PV.",
                __func__, d->domid);
        return -EINVAL;
    }
    if (d->vkbd_backend != NULL) {
        error("%s: Could not attach VKBD to dom%u."
              " Domain already has a VKBD device, input-server can only"
              " manage one per domain.", __func__, d->domid);
        return -EEXIST;
    }
    /* Initialise the VKBD backend with libxenbackend. */
    xen_vkbd_backend_create(d);

    /* XXX: VKBD wants absolute coordinates? This used to be passed as DMBUS
     *      RPC, but was always /true/. */
    d->abs_enabled = true;

    return 0;
}

void domain_detach_vkbd(struct domain *d)
{
    assert(d != NULL);
    if (d->vkbd_backend == NULL)
        return;

    xen_vkbd_backend_release(d);

    /* XXX: Vkbd was apparently used to add/remove the /struct domain/ from input-server management.
     *      Since Input only handles one Vkbd (one keyboard and one mouse), detaching Vkbd means we need
     *      to reset focus, release resources, ...
     */
    domain_release(d);
}

void domains_init(void)
{
    int i;

    if (xc_handle == NULL)
        xc_handle = xc_interface_open(NULL, NULL, 0);

    for (i = 0; i < NDOMAIN_MAX; i++)
    {
        memset( &domains[i], 0, sizeof(struct domain) );
        domains[i].domid=-1;
        domains[i].slot=-1;
        domains[i].client=NULL;
        domains[i].initialised=false;
    }
}


void domains_release(bool infork)
{
    unsigned int i = 0;

    destroy_in_fork = infork;

    for (i = 0; i < NDOMAIN_MAX; i++)
    {
        if (domains[i].client)
            dmbus_client_disconnect(domains[i].client);
    }

    /* FIXME: for the moment only fds are released */
    if (xc_handle)
        xc_interface_close(xc_handle);
    xc_handle = NULL;

    destroy_in_fork = false;
}
