/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2020, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*****************************************************************************
*
* File Name: focaltech_core.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract: entrance for focaltech ts driver
*
* Version: V1.0
*
*****************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <linux/of_irq.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <drm/drm_panel.h>
/* ASUS BSP Display +++ */
#include "focaltech_core.h"
#include "asus_tp.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_DRIVER_NAME                     "fts_ts"
#define FTS_DRIVER_PEN_NAME                 "fts_ts,pen"
#define INTERVAL_READ_REG                   50  /* unit:ms */
#define TIMEOUT_READ_REG                    1000 /* unit:ms */
#if FTS_POWER_SOURCE_CUST_EN
#define FTS_VTG_MIN_UV                      2800000
#define FTS_VTG_MAX_UV                      3008000
#define FTS_I2C_VTG_MIN_UV                  1800000
#define FTS_I2C_VTG_MAX_UV                  1800000
#endif

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
struct fts_ts_data *fts_data;
//static struct drm_panel *active_panel;
static void fts_ts_panel_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *event, void *client_data);
bool tp3518u = false;
//fod
static int fts_fp_position[4] = {7040,10240, 28384, 31584};
static int *fp_position = NULL;
int fp_key_i= -1;
int fp_press = 0; // 0 : up , 1 : O , 2 : F

//bool fp_filter = false;
ktime_t start ;
bool first_down = false;
//unsigned int time_delta;
bool burst_cancel = false;
int g_panel_state = 1;

//extern void report_last_used_atr_slot(void);
/*****************************************************************************
* FOD
*****************************************************************************/
int fod_gesture_touched = 0;
struct class* fod_class;

static ssize_t fod_touched_show(struct class *class,
					struct class_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", fod_gesture_touched);
}

static ssize_t fod_touched_store(struct class *class,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	if (!count)
		return -EINVAL;

	sscanf(buf, "%d", &fod_gesture_touched);

	return count;
}

static CLASS_ATTR_RW(fod_touched);

static void init_fod_touch_class_obj(void)
{
	int err = 0;
	fod_class = class_create(THIS_MODULE, "asus_fod_touch");
	err = class_create_file(fod_class, &class_attr_fod_touched);
	if (err) {
		pr_err("[Display] Fail to create fod_touched file node\n");
	}
}

struct touch_subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	struct list_head interfaces;
	struct mutex mutex;

	struct kset *drivers_kset;
	struct klist klist_devices;
	struct klist klist_drivers;
	struct blocking_notifier_head bus_notifier;
	unsigned int drivers_autoprobe:1;
	struct bus_type *bus;

	struct kset glue_dirs;
	struct class *class;
};

struct kobject* touch_class_get_kobj(struct class *cls)
{
	if (cls) {
		struct touch_subsys_private* private = (struct touch_subsys_private*)cls->p;
		return &private->subsys.kobj;
	}
	return NULL;
}

void fod_touch_notify(int value)
{
	if (fod_gesture_touched != value) {
		fod_gesture_touched = value;
		sysfs_notify(touch_class_get_kobj(fod_class), NULL, "fod_touched");
	}
}


/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static int fts_ts_suspend(struct device *dev);
static int fts_ts_resume(struct device *dev);

static void fts_ts_register_for_panel_events(struct device_node *dp,
					struct fts_ts_data *ts_data)
{
	const char *touch_type;
	int rc = 0;
	void *cookie = NULL;

	rc = of_property_read_string(dp, "focaltech,touch-type",
						&touch_type);
	if (rc) {
		dev_warn(&fts_data->client->dev,
			"%s: No touch type\n", __func__);
		return;
	}
	if (strcmp(touch_type, "primary")) {
		pr_err("Invalid touch type\n");
		return;
	}

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH, 0 /*active_panel*/,
			&fts_ts_panel_notifier_callback, ts_data);
	if (!cookie) {
		pr_err("Failed to register for panel events\n");
		return;
	}

	/*FTS_DEBUG("registered for panel notifications panel: 0x%x\n",
			active_panel);*/

	ts_data->notifier_cookie = cookie;
}

int fts_check_cid(struct fts_ts_data *ts_data, u8 id_h)
{
    int i = 0;
    struct ft_chip_id_t *cid = &ts_data->ic_info.cid;
    u8 cid_h = 0x0;

    if (cid->type == 0)
        return -ENODATA;

    for (i = 0; i < FTS_MAX_CHIP_IDS; i++) {
        cid_h = ((cid->chip_ids[i] >> 8) & 0x00FF);
        if (cid_h && (id_h == cid_h)) {
            return 0;
        }
    }

    return -ENODATA;
}
/*****************************************************************************
*  Name: fts_wait_tp_to_valid
*  Brief: Read chip id until TP FW become valid(Timeout: TIMEOUT_READ_REG),
*         need call when reset/power on/resume...
*  Input:
*  Output:
*  Return: return 0 if tp valid, otherwise return error code
*****************************************************************************/
int fts_wait_tp_to_valid(void)
{
    int ret = 0;
    int cnt = 0;
    u8 idh = 0;
    struct fts_ts_data *ts_data = fts_data;
    u8 chip_idh = ts_data->ic_info.ids.chip_idh;

    do {
        ret = fts_read_reg(FTS_REG_CHIP_ID, &idh);
        if ((idh == chip_idh) || (fts_check_cid(ts_data, idh) == 0)) {
            FTS_INFO("TP Ready,Device ID:0x%02x", idh);
            return 0;
        } else
            FTS_DEBUG("TP Not Ready,ReadData:0x%02x,ret:%d", idh, ret);

        cnt++;
        msleep(INTERVAL_READ_REG);
    } while ((cnt * INTERVAL_READ_REG) < TIMEOUT_READ_REG);

    return -EIO;
}

/*****************************************************************************
*  Name: fts_tp_state_recovery
*  Brief: Need execute this function when reset
*  Input:
*  Output:
*  Return:
*****************************************************************************/
void fts_tp_state_recovery(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();
    /* wait tp stable */
    fts_wait_tp_to_valid();
    /* recover TP charger state 0x8B */
    /* recover TP glove state 0xC0 */
    /* recover TP cover state 0xC1 */
    fts_ex_mode_recovery(ts_data);
    /* recover TP gesture state 0xD0 */
    fts_gesture_recovery(ts_data);
    FTS_FUNC_EXIT();
}

int fts_reset_proc(int hdelayms)
{
    //if (fts_data->log_level >= 2)
    FTS_DEBUG("tp reset");
    gpio_direction_output(fts_data->pdata->reset_gpio, 0);
    msleep(5);
    gpio_direction_output(fts_data->pdata->reset_gpio, 1);
    if (hdelayms) {
        msleep(hdelayms);
    }
    fts_data->wait_reset = false;
    return 0;
}

void fts_irq_disable(void)
{
//    unsigned long irqflags;

    FTS_FUNC_ENTER();
//    spin_lock_irqsave(&fts_data->irq_lock, irqflags);

    if (!fts_data->irq_disabled) {
        disable_irq_nosync(fts_data->irq);
        fts_data->irq_disabled = true;
    }

//    spin_unlock_irqrestore(&fts_data->irq_lock, irqflags);
    FTS_FUNC_EXIT();
}

void fts_irq_enable(void)
{
//    unsigned long irqflags = 0;

    FTS_FUNC_ENTER();
//    spin_lock_irqsave(&fts_data->irq_lock, irqflags);

    if (fts_data->irq_disabled) {
        enable_irq(fts_data->irq);
        fts_data->irq_disabled = false;
    }

//    spin_unlock_irqrestore(&fts_data->irq_lock, irqflags);
    FTS_FUNC_EXIT();
}

void fts_hid2std(void)
{
    int ret = 0;
    u8 buf[3] = {0xEB, 0xAA, 0x09};

    if (fts_data->bus_type != BUS_TYPE_I2C)
        return;

    ret = fts_write(buf, 3);
    if (ret < 0) {
        FTS_ERROR("hid2std cmd write fail");
    } else {
        msleep(10);
        buf[0] = buf[1] = buf[2] = 0;
        ret = fts_read(NULL, 0, buf, 3);
        if (ret < 0) {
            FTS_ERROR("hid2std cmd read fail");
        } else if ((0xEB == buf[0]) && (0xAA == buf[1]) && (0x08 == buf[2])) {
            FTS_DEBUG("hidi2c change to stdi2c successful");
        } else {
            FTS_DEBUG("hidi2c change to stdi2c not support or fail");
        }
    }
}

