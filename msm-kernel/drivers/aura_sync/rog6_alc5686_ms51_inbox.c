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
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/time.h>
#include <linux/firmware.h>
#include <linux/kthread.h>
#include <drm/drm_panel.h>
#include <linux/soc/qcom/panel_event_notifier.h>

#define MS51_2LED_FW_PATH "ASUS_ROG_FAN7_2LED.bin"
#define MS51_3LED_FW_PATH "ASUS_ROG_FAN7_3LED.bin"
#define AURA_INBOX_PD_FILE_NAME "ASUS_ROG_FAN6_PD.bin"

#include "rog6_alc5686_ms51_inbox.h"

struct delayed_work	disable_autosuspend_work;
struct task_struct *latch_det_thread = NULL;
extern void wakeup_panel_by_pwrkey(void);

static void rog_inbox_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification, void *client_data)
{
	printk("[ROG7_INBOX] %s +\n",__func__);
	if (!notification) {
		pr_err("Invalid notification\n");
		return;
	}

	printk("[ROG7_INBOX] Notification type:%d, early_trigger:%d",
			notification->notif_type,
			notification->notif_data.early_trigger);
	if(notification->notif_data.early_trigger){
		return;
	}
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		printk("[ROG7_INBOX] DRM_PANEL_EVENT_UNBLANK\n");
		//wakeup MS51 and PD
		pd_wakeup(1);
		aura_lpm_enable(0);
		mps_lpm_mode_enable(0);
		break;
	case DRM_PANEL_EVENT_BLANK:
		printk("[ROG7_INBOX] DRM_PANEL_EVENT_BLANK\n");
		if( g_aura_lpm == 1){
			printk("[ROG7_INBOX] PD and MS51 enter LPM\n");
			pd_wakeup(0);
			aura_lpm_enable(1);
			mps_lpm_mode_enable(1);
		}
		break;
	case DRM_PANEL_EVENT_BLANK_LP:
		printk("[ROG7_INBOX] DRM_PANEL_EVENT_BLANK_LP\n");
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		printk("[ROG7_INBOX] DRM_PANEL_EVENT_FPS_CHANGE\n");
		break;
	default:
		printk("[ROG7_INBOX] notification serviced\n");
		break;
	}
}

static void rog_inbox_register_for_panel_events(struct inbox_drvdata *drvdata)
{
	void *cookie = NULL;

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_ROG_INBOX7, 0 /*active_panel*/,
			&rog_inbox_panel_notifier_callback, drvdata);
	if (!cookie) {
		pr_err("[ROG7_INBOX] Failed to register for panel events\n");
		return;
	}

	printk("[ROG7_INBOX] registered for panel notifications --\n");

	drvdata->notifier_cookie = cookie;
}

static u32 hid_report_id_aprom(u8 addr)
{
	u32 report_id = 0;

	switch (addr){
		case addr_0x16:	//2LED
			report_id = 0x0B;
		break;
		case addr_0x18: //3LED
			report_id = 0x0E;
		break;
		case addr_0x75:
			report_id = 0x10;
		break;
		case addr_0x40:
			report_id = 0x11;
		break;
		case addr_0x41:
			report_id = 0x12;
		break;
		case addr_0x28:	//PD ISP mode
			report_id = 0x13;
		break;
		case addr_0x64:	//PD
			report_id = 0x14;
		break;
		case addr_MPS:
			report_id = 0x15;
		break;
		case addr_0x28_long:	//PD ISP mode long command
			report_id = 0x17;
		break;
		case addr_set_codec_cali:
			report_id = 0x18;
		break;
		case addr_get_codec_cali:
			report_id = 0x19;
		break;
		case addr_keyboard_mode:
			report_id = 0x0D;
		break;
		default:
			printk("[ROG7_INBOX] unknown addr.\n");
		break;
	}
	
	return report_id;
}

static u32 hid_report_id_long_cmd(u8 addr)
{
	u32 report_id = 0;

	switch (addr){
		case addr_0x16:
			report_id = 0x0C;
		break;
		case addr_0x18:
			report_id = 0x0F;
		break;
		case addr_set_codec_cali:
			report_id = 0x18;
		break;
		case addr_get_codec_cali:
			report_id = 0x19;
		break;
		default:
			printk("[ROG7_INBOX] unknown addr.\n");
		break;
	}
	
	return report_id;
}

static int asus_usb_hid_write_extra(u8 repor_id, u8 *cmd, int cmd_len)
{
	struct hid_device *hdev;
	int ret = 0;
	char *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	if(FEATURE_WRITE_COMMAND_SIZE < cmd_len+3)
	{
		printk("[ROG7_INBOX] buffer not enough !\n");
		return -2;
	}
	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = repor_id;
	memcpy(&(buffer[1]), cmd, cmd_len);

//     for ( i=0; i<FEATURE_WRITE_COMMAND_SIZE; i++ )
//             printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);

	hid_hw_power(hdev, PM_HINT_FULLON);

	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	mutex_unlock(&hid_command_lock);
	return ret;
}

static int asus_usb_hid_write_aprom(u8 repor_id, u8 *cmd, int cmd_len)
{
	struct hid_device *hdev;
	int ret = 0;
//     int i = 0;
	char *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	if(FEATURE_WRITE_COMMAND_SIZE < cmd_len+3)
	{
		printk("[ROG7_INBOX] buffer not enough !\n");
		return -2;
	}
	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = repor_id;
	buffer[1] = 0x2;		//WRITE CMD
	buffer[2] = cmd_len;	//CMD LEN
	memcpy(&(buffer[3]), cmd, cmd_len);

//     for ( i=0; i<FEATURE_WRITE_COMMAND_SIZE; i++ )
//             printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);


	hid_hw_power(hdev, PM_HINT_FULLON);

	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	mutex_unlock(&hid_command_lock);
	return ret;
}

static int asus_usb_hid_read_aprom(u8 repor_id, u8 *cmd, int cmd_len, u8 *data)
{
	struct hid_device *hdev;
	int ret = 0;
//	int i = 0;
	char *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = repor_id;
	buffer[1] = 0x1;		//READ CMD
	buffer[2] = cmd_len;	//CMD_LEN
	memcpy(&(buffer[3]), cmd, cmd_len);

	//for ( i=0; i<FEATURE_WRITE_COMMAND_SIZE; i++ )
		//printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request HID_REQ_SET_REPORT fail: ret = %d\n", ret);

	msleep(0);
	ret = hid_hw_raw_request(hdev, buffer[0], data, FEATURE_READ_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request HID_REQ_GET_REPORT fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);

//	for ( i=0; i<FEATURE_READ_COMMAND_SIZE; i++ )
//		printk("[ROG7_INBOX][%s] data[%d] = 0x%02x\n", __func__, i, data[i]);

	kfree(buffer);
	mutex_unlock(&hid_command_lock);
	return ret;
}

static int asus_usb_hid_write_log_cmd(u8 repor_id, u8 *cmd, int cmd_len)
{
	struct hid_device *hdev;
	int ret = 0;
	//int i = 0;
	u8 *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}
	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_LONG_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = repor_id;
	buffer[1] = 0x2;		// WRITE CMD
	buffer[2] = cmd_len;	// CMD_LEN
	buffer[3] = 0;			// Fix 0x00
	memcpy(&(buffer[4]), cmd, cmd_len);

	//for ( i=0; i<FEATURE_WRITE_LONG_COMMAND_SIZE; i++ )
		//printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_LONG_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	mutex_unlock(&hid_command_lock);
	return ret;
}
/*
static int asus_usb_hid_write_long_cmd(u8 repor_id, u8 *cmd, int cmd_len)
{
	struct hid_device *hdev;
	int ret = 0;
//	int i = 0;
	u8 *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	buffer = kzalloc(FEATURE_WRITE_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_LONG_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = repor_id;
	buffer[1] = 0x2;		// WRITE CMD
	buffer[2] = cmd_len;	// CMD_LEN
	//buffer[3] = 0;			// Fix 0x00
	memcpy(&(buffer[3]), cmd, cmd_len);

//	for ( i=0; i<FEATURE_WRITE_LONG_COMMAND_SIZE; i++ )
//		printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_LONG_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);

	return ret;
}
*/
static int asus_usb_hid_read_long_cmd(u8 repor_id, u8 *cmd, int cmd_len, u8 *data, int data_len)
{
	struct hid_device *hdev;
	int ret = 0;
//	int i = 0;
	char *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_LONG_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = repor_id;
	buffer[1] = 0x1;		//READ CMD
	buffer[2] = cmd_len;	//CMD_LEN
	buffer[3] = data_len;	//DATA_LEN
	memcpy(&(buffer[4]), cmd, cmd_len);

//	for ( i=0; i<FEATURE_WRITE_LONG_COMMAND_SIZE; i++ )
//		printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_LONG_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request HID_REQ_SET_REPORT fail: ret = %d\n", ret);

	msleep(0);
	ret = hid_hw_raw_request(hdev, buffer[0], data, FEATURE_READ_LONG_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request HID_REQ_GET_REPORT fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);

//	for ( i=0; i<FEATURE_READ_LONG_COMMAND_SIZE; i++ )
//		printk("[ROG7_INBOX][%s] data[%d] = 0x%02x\n", __func__, i, data[i]);

	kfree(buffer);
	mutex_unlock(&hid_command_lock);
	return ret;
}

static ssize_t gpio8_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	struct hid_device *hdev;
	u8 *buffer;
	u8 key_state_tmp1, key_state_tmp2;
	
	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}
	
	key_state_tmp1 = key_state;
	
	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer, 0, 2);
	
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = 0x0A;
	buffer[1] = 0x08;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);

	kfree(buffer);

	key_state_tmp2 = key_state;
	key_state = key_state_tmp1;
	printk("[ROG7_INBOX] gpio8_show : %x\n", key_state_tmp2);
	return snprintf(buf, PAGE_SIZE,"%x\n", key_state_tmp2);
}

static ssize_t gpio8_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int ret;
	struct hid_device *hdev;
	u8 *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s, reg_val %d\n", __func__, reg_val);

	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer,0,2);

	buffer[0] = 0xA;
	if (reg_val > 0)
		buffer[1] = 0x01;
	else
		buffer[1] = 0x00;
		
	hdev = rog6_inbox_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);

	kfree(buffer);
	
	return count;
}

static ssize_t gpio9_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	struct hid_device *hdev;
	u8 *buffer;
	u8 key_state_tmp1, key_state_tmp2;
	
	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}
	
	key_state_tmp1 = key_state;
	
	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer, 0, 2);
	
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = 0x0A;
	buffer[1] = 0x09;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);

	kfree(buffer);

	key_state_tmp2 = key_state;
	key_state = key_state_tmp1;
	printk("[ROG7_INBOX] gpio9_show : %x\n", key_state_tmp2);
	return snprintf(buf, PAGE_SIZE,"%x\n", key_state_tmp2);
}

static ssize_t gpio9_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int ret;
	struct hid_device *hdev;
	u8 *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s, reg_val %d\n", __func__, reg_val);

	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer,0,2);

	buffer[0] = 0xA;
	if (reg_val > 0)
		buffer[1] = 0x91;
	else
		buffer[1] = 0x90;
		
	hdev = rog6_inbox_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	
	return count;
}

static ssize_t gpio10_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	struct hid_device *hdev;
	u8 *buffer;
	u8 key_state_tmp1, key_state_tmp2;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}
	
	key_state_tmp1 = key_state;
	
	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer, 0, 2);
	
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = 0x0A;
	buffer[1] = 0x0A;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);

	kfree(buffer);

	key_state_tmp2 = key_state;
	key_state = key_state_tmp1;
	printk("[ROG7_INBOX] gpio10_show : %x\n", key_state_tmp2);
	return snprintf(buf, PAGE_SIZE,"%x\n", key_state_tmp2);

}

static ssize_t gpio10_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int ret;
	struct hid_device *hdev;
	u8 *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s, reg_val %d\n", __func__, reg_val);

	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer,0,2);

	buffer[0] = 0xA;
	if (reg_val > 0)
		buffer[1] = 0xA1;
	else
		buffer[1] = 0xA0;
		
	hdev = rog6_inbox_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	
	return count;
}

static ssize_t gpio11_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	struct hid_device *hdev;
	u8 *buffer;
	u8 key_state_tmp1, key_state_tmp2;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}
	
	key_state_tmp1 = key_state;
	
	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer, 0, 2);
	
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = 0x0A;
	buffer[1] = 0x0B;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);

	kfree(buffer);

	key_state_tmp2 = key_state;
	key_state = key_state_tmp1;
	printk("[ROG7_INBOX] gpio11_show : %x\n", key_state_tmp2);
	return snprintf(buf, PAGE_SIZE,"%x\n", key_state_tmp2);

}

static ssize_t gpio11_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int ret;
	struct hid_device *hdev;
	u8 *buffer;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s, reg_val %d\n", __func__, reg_val);

	buffer = kzalloc(2, GFP_KERNEL);
	memset(buffer,0,2);

	buffer[0] = 0xA;
	if (reg_val > 0)
		buffer[1] = 0xB1;
	else
		buffer[1] = 0xB0;
		
	hdev = rog6_inbox_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, sizeof(buffer),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	
	return count;
}

static ssize_t led_test_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned char data[3] = {0};
	u32 reg_val;
	int err = 0;

	err = kstrtou32(buf, 10, &reg_val);
	if (err)
		return count;

	printk("[ROG7_INBOX] %s, reg_val %d\n", __func__, reg_val);
	
	data[0] = 0x80;
	data[1] = 0x21;
	data[2] = 0x04;

	g_2led_mode = 0x04;
	
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	msleep(10);
	
	data[0] = 0x80;
	data[1] = 0x2F;
	data[2] = 0x01;
		
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t red_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_red_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x10;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	msleep(10);
	
	data[1] = 0x13;
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	if (IC_switch == addr_0x16){
		g_2led_red = tmp;
	}else if (IC_switch == addr_0x18){
		msleep(10);
		data[1] = 0x16;
		err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		g_3led_red = tmp;
	}
	return count;
}

static ssize_t red_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x10;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t green_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_green_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x11;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	msleep(10);
	
	data[1] = 0x14;
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	if (IC_switch == addr_0x16){
		g_2led_green = tmp;
	}else if (IC_switch == addr_0x18){
		msleep(10);
		data[1] = 0x17;
		err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		g_3led_green = tmp;
	}

	return count;
}

static ssize_t green_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x11;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t blue_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_blue_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x12;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	msleep(10);
	
	data[1] = 0x15;
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	if (IC_switch == addr_0x16){
		g_2led_blue = tmp;
	}else if (IC_switch == addr_0x18){
		msleep(10);
		data[1] = 0x18;
		err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		g_3led_blue = tmp;
	}

	return count;
}

static ssize_t blue_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x12;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t red1_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_red_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x13;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t red1_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x13;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t green1_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_green_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x14;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t green1_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x14;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t blue1_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_blue_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x15;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t blue1_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);


	cmd[0] = 0x80;
	cmd[1] = 0x15;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t red2_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	if (IC_switch == addr_0x16){
		printk("[ROG7_INBOX] addr_0x16 not support LED3\n");
		return count;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_red_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x16;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t red2_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	if (IC_switch == addr_0x16){
		return snprintf(buf, PAGE_SIZE,"Not support on addr_0x16.\n");
	}

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x16;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t green2_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	if (IC_switch == addr_0x16){
		printk("[ROG7_INBOX] addr_0x16 not support LED3\n");
		return count;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_red_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x17;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t green2_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	if (IC_switch == addr_0x16){
		return snprintf(buf, PAGE_SIZE,"Not support on addr_0x16.\n");
	}

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x17;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t blue2_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val, tmp;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	if (IC_switch == addr_0x16){
		printk("[ROG7_INBOX] addr_0x16 not support LED3\n");
		return count;
	}

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] %s reg_val = %d.\n", __func__, reg_val);
	tmp = DIV_ROUND_CLOSEST(reg_val*g_2led_red_max, 255);
	//printk("[ROG7_INBOX] %s tmp = %d.\n", __func__, tmp);

	data[0] = 0x80;
	data[1] = 0x18;
	data[2] = tmp;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t blue2_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	if (IC_switch == addr_0x16){
		return snprintf(buf, PAGE_SIZE,"Not support on addr_0x16.\n");
	}

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x18;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%d] IC:%d, PWM:0x%02x (%d)\n", __func__, IC_switch, val, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t apply_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE,"%d\n", g_2led_apply);
}

