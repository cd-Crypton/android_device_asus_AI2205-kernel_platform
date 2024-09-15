/* Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Edit by ASUS Deeo, deeo_ho@asus.com
 * V10
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/hidraw.h>
#include <linux/usb.h>
#include <linux/time.h>

#include "rog_accy_driver.h"

//#if defined(CONFIG_DRM)
#include <drm/drm_panel.h>
//#endif
#include <linux/soc/qcom/panel_event_notifier.h>
void rog_accy_uevent(void);

//extern int set_HDC2010_INT_TH(int enable);
#define set_HDC2010_INT_TH(a) {}

static u8 pogo_mutex_state = 0;

/*
 * 	gDongleType
 * 	- 1: Error
 * 	0 	: No Insert
 * 	1 	: InBox5
 *  2   : ERROR
 *  3   : Other
 * 	11 	: Fan Dongle 6
 * 255  : Default status
 */
uint8_t gDongleType=0;
EXPORT_SYMBOL(gDongleType);

/*
 * 	gPanelStatusForHdcpWork
 * 	0 	: Phone panel off
 * 	1 	: Phone panel on
 */
uint8_t gPanelStatusForHdcpWork=0;
EXPORT_SYMBOL(gPanelStatusForHdcpWork);

uint8_t gInboxPowerSwap=1;
EXPORT_SYMBOL(gInboxPowerSwap);

uint8_t gPanelStatus=0;
EXPORT_SYMBOL(gPanelStatus);

//#if defined(CONFIG_DRM)
//static struct drm_panel *active_panel;
static void rog_accy_panel_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *event, void *client_data);
//#endif
/*
static int drm_check_dt(struct device_node *np)
{
    int i = 0;
    int count = 0;
    int retry = 10;
    struct device_node *node = NULL;
    struct drm_panel *panel = NULL;

    count = of_count_phandle_with_args(np, "panel", NULL);
    if (count <= 0) {
        printk("[ROG_ACCY] find drm_panel count(%d) fail", count);
        return -ENODEV;
    }

    for (retry = 10;retry >0;retry--){
	printk("[ROG_ACCY] retry count(%d)", retry);
	for (i = 0; i < count; i++) {
	    node = of_parse_phandle(np, "panel", i);
	    panel = of_drm_find_panel(node);
	    of_node_put(node);
	    //FTS_ERROR("IS_ERR(panel)=%d \n", IS_ERR(panel));
	    //FTS_ERROR("node=%p \n", node);
	    //FTS_ERROR("drm_panel=%p \n", panel);

	    if (!IS_ERR(panel)) {
		printk("[ROG_ACCY] find drm_panel successfully");
		active_panel = panel;
		return 0;
	    }
	}
	msleep(2000);
    }

    printk("[ROG_ACCY] no find drm_panel");
    return -ENODEV;
}
*/
static void rog_accy_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification, void *client_data)
{
	//printk("[ROG_ACCY] %s +\n",__func__);
	if (!notification) {
		pr_err("Invalid notification\n");
		return;
	}

	printk("Notification type:%d, early_trigger:%d",
			notification->notif_type,
			notification->notif_data.early_trigger);
	if(notification->notif_data.early_trigger){
		return;
	}
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		printk("[ROG_ACCY] DRM_PANEL_EVENT_UNBLANK\n");
		gPanelStatus = DRM_PANEL_EVENT_UNBLANK;
		//set_HDC2010_INT_TH(0);
		break;
	case DRM_PANEL_EVENT_BLANK:
		printk("[ROG_ACCY] DRM_PANEL_EVENT_BLANK\n");
		gPanelStatus = DRM_PANEL_EVENT_BLANK;
		//set_HDC2010_INT_TH(1);
		break;
	case DRM_PANEL_EVENT_BLANK_LP:
		printk("[ROG_ACCY] DRM_PANEL_EVENT_BLANK_LP\n");
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		printk("[ROG_ACCY] DRM_PANEL_EVENT_FPS_CHANGE\n");
		break;
	default:
		printk("[ROG_ACCY] notification serviced\n");
		break;
	}
}
static void rog_accy_register_for_panel_events(struct device_node *dp,
					struct rog_accy_data *accy_data)
{
	const char *touch_type;
	int rc = 0;
	void *cookie = NULL;