static int fts_match_cid(struct fts_ts_data *ts_data,
                         u16 type, u8 id_h, u8 id_l, bool force)
{
#ifdef FTS_CHIP_ID_MAPPING
    u32 i = 0;
    u32 j = 0;
    struct ft_chip_id_t chip_id_list[] = FTS_CHIP_ID_MAPPING;
    u32 cid_entries = sizeof(chip_id_list) / sizeof(struct ft_chip_id_t);
    u16 id = (id_h << 8) + id_l;

    memset(&ts_data->ic_info.cid, 0, sizeof(struct ft_chip_id_t));
    for (i = 0; i < cid_entries; i++) {
        if (!force && (type == chip_id_list[i].type)) {
            break;
        } else if (force && (type == chip_id_list[i].type)) {
            FTS_INFO("match cid,type:0x%x", (int)chip_id_list[i].type);
            ts_data->ic_info.cid = chip_id_list[i];
            return 0;
        }
    }

    if (i >= cid_entries) {
        return -ENODATA;
    }

    for (j = 0; j < FTS_MAX_CHIP_IDS; j++) {
        if (id == chip_id_list[i].chip_ids[j]) {
            FTS_DEBUG("cid:%x==%x", id, chip_id_list[i].chip_ids[j]);
            FTS_INFO("match cid,type:0x%x", (int)chip_id_list[i].type);
            ts_data->ic_info.cid = chip_id_list[i];
            return 0;
        }
    }

    return -ENODATA;
#else
    return -EINVAL;
#endif
}

static int fts_get_chip_types(
    struct fts_ts_data *ts_data,
    u8 id_h, u8 id_l, bool fw_valid)
{
    u32 i = 0;
    struct ft_chip_t ctype[] = FTS_CHIP_TYPE_MAPPING;
    struct ft_chip_t ctype_3518[] = FTS_CHIP_TYPE_MAPPING_3518;
    u32 ctype_entries = sizeof(ctype) / sizeof(struct ft_chip_t);

    if ((0x0 == id_h) || (0x0 == id_l)) {
        FTS_ERROR("id_h/id_l is 0");
        return -EINVAL;
    }

    FTS_DEBUG("verify id:0x%02x%02x", id_h, id_l);
    for (i = 0; i < ctype_entries; i++) {
        if (VALID == fw_valid) {
            if (((id_h == ctype[i].chip_idh) && (id_l == ctype[i].chip_idl))
                || (!fts_match_cid(ts_data, ctype[i].type, id_h, id_l, 0)))
                break;
        } else {
            if (((id_h == ctype[i].rom_idh) && (id_l == ctype[i].rom_idl))
                || ((id_h == ctype[i].pb_idh) && (id_l == ctype[i].pb_idl))
                || ((id_h == ctype[i].bl_idh) && (id_l == ctype[i].bl_idl))) {
                break;
            }
        }
    }
#if FTS_CHECK_3518    
    if (i >= ctype_entries) {
      FTS_DEBUG("check TP FTS3518U");
      for (i = 0; i < ctype_entries; i++) {
	  if (VALID == fw_valid) {
	      if ((id_h == ctype_3518[i].chip_idh) && (id_l == ctype_3518[i].chip_idl)){
		  tp3518u = true;
		  FTS_DEBUG("TP is FTS3518U");
		  break;
	      }
	  } else {
	      if (((id_h == ctype_3518[i].rom_idh) && (id_l == ctype_3518[i].rom_idl))
		  || ((id_h == ctype_3518[i].pb_idh) && (id_l == ctype_3518[i].pb_idl))
		  || ((id_h == ctype_3518[i].bl_idh) && (id_l == ctype_3518[i].bl_idl)))
		  break;
	  }
      }      
    }
#endif

    if (i >= ctype_entries) {
        return -ENODATA;
    }
#if FTS_CHECK_3518        
    if (tp3518u) {
        ts_data->ic_info.ids = ctype_3518[i];
    } else
#endif
    fts_match_cid(ts_data, ctype[i].type, id_h, id_l, 1);
    ts_data->ic_info.ids = ctype[i];
    return 0;
}

static int fts_read_bootid(struct fts_ts_data *ts_data, u8 *id)
{
    int ret = 0;
    u8 chip_id[2] = { 0 };
    u8 id_cmd[4] = { 0 };
    u32 id_cmd_len = 0;

    id_cmd[0] = FTS_CMD_START1;
    id_cmd[1] = FTS_CMD_START2;
    ret = fts_write(id_cmd, 2);
    if (ret < 0) {
        FTS_ERROR("start cmd write fail");
        return ret;
    }

    msleep(FTS_CMD_START_DELAY);
    id_cmd[0] = FTS_CMD_READ_ID;
    id_cmd[1] = id_cmd[2] = id_cmd[3] = 0x00;
    if (ts_data->ic_info.is_incell)
        id_cmd_len = FTS_CMD_READ_ID_LEN_INCELL;
    else
        id_cmd_len = FTS_CMD_READ_ID_LEN;
    ret = fts_read(id_cmd, id_cmd_len, chip_id, 2);
    if ((ret < 0) || (0x0 == chip_id[0]) || (0x0 == chip_id[1])) {
        FTS_ERROR("read boot id fail,read:0x%02x%02x", chip_id[0], chip_id[1]);
        return -EIO;
    }

    id[0] = chip_id[0];
    id[1] = chip_id[1];
    return 0;
}

/*****************************************************************************
* Name: fts_get_ic_information
* Brief: read chip id to get ic information, after run the function, driver w-
*        ill know which IC is it.
*        If cant get the ic information, maybe not focaltech's touch IC, need
*        unregister the driver
* Input:
* Output:
* Return: return 0 if get correct ic information, otherwise return error code
*****************************************************************************/
static int fts_get_ic_information(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int cnt = 0;
    u8 chip_id[2] = { 0 };

    ts_data->ic_info.is_incell = FTS_CHIP_IDC;
    ts_data->ic_info.hid_supported = FTS_HID_SUPPORTTED;

    FTS_FUNC_ENTER();
    do {
        ret = fts_read_reg(FTS_REG_CHIP_ID, &chip_id[0]);
        ret = fts_read_reg(FTS_REG_CHIP_ID2, &chip_id[1]);
        if ((ret < 0) || (0x0 == chip_id[0]) || (0x0 == chip_id[1])) {
            FTS_DEBUG("chip id read invalid, read:0x%02x%02x",
                      chip_id[0], chip_id[1]);
        } else {
            ret = fts_get_chip_types(ts_data, chip_id[0], chip_id[1], VALID);
            if (!ret)
                break;
            else
                FTS_DEBUG("TP not ready, read:0x%02x%02x",
                          chip_id[0], chip_id[1]);
        }

        cnt++;
        msleep(INTERVAL_READ_REG);
    } while ((cnt * INTERVAL_READ_REG) < TIMEOUT_READ_REG);

    if ((cnt * INTERVAL_READ_REG) >= TIMEOUT_READ_REG) {
        FTS_INFO("fw is invalid, need read boot id");
        if (ts_data->ic_info.hid_supported) {
            fts_hid2std();
        }


        ret = fts_read_bootid(ts_data, &chip_id[0]);
        if (ret <  0) {
            FTS_ERROR("read boot id fail");
            return ret;
        }

        ret = fts_get_chip_types(ts_data, chip_id[0], chip_id[1], INVALID);
        if (ret < 0) {
            FTS_ERROR("can't get ic informaton");
            return ret;
        }
    }

    FTS_INFO("get ic information, chip id = 0x%02x%02x(cid type=0x%x)",
             ts_data->ic_info.ids.chip_idh, ts_data->ic_info.ids.chip_idl,
             ts_data->ic_info.cid.type);
    FTS_FUNC_EXIT();
    return 0;
}

