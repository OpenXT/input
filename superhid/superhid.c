/*
 * hid.c -- HID Composite driver
 *
 * Based on multi.c
 *
 * Copyright (C) 2010 Fabien Chouteau <fabien.chouteau@barco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb/composite.h>

#include "gadget_chips.h"
#define DRIVER_DESC		"HID Gadget"
#define DRIVER_VERSION		"2010/03/16"

/*-------------------------------------------------------------------------*/

#define HIDG_VENDOR_NUM		0x0525	/* XXX NetChip */
#define HIDG_PRODUCT_NUM	0xa4ac	/* Linux-USB HID gadget */

/*-------------------------------------------------------------------------*/

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "f_hid.c"


struct hidg_func_node {
	struct list_head node;
	struct hidg_func_descriptor *func;
};

struct hidg_device_node {
	struct list_head node;
	struct platform_device *dev;
};

static LIST_HEAD(hidg_func_list);

static LIST_HEAD(hidg_devices);

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	/* .bDeviceClass =		USB_CLASS_COMM, */
	/* .bDeviceSubClass =	0, */
	/* .bDeviceProtocol =	0, */
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(HIDG_VENDOR_NUM),
	.idProduct =		cpu_to_le16(HIDG_PRODUCT_NUM),
	/* .bcdDevice = f(hardware) */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* NO SERIAL NUMBER */
	.bNumConfigurations =	1,
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,

	/* REVISIT SRP-only hardware is possible, although
	 * it would not be called "OTG" ...
	 */
	.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};


/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};



/****************************** Configurations ******************************/

static int __init do_config(struct usb_configuration *c)
{
	struct hidg_func_node *e;
	int func = 0, status = 0;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	list_for_each_entry(e, &hidg_func_list, node) {
		status = hidg_bind_config(c, e->func, func++);
		if (status)
			break;
	}

	return status;
}

static struct usb_configuration config_driver = {
	.label			= "HID Gadget",
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/****************************** Gadget Bind ******************************/

static int __init hid_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	struct list_head *tmp;
	int status, funcs = 0;

	list_for_each(tmp, &hidg_func_list)
		funcs++;

	if (!funcs)
		return -ENODEV;

	/* set up HID */
	status = ghid_setup(cdev->gadget, funcs);
	if (status < 0)
		return status;

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		return status;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	/* register our configuration */
	status = usb_add_config(cdev, &config_driver, do_config);
	if (status < 0)
		return status;

	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, DRIVER_DESC ", version: " DRIVER_VERSION "\n");

	return 0;
}

static int __exit hid_unbind(struct usb_composite_dev *cdev)
{
	ghid_cleanup();
	return 0;
}

static int __init hidg_plat_driver_probe(struct platform_device *pdev)
{
	struct hidg_func_descriptor *func = dev_get_platdata(&pdev->dev);
	struct hidg_func_node *entry;

	if (!func) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -ENODEV;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->func = func;
	list_add_tail(&entry->node, &hidg_func_list);

	return 0;
}

static int hidg_plat_driver_remove(struct platform_device *pdev)
{
	struct hidg_func_node *e, *n;

	list_for_each_entry_safe(e, n, &hidg_func_list, node) {
		list_del(&e->node);
		kfree(e);
	}

	return 0;
}


/****************************** Some noise ******************************/


static __refdata struct usb_composite_driver hidg_driver = {
	.name		= "g_hid",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= hid_bind,
	.unbind		= __exit_p(hid_unbind),
};

static struct platform_driver hidg_plat_driver = {
	.remove		= hidg_plat_driver_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "hidg",
	},
};


MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Fabien Chouteau, Peter Korsgaard, Jed");
MODULE_LICENSE("GPL");