	//dev_err(of_find_device_by_node(dp), "%s: [ROG_ACCY] rog_accy_register_for_panel_events+\n", __func__);

	rc = of_property_read_string(dp, "accy,type",
						&touch_type);
	if (rc) {
		printk("[ROG_ACCY] %s: No type\n", __func__);
		//dev_err(of_find_device_by_node(dp), "[ROG_ACCY] %s: No type\n", __func__);
		return;
	}
	if (strcmp(touch_type, "primary")) {
		pr_err("Invalid touch type\n");
		//dev_err(of_find_device_by_node(dp), "[ROG_ACCY] %s: Invalid touch type\n\n", __func__);
		return;
	}

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_ROG_ACCY, 0,
			&rog_accy_panel_notifier_callback, accy_data);
	if (!cookie) {
		pr_err("[ROG_ACCY] Failed to register for panel events\n");
		//dev_err(of_find_device_by_node(dp), "[ROG_ACCY] %s:Failed to register for panel events\n", __func__);
		return;
	}
/*
	printk("[ROG_ACCY] registered for panel notifications panel: 0x%x\n",
			active_panel);
*/
	accy_data->notifier_cookie = cookie;
	//dev_err(of_find_device_by_node(dp), "%s: [ROG_ACCY] rog_accy_register_for_panel_events-\n", __func__);
}

/*
static ssize_t sync_state_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	printk("[ROG_ACCY][EXTCON] old state : %d, new state : %d\n", accy_extcon->state, val);
	switch(val) {
		case EXTRA_DOCK_STATE_UNDOCKED:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_UNDOCKED\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_UNDOCKED);
		break;
		case EXTRA_DOCK_STATE_ASUS_INBOX:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_INBOX\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_ASUS_INBOX);
		break;
		case EXTRA_DOCK_STATE_ASUS_STATION:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_STATION\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_ASUS_STATION);
		break;
		case EXTRA_DOCK_STATE_ASUS_DT:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_DT\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_ASUS_DT);
		break;
		case EXTRA_DOCK_STATE_BACKGROUND_PAD:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_BACKGROUND_PAD\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_BACKGROUND_PAD);
		break;
		case EXTRA_DOCK_STATE_ASUS_2nd_INBOX:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_2nd_INBOX\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_ASUS_2nd_INBOX);
		break;

		case EXTRA_DOCK_STATE_ASUS_OTHER:
		default:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_OTHER\n");
			asus_extcon_set_state_sync(accy_extcon, EXTRA_DOCK_STATE_ASUS_OTHER);
		break;
	}

	pogo_mutex_state = 0;
	//printk("[ROG_ACCY] pogo_sema up!!! %d\n", val);
	//up(&g_rog_accy_data->pogo_sema);
	return count;
}

static ssize_t sync_state_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[ROG_ACCY][EXTCON] current state : %d\n", accy_extcon->state);

	return snprintf(buf, PAGE_SIZE,"%d\n", accy_extcon->state);
}
*/
void rog_accy_uevent(void){
	
	u8 type;
	type = gDongleType;

	if (type == Dongle_default_status){
		printk("[ROG_ACCY] type = 255, fake dongle type\n");
		return;
	}

	if (pogo_mutex_state && type == Dongle_NO_INSERT){
		printk("[ROG_ACCY] type : %d, pogo_mutex_state : %d, force unlock!!\n", type, pogo_mutex_state);
		pogo_mutex_state = 0;
		//printk("[ROG_ACCY] pogo_sema up, %d!!!\n", type);
		//up(&g_rog_accy_data->pogo_sema);
	}
		
	//down(&g_rog_accy_data->pogo_sema);
	//printk("[ROG_ACCY] pogo_sema down, %d!!!\n", type);
	//pogo_mutex_state = 1;
	kobject_uevent(&g_rog_accy_data->dev->kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL(rog_accy_uevent);

enum asus_dongle_type pre_dongletype = 255;

static ssize_t gDongleType_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[ROG_ACCY] gDongleType_show : %d\n", gDongleType);
	return sprintf(buf, "%d\n", gDongleType);
}

static ssize_t gDongleType_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int val;
	sscanf(data, "%d", &val);
	printk("[ROG_ACCY] gDongleType_store : %d\n", val);

	switch(val) {
		case Dongle_NO_INSERT:
			gDongleType = Dongle_NO_INSERT;
		break;
		case Dongle_INBOX5:
			gDongleType = Dongle_INBOX5;
		break;
		case Dongle_FANDG6:
			gDongleType = Dongle_FANDG6;
		break;
		case Dongle_ERROR:
			gDongleType = Dongle_ERROR;
		break;
		case Dongle_Others:
		default:
			printk("[ROG_ACCY] NO Recognize Dongle Type!!! Set gDongleType as Dongle_Others!\n");
			gDongleType = Dongle_Others;
			break;
	}

	if (pogo_mutex_state)
	{
		printk("[ROG_ACCY] pogo_mutex_state : %d, skip send uevent.\n", pogo_mutex_state);
		return count;
	}

	//down(&g_rog_accy_data->pogo_sema);
	//printk("[ROG_ACCY] pogo_sema down!!! %d\n", val);
	//pogo_mutex_state = 1;

	rog_accy_uevent();
	kobject_uevent(&g_rog_accy_data->dev->kobj, KOBJ_CHANGE);
	return count;
}

