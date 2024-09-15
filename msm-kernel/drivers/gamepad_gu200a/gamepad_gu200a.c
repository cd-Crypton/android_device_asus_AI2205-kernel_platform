#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/hidraw.h>
#include <linux/usb.h>
#include <linux/time.h>
#include <linux/hid.h>
#include <linux/mutex.h>
#include "../hid/usbhid/usbhid.h"

#include "gamepad_gu200a.h"

static int asus_usb_hid_long_write(char* cmd, int cmd_len)
{
	struct hid_device *hdev;
	int err = 0;
	char *buffer;
	int index = 0;

	if (gamepad_hidraw == NULL) {
		printk("[GAMEPAD_GU200A][%s] gamepad_hidraw is NULL !\n", __func__);
		return -1;
	}

	buffer = kzalloc(cmd_len, GFP_KERNEL);
	memset(buffer, 0, cmd_len);

	hdev = gamepad_hidraw->hid;
	hid_hw_power(hdev, PM_HINT_FULLON);

	memcpy(buffer,cmd,cmd_len);
	if(DebugFlag){
		for(index=0;index<cmd_len;index++)
			printk("[GAMEPAD_GU200A][%s][send] buffer[%d] = 0x%2x\n",__func__, index, buffer[index]);
	}

	err = hid_hw_output_report(hdev, buffer, cmd_len);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] CMD GROUP: 0x%02x, CMD ID: 0x%02x, err: %d\n",__func__,cmd[1], cmd[2], err);
	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	return err;
}

static int asus_usb_hid_long_read(char* cmd, int cmd_len, char* re_data, int re_len)
{
	int err = 0;
	struct hid_device       *hdev = gamepad_hidraw->hid;
	struct usbhid_device	*usbhid = hdev->driver_data;
	char *buffer;
	int index = 0;

	if (gamepad_hidraw == NULL) {
		printk("[GAMEPAD_GU200A][%s] gamepad_hidraw is NULL !\n", __func__);
		return -ENODEV;
	}
	
	buffer = kzalloc(cmd_len, GFP_KERNEL); 
	memset(buffer, 0, cmd_len);

	memcpy(buffer,cmd,cmd_len);

	if(DebugFlag){
		for(index=0;index<cmd_len;index++)
			printk("[GAMEPAD_GU200A][%s][send] buffer[%d] = 0x%2x\n",__func__, index, buffer[index]);		
	}

	hid_hw_power(hdev, PM_HINT_FULLON);

	err = hid_hw_output_report(hdev, buffer, cmd_len);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] CMD GROUP: 0x%02x, CMD ID: 0x%02x, err: %d\n",__func__, cmd[1], cmd[2], err);

	msleep(DELAY_MS);

	memcpy(re_data,usbhid->inbuf,re_len);
	if(DebugFlag){
		for(index=0;index<re_len;index++)
			printk("[GAMEPAD_GU200A][%s][return] re_data[%d]: 0x%02x\n",__func__, index, re_data[index]);	
	}

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);

	return err;
}

static int asus_usb_hid_write(u32 values, char asus_gp_cmd_group, char asus_gp_cmd_id)
{
	struct hid_device *hdev;
	int err = 0;
	int buf_size = 0;
	char *buffer;

	if(DebugFlag)
		printk("[GAMEPAD_GU200A][%s] CMD GROUP: 0x%2x, CMD ID: 0x%2x, value: 0x%x\n",__func__, asus_gp_cmd_group, asus_gp_cmd_id, values);

	if (gamepad_hidraw == NULL) {
		printk("[GAMEPAD_GU200A][%s] gamepad_hidraw is NULL !\n", __func__);
		return -1;
	}

	if(asus_gp_cmd_group < 0x80)
	{
		printk("[GAMEPAD_GU200A][%s] Set error command : 0x%x\n", __func__, asus_gp_cmd_id);
		return -1;
	}
	if(asus_gp_cmd_group != ASUS_GAMEPAD_GU200A_COLOR_ASSIGN){
		buf_size = ASUS_GAMEPAD_GU200A_CMD_LEN+1;
	}else{
		buf_size = 14;
	}
	buffer = kzalloc(buf_size, GFP_KERNEL); //Include report ID
	memset(buffer, 0, buf_size);


	hdev = gamepad_hidraw->hid;
	hid_hw_power(hdev, PM_HINT_FULLON);

	buffer[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	buffer[1] = asus_gp_cmd_group;
	
	if(asus_gp_cmd_group == ASUS_GAMEPAD_GU200A_COLOR_ASSIGN){
		memcpy(buffer+2,g_color_array,12);
		if(DebugFlag)
			printk("[GAMEPAD_GU200A][%s] color:[%6x] [%6x] [%6x] [%6x]\n", __func__
					, (g_color_array[0]<<16)|(g_color_array[1]<<8)|g_color_array[2]
					, (g_color_array[3]<<16)|(g_color_array[4]<<8)|g_color_array[5]
					, (g_color_array[6]<<16)|(g_color_array[7]<<8)|g_color_array[8]
					, (g_color_array[9]<<16)|(g_color_array[10]<<8)|g_color_array[11]);	
	}else{	
		buffer[2] = asus_gp_cmd_id;
		buffer[3] = values; 
	}
	err = hid_hw_output_report(hdev, buffer, buf_size);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] hid_hw_output_report fail, CMD GROUP: 0x%02x, CMD ID: 0x%02x, err: %d\n",__func__, asus_gp_cmd_group, asus_gp_cmd_id, err);
	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);
	return err;
}

static int asus_usb_hid_read(u8 *data, char asus_gp_cmd_group, char asus_gp_cmd_id)
{
	int err = 0;
	struct hid_device       *hdev = gamepad_hidraw->hid;
	struct usbhid_device	*usbhid = hdev->driver_data;
	char *buffer;
	int index = 0;
	int buf_size = ASUS_GAMEPAD_GU200A_CMD_LEN+1;

	if(DebugFlag)
		printk("[GAMEPAD_GU200A][%s] hid_hw_output_report fail, CMD GROUP: 0x%02x, CMD ID: 0x%02x, err: %d\n",__func__, asus_gp_cmd_group, asus_gp_cmd_id, err);

	if (gamepad_hidraw == NULL) {
		printk("[GAMEPAD_GU200A][%s] gamepad_hidraw is NULL !\n", __func__);
		return -ENODEV;
	}
	
	buffer = kzalloc(buf_size, GFP_KERNEL); //Include report ID
	memset(buffer, 0, buf_size);

	hid_hw_power(hdev, PM_HINT_FULLON);

	buffer[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	buffer[1] = asus_gp_cmd_group;
	buffer[2] = asus_gp_cmd_id;

	err = hid_hw_output_report(hdev, buffer, buf_size);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] hid_hw_output_report fail, CMD GROUP: 0x%02x, CMD ID: 0x%02x, err: %d\n",__func__, asus_gp_cmd_group, asus_gp_cmd_id, err);

	msleep(DELAY_MS);
	(*data) = usbhid->inbuf[3];	

	if(DebugFlag)
		for(index = 0 ; index < usbhid->bufsize ; index++)
			printk("[GAMEPAD_GU200A][%s] usbhid->inbuf[%d]: 0x%02x\n", __func__, index, usbhid->inbuf[index]);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	kfree(buffer);

	return err;
}