static ssize_t apply_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &val);
	if (ret) {
		g_2led_apply = -1;
		return count;
	}

	//printk("[ROG7_INBOX] apply_store: %d\n", val);
	g_2led_apply = 0;
	
	if (val) {
		data[0] = 0x80;
		data[1] = 0x2F;
		data[2] = 0x01;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
		if (err < 0) {
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
			g_2led_apply = -1;
			return count;
		}

		if (IC_switch == addr_0x16)
			printk("[ROG7_INBOX] Send apply. IC:%d, RGB:%d %d %d, mode:%d, speed:%d, led_on:%d\n", IC_switch, g_2led_red, g_2led_green, g_2led_blue, g_2led_mode, g_2led_speed, g_led_on);
		else if (IC_switch == addr_0x18)
			printk("[ROG7_INBOX] Send apply. IC:%d, RGB:%d %d %d, mode:%d, speed:%d, led_on:%d\n", IC_switch, g_3led_red, g_3led_green, g_3led_blue, g_3led_mode, g_3led_speed, g_led_on);
	}
	else {
		printk("[ROG7_INBOX] don't send apply command\n");
	}

	return count;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x21;
	cmd[2] = 0x00;

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%s] IC:%d, Mode:%d\n", __func__, IC_switch, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t set_frame(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	if (val > 255){
		printk("[ROG7_INBOX] Frame should not over 255.\n");
		return count;
	}
	//printk("[ROG7_INBOX][%s] %d\n", __func__, val);

	data[0] = 0x80;
	data[1] = 0xF2;
	data[2] = val;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0) {
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
		return count;
	}

	return count;
}

static ssize_t get_frame(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0xF3;
	cmd[2] = 0x00;

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%s] IC:%d, Frame:%d\n", __func__, IC_switch, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t set_speed(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	if (val != 254 && val != 255 && val != 0){
		printk("[ROG7_INBOX] speed should be 0, 255, 254\n");
		return count;
	}
	//printk("[ROG7_INBOX][%s] %d\n", __func__, val);
	
	data[0] = 0x80;
	data[1] = 0x22;
	data[2] = val;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0) {
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
		return count;
	}

	if (IC_switch == addr_0x16)
		g_2led_speed = val;
	else if (IC_switch == addr_0x18)
		g_3led_speed = val;
	
	return count;
}

static ssize_t get_speed(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x80;
	cmd[1] = 0x22;
	cmd[2] = 0x00;

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%s] IC:%d, Speed:%d\n", __func__, IC_switch, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t set_cali_data(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int red_val = 0, green_val = 0, blue_val = 0;

	sscanf(buf, "%d %d %d", &red_val, &green_val, &blue_val);
	printk("[ROG7_INBOX][%d] %d, %d, %d\n", __func__, red_val, green_val, blue_val);

	if (IC_switch == addr_0x16){
		g_2led_red_max = red_val;
		g_2led_green_max = green_val;
		g_2led_blue_max = blue_val;
	}else if (IC_switch == addr_0x18){
		g_3led_red_max = red_val;
		g_3led_green_max = green_val;
		g_3led_blue_max = blue_val;
	}

	return count;
}

static ssize_t get_cali_data(struct device *dev, struct device_attribute *attr,char *buf)
{
	if (IC_switch == addr_0x16){
		printk("[ROG7_INBOX] IC:%d, R:%d, G:%d, B:%d\n", IC_switch, g_2led_red_max, g_2led_green_max, g_2led_blue_max);
		return snprintf(buf, PAGE_SIZE,"R:%d, G:%d, B:%d\n", g_2led_red_max, g_2led_green_max, g_2led_blue_max);
	}else if (IC_switch == addr_0x18){
		printk("[ROG7_INBOX] IC:%d, R:%d, G:%d, B:%d\n", IC_switch, g_3led_red_max, g_3led_green_max, g_3led_blue_max);
		return snprintf(buf, PAGE_SIZE,"R:%d, G:%d, B:%d\n", g_3led_red_max, g_3led_green_max, g_3led_blue_max);
	}
	
	return snprintf(buf, PAGE_SIZE,"IC:%d, Choose wrong IC.\n", IC_switch);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] mode_store: %d\n", val);

	data[0] = 0x80;
	data[1] = 0x21;
	data[2] = val;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	if(IC_switch == addr_0x16){
		g_2led_mode = val;
		g_2led_mode2 = val;
	}else if(IC_switch == addr_0x18) {
		g_3led_mode = val;
		g_3led_mode2 = val;
	}

	if(!val){
		g_2led_mode = 0;
		g_2led_mode2 = 0;
		g_3led_mode = 0;
		g_3led_mode2 = 0;
	}

	return count;
}

static ssize_t fw_mode_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0xCA;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 1, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%s] IC:%d, FW Mode:%d\n", __func__, IC_switch, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t fw_ver_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val[3] = {0};

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0xCB;
	cmd[1] = 0x01;
	cmd[2] = 0x00;

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	memcpy(val, data, FEATURE_READ_COMMAND_SIZE);
	kfree(data);

	printk("[ROG7_INBOX][%s] IC:%d, FW_VER:0x%02x%02x\n", __func__, IC_switch, val[1], val[2]);
	return snprintf(buf, PAGE_SIZE,"0x%02x%02x\n", val[1], val[2]);
}
/*
static int ms51_GetFirmwareSize(char *firmware_name)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize = 0;
	char filepath[128];

	printk("[ROG7_INBOX] ms51_GetFirmwareSize.\n");
	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s", firmware_name);
	if (NULL == pfile) {
		pfile = filp_open(filepath, O_RDONLY, 0);
	}
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}
	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);
	return fsize;
}

static int ms51_ReadFirmware(char *fw_name, unsigned char *fw_buf)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize;
	char filepath[128];
	loff_t pos;
	int ret = 0;

	printk("[ROG7_INBOX] ms51_ReadFirmware.\n");
	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s", fw_name);
	if (NULL == pfile) {
		pfile = filp_open(filepath, O_RDONLY, 0);
	}
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}
	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	pos = 0;
	ret = kernel_read(pfile, fw_buf, fsize, &pos);
	if (ret<0)
		pr_err("Read file %s fail\n", filepath);

	filp_close(pfile, NULL);
	return 0;
}
*/
static int ms51_UpdateFirmware(const unsigned char *fw_buf,int fwsize,int aura_id)
{
	int err = 0;
	unsigned char *buf;
	short addr;
	int count = 0;

	buf = kmalloc(sizeof(unsigned char)*49, GFP_DMA);
	if (!buf) {
		printk("unable to allocate key input memory\n");
		return -ENOMEM;
	}

	//erase--remove this because we will send all 13kb data (add 0 to the end)
	//err = ms51_fw_erase(client);
	//if (err !=1)
		//printk("[AURA_MS51_INBOX] ms51_fw_erase :err %d\n", err);
	//msleep(500);
	//printk("[AURA_MS51_INBOX] after erase :\n");

	//flash
	usb_autopm_get_interface(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent));
	//first write
	memset(buf,0,sizeof(unsigned char)*48);
	buf[0] = 0xA0;
	buf[13] = 0x34;
	memcpy(&(buf[16]),fw_buf+0,32);

	printk("[ROG7_INBOX][%s] num=0\n", __func__);
	err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(aura_id), buf, 48);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd: err %d\n", __func__, err);

	msleep(1000);

	//the follwing write
	for(addr = 32; addr < 13*1024; addr = addr+32){
		memset(buf,0,sizeof(unsigned char)*48);
		buf[0] = 0xA0;
		if(addr <= fwsize-32){
			printk("if: addr = %d\n", addr);
			memcpy(&(buf[16]),fw_buf+addr,32);
			count = 48;
		}else{
			printk("else: addr = %d\n", addr);
			if(addr >= fwsize){
				memset(&(buf[16]),0,sizeof(unsigned char)*32);
				count = 16;
			}else{
				memcpy(&(buf[16]),fw_buf+addr,fwsize-addr);
				//memset(&(buf[16+fwsize-addr]),0,sizeof(unsigned char)*(32-fwsize+addr));
				count = 16 + (fwsize-addr);
			}
		}

		printk("[ROG7_INBOX][%s] num=%d, count=%d\n", __func__, addr/32, count);
		err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(aura_id), buf, count);
		if (err < 0){
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd: err %d\n", __func__, err);
			break;
		}
		msleep(10);
	}//end for

	printk("[ROG7_INBOX] ms51_UpdateFirmware finished.\n");
	kfree(buf);
	usb_autopm_put_interface(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent));
	return 0;
}

static ssize_t fw_update_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *buf, size_t count)
{
	int err = 0;
	const struct firmware *fw = NULL;

	// read firmware from fw_path which is defined by boot parameter "firmware_class.path"
	if(g_Aura_ic_updating == 1){
		printk("[ROG7_INBOX] Request MS51_2LED_FW_PATH...\n");
		err = request_firmware(&fw, MS51_2LED_FW_PATH, dev);
	}else if(g_Aura_ic_updating == 2) {
		printk("[ROG7_INBOX] Request MS51_3LED_FW_PATH...\n");
		err = request_firmware(&fw, MS51_3LED_FW_PATH, dev);
	}else {
		printk("[ROG7_INBOX] IC switch error, %d !!!\n", g_Aura_ic_updating);
		return -ENOENT;
	}

	if (err) {
		printk("[ROG7_INBOX] Error: request_firmware failed!!!\n");
		return -ENOENT;
	}

	mutex_lock(&ms51_mutex);
	err = ms51_UpdateFirmware(fw->data,fw->size,g_Aura_ic_updating);
	if(err)
		printk("[ROG7_INBOX] ms51_UpdateFirmware, err %d\n", err);

	g_Aura_ic_updating = 0;
	mutex_unlock(&ms51_mutex);
	release_firmware(fw);
	return count;

}

static ssize_t fw_update_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	if(g_Aura_ic_updating == 1){
		printk("[ROG7_INBOX] assign %s\n", MS51_2LED_FW_PATH);
		return snprintf(buf, PAGE_SIZE, "%s\n", MS51_2LED_FW_PATH);
	}else if(g_Aura_ic_updating == 2) {
		printk("[ROG7_INBOX] assign %s\n", MS51_3LED_FW_PATH);
		return snprintf(buf, PAGE_SIZE, "%s\n", MS51_3LED_FW_PATH);
	}else {
		printk("[ROG7_INBOX] FW assign error, IC switch %d !!!\n", IC_switch);
		return snprintf(buf, PAGE_SIZE, "Assign Error\n");
	}
}

static ssize_t ap2ld_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	if(val == 1 && g_Aura_ic_updating == 0){
		g_Aura_ic_updating = addr_0x16;
	}else if(val == 2 && g_Aura_ic_updating == 0){
		g_Aura_ic_updating = addr_0x18;
	}else{
		printk("[ROG7_INBOX] enter LD fail, cause other id=%d is updating\n",g_Aura_ic_updating);
		return count;
	}

	printk("[ROG7_INBOX] AP to LD. id=%d\n",g_Aura_ic_updating);

	data[0] = 0xCB;
	data[1] = 0x02;
	data[2] = 0x00;
		
	err = asus_usb_hid_write_aprom(hid_report_id_aprom( g_Aura_ic_updating), data, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t ap2ld_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", g_Aura_ic_updating);
}


static ssize_t ld2ap_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};
	u8 aura_id = 0;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	if (val == 1){
		aura_id = addr_0x16;
	}else if (val == 2){
		aura_id = addr_0x18;
	}else{
		aura_id = 0;
	}
	printk("[ROG7_INBOX] LD to AP.\n");

	data[0] = 0xAB;
	data[1] = 0x00;
	data[2] = 0x00;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(aura_id), data, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t led_on_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[3] = {0};

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] led_on: %d\n", val);
	g_led_on = (val==1)?0x01:0x00;

	data[0] = 0x60;
	data[1] = 0x06;		// MS51 GPIO P0.6
	data[2] = g_led_on;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	return count;
}

static ssize_t led_on_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", g_led_on);
}

static int door_enable(int enable)
{
	int ret = 0;
	u8 on = 0x00;
	u8 current_val = 0x0;
	u8 target_val = 0x0;

	if( enable == g_door_on ){
		printk("[ROG7_INBOX][%s] skip set door %d\n",__func__,enable);
		return 0;
	}

	if(enable > 0){
		on = 0x01;
		g_door_on = 1;
	}else{
		on = 0x00;
		g_door_on = 0;
	}

	ret = pd_byte_read_reg(GPIO_GP1, &current_val);
	if(ret < 0){
		printk("[ROG7_INBOX][%s] get reg failed %d\n", __func__, ret);
		return -1;
	}

	if((current_val & DOOR_EN) && enable){
		printk("[ROG7_INBOX][%s] logo bit already on\n",__func__);
	}else if(!(current_val & DOOR_EN) && !enable){
		printk("[ROG7_INBOX][%s] logo bit already off\n",__func__);
	}else{
		target_val = (current_val & (DOOR_EN ^ 0xFF)) | (on<<5);
		printk("[ROG7_INBOX][%s] set reg %x\n", __func__, target_val);
		pd_byte_write_reg(GPIO_GP1, target_val);
		if(g_door_on)
			printk("[ROG7_INBOX][%s] enable door\n",__func__);
		else
			printk("[ROG7_INBOX][%s] disable door\n",__func__);
	}
	return ret;
}

static ssize_t door_on_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	u32 val = 0;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	door_enable(val);
	return ret;
}

static ssize_t door_on_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", g_door_on);
}

static int logo_enable(int enable)
{
	int ret = 0;
	u8 on = 0x00;
	u8 current_val = 0x0;
	u8 target_val = 0x0;

	if( enable == g_logo_on ){
		printk("[ROG7_INBOX][%s] skip set logo %d\n",__func__,enable);
		return 0;
	}

	if(enable > 0){
		on = 0x01;
		g_logo_on = 1;
	}else{
		on = 0x00;
		g_logo_on = 0;
	}

	ret = pd_byte_read_reg(GPIO_GP2, &current_val);
	if(ret < 0){
		printk("[ROG7_INBOX][%s] get reg failed %d\n", __func__, ret);
		return -1;
	}

	if((current_val & LOGO_EN) && enable){
		printk("[ROG7_INBOX][%s] logo bit already on\n",__func__);
	}else if(!(current_val & LOGO_EN) && !enable){
		printk("[ROG7_INBOX][%s] logo bit already off\n",__func__);
	}else{
		target_val = (current_val & (LOGO_EN ^ 0xFF)) | (on<<6);
		printk("[ROG7_INBOX][%s] set reg %x ,current_val = %x\n", __func__, target_val,current_val);
		pd_byte_write_reg(GPIO_GP2, target_val);
		if(g_logo_on)
			printk("[ROG7_INBOX][%s] enable logo\n",__func__);
		else
			printk("[ROG7_INBOX][%s] disable logo\n",__func__);
	}
	return ret;
}