void vph_output_side_port(int val)
{
	if (val > 0) {
		printk("[ROG_ACCY] VPH_CTRL[%d] set HIGH.\n", g_rog_accy_data->vph_ctrl);
		gpio_set_value(g_rog_accy_data->vph_ctrl, 1);
	}else {
		printk("[ROG_ACCY] VPH_CTRL[%d] set LOW.\n", g_rog_accy_data->vph_ctrl);
		gpio_set_value(g_rog_accy_data->vph_ctrl, 0);
	}
}
EXPORT_SYMBOL_GPL(vph_output_side_port);

int vph_get_status(void)
{
	int ret = 0;

	ret = gpio_get_value(g_rog_accy_data->vph_ctrl);
	if (ret){
		printk("[ROG_ACCY] VPH_CTRL[%d] get HIGH.\n", g_rog_accy_data->vph_ctrl);
	}else{
		printk("[ROG_ACCY] VPH_CTRL[%d] get LOW.\n", g_rog_accy_data->vph_ctrl);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(vph_get_status);

static int get_AI2205_hw_id()
{
	int hwid = 0;
	u32 board_id[2] = { 0 };
	struct device_node *root;

	root = of_find_node_by_path("/");
	if (!of_property_read_u32_array(root, "qcom,board-id", board_id, 2)) {
		hwid = board_id[0];
		printk("[ROG_ACCY] %s hwid=%d\n", __func__, hwid);
	}

	if (hwid == AI2205_EVB)
		printk("[ROG_ACCY]HW = EVB\n");
	else if (hwid == AI2205_SR1)
		printk("[ROG_ACCY]HW = SR1\n");
	else if (hwid == AI2205_SR2)
		printk("[ROG_ACCY]HW = SR2\n");
	else if (hwid == AI2205_ER1)
		printk("[ROG_ACCY]HW = ER1\n");
	else if (hwid == AI2205_ER2)
		printk("[ROG_ACCY]HW = ER2\n");
	else
		printk("[ROG_ACCY]HW = UnKnow\n");
	return hwid;
}

static ssize_t vph_ctrl_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[ROG_ACCY] VPH_CTRL[%d] = %s\n", g_rog_accy_data->vph_ctrl, (gpio_get_value(g_rog_accy_data->vph_ctrl) == 0)?"LOW":"HIGH");
	return snprintf(buf, PAGE_SIZE,"%d\n", gpio_get_value(g_rog_accy_data->vph_ctrl));
}

static ssize_t vph_ctrl_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		printk("[ROG_ACCY][%s] VPH_CTRL[%d] set HIGH.\n", __func__, g_rog_accy_data->vph_ctrl);
		vph_output_side_port(1);
	}else {
		printk("[ROG_ACCY][%s] VPH_CTRL[%d] set LOW.\n", __func__, g_rog_accy_data->vph_ctrl);
		vph_output_side_port(0);
	}

	return count;
}