static u32 rainbow_mode_convert(char mode, size_t color){
	unsigned char rainbow_mode=0;
	switch(color){
		case 1:
			rainbow_mode=18;
			break;
		case 2:
			rainbow_mode=10;
			break;
		case 3:
			rainbow_mode=14;
			break;
		case 4:
			rainbow_mode=8;
			break;
		case 5:
			rainbow_mode=12;
			break;
		case 6:
			rainbow_mode=16;
			break;
		default:
			return 0;
			break;
	}
	if(mode==31)
		return rainbow_mode;
	else if(mode==32)
		return rainbow_mode+1;
	else
		return 0;
}

void set_mode_and_random(int mode, int value){
	int err = 0;

	err = asus_usb_hid_write(mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] ASUS_GAMEPAD_GU200A_AURA_MODE err: %d\n", __func__, err);
		return;
	}
	err = asus_usb_hid_write(value, ASUS_GAMEPAD_GU200A_AURA_RANDOM, ASUS_GAMEPAD_SET_RANDOM);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] ASUS_GAMEPAD_SET_RANDOM err: %d\n", __func__, err);
		return;
	}
}

static ssize_t red_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	g_color_array[RED_UPPER_LEFT] = reg_val;
	g_color_array[RED_LOWER_LEFT] = reg_val;
	g_color_array[RED_UPPER_RIGHT] = reg_val;
	g_color_array[RED_LOWER_RIGHT] = reg_val;

	cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_COLOR_ASSIGN;
	memcpy(cmd+2,g_color_array,12);
	err = asus_usb_hid_long_write(cmd,14);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_long_write : err %d\n", __func__, err);
  
	err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_write : err %d\n", __func__, err);

	return count;
}

static ssize_t green_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	g_color_array[GREEN_UPPER_LEFT] = reg_val;
	g_color_array[GREEN_LOWER_LEFT] = reg_val;
	g_color_array[GREEN_UPPER_RIGHT] = reg_val;
	g_color_array[GREEN_LOWER_RIGHT] = reg_val;

	cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_COLOR_ASSIGN;
	memcpy(cmd+2,g_color_array,12);
	err = asus_usb_hid_long_write(cmd,14);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_long_write : err %d\n", __func__, err);
  
	err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_write : err %d\n", __func__, err);

	return count;
}

static ssize_t blue_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	g_color_array[BLUE_UPPER_LEFT] = reg_val;
	g_color_array[BLUE_LOWER_LEFT] = reg_val;
	g_color_array[BLUE_UPPER_RIGHT] = reg_val;
	g_color_array[BLUE_LOWER_RIGHT] = reg_val;

	cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_COLOR_ASSIGN;
	memcpy(cmd+2,g_color_array,12);
	err = asus_usb_hid_long_write(cmd,14);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_long_write : err %d\n", __func__, err);
  
	err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_write : err %d\n", __func__, err);

	return count;
}

static ssize_t rgb_pwm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long int RGB;
	unsigned int red, green, blue;
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};

    RGB = strtoul(buf,NULL,16);
    red = (RGB >> 16) & 0xFF;
    green = (RGB >> 8) & 0xFF;
    blue = RGB & 0xFF;	

	printk("[GAMEPAD_GU200A] [%s] reg_val : %x, red : %x, green : %x, blue : %x\n", __func__, RGB, red, green, blue);

	g_color_array[RED_UPPER_LEFT] = red;
	g_color_array[RED_LOWER_LEFT] = red;
	g_color_array[RED_UPPER_RIGHT] = red;
	g_color_array[RED_LOWER_RIGHT] = red;	
	g_color_array[GREEN_UPPER_LEFT] = green;
	g_color_array[GREEN_LOWER_LEFT] = green;
	g_color_array[GREEN_UPPER_RIGHT] = green;
	g_color_array[GREEN_LOWER_RIGHT] = green;
	g_color_array[BLUE_UPPER_LEFT] = blue;
	g_color_array[BLUE_LOWER_LEFT] = blue;
	g_color_array[BLUE_UPPER_RIGHT] = blue;
	g_color_array[BLUE_LOWER_RIGHT] = blue;

	cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_COLOR_ASSIGN;
	memcpy(cmd+2,g_color_array,12);
	err = asus_usb_hid_long_write(cmd,14);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_long_write : err %d\n", __func__, err);
    
	err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
	if (err < 0)
		printk("[GAMEPAD_GU200A] [%s] asus_usb_hid_write : err %d\n", __func__, err);
	
	return count;
}