/* Report IDs for the various devices */
#define REPORT_ID_KEYBOARD      0x01
#define REPORT_ID_MOUSE         0x02
#define REPORT_ID_TABLET        0x03
#define REPORT_ID_MULTITOUCH    0x04
#define REPORT_ID_STYLUS        0x05
#define REPORT_ID_PUCK          0x06
#define REPORT_ID_FINGER        0x07
#define REPORT_ID_MT_MAX_COUNT  0x10
#define REPORT_ID_CONFIG        0x11
#define REPORT_ID_INVALID       0xff

/* This is a relative mouse with 5 buttons and a vertical wheel. */
#define MOUSE								\
	0x05, 0x01,                   /* USAGE_PAGE (Generic Desktop)     */ \
		0x09, 0x02,                 /* USAGE (Mouse)                    */ \
		0xa1, 0x01,                 /* COLLECTION (Application)         */ \
		0x85, REPORT_ID_MOUSE,      /*   REPORT_ID (2)                  */ \
		0x09, 0x01,                 /*   USAGE (Pointer)                */ \
		0xa1, 0x00,                 /*   COLLECTION (Physical)          */ \
		0x05, 0x09,                 /*     USAGE_PAGE (Button)          */ \
		0x19, 0x01,                 /*     USAGE_MINIMUM (Button 1)     */ \
		0x29, 0x05,                 /*     USAGE_MAXIMUM (Button 5)     */ \
		0x15, 0x00,                 /*     LOGICAL_MINIMUM (0)          */ \
		0x25, 0x01,                 /*     LOGICAL_MAXIMUM (1)          */ \
		0x95, 0x05,                 /*     REPORT_COUNT (5)             */ \
		0x75, 0x01,                 /*     REPORT_SIZE (1)              */ \
		0x81, 0x02,                 /*     INPUT (Data,Var,Abs)         */ \
		0x95, 0x01,                 /*     REPORT_COUNT (1)             */ \
		0x75, 0x03,                 /*     REPORT_SIZE (3)              */ \
		0x81, 0x03,                 /*     INPUT (Cnst,Var,Abs)         */ \
		0x05, 0x01,                 /*     USAGE_PAGE (Generic Desktop) */ \
		0x09, 0x30,                 /*     USAGE (X)                    */ \
		0x09, 0x31,                 /*     USAGE (Y)                    */ \
		0x09, 0x38,                 /*     USAGE (Z)                    */ \
		0x15, 0x81,                 /*     LOGICAL_MINIMUM (-127)       */ \
		0x25, 0x7f,                 /*     LOGICAL_MAXIMUM (127)        */ \
		0x75, 0x08,                 /*     REPORT_SIZE (8)              */ \
		0x95, 0x03,                 /*     REPORT_COUNT (3)             */ \
		0x81, 0x06,                 /*     INPUT (Data,Var,Rel)         */ \
		0x95, 0x01,                 /*     REPORT_COUNT (1)             */ \
		0x75, 0x08,                 /*     REPORT_SIZE (8)              */ \
		0x81, 0x03,                 /*     INPUT (Cnst,Var,Abs)         */ \
		0xc0,                       /*   END_COLLECTION                 */ \
		0xc0                        /* END_COLLECTION                   */