static ssize_t inbox_power_swap_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[ROG_ACCY] gInboxPowerSwap = %s\n", gInboxPowerSwap?"ON":"OFF");
	return snprintf(buf, PAGE_SIZE,"%d\n", gInboxPowerSwap);
}

static ssize_t inbox_power_swap_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u8 val;

	ret = kstrtou8(data, 10, &val);
	if (ret)
		return ret;

	gInboxPowerSwap = val;
	printk("[ROG_ACCY]set gInboxPowerSwap = %d\n",gInboxPowerSwap);
	return count;
}
/*
static ssize_t pogo_mutex_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		if(0) {
			printk("[ROG_ACCY] pogo_mutex is already lock.\n");
		} else {
			//down(&g_rog_accy_data->pogo_sema);
			printk("[ROG_ACCY] pogo_sema down!!!\n");

			//pogo_mutex_state = 1;
		}
	}else {
		if(1) {
			pogo_mutex_state = 0;
			printk("[ROG_ACCY] pogo_sema up!!!\n");
			//up(&g_rog_accy_data->pogo_sema);
		} else {
			printk("[ROG_ACCY] pogo_mutex is already unlock.\n");
		}
	}

	return count;
}

static ssize_t pogo_mutex_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[ROG_ACCY] pogo_mutex_show : %d\n", pogo_mutex_state);

	return snprintf(buf, PAGE_SIZE,"%d\n", pogo_mutex_state);
}
*/
static DEVICE_ATTR(gDongleType, S_IRUGO | S_IWUSR, gDongleType_show, gDongleType_store);
static DEVICE_ATTR(VPH_CTRL, S_IRUGO | S_IWUSR, vph_ctrl_show, vph_ctrl_store);
static DEVICE_ATTR(Inbox7PowerSwap, S_IRUGO | S_IWUSR, inbox_power_swap_show, inbox_power_swap_store);
//static DEVICE_ATTR(pogo_mutex, S_IRUGO | S_IWUSR, pogo_mutex_show, pogo_mutex_store);
//static DEVICE_ATTR(sync_state, S_IRUGO | S_IWUSR, sync_state_show, sync_state_store);

static struct attribute *rog_accy_attrs[] = {
	&dev_attr_gDongleType.attr,
	&dev_attr_VPH_CTRL.attr,
	&dev_attr_Inbox7PowerSwap.attr,
	//&dev_attr_pogo_mutex.attr,
	//&dev_attr_sync_state.attr,
	NULL
};

const struct attribute_group rog_accy_group = {
	.attrs = rog_accy_attrs,
};

/*
extern struct hidraw *rog6_inbox_hidraw;
void hid_switch_usb_autosuspend(bool flag){
	struct hid_device *hdev;
	struct usb_interface *intf;

	if (rog6_inbox_hidraw == NULL || g_rog_accy_data->lock) {
		printk("[ROG_ACCY] rog6_inbox_hidraw is NULL or lock %d\n", g_rog_accy_data->lock);
		return;
	}

	hdev = rog6_inbox_hidraw->hid;
	intf = to_usb_interface(hdev->dev.parent);

	printk("[ROG_ACCY] hid_swithc_usb_autosuspend %d\n", flag);
	if(flag) {
		//usb_enable_autosuspend(interface_to_usbdev(intf));
	}else {
		//usb_disable_autosuspend(interface_to_usbdev(intf));
	}

	return;
}
*/