static ssize_t rgb_pwm_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	int err = 0;
	unsigned char cmd1[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	unsigned char re_cmd1[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	unsigned char cmd2[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	unsigned char re_cmd2[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};	

	cmd1[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd1[1] = ASUS_GAMEPAD_GU200A_GET_COLOR;
	err = asus_usb_hid_long_read(cmd1,2,re_cmd1,14);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] asus_usb_hid_long_read : err %d\n", __func__, err);	


	cmd2[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd2[1] = ASUS_GAMEPAD_GU200A_AURA_MODE;
	cmd2[2] = ASUS_GAMEPAD_GET_MODE;
	err = asus_usb_hid_long_read(cmd2,3,re_cmd2,4);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] asus_usb_hid_long_read : err %d\n", __func__, err);		

	return snprintf(buf, PAGE_SIZE,"mode = %d\n    左上 | 右上\nR : 0x%2x | 0x%2x\nG : 0x%2x | 0x%2x\nB : 0x%2x | 0x%2x\n---------------\n    左下 | 右下\nR : 0x%2x | 0x%2x\nG : 0x%2x | 0x%2x\nB : 0x%2x | 0x%2x\n"
					,re_cmd2[3],re_cmd1[2],re_cmd1[8],re_cmd1[3],re_cmd1[9],re_cmd1[4],re_cmd1[10],re_cmd1[5],re_cmd1[11],re_cmd1[6],re_cmd1[12],re_cmd1[7],re_cmd1[13]);
}

static ssize_t apply_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	printk("[GAMEPAD_GU200A][%s] mode:%2d, speed:%3d, color:[%6x] [%6x] [%6x] [%6x]\n", __func__, g_mode, g_speed
			, (g_color_array[0]<<16)|(g_color_array[1]<<8)|g_color_array[2]
			, (g_color_array[3]<<16)|(g_color_array[4]<<8)|g_color_array[5]
			, (g_color_array[6]<<16)|(g_color_array[7]<<8)|g_color_array[8]
			, (g_color_array[9]<<16)|(g_color_array[10]<<8)|g_color_array[11]);

	err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
	if (err < 0){
		apply_state=-1;
		printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);
	}

	return count;
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 mode;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &mode);
	if (ret)
		return count;

	switch(mode){
		case 0: //close
			if(DebugFlag)
				printk("[GAMEPAD_GU200A][%s] close mode.\n", __func__);
			g_mode = 0;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			break;
		case 1: //static
			if(DebugFlag)
				printk("[GAMEPAD_GU200A][%s] static.\n", __func__);
			g_mode = 1;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			break;
		case 2: //breath
			if(DebugFlag)
				printk("[GAMEPAD_GU200A][%s] breath.\n", __func__);
			g_mode = 2;
			set_mode_and_random(g_mode,0);
			break;
		case 3: //storbing
			if(DebugFlag)
				printk("[GAMEPAD_GU200A][%s] storbing.\n", __func__);
			g_mode = 4;
			set_mode_and_random(g_mode,0);
			break;
		case 4: //color cycle
			if(DebugFlag)
				printk("[GAMEPAD_GU200A][%s] color sycle.\n", __func__);
			g_mode = 5;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			break;
		default:
			printk("[GAMEPAD_GU200A][%s] unknowed mode.\n", __func__);
			return count;
			break;
	}
	
	if (err < 0)
		printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

	return count;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char data = 0;
	int err = 0;

	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_GET_MODE);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err %d\n", __func__, err);

	printk("[GAMEPAD_GU200A] %s, %d\n", __func__, data);
	return snprintf(buf, PAGE_SIZE,"%d\n",data);
}

static ssize_t fw_ver_right_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	unsigned char re_cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	int err = 0;

	cmd[0] = ASUS_GAMEPAD_GU200A_FLASH_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_GET_RIGHT_FW_VER;
	err = asus_usb_hid_long_read(cmd,2,re_cmd,11);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] err %d\n", __func__, err);	
		re_cmd[10] = 0xff;
		re_cmd[9] = 0xff;
		re_cmd[8] = 0xff;
		re_cmd[7] = 0xff;
	}
	if(g_fw_mode == 2){
		re_cmd[10] = 0xff;
		re_cmd[9] = 0xff;
		re_cmd[8] = 0xff;
		re_cmd[7] = 0xfe;
	}
	
	printk("[GAMEPAD_GU200A] [%s] FW_mode = %d, right FW version : %02x.%02x.%02x.%02x\n", __func__, g_fw_mode, re_cmd[10],re_cmd[9],re_cmd[8],re_cmd[7]);
	return snprintf(buf, PAGE_SIZE,"%02x.%02x.%02x.%02x\n", re_cmd[10],re_cmd[9],re_cmd[8],re_cmd[7]);
}

static ssize_t fw_ver_left_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	unsigned char re_cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	int err = 0;

	cmd[0] = ASUS_GAMEPAD_GU200A_FLASH_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_GET_LEFT_FW_VER;
	err = asus_usb_hid_long_read(cmd,2,re_cmd,11);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] err %d\n", __func__, err);	
		re_cmd[10] = 0xff;
		re_cmd[9] = 0xff;
		re_cmd[8] = 0xff;
		re_cmd[7] = 0xff;
	}
	if(g_fw_mode == 2){
		re_cmd[10] = 0xff;
		re_cmd[9] = 0xff;
		re_cmd[8] = 0xff;
		re_cmd[7] = 0xfe;
	}
	
	printk("[GAMEPAD_GU200A] [%s] FW_mode = %d, left FW version : %02x.%02x.%02x.%02x\n", __func__, g_fw_mode, re_cmd[10],re_cmd[9],re_cmd[8],re_cmd[7]);
	return snprintf(buf, PAGE_SIZE,"%02x.%02x.%02x.%02x\n", re_cmd[10],re_cmd[9],re_cmd[8],re_cmd[7]);
}

static ssize_t fw_mode_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	printk("[GAMEPAD_GU200A] FW mode : %d\n", g_fw_mode);
	return snprintf(buf, PAGE_SIZE,"%d\n", g_fw_mode);
}

static ssize_t frame_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	printk("[GAMEPAD_GU200A][%s] 0x%x\n", __func__, reg_val);
	err = asus_usb_hid_write(reg_val, ASUS_GAMEPAD_GU200A_AURA_FRAME, ASUS_GAMEPAD_SET_FRAME);
	if (err < 0)
		printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

	return count;
}

static ssize_t frame_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char data= 0;
	int err = 0;

	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_FRAME, ASUS_GAMEPAD_GET_FRAME);
	if (err < 0)
		printk("[GAMEPAD_GU200A] frame_show:err %d\n", err);

	printk("[GAMEPAD_GU200A][%s] 0x%x", __func__, data);
	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t mode2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned char rainbow_mode = 0;
	unsigned char mode2 = 0;
	int err = 0;
	int rgb_num=0;//rgb_num_t=0;
	long rgb_tmp = 0;
	int ntokens = 0;
	const char *cp = buf;
	const char *buf_tmp;
	//int i = 0;
	mode2_state=0;
	memset(g_color_array,0,COLOR_MAX);

	sscanf(buf, "%d", &mode2);
	//printk("[GAMEPAD_GU200A][%s] mode2 = 0x%x.\n", __func__, mode2);

	while ((cp = strpbrk(cp + 1, ",")))
	{
		ntokens++;  //the number of ","
	}
	if(DebugFlag)
		printk("[GAMEPAD_GU200A][%s] mode2 = 0x%x ,buf=%s ,ntokens=%d .\n", __func__, mode2, buf, ntokens);
	if(ntokens > 2)
	{
		printk("[GAMEPAD_GU200A][%s] mode2_store,wrong input,too many ntokens\n", __func__);
		mode2_state=-1;
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
				g_color_array[rgb_num+6] = (rgb_tmp >> 16)&0xFF;
				g_color_array[rgb_num++] = (rgb_tmp >> 16)&0xFF;
				g_color_array[rgb_num+6] = (rgb_tmp >> 8)&0xFF;
				g_color_array[rgb_num++] = (rgb_tmp >> 8)&0xFF;
				g_color_array[rgb_num+6] = rgb_tmp & 0xFF;
				g_color_array[rgb_num++] = rgb_tmp & 0xFF;
				break;
			}
		}
	}

	if(rgb_num != ntokens*3){
		printk("[GAMEPAD_GU200A] mode2_store,wrong input,rgb_num != ntokens*3\n");
		mode2_state=-1;
		return count;
	}