/*****************************************************************************
*  Reprot related
*****************************************************************************/
static void fts_show_touch_buffer(u8 *data, int datalen)
{
    int i = 0;
    int count = 0;
    char *tmpbuf = NULL;

    tmpbuf = kzalloc(1024, GFP_KERNEL);
    if (!tmpbuf) {
        FTS_ERROR("tmpbuf zalloc fail");
        return;
    }

    for (i = 0; i < datalen; i++) {
        count += snprintf(tmpbuf + count, 1024 - count, "%02X,", data[i]);
        if (count >= 1024)
            break;
    }
    FTS_DEBUG("point buffer:%s", tmpbuf);

    if (tmpbuf) {
        kfree(tmpbuf);
        tmpbuf = NULL;
    }
}

void fts_release_all_finger(void) // Tingyi:Questio 1
{
    struct fts_ts_data *ts_data = fts_data;
    struct input_dev *input_dev = ts_data->input_dev;
    u32 i = 0;

    FTS_FUNC_ENTER();
    mutex_lock(&ts_data->report_mutex);

    fts_data->atr_press = false;
    clear_atr_slot();

    for (i = 0; i < MAX_I2C_TOUCH_NUMBER + MAX_ATR_TOUCH_NUMBER; i ++) { // Tingyi: Bug?
        input_mt_slot(input_dev, i);
        input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
    }

    input_report_key(input_dev, BTN_TOUCH, 0);
    input_sync(input_dev);
    
    ts_data->touchs = 0;
    ts_data->key_state = 0;
    mutex_unlock(&ts_data->report_mutex);
    FTS_FUNC_EXIT();
}

void wakeup_panel_by_pwrkey(void)
{
    if(g_panel_state == 1)
        return;

    input_report_key(fts_data->input_dev, KEY_POWER,1);
    input_sync(fts_data->input_dev);
    input_report_key(fts_data->input_dev, KEY_POWER,0);
    input_sync(fts_data->input_dev);
    FTS_DEBUG("wake panel for fandongle7");
    return;
}
EXPORT_SYMBOL(wakeup_panel_by_pwrkey);


static int fts_input_report_b(struct fts_ts_data *data)
{
    int i = 0;
    int uppoint = 0;
    int touchs = 0;
    int ret = 0;
    bool va_reported = false;

    struct ts_event *events = data->events;

    mutex_lock(&data->report_mutex);

    for (i = 0; i < data->touch_point; i++)
    {
        va_reported = true;
        input_mt_slot(data->input_dev, events[i].id);

        if (EVENT_DOWN(events[i].flag))
        {
            if ((fp_press == 1) && (events[i].p >= data->fp_mini))
                fp_press = 0;

            // event down for FP check
            if((data-> fp_report_type > 0) && (fp_press == 0))
            {
                if (fp_position[0] <= events[i].x && events[i].x <= fp_position[1] &&
                    fp_position[2] <= events[i].y && events[i].y <= fp_position[3])
                {
                    FTS_DEBUG("[B](id,x,y,p,rate)(%d,%d,%d,%d,%d) DOWN!",
                            events[i].id,
                            events[i].x, events[i].y,
                            events[i].p, events[i].rate);
                    if (events[i].p < data->fp_mini) {
                        FTS_INFO("Finger %d area %d too small , KEY O",events[i].id,events[i].p);
                        input_report_key(data->input_dev, KEY_O,1);
                        input_sync(data->input_dev);
                        input_report_key(data->input_dev, KEY_O,0);
                        input_sync(data->input_dev);
                        data->fp_x = events[i].x;
                        data->fp_y = events[i].y;
                        fp_press = 1;
                        fp_key_i = events[i].id;
                        data->fp_filter = true;
                    } else {
                        FTS_INFO("Finger %d down at FP area, KEY F",events[i].id);
                        input_report_key(data->input_dev, KEY_F,1);
                        input_sync(data->input_dev);
                        input_report_key(data->input_dev, KEY_F,0);
                        input_sync(data->input_dev);
                        /* ASUS BSP FOD +++ */
                        fod_touch_notify(1);
                        data->fp_x = events[i].x;
                        data->fp_y = events[i].y;
                        fp_press = 2;
                        fp_key_i = events[i].id;
                        data->fp_filter = true;
                    }
                } else if (data-> fp_report_type == 1) {
                    FTS_INFO("Finger %d down out of FP area,KEY L",events[i].id);
                    input_report_key(data->input_dev, KEY_L,1);
                    input_sync(data->input_dev);
                    input_report_key(data->input_dev, KEY_L,0);
                    input_sync(data->input_dev);
                    fp_press = 2;
                    fp_key_i = events[i].id;
                    data->fp_filter = true;
                }
            }

            // event down for ??
            if (data->fp_filter && (events[i].id == fp_key_i)) {
                if (fp_position[0] <= events[i].x && events[i].x <= fp_position[1] &&
                fp_position[2] <= events[i].y && events[i].y <= fp_position[3])
            {
                //ignore , icon position not report finger down
                } else {
                    if (data-> fp_report_type == 2) {
                        input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
                        input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, events[i].p);
			            input_report_abs(data->input_dev, ABS_MT_POSITION_X, events[i].x);
			            input_report_abs(data->input_dev, ABS_MT_POSITION_Y, events[i].y);
                    }
                }
            } else {
                if (!fts_data->wait_reset){
                    input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);
                    if (data->realtime == 1) {
                        input_set_timestamp(data->input_dev, events[i].time);
                    }
                    input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, events[i].p);
		            input_report_abs(data->input_dev, ABS_MT_POSITION_X, events[i].x);
		            input_report_abs(data->input_dev, ABS_MT_POSITION_Y, events[i].y);
                }
            }

            touchs |= BIT(events[i].id);
            data->touchs |= BIT(events[i].id);
            if (events[i].count < INT_MAX)
                events[i].count++;

            // show debug info by log level
            if ((data->log_level >= 2) ||
                ((1 == data->log_level) && (FTS_TOUCH_DOWN == events[i].flag)))
            {
                if(data->realtime == 1)
                {
                    FTS_DEBUG("[B](id,x,y,count,area,rate,touchs,event_time)(%d,%d,%d,%d,%d,%d,%d,%llu) DOWN!",
                    events[i].id,
                    events[i].x, events[i].y,events[i].count,
                    events[i].p, events[i].rate,touchs,events[i].time);
                } else {
                    FTS_INFO("[B](id,x,y,count)(%d,%d,%d,%d) DOWN!",
                    events[i].id,(events[i].x)/16, (events[i].y)/16,events[i].count);
                }
            }else if(events[i].count == 1)
            {
                FTS_INFO("[B](id,x,y,count)(%d,%d,%d,%d) DOWN*!",
                    events[i].id,(events[i].x)/16, (events[i].y)/16,events[i].count);
            }

        // ! (EVENT_DOWN(events[i].flag))
        } else
        {
            // Finger UP
            if (!fts_data->wait_reset){
                uppoint++;
                input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
                data->touchs &= ~BIT(events[i].id);
                // Show log
                if (data->log_level >= 1) {
                    //FTS_INFO("[B]id %d UP !", events[i].id);
                    FTS_INFO("[B](id,x,y,count)(%d,%d,%d,%d) UP!",
                    events[i].id,(events[i].x)/16, (events[i].y)/16, events[i].count);
                    events[i].count=0;
                }
            }

            // FP UP
            if ((fp_press!=0) && (events[i].id == fp_key_i)) {
                FTS_INFO("Notify FP finger up, KEY_U");
                input_report_key(data->input_dev, KEY_U,1);
                input_sync(data->input_dev);
                input_report_key(data->input_dev, KEY_U,0);
                input_sync(data->input_dev);
                fp_press = 0;
                data->fp_filter = false;
            }
        }
    } // for (i = 0; i < data->touch_point; i++)

    // Check if a finger is down then up during this event loop
    if (unlikely(data->touchs ^ touchs)) {
        for (i = 0; i < MAX_I2C_TOUCH_NUMBER; i++)  {
            if (BIT(i) & (data->touchs ^ touchs)) {
                if (!fts_data->wait_reset){
                    if (data->log_level >= 1) {
                        FTS_DEBUG("[B]id %d UP!", i);
                    }
                    va_reported = true;
                    input_mt_slot(data->input_dev, i);
                    input_set_timestamp(data->input_dev, ktime_get());
                    input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
                }
            }
        }
    }
    data->touchs = touchs;

    if (va_reported) {
        /* touchs==0, there's no point but key */
        if (EVENT_NO_DOWN(data) || (!touchs))
        {
            data->finger_press = false;

            if (!data->atr_press) {
                if (!fts_data->wait_reset){
                    if (data->log_level >= 2)
                        FTS_DEBUG("[B]Points All Up!");
                    input_report_key(data->input_dev, BTN_TOUCH, 0);
                }

                fp_press = 0;
                data->fp_filter = false;
                if (fts_data->wait_reset)
                {
                    fts_data->wait_reset = false;
                    FTS_INFO("reset tp");
                    fts_irq_disable();
                    fts_reset_proc(150);
                    report_rate_recovery(data);
                    fts_ex_fun_recovery(data);
                    fts_irq_enable();
                    if (data->extra_reconfig == 2)
                        set_sub_noise_mode(true);
                }

                if(fts_data->reset_base){
                    FTS_INFO("reset tp base for psensor near");
                    fts_data->reset_base = false;
                    ret = fts_write_reg(0xE4,1);
                    if (ret < 0)
                        FTS_ERROR("reset tp base fail, ret=%d", ret);
                }
            } else if (data->log_level >= 2){
                FTS_INFO("atr pressed, ignore touch up");
            }
        } else { // NOT (EVENT_NO_DOWN(data) || (!touchs))
            if (!data->atr_press)
            {
                if (!data->fp_filter) {
                input_report_key(data->input_dev, BTN_TOUCH, 1);
                }
            }
        }
    }

    input_sync(data->input_dev);

    // If all fingers up but atr hold, need to change slot to let app clear last finger
    /*if (!data->finger_press && data->atr_press){
        report_last_used_atr_slot();
    }*/


    mutex_unlock(&data->report_mutex);
    
    return 0;
}