static ssize_t logo_on_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	// PD control logo enable
	logo_enable(val);

	return count;
}

static ssize_t logo_on_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", g_logo_on);
}

static int fan_enable(unsigned char option)
{
	int err = 0;
	//unsigned char data[3] = {0};

	option = (option==1)?0x01:0x00;
	printk("[ROG7_INBOX] FAN %s\n", (option==1)?"Enable":"Disable");
/*
	data[0] = 0x14;
	if (option==1)
		data[1] = reg | 0x40;		// PD GPIO
	else
		data[1] = ret & 
		
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x64), data, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write err: %d\n", __func__, err);
*/
	return err;
}

static int fan_pwm(unsigned char pwm)
{
	int err = 0;
	unsigned char data[3] = {0};

	printk("[ROG7_INBOX] FAN PWM: %d\n", pwm);

	if (pwm < 0 || pwm > 255) {
		printk("[ROG7_INBOX][%s] input value error: %d (0 ~ 255)\n", __func__, pwm);
		return -1;
	}

	data[0] = 0x60;
	data[1] = 0x01;
	data[2] = pwm;
		
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), data, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s]: asus_usb_hid_write err: %d\n", __func__, err);
	
	return err;
}

static ssize_t fan_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	//printk("[ROG7_INBOX] fan_enable_store: %d\n", val);

	if (val > 0)
		fan_enable(1);
	else
		fan_enable(0);

	return count;
}

//+++inbox user fan
static ssize_t inbox_user_fan(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int num = 99;
	u32  tmp;
	int err = 0;

	mutex_lock(&update_lock);
	
	sscanf(buf, "%d", &num);
	printk("[INBOX_FAN] %s: %d", __func__, num);
	
	switch (num) {
		case 0:
			tmp = 0;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(0);
			break;
		case 1:
			tmp = 127;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		case 2:
			tmp = 135;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		case 3:
			tmp = 163;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		case 4:
			tmp = 171;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		default:
			printk("[INBOX_FAN] %s :mode isn't 0-4, unsupport\n", __func__);

	}
	msleep(500); //Wait 0.5s
	mutex_unlock(&update_lock);
	printk("%s ---", __func__);
	return size;
}

//+++inbox thermal fan
static ssize_t inbox_thermal_fan(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int num = 99;
	u32 tmp;
	int err = 0;

	mutex_lock(&update_lock);
	sscanf(buf, "%d", &num);
	printk("[INBOX_FAN] %s: %d", __func__, num);
	
	switch (num) {
		case 0:
			tmp = 0;
			err = fan_pwm(tmp);
			err = fan_enable(1);
			break;
		case 1:
			tmp = 127;
			err = fan_pwm(tmp);
			err = fan_enable(1);
			break;
		case 2:
			tmp = 135;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		case 3:
			tmp = 163;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		case 4:
			tmp = 171;
			err = fan_pwm(tmp);
			msleep(30);
			err = fan_enable(1);
			break;
		default:
			printk("[INBOX_FAN][%s] mode isn't 0-4, unsupport\n",__func__);
	}
	msleep(500); //Wait 0.5s
	mutex_unlock(&update_lock);
	printk("%s ---", __func__);
	return size;
}
//---inbox thermal fan
static ssize_t fan_rpm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 rpm;
	u8 val = 0;
	int err = 0;
	ssize_t ret;
	int idx = 0;
	bool skip_interpolation = false;
	u8 *data;
	u8 cmd[3] = {0};
	int retry = 10;
	u8 pwm = 0;
	//int raw_data = 0;
	const struct rpm_table_t table[11] = {
		{15, 540},
		{25, 1080},
		{50, 2100},
		{75, 2880},
		{100, 3500},
		{126, 4200},
		{150, 4720},
		{175, 5190},
		{196, 5500},
		{225, 6030},
		{255, 6450},
	};

	ret = kstrtou32(buf, 10, &rpm);

	//rpm to val
	if (rpm == 0) {
		val = 0;
		skip_interpolation = true;
	} else if (rpm < table[0].fan_rpm) {
		val = (u8)table[0].pwm_val;
		skip_interpolation = true;
	} else if (rpm > table[10].fan_rpm) {
		val = (u8)table[10].pwm_val;
		skip_interpolation = true;
	} else {
		for(idx = 0; idx < 10; idx++){
			if (rpm == table[idx].fan_rpm) {
				val = (u8)table[idx].pwm_val;
				skip_interpolation = true;
				break;
			}
			if (rpm == table[idx+1].fan_rpm) {
				val = (u8)table[idx+1].pwm_val;
				skip_interpolation = true;
				break;
			}
			if (rpm > table[idx].fan_rpm && rpm < table[idx+1].fan_rpm){
				break;
			}
		}
	}

	if (!skip_interpolation)
		val = (u8)(rpm - table[idx].fan_rpm) * (table[idx+1].pwm_val - table[idx].pwm_val) / (table[idx+1].fan_rpm - table[idx].fan_rpm) + table[idx].pwm_val;

	if (val > 255) {
		printk("[ROG6_INBOX] pwm out of range, set default RPM");
		val = 127;
	}

	data = kzalloc(FEATURE_READ_COMMAND_SIZE, GFP_KERNEL);
	do {
		err = fan_pwm(val);
/*
		if (err < 0)
			printk("[ROG6_INBOX] %s set pwm failed, err=%d\n",__func__ ,err);
		else
			printk("[ROG6_INBOX] %s set pwm = %d\n",__func__ ,val);
*/
		//get fan pwm
		msleep(20);
		memset(data, 0, FEATURE_READ_COMMAND_SIZE);

		cmd[0] = 0x60;
		cmd[1] = 0x01;
		cmd[2] = 0x00;

		ret = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x16), cmd, 2, data);
		if (ret < 0)
			printk("[ROG6_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

		//printk("[ROG6_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

		pwm = data[1];

		if (val == pwm) {
			printk("[ROG6_INBOX] %s set pwm = %d\n",__func__ ,val);
			retry = 0;
		} else {
			//set fan failed, retry..
			printk("[ROG6_INBOX] %s retry, val=%d pwm=%d\n",__func__ ,val ,pwm);
			msleep(50);
			retry --;
		}
	} while (retry > 0);

	kfree(data);
	//printk("[ROG6_INBOX] %s-\n", __func__);
	return count;
}
static ssize_t fan_rpm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	int raw_data = 0;
	long rpm = 0;

	if(g_Aura_ic_updating != 0)
		return 0;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x60;
	cmd[1] = 0x02;
	cmd[2] = 0x00;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	msleep(10);

	cmd[0] = 0xCC;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x16), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %02x, %02x, %02x\n", __func__, data[0], data[1], data[2]);
	raw_data = data[1]*256 + data[2];
	kfree(data);
	//printk("[ROG7_INBOX][%s] %d\n", __func__, raw_data);

	if (raw_data != 0) {
		rpm = (24 * 1000000) >> 9;
		do_div(rpm,raw_data);
		printk("[ROG7_INBOX][FAN RPM] %ld\n", rpm*30);
		return snprintf(buf, PAGE_SIZE,"%ld\n", rpm*30);
	} else {
		printk("[ROG7_INBOX][FAN RPM] raw data abnormal.\n");
		return snprintf(buf, PAGE_SIZE,"%d\n", raw_data);
	}
}

static ssize_t fan_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;

	if(g_Aura_ic_updating != 0)
		return 0;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;
	printk("[ROG7_INBOX] %s %d\n",__func__,val);
	err = fan_pwm(val);

	return count;
}

static ssize_t fan_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x60;
	cmd[1] = 0x01;
	cmd[2] = 0x00;

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x16), cmd, 2, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	val = data[1];
	kfree(data);

	printk("[ROG7_INBOX][%s] %d\n", __func__, val);
	return snprintf(buf, PAGE_SIZE,"%d\n", val);
}

static ssize_t unique_id_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[48] = {0};
	u8 val[FEATURE_READ_LONG_COMMAND_SIZE] = {0};

	data = kzalloc(FEATURE_READ_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(data,0,FEATURE_READ_LONG_COMMAND_SIZE);

	cmd[0] = 0xCB;
	cmd[1] = 0x3;

	err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);

	msleep(10);

	cmd[0] = 0xCC;
	cmd[1] = 0x00;

	err = asus_usb_hid_read_long_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 1, data, 12);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_long_cmd:err %d\n", err);

/*
	printk("[ROG7_INBOX][%s] %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x\n", __func__,
		data[0], data[1], data[2], data[3], data[4], data[5], data[6],
		data[7], data[8], data[9], data[10], data[11], data[12], data[13]);
*/
	memcpy(val, data, FEATURE_READ_LONG_COMMAND_SIZE);
	kfree(data);

	printk("[ROG7_INBOX] Unique ID : 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", 
		val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8], val[9], val[10], val[11], val[12]);

	return snprintf(buf, PAGE_SIZE,"0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		val[1], val[2], val[3], val[4], val[5], val[6],
		val[7], val[8], val[9], val[10], val[11], val[12]);
}

static ssize_t mode2_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	if(IC_switch == addr_0x16)
		return snprintf(buf, PAGE_SIZE,"%d\n", g_2led_mode2);
	else if(IC_switch == addr_0x18)
		return snprintf(buf, PAGE_SIZE,"%d\n", g_3led_mode2);
	
	return snprintf(buf, PAGE_SIZE,"IC:%d, choose wrong IC.\n", IC_switch);
}

static ssize_t mode2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned char rgb[RGB_MAX] = {0};
	unsigned char rainbow_mode = 0;
	unsigned char mode2 = 0;
	int err = 0;
	int i = 0, n = 0, rgb_num = 0;
	long rgb_tmp = 0;
	int ntokens = 0;
	const char *cp = buf;
	const char *buf_tmp;
	unsigned char data[3] = {0};

	sscanf(buf, "%d", &mode2);

	while ((cp = strpbrk(cp + 1, ","))){
		ntokens++;  //the number of ","
		//printk("[ROG7_INBOX] mode2_store %s.\n", cp);
	}

	//printk("[ROG7_INBOX][%s] mode2=%d ntokens=%d, buf=%s\n", __func__, mode2, ntokens, buf);
	if(ntokens > 6){
		printk("[ROG7_INBOX][%s] Too many ntokens %d, Maxium can't over 6\n", __func__, ntokens);
		g_2led_mode2=-1;
		return count;
	}

	cp=buf;
	while((cp = strpbrk(cp, ",")))  //goto the ",".
	{
		cp++; // go after the ','
		while(*cp != ',' && *cp != '\0' && *cp !='\n')
		{
			if(*cp==' '){
				cp++; //skip the ' '
			}else{
				buf_tmp = cp;
				rgb_tmp = 0;
				sscanf(buf_tmp, "%x",&rgb_tmp);
				rgb[rgb_num++] = (rgb_tmp >> 16)&0xFF;
				rgb[rgb_num++] = (rgb_tmp >> 8)&0xFF;
				rgb[rgb_num++] = rgb_tmp & 0xFF;
				break;
			}
		}
	}

	if(rgb_num != ntokens*3){
		printk("[ROG7_INBOX][%s] Wrong input. rgb_num (%d) != ntokens*3 (%d*3)\n", __func__, rgb_num, ntokens);
		g_2led_mode2=-1;
		return count;
	}
/*
	printk("[ROG7_INBOX][%s] rgb_num = %d \n",__func__, rgb_num);
	for(i=0;i<rgb_num;i++)
		printk("[ROG7_INBOX][%s] rgb[%d] = 0x%x \n",__func__, i, rgb[i]);
*/
	switch(mode2){
		case 0: //closed
			rainbow_mode = 0;
			break;
		case 1: //6 color rainbow
			rainbow_mode = 0x7;
			break;
		case 2: //static
			rainbow_mode = 1;
			break;
		case 3: //breath at the same time
			rainbow_mode = 0x2;
			break;
		case 4: //breath at different time
			rainbow_mode = 0x11;
			break;
		case 5: //breath only one led
			rainbow_mode = 0x10;
			break;
		case 6: //commet
			rainbow_mode = 0x12;
			break;
		case 7: //flash and dash
			rainbow_mode = 0x14;
			break;
		case 8: //commet in different direction direction
			rainbow_mode = 0x13;
			break;
		case 9: //flash and dash in different direction
			rainbow_mode = 0x15;
			break;
		case 10: //6 color in different direction
			rainbow_mode = 0x8;
			break;
		case 11: //6 color in different direction
			rainbow_mode = 0xF;
			break;
		case 12: //LED1 static, LED2 & LED3 breath
			rainbow_mode = 0x1F;
			break;
		case 13: //LED2 static, LED1 & LED3 breath
			rainbow_mode = 0x2F;
			break;
		case 14: //LED3 static, LED1 & LED2 breath
			rainbow_mode = 0x3F;
			break;
		case 15: //LED1 breath, LED2 & LED3 static
			rainbow_mode = 0x4F;
			break;
		case 16: //LED2 breath, LED1 & LED3 static
			rainbow_mode = 0x5F;
			break;
		case 17: //LED3 breath, LED1 & LED2 static
			rainbow_mode = 0x6F;
			break;
		case 18: //LED1 & LED3 breath, LED2 breath at different time
			rainbow_mode = 0x11;
			break;
		case 19: //LED1 & LED2 breath, LED3 breath at different time
			rainbow_mode = 0x21;
			break;
		case 20: //LED2 & LED3 breath, LED1 breath at different time
			rainbow_mode = 0x31;
			break;
		case 21: //Flow breath 01 (slow)
			rainbow_mode = 0x16;
			break;
		case 22: //Flow breath 02 (Fast)
			rainbow_mode = 0x17;
			break;
	}

	if ( (IC_switch == addr_0x16) && ((mode2 == 12) || (mode2 == 13)) ){
		printk("[ROG7_INBOX][%s] 2LED MS51 not support mode2 %d\n", __func__, mode2);
		return count;
	}

	switch(rainbow_mode){
		case 0:  //mode 0
			break;
		case 0x1: //static
		case 0x2: //breath at the same time
		case 0xF: //breath one led
		case 0x10: //breath one led
		case 0x11: //breath at the different time //LED1 & LED3 breath, LED2 breath at different time
		case 0x1f://LED1 static, LED2 & LED3 breath
		case 0x2f://LED2 static, LED1 & LED3 breath
		case 0x3f://LED3 static, LED1 & LED2 breath
		case 0x4f://LED1 breath, LED2 & LED3 static
		case 0x5f://LED2 breath, LED1 & LED3 static
		case 0x6f://LED3 breath, LED1 & LED2 static
		case 0x21://LED1 & LED2 breath, LED3 breath at different time
		case 0x31://LED1 & LED3 breath, LED1 breath at different time
			if( ((ntokens != 2) && (IC_switch == addr_0x16)) || ((ntokens != 3) && (IC_switch == addr_0x18))){
				printk("[ROG7_INBOX][%s] Wrong input. ntokens(%d) != %d\n", __func__, ntokens, (IC_switch == addr_0x16)?2:3);
				g_2led_mode2 = -1;
				return count;
			}
			//sscanf(buf, "%x, %x %x %x,%x %x %x", &rainbow_mode,&rgb[0],&rgb[1],&rgb[2],&rgb[3],&rgb[4],&rgb[5]);
			//printk("[ROG7_INBOX] mode2_store,static two leds. mode=0x%x,client->addr:0x%02x.\n", rainbow_mode,client->addr);

			for(i=0; i<=ntokens*3; i++){
				data[0] = 0x80;
				data[1] = (0x10 + i);
				data[2] = rgb[i];

				err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
				if (err < 0) {
					g_2led_mode2 = -1;
					printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
				}
			}
			break;
		case 0x7:
		case 0x8://6 colors rainbow
			if(ntokens != 6){
				printk("[ROG7_INBOX][%s] Wrong input. ntokens(%d) != 6\n", __func__, ntokens);
				g_2led_mode2=-1;
				return count;
			}

			for(i=0; i<ntokens; i++){
				data[0] = 0xD0 + i;
				for(n=0; n<3; n++){
					data[1] = n;
					data[2] = rgb[3*i+n];

					err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
					if (err < 0){
						g_2led_mode2=-1;
						printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
					}

				}
			}
			break;
		case 0x12://comet
		case 0x13://comet in different direction
		case 0x14://flash and dash
		case 0x15://flash and dash in different direction
		case 0x16://Flow breath 01 (slow)
		case 0x17://Flow breath 02 (fast)
			if( ((ntokens != 2) && (IC_switch == addr_0x16)) || ((ntokens != 3) && (IC_switch == addr_0x18))){
				printk("[ROG7_INBOX][%s] Wrong input. ntokens(%d) != %d\n", __func__, ntokens, (IC_switch == addr_0x16)?2:3);
				return count;
			}

			//sscanf(buf, "%x, %x %x %x", &rainbow_mode,&rgb[0],&rgb[1],&rgb[2]);
			//printk("[AURA_MS51_INBOX] mode2_store,comet or flash and dash. mode=0x%x,client->addr:0x%x.\n", rainbow_mode,client->addr);
			
			for(i=0; i<ntokens; i++){
				data[0] = 0xDB + i;
				for(n=0; n<=2; n++){
					data[1] = n;
					data[2] = rgb[3*i+n];

					err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
					if (err < 0) {
						g_2led_mode2 = -1;
						printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
					}
				}
			}
			break;
		default:
			break;
	}

	// send mode command
	data[0] = 0x80;
	data[1] = 0x21;
	data[2] = rainbow_mode;
			
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(IC_switch), data, 3);
	if (err < 0) {
		g_2led_mode2 = -1;
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	}

	if(IC_switch == addr_0x16){
		g_2led_mode2 = mode2;
		g_2led_mode = (u8)rainbow_mode;
	}else if(IC_switch == addr_0x18){
		g_3led_mode2 = mode2;
		g_3led_mode = (u8)rainbow_mode;
	}

	return count;
}