/*
	for(i=0;i<COLOR_MAX;i++)
	{
		printk("[GAMEPAD_GU200A][mode2_store] g_color_array[%d]=0x%x \n",i,g_color_array[i]);
	}
*/
	switch(mode2){
		case 0: //closed
			if(ntokens == 0)
				printk("[GAMEPAD_GU200A][%s] closed.\n", __func__);			
			else{
				printk("[GAMEPAD_GU200A][%s] closed, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 0;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

			break;		
		case 2: //static
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] static, single color.\n", __func__);
				memcpy(g_color_array+3,g_color_array,3);
				memcpy(g_color_array+6,g_color_array,3);
				memcpy(g_color_array+9,g_color_array,3);
				/*for(i=0;i<3;i++){					
					g_color_array[i+3]=g_color_array[i];
					g_color_array[i+6]=g_color_array[i];
					g_color_array[i+9]=g_color_array[i];
				}		*/		
			}else if(ntokens == 2){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] static, mix color.\n", __func__);
			}else{
				printk("[GAMEPAD_GU200A][%s] static, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 1;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write:err %d\n", __func__, err);

			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_ASSIGN, ASUS_GAMEPAD_CMDID_NONE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write : ASUS_GAMEPAD_GU200A_COLOR_ASSIGN err %d\n", __func__, err);
    
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

			break;
		case 3: //breath
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] breath, single color.\n", __func__);
				memcpy(g_color_array+3,g_color_array,3);
				memcpy(g_color_array+6,g_color_array,3);
				memcpy(g_color_array+9,g_color_array,3);
				/*for(i=0;i<3;i++){
					g_color_array[i+3]=g_color_array[i];
					g_color_array[i+6]=g_color_array[i];
					g_color_array[i+9]=g_color_array[i];
				}	*/			
			}else if(ntokens == 2){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] breath, mix color.\n", __func__);
			}else{
				printk("[GAMEPAD_GU200A][%s] breath, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 2;
			set_mode_and_random(g_mode,0);
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_ASSIGN, ASUS_GAMEPAD_CMDID_NONE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write : ASUS_GAMEPAD_GU200A_COLOR_ASSIGN err %d\n", __func__, err);
    
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);	

			break;		
		case 6: //commet from upper to lower
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] commet from upper to lower, single color.\n", __func__);
				memcpy(g_color_array+3,g_color_array,3);
				/*for(i=0;i<3;i++){
					g_color_array[i+3]=g_color_array[i];
				}	*/			
			}else{
				printk("[GAMEPAD_GU200A][%s] commet - from upper to lower, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 20;
			set_mode_and_random(g_mode,0);
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_ASSIGN, ASUS_GAMEPAD_CMDID_NONE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write : ASUS_GAMEPAD_GU200A_COLOR_ASSIGN err %d\n", __func__, err);
    
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);
			
			break;
		case 7: //flash and dash - from upper to lower
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] flash and dash - from upper to lower, single color.\n", __func__);
				memcpy(g_color_array+3,g_color_array,3);
				/*for(i=0;i<3;i++){
					g_color_array[i+3]=g_color_array[i];
				}	*/			
			}else{
				printk("[GAMEPAD_GU200A][%s] flash and dash - from upper to lower, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 22;
			set_mode_and_random(g_mode,0);
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_ASSIGN, ASUS_GAMEPAD_CMDID_NONE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write : ASUS_GAMEPAD_GU200A_COLOR_ASSIGN err %d\n", __func__, err);
    
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

			break;
		case 8: //commet - from lower to upper
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] commet - frome lower to upper, single color.\n", __func__);
				memcpy(g_color_array+3,g_color_array,3);
				/*for(i=0;i<3;i++){
					g_color_array[i+3]=g_color_array[i];
				}	*/				
			}else{
				printk("[GAMEPAD_GU200A][%s] commet frome lower to upper, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 21;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write:err %d\n", __func__, err);

			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_ASSIGN, ASUS_GAMEPAD_CMDID_NONE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write : ASUS_GAMEPAD_GU200A_COLOR_ASSIGN err %d\n", __func__, err);
    
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

			break;
		case 9: //flash and dash - from lower to upper
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] flash and dash - frome lower to upper, single color.\n", __func__);
				memcpy(g_color_array+3,g_color_array,3);
				/*for(i=0;i<3;i++){
					g_color_array[i+3]=g_color_array[i];
				}	*/				
			}else{
				printk("[GAMEPAD_GU200A][%s] flash and dash - frome lower to upper, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			g_mode = 23;
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write:err %d\n", __func__, err);
			
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_ASSIGN, ASUS_GAMEPAD_CMDID_NONE);
			if (err < 0)
				printk("[GAMEPAD_GU200A][%s] asus_usb_hid_write : ASUS_GAMEPAD_GU200A_COLOR_ASSIGN err %d\n", __func__, err);
    
			err = asus_usb_hid_write(1, ASUS_GAMEPAD_GU200A_COLOR_APPLY, ASUS_GAMEPAD_SET_APPLY);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

			break;
		case 31: //rainbow - from upper to lower
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] %d th rainbow - from upper to lower.\n", __func__, g_color_array[2]);
			}else{
				printk("[GAMEPAD_GU200A][%s] rainbow - from upper to lower, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			rainbow_mode = rainbow_mode_convert(mode2, g_color_array[2]);
			if(rainbow_mode)
				g_mode = rainbow_mode;
			else{
				printk("[GAMEPAD_GU200A][%s] unknowed rainbow.\n", __func__);
				return count;
			}
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);
			break;		
		case 32: //rainbow - frome lower to upper
			if(ntokens == 1){
				if(DebugFlag)
					printk("[GAMEPAD_GU200A][%s] %d th rainbow - frome lower to upper .\n", __func__, g_color_array[2]);
			}else{
				printk("[GAMEPAD_GU200A][%s] rainbow - frome lower to upper, wrong input.\n", __func__);
				mode2_state=-1;
				return count;
			}
			rainbow_mode = rainbow_mode_convert(mode2, g_color_array[2]);
			if(rainbow_mode)
				g_mode = rainbow_mode;
			else{
				printk("[GAMEPAD_GU200A][%s] unknowed rainbow.\n", __func__);
				return count;
			}
			err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
			if (err < 0)
				printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);
			break;			
		default:
			printk("[GAMEPAD_GU200A][%s] unknowed mode2\n", __func__);
			break;
	}
	return count;
}