static int fts_read_touchdata(struct fts_ts_data *data)
{
    int ret = 0;
    int size = 0;
    u8 *buf = data->point_buf;
    memset(buf, 0xFF, data->pnt_buf_size);
    buf[0] = 0x01;

    if (data->gesture_mode) {
        if (0 == fts_gesture_readdata(data, NULL)) {
//            FTS_INFO("succuss to get gesture data in irq handler");
            return 1;
        }
    }
    
    size = data->pnt_buf_size - 1;
    ret = fts_read(buf, 1, buf + 1, size);
    if (ret < 0) {
        FTS_ERROR("read touchdata failed, ret:%d", ret);
        return ret;
    }

    if (data->log_level >= 3) {
        fts_show_touch_buffer(buf, data->pnt_buf_size);
    }

    return 0;
}

static int fts_burst_read_touchdata(struct fts_ts_data *data)
{
    int ret = 0;
    u8 *buf = data->point_buf;
    memset(buf, 0xFF, data->pnt_buf_size);
    buf[0] = 0x01;

    if (data->gesture_mode) {
        if (0 == fts_gesture_readdata(data, NULL)) {
//            FTS_INFO("succuss to get gesture data in irq handler");
            return 1;
        }
    }

    ret = fts_read(buf, 1, buf + 1, data-> burst_data_len);

    if (ret < 0) {
        FTS_ERROR("read touchdata failed, ret:%d", ret);
        return ret;
    }

    if (data->log_level >= 3) {
        fts_show_touch_buffer(buf, data->pnt_buf_size);
    }

    return 0;
}

static int fts_read_parse_touchdata(struct fts_ts_data *data)
{
    int ret = 0;
    int i = 0;
    u8 pointid = 0;
    int base = 0;
    struct ts_event *events = data->events;
    u8 *buf = data->point_buf;
    ktime_t scan_time;
    u8 fw_tpid = 0 , mode = 0;

    if (!burst_cancel && (fts_data->report_rate == REPORT_RATE_2))
    {
	ret = fts_burst_read_touchdata(data);
    } else 
	ret = fts_read_touchdata(data);
    
    if (ret) {
        return ret;
    }
    data->point_num = buf[FTS_TOUCH_POINT_NUM] & 0x0F;
    
    if ((fts_data->report_rate == REPORT_RATE_2) && (data->point_num > BURST_MAX_POINT)) {
	ret = fts_read_touchdata(data);
	data->point_num = buf[FTS_TOUCH_POINT_NUM] & 0x0F;
	burst_cancel = true;
    } else {
        burst_cancel = false;
    } 
    
    data->touch_point = 0;
    
    if (data->ic_info.is_incell) {
        if ((data->point_num == 0x0F) && (buf[2] == 0xFF) && (buf[3] == 0xFF)
            && (buf[4] == 0xFF) && (buf[5] == 0xFF) && (buf[6] == 0xFF)) {
            FTS_DEBUG("touch buff is 0xff, need recovery state");
            fts_release_all_finger();
            fts_tp_state_recovery(data);
            data->point_num = 0;
            return -EIO;
        }
    }

    if (data->point_num > MAX_I2C_TOUCH_NUMBER) {
        FTS_INFO("invalid point_num(%d)", data->point_num);
	    fts_release_all_finger();
        data->point_num = 0;
	    fts_show_touch_buffer(buf, data->pnt_buf_size);
        return -EIO;
    }

    for (i = 0; i < MAX_I2C_TOUCH_NUMBER; i++) {
        base = FTS_ONE_TCH_LEN * i;
        pointid = (buf[FTS_TOUCH_ID_POS + base]) >> 4;

	if (pointid >= FTS_MAX_ID)
            break;
	else if (pointid >= MAX_I2C_TOUCH_NUMBER) {
            FTS_ERROR("ID(%d) beyond MAX_I2C_TOUCH_NUMBER", pointid);
            return -EINVAL;
    }
	data->finger_press = true;
    data->touch_point++;
    if (data->extra_reconfig == 1) {
	    events[i].x = ((buf[ASUS_TOUCH_X_1_POS + base] & 0x0F) << 12) +
			  ((buf[ASUS_TOUCH_X_2_POS + base] & 0xFF) << 4);
	    events[i].y = ((buf[ASUS_TOUCH_Y_1_POS + base] & 0x0F) << 12) +
			  ((buf[ASUS_TOUCH_Y_2_POS + base] & 0xFF) << 4);
    } else {
	    events[i].x = ((buf[ASUS_TOUCH_X_1_POS + base] & 0x0F) << 12) +
			  ((buf[ASUS_TOUCH_X_2_POS + base] & 0xFF) << 4) +
			  (buf[ASUS_TOUCH_X_3_POS + base] >> 4);
	    events[i].y = ((buf[ASUS_TOUCH_Y_1_POS + base] & 0x0F) << 12) +
			  ((buf[ASUS_TOUCH_Y_2_POS + base] & 0xFF) << 4) +
			  (buf[ASUS_TOUCH_Y_3_POS + base] & 0x0F);
    }
        events[i].flag = buf[FTS_TOUCH_EVENT_POS + base] >> 6;
        events[i].id = buf[FTS_TOUCH_ID_POS + base] >> 4;
        events[i].p = buf[ASUS_TOUCH_AREA_POS + base] >> 4; // FP area
        events[i].rate = buf[ASUS_TOUCH_RATE_POS + base] & 0x0F; //report rate
        if (data->realtime == 1) { 
        switch (events[i].rate) {
	  case 9 :
	    scan_time = REPORT_RATE_LEVEL9;
	    break;
	 case 8 :
	    scan_time = REPORT_RATE_LEVEL8;
	    break;
	 case 7 :
	    scan_time = REPORT_RATE_LEVEL7;
	    break;
	 case 6 :
	    scan_time = REPORT_RATE_LEVEL6;
	    break;
	 case 5 :
	    scan_time = REPORT_RATE_LEVEL5;
	    break;
	 case 4 :
	    scan_time = REPORT_RATE_LEVEL4;
	    break;
	 case 3 :
	    scan_time = REPORT_RATE_LEVEL3;
	    break;
	 case 2 :
	    scan_time = REPORT_RATE_LEVEL2;
	    break;
	}
	    events[i].time = ktime_sub_us(data->irq_received,scan_time);
	}
        if (EVENT_DOWN(events[i].flag) && (data->point_num == 0)) {
            FTS_ERROR("abnormal touch data from fw");
	    fts_release_all_finger();
	    fts_show_touch_buffer(buf, data->pnt_buf_size);
            return -EIO;
        }
    }

    if (data->touch_point == 0) {
        FTS_INFO("no touch point information(%02x)", buf[2]);
	fts_show_touch_buffer(buf, data->pnt_buf_size);
	fts_read_reg(FTS_REG_TOUCH_ID, &fw_tpid);
	msleep(10);
	fts_read_reg(FTS_REG_TOUCH_MODE, &mode);
	FTS_INFO("FW read reg 0x02: %d reg 0xA5: %d",fw_tpid,mode);
	FTS_INFO("reset tp");
	fts_irq_disable();
	fts_reset_proc(150);
	report_rate_recovery(data);
	asus_game_recovery(data);
	fts_ex_fun_recovery(data);
	fts_irq_enable();
	
        return -EIO;
    }

    return 0;
}