static int headset_enable(int enable){
	int ret = 0;
	int err = 0;
	u8 cmd[3] = {0};

	usb_autopm_get_interface(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent));
	memset(cmd, 0, 3);
	if(!enable){
		printk("[ROG7_INBOX] %s try to clear headset\n",__func__);
		cmd[0] = 0x10;
	}else{
		printk("[ROG7_INBOX] %s try to set headset\n",__func__);
		cmd[0] = 0x11;
	}

	err = asus_usb_hid_write_extra(hid_report_id_aprom(addr_keyboard_mode), cmd, 1);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	else
		printk("[ROG7_INBOX] %s- enable=%d",__func__, enable);
	usb_autopm_put_interface(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent));
	return ret;
}

static ssize_t keyboard_mode_store(struct device *dev, struct device_attribute *mattr, const char *buf, size_t count)
{
	int err = 0;
	int color = 0;
	int R_pwm = 0, G_pwm = 0, B_pwm = 0;
	u8 cmd[3] = {0};

	err = kstrtou32(buf, 10, &color);
	if (err)
		return count;

	printk("[ROG6_INBOX][%s+] color=0x%x",__func__ ,color);

	if( color > 0 ){
		printk("[ROG6_INBOX] %s set the color of keyboard mode 0x%x\n",__func__ ,color);
		R_pwm = (color & 0x00FF0000)>>16;
		G_pwm = (color & 0x0000FF00)>>8;
		B_pwm = (color & 0x000000FF);

		//set the color of LOGO in keyboard mode
		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x21;
		cmd[2] = 0x01;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x10;
		cmd[2] = R_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x11;
		cmd[2] = G_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x12;
		cmd[2] = B_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x13;
		cmd[2] = R_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x14;
		cmd[2] = G_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x15;
		cmd[2] = B_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x2f;
		cmd[2] = 0x01;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		//set the color of light bar in keyboard mode
		memset(cmd, 0, 3);
		cmd[0] = 0xDB;
		cmd[1] = 0x00;
		cmd[2] = R_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDB;
		cmd[1] = 0x01;
		cmd[2] = G_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDB;
		cmd[1] = 0x02;
		cmd[2] = B_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDC;
		cmd[1] = 0x00;
		cmd[2] = R_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDC;
		cmd[1] = 0x01;
		cmd[2] = G_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDC;
		cmd[1] = 0x02;
		cmd[2] = B_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDD;
		cmd[1] = 0x00;
		cmd[2] = R_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDD;
		cmd[1] = 0x01;
		cmd[2] = G_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0xDD;
		cmd[1] = 0x02;
		cmd[2] = B_pwm;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

		memset(cmd, 0, 3);
		cmd[0] = 0x80;
		cmd[1] = 0x2F;
		cmd[2] = 0x01;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), cmd, 3);
		if (err < 0)
			printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	} else {
		printk("[ROG6_INBOX] %s Disable Keyboard mode\n",__func__);
		color = 0;
	}

	memset(cmd, 0, 3);
	if(!color)
		cmd[0] = 0x20;
	else
		cmd[0] = 0x21;

	err = asus_usb_hid_write_extra(hid_report_id_aprom(addr_keyboard_mode), cmd, 1);
	if (err < 0)
		printk("[ROG6_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	else
		printk("[ROG6_INBOX] %s+ keyboard mode, color=0x%x",__func__, color);
	return count;
}
static ssize_t key_state_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	printk("[ROG7_INBOX][%s] 0x%02x\n", __func__, key_state);
	return snprintf(buf, PAGE_SIZE,"0x%02x\n", key_state);
}

static int PD_SWITCH_CHECK(u8 val)
{
	bool SW1 = val & EN_SW1;
	bool SW2 = val & EN_SW2;
	bool SW6 = val & EN_SW6;

	if(( SW1 || SW2 ) && SW6)
		return -1;
	else
		return 0;
}

static int pd_byte_write_reg(u8 reg_addr, u8 val)
{
	int ret = 0;
	unsigned char data[2] = {0};

	if(g_PD_updating){
		printk("[ROG7_INBOX] %s PDIC updating, ignore command\n",__func__);
		return -1;
	}

	if(reg_addr == GPIO_GP2){
		ret = PD_SWITCH_CHECK(val);
		if(ret < 0){
			printk("[ROG7_INBOX] %s ERROR:set 0x%x to 0x14 is not available!!\n",__func__, val);
			return ret;
		}
	}
	if(reg_addr == GPIO_GP2){
		printk("[ROG7_INBOX] %s set %x to GP2\n",__func__, val);
	}
	mutex_lock(&pd_lock);
	//printk("[ROG7_INBOX] %s+\n",__func__);
	//pd_wakeup(1);
	data[0] = reg_addr;
	data[1] = val;
	ret = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x64), data, 2);
	if (ret < 0)
		printk("[ROG7_INBOX][%s] write %x to %x failed:err %d\n", __func__, val, reg_addr, ret);
	msleep(10);
	//printk("[ROG7_INBOX] %s-\n",__func__);
	//pd_wakeup(0);
	mutex_unlock(&pd_lock);
	return ret;
}

static int pd_isp_byte_write_reg(u8 reg_addr, u8 val)
{
	int ret = 0;
	unsigned char data[2] = {0};

	//printk("[ROG7_INBOX] %s+\n",__func__);
	data[0] = reg_addr;
	data[1] = val;
	ret = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x28), data, 2);
	if (ret < 0)
		printk("[ROG7_INBOX][%s] write %x to %x failed:err %d\n", __func__, val, reg_addr, ret);
	//msleep(10);
	//printk("[ROG7_INBOX] %s-\n",__func__);
	return ret;
}

static int pd_isp_16byte_write(u8 *buf)
{
	int ret = 0;
	//u8 *data;
	unsigned char data[48] = {0};

	//data = kzalloc(17, GFP_KERNEL);
	//memset(data, 0, 17);
	//printk("[ROG7_INBOX] %s+\n",__func__);
	data[0] = 0x41;
	data[1] = 0x10;

	memcpy(&(data[2]), buf, 16);
	ret = asus_usb_hid_write_log_cmd(hid_report_id_aprom(addr_0x28_long), data, 18);
	//ret = asus_usb_hid_write_long_cmd(hid_report_id_aprom(addr_0x28), data, 48);
	if (ret < 0)
		printk("[ROG7_INBOX][%s] write  %x failed:err %d\n", __func__, ret);
	else
		printk("[ROG7_INBOX][%s] write %x %x %x ... %x %x %x\n", __func__, data[2], data[3], data[4], data[15], data[16], data[17]);
	//kfree(data);
	//msleep(g_delay_ms);

	//printk("[ROG7_INBOX] %s-\n",__func__);
	return ret;
}

/*
static int pd_isp_16byte_read(u8 *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[48] = {0};
	u8 val[FEATURE_READ_LONG_COMMAND_SIZE] = {0};

	data = kzalloc(FEATURE_READ_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(data,0,FEATURE_READ_LONG_COMMAND_SIZE);

	cmd[0] = 0xC1;

	err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x28), cmd, 1);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);

	msleep(10);

	cmd[0] = 0xCC;
	cmd[1] = 0x00;

	err = asus_usb_hid_read_long_cmd(hid_report_id_long_cmd(addr_0x28_long), cmd, 1, data, 12);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_long_cmd:err %d\n", err);


	printk("[ROG7_INBOX][%s] %02x, %02x, %02x, %02x, %02x, %02x, %02x,.... %02x, %02x, %02x, %02x, %02x, %02x, %02x\n", __func__,
		data[0], data[1], data[2], data[3], data[4], data[5], data[6],
		data[7], data[8], data[9], data[10], data[11], data[12], data[13]);

	memcpy(val, data, FEATURE_READ_LONG_COMMAND_SIZE);
	kfree(data);

	//printk("[ROG7_INBOX] %s : 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", __func__,
		//val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8], val[9], val[10], val[11], val[12]);

	return snprintf(buf, PAGE_SIZE,"0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		val[1], val[2], val[3], val[4], val[5], val[6],
		val[7], val[8], val[9], val[10], val[11], val[12]);
}
*/
/*
static int CHECK_PD_GPIO_GP2(int reg, u8 val)
{
	int ret = 0;
	if(reg != GPIO_GP2){
		//skip check
		ret = 1;
	}else{
		//check switch should not all off
		if(val & 0x0F){
			ret = 1;
		}else{
			printk("[ROG7_INBOX] get PD GPIO GP2 should not be zero");
			ret = 0;
		}
	}
	return ret;
}*/
static int pd_byte_read_reg(int reg_addr, u8 *val)
{
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	//u8 result[2] = {0};
	int retry = 5;
	bool need_retry = false;
	//int i = 0;

	if(g_PD_updating){
		printk("[ROG7_INBOX] %s PDIC updating, ignore command\n",__func__);
		return -1;
	}

	mutex_lock(&pd_lock);
	//printk("[ROG7_INBOX] %s+\n",__func__);
	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	//pd_wakeup(1);
	cmd[0] = reg_addr;
	do{
		//for(i = 0 ;i < 2; i++){
			ret = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x64), cmd, 1, data);
			//if(data[1] == 0xff)
				//need_retry = true;
			//msleep(100);
			//ret = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x64), cmd, 1, data); //need fix by firmware
			if (ret < 0){
				printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, ret);
			}else{
				*val = data[1];
				if(data[1] == 0xff)
					need_retry = true;
				//result[i] = data[1];
				msleep(20);
			}
		//}
		//printk("[ROG7_INBOX][%s] result %x %x, retry=%d\n", __func__, result[0],result[1],retry);
		//if(result[0] == result[1] && ((result[0] & 0x0F) != 0x00)){
			//*val = data[1];
		//}else{
			//msleep(100);
			//if(reg_addr == GPIO_GP2){
				//*val = 0xFF;
				//printk("[ROG7_INBOX][%s] the reg 0x%x should not be 0x%x 0x%x, return 0xFF\n", __func__,reg_addr,result[0],result[1]);
			//}
		//}
		--retry;
		if(need_retry){
			need_retry = false;
			continue;
		}else{
			break;
		}
	}while(retry > 0);

	kfree(data);
	//pd_wakeup(0);
	//printk("[ROG7_INBOX] %s- get %x from addr %x\n",__func__, *val, reg_addr);
	mutex_unlock(&pd_lock);
	return ret;
}

static ssize_t cooling_en_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;
	u8 current_val = 0x0, target_val = 0x0;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	g_cooling_en = (val==1)?0x01:0x00;
	printk("[ROG7_INBOX] Cooler Module %s\n", (g_cooling_en==1)?"Enable":"Disable");

	if(g_cooling_en){
		target_val = 0x20;
	}else{
		target_val = 0x00;
	}
	pd_byte_read_reg(GPIO_GP2, &current_val);
	printk("[ROG7_INBOX] %s get current val %x from 0x13\n",__func__,current_val);
	target_val = ((target_val & 0x20) | (current_val & 0xDF));
	printk("[ROG7_INBOX] %s set target val %x to 0x13\n",__func__,target_val);
	pd_byte_write_reg(GPIO_GP2, target_val);

	return count;
}

static int pd_cooler_enable(int enable)
{
	int ret = 0;
	u8 current_val = 0x0, target_val = 0x0;

	g_cooling_en = enable;
	printk("[ROG7_INBOX] Cooler Module %s\n", (g_cooling_en==1)?"Enable":"Disable");

	if(g_cooling_en){
		target_val = 0x20;
	}else{
		target_val = 0x00;
	}
	ret = pd_byte_read_reg(GPIO_GP2, &current_val);
	if (ret < 0){
		printk("[ROG7_INBOX] %s get current val failed from GPIO_GP2\n",__func__);
		return ret;
	}
	printk("[ROG7_INBOX] %s get current val %x from GPIO_GP2\n",__func__,current_val);
	target_val = ((target_val & 0x20) | (current_val & 0xDF));
	printk("[ROG7_INBOX] %s set target val %x to GPIO_GP2\n",__func__,target_val);
	ret = pd_byte_write_reg(GPIO_GP2, target_val);
	if (ret < 0){
		printk("[ROG7_INBOX] %s set target val %x failed from GPIO_GP2\n",__func__,target_val);
		return ret;
	}
	return ret;
}

static int pd_set_switch(int switch_id, bool ctl)
{
	int ret = 0;
	u8 current_val = 0x0;
	u8 target_val = 0x0;
	u8 switch_mask = 0x0;
	u8 on = 0x0;

	printk("[ROG7_INBOX] %s id=%d ctl=%d\n",__func__,switch_id,ctl);

	//SW4 should not active before VBAT on
	if(!vph_get_status() && switch_id == 4 && ctl){
		printk("[ROG7_INBOX] %s SW4 should not active before VBAT on, skip turn SW4 on\n",__func__);
	}

	if (switch_id == 1)	{
		switch_mask = EN_SW1;
	} else if (switch_id == 2) {
		switch_mask = EN_SW2;
	} else if (switch_id == 4) {
		switch_mask = EN_SW4;
	} else if (switch_id == 6) {
		switch_mask = EN_SW6;
	} else if (switch_id == 7) {
		switch_mask = EN_SW7;
	} else {
		printk("[ROG7_INBOX] %s WARNING: switch id %d is invalid!\n",__func__,switch_id);
		return -1;
	}

	if ( pd_byte_read_reg(GPIO_GP2, &current_val) < 0){
		//get reg failed
		ret = -1;
	}else{
/*
 * 		// firmware cover this
		//SW2 and SW6 should not active at the same time
		if(switch_id == 2 && ctl){
			//turn off sw6
			pd_byte_write_reg(GPIO_GP2, current_val & 0XF7);
		}else if(switch_id == 6 && ctl){
			//turn off sw2
			pd_byte_write_reg(GPIO_GP2, current_val & 0XFD);
		}
*/
		if (ctl){
			on = switch_mask;
		}else{
			on = 0x00;
		}

		target_val = ((current_val & (switch_mask ^ 0xFF)) | on);
		printk("[ROG7_INBOX] %s write 0x%x to 0x14, current = 0x%x\n",__func__,target_val, current_val);

		if(pd_byte_write_reg(GPIO_GP2, target_val) < 0)
		{
			printk("[ROG7_INBOX] %s write 0x%x to 0x14 failed\n",__func__,target_val);
			ret = -1;
		}
	}
	return ret;
}