static ssize_t mode2_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, PAGE_SIZE,"%d\n", mode2_state);
}

static ssize_t led_on_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[GAMEPAD_GU200A] %s, reg_val %d\n", __func__, reg_val);
	g_led_on=reg_val;
	//err = asus_usb_hid_write(reg_val,ASUS_GAMEPAD_SET_LED_ON);
	if (err < 0)
		printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

	return count;
}

static ssize_t led_on_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char data= 0;
	int err = 0;

	//err = asus_usb_hid_read(&data,ASUS_GAMEPAD_GET_LED_ON);
	if (err < 0)
		printk("[GAMEPAD_GU200A] led_on_show:err %d\n", err);

	printk("[GAMEPAD_GU200A][%s] data: 0x%x", __func__, data);
	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t speed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[GAMEPAD_GU200A] %s, reg_val %d\n", __func__, reg_val);
	g_speed=reg_val;
	err = asus_usb_hid_write(reg_val, ASUS_GAMEPAD_GU200A_AURA_SPEED, ASUS_GAMEPAD_SET_SPEED);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);

	return count;
}

static ssize_t speed_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char data= 0;
	int err = 0;

	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_SPEED, ASUS_GAMEPAD_GET_SPEED);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);

	printk("[GAMEPAD_GU200A][%s] data: 0x%x", __func__, data);
	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t brightness_aura_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	printk("[GAMEPAD_GU200A] %s, reg_val %d\n", __func__, reg_val);
	g_brightness=reg_val;
	err = asus_usb_hid_write(reg_val, ASUS_GAMEPAD_GU200A_AURA_BRIGHTNESS, ASUS_GAMEPAD_SET_BRIGHTNESS);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);

	return count;
}

static ssize_t brightness_aura_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char data= 0;
	int err = 0;

	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_BRIGHTNESS, ASUS_GAMEPAD_GET_BRIGHTNESS);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);
	printk("[GAMEPAD_GU200A][%s] data: 0x%x", __func__, data);
	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t random_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	//printk("[GAMEPAD_GU200A] %s, reg_val %d\n", __func__, reg_val);
	err = asus_usb_hid_write(reg_val, ASUS_GAMEPAD_GU200A_AURA_RANDOM, ASUS_GAMEPAD_SET_RANDOM);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);

	return count;
}

static ssize_t random_show(struct device *dev, struct device_attribute *attr,char *buf)
{

	unsigned char data= 0;
	int err = 0;

	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_RANDOM, ASUS_GAMEPAD_GET_RANDOM);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);

	printk("[GAMEPAD_GU200A][%s] data: 0x%x", __func__, data);
	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t rog_button_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;
	g_rog_button = reg_val;

	if(reg_val > 0 && reg_val < 5){
		cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
		cmd[1] = ASUS_GAMEPAD_GU200A_ROG_BUTTON_MODE;
		cmd[2] = ASUS_GAMEPAD_GU200A_SET_ROG_BUTTON;
		cmd[3] = reg_val;
		err = asus_usb_hid_long_write(cmd,4);
	}else if(reg_val == 5){
		cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
		cmd[1] = ASUS_GAMEPAD_GU200A_ROG_BUTTON_MODE;
		cmd[2] = ASUS_GAMEPAD_GU200A_SET_ROG_BUTTON;
		cmd[3] = 0x04;
		cmd[4] = 0x01;	
		err = asus_usb_hid_long_write(cmd,5);
	}else {
		g_rog_button = 0;
	}
	printk("[GAMEPAD_GU200A][%s] reg_val : %d , rog_button_effect : %s\n", __func__, reg_val, rog_button_effect[g_rog_button]);

	return count;
}

static ssize_t rog_button_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	unsigned char re_cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	int err = 0;

	cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_ROG_BUTTON_MODE;
	cmd[2] = ASUS_GAMEPAD_GU200A_GET_ROG_BUTTON;
	err = asus_usb_hid_long_read(cmd,3,re_cmd,4);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err %d\n", __func__, err);	

	if(re_cmd[3] > 0 && re_cmd[3] < 5){
		g_rog_button = re_cmd[3];
	}else{
		g_rog_button = 0;
	}

	printk("[GAMEPAD_GU200A][%s] rog_button_effect : 0x%2x, %s\n", __func__, re_cmd[3], rog_button_effect[g_rog_button]);
	return snprintf(buf, PAGE_SIZE,"%d\n", re_cmd[3]);
}

static ssize_t key_remapping_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	u32 remapping_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &remapping_val);
	if (ret)
		return count;
	
	if(g_key && remapping_val >= 0 && remapping_val < 21){
		cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
		cmd[1] = ASUS_GAMEPAD_GU200A_KEY_MAPPING;
		cmd[2] = g_key;
		cmd[3] = remapping_val;

		printk("[GAMEPAD_GU200A][%s] g_key=%d, remapping_val = %d\n", __func__, g_key, remapping_val);
		err = asus_usb_hid_long_write(cmd,4);
		if (err < 0)
			printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);
	}else{
		printk("[GAMEPAD_GU200A][%s] error key ! g_key = %d, remapping_val = %d\n", __func__, g_key, remapping_val);
	}

	return count;
}

static ssize_t key_remapping_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned char cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	unsigned char re_cmd[ASUS_GAMEPAD_GU200A_CMD_LONG_LEN] = {0};
	int err = 0;

	if(g_key){
		cmd[0] = ASUS_GAMEPAD_GU200A_REPORT_ID;
		cmd[1] = ASUS_GAMEPAD_GU200A_KEY_MAPPING;
		cmd[2] = g_key + 0x80;
		err = asus_usb_hid_long_read(cmd,3,re_cmd,4);
		if (err < 0)
			printk("[GAMEPAD_GU200A][%s] err %d\n", __func__, err);	
	}
	printk("[GAMEPAD_GU200A][%s] g_key : %d , 0x%2x (%s)remapping to 0x%2x (%s)", __func__, g_key, re_cmd[2], button_name[g_key], re_cmd[3], button_name[re_cmd[3]]);

	return snprintf(buf, PAGE_SIZE,"%d\n", re_cmd[3]);
}

static ssize_t key_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 key;
	int err = 0;
	int i = 0;

	err = kstrtou32(buf, 10, &key);
	if (err)
		return count;
	
	printk("[GAMEPAD_GU200A][%s] input key : %d\n", __func__, key );
	if(key>0 && key<21){
		g_key = key;
		printk("[GAMEPAD_GU200A][%s] switch key to 0x%x : %s\n", __func__, g_key, button_name[g_key]);
	}else{
		g_key = -1;
		printk("[GAMEPAD_GU200A][%s] error! unknowed key input\n", __func__);
		for(i=1;i<21;i++)
			printk("[GAMEPAD_GU200A][%s] key %2d : %5s\n", __func__, i, button_name[i]);
	}	
	return count;
}