static void fts_irq_read_report(void)
{
    int ret = 0;
    struct fts_ts_data *ts_data = fts_data;

    ret = fts_read_parse_touchdata(ts_data);
    if (ret == 0) {
        fts_input_report_b(ts_data);
    }

}

static irqreturn_t fts_irq_handler(int irq, void *data)
{
    if ((fts_data->perftime == 1) || (fts_data->realtime == 1) ) {
	fts_data->irq_received = ktime_get();
//	FTS_INFO("irq time %llu",fts_data->irq_received);
    }
    
    fts_irq_read_report();
    return IRQ_HANDLED;
}

static int fts_irq_registration(struct fts_ts_data *ts_data)
{
    int ret = 0;
    struct fts_ts_platform_data *pdata = ts_data->pdata;

    ts_data->irq = gpio_to_irq(pdata->irq_gpio);
    pdata->irq_gpio_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
    FTS_INFO("irq:%d, flag:%x", ts_data->irq, pdata->irq_gpio_flags);
    ret = request_threaded_irq(ts_data->irq, NULL, fts_irq_handler,
                               pdata->irq_gpio_flags,
                               FTS_DRIVER_NAME, ts_data);

    return ret;
}
static int fts_input_init(struct fts_ts_data *ts_data)
{
    int ret = 0;
    struct fts_ts_platform_data *pdata = ts_data->pdata;
    struct input_dev *input_dev;

    FTS_FUNC_ENTER();
    input_dev = input_allocate_device();
    if (!input_dev) {
        FTS_ERROR("Failed to allocate memory for input device");
        return -ENOMEM;
    }

    /* Init and register Input device */
    input_dev->name = FTS_DRIVER_NAME;
    if (ts_data->bus_type == BUS_TYPE_I2C)
        input_dev->id.bustype = BUS_I2C;
    else
        input_dev->id.bustype = BUS_SPI;
    
    input_dev->id.product = 0x5652;
    input_dev->id.vendor = 0x2808;
    input_dev->id.version = 0x8d;
    
    input_dev->dev.parent = ts_data->dev;

    input_set_drvdata(input_dev, ts_data);

    __set_bit(EV_SYN, input_dev->evbit);
    __set_bit(EV_ABS, input_dev->evbit);
    __set_bit(EV_KEY, input_dev->evbit);
    __set_bit(BTN_TOUCH, input_dev->keybit);
    __set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

    input_mt_init_slots(input_dev, MAX_I2C_TOUCH_NUMBER + MAX_ATR_TOUCH_NUMBER, INPUT_MT_DIRECT);
    input_set_abs_params(input_dev, ABS_MT_POSITION_X, pdata->x_min, pdata->x_max, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, pdata->y_min, pdata->y_max, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 0xFF, 0, 0);

    ret = input_register_device(input_dev);
    if (ret) {
        FTS_ERROR("Input device registration failed");
        input_set_drvdata(input_dev, NULL);
        input_free_device(input_dev);
        input_dev = NULL;
        return ret;
    }
    ts_data->input_dev = input_dev;

    FTS_FUNC_EXIT();
    return 0;
}

static int fts_report_buffer_init(struct fts_ts_data *ts_data) // Tingyi: ??? To check..
{
    int point_num = 0;
    int events_num = 0;


    point_num = MAX_I2C_TOUCH_NUMBER;
    ts_data->pnt_buf_size = point_num * FTS_ONE_TCH_LEN + 3;
    ts_data->point_buf = (u8 *)kzalloc(ts_data->pnt_buf_size + 1, GFP_KERNEL);
    ts_data->burst_data_len = BURST_MAX_POINT * FTS_ONE_TCH_LEN + 3; 
    
    if (!ts_data->point_buf) {
        FTS_ERROR("failed to alloc memory for point buf");
        return -ENOMEM;
    }

    events_num = point_num * sizeof(struct ts_event);
    ts_data->events = (struct ts_event *)kzalloc(events_num, GFP_KERNEL);
    if (!ts_data->events) {
        FTS_ERROR("failed to alloc memory for point events");
        kfree_safe(ts_data->point_buf);
        return -ENOMEM;
    }

    return 0;
}

#if FTS_POWER_SOURCE_CUST_EN
static int fts_power_source_ctrl(struct fts_ts_data *ts_data, int enable)
{
    int ret = 0;

    if (IS_ERR_OR_NULL(ts_data->vdd)) {
        FTS_ERROR("vdd is invalid");
        return -EINVAL;
    }

    FTS_FUNC_ENTER();
    if (enable) {
        if (ts_data->power_disabled) {
//            FTS_DEBUG("regulator enable !");
            gpio_direction_output(ts_data->pdata->reset_gpio, 0);
            msleep(1);

            if (!IS_ERR_OR_NULL(ts_data->vcc_i2c)) {
                ret = regulator_enable(ts_data->vcc_i2c);
                if (ret) {
                    FTS_ERROR("enable vcc_i2c regulator failed,ret=%d", ret);
                }
            }
	    
	    ret = regulator_enable(ts_data->vdd);
            if (ret) {
                FTS_ERROR("enable vdd regulator failed,ret=%d", ret);
            }

            ts_data->power_disabled = false;
        }
    } else {
        if (!ts_data->power_disabled) {
//            FTS_DEBUG("regulator disable !");
            gpio_direction_output(ts_data->pdata->reset_gpio, 0);
            msleep(10);
            ret = regulator_disable(ts_data->vdd);
            if (ret) {
                FTS_ERROR("disable vdd regulator failed,ret=%d", ret);
            }
            if (!IS_ERR_OR_NULL(ts_data->vcc_i2c)) {
                ret = regulator_disable(ts_data->vcc_i2c);
                if (ret) {
                    FTS_ERROR("disable vcc_i2c regulator failed,ret=%d", ret);
                }
            }
            ts_data->power_disabled = true;
        }
    }

    FTS_FUNC_EXIT();
    return ret;
}

/*****************************************************************************
* Name: fts_power_source_init
* Brief: Init regulator power:vdd/vcc_io(if have), generally, no vcc_io
*        vdd---->vdd-supply in dts, kernel will auto add "-supply" to parse
*        Must be call after fts_gpio_configure() execute,because this function
*        will operate reset-gpio which request gpio in fts_gpio_configure()
* Input:
* Output:
* Return: return 0 if init power successfully, otherwise return error code
*****************************************************************************/
static int fts_power_source_init(struct fts_ts_data *ts_data)
{
    int ret = 0;

    FTS_FUNC_ENTER();
    ts_data->vdd = regulator_get(ts_data->dev, "vdd");
    if (IS_ERR_OR_NULL(ts_data->vdd)) {
        ret = PTR_ERR(ts_data->vdd);
        FTS_ERROR("get vdd regulator failed,ret=%d", ret);
        return ret;
    }

    if (regulator_count_voltages(ts_data->vdd) > 0) {
        ret = regulator_set_voltage(ts_data->vdd, FTS_VTG_MIN_UV,
                                    FTS_VTG_MAX_UV);
        if (ret) {
            FTS_ERROR("vdd regulator set_vtg failed ret=%d", ret);
            regulator_put(ts_data->vdd);
            return ret;
        }
    }

    ts_data->vcc_i2c = regulator_get(ts_data->dev, "vcc_i2c");
    if (!IS_ERR_OR_NULL(ts_data->vcc_i2c)) {
        if (regulator_count_voltages(ts_data->vcc_i2c) > 0) {
            ret = regulator_set_voltage(ts_data->vcc_i2c,
                                        FTS_I2C_VTG_MIN_UV,
                                        FTS_I2C_VTG_MAX_UV);
            if (ret) {
                FTS_ERROR("vcc_i2c regulator set_vtg failed,ret=%d", ret);
                regulator_put(ts_data->vcc_i2c);
            }
        }
    }

    ts_data->power_disabled = true;
    ret = fts_power_source_ctrl(ts_data, ENABLE);
    if (ret) {
        FTS_ERROR("fail to enable power(regulator)");
    }

    FTS_FUNC_EXIT();
    return ret;
}