static int pd_set_OV(int level)
{
	int ret = 0;
	u8 current_val = 0x00;
	u8 target_val = 0x00;
	u8 OV_val = 0x00;

	if((level < 0) || (level > 4))
	{
		printk("[ROG7_INBOX] level %s is invalid\n",level);
		ret = -1;
	}

	pd_byte_read_reg(GPIO_GP1, &current_val);
	printk("[ROG7_INBOX] %s get current val %x from GPIO_GP1\n",__func__,current_val);

	switch (level){
		case 0:
		case 1:
			OV_val = 0x00;
		break;
		case 2:
			OV_val = 0x01;
		break;
		case 3:
			OV_val = 0x03;
		break;
		case 4:
			OV_val = 0x07;
		break;
		default:
			printk("[ROG7_INBOX] %s unknown level.\n",__func__);
		break;
	}

	target_val = (current_val & 0xF0) | (OV_val & 0x0F);

	printk("[ROG7_INBOX] %s set target val %x to GPIO_GP1\n",__func__,target_val);
	pd_byte_write_reg(GPIO_GP1, target_val);
	return ret;
}

static ssize_t cooling_en_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int cooling_status = 0;
	u8 current_val = 0x0;

	pd_byte_read_reg(GPIO_GP2, &current_val);
	if ( 0x20 && current_val)
		cooling_status = 1;
	else
		cooling_status = 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", cooling_status);
}

static int pd_set_cooler_stage(int stage,bool force_set)
{
	int ret = 0;
	
	if((stage < 0) || (stage > 5))
	{
		printk("[ROG7_INBOX] %s stage is invalid\n",__func__,stage);
		return -1;
	}

	if(!force_set && stage == g_cooling_stage){
		printk("[ROG7_INBOX] %s stage %d no change, skip set cooler\n",__func__,stage);
		return ret;
	}else{
		g_cooling_stage = stage;
		queue_work(fan7_wq, &fan7_cooler_work);
		printk("[ROG7_INBOX] %s: queue work to set stage %d\n",__func__,stage);
	}
	return ret;
}

static ssize_t cooling_stage_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s %d\n",__func__,val);

	if(val >= 0 && val < 255){
		//remapping cooler stage and prepare power source
		if(val == 0){
			pd_set_cooler_stage(0,false);
		}else if(val < 46){
			pd_set_cooler_stage(1,false);
		}else{
			pd_set_cooler_stage(4,false);
		}
		printk("[ROG7_INBOX][%s] set cooler stage as %d\n",__func__ ,val);
	} else {
		printk("[ROG7_INBOX][%s] error value %d, should between 0 ~ 255\n", __func__, val);
		return count;
	}

	return count;
}

static ssize_t cooling_stage_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 current_cooler_stage = 0x0;

	pd_byte_read_reg(GPIO_GP1, &current_cooler_stage);
	current_cooler_stage = current_cooler_stage & 0x0F;
	return snprintf(buf, PAGE_SIZE,"0x%02x\n", current_cooler_stage);
}

static ssize_t cooling_stage2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s %d\n",__func__,val);

	if(val >= 0 && val < 5){
		g_cooling_stage = val;
		pd_set_cooler_stage(val,false);
		printk("[ROG7_INBOX][%s] set cooler stage as %x\n",__func__ ,val);
	} else {
		printk("[ROG7_INBOX][%s] error value %d, should between 0 ~ 255\n", __func__, val);
		return count;
	}

	return count;
}

static ssize_t cooling_stage2_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE,"%d\n", g_cooling_stage);
}

static ssize_t MPS_ID_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x28;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x75), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %02x, %02x, %02x\n", __func__, data[0], data[1], data[2]);
	printk("[ROG7_INBOX] MPS ID : 0x%02x\n", data[1]);

	val = data[1];
	kfree(data);

	return snprintf(buf, PAGE_SIZE,"0x%02x\n", val);
}

static int set_HDC2010_INT_level(int level)
{
	int err = 0;
	unsigned char data[2] = {0};

	//simulate GPIO by interrupt pin
	//use this pin to wakeup PDIC before send command to PDIC
	//printk("[INBOX] %s+ enable=%d",__func__,level);
	if(level){
		//set interrupt pole as active low
		data[0] = 0x0E;
		data[1] = 0x54;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x40), data, 2);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	}else{
		//set interrupt pole as active high
		data[0] = 0x0E;
		data[1] = 0x56;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x40), data, 2);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	}
	msleep(10);
	//printk("[INBOX] %s-",__func__);
	return err;
}

static int pd_wakeup(int enable)
{
	int ret = 0;

	if(enable){
		//keep low to wakeup PD
		set_HDC2010_INT_level(0);
	}else{
		set_HDC2010_INT_level(1);
	}
	msleep(30);
	return ret;
}

static ssize_t HDC2010_MANID_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val_L = 0x0, val_H = 0x0;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0xFC;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %02x, %02x, %02x\n", __func__, data[0], data[1], data[2]);
	printk("[ROG7_INBOX] HDC2010 MANID REG[0xFC]: 0x%02x\n", data[1]);
	val_L = data[1];
	
	memset(data, 0, 3);

	cmd[0] = 0xFD;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %02x, %02x, %02x\n", __func__, data[0], data[1], data[2]);
	printk("[ROG7_INBOX] HDC2010 MANID REG[0xFD]: 0x%02x\n", data[1]);
	val_H = data[1];

	kfree(data);

	return snprintf(buf, PAGE_SIZE,"0x%02x%02x\n", val_H, val_L);
}

static ssize_t HDC2010_INT_as_GPIO_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	set_HDC2010_INT_level(val);
	return count;
}

static ssize_t HDC2010_INT_as_GPIO_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int val = 0;

	return snprintf(buf, PAGE_SIZE,"%d\n",val);
}

static ssize_t HDC2010_DEVID_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val_L = 0x0, val_H = 0x0;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0xFE;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %02x, %02x, %02x\n", __func__, data[0], data[1], data[2]);
	printk("[ROG7_INBOX] HDC2010 DEVID REG[0xFE]: 0x%02x\n", data[1]);
	val_L = data[1];
	
	memset(data, 0, 3);

	cmd[0] = 0xFF;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG7_INBOX][%s] %02x, %02x, %02x\n", __func__, data[0], data[1], data[2]);
	printk("[ROG7_INBOX] HDC2010 DEVID REG[0xFF]: 0x%02x\n", data[1]);
	val_H = data[1];

	kfree(data);

	return snprintf(buf, PAGE_SIZE,"0x%02x%02x\n", val_H, val_L);
}

static ssize_t measure_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	ssize_t ret;
	unsigned char data[2] = {0};
	u8 cmd[3] = {0};
	u8 *config;
	u8 target_val = 0x00;
	u8 AMM = 0x00;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return count;

	config = kzalloc(3, GFP_KERNEL);
	memset(config, 0, 3);

	cmd[0] = 0x0E;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, config);
	if (err < 0){
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
		target_val = 0x56;
	}else{
		g_TH_meaure = (val==1)?0x01:0x00;
		AMM = (val==1)?0x50:0x00;
		target_val = (config[1] & 0x0F)|(AMM & 0xF0);
		printk("[ROG7_INBOX][%s] set 0x%x to 0x0E\n", __func__, target_val);
	}

	// 0x20 : 1/60Hz, 0x30 : 1/10Hz, 0x50 : 1Hz
	data[0] = 0x0E;
	data[1] = target_val;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x40), data, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	
	msleep(10);

	// 0x0 : no action, 0x1 : start
	data[0] = 0x0F;
	data[1] = g_TH_meaure;

	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x40), data, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	kfree(config);
	return count;
}

static ssize_t measure_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", g_TH_meaure);
}
/*
u8 read_HDC2010(u32 ID, u8 addr)
{
	int err = 0;
	u8 data[3] = {0};
	u8 cmd[1] = {0};

	if(ID != hid_report_id_aprom(addr_0x40))
	{
		printk("[ROG7_INBOX]%s error ID",__func__);
		return data[1];
	}
	msleep(10);
	cmd[1] = addr;
	err = asus_usb_hid_read_aprom(ID, cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	printk("[ROG7_INBOX] %s read %x from %x\n",__func__ ,data[1] ,addr);
	return data[1];
}

u8 write_HDC2010(u32 ID, u8 addr, u8 data)
{
	int err = 0;
	unsigned char cmd[2] = {0};

	if(ID != hid_report_id_aprom(addr_0x40))
	{
		printk("[ROG7_INBOX]%s error ID",__func__);
		return 0x0;
	}
	msleep(10);
	cmd[0] = addr;
	cmd[1] = data;

	err = asus_usb_hid_write_aprom(ID, cmd, 2);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);

	printk("[ROG7_INBOX] %s set %x to %x\n",__func__ ,data ,addr);
	return data;
}
*/
static ssize_t temperature_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 LSB = 0, MSB = 0x0;
	u32 temperature = 0;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x0;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	LSB = data[1];
	//printk("[ROG7_INBOX][%s] REG[0x%02x] %02x, %02x, %02x\n", __func__, cmd[0], data[0], data[1], data[2]);

	msleep(10);
	memset(data, 0, 3);
	cmd[0] = 0x1;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	MSB = data[1];
	//printk("[ROG7_INBOX][%s] REG[0x%02x] %02x, %02x, %02x\n", __func__, cmd[0], data[0], data[1], data[2]);

	kfree(data);

	temperature = ((((MSB << 8) + LSB)*160) >> 16)-40;
	printk("[ROG7_INBOX][%s] %d\n", __func__, temperature);

	return snprintf(buf, PAGE_SIZE,"%d\n", temperature);
}

static ssize_t humidity_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 LSB = 0, MSB = 0x0;
	u32 humidity = 0;
	u32 humidity_offset = 7;

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x2;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	LSB = data[1];
	//printk("[ROG7_INBOX][%s] REG[0x%02x] %02x, %02x, %02x\n", __func__, cmd[0], data[0], data[1], data[2]);

	msleep(10);
	memset(data, 0, 3);
	cmd[0] = 0x3;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x40), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	MSB = data[1];
	//printk("[ROG7_INBOX][%s] REG[0x%02x] %02x, %02x, %02x\n", __func__, cmd[0], data[0], data[1], data[2]);

	kfree(data);

	humidity = ((((MSB << 8) + LSB)*100) >> 16);
	humidity += humidity_offset;

	if(humidity > 100)
		humidity = 100;

	printk("[ROG7_INBOX][%s] %d\n", __func__, humidity);

	return snprintf(buf, PAGE_SIZE,"%d\n", humidity);
}

static ssize_t ic_switch_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	switch (IC_switch){
		case addr_0x16:
			printk("[ROG7_INBOX] Choose addr_0x16.\n");
		break;
		case addr_0x18:
			printk("[ROG7_INBOX] Choose addr_0x18.\n");
		break;
		case addr_0x75:
			printk("[ROG7_INBOX] Choose addr_0x75.\n");
		break;
		case addr_0x40:
			printk("[ROG7_INBOX] Choose addr_0x40.\n");
		break;
		default:
			printk("[ROG7_INBOX] unknown addr.\n");
		break;
	}

	return snprintf(buf, PAGE_SIZE,"%d\n", IC_switch);
}

static ssize_t ic_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;

	sscanf(buf, "%d", &val);

	if (val == 1)
		IC_switch = addr_0x16;
	else if (val == 2)
		IC_switch = addr_0x18;
	else if (val == 3)
		IC_switch = addr_0x75;
	else if (val == 4)
		IC_switch = addr_0x40;
	else{
		printk("[ROG7_INBOX] Input error I2C address.\n");
	}

	//printk("[ROG7_INBOX] IC switch 0x%x\n", IC_switch);
	return count;
}


int inbox7_power_source_swap(int direction)
{
	int ret = 0;
	int timeout = 10;

	//SR only support OTG Power source
	//ER1 need to rework
	if(g_HWID < AI2205_ER1 || !gInboxPowerSwap){
		printk("[ROG7_INBOX] HW not support swap to VBAT from OTG\n");
		return -1;
	}

	if(g_FAN_ID != FAN7_ID_PR){
		printk("[ROG7_INBOX] Only FANDG PR support power source swap\n");
		return -1;
	}
	mutex_lock(&power_lock);
	if(direction == POWER_VBAT && g_Power_source != POWER_VBAT){
		do{
			vph_output_side_port(1);
			timeout--;
			msleep(10);
		}while(!vph_get_status() && timeout!=0);

		msleep(100);
		if(vph_get_status()){
			printk("[ROG7_INBOX] switch power source to VBAT\n");
			pd_set_switch(6,0);
			//msleep(100);
			pd_set_switch(2,0);
			//msleep(100);
			pd_set_switch(4,1);
			//msleep(100);
			pd_set_switch(7,1);
			//msleep(100);
			g_Power_source = POWER_VBAT;
		}else{
			printk("[ROG7_INBOX] turn on VBAT fail\n");
			ret = -1;
		}

	}else if(direction == POWER_OTG && g_Power_source != POWER_OTG){
		printk("[ROG7_INBOX] switch power source to OTG\n");
		pd_set_switch(6,0);
		//msleep(100);
		pd_set_switch(2,0);
		//msleep(100);
		pd_set_switch(7,0);
		//msleep(100);
		pd_set_switch(4,0);
		msleep(100);

		do{
			vph_output_side_port(0);
			timeout--;
			msleep(10);
		}while(vph_get_status() && timeout!=0);

		if(vph_get_status()){
			printk("[ROG7_INBOX] turn off VBAT fail\n");
			ret = -1;
		}else{
			g_Power_source = POWER_OTG;
		}
	}else{
		printk("[ROG7_INBOX] %s power source didn't change, %d\n",__func__,direction);
	}
	mutex_unlock(&power_lock);
	return ret;
}

static int pd_get_PCBID()
{
	int err = 0;
	int id = 0;
	u8 reg_gp1 = 0;
	u8 reg_gp2 = 0;

	err = pd_byte_read_reg(GPIO_GP1, &reg_gp1);
	if(err < 0)
		printk("[ROG7_INBOX]%s get GPIO_GP1 failed\n",__func__);
	err = pd_byte_read_reg(GPIO_GP2, &reg_gp2);
	if(err < 0)
		printk("[ROG7_INBOX]%s get GPIO_GP2 failed\n",__func__);

	id = ((reg_gp1 & 0x10) >> 3) | ((reg_gp2 & 0x80) >> 7);
	printk("[ROG7_INBOX]%s get PCB ID = %d\n",__func__ ,id);
	return id;
}