void GamePad_connect(int option){
	char *type_uevent[2];
	char str[30];
	int type = 0;

	switch(option) {
		case 0:
			printk("[ROG_ACCY][%s] gamepad disconnect \n",__func__);
			type = Gamepad_NO_INSERT;
		break;
		case 1:
			printk("[ROG_ACCY][%s] Kunai connect \n",__func__);
			type = Gamepad_KUNAI_Series;
		break;
		case 2:
			printk("[ROG_ACCY][%s] GU200A connect \n",__func__);
			type = Gamepad_GU200A;
		break;
		default:
			printk("[ROG_ACCY][%s] unknuwed option \n",__func__);
			type = Gamepad_NO_INSERT;
			return;
		break;
	}

	sprintf(str, "GAMEPAD=%d", type);
	type_uevent[0] = str;
	type_uevent[1] = NULL;
	kobject_uevent_env(&g_rog_accy_data->dev->kobj, KOBJ_CHANGE, type_uevent);

	return;
}
EXPORT_SYMBOL_GPL(GamePad_connect);

void FANDG_connect(int option)
{
	int extcon_val = EXTRA_DOCK_STATE_UNDOCKED;
	char *dongle_type_uevent[2];
	char str[30];

	switch(option) {
		case 0:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_UNDOCKED, PDID %s\n", (FANDG_PDID_detect == true)?"detect":"not detect");
			#ifdef ASUS_AI2205_PROJECT
			if (!FANDG_USBID_detect) {
				gDongleType = Dongle_NO_INSERT;
				extcon_val = EXTRA_DOCK_STATE_UNDOCKED;
				// Turn off VPH & HUB Mode
				//vph_output_side_port(0);	//HW removed
				//Inbox_role_switch(0);
				break;
			}else {
				printk("[ROG_ACCY][EXTCON] PDID %s, USBID %s, skip!\n", (FANDG_PDID_detect == true)?"detect":"non-detect", (FANDG_USBID_detect == true)?"detect":"non-detect");
				return;
			}
			#else
			gDongleType = Dongle_NO_INSERT;
			extcon_val = EXTRA_DOCK_STATE_UNDOCKED;
			#endif
		break;
		case 1:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_2nd_INBOX, PDID %s\n", (FANDG_PDID_detect == true)?"detect":"not detect");
			#ifdef ASUS_AI2205_PROJECT
			if (FANDG_USBID_detect /*&& FANDG_PDID_detect*/) {
				gDongleType = Dongle_FANDG6;
				extcon_val = EXTRA_DOCK_STATE_ASUS_2nd_INBOX;
			}else {
				printk("[ROG_ACCY][EXTCON] PDID %s, USBID %s, skip!\n", (FANDG_PDID_detect == true)?"detect":"non-detect", (FANDG_USBID_detect == true)?"detect":"non-detect");
				return;
			}
			#else
			gDongleType = Dongle_FANDG6;
			extcon_val = EXTRA_DOCK_STATE_ASUS_2nd_INBOX;
			#endif
		break;
		case 2:
			printk("[ROG_ACCY][EXTCON] EXTRA_DOCK_STATE_ASUS_INBOX7, PDID %s\n", (FANDG_PDID_detect == true)?"detect":"not detect");
			#ifdef ASUS_AI2205_PROJECT
			if (FANDG_USBID_detect /*&& FANDG_PDID_detect*/) {
				gDongleType = Dongle_FANDG7;
				extcon_val = EXTRA_DOCK_STATE_ASUS_INBOX7;
			}else {
				printk("[ROG_ACCY][EXTCON] PDID %s, USBID %s, skip!\n", (FANDG_PDID_detect == true)?"detect":"non-detect", (FANDG_USBID_detect == true)?"detect":"non-detect");
				return;
			}
			#else
			gDongleType = Dongle_FANDG7;
			extcon_val = EXTRA_DOCK_STATE_ASUS_INBOX7;
			#endif
		break;
		default:
			printk("[ROG_ACCY][EXTCON] unknow option %d, ignore!!!\n", option);
			return;
		break;
	};
	
	if (extcon_val != accy_extcon->state){
		sprintf(str, "DONGLE_TYPE=%d", gDongleType);
		dongle_type_uevent[0] = str;
		dongle_type_uevent[1] = NULL;
		kobject_uevent_env(&g_rog_accy_data->dev->kobj, KOBJ_CHANGE, dongle_type_uevent);
		msleep(50);
		asus_extcon_set_state_sync(accy_extcon, extcon_val); 
	}else
		printk("[ROG_ACCY][EXTCON] Last state %d, Current State %d, No changed, skip!\n", accy_extcon->state, extcon_val);
}
EXPORT_SYMBOL_GPL(FANDG_connect);