static int fts_power_source_exit(struct fts_ts_data *ts_data)
{
    fts_power_source_ctrl(ts_data, DISABLE);

    if (!IS_ERR_OR_NULL(ts_data->vdd)) {
        if (regulator_count_voltages(ts_data->vdd) > 0)
            regulator_set_voltage(ts_data->vdd, 0, FTS_VTG_MAX_UV);
        regulator_put(ts_data->vdd);
    }

    if (!IS_ERR_OR_NULL(ts_data->vcc_i2c)) {
        if (regulator_count_voltages(ts_data->vcc_i2c) > 0)
            regulator_set_voltage(ts_data->vcc_i2c, 0, FTS_I2C_VTG_MAX_UV);
        regulator_put(ts_data->vcc_i2c);
    }

    return 0;
}

static int fts_power_source_suspend(struct fts_ts_data *ts_data)
{
    int ret = 0;

    ret = fts_power_source_ctrl(ts_data, DISABLE);
    if (ret < 0) {
        FTS_ERROR("power off fail, ret=%d", ret);
    }

    return ret;
}

static int fts_power_source_resume(struct fts_ts_data *ts_data)
{
    int ret = 0;

    ret = fts_power_source_ctrl(ts_data, ENABLE);
    if (ret < 0) {
        FTS_ERROR("power on fail, ret=%d", ret);
    }

    return ret;
}
#endif /* FTS_POWER_SOURCE_CUST_EN */

static int fts_gpio_configure(struct fts_ts_data *data)
{
    int ret = 0;

    FTS_FUNC_ENTER();
    /* request irq gpio */
    if (gpio_is_valid(data->pdata->irq_gpio)) {
        ret = gpio_request(data->pdata->irq_gpio, "fts_irq_gpio");
        if (ret) {
            FTS_ERROR("[GPIO]irq gpio request failed");
            goto err_irq_gpio_req;
        }

        ret = gpio_direction_input(data->pdata->irq_gpio);
        if (ret) {
            FTS_ERROR("[GPIO]set_direction for irq gpio failed");
            goto err_irq_gpio_dir;
        }
    }

    /* request reset gpio */
    if (gpio_is_valid(data->pdata->reset_gpio)) {
        ret = gpio_request(data->pdata->reset_gpio, "fts_reset_gpio");
        if (ret) {
            FTS_ERROR("[GPIO]reset gpio request failed");
            goto err_irq_gpio_dir;
        }

        ret = gpio_direction_output(data->pdata->reset_gpio, 0);
        if (ret) {
            FTS_ERROR("[GPIO]set_direction for reset gpio failed");
            goto err_reset_gpio_dir;
        }
    }
 
    FTS_FUNC_EXIT();
    return 0;

err_reset_gpio_dir:
    if (gpio_is_valid(data->pdata->reset_gpio))
        gpio_free(data->pdata->reset_gpio);
err_irq_gpio_dir:
    if (gpio_is_valid(data->pdata->irq_gpio))
        gpio_free(data->pdata->irq_gpio);
err_irq_gpio_req:
    FTS_FUNC_EXIT();
    return ret;
}

static int fts_get_dt_coords(struct device *dev, char *name,
                             struct fts_ts_platform_data *pdata)
{
    int ret = 0;
    u32 coords[FTS_COORDS_ARR_SIZE] = { 0 };
    struct property *prop;
    struct device_node *np = dev->of_node;
    int coords_size;

    prop = of_find_property(np, name, NULL);
    if (!prop)
        return -EINVAL;
    if (!prop->value)
        return -ENODATA;

    coords_size = prop->length / sizeof(u32);
    if (coords_size != FTS_COORDS_ARR_SIZE) {
        FTS_ERROR("invalid:%s, size:%d", name, coords_size);
        return -EINVAL;
    }

    ret = of_property_read_u32_array(np, name, coords, coords_size);
    if (ret < 0) {
        FTS_ERROR("Unable to read %s, please check dts", name);
        pdata->x_min = FTS_X_MIN_DISPLAY_DEFAULT;
        pdata->y_min = FTS_Y_MIN_DISPLAY_DEFAULT;
        pdata->x_max = FTS_X_MAX_DISPLAY_DEFAULT;
        pdata->y_max = FTS_Y_MAX_DISPLAY_DEFAULT;
        return -ENODATA;
    } else {
        pdata->x_min = coords[0];
        pdata->y_min = coords[1];
        pdata->x_max = coords[2]*16 - 1;
        pdata->y_max = coords[3]*16 - 1;
    }

    FTS_INFO("display x(%d %d) y(%d %d)", pdata->x_min, pdata->x_max,
             pdata->y_min, pdata->y_max);
    return 0;
}

static int fts_parse_dt(struct device *dev, struct fts_ts_platform_data *pdata)
{
    int ret = 0;
    struct device_node *np = dev->of_node;

    FTS_FUNC_ENTER();

    ret = fts_get_dt_coords(dev, "focaltech,display-coords", pdata);
    if (ret < 0)
        FTS_ERROR("Unable to get display-coords");

    /* reset, irq gpio info */
    pdata->reset_gpio = of_get_named_gpio_flags(np, "focaltech,reset-gpio",
                        0, &pdata->reset_gpio_flags);
    if (pdata->reset_gpio < 0)
        FTS_ERROR("Unable to get reset_gpio");

    pdata->irq_gpio = of_get_named_gpio_flags(np, "focaltech,irq-gpio",
                      0, &pdata->irq_gpio_flags);
    if (pdata->irq_gpio < 0)
        FTS_ERROR("Unable to get irq_gpio");

    FTS_INFO("max touch number:%d, irq gpio:%d, reset gpio:%d",
             MAX_I2C_TOUCH_NUMBER, pdata->irq_gpio, pdata->reset_gpio);

    FTS_FUNC_EXIT();
    return 0;
}

static void fts_ts_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification, void *client_data)
{
	struct fts_ts_data *ts_data = client_data;
	bool proxy_status = false;

	if (!notification) {
		pr_err("Invalid notification\n");
		return;
	}

	FTS_DEBUG("Notification type:%d, early_trigger:%d",
			notification->notif_type,
			notification->notif_data.early_trigger);
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		ts_data->display_state = 1;

		if (notification->notif_data.early_trigger)
			FTS_DEBUG("resume notification pre commit\n");
		else {
			if (ts_data->irq_off == ENABLE) {
				ts_data->irq_off = DISABLE;
				fts_release_all_finger();
				fts_irq_enable();
			} else {
				if (ts_data->next_resume_isaod) {
					FTS_INFO("Display resume into AOD, not care");
				} else {
					if (fts_data->fp_report_type == 1) {
						FTS_INFO("resume into hbm mode");
					} else {
						fts_release_all_finger();
						queue_work(fts_data->ts_workqueue, &fts_data->resume_work);
					}
				}
			}
		}
		g_panel_state = 1;
		break;
	case DRM_PANEL_EVENT_BLANK:
		ts_data->display_state = 0;
		if (notification->notif_data.early_trigger) {
			ts_data->irq_off = DISABLE;
			if (ts_data->phone_call_state == ENABLE && !ts_data->suspended) {
				proxy_status = proximityStatus();
				if (proxy_status) {
					ts_data->irq_off = ENABLE;
					fts_irq_disable();
					FTS_INFO("Proximity on and on phone call, disable irq not suspend");
					fts_release_all_finger();
				}
			}
			if (ts_data->irq_off != ENABLE) {
				cancel_work_sync(&fts_data->resume_work);
				ts_data->fp_filter = false;
				fts_ts_suspend(ts_data->dev);
			}
		} else {
			FTS_DEBUG("suspend notification post commit\n");
		}
		g_panel_state = 0;
		break;
	case DRM_PANEL_EVENT_BLANK_LP:
		FTS_INFO("DRM_PANEL_BLANK_LP,Display resume into LP1/LP2");
		ts_data->display_state = 2;
		ts_data->next_resume_isaod = false;
		ts_data->fp_filter = false;
		if (!ts_data->suspended) {
			FTS_INFO("Display AOD mode , suspend touch");
			if (ts_data->irq_off == ENABLE) {
				ts_data->irq_off = DISABLE;
				fts_irq_enable();
			}
			cancel_work_sync(&fts_data->resume_work);
			fts_ts_suspend(ts_data->dev);
		}
		g_panel_state = 2;
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		FTS_DEBUG("shashank:Received fps change old fps:%d new fps:%d\n",
				notification->notif_data.old_fps,
				notification->notif_data.new_fps);
		break;
	default:
		FTS_DEBUG("notification serviced :%d\n",
				notification->notif_type);
		break;
	}
}