static ssize_t pd_fw_date_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 year = 0;
	u8 mon = 0;
	u8 day = 0;

	err = pd_byte_read_reg(VERSION_1, &year);
	//msleep(200);
	err = pd_byte_read_reg(VERSION_2, &mon);
	//msleep(200);
	err = pd_byte_read_reg(VERSION_3, &day);
	//msleep(200);
	printk("[ROG7_INBOX]%s %d-%d-%d\n",__func__ ,year,mon,day);
	return snprintf(buf, PAGE_SIZE,"%d-%d-%d\n",year,mon,day);
}

static ssize_t pd_VendorID_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u16 PD_VID = 0;
	u8 LSB = 0, MSB = 0;

	pd_byte_read_reg(VENDOR_ID_MSB, &MSB);
	pd_byte_read_reg(VENDOR_ID_LSB, &LSB);
	PD_VID = (MSB<<8)|LSB;
	return snprintf(buf, PAGE_SIZE,"%x\n", PD_VID);
}

static ssize_t pd_DeviceID_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u16 PD_DID = 0;
	u8 LSB = 0, MSB = 0;

	pd_byte_read_reg(DEVICE_ID_MSB, &MSB);
	pd_byte_read_reg(DEVICE_ID_LSB, &LSB);
	PD_DID = (MSB<<8)|LSB;
	return snprintf(buf, PAGE_SIZE,"%x\n", PD_DID);
}

static ssize_t pd_gpio_gp0_ctl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	u8 gpio_flag = 0;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;

	gpio_flag = val & 0xFF;
	pd_byte_write_reg(GPIO_GP0, gpio_flag);
	return count;
}
static ssize_t pd_gpio_gp0_ctl_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 reg_val = 0;

	pd_byte_read_reg(GPIO_GP0, &reg_val);
	return snprintf(buf, PAGE_SIZE,"%x\n", reg_val);
}

static ssize_t pd_gpio_gp1_ctl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	u8 gpio_flag = 0;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;

	gpio_flag = val & 0xFF;
	pd_byte_write_reg(GPIO_GP1, gpio_flag);
	return count;
}
static ssize_t pd_gpio_gp1_ctl_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 reg_val = 0;

	pd_byte_read_reg(GPIO_GP1, &reg_val);
	return snprintf(buf, PAGE_SIZE,"%x\n", reg_val);
}

static ssize_t pd_gpio_gp2_ctl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	u8 gpio_flag = 0;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;

	gpio_flag = val & 0xFF;
	pd_byte_write_reg(GPIO_GP2, gpio_flag);
	return count;
}

static ssize_t pd_set_OV_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;

	if(val>4 || val <0)
	{
		printk("[ROG7_INBOX]%s: para is invalid",__func__);
	}

	pd_set_OV(val);
	return count;
}

static ssize_t pd_gpio_gp2_ctl_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 reg_val = 0;

	pd_byte_read_reg(GPIO_GP2, &reg_val);
	return snprintf(buf, PAGE_SIZE,"%x\n", reg_val);
}

static ssize_t pd_ISP_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;
	unsigned char data[3] = {0};

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;

	printk("[ROG7_INBOX] %s+\n",__func__);
	if (val > 0){
		//enter ISP
		data[0] = 0x0F;
		data[1] = 0x40;
		data[2] = 0x00;
		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x64), data, 3);
		if (err < 0){
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
		}else{
			printk("[ROG7_INBOX] %s+ enter ISP\n",__func__);
			g_PD_updating = true;
		}
	}else{
		//leave ISP
		data[0] = 0x20;
		data[1] = 0x00;
		data[2] = 0x00;
		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x28), data, 3);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
		else
			printk("[ROG7_INBOX] %s+ leave ISP\n",__func__);
	}
	msleep(10);

	printk("[ROG7_INBOX] %s-\n",__func__);
	return count;
}
static ssize_t pd_ISP_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x0F;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x64), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	return snprintf(buf, PAGE_SIZE,"%x\n", data[1]);
}

static ssize_t pd_checksum_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[3] = {0};

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	cmd[0] = 0x8A;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x28), cmd, 1, data);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", err);
	else
		printk("[ROG7_INBOX] %s checksum=0x%x (%x, %x, %x)\n", __func__,   data[1],data[0],data[1],data[2]);
	return snprintf(buf, PAGE_SIZE,"%x\n", data[1]);
}

static ssize_t pd_debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;

	printk("[ROG7_INBOX]%s debug = %d\n",__func__,val);

	return count;
}
static ssize_t pd_update_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int ret = 0;
	int err = 0;

	const struct firmware *fw = NULL;
	u32 block_size = 0;
	u8 block_idx = 0, block_offset = 0;
	u8	buffer[16] = {0};
	//u8	buffer_readback[16] = {0};

	printk("[ROG7_INBOX]%s+\n",__func__);
	usb_autopm_put_interface(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent));
	//get firmware
	err = request_firmware(&fw, AURA_INBOX_PD_FILE_NAME, dev);
	if (err) {
		printk("[ROG7_INBOX][%s] Error: request_firmware failed!!!, Err %d",__func__,err);
		ret = err;
		goto err;
	}

	block_size = fw->size / 512;
	if ((fw->size % 512) != 0)
		++block_size;
	printk("[ROG7_INBOX][%s] block_size = %d fw_size=%d\n",__func__,block_size,fw->size);
	//start update PD
	for(block_idx = 0; block_idx<block_size; block_idx++)
	{
		//command SET INDEX COMMAND block_idx
		pd_isp_byte_write_reg(0x01, block_idx);
		printk("[ROG7_INBOX][%s] write block index = %d\n",__func__,block_idx);
		for(block_offset = 0; block_offset<32; block_offset++){
			memcpy(buffer, (fw->data + block_idx*512 + block_offset*16), 16);
			//0x41 command 16 byte write
			if(pd_isp_16byte_write(buffer) < 0){
				printk("[ROG7_INBOX][%s] write failed, index = %d,offset = %d\n",__func__,block_idx,block_offset);
				ret = -1;
				goto err;
			}else{
				//verify
				//pd_isp_16byte_read(buffer_readback);
				//memcmp buffer_readback and buffer

			}
		}
		msleep(50);	//spec define
	}
	// Exit ISP mode
	pd_isp_byte_write_reg(0x02, 0x0);
	g_PD_updating = false;
err:
	release_firmware(fw);
	usb_autopm_put_interface(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent));
	printk("[ROG7_INBOX]%s-\n",__func__);
	return snprintf(buf, PAGE_SIZE,"%x\n", ret);
}

static ssize_t pd_set_power_source_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int err = 0;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return count;
	if (inbox7_power_source_swap(val) < 0)
		printk("[ROG7_INBOX]%s swap power failed\n",__func__);
	else
		printk("[ROG7_INBOX]%s set power source to %s\n",__func__, val?"VBAT":"OTG");
	return count;
}

static int get_hall_sensor_status(void)
{
/*
	int ret = 0;
	u8 *data;
	u8 cmd[3] = {0};
	u8 val[3] = {0};

	if(IC_switch == addr_0x16){
		cmd[0] = 0x60;
		cmd[1] = 0x05;
		cmd[2] = 0x00;
	}else if(IC_switch == addr_0x18){
		cmd[0] = 0x60;
		cmd[1] = 0x06;
		cmd[2] = 0x00;
	}else{
		printk("[ROG7_INBOX] %s not support ic = %d\n", __func__ ,IC_switch);
		ret = -1;
		goto err;
	}

	data = kzalloc(3, GFP_KERNEL);
	memset(data, 0, 3);

	ret = asus_usb_hid_read_aprom(hid_report_id_aprom(IC_switch), cmd, 2, data);
	if (ret < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_aprom:err %d\n", ret);

	//printk("[ROG7_INBOX][%s] %x, %x, %x\n", __func__, data[0], data[1], data[2]);

	memcpy(val, data, FEATURE_READ_COMMAND_SIZE);
	kfree(data);

	printk("[ROG7_INBOX][%s] IC:%d, Hall sensor status = %d\n", __func__, IC_switch, val[1]);
	ret = val[1];
err:
	return ret;
*/
	int ret = 0;
	int err = 0;
	u8 reg_val = 0;

	err = pd_byte_read_reg(GPIO_GP1, &reg_val);
	if(err < 0){
		printk("[ROG7_INBOX][%s] err=%d\n", __func__,err);
		return err;
	}else{
		if(DET_INT & reg_val){
			ret = 1;
		}else{
			ret = 0;
		}
	}
	printk("[ROG7_INBOX][%s] status = %d\n", __func__, ret);
	return ret;
}

static ssize_t get_hall_status_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE,"%d\n", get_hall_sensor_status());
}

static ssize_t codec_cali_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	struct hid_device *hdev;
	int ret = 0;
	//int i = 0;
	char *buffer;
	u8 cali_data[5] = {0};
	int retry=10;

	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] rog6_inbox_hidraw is NULL !\n");
		return -1;
	}

	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = 0x18;
	buffer[1] = 0x03;
	buffer[2] = 0x03;
	buffer[3] = 0x00;
	buffer[4] = 0x05;

	//for ( i=0; i<FEATURE_WRITE_COMMAND_SIZE; i++ )
		//printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);
	do{
		hid_hw_power(hdev, PM_HINT_FULLON);
		ret = hid_hw_raw_request(hdev, buffer[0], cali_data, FEATURE_WRITE_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
		if (ret < 0)
			printk("[ROG7_INBOX] hid_hw_raw_request HID_REQ_GET_REPORT fail: ret = %d, retry = %d\n", ret, retry);
		else
			printk("[ROG7_INBOX] %s get cali data %x %x %x %x, report id= 0x%x\n",__func__, cali_data[4], cali_data[3], cali_data[2], cali_data[1], cali_data[0]);
		msleep(100);
		retry--;
	}while(ret == -EAGAIN && retry > 0);

	hid_hw_power(hdev, PM_HINT_NORMAL);

	//for ( i=0; i<FEATURE_READ_COMMAND_SIZE; i++ )
		//printk("[ROG7_INBOX][%s] data[%d] = 0x%02x\n", __func__, i, data[i]);

	kfree(buffer);
	mutex_unlock(&hid_command_lock);

	return snprintf(buf, PAGE_SIZE,"0x%x%x%x%x\n", cali_data[4], cali_data[3], cali_data[2], cali_data[1]);
}

static ssize_t codec_cali_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 cali_data;
	u8 *buffer;
	int err = 0;
	int ret = 0;
	struct hid_device *hdev;

	err = kstrtou32(buf, 10, &cali_data);
	if (err)
		return count;

	printk("[ROG7_INBOX] %s, cali_data %d\n", __func__, cali_data);

	mutex_lock(&hid_command_lock);
	buffer = kzalloc(FEATURE_WRITE_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(buffer,0,FEATURE_WRITE_LONG_COMMAND_SIZE);
	hdev = rog6_inbox_hidraw->hid;

	buffer[0] = 0x18;
	buffer[1] = (u8)(cali_data & 0xff);
	buffer[2] = (u8)((cali_data >> 8) & 0xff);
	buffer[3] = (u8)((cali_data >> 16) & 0xff);
	buffer[4] = (u8)((cali_data >> 24) & 0xff);

//	for ( i=0; i<FEATURE_WRITE_LONG_COMMAND_SIZE; i++ )
//		printk("[ROG7_INBOX][%s] buffer[%d] = 0x%02x\n", __func__, i, buffer[i]);

	hid_hw_power(hdev, PM_HINT_FULLON);
	ret = hid_hw_raw_request(hdev, buffer[0], buffer, FEATURE_WRITE_LONG_COMMAND_SIZE,
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		printk("[ROG7_INBOX] hid_hw_raw_request fail: ret = %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	mutex_unlock(&hid_command_lock);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	else
		printk("[ROG7_INBOX] %s set calibration data %x %x %x %x\n", __func__,buffer[4],buffer[3],buffer[2],buffer[1]);
	return count;
}

static int get_ms51_sprom(u8 *buf)
{
	int err = 0;
	int offset = 0;
	u8 *data;
	u8 cmd[38] = {0};

	data = kzalloc(FEATURE_READ_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(data, 0, FEATURE_READ_LONG_COMMAND_SIZE);

	do{
		printk("[ROG7_INBOX] %s offset = %d\n", __func__ ,offset);
		cmd[0] = 0xCB;
		cmd[1] = 0x05;
		cmd[2] = offset;
		err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);

		msleep(10);

		cmd[0] = 0xCC;
		cmd[1] = 0x00;
		cmd[2] = 0x00;

		err = asus_usb_hid_read_long_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 1, data, 12);
		if (err < 0)
			printk("[ROG7_INBOX] asus_usb_hid_read_long_cmd:err %d\n", err);
		else{
			printk("[ROG7_INBOX] %s get sprom %x %x %x %x %x %x %x %x %x %x %x %x\n",__func__ ,data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12]);
			memcpy(buf+offset,data+1,12);
		}
		offset += 12;
	}while(offset <= 24);

	kfree(data);

	return err;
}

static int set_ms51_sprom(u8 *buf)
{
	int err = 0;
	u8 cmd[38] = {0};

	cmd[0] = 0xCB;
	cmd[1] = 0x04;
	memcpy(cmd+2, buf, 36);

	err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 38);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);

	return err;
}

static ssize_t ms51_sprom_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 sprom[SPROM_SIZE]={0};
	get_ms51_sprom(sprom);
	return snprintf(buf, PAGE_SIZE,"%02x %02x %02x %02x\n",sprom[SPROM_SIZE-4] , sprom[SPROM_SIZE-3], sprom[SPROM_SIZE-2], sprom[SPROM_SIZE-1]);
}

static ssize_t ms51_sprom_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 cmd[SPROM_SIZE] = {	0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,
					0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
					0x11,0x22,0x33,0x44,0x55,0x66,
					0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
					0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
					0xAA,0xBB,0xCC,0xDD,0xEE,0xFF	};

	set_ms51_sprom(cmd);
	return count;
}

static ssize_t ms51_codec_cali_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 *data;
	u32 calibaration_data = 0;
	u8 cmd[3] = {0};
	int err = 0;

	data = kzalloc(FEATURE_READ_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(data, 0, FEATURE_READ_LONG_COMMAND_SIZE);

	cmd[0] = 0xCB;
	cmd[1] = 0x05;
	cmd[2] = 0x18;
	err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 3);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);

	msleep(10);

	cmd[0] = 0xCC;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	err = asus_usb_hid_read_long_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 1, data, 12);
	if (err < 0)
		printk("[ROG7_INBOX] asus_usb_hid_read_long_cmd:err %d\n", err);
	else
		printk("[ROG7_INBOX] %s get sprom %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",__func__ ,data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13]);

	calibaration_data = (data[9]<<24) | (data[10]<<16) | (data[11]<<8) | data[12];

	kfree(data);
	return snprintf(buf, PAGE_SIZE,"%d\n",calibaration_data);
}
static ssize_t ms51_codec_cali_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	int ret = 0;
	int offset = 0;
	u32 cali_val = 0;
	u8 sprom[36] = {0};
	u8 *data;
	u8 cmd[38] = {0};

	ret = kstrtou32(buf, 10, &cali_val);
	if (ret)
		return count;

	data = kzalloc(FEATURE_READ_LONG_COMMAND_SIZE, GFP_KERNEL);
	memset(data, 0, FEATURE_READ_LONG_COMMAND_SIZE);

	//======get sprom 36 bytes===========
	do{
		cmd[0] = 0xCB;
		cmd[1] = 0x05;
		cmd[2] = offset;
		err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 3);
		if (err < 0)
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);

		msleep(10);

		cmd[0] = 0xCC;
		cmd[1] = 0x00;
		cmd[2] = 0x00;

		err = asus_usb_hid_read_long_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 1, data, 12);
		if (err < 0)
			printk("[ROG7_INBOX] asus_usb_hid_read_long_cmd:err %d\n", err);
		else{
			printk("[ROG7_INBOX] %s get sprom %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",__func__ ,data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13]);
			memcpy(sprom+offset,data+1,12);
		}
		offset += 12;
	}while(offset <= 24);
	//========modify audio calibration data=======

	sprom[32] = (u8)(cali_val>>24 & 0xff);
	sprom[33] = (u8)(cali_val>>16 & 0xff);
	sprom[34] = (u8)(cali_val>>8 & 0xff);
	sprom[35] = (u8)(cali_val & 0xff);
	printk("[ROG7_INBOX] pack cali to SPROM %x %x %x %x\n",sprom[32],sprom[33],sprom[34],sprom[35]);

	//===========Store data to sprom==============
	cmd[0] = 0xCB;
	cmd[1] = 0x04;
	memcpy(cmd+2, sprom, 36);

	err = asus_usb_hid_write_log_cmd(hid_report_id_long_cmd(addr_0x16), cmd, 38);
	if (err < 0)
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_log_cmd:err %d\n", __func__, err);
	else
		printk("[ROG7_INBOX][%s] set sprom %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", __func__,cmd[2],cmd[3],cmd[4],cmd[5],cmd[6],cmd[7],cmd[8],cmd[9],cmd[10],cmd[11],cmd[12],cmd[13],cmd[14],cmd[15],cmd[16],cmd[17],
																																									cmd[18],cmd[19],cmd[20],cmd[21],cmd[22],cmd[23],cmd[24],cmd[25],cmd[26],cmd[27],cmd[28],cmd[29],cmd[30],cmd[31],cmd[32],cmd[33],cmd[34],cmd[35],cmd[36],cmd[37]);
	kfree(data);
	return count;
}