static int rog_accy_parse_dt(struct device *dev, struct rog_accy_data *rog_accy_device){
	struct device_node *np = dev->of_node;
	int retval=0;

	// Set VPH_CTRL
	rog_accy_device->vph_ctrl = of_get_named_gpio(np, "accy,vph_ctrl", 0);
	if ( gpio_is_valid(rog_accy_device->vph_ctrl) ) {
		printk("[ROG_ACCY] Request VPH_CTRL config.\n");
		dev_err(dev, "%s: [ROG_ACCY] Request VPH_CTRL config.\n", __func__);
		retval = gpio_request(rog_accy_device->vph_ctrl, "VPH_CTRL");
		if (retval) {
			printk("[ROG_ACCY] VPH_CTRL gpio_request, err %d\n", retval);
			dev_err(dev, "%s: [ROG_ACCY] VPH_CTRL gpio_request, err %d\n", __func__,retval);
		}
		printk("[ROG_ACCY] VPH_CTRL default off.\n");
		dev_err(dev, "%s: [ROG_ACCY] VPH_CTRL default off.\n", __func__);
		retval = gpio_direction_output(rog_accy_device->vph_ctrl, 0);
		if (retval){
			printk("[ROG_ACCY] VPH_CTRL output high, err %d\n", retval);
			dev_err(dev, "%s: [ROG_ACCY] VPH_CTRL output high, err %d\n", __func__, retval);
		}
		gpio_set_value(rog_accy_device->vph_ctrl, 0);
	}
/*
	// Parse pinctrl state
	if ( gpio_is_valid(rog_accy_device->vph_ctrl) ){
		printk("[ROG_ACCY] Get the pinctrl node.\n");
		// Get the pinctrl node
		rog_accy_device->pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR_OR_NULL(rog_accy_device->pinctrl)) {
		     dev_err(dev, "%s: Failed to get pinctrl\n", __func__);
		}

		// Get the default state
		printk("[ROG_ACCY] Get pinctrl defaul state.\n");
		rog_accy_device->pins_default = pinctrl_lookup_state(rog_accy_device->pinctrl, "vph_ctrl_default");
		if (IS_ERR_OR_NULL(rog_accy_device->pins_default)) {
			dev_err(dev, "%s: Failed to get pinctrl active state\n", __func__);
		}

		// Set the default state
		printk("[ROG_ACCY] Set defaul state.\n");
		retval = pinctrl_select_state(rog_accy_device->pinctrl, rog_accy_device->pins_default);
		if (retval)
			printk("[ROG_ACCY] pinctrl_select_state err:%d\n", retval);
	}
*/
	return 0;
} 

static int rog_accy_probe(struct platform_device *pdev)
{
	int status = 0;
	struct device *dev = &pdev->dev;
	struct rog_accy_data *rog_accy_device;
	int retval;
//	enum asus_dongle_type type = Dongle_default_status;

	printk("[ROG_ACCY] rog_accy_probe+ 1012\n");
	dev_err(dev, "%s: [ROG_ACCY] rog_accy_probe 1012\n", __func__);

	g_HWID = get_AI2205_hw_id();

	rog_accy_device = kzalloc(sizeof(*rog_accy_device), GFP_KERNEL);
	if (rog_accy_device == NULL) {
		printk("[ROG_ACCY] alloc ROG_ACCY data fail.\r\n");
		goto kmalloc_failed;
	}

	rog_accy_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(rog_accy_class)) {
		printk("[ROG_ACCY] rog_accy_probe: class_create() is failed - unregister chrdev.\n");
		goto class_create_failed;
	}
	
	dev = device_create(rog_accy_class, &pdev->dev,
			    rog_accy_device->devt, rog_accy_device, "dongle");
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	printk("[ROG_ACCY] rog_accy_probe: device_create() status %d\n", status);

	status = sysfs_create_group(&pdev->dev.kobj, &rog_accy_group);
	printk("[ROG_ACCY] rog_accy_probe: sysfs_create_group() status %d\n", status);