static void fts_resume_work(struct work_struct *work)
{
    struct fts_ts_data *ts_data = container_of(work, struct fts_ts_data,
                                  resume_work);

    fts_ts_resume(ts_data->dev);
}

void resume_touch(bool trigger) 
{
  if (trigger)
      queue_work(fts_data->ts_workqueue, &fts_data->resume_work);
}

static int fts_ts_probe_entry(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int pdata_size = sizeof(struct fts_ts_platform_data);

    FTS_FUNC_ENTER();
    FTS_INFO("%s", FTS_DRIVER_VERSION);
    ts_data->pdata = kzalloc(pdata_size, GFP_KERNEL);
    if (!ts_data->pdata) {
        FTS_ERROR("allocate memory for platform_data fail");
        return -ENOMEM;
    }

    if (ts_data->dev->of_node) {
        ret = fts_parse_dt(ts_data->dev, ts_data->pdata);
        if (ret)
            FTS_ERROR("device-tree parse fail");

    } else {
        if (ts_data->dev->platform_data) {
            memcpy(ts_data->pdata, ts_data->dev->platform_data, pdata_size);
        } else {
            FTS_ERROR("platform_data is null");
            return -ENODEV;
        }
    }

    spin_lock_init(&ts_data->irq_lock);
    mutex_init(&ts_data->report_mutex);
    mutex_init(&ts_data->bus_lock);

    /* Init communication interface */
    ret = fts_bus_init(ts_data);
    if (ret) {
        FTS_ERROR("bus initialize fail");
        goto err_bus_init;
    }

    ret = fts_input_init(ts_data);
    if (ret) {
        FTS_ERROR("input initialize fail");
        goto err_input_init;
    }

    ret = fts_report_buffer_init(ts_data);
    if (ret) {
        FTS_ERROR("report buffer init fail");
        goto err_report_buffer;
    }

    ret = fts_gpio_configure(ts_data);
    if (ret) {
        FTS_ERROR("configure the gpios fail");
        goto err_gpio_config;
    }

#if FTS_POWER_SOURCE_CUST_EN
    ret = fts_power_source_init(ts_data);
    if (ret) {
        FTS_ERROR("fail to get power(regulator)");
        goto err_power_init;
    }
#endif

#if (!FTS_CHIP_IDC)
    fts_reset_proc(200);
#endif

    ret = fts_get_ic_information(ts_data);
    if (ret) {
        FTS_ERROR("not focal IC, unregister driver");
        goto err_irq_req;
    }

    ret = fts_create_apk_debug_channel(ts_data);
    if (ret) {
        FTS_ERROR("create apk debug node fail");
    }

    ret = fts_create_sysfs(ts_data);
    if (ret) {
        FTS_ERROR("create sysfs node fail");
    }

    ret = fts_ex_mode_init(ts_data);
    if (ret) {
        FTS_ERROR("init glove/cover/charger fail");
    }
    
    ret = asus_create_sysfs(ts_data);
    if (ret) {
        FTS_ERROR("create asus sysfs node fail");
    }

    ret = asus_game_create_sysfs(ts_data);
    if (ret) {
        FTS_ERROR("create asus game sysfs node fail");
    }

    if (!tp3518u) {
	ret = fts_gesture_init(ts_data);
	if (ret) {
	    FTS_ERROR("init gesture fail");
	}
	
	ret = asus_gesture_init(ts_data);
	if (ret) {
	    FTS_ERROR("init asus gesture fail");
	}
    }
#if FTS_TEST_EN
    if (!tp3518u) {
	ret = fts_test_init(ts_data);
	if (ret) {
	    FTS_ERROR("init production test fail");
	}
    }
#endif

    ret = fts_irq_registration(ts_data);
    if (ret) {
        FTS_ERROR("request irq failed");
        goto err_irq_req;
    }
    
    if (!tp3518u) {
	ret = fts_fwupg_init(ts_data);
	if (ret) {
	    FTS_ERROR("init fw upgrade fail");
	}
    }

    if (ts_data->ts_workqueue) {
        INIT_WORK(&ts_data->resume_work, fts_resume_work);
    }

    fts_ts_register_for_panel_events(ts_data->dev->of_node, ts_data);

#if !FTS_AUTO_UPGRADE_EN    
    fts_irq_enable();
#endif

    FTS_FUNC_EXIT();
    ts_data->init_success = 1;
    fp_position = fts_fp_position;
    ts_data->fp_mini = 4;
    FTS_INFO("FOD location %d %d %d %d", fp_position[0], fp_position[1], fp_position[2], fp_position[3]);
    return 0;

err_irq_req:
#if FTS_POWER_SOURCE_CUST_EN
err_power_init:
    fts_power_source_exit(ts_data);
#endif
    if (gpio_is_valid(ts_data->pdata->reset_gpio))
        gpio_free(ts_data->pdata->reset_gpio);
    if (gpio_is_valid(ts_data->pdata->irq_gpio))
        gpio_free(ts_data->pdata->irq_gpio);
err_gpio_config:
    kfree_safe(ts_data->point_buf);
    kfree_safe(ts_data->events);
err_report_buffer:
    input_unregister_device(ts_data->input_dev);
err_input_init:
    if (ts_data->ts_workqueue)
        destroy_workqueue(ts_data->ts_workqueue);
err_bus_init:
    kfree_safe(ts_data->bus_tx_buf);
    kfree_safe(ts_data->bus_rx_buf);
    kfree_safe(ts_data->pdata);
    ts_data->init_success = 0;
    FTS_FUNC_EXIT();
    return ret;
}

static int fts_ts_remove_entry(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();

    ts_data->init_success = 0;
    fts_release_apk_debug_channel(ts_data);
    fts_remove_sysfs(ts_data);
    asus_remove_sysfs(ts_data);
    asus_game_remove_sysfs(ts_data);
    fts_ex_mode_exit(ts_data);

    fts_fwupg_exit(ts_data);

#if FTS_TEST_EN
    fts_test_exit(ts_data);
#endif

    fts_gesture_exit(ts_data);
    fts_bus_exit(ts_data);

    free_irq(ts_data->irq, ts_data);
    input_unregister_device(ts_data->input_dev);
    if (ts_data->ts_workqueue)
        destroy_workqueue(ts_data->ts_workqueue);

    if (/*active_panel && */ts_data->notifier_cookie)
        panel_event_notifier_unregister(ts_data->notifier_cookie);

    if (gpio_is_valid(ts_data->pdata->reset_gpio))
        gpio_free(ts_data->pdata->reset_gpio);

    if (gpio_is_valid(ts_data->pdata->irq_gpio))
        gpio_free(ts_data->pdata->irq_gpio);

#if FTS_POWER_SOURCE_CUST_EN
    fts_power_source_exit(ts_data);
#endif

    kfree_safe(ts_data->point_buf);
    kfree_safe(ts_data->events);

    kfree_safe(ts_data->pdata);
    kfree_safe(ts_data);

    FTS_FUNC_EXIT();

    return 0;
}