static ssize_t ms51_sn1_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	u8 sprom[SPROM_SIZE]={0};
	get_ms51_sprom(sprom);
	return snprintf(buf, PAGE_SIZE,"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",sprom[0] , sprom[1], sprom[2], sprom[3]
																													  ,sprom[4] , sprom[5], sprom[6], sprom[7]
																													  ,sprom[8] , sprom[9], sprom[10], sprom[11]
																													  ,sprom[12] , sprom[13], sprom[14], sprom[15]);
}

static ssize_t ms51_sn1_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 sprom[SPROM_SIZE]={0};
	u64 serialnumber1 = 0;
	int i=0;
	int ret = 0;

	ret = kstrtou64(buf, 16, &serialnumber1);
	if (ret)
		return count;

	printk("[ROG7_INBOX] %s input:0x16%x\n",__func__,serialnumber1);
	get_ms51_sprom(sprom);

	printk("[ROG7_INBOX] get SN1 %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",sprom[0] , sprom[1], sprom[2], sprom[3]
																													  ,sprom[4] , sprom[5], sprom[6], sprom[7]
																													  ,sprom[8] , sprom[9], sprom[10], sprom[11]
																													  ,sprom[12] , sprom[13], sprom[14], sprom[15]);
	memset(sprom, 0, 16);

	for(i=0; i<8; i++)
		sprom[i] = (u8)((serialnumber1>>(64-((i+1)*8))) & 0xff);

	printk("[ROG7_INBOX] set SN1 %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",sprom[0] , sprom[1], sprom[2], sprom[3]
																													  ,sprom[4] , sprom[5], sprom[6], sprom[7]
																													  ,sprom[8] , sprom[9], sprom[10], sprom[11]
																													  ,sprom[12] , sprom[13], sprom[14], sprom[15]);
	set_ms51_sprom(sprom);

	return count;
}

static ssize_t ambient_temperature_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[1] = {0};
	int ambient_temperature = 0;

	data = kzalloc(FEATURE_READ_COMMAND_SIZE, GFP_KERNEL);
	memset(data, 0, FEATURE_READ_COMMAND_SIZE);

	cmd[0] = 0xCF;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_0x16), cmd, 1, data);
	if (err < 0)
		printk("[ROG6_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	//printk("[ROG6_INBOX][%s] REG[0x%02x] %02x, %02x\n", __func__, cmd[0], data[0], data[1]);
	if (*data > 0xF0)
		ambient_temperature = (-1) * (int)(data[1] & 0x0F);
	else
		ambient_temperature = (int)data[1];

	kfree(data);

	printk("[ROG6_INBOX][%s] %d\n", __func__, ambient_temperature);

	return snprintf(buf, PAGE_SIZE,"%d\n", ambient_temperature);
}

static u8 mps_lpm_mode_enable(int enable)
{
	u8 ret = 0;
	int err = 0;
	unsigned char data[3] = {0};

	if (enable) {
		data[0] = 0x04;
		data[1] = 0xE4;
		data[2] = 0x00;
		printk("[ROG7_INBOX] MPS enter LPM\n");
	}else {
		data[0] = 0x04;
		data[1] = 0xF0;
		data[2] = 0x00;
		printk("[ROG7_INBOX] MPS exit LPM\n");
	}
	err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_MPS), data, 2);
	if (err < 0) {
		printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
	}
	return ret;
}

static ssize_t mps_lpm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret) {
		return count;
	}

	val = mps_lpm_mode_enable(val);
	g_mps_lpm = val;
	return count;
}
static ssize_t mps_lpm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	u8 *data;
	u8 cmd[1] = {0};

	data = kzalloc(FEATURE_READ_COMMAND_SIZE, GFP_KERNEL);
	memset(data, 0, FEATURE_READ_COMMAND_SIZE);

	cmd[0] = 0x04;

	err = asus_usb_hid_read_aprom(hid_report_id_aprom(addr_MPS), cmd, 1, data);
	if (err < 0)
		printk("[ROG6_INBOX] asus_usb_hid_read_aprom:err %d\n", err);

	printk("[ROG7_INBOX] %s+ LPM %d, reg = 0x%x",__func__, g_mps_lpm ,data[1]);
	kfree(data);
	return snprintf(buf, PAGE_SIZE,"%d\n", g_mps_lpm);
}

static int aura_lpm_enable(int enable)
{
	int ret = 0;
	int err = 0;
	unsigned char data[3] = {0};

	if (enable) {
		data[0] = 0x80;
		data[1] = 0x30;
		data[2] = 0x01;

		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x16), data, 3);
		if (err < 0) {
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
			return -EIO;
		}
		err = asus_usb_hid_write_aprom(hid_report_id_aprom(addr_0x18), data, 3);
		if (err < 0) {
			printk("[ROG7_INBOX][%s] asus_usb_hid_write_aprom:err %d\n", __func__, err);
			return -EIO;
		}

		printk("[ROG7_INBOX] %s Aura enter LPM\n",__func__);
	}else {
		// get int level
		set_HDC2010_INT_level(1);
		set_HDC2010_INT_level(0);
		printk("[ROG7_INBOX] %s Aura exit LPM\n",__func__);
	}

	return ret;
}

static ssize_t aura_lpm_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret) {
		g_aura_lpm = -1;
		return count;
	}

	if( val == 0){
		//wakeup immediately
		mps_lpm_mode_enable(0);
		aura_lpm_enable(0);
	}
	g_aura_lpm = val;
	printk("[ROG7_INBOX] set lpm flag %d\n",g_aura_lpm);

	return count;
}
static ssize_t aura_lpm_mode_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	printk("[ROG7_INBOX] %s+ LPM %d",__func__, g_aura_lpm);
	return snprintf(buf, PAGE_SIZE,"%d\n", g_aura_lpm);
}

void adapter_plug_in(int plug_in)
{
	printk("[ROG7_INBOX]%s %d\n",__func__,plug_in);
	g_Adapter_online = plug_in;
}
EXPORT_SYMBOL_GPL(adapter_plug_in);

void bypass_charging_notify_callback(int enable)
{
	printk("[ROG7_INBOX]%s+ %d\n",__func__,enable);
	g_bypass_charging = enable;

	if(rog6_inbox_hidraw == NULL){
		printk("[ROG7_INBOX]%s: skip power source swap\n",__func__);
		return;
	}

	//power source switch to 5V if bypass charging enable (TT382182)
	if(enable){
		inbox7_power_source_swap(POWER_OTG);
	}else{
		inbox7_power_source_swap(POWER_VBAT);
	}
	//set cooler again cause inbox7_power_source_swap will disable cooler
	pd_set_cooler_stage(g_cooling_stage,true);
	printk("[ROG7_INBOX]%s-\n",__func__,enable);
}
EXPORT_SYMBOL_GPL(bypass_charging_notify_callback);

static DEVICE_ATTR(gpio8, 0664, gpio8_show, gpio8_store);
static DEVICE_ATTR(gpio9, 0664, gpio9_show, gpio9_store);
static DEVICE_ATTR(gpio10, 0664, gpio10_show, gpio10_store);
static DEVICE_ATTR(gpio11, 0664, gpio11_show, gpio11_store);
static DEVICE_ATTR(led_test, 0664, NULL, led_test_store);
static DEVICE_ATTR(red_pwm, 0664, red_pwm_show, red_pwm_store);
static DEVICE_ATTR(green_pwm, 0664, green_pwm_show, green_pwm_store);
static DEVICE_ATTR(blue_pwm, 0664, blue_pwm_show, blue_pwm_store);
static DEVICE_ATTR(red1_pwm, 0664, red1_pwm_show, red1_pwm_store);
static DEVICE_ATTR(green1_pwm, 0664, green1_pwm_show, green1_pwm_store);
static DEVICE_ATTR(blue1_pwm, 0664, blue1_pwm_show, blue1_pwm_store);
static DEVICE_ATTR(red2_pwm, 0664, red2_pwm_show, red2_pwm_store);
static DEVICE_ATTR(green2_pwm, 0664, green2_pwm_show, green2_pwm_store);
static DEVICE_ATTR(blue2_pwm, 0664, blue2_pwm_show, blue2_pwm_store);
static DEVICE_ATTR(apply, 0664, apply_show, apply_store);
static DEVICE_ATTR(mode, 0664, mode_show, mode_store);
static DEVICE_ATTR(frame, 0664, get_frame, set_frame);
static DEVICE_ATTR(speed, 0664, get_speed, set_speed);
static DEVICE_ATTR(aura_lpm_mode, 0664, aura_lpm_mode_show, aura_lpm_mode_store);
static DEVICE_ATTR(Calibration, 0664, get_cali_data, set_cali_data);
static DEVICE_ATTR(fw_mode, 0664, fw_mode_show, NULL);
static DEVICE_ATTR(fw_ver, 0664, fw_ver_show, NULL);
static DEVICE_ATTR(fw_update, 0664, fw_update_show, fw_update_store);
static DEVICE_ATTR(ap2ld, 0664, ap2ld_show, ap2ld_store);
static DEVICE_ATTR(ld2ap, 0664, NULL, ld2ap_store);
static DEVICE_ATTR(led_on, 0664, led_on_show, led_on_store);
static DEVICE_ATTR(door_on, 0664, door_on_show, door_on_store);
static DEVICE_ATTR(logo_on, 0664, logo_on_show, logo_on_store);
static DEVICE_ATTR(fan_enable, 0664, NULL, fan_enable_store);
static DEVICE_ATTR(fan_RPM, 0664, fan_rpm_show, fan_rpm_store);
static DEVICE_ATTR(fan_PWM, 0664, fan_pwm_show, fan_pwm_store);
static DEVICE_ATTR(inbox_user_type, 0664, NULL, inbox_user_fan);
static DEVICE_ATTR(inbox_thermal_type, 0664, NULL, inbox_thermal_fan);
static DEVICE_ATTR(unique_id, 0664, unique_id_show, NULL);
static DEVICE_ATTR(mode2, 0664, mode2_show, mode2_store);
static DEVICE_ATTR(key_state, 0664, key_state_show, NULL);
static DEVICE_ATTR(keyboard_mode, 0664, NULL, keyboard_mode_store);
static DEVICE_ATTR(cooling_en, 0664, cooling_en_show, cooling_en_store);
static DEVICE_ATTR(cooling_stage, 0664, cooling_stage_show, cooling_stage_store);
static DEVICE_ATTR(cooling_stage2, 0664, cooling_stage2_show, cooling_stage2_store);
static DEVICE_ATTR(MPS_ID, 0664, MPS_ID_show, NULL);
static DEVICE_ATTR(HDC2010_MANID, 0664, HDC2010_MANID_show, NULL);
static DEVICE_ATTR(HDC2010_DEVID, 0664, HDC2010_DEVID_show, NULL);
static DEVICE_ATTR(HDC2010_INT_as_GPIO, 0664, HDC2010_INT_as_GPIO_show, HDC2010_INT_as_GPIO_store);
static DEVICE_ATTR(measure, 0664, measure_show, measure_store);
static DEVICE_ATTR(temperature, 0664, temperature_show, NULL);
static DEVICE_ATTR(humidity, 0664, humidity_show, NULL);
static DEVICE_ATTR(ambient_temperature, 0664, ambient_temperature_show, NULL);
static DEVICE_ATTR(ic_switch, 0664, ic_switch_show, ic_switch_store);
static DEVICE_ATTR(pd_VID, 0664, pd_VendorID_show, NULL);
static DEVICE_ATTR(pd_DID, 0664, pd_DeviceID_show, NULL);
static DEVICE_ATTR(pd_gpio_gp0_ctl, 0664, pd_gpio_gp0_ctl_show, pd_gpio_gp0_ctl_store);
static DEVICE_ATTR(pd_gpio_gp1_ctl, 0664, pd_gpio_gp1_ctl_show, pd_gpio_gp1_ctl_store);
static DEVICE_ATTR(pd_gpio_gp2_ctl, 0664, pd_gpio_gp2_ctl_show, pd_gpio_gp2_ctl_store);
static DEVICE_ATTR(pd_set_OV, 0664, NULL, pd_set_OV_store);
static DEVICE_ATTR(pd_set_power_source, 0664, NULL, pd_set_power_source_store);
static DEVICE_ATTR(pd_ISP, 0664, pd_ISP_show, pd_ISP_store);
static DEVICE_ATTR(pd_update, 0664, pd_update_show, NULL);
static DEVICE_ATTR(pd_fw_date, 0664, pd_fw_date_show, NULL);
static DEVICE_ATTR(pd_checksum, 0664, pd_checksum_show, NULL);
static DEVICE_ATTR(pd_debug, 0664, NULL, pd_debug_store);
static DEVICE_ATTR(mps_lpm, 0664, mps_lpm_show, mps_lpm_store);
static DEVICE_ATTR(get_hall_status, 0664, get_hall_status_show, NULL);
static DEVICE_ATTR(codec_cali, 0664, codec_cali_show, codec_cali_store);
static DEVICE_ATTR(ms51_sprom, 0664, ms51_sprom_show, ms51_sprom_store);
static DEVICE_ATTR(ms51_codec_cali, 0664, ms51_codec_cali_show, ms51_codec_cali_store);
static DEVICE_ATTR(ms51_sn1, 0664, ms51_sn1_show, ms51_sn1_store);
//static DEVICE_ATTR(ms51_sn2, 0664, ms51_sn2_show, ms51_sn2_store);