static ssize_t key_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    printk("[GAMEPAD_GU200A][%s] key: %d\n", __func__, g_key);
	return snprintf(buf, PAGE_SIZE,"%d\n", g_key);
}

static ssize_t debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 tmp;
	int err = 0;

	err = kstrtou32(buf, 10, &tmp);
	if (err)
		return count;

	DebugFlag = ( tmp == 1 ) ? true:false;
	printk("[GAMEPAD_GU200A][%s] DebugFlag: %s\n", __func__, (DebugFlag == true)? "True":"False" );

	return count;
}

static ssize_t sleep_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val,mode;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	mode = (reg_val>0) ? 1:0;
	printk("[GAMEPAD_GU200A][%s] sleep_mode : %s\n", __func__, (mode == 1)? "enable":"disable" );
	err = asus_usb_hid_write(reg_val, ASUS_GAMEPAD_GU200A_AURA_SLEEP_MODE, ASUS_GAMEPAD_SET_SLEEP_MODE); //0x00=disable or 0x01=enable
	if (err < 0)
		printk("[GAMEPAD_GU200A] asus_usb_hid_write:err %d\n", err);

	return count;
}

static ssize_t sleep_mode_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	unsigned char data= 0;
	int err = 0;

	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_SLEEP_MODE, ASUS_GAMEPAD_GET_SLEEP_MODE);
	if (err < 0)
		printk("[GAMEPAD_GU200A] frame_show:err %d\n", err);

	printk("[GAMEPAD_GU200A][%s] sleep_mode : %s", __func__, (data == 1)? "enable":"disable");
	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t debug_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	printk("[GAMEPAD_GU200A][%s] DebugFlag: %s\n", __func__, (DebugFlag == true)? "True":"False" );
	return snprintf(buf, PAGE_SIZE,"%s\n", (DebugFlag == true)? "True":"False");
}

static ssize_t factory_reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 reg_val;
	int err = 0;
	ssize_t ret;

	ret = kstrtou32(buf, 10, &reg_val);
	if (ret)
		return count;

	switch(reg_val){
		case 0:
			printk("[GAMEPAD_GU200A][%s] factory_reset : reset all\n", __func__);
			break;
		case 1:
			printk("[GAMEPAD_GU200A][%s] factory_reset : button remapping reset\n", __func__);
			break;
		case 2:
			printk("[GAMEPAD_GU200A][%s] factory_reset : aura RGB reset\n", __func__);
			break;
		default:
			printk("[GAMEPAD_GU200A][%s] factory_reset : error unput\n", __func__);
			return count;
			break;
	}

	err = asus_usb_hid_write(0, ASUS_GAMEPAD_GU200A_RESET, reg_val);
	if (err < 0)
		printk("[GAMEPAD_GU200A][%s] err: %d\n", __func__, err);

	return count;
}

void init_brightness(int value){
	int err = 0;
	u8 data= 0;
	err = asus_usb_hid_write(value, ASUS_GAMEPAD_GU200A_AURA_BRIGHTNESS, ASUS_GAMEPAD_SET_BRIGHTNESS);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] ASUS_GAMEPAD_SET_BRIGHTNESS err: %d\n", __func__, err);
		return;
	}
	err = asus_usb_hid_read(&data, ASUS_GAMEPAD_GU200A_AURA_BRIGHTNESS, ASUS_GAMEPAD_GET_BRIGHTNESS);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] ASUS_GAMEPAD_GU200A_AURA_BRIGHTNESS err: %d\n", __func__, err);
		return;
	}	
	g_brightness = data;
	printk("[GAMEPAD_GU200A][%s] set brightness : %d\n", __func__, g_brightness);

	return;
}

void init_mode(void){
	int err = 0;
	g_mode = 0;
	err = asus_usb_hid_write(g_mode, ASUS_GAMEPAD_GU200A_AURA_MODE, ASUS_GAMEPAD_SET_MODE);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] ASUS_GAMEPAD_SET_MODE err: %d\n", __func__, err);
		return;
	}

	return;
}

void show_fw_ver_in_probe(void)
{
	unsigned char cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	unsigned char re_left_cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	unsigned char re_right_cmd[ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN] = {0};
	int err = 0;

	cmd[0] = ASUS_GAMEPAD_GU200A_FLASH_REPORT_ID;
	cmd[1] = ASUS_GAMEPAD_GU200A_GET_LEFT_FW_VER;
	err = asus_usb_hid_long_read(cmd,2,re_left_cmd,11);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] get left fw err %d\n", __func__, err);	
		re_left_cmd[10] = 0xff;
		re_left_cmd[9] = 0xff;
		re_left_cmd[8] = 0xff;
		re_left_cmd[7] = 0xff;
	}
	if (g_fw_mode == 2){
		re_left_cmd[10] = 0xff;
		re_left_cmd[9] = 0xff;
		re_left_cmd[8] = 0xff;
		re_left_cmd[7] = 0xfe;		
	}	

	cmd[1] = ASUS_GAMEPAD_GU200A_GET_RIGHT_FW_VER;
	err = asus_usb_hid_long_read(cmd,2,re_right_cmd,11);
	if (err < 0){
		printk("[GAMEPAD_GU200A][%s] get right fw err %d\n", __func__, err);	
		re_right_cmd[10] = 0xff;
		re_right_cmd[9] = 0xff;
		re_right_cmd[8] = 0xff;
		re_right_cmd[7] = 0xff;
	}
	if (g_fw_mode == 2){
		re_right_cmd[10] = 0xff;
		re_right_cmd[9] = 0xff;
		re_right_cmd[8] = 0xff;
		re_right_cmd[7] = 0xfe;		
	}	
	
	printk("[GAMEPAD_GU200A][%s] FW mode : %d, left FW version : %02x.%02x.%02x.%02x\n"
				, __func__, g_fw_mode, re_left_cmd[10],re_left_cmd[9],re_left_cmd[8],re_left_cmd[7]);
	printk("[GAMEPAD_GU200A][%s] FW mode : %d, right FW version : %02x.%02x.%02x.%02x\n"
				, __func__, g_fw_mode, re_right_cmd[10],re_right_cmd[9],re_right_cmd[8],re_right_cmd[7]);

	return ;
}