struct hidg_func_descriptor my_hid_data = {
	.subclass= 0, /* No subclass */
	.protocol= 0,
	.report_length= 8,
	.report_desc_length= 144,
	/* Length without the mouse: */
	/* .report_desc_length= 84, */
	.report_desc= {
		MOUSE,
		0x05, 0x0D,         /*  Usage Page (Digitizer),             */
		0x09, 0x04,         /*  Usage (Touchscreen),                */
		0xA1, 0x01,         /*  Collection (Application),           */
		0x85, REPORT_ID_MULTITOUCH, /* Report ID (4),               */
		0x09, 0x22,         /*      Usage (Finger),                 */
		0xA1, 0x00,         /*      Collection (Physical),          */
		0x09, 0x42,         /*          Usage (Tip Switch),         */
		0x15, 0x00,         /*          Logical Minimum (0),        */
		0x25, 0x01,         /*          Logical Maximum (1),        */
		0x75, 0x01,         /*          Report Size (1),            */
		0x95, 0x01,         /*          Report Count (1),           */
		0x81, 0x02,         /*          Input (Variable),           */
		0x09, 0x32,         /*          Usage (In Range),           */
		0x81, 0x02,         /*          Input (Variable),           */
		0x09, 0x37,         /*          Usage (Data Valid),         */
		0x81, 0x02,         /*          Input (Variable),           */
		0x25, 0x1F,         /*          Logical Maximum (31),       */
		0x75, 0x05,         /*          Report Size (5),            */
		0x09, 0x51,         /*          Usage (51h),                */
		0x81, 0x02,         /*          Input (Variable),           */
		0x05, 0x01,         /*          Usage Page (Desktop),       */
		0x55, 0x0E,         /*          Unit Exponent (14),         */
		0x65, 0x11,         /*          Unit (Centimeter),          */
		0x35, 0x00,         /*          Physical Minimum (0),       */
		0x75, 0x10,         /*          Report Size (16),           */
		0x46, 0x56, 0x0A,   /*          Physical Maximum (2646),    */
		0x26, 0xFF, 0x0F,   /*          Logical Maximum (4095),     */
		0x09, 0x30,         /*          Usage (X),                  */
		0x81, 0x02,         /*          Input (Variable),           */
		0x46, 0xB2, 0x05,   /*          Physical Maximum (1458),    */
		0x26, 0xFF, 0x0F,   /*          Logical Maximum (4095),     */
		0x09, 0x31,         /*          Usage (Y),                  */
		0x81, 0x02,         /*          Input (Variable),           */
		0x05, 0x0D,         /*          Usage Page (Digitizer),     */
		0x75, 0x08,         /*          Report Size (8),            */
		0x85, REPORT_ID_MT_MAX_COUNT, /* Report ID (10),            */
		0x09, 0x55,         /*          Usage (55h),                */
		0x25, 0x10,         /*          Logical Maximum (16),       */
		0xB1, 0x02,         /*          Feature (Variable),         */
		0xC0,               /*      End Collection,                 */
		0xC0                /*  End Collection                      */
	}
};

static int create_new_hidg(void);

static ssize_t hidg_new(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	create_new_hidg();

	return 0;
}

static DEVICE_ATTR(new_superhid, S_IRUGO, hidg_new, NULL);

static int create_new_hidg(void)
{
	static int id = 0;
	struct platform_device *dev;
	int ret;
	struct hidg_device_node *entry;

	dev = platform_device_alloc("hidg", id);
	dev->dev.platform_data = &my_hid_data;

	ret = platform_device_add(dev);

	if (ret)
	{
		printk("SuperHID Gadget registration failed\n");
		return -1;
	}

	/* ISUCK: Creating the sysfs node if this is the first hidg */
	if (id == 0)
	  ret = device_create_file(&dev->dev, &dev_attr_new_superhid);

	entry = kzalloc(sizeof(struct hidg_device_node), GFP_KERNEL);
	if (!entry)
	  return -ENOMEM;
	entry->dev = dev;
	list_add_tail(&entry->node, &hidg_devices);

	return id++;
}

static int __init hidg_init(void)
{
	int status;

	status = create_new_hidg();
	if ( status < 0)
		return status;

	status = platform_driver_probe(&hidg_plat_driver,
				       hidg_plat_driver_probe);
	if (status < 0)
		return status;

	status = usb_composite_probe(&hidg_driver);
	if (status < 0)
		platform_driver_unregister(&hidg_plat_driver);

	return status;
}
module_init(hidg_init);

static void __exit hidg_cleanup(void)
{
	platform_driver_unregister(&hidg_plat_driver);
	usb_composite_unregister(&hidg_driver);
}
module_exit(hidg_cleanup);