static int fts_ts_suspend(struct device *dev)
{
    int ret = 0;
    struct fts_ts_data *ts_data = fts_data;
    int gesture_suspend_ok = 0;
    int enter_gesture_mode = 0;
    u8 reg_e5 = 0;
    u8 reg_e6 = 0;
    
    FTS_FUNC_ENTER();
    if (ts_data->suspended) {
        FTS_INFO("Already in suspend state");
        return 0;
    }

    if (ts_data->fw_loading) {
        FTS_INFO("fw upgrade in process, can't suspend");
        return 0;
    }

    fts_read_reg(FTS_REG_PALM, &reg_e5);
    fts_read_reg(FTS_REG_ILLEGAL_GESTURE, &reg_e6);
    FTS_INFO("Read reg status before suspend E5 0x%X E6 0x%X ",reg_e5,reg_e6);
    
    if(ts_data->sub_noise)
        set_sub_noise_mode(false);

    enter_gesture_mode = is_enter_gesture_mode(ts_data);
//    FTS_INFO("Is enter gesture mode %d",enter_gesture_mode);
    if (enter_gesture_mode == 1) {
        FTS_INFO("Gesture mode enabled , enter gesture mode");
        gesture_suspend_ok = fts_gesture_suspend(ts_data);
	if (gesture_suspend_ok > 0) {
//	  FTS_INFO("Enter gesture mode succussed");
	}
	if (enable_irq_wake(ts_data->irq)) {
	  FTS_DEBUG("enable_irq_wake(irq:%d) fail", ts_data->irq);
	}
    }
    
    if ((gesture_suspend_ok < 0) || (enter_gesture_mode!=1)) {  
//        FTS_INFO("make TP enter into sleep mode");
	fts_irq_disable();
        ret = fts_write_reg(FTS_REG_POWER_MODE, FTS_REG_POWER_MODE_SLEEP);
        if (ret < 0)
            FTS_ERROR("set TP to sleep mode fail, ret=%d", ret);

        if (!ts_data->ic_info.is_incell) {
#if FTS_POWER_SOURCE_CUST_EN
            ret = fts_power_source_suspend(ts_data);
            if (ret < 0) {
                FTS_ERROR("power enter suspend fail");
            }
#endif
        }
    }
    
    fts_release_all_finger();
    ts_data->suspended = true;
    fp_press = 0;
    FTS_FUNC_EXIT();
    return 0;
}

static int fts_ts_resume(struct device *dev)
{
    struct fts_ts_data *ts_data = fts_data;
    int enter_gesture_mode = 0;
    int ret = 0;
    u8 reg_e6 = 0;
    bool proxy_status = false;

    FTS_FUNC_ENTER();
    if (!ts_data->suspended) {
        FTS_DEBUG("Already in awake state");
        return 0;
    }
    
    enter_gesture_mode = is_enter_gesture_mode(ts_data);
    FTS_INFO("Is enter gesture mode %d",enter_gesture_mode);
    if (ts_data->gesture_mode && (enter_gesture_mode == 1)) {
	fts_read_reg(FTS_REG_ILLEGAL_GESTURE, &reg_e6);
	FTS_INFO("Read reg status before gesture resume E6 0x%X ",reg_e6);
	msleep(2);
        ret = fts_gesture_resume(ts_data);
	if (ret < 0){
	    FTS_ERROR("fts_gesture_resume fail, set enter_gesture_mode = 0 ");
	    enter_gesture_mode = 0;
	}
    } else{
	FTS_ERROR("abnormal gesture setting, set enter_gesture_mode = 0 ");
	enter_gesture_mode = 0;
    }

    if (!ts_data->ic_info.is_incell) {
#if FTS_POWER_SOURCE_CUST_EN
        fts_power_source_resume(ts_data);
#endif
	if(!ts_data->fp_filter) {
	  fts_reset_proc(150);
	  proxy_status = proximityStatus();
	  FTS_INFO("Psensor status: %d",proxy_status);
	  if(proxy_status){
	    fts_data-> reset_base = true;
	  }else {
	    fts_data-> reset_base = false;
	  }
	} else {
	  fts_data-> wait_reset = true;
	}
    }

    fts_wait_tp_to_valid();

    fts_ex_fun_recovery(ts_data);

    FTS_INFO("TP resume ");
    if (enter_gesture_mode == 1) {
	if (disable_irq_wake(ts_data->irq)) {
	    FTS_DEBUG("disable_irq_wake(irq:%d) fail", ts_data->irq);
	}
    } else
	fts_irq_enable();

    ts_data->suspended = false;
    ts_data->finger_press=false;
    fp_press = 0;
    asus_game_recovery(ts_data);
    report_rate_recovery(ts_data);
    if (ts_data->extra_reconfig == 2)
        set_sub_noise_mode(true);
    FTS_FUNC_EXIT();
    return 0;
}

/*****************************************************************************
* TP Driver
*****************************************************************************/
static void fts_init_work(struct work_struct *work)
{
    int ret = 0;
    
    ret = fts_ts_probe_entry(fts_data);
    if (ret) {
        FTS_ERROR("Touch Screen(I2C BUS) driver probe fail");
        kfree_safe(fts_data);
//        return ret;
    }
  
}
static int fts_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    //int ret = 0;
    struct fts_ts_data *ts_data = NULL;

    FTS_INFO("Touch Screen(I2C BUS) driver prboe...");
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        FTS_ERROR("I2C not supported");
        return -ENODEV;
    }



    /* malloc memory for global struct variable */
    ts_data = (struct fts_ts_data *)kzalloc(sizeof(*ts_data), GFP_KERNEL);
    if (!ts_data) {
        FTS_ERROR("allocate memory for fts_data fail");
        return -ENOMEM;
    }

    fts_data = ts_data;
    ts_data->client = client;
    ts_data->dev = &client->dev;
    ts_data->log_level = 1;
    ts_data->fw_is_running = 0;
    ts_data->bus_type = BUS_TYPE_I2C;
    i2c_set_clientdata(client, ts_data);
    
    ts_data->ts_workqueue = create_singlethread_workqueue("fts_wq");
    if (!ts_data->ts_workqueue) {
        FTS_ERROR("create fts workqueue fail");
    }
    
    ts_data->init_workqueue = alloc_ordered_workqueue("fts_init",0);
    if (!ts_data->init_workqueue) {
        FTS_ERROR("create fts init workqueue fail");
    }
    INIT_WORK(&ts_data->init_work, fts_init_work);
    queue_work(ts_data->init_workqueue, &ts_data->init_work);

/*    ret = fts_ts_probe_entry(ts_data);
    if (ret) {
        FTS_ERROR("Touch Screen(I2C BUS) driver probe fail");
        kfree_safe(ts_data);
        return ret;
    }
*/

    //fod touch
    init_fod_touch_class_obj();

    FTS_INFO("Touch Screen(I2C BUS) driver prboe successfully");
    return 0;
}

static int fts_ts_remove(struct i2c_client *client)
{
    return fts_ts_remove_entry(i2c_get_clientdata(client));
}

static const struct i2c_device_id fts_ts_id[] = {
    {FTS_DRIVER_NAME, 0},
    {},
};
static const struct of_device_id fts_dt_match[] = {
    {.compatible = "focaltech,fts", },
    {},
};
MODULE_DEVICE_TABLE(of, fts_dt_match);

static struct i2c_driver fts_ts_driver = {
    .probe = fts_ts_probe,
    .remove = fts_ts_remove,
    .driver = {
        .name = FTS_DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(fts_dt_match),
    },
    .id_table = fts_ts_id,
};

static int __init fts_ts_init(void)
{
    int ret = 0;

    FTS_FUNC_ENTER();
    ret = i2c_add_driver(&fts_ts_driver);
    if ( ret != 0 ) {
        FTS_ERROR("Focaltech touch screen driver init failed!");
    }
    FTS_FUNC_EXIT();
    return ret;
}

static void __exit fts_ts_exit(void)
{
    i2c_del_driver(&fts_ts_driver);
}
late_initcall(fts_ts_init);
module_exit(fts_ts_exit);

MODULE_AUTHOR("FocalTech Driver Team");
MODULE_DESCRIPTION("FocalTech Touchscreen Driver");
MODULE_LICENSE("GPL v2");

MODULE_IMPORT_NS(ANDROID_GKI_VFS_EXPORT_ONLY);
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