static DEVICE_ATTR(red_pwm, 0664, NULL, red_pwm_store);
static DEVICE_ATTR(green_pwm, 0664, NULL, green_pwm_store);
static DEVICE_ATTR(blue_pwm, 0664, NULL, blue_pwm_store);
static DEVICE_ATTR(rgb_pwm, 0664, rgb_pwm_show, rgb_pwm_store);
static DEVICE_ATTR(mode, 0664, mode_show, mode_store);
static DEVICE_ATTR(mode2, 0664, mode2_show, mode2_store);
static DEVICE_ATTR(apply, 0664, NULL, apply_store);
static DEVICE_ATTR(fw_ver_right, 0664, fw_ver_right_show, NULL);
static DEVICE_ATTR(fw_ver_left, 0664, fw_ver_left_show, NULL);
static DEVICE_ATTR(fw_mode, 0664, fw_mode_show, NULL);
static DEVICE_ATTR(frame, 0664, frame_show, frame_store);
static DEVICE_ATTR(led_on, 0664, led_on_show, led_on_store);
static DEVICE_ATTR(speed, 0664, speed_show, speed_store);
static DEVICE_ATTR(brightness_aura, 0664, brightness_aura_show, brightness_aura_store);
static DEVICE_ATTR(random, 0664, random_show, random_store);
static DEVICE_ATTR(debug, 0664, debug_show, debug_store);
static DEVICE_ATTR(rog_button, 0664, rog_button_show, rog_button_store);
static DEVICE_ATTR(key_remapping, 0664, key_remapping_show, key_remapping_store);
static DEVICE_ATTR(key_switch, 0664, key_switch_show, key_switch_store);
static DEVICE_ATTR(factory_reset, 0664, NULL, factory_reset_store);
static DEVICE_ATTR(sleep_mode, 0664, sleep_mode_show, sleep_mode_store);

static struct attribute *pwm_attrs[] = {
	&dev_attr_red_pwm.attr,
	&dev_attr_green_pwm.attr,
	&dev_attr_blue_pwm.attr,
	&dev_attr_rgb_pwm.attr,
	&dev_attr_mode.attr,
	&dev_attr_apply.attr,
	&dev_attr_fw_ver_right.attr,
	&dev_attr_fw_ver_left.attr,
	&dev_attr_fw_mode.attr,
	&dev_attr_frame.attr,
	&dev_attr_mode2.attr,
	&dev_attr_led_on.attr,
	&dev_attr_speed.attr,
	&dev_attr_brightness_aura.attr,
	&dev_attr_random.attr,
	&dev_attr_debug.attr,
	&dev_attr_rog_button.attr,
	&dev_attr_key_remapping.attr,
	&dev_attr_key_switch.attr,
	&dev_attr_factory_reset.attr,
	&dev_attr_sleep_mode.attr,
	NULL
};

static const struct attribute_group aprom_attr_group = {
	.attrs = pwm_attrs,
};

static struct attribute *bootloader_attrs[] = {
	&dev_attr_fw_ver_right.attr,
	&dev_attr_fw_ver_left.attr,
	&dev_attr_fw_mode.attr,
	NULL
};

static const struct attribute_group bootloader_attr_group = {
	.attrs = bootloader_attrs,
};

static void aura_sync_set(struct led_classdev *led,
			      enum led_brightness brightness)
{
	//printk("[GAMEPAD_GU200A] aura_sync_set : %d.\n", brightness);
}

static enum led_brightness aura_sync_get(struct led_classdev *led_cdev)
{
	struct gamepad_drvdata *data;

	//printk("[GAMEPAD_GU200A] aura_sync_get.\n");
	data = container_of(led_cdev, struct gamepad_drvdata, led);

	return data->led.brightness;
}

static int aura_sync_register(struct device *dev, struct gamepad_drvdata *data)
{
	data->led.name = "aura_gamepad";

	data->led.brightness = LED_OFF;
	data->led.max_brightness = LED_HALF;
	data->led.default_trigger = "none";
	data->led.brightness_set = aura_sync_set;
	data->led.brightness_get = aura_sync_get;

	return led_classdev_register(dev, &data->led);
}

static void aura_sync_unregister(struct gamepad_drvdata *data)
{
	led_classdev_unregister(&data->led);
}


#ifdef CONFIG_PM
static int gamepad_usb_resume(struct hid_device *hdev)
{
	return 0;
}

static int gamepad_usb_suspend(struct hid_device *hdev, pm_message_t message)
{
	return 0;
}
#endif /* CONFIG_PM */

static int gamepad_usb_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct gamepad_drvdata *drvdata = dev_get_drvdata(&hdev->dev);
	int index = 0;
	char raw_event_str[30];

	if(DebugFlag) {
		printk("[GAMEPAD_GU200A][%s] =====\n", __func__);
		printk("[GAMEPAD_GU200A][%s] UsageID : 0x%x\n", __func__, ((hdev->collection->usage >> 16) & 0x00FF ));
		for(index = 0 ; index < size ; index++)
			printk("[GAMEPAD_GU200A][%s] data[%d] = 0x%02x\n", __func__, index, data[index]);
	}

	if(data[0] == 0x80) {
		
		snprintf(raw_event_str, sizeof(raw_event_str), "IRQ=%02x,%02x,%02x,%02x,%02x,%02x", data[1], data[2], data[3], data[4], data[5], data[6]);
		
		if (g_store_reverse && (g_raw_event_ptr_store == g_raw_event_ptr_send)) {
            printk("[GAMEPAD_GU200A][%s] buffer is full ! give up str : %s\n", __func__, raw_event_str);
            return -1;
        }   
		
        strcpy(g_raw_event_ptr_store, raw_event_str);
        
		if (g_raw_event_ptr_store == &g_raw_event_str_array[BUFFER_LENGTH-1][0]) {
            g_raw_event_ptr_store = &g_raw_event_str_array[0][0];
            g_store_reverse = true;
        } else {
            g_raw_event_ptr_store += sizeof(g_raw_event_str_array[0]);
        }
			
		printk("[GAMEPAD_GU200A][%s] store str : %s\n", __func__, raw_event_str);

		if(!g_uevent_send_working){
			printk("[GAMEPAD_GU200A][%s] call send work\n", __func__);
			queue_work(drvdata->send_event_workqueue, &drvdata->send_event_work);
		}
		
		return -1;
	}

	return 0;
}