static struct attribute *pwm_attrs[] = {
	&dev_attr_gpio8.attr,
	&dev_attr_gpio9.attr,
	&dev_attr_gpio10.attr,
	&dev_attr_gpio11.attr,
	&dev_attr_led_test.attr,
	&dev_attr_red_pwm.attr,
	&dev_attr_green_pwm.attr,
	&dev_attr_blue_pwm.attr,
	&dev_attr_red1_pwm.attr,
	&dev_attr_green1_pwm.attr,
	&dev_attr_blue1_pwm.attr,
	&dev_attr_red2_pwm.attr,
	&dev_attr_green2_pwm.attr,
	&dev_attr_blue2_pwm.attr,
	&dev_attr_apply.attr,
	&dev_attr_mode.attr,
	&dev_attr_frame.attr,
	&dev_attr_speed.attr,
	&dev_attr_aura_lpm_mode.attr,
	&dev_attr_Calibration.attr,
	&dev_attr_fw_mode.attr,
	&dev_attr_fw_ver.attr,
	&dev_attr_fw_update.attr,
	&dev_attr_ap2ld.attr,
	&dev_attr_ld2ap.attr,
	&dev_attr_led_on.attr,
	&dev_attr_door_on.attr,
	&dev_attr_logo_on.attr,
	&dev_attr_fan_enable.attr,
	&dev_attr_fan_RPM.attr,
	&dev_attr_fan_PWM.attr,
	&dev_attr_inbox_user_type.attr,
	&dev_attr_inbox_thermal_type.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_mode2.attr,
	&dev_attr_key_state.attr,
	&dev_attr_keyboard_mode.attr,
	&dev_attr_cooling_en.attr,
	&dev_attr_cooling_stage.attr,
	&dev_attr_cooling_stage2.attr,
	&dev_attr_MPS_ID.attr,
	&dev_attr_HDC2010_MANID.attr,
	&dev_attr_HDC2010_DEVID.attr,
	&dev_attr_HDC2010_INT_as_GPIO.attr,
	&dev_attr_measure.attr,
	&dev_attr_temperature.attr,
	&dev_attr_humidity.attr,
	&dev_attr_ambient_temperature.attr,
	&dev_attr_ic_switch.attr,
	&dev_attr_pd_VID.attr,
	&dev_attr_pd_DID.attr,
	&dev_attr_pd_gpio_gp0_ctl.attr,
	&dev_attr_pd_gpio_gp1_ctl.attr,
	&dev_attr_pd_gpio_gp2_ctl.attr,
	&dev_attr_pd_set_OV.attr,
	&dev_attr_pd_set_power_source.attr,
	&dev_attr_pd_ISP.attr,
	&dev_attr_pd_update.attr,
	&dev_attr_pd_fw_date.attr,
	&dev_attr_pd_checksum.attr,
	&dev_attr_pd_debug.attr,
	&dev_attr_mps_lpm.attr,
	&dev_attr_get_hall_status.attr,
	&dev_attr_codec_cali.attr,
	&dev_attr_ms51_sprom.attr,
	&dev_attr_ms51_codec_cali.attr,
	&dev_attr_ms51_sn1.attr,
	//&dev_attr_ms51_sn2.attr,
	NULL
};

static const struct attribute_group pwm_attr_group = {
	.attrs = pwm_attrs,
};

static void aura_sync_set(struct led_classdev *led,
			      enum led_brightness brightness)
{
}

static enum led_brightness aura_sync_get(struct led_classdev *led_cdev)
{
	struct inbox_drvdata *data;

	data = container_of(led_cdev, struct inbox_drvdata, led);

	return data->led.brightness;
}

static int aura_sync_register(struct device *dev, struct inbox_drvdata *data)
{
	data->led.name = "aura_inbox";

	data->led.brightness = LED_OFF;
	data->led.max_brightness = LED_HALF;
	data->led.default_trigger = "none";
	data->led.brightness_set = aura_sync_set;
	data->led.brightness_get = aura_sync_get;

	return led_classdev_register(dev, &data->led);
}
static void aura_sync_unregister(struct inbox_drvdata *data)
{
	led_classdev_unregister(&data->led);
}

#ifdef CONFIG_PM
static int rog7_inbox_usb_resume(struct hid_device *hdev)
{
	if(g_Charger_mode) {
		printk("[ROG7_INBOX] In charger mode, stop %s\n",__func__);
		return 0;
	}

	printk("%s\n", __func__);
	return 0;
}

static int rog7_inbox_usb_suspend(struct hid_device *hdev, pm_message_t message)
{
	if(g_Charger_mode) {
		printk("[ROG7_INBOX] In charger mode, stop %s\n",__func__);
		return 0;
	}
	
	printk("%s\n", __func__);
	return 0;
}
#endif /* CONFIG_PM */

static int rog7_inbox_usb_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	return 0;
}

void disable_autosuspend_worker(struct work_struct *work)
{
	if (rog6_inbox_hidraw == NULL) {
		printk("[ROG7_INBOX] %s : rog6_inbox_hidraw is NULL.\n", __func__);
		return;
	}
		
	//usb_disable_autosuspend(interface_to_usbdev(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent)));
}

static int latch_det_handler(void *data)
{
	int fandg_status = -1;
	int last_fandg_status = -1;
	int last_latch_status = -1;

	printk("[ROG7_INBOX]%s++\n", __func__);
	do{
		msleep(1000);

		if((gPanelStatus == DRM_PANEL_EVENT_UNBLANK) && (!g_Aura_ic_updating)){
			g_latch_status = get_hall_sensor_status();
			if(g_latch_status == 1 && last_latch_status != g_latch_status){
				//printk("[ROG7_INBOX]%s: latch on\n", __func__);
				if(!g_bypass_charging)
					inbox7_power_source_swap(POWER_VBAT);
				else
					printk("[ROG7_INBOX]%s: skip power source swap to VBAT, cause bypass charging\n", __func__);
				FANDG_USBID_detect = true;
				fandg_status = 2;
				headset_enable(1);
				last_latch_status = g_latch_status;
			}else if(g_latch_status == 0 && last_latch_status != g_latch_status){
				//printk("[ROG7_INBOX]%s: latch off\n", __func__);
				reset_status();
				FANDG_USBID_detect = false;
				fandg_status = 0;
				headset_enable(0);
				last_latch_status = g_latch_status;
			}
			if(last_fandg_status != fandg_status){
				FANDG_connect(fandg_status);
				last_fandg_status = fandg_status;
			}
		}else{
			;//printk("[ROG7_INBOX]%s skip check latch\n", __func__);
		}
	}while(!kthread_should_stop());
	printk("[ROG7_INBOX]%s--\n", __func__);
	return 0;
}

void fan7_cooler_wq_func(struct work_struct *work){

    printk("%s cooler stage %d\n",__func__,g_cooling_stage);

    // only use VBAT when cooler enable without bypass charging
	if(g_cooling_stage > 0 && !g_bypass_charging){
		printk("[ROG7_INBOX] power source switch to 8V due to cooler enable",__func__);
		inbox7_power_source_swap(POWER_VBAT);
	}else{
		printk("[ROG7_INBOX] power source switch to 5V due to cooler disable",__func__);
		inbox7_power_source_swap(POWER_OTG);
	}

	switch (g_cooling_stage){
		case 0:	//turn off cooler
			printk("[ROG7_INBOX] %s off\n",__func__);
			pd_set_switch(2,0);
			pd_set_switch(6,0);
			pd_cooler_enable(0);
			pd_set_OV(0);
			break;
		case 1:
			printk("[ROG7_INBOX] %s stage 1\n",__func__);
			//turn off cooler
			pd_set_switch(2,0);
			pd_set_switch(6,0);
			pd_cooler_enable(0);
			pd_set_OV(1);
			//set cooler stage 1
			if(g_Power_source == POWER_VBAT){
				pd_set_switch(6,1);
			}else{
				pd_set_switch(2,1);
			}
			pd_cooler_enable(1);
			break;
		case 2:
			printk("[ROG7_INBOX] %s stage 2\n",__func__);
			//turn off cooler
			pd_set_switch(2,0);
			pd_set_switch(6,0);
			pd_cooler_enable(0);
			pd_set_OV(0);
			//set cooler stage 2
			pd_set_OV(2);
			pd_set_switch(6,0);
			pd_set_switch(2,1);
			pd_cooler_enable(1);
			break;
		case 3:
			printk("[ROG7_INBOX] %s stage 3\n",__func__);
			//turn off cooler
			pd_set_switch(2,0);
			pd_set_switch(6,0);
			pd_cooler_enable(0);
			pd_set_OV(0);
			//set cooler stage 3
			pd_set_OV(3);
			pd_set_switch(6,0);
			pd_set_switch(2,1);
			pd_cooler_enable(1);
			break;
		case 4:
			printk("[ROG7_INBOX] %s stage 4\n",__func__);
			//turn off cooler
			pd_set_switch(2,0);
			pd_set_switch(6,0);
			pd_cooler_enable(0);
			pd_set_OV(0);
			//set cooler stage 4
			pd_set_OV(4);
			pd_set_switch(6,0);
			pd_set_switch(2,1);
			pd_cooler_enable(1);
			break;
		default:
			printk("[ROG7_INBOX] unknown cooler stage.\n");
			break;
	}
}

static void reset_status(void)
{
	g_Power_source = -1;
	g_PD_updating = false;

	g_2led_red_max = 255;
	g_2led_green_max = 255;
	g_2led_blue_max = 255;
	g_3led_red_max = 255;
	g_3led_green_max = 255;
	g_3led_blue_max = 255;

	g_2led_red=-1;
	g_2led_green=-1;
	g_2led_blue=-1;
	g_2led_mode=-1;
	g_2led_speed=-1;
	g_3led_red=-1;
	g_3led_green=-1;
	g_3led_blue=-1;
	g_3led_mode=-1;
	g_3led_speed=-1;

	g_led_on=-1;
	g_door_on=-1;
	g_logo_on=-1;
	g_aura_lpm = -1;
	g_mps_lpm = -1;
	g_Aura_ic_updating = 0;
	g_latch_status = -1;
	g_cooling_stage = -1;
	printk("[INBOX] %s\n",__func__);
	return;
}

static int rog7_inbox_usb_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret = 0;
	struct inbox_drvdata *drvdata;

	if(g_Charger_mode) {
		printk("[ROG7_INBOX] In charger mode, stop rog7_inbox_usb_probe\n");
		return 0;
	}

	wakeup_panel_by_pwrkey();
	// default control 0x16
	IC_switch = addr_0x16;

	printk("[ROG7_INBOX] hid->name : %s\n", hdev->name);
	printk("[ROG7_INBOX] hid->vendor  : 0x%04x\n", hdev->vendor);
	printk("[ROG7_INBOX] hid->product : 0x%02x\n", hdev->product);
	//ASUSEvtlog("[ROG7_INBOX] Inbox connect\n");

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		hid_err(hdev, "[ROG7_INBOX] Can't alloc drvdata\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, drvdata);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "[ROG7_INBOX] parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "[ROG7_INBOX] hw start failed\n");
		goto err_free;
	}

	rog6_inbox_hidraw = hdev->hidraw;

	mutex_init(&ms51_mutex);
	mutex_init(&hid_command_lock);
	mutex_init(&pd_lock);
	mutex_init(&power_lock);
		
	// Register sys class
	ret = aura_sync_register(&hdev->dev, drvdata);
	if (ret) {
		hid_err(hdev, "[ROG7_INBOX] aura_sync_register failed\n");
		goto err_free;
	}
	ret = sysfs_create_group(&drvdata->led.dev->kobj, &pwm_attr_group);
	if (ret)
		goto unregister;

	mutex_init(&update_lock);

	g_FAN_ID = pd_get_PCBID();
	g_Power_source = -1;
	g_PD_updating = false;

// Set global variable
	reset_status();

	latch_det_thread = kthread_create(latch_det_handler, NULL, "LatchMonitor");
	wake_up_process(latch_det_thread);
	INIT_DELAYED_WORK(&disable_autosuspend_work, disable_autosuspend_worker);
	schedule_delayed_work(&disable_autosuspend_work, msecs_to_jiffies(1000));

	device_init_wakeup(&interface_to_usbdev(to_usb_interface(hdev->dev.parent))->dev, true);

	fan7_wq = create_singlethread_workqueue("fan7_wq");
	INIT_WORK(&fan7_cooler_work,fan7_cooler_wq_func);	//for cooler

	//inbox7_power_source_swap(POWER_VBAT);
	set_HDC2010_INT_level(0);
	usb_enable_autosuspend(interface_to_usbdev(to_usb_interface(rog6_inbox_hidraw->hid->dev.parent)));

	hid_err(hdev, "%s: [ROG7_INBOX] rog_inbox_register_for_panel_events +++\n", __func__);
	rog_inbox_register_for_panel_events(drvdata);
	hid_err(hdev, "%s: [ROG7_INBOX] rog_inbox_register_for_panel_events ---\n", __func__);
#if defined ASUS_AI2205_PROJECT
	//ASUSEvtlog("[ROG7_INBOX] Inbox 7 connect\n");
	//FANDG_USBID_detect = true;
	//FANDG_connect(2);
#endif

	return 0;

unregister:
	aura_sync_unregister(drvdata);
err_free:
	printk("[ROG7_INBOX] %s fail.\n",__func__);
	hid_hw_stop(hdev);
	return ret;
}

static void rog7_inbox_usb_remove(struct hid_device *hdev)
{
	struct inbox_drvdata *drvdata = dev_get_drvdata(&hdev->dev);;
	
	if(g_Charger_mode) {
		printk("[ROG7_INBOX] In charger mode, stop %s\n",__func__);
		return;
	}

	printk("[ROG7_INBOX] %s\n",__func__);
	kthread_stop(latch_det_thread);

	//reissue undock event
	FANDG_USBID_detect = false;
	if(g_latch_status != 0){
		printk("[ROG7_INBOX] %s reissue undock event, if latch handler miss it\n",__func__);
		FANDG_connect(0);
	}
	//else{printk("[ROG7_INBOX] %s g_latch_status=%d\n",__func__,g_latch_status);}

    if (drvdata->notifier_cookie){
		printk("[ROG7_INBOX] %s panel_event_notifier_unregister\n",__func__);
        panel_event_notifier_unregister(drvdata->notifier_cookie);
	}
#if defined ASUS_AI2205_PROJECT
	//ASUSEvtlog("[ROG7_INBOX] Inbox 7 disconnect\n");
	//FANDG_USBID_detect = false;
	//FANDG_connect(0);
#endif
	vph_output_side_port(0);
	sysfs_remove_group(&drvdata->led.dev->kobj, &pwm_attr_group);
	aura_sync_unregister(drvdata);
	rog6_inbox_hidraw = NULL;
	hid_hw_stop(hdev);
}

static struct hid_device_id rog7_inbox_idtable[] = {
	{ HID_USB_DEVICE(0x0BDA, 0x5685),
		.driver_data = 0 },
	{ HID_USB_DEVICE(0x0B05, 0x7907),
		.driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(hid, rog7_inbox_idtable);

static struct hid_driver rog6_inbox_hid_driver = {
	.name		= "rog6_inbox",
	.id_table		= rog7_inbox_idtable,
	.probe			= rog7_inbox_usb_probe,
	.remove			= rog7_inbox_usb_remove,
	.raw_event		= rog7_inbox_usb_raw_event,
#ifdef CONFIG_PM
	.suspend        = rog7_inbox_usb_suspend,
	.resume			= rog7_inbox_usb_resume,
#endif
};

static int __init rog7_inbox_usb_init(void)
{
	printk("[ROG7_INBOX] %s\n",__func__);
	return hid_register_driver(&rog6_inbox_hid_driver);
}

static void __exit rog7_inbox_usb_exit(void)
{
	printk("[ROG7_INBOX] %s\n",__func__);
	hid_unregister_driver(&rog6_inbox_hid_driver);
}

module_init(rog7_inbox_usb_init);
module_exit(rog7_inbox_usb_exit);

MODULE_AUTHOR("ASUS Deeo");
MODULE_DESCRIPTION("ROG6 INBOX HID Interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("ASUS:ROG6 IBOX HID driver");