// Extcon file node register
	accy_extcon = extcon_asus_dev_allocate();
	if (IS_ERR(accy_extcon)) {
		status = PTR_ERR(accy_extcon);
		printk("[ROG_ACCY] failed to allocate accy_extcon status %d\n", status);
	}
	accy_extcon->name = "dock";  // assing extcon class name

	status = extcon_asus_dev_register(accy_extcon);
	if (status < 0) {
		printk("[ROG_ACCY] failed to register accy_extcon status %d\n", status);
	}

// Parse platform data from dtsi
	retval = rog_accy_parse_dt(&pdev->dev, rog_accy_device);
	if (retval) {
		printk("[ROG_ACCY] rog_accy_parse_dt get fail !!!\n");
		goto skip_pinctrl;
	}

    sema_init(&rog_accy_device->pogo_sema, 1);

	rog_accy_device->lock = false;
	rog_accy_device->dev = &pdev->dev;
	g_rog_accy_data = rog_accy_device;
/*
	retval = drm_check_dt(rog_accy_device->dev->of_node);
	if (retval) 
		printk("[ROG_ACCY]  parse drm-panel fail\n");
*/
	dev_err(dev, "%s: [ROG_ACCY] rog_accy_register_for_panel_events +++\n", __func__);
	rog_accy_register_for_panel_events(rog_accy_device->dev->of_node, rog_accy_device);
	dev_err(dev, "%s: [ROG_ACCY] rog_accy_register_for_panel_events ---\n", __func__);
	dev_err(dev, "%s: [ROG_ACCY] rog_accy_probe-\n", __func__);
	return 0;

skip_pinctrl:
class_create_failed:
kmalloc_failed:
	return -1;
}

static int rog_accy_remove(struct platform_device *pdev)
{
	printk("[ROG_ACCY] rog_accy_remove.\n");
//#if defined(CONFIG_DRM)
    if (/*active_panel && */g_rog_accy_data->notifier_cookie){
        panel_event_notifier_unregister(g_rog_accy_data->notifier_cookie);
	}
//#endif
	sysfs_remove_group(&pdev->dev.kobj, &rog_accy_group);
	device_destroy(rog_accy_class, g_rog_accy_data->devt);
	class_destroy(rog_accy_class);
	gpio_free(g_rog_accy_data->vph_ctrl);
	kfree(g_rog_accy_data);

	return 0;
}

int rog_accy_suspend(struct device *dev)
{
	return 0;
}

int rog_accy_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops rog_accy_pm_ops = {
	.suspend	= rog_accy_suspend,
	.resume	= rog_accy_resume,
};

static struct of_device_id dongle_match_table[] = {
	{ .compatible = "asus:rog_accy",},
	{ },
};

static struct platform_driver rog_accy_driver = {
	.driver = {
		.name = "rog_accy",
		.owner = THIS_MODULE,
		.pm	= &rog_accy_pm_ops,
		.of_match_table = dongle_match_table,
	},
	.probe         	= rog_accy_probe,
	.remove			= rog_accy_remove,
};

static int __init rog_accy_init(void)
{
	int ret;

	ret = platform_driver_register(&rog_accy_driver);
	if (ret != 0) {
		printk("[ROG_ACCY] rog_accy_init fail, Error : %d\n", ret);
	}
	
	return ret;
}
module_init(rog_accy_init);

static void __exit rog_accy_exit(void)
{
	platform_driver_unregister(&rog_accy_driver);
}
module_exit(rog_accy_exit);

MODULE_AUTHOR("ASUS Deeo Ho");
MODULE_DESCRIPTION("ROG Phone ACCY driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("asus:rog_accy");