static void uevent_send_work(struct work_struct *work)
{
	struct hid_device *hdev= gamepad_hidraw->hid;
	char *type_uevent[2];
	char send_event_str[30];

	g_uevent_send_working = true;
	printk("[GAMEPAD_GU200A][%s]++\n", __func__);

	do{		
		strcpy(send_event_str, g_raw_event_ptr_send);
		type_uevent[0] = g_raw_event_ptr_send;
		type_uevent[1] = NULL;
		kobject_uevent_env(&hdev->dev.kobj, KOBJ_CHANGE, type_uevent);	
		memset(g_raw_event_ptr_send, 0, sizeof(g_raw_event_str_array[0]));

		if (g_raw_event_ptr_send == &g_raw_event_str_array[BUFFER_LENGTH-1][0]) {
			g_raw_event_ptr_send = &g_raw_event_str_array[0][0];
			g_store_reverse = false;
		} else {
			g_raw_event_ptr_send += sizeof(g_raw_event_str_array[0]);
		}		

		printk("[GAMEPAD_GU200A][%s] send str : %s\n", __func__, send_event_str);
	}while(*g_raw_event_ptr_send != 0);

	g_uevent_send_working = false;
	printk("[GAMEPAD_GU200A][%s]--\n", __func__);
	return;
}

static int gamepad_usb_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret = 0;
	unsigned int cmask = HID_CONNECT_DEFAULT;
	struct gamepad_drvdata *drvdata;
	unsigned UsageID;

	printk("[GAMEPAD_GU200A] hid->name : %s\n", hdev->name);
	printk("[GAMEPAD_GU200A] hid->vendor  : 0x%x\n", hdev->vendor);
	printk("[GAMEPAD_GU200A] hid->product : 0x%x\n", hdev->product);
	//ASUSEvtlog("[GAMEPAD_GU200A] GamePad connect\n");

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "[GAMEPAD_GU200A] parse failed\n");
		return -ENOMEM;
	}

	printk("[GAMEPAD_GU200A] hid->collection->usage : 0x%x\n", hdev->collection->usage);
	UsageID = ((hdev->collection->usage >> 16) & 0x00FF );

	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		hid_err(hdev, "[GAMEPAD_GU200A] Can't alloc drvdata\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, drvdata);
	ret = hid_hw_start(hdev, cmask);
	if (ret) {
		hid_err(hdev, "[GAMEPAD_GU200A] hw start failed\n");
		goto err_free;
	}

	ret = hid_hw_open(hdev);
	if(ret) {
		hid_err(hdev, "[GAMEPAD_GU200A] hid_hw_open failed\n");
		goto err_free;
	}

	printk("[GAMEPAD_GU200A] UsageID : 0x%x\n", UsageID);
	if( UsageID != 0xee ){
		printk("[GAMEPAD_GU200A] Skip register sysfs.\n");
		return 0;
	}

	gamepad_hidraw = hdev->hidraw;
		
	// Register sys class  
	ret = aura_sync_register(&hdev->dev, drvdata);
	if (ret) {
		hid_err(hdev, "[GAMEPAD_GU200A] aura_sync_register failed\n");
		goto err_free;
	}

    g_mode=-1;
    g_speed=-1;
    g_led_on=-1;
	g_brightness=-1;
	g_key=1;
	g_rog_button = 0;	
	g_fw_mode = 0;

	memset(g_raw_event_str_array, 0, sizeof(g_raw_event_str_array));
	g_raw_event_ptr_store = g_raw_event_str_array[0];
	g_raw_event_ptr_send = g_raw_event_str_array[0];
	g_uevent_send_working = false;
	g_store_reverse = false;

	if(hdev->product == 0x1AE4){
		g_fw_mode = 1;
		printk("[GAMEPAD_GU200A] gu200a in AP mode.\n");
		ret = sysfs_create_group(&drvdata->led.dev->kobj, &aprom_attr_group);
		if (ret){
			printk("[GAMEPAD_GU200A] sysfs_create_group err : %d\n",ret);
			goto unregister;
		}
		init_brightness(100);
		init_mode();
	}		
	if(hdev->product == 0xFF48){
		g_fw_mode = 2;
		printk("[GAMEPAD_GU200A] gu200a in bootloader.\n");
		ret = sysfs_create_group(&drvdata->led.dev->kobj, &bootloader_attr_group);
		if (ret){
			printk("[GAMEPAD_GU200A] sysfs_create_group err : %d\n",ret);
			goto unregister;
		}
	}	

	show_fw_ver_in_probe();

	drvdata->send_event_workqueue = alloc_ordered_workqueue("send_event_workqueue", 0);
	if (!drvdata->send_event_workqueue) {
		return -1;
	}
	INIT_WORK(&drvdata->send_event_work, uevent_send_work);

	GamePad_connect(2);
	return 0;

unregister:
	aura_sync_unregister(drvdata);
err_free:
	printk("[GAMEPAD_GU200A] gamepad_usb_probe fail.\n");
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	devm_kfree(&hdev->dev, drvdata);
	return ret;
}

static void gamepad_usb_remove(struct hid_device *hdev)
{
	unsigned UsageID;
	struct gamepad_drvdata *drvdata = dev_get_drvdata(&hdev->dev);
	//ASUSEvtlog("[GAMEPAD_GU200A] GamePad disconnect!!!\n");
	printk("[GAMEPAD_GU200A] GamePad disconnect!!!\n");
	UsageID = ((hdev->collection->usage >> 16) & 0x00FF );

	if( UsageID == 0xee ){
		sysfs_remove_group(&drvdata->led.dev->kobj, &aprom_attr_group);
		aura_sync_unregister(drvdata);
		gamepad_hidraw = NULL;
		GamePad_connect(0);
	}

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	devm_kfree(&hdev->dev, drvdata);
}

static const struct hid_report_id gamepad_usb_report_table[] = {
	{ HID_INPUT_REPORT   }
};

static struct hid_device_id gamepad_idtable[] = {
	{ HID_USB_DEVICE(0x0B05, 0x1AE4),
		.driver_data = 0 },
	{ HID_USB_DEVICE(0x0B05, 0xFF48),
		.driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(hid, gamepad_idtable);

static struct hid_driver gamepad_hid_driver = {
	.name		= "gamepad_gu200a",
	.id_table		= gamepad_idtable,
	.probe			= gamepad_usb_probe,
	.remove			= gamepad_usb_remove,
	.raw_event		= gamepad_usb_raw_event,
	.report_table	= gamepad_usb_report_table,
#ifdef CONFIG_PM
	.suspend        = gamepad_usb_suspend,
	.resume			= gamepad_usb_resume,
#endif
};

static int __init gamepad_usb_init(void)
{
	printk("[GAMEPAD_GU200A] gamepad_usb_init\n");
	return hid_register_driver(&gamepad_hid_driver);
}

static void __exit gamepad_usb_exit(void)
{
	printk("[GAMEPAD_GU200A] gamepad_usb_exit\n");
	hid_unregister_driver(&gamepad_hid_driver);
}

module_init(gamepad_usb_init);
module_exit(gamepad_usb_exit);


MODULE_AUTHOR("ASUS Deeo");
MODULE_DESCRIPTION("GamePad GU200A HID Interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("asus:gamepad GU200A HID");
