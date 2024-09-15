/*
 * Copyright (c) 2019-2020, The ASUS Company. All rights reserved.
 */

#define pr_fmt(fmt)	"BATTERY_CHG: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/soc/qcom/battery_charger_asus.h>

#include <linux/of_gpio.h>
//ASUS BSP +++
#include "fg-core.h"
#include <dt-bindings/iio/qcom,spmi-vadc.h>
#include <dt-bindings/iio/qcom,spmi-adc5-gen3-pm8550.h>
#include <dt-bindings/iio/qcom,spmi-adc5-gen3-pm8550b.h>
#include <linux/iio/consumer.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/delay.h>
#include <asus_chg_wakelock.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/asusdebug.h>
//ASUS BSP ---

//[+++] Add the structure for PMIC-GLINK response

#include <drm/drm_panel.h>



struct ADSP_ChargerPD_Info {
	int PlatformID;
	int BATT_ID;
	int    VBUS_SRC;//The VBUS is from side or bottom
	bool   chg_limit_en;
	u32    chg_limit_cap;
	bool   usbin_suspend_en;
    bool   charging_suspend_en;
	char   ChgPD_FW[64];
	char firmware_version[128];
	int batt_temp;
	int fg_real_soc;
	int cell1_voltage;
	int cell2_voltage;
	int TFCC;
	int TRM;
	int is_emark_cable;
	int gauge_FW_compare;
	int lt_max_cell1;
	int lt_max_cell2;
	int Gauge_ENABLE;  //calibrate
	int fg_2nd_gauge_SW_reset;
	int fg_fake_Qmax0;
	int fg_fake_Qmax1;
	int fg_fake_SFR_CC;
	u32 launchedtime;
	
};



struct evtlog_context_resp_msg3 {
	struct pmic_glink_hdr		hdr;
	u8				buf[128];
	u32				reserved;
};

struct battery_charger_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			property_id;
	u32			value;
};

struct battery_charger_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			value;
	u32			ret_code;
};

struct oem_get_platformID_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_platformID_resp {
	struct pmic_glink_hdr	hdr;
	u32    PlatID_version;
};

struct oem_get_BattID_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_BattID_resp {
	struct pmic_glink_hdr	hdr;
	u32    Batt_ID;
};

struct oem_get_VBUS_SRC_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_VBUS_SRC_resp {
	struct pmic_glink_hdr	hdr;
	u32    vbus_src;
};

struct oem_set_USB2_Client_resp {
	struct pmic_glink_hdr	hdr;
	u32    on;
};

struct oem_set_BTM_OTG_req {
	struct pmic_glink_hdr	hdr;
	u32    on;
};

struct oem_set_BTM_OTG_resp {
	struct pmic_glink_hdr	hdr;
};

struct oem_chg_limit_en_req {
	struct pmic_glink_hdr	hdr;
	u32    enable;
};

struct oem_chg_limit_en_resp {
	struct pmic_glink_hdr	hdr;
	u32    value;
};

struct oem_chg_limit_cap_req {
	struct pmic_glink_hdr	hdr;
	u32    cap;
};

struct oem_chg_limit_cap_resp {
	struct pmic_glink_hdr	hdr;
	u32    value;
};

struct oem_usbin_suspend_en_req {
	struct pmic_glink_hdr	hdr;
	u32    enable;
};

struct oem_usbin_suspend_en_resp {
	struct pmic_glink_hdr	hdr;
	u32    value;
};

struct oem_charging_suspend_en_req {
	struct pmic_glink_hdr	hdr;
	u32    enable;
};

struct oem_charging_suspend_en_resp {
	struct pmic_glink_hdr	hdr;
	u32    value;
};

struct oem_get_ChgPD_FW_Ver_resp {
	struct pmic_glink_hdr	hdr;
	char   ver[64] ;
};
struct oem_get_FW_version_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_FW_version_resp {
	struct pmic_glink_hdr	hdr;
	char    fw_version[128];
};
struct oem_get_batt_temp_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_fg_soc_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_fg_soc_scale_req {
	struct pmic_glink_hdr	hdr;
	u32    bat_scale;
};

struct oem_get_cell_voltage_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_TFCC_TRM_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_lt_max_cell_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_set_Shutdown_mode_req {
	struct pmic_glink_hdr	hdr;
	u8    shutdown_mode;
};

struct oem_get_gauge_FW_compare_req {
	struct pmic_glink_hdr	hdr;
};

struct oem_get_batt_temp_resp {
	struct pmic_glink_hdr	hdr;
	int    batt_temp;
};

struct oem_get_fg_soc_resp {
	struct pmic_glink_hdr	hdr;
	int    fg_soc;
};

struct oem_get_bat_rescale_resp {
	struct pmic_glink_hdr	hdr;
	int    bat_scale;
};

struct oem_get_cell_voltage_resp {
	struct pmic_glink_hdr	hdr;
	u16    cell1_voltage;
	u16    cell2_voltage;
};

struct oem_get_TFCC_TRM_resp {
	struct pmic_glink_hdr	hdr;
	u16    TFCC;
	u16    TRM;
};

struct oem_get_lt_max_cell_resp {
	struct pmic_glink_hdr	hdr;
	u16    lt_max_cell1;
	u16    lt_max_cell2;
};


struct oem_get_gauge_FW_compare_resp {
	struct pmic_glink_hdr	hdr;
	u16   gauge_FW_compare;
};

struct oem_set_ASUS_media_req {
	struct pmic_glink_hdr	hdr;
	u8    asus_media;
};
struct oem_set_hwid_to_ADSP_req {
	struct pmic_glink_hdr	hdr;
	u32    hwid_val;
};
//1016++
struct oem_set_fg_2nd_gauge_SW_reset_req {
	struct pmic_glink_hdr	hdr;
	u8    fg_2nd_gauge_SW_reset;
};

struct oem_set_fg_fake_Qmax0_req {
	struct pmic_glink_hdr	hdr;
	u8    fg_fake_Qmax0;
};

struct oem_set_fg_fake_Qmax1_req {
	struct pmic_glink_hdr	hdr;
	u8    fg_fake_Qmax1;
};

struct oem_set_fg_fake_SFR_CC_req {
	struct pmic_glink_hdr	hdr;
	u32    fg_fake_SFR_CC;
};
//1016--
struct oem_set_hwid_to_ADSP_resp {
	struct pmic_glink_hdr	hdr;
};

struct oem_set_SideOTG_WA_resp {
	struct pmic_glink_hdr	hdr;
	u32    enable;
};

struct oem_set_Charger_Type_resp {
	struct pmic_glink_hdr	hdr;
	u32    charger_type;
};

struct battery_charger_ship_mode_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			ship_mode_type;
};

struct oem_chg_limit_mode_req {
	struct pmic_glink_hdr	hdr;
	u32    mode;
};

struct oem_set_Gauge_EN_req {
	struct pmic_glink_hdr	hdr;
	u32    enable;
};

struct oem_chg_limit_mode_resp {
	struct pmic_glink_hdr	hdr;
	u32    value;
};

struct oem_chg_limit_mode2_req {
	struct pmic_glink_hdr	hdr;
	u32    mode;
	u32    value;
};

struct oem_evt_adsp_to_hlos_req {
	struct pmic_glink_hdr	hdr;
	u32    EvtID;
	u32    value;
};

struct oem_set_Adapter_ID_resp {
	struct pmic_glink_hdr	hdr;
	u32    adapter_id;
};

struct oem_set_side_vid_resp {
	struct pmic_glink_hdr	hdr;
	u32    side_vid;
};

struct oem_set_side_pid_resp {
	struct pmic_glink_hdr	hdr;
	u32    side_pid;
};

struct oem_set_fw_version_resp {
	struct pmic_glink_hdr	hdr;
	unsigned char    fw_version;
};

struct oem_set_fan_frozen_enable_resp {
	struct pmic_glink_hdr	hdr;
	u32    enable;
};

//Send cc reset req to rt1715
struct oem_set_bot_cc_reset_msg {
	struct pmic_glink_hdr hdr;
	u32 reset;
};

struct oem_get_cc_status_msg {
	struct pmic_glink_hdr hdr;
	u32 side_cc_status;
};

struct oem_get_is_emark_cable_msg {
	struct pmic_glink_hdr hdr;
	u32 is_emark_cable;
};

struct oem_set_full_charging_voltage {
	struct pmic_glink_hdr	hdr;
	u32    mode;
	u32    value;
};

struct oem_set_bat_safe_resp {
	struct pmic_glink_hdr	hdr;
	u32    bat_safe_info;
	u32    max_chg_vol;
};

struct oem_set_bat_record_resp {
	struct pmic_glink_hdr	hdr;
	u32    bat_record_scale;	
};

struct oem_get_bat_record_resp {
	struct pmic_glink_hdr	hdr;
	u16    bat_record_scale;
	u16    bat_cycle_count;	
};


struct extcon_asus_dev	*bat_id_extcon;
struct extcon_asus_dev	*bat_extcon;
struct extcon_asus_dev	*quickchg_extcon;
EXPORT_SYMBOL(quickchg_extcon);
struct extcon_asus_dev	*thermal_extcon;
struct extcon_asus_dev	*audio_dongle_extcon;

unsigned char g_fw_version = 0;
EXPORT_SYMBOL(g_fw_version);

u32 g_bat_safe_info = 0;
u32 g_max_chg_vol = 0;
EXPORT_SYMBOL(g_bat_safe_info);
EXPORT_SYMBOL(g_max_chg_vol);



//Define the OWNER ID
#define PMIC_GLINK_MSG_OWNER_OEM	32782
#define MSG_OWNER_BC				32778

//Define the opcode
#define OEM_ASUS_EVTLOG_IND			0x1002
#define OEM_PD_EVTLOG_IND			0x1003
#define OEM_ASUS_EVTLOG_PRINT_IND		0x1004
#define OEM_ASUS_BOT_CC_RESET_IND		0x1005
#define OME_GET_BATT_ID				0x2001
#define OEM_GET_ADSP_PLATFORM_ID	0x2002
#define OEM_GET_VBUS_SOURCE			0x2003
#define OEM_GET_CHG_LIMIT			0x2004 //OEM_SET_CHG_LIMIT = 0x2103
#define OEM_GET_CHG_LIMIT_CAP		0x2005 //OEM_SET_CHG_LIMIT_CAP = 0x2104
#define OEM_GET_USBIN_SUSPNED		0x2006 //OEM_SET_USBIN_SUSPNED = 0x2105
#define OEM_GET_ChgPD_FW_VER		0x2007
#define OEM_GET_CHARGING_SUSPNED	0x2008 //OEM_SET_CHARGING_SUSPNED = 0x2108
#define OEM_GET_CHG_LIMIT_MODE		0x2009 //OEM_SET_CHG_LIMIT_MODE = 0x2110
#define OEM_PANELONOFF_CHG_LIMIT_REQ	0x2010
#define OEM_GET_DEBUG_MASK_REQ		0x2011
#define OEM_GET_IS_EMARK_CABLE		0x2012

#define OEM_SET_USB2_CLIENT			0x2101
#define OEM_SET_BTM_OTG				0x2102
#define OEM_SET_CHG_LIMIT			0x2103
#define OEM_SET_CHG_LIMIT_CAP		0x2104
#define OEM_SET_USBIN_SUSPNED		0x2105
#define OEM_SET_HWID_TO_ADSP		0x2106
#define OEM_SET_SideOTG_WA			0x2107
#define OEM_SET_CHARGING_SUSPNED	0x2108
#define OEM_SET_CHARGER_TYPE_CHANGE	0x2109
#define OEM_SET_CHG_LIMIT_MODE		0x2110
#define OEM_SET_DEBUG_MASK_REQ		0x2111
#define OEM_EVT_ADSP_TO_HLOS_REQ	0x2112

#define OEM_OVERWRITE_I_LIMIT_REQ	0x2113
#define OEM_OVERWRITE_V_LIMIT_REQ	0x2114
#define OEM_OVERWRITE_I_STEP_REQ	0x2115
#define OEM_VIRTUAL_THERMAL_CHG_LIMIT_REQ	0x2116
#define OEM_SET_CHG_LIMIT_MODE2  		0x2117

#define OEM_SET_ADAPTER_ID_CHANGE	0x2119
#define OEM_GET_SIDEPORT_CC_STATUS_REQ	0x2120
#define OEM_SET_SIDE_VID_CHANGE	0x2121
#define OEM_SET_SIDE_PID_CHANGE	0x2122
#define OEM_SET_FW_VERSION_CHANGE	0x2123
#define OEM_SET_BAT_SAFE_INFO_CHANGE	0x2124
#define OEM_SET_BAT_RECORD_INFO_CHANGE	0x2125
#define OEM_SET_FAN_FROZEN_ENABLE_REQ	0x2126
#define OEM_SET_BAT_Gauge_ENABLE_REQ	0x2127

#define OEM_GET_FW_version			0x3001
#define OEM_GET_Batt_temp			0x3002
#define OEM_GET_FG_SoC_REQ			0x3003
#define OEM_SET_ASUS_media			0x3104
#define OEM_GET_Cell_Voltage_REQ	0x3005
#define OEM_SET_Shutdown_mode       0x3106
#define OEM_GET_Gauge_FW_Compare_REQ	0x3007
#define OEM_GET_TFCC_TRM_REQ        0x3008
#define OEM_GET_LT_Max_Cell_REQ     0x3009
#define OEM_GET_BAT_RECORD_REQ     0x3010
//1016++
#define OEM_SET_fg_2nd_gauge_SW_reset	0x3011
#define OEM_SET_fg_fake_Qmax0	0x3012
#define OEM_SET_fg_fake_Qmax1	0x3013
#define OEM_SET_fg_fake_SFR_CC	0x3014
//1016--

//Define Message Type
#define MSG_TYPE_REQ_RESP	1
#define MSG_TYPE_NOTIFICATION	2

//define OEM_SET_CHG_LIMIT_MODE
#define SET_SLOW_CHG_MODE		0x1
#define SET_SIDE_THM_ALT_MODE	0x2
#define SET_BTM_THM_ALT_MODE	0x4
#define SET_BYPASS_CHG_MODE		0x8

#define SET_18W_WA_MODE			0x20

#define AUDIO_REQ_SET_LCM_MODE	0x100

//define the event ID from ADSP to HLOS
#define ADSP_HLOS_EVT_ADUIO_INVALID 0x01
#define ADSP_HLOS_EVT_VBUS_ATTACHED 0x02
//[---] Add the structure for PMIC-GLINK response

//[+++] Add debug log
#define CHARGER_TAG "[BAT][CHG]"
#define ERROR_TAG "[ERR]"
#define CHG_DBG(...)  printk(KERN_INFO CHARGER_TAG __VA_ARGS__)
#define CHG_DBG_E(...)  printk(KERN_ERR CHARGER_TAG ERROR_TAG __VA_ARGS__)
//[---] Add debug log

//[+++] Add the global variables
#define ASUS_CHARGER_TYPE_LEVEL0 0 // For disconnection, reset to default
#define ASUS_CHARGER_TYPE_LEVEL1 1 // This is for normal 18W QC3 or PD
#define ASUS_CHARGER_TYPE_LEVEL2 2 // This is for ASUS 30W adapter
#define ASUS_CHARGER_TYPE_LEVEL3 3 // This is for ASUS 65W adapter

#define SWITCH_LEVEL4_NOT_QUICK_CHARGING    8 //Now, this is used for 65W
#define SWITCH_LEVEL4_QUICK_CHARGING        7 //Now, this is used for 65W
#define SWITCH_LEVEL3_NOT_QUICK_CHARGING    6 //EQual to SWITCH_NXP_NOT_QUICK_CHARGING(ASUS 30W)
#define SWITCH_LEVEL3_QUICK_CHARGING        5 //EQual to SWITCH_NXP_QUICK_CHARGING(ASUS 30W)
#define SWITCH_LEVEL1_NOT_QUICK_CHARGING    4 //EQual to SWITCH_QC_NOT_QUICK_CHARGING(DCP 10W)
#define SWITCH_LEVEL1_QUICK_CHARGING        3 //EQual to SWITCH_QC_QUICK_CHARGING (DCP 10W)
#define SWITCH_LEVEL2_NOT_QUICK_CHARGING    2 //EQual to SWITCH_QC_NOT_QUICK_CHARGING_PLUS (QC 18W)
#define SWITCH_LEVEL2_QUICK_CHARGING        1 //EQual to SWITCH_QC_QUICK_CHARGING_PLUS (QC 18W)
#define SWITCH_LEVEL0_DEFAULT               0 //EQual to SWITCH_QC_OTHER
int g_SWITCH_LEVEL = SWITCH_LEVEL0_DEFAULT;
EXPORT_SYMBOL(g_SWITCH_LEVEL);
struct delayed_work	asus_set_qc_state_work;
#define Side_Port_Not_Asus_VID_or_No_charger 102
#define Side_Port_Asus_VID 103
#define Bottom_Port_Not_Asus_VID_or_No_charger 100
#define Bottom_Port_Asus_VID 101
bool g_ADAPTER_ID = 0;
EXPORT_SYMBOL(g_ADAPTER_ID);
//porting - extern bool g_Charger_mode;
bool g_Charger_mode = false;
bool g_IsBootComplete = 0;
bool g_asuslib_init = 0;

struct battery_chg_dev *g_bcdev;
EXPORT_SYMBOL(g_bcdev);

struct power_supply *qti_phy_usb;
struct power_supply *qti_phy_bat;
int PMI_MUX_GPIO;
int POGO_OTG_GPIO;
struct ADSP_ChargerPD_Info ChgPD_Info;
char st_battery_name[64] = "C21P2001-T-03-0001-0.17";
//porting - extern int usb2_host_mode;
u32 chg_limit_mode;
u32 debug_mask;
bool g_vbus_plug = 0;
EXPORT_SYMBOL(g_vbus_plug);
bool power_supply_change_flag = 1;
EXPORT_SYMBOL(power_supply_change_flag);

volatile int g_ultra_cos_spec_time = 2880;
int g_charger_mode_full_time = 0;
int demo_recharge_delta = 0;
int factory_recharger_delta = 2;
bool g_vbus_first = false;
//[+++] The possible module to set usbin_suspend
bool smartchg_stop_flag = 0;
bool bypass_stop_flag = 0;
bool node_usbin_suspend_flag = 0;
bool ultra_bat_life_flag = 0;
bool g_cos_over_full_flag = 0;
bool demo_app_property_flag = 0;
bool cn_demo_app_flag = 0;
//[---] The possible module to set usbin_suspend
bool fac_chg_limit_en = 0;
int fac_chg_limit_cap = 70;
bool g_qxdm_en = false;//add to printk the WIFI hotspot & QXDM UTS event
bool g_wifi_hs_en = false;//add to printk the WIFI hotspot & QXDM UTS event
bool feature_stop_chg_flag = false;
EXPORT_SYMBOL(feature_stop_chg_flag);
bool ASUS_media_en = 0;
//int fg_soc_remap_scale_test = 0; //20220818
//int fg_init;
int fg_soc_remap_scale;
int fg_scale_flag;

//bool bFactoryChgLimit = false;
struct wake_lock cable_resume_wake_lock;
struct wake_lock cos_wa_wake_lock;
//[---] Add the global variables

//[+++] Add the PDO of source for RT1715
const u32 default_rt1715_src_caps[] = { 0x00019032 };	/* 5V, 500 mA */
int default_rt1715_src_caps_size = ARRAY_SIZE(default_rt1715_src_caps);
//[---] Add the PDO of source for RT1715

bool side_port_cc_status = false;
EXPORT_SYMBOL(side_port_cc_status);

int battery_soc_record_worker_counter=0;
bool stop_battery_soc_record_worker_flag = false;
EXPORT_SYMBOL(stop_battery_soc_record_worker_flag);
int qti_cell1_voltage =3500;
int qti_cell2_voltage =3500;
EXPORT_SYMBOL(qti_cell1_voltage);
EXPORT_SYMBOL(qti_cell2_voltage);

int qti_user_soc;
EXPORT_SYMBOL(qti_user_soc);

int bat_record_scale =95;
int adsp_bat_scale =100;
int adsp_bat_scale_1 =101;
int g_bat_cycle_count = 20;
//int g_bat_scale = 0 ;

u64 glink_write_loading = 0; //battery_chg_write count per second
EXPORT_SYMBOL(glink_write_loading);

void monitor_charging_enable(void);
int asus_set_vbus_attached_status(int value);
int asus_thermal_btm(void);
int asus_thermal_side(void);
int asus_phone_skin_thermal(void);
int skin_thermal_to_ADSP(int state);
void asus_update_thermal_result(void);
void get_vbus_status_from_ADSP(void);

//[+++] Add the external function
//extern int battery_chg_write(struct battery_chg_dev *bcdev, void *data, int len);
extern int rt_chg_get_during_swap(void);
extern bool rt_chg_check_asus_vid(void);
extern void qti_charge_notify_device_charge(void);
extern void qti_charge_notify_device_not_charge(void);
typedef void(*dwc3_role_switch_fn)(bool);
dwc3_role_switch_fn dwc3_role_switch;
extern void msm_dwc3_register_switch(dwc3_role_switch_fn *funcPtr);
extern int asus_extcon_set_state_sync(struct extcon_asus_dev *edev, int cable_state);
extern struct extcon_asus_dev *extcon_asus_dev_allocate(void);
extern int extcon_asus_dev_register(struct extcon_asus_dev *edev);

extern void Inbox_role_switch(int enable);
extern bool FANDG_PDID_detect;
extern void adapter_plug_in(int plug_in);
extern void bypass_charging_notify_callback(int enable);

//[---] Add the external function
//[CR] To reset cc pin from rt1715
extern int typec_disable_function(bool disable);
extern int typec_attached_src_reset(void);

/* //porint -
//ASUS_BSP +++ LiJen add to printk the WIFI hotspot & QXDM UTS event
#include <linux/proc_fs.h>
#define uts_status_PROC_FILE	"driver/UTSstatus"
static struct proc_dir_entry *uts_status_proc_file;
static int uts_status_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "WIFIHS:%d, QXDM:%d\n", g_wifi_hs_en, g_qxdm_en);
	return 0;
}

static ssize_t uts_status_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

	switch (val) {
	case 0:
		printk("%s: WIFI Hotspot disable\n");
		g_wifi_hs_en = false;
		break;
	case 1:
		printk("%s: WIFI Hotspot enable\n");
		g_wifi_hs_en = true;
		break;
	case 2:
		printk("%s: QXDM disable\n");
		g_qxdm_en = false;
		break; 
	case 3:
		printk("%s: QXDM enable\n");
		g_qxdm_en = true;
		break;       
	default:
		printk("%s: Invalid mode\n");
		break;
	}
    
	return len;
}

static int uts_status_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, uts_status_proc_read, NULL);
}

static const struct file_operations uts_status_fops = {
	.owner = THIS_MODULE,
    .open = uts_status_proc_open,
    .read = seq_read,
	.write = uts_status_proc_write,
    .release = single_release,
};

void static create_uts_status_proc_file(void)
{
		uts_status_proc_file = proc_create(uts_status_PROC_FILE, 0666, NULL, &uts_status_fops);

    if (uts_status_proc_file) {
		printk("create_uts_status_proc_file sucessed!\n");
    } else {
	    printk("create_uts_status_proc_file failed!\n");
    }
}
//ASUS_BSP --- LiJen add to printk the WIFI hotspot & QXDM UTS event
*/ //porting -

#define BC_WAIT_TIME_MS			3000
int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;
	//ASUS_BSP +++
	static struct timespec64  lasttime = {0,0};
	struct timespec64  mtNow;
	static int glink_write_count = 0;
	u64 glink_write_time = 0; //us
	//ASUS_BSP ---

	if (bcdev == NULL) {
		printk("[BAT][CHG] bcdev is NULL, don't send glink message\n");
		return 0;
	}
	
	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx)
		return 0;

	mutex_lock(&bcdev->rw_lock);
	//ASUS_BSP +++ get glink cmd count
	glink_write_count ++; 
	if(lasttime.tv_sec == 0 && lasttime.tv_nsec == 0){
		ktime_get_coarse_real_ts64(&lasttime);
	}
	ktime_get_coarse_real_ts64(&mtNow);
	glink_write_time = (mtNow.tv_sec - lasttime.tv_sec)*USEC_PER_SEC + (mtNow.tv_nsec - lasttime.tv_nsec)/NSEC_PER_USEC;
	glink_write_loading = ((glink_write_count*USEC_PER_SEC)/glink_write_time);
	if (glink_write_time >= 1*USEC_PER_SEC) { //1s
	    if(glink_write_loading >= 30){
    		pr_err("glink write count. <%llu,%d,%llu>", glink_write_time/USEC_PER_SEC, glink_write_count, glink_write_loading);
        }
		glink_write_count = 0;
		ktime_get_coarse_real_ts64(&lasttime);
	}
	//ASUS_BSP ---	
	reinit_completion(&bcdev->ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->rw_lock);

	return rc;
}
EXPORT_SYMBOL(battery_chg_write);

void usbin_suspend_to_ADSP(int enable);
void write_CHGLimit_value(int input);
void get_oem_batt_temp_from_ADSP(void);
void get_oem_cell_volt_from_ADSP(void);
void get_oem_TFCC_TRM_from_ADSP(void);
void get_oem_gauge_soc_from_ADSP(void);
void get_oem_lt_max_cell_from_ADSP(void);
void get_oem_bat_scale_from_ADSP(void);

//[+++] Add thermal alert adc function
bool usb_alert_side_flag = 0;
bool usb_alert_btm_flag = 0;
bool pre_usb_alert_side_flag = 0;
bool pre_usb_alert_btm_flag = 0;
bool g_once_usb_thermal_btm = 0;
EXPORT_SYMBOL(g_once_usb_thermal_btm);
bool g_once_usb_thermal_side = 0;
EXPORT_SYMBOL(g_once_usb_thermal_side);
int g_last_skin_thermal_ac_state = 0;
int bat_cur_counter = 0;
EXPORT_SYMBOL(bat_cur_counter);
int	g_chg_sts = 0;
int	g_bat_cur= 0; // mA
int	g_RM_Cal_Value = 150;
int	g_RM_Cal_Counter = 3;
int	g_RM_Cal_time = 3600;  //60 min
int battery_RM = 0 ;
int g_temp_state = 0;
int g_temp_side_state = 0;
int g_temp_btm_state = 0;
static int usb1_active=0;
static int usb2_active=0;
static struct alarm bat_alarm;
struct timespec64  last_check_time_1m;
struct notifier_block charge_notify;
struct notifier_block host_notify;
struct iio_channel *side_usb_temp_vadc_chan;
struct iio_channel *btm_usb_temp_vadc_chan;
struct iio_channel *volkey_fpc_temp_vadc_chan;
struct iio_channel *batt_therm_temp_vadc_chan;
struct delayed_work	asus_min_check_work;
struct delayed_work	power_supply_changed_delay_work;
struct delayed_work	asus_18W_workaround_work;
//[+++] Add thermal alert adc function

int g_current_charging_level =0;
//[+++]Add log to show charging status in ASUSEvtlog.txt
static char *charging_stats[] = {
	"UNKNOWN",
	"CHARGING",
	"DISCHARGING",
	"NOT_CHARGING",
	"FULL",
};
char *health_type[] = {
	"Unknown",
	"Good",
	"Overheat",
	"Dead",
	"Over voltage",
	"Unspecified failure",
	"Cold",
	"Watchdog timer expire",
	"Safety timer expire",
	"Over current",
	"Calibration required",
	"Warm",
	"Cool",
	"Hot",
};
char *qc_extcon_type[] = {
	"Other",
	"18W",
	"18W(N)",
	"10W",
	"10W(N)",
	"30W",
	"30W(N)",
	"65W",
	"65(W)",
};
struct delayed_work	asus_update_batt_status_work;
struct timespec64  last_check_time_3m;
//[---]Add log to show charging status in ASUSEvtlog.txt

#if 0
static const int battery_prop_map[BATT_PROP_MAX] = {
	[BATT_STATUS]		= POWER_SUPPLY_PROP_STATUS,
	[BATT_HEALTH]		= POWER_SUPPLY_PROP_HEALTH,
	[BATT_PRESENT]		= POWER_SUPPLY_PROP_PRESENT,
	[BATT_CHG_TYPE]		= POWER_SUPPLY_PROP_CHARGE_TYPE,
	[BATT_CAPACITY]		= POWER_SUPPLY_PROP_CAPACITY,
	[BATT_VOLT_OCV]		= POWER_SUPPLY_PROP_VOLTAGE_OCV,
	[BATT_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[BATT_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[BATT_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[BATT_CHG_CTRL_LIM]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	[BATT_CHG_CTRL_LIM_MAX]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	[BATT_TEMP]		= POWER_SUPPLY_PROP_TEMP,
	[BATT_TECHNOLOGY]	= POWER_SUPPLY_PROP_TECHNOLOGY,
	[BATT_CHG_COUNTER]	= POWER_SUPPLY_PROP_CHARGE_COUNTER,
	[BATT_CYCLE_COUNT]	= POWER_SUPPLY_PROP_CYCLE_COUNT,
	[BATT_CHG_FULL_DESIGN]	= POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	[BATT_CHG_FULL]		= POWER_SUPPLY_PROP_CHARGE_FULL,
	[BATT_MODEL_NAME]	= POWER_SUPPLY_PROP_MODEL_NAME,
	[BATT_TTF_AVG]		= POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	[BATT_TTE_AVG]		= POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	[BATT_POWER_NOW]	= POWER_SUPPLY_PROP_POWER_NOW,
	[BATT_POWER_AVG]	= POWER_SUPPLY_PROP_POWER_AVG,
};

static const int usb_prop_map[USB_PROP_MAX] = {
	[USB_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[USB_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[USB_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[USB_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[USB_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[USB_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[USB_ADAP_TYPE]		= POWER_SUPPLY_PROP_USB_TYPE,
};
#endif
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "DCP", "CDP", "ACA", "C",
	"PD", "PD_DRP", "PD_PPS", "BrickID"
};

int asus_set_invalid_audio_dongle(int src, int set);
//ASUS_BSP Panel Check +++
struct delayed_work asus_set_panelonoff_current_work;
EXPORT_SYMBOL(asus_set_panelonoff_current_work);
struct delayed_work asus_set_panelonoff_Gauge_EN_work;
EXPORT_SYMBOL(asus_set_panelonoff_Gauge_EN_work);
struct delayed_work asus_set_Gauge_EN_delay_work;
int g_drm_blank = 0;
EXPORT_SYMBOL(g_drm_blank);

int asus_set_panelonoff_charging_current_limit(u32 panelOn);
void asus_set_panelonoff_current_worker(struct work_struct *work)
{
	if (g_drm_blank == DRM_PANEL_EVENT_UNBLANK) {
		asus_set_panelonoff_charging_current_limit(true);
	} else if (g_drm_blank == DRM_PANEL_EVENT_BLANK) {
		asus_set_panelonoff_charging_current_limit(false);
	} else if (g_drm_blank == DRM_PANEL_EVENT_BLANK_LP) {
		asus_set_panelonoff_charging_current_limit(false);
	}
}

//ASUS_BSP Panel Check ---

//ASUS_BSP battery safety upgrade +++
int asus_set_full_charging_voltage(u32 mode, u32 value)
{
	struct oem_set_full_charging_voltage req_msg = { { 0 } };
	int rc;

	CHG_DBG_E("%s. mode : 0x%x, value : %d\n", __func__, mode, value);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_CHG_LIMIT_MODE2;
	req_msg.mode = mode;
	req_msg.value= value;

pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);

	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set charger_limit_mode rc=%d\n", rc);
		return rc;
	}
	return 0;
}
//ASUS_BSP battery safety upgrade ---




static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;   
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP; 
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		req_msg.property_id);
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

int asus_set_charger_limit_mode(u32 mode, u32 value)
{
	struct oem_chg_limit_mode2_req req_msg = { { 0 } };
	int rc;

	CHG_DBG("%s. mode : 0x%x, value : %d\n", __func__, mode, value);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_CHG_LIMIT_MODE2;
	req_msg.mode = mode;
	req_msg.value= value;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);

	if (mode == 0x8 && value == 1) {
		pr_err("sideway enable ");
		bypass_charging_notify_callback(1);
	}
	else if (mode == 0x8 && value == 0) {
		pr_err("sideway disable ");
		bypass_charging_notify_callback(0);
	}
	
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set charger_limit_mode rc=%d\n", rc);
		return rc;
	}
	return 0;
}
int asus_set_panelonoff_Gauge_ENABLE(int Gauge_ENABLE)
{
	struct oem_set_Gauge_EN_req req_msg = { { 0 } };
	int rc;

	CHG_DBG_E("asus_set_panelonoff_Gauge_ENABLE = %d\n", Gauge_ENABLE);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_BAT_Gauge_ENABLE_REQ;
	req_msg.enable = Gauge_ENABLE;   //true :start count 
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		CHG_DBG_E("Failed to set asus_set_panelonoff_Gauge_ENABLE rc=%d\n", rc);
		return rc;
	}
	return 0;
}
void asus_set_panelonoff_Gauge_EN_worker(struct work_struct *work)
{	
    struct timespec64  mtNow;
	static unsigned long temp_first_time = 0;
	mtNow = ktime_to_timespec64(ktime_get_boottime());   //mtNow.tv_sec;
	//within 30 min = 30*60= 1800 sec
    //g_bat_cur = mA ; g_RM_Cal_time = sec
	if (((battery_RM - ChgPD_Info.TRM) > g_RM_Cal_Value) && (abs(g_bat_cur) < 5))
		{
		if(bat_cur_counter ==0)
			{
			temp_first_time = mtNow.tv_sec;
			pr_err("%s, temp_first_time = %d\n", __func__,temp_first_time);		
			}
		bat_cur_counter++;
		pr_err("%s, mtNow.tv_sec = %d, bat_cur_counter = %d\n", __func__,mtNow.tv_sec,bat_cur_counter);
		}
	if (g_drm_blank == DRM_PANEL_EVENT_BLANK && g_chg_sts ==2 && bat_cur_counter >= g_RM_Cal_Counter && (mtNow.tv_sec - temp_first_time < g_RM_Cal_time))  //display-off 
	{
		asus_set_panelonoff_Gauge_ENABLE(1);
		bat_cur_counter = 0;
		temp_first_time = 0;
		pr_err("%s, no charging, RM - TRM = %d mA, g_bat_cur = %d\n", __func__,battery_RM - ChgPD_Info.TRM,g_bat_cur);
	}
	if (mtNow.tv_sec - temp_first_time > 1800)
		bat_cur_counter = 0;
	cancel_delayed_work(&asus_set_panelonoff_Gauge_EN_work);
    schedule_delayed_work(&asus_set_panelonoff_Gauge_EN_work, 180 * HZ);//2hour=2*60*60=7200  //180 3min
	
}
void asus_set_Gauge_EN_delay_worker(struct work_struct *work)
{	
	 pr_err("%s +++, delay 2 mins \n", __func__);
	asus_set_panelonoff_Gauge_ENABLE(1);
	cancel_delayed_work(&asus_set_Gauge_EN_delay_work);
}

int asus_set_panelonoff_charging_current_limit(u32 panelOn)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;

	CHG_DBG("panelOn= 0x%x\n", panelOn);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_PANELONOFF_CHG_LIMIT_REQ;
	req_msg.mode = panelOn;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		CHG_DBG_E("Failed to set asus_set_panelonoff_charging_current_limit rc=%d\n", rc);
		return rc;
	}
	return 0;
}
//[PM_debug+++]
//#if defined ASUS_ZS673KS_PROJECT
bool bBTM_OTG_EN; //used by qcom-hv-haptic.c
//#endif
//[PM_debug---]
//[+++]Add the PMIC-GLINK interface of external functions for USB or RT1715 drivers
int BTM_OTG_EN(bool enable)
{
	struct oem_set_BTM_OTG_req req_msg = { { 0 } };
	int rc;

    //[PM_debug+++]
//#if defined ASUS_ZS673KS_PROJECT
    bBTM_OTG_EN = enable;
//#endif
    //[PM_debug---]
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_BTM_OTG;
	req_msg.on = enable;
	CHG_DBG("%s. enable : %d", __func__, enable);
	if (g_bcdev == NULL) {
		pr_err("g_bcdev is null\n");
		return -1;
	}
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set BTM OTG rc=%d\n", rc);
		return rc;
	}
	return 0;
}
EXPORT_SYMBOL(BTM_OTG_EN);

int PASS_HWID_TO_ADSP(void) {
    struct oem_set_hwid_to_ADSP_req req_msg = { { 0 } };
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_HWID_TO_ADSP;

	req_msg.hwid_val = 0; //porting: remove later
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set HWID to ADSP rc=%d\n", rc);
		return rc;
	}
	return 0;
}
//[---]Add the PMIC-GLINK interface of external functions for USB or RT1715 drivers

static ssize_t BTM_OTG_EN1_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int rc, tmp;
	bool btm_otg_en;
	tmp = simple_strtol(buf, NULL, 10);

	CHG_DBG("%s. Set BTM_OTG_EN : %d\n", __func__, tmp);
	btm_otg_en = tmp;
	rc = BTM_OTG_EN(btm_otg_en);
	if (rc)
		pr_err("%s. Failed to control BTM_OTG_EN\n", __func__);
	return count;
}

static ssize_t BTM_OTG_EN1_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "BTM_OTG read OK\n");
}
static CLASS_ATTR_RW(BTM_OTG_EN1);

static ssize_t pmi_mux_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int rc, tmp;
	bool pmi_mux_en;
	tmp = simple_strtol(buf, NULL, 10);

	CHG_DBG("%s. Set PMI_MUX_EN : %d\n", __func__, tmp);
	pmi_mux_en = tmp;
	rc = gpio_direction_output(PMI_MUX_GPIO, pmi_mux_en);
	if (rc)
		pr_err("%s. Failed to control PMI_MUX_EN\n", __func__);
	return count;
}

static ssize_t pmi_mux_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	bool PMI_MUX_status;
	PMI_MUX_status = gpio_get_value_cansleep(PMI_MUX_GPIO);

	return scnprintf(buf, PAGE_SIZE, "PMI_MUX_EN : %d\n", PMI_MUX_status);
}
static CLASS_ATTR_RW(pmi_mux_en);

//[+++] Addd the interface for accessing the BATTERY power supply
static ssize_t asus_get_FG_SoC_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_fg_soc_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_FG_SoC_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	pr_err("[BAT][CHG]%s,RSOC = %d\n", __func__, ChgPD_Info.fg_real_soc);
	if (rc < 0) {
		pr_err("Failed to get battery SO rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.fg_real_soc);
}
static CLASS_ATTR_RO(asus_get_FG_SoC);
//++
static ssize_t asus_get_TFCC_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_TFCC_TRM_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_TFCC_TRM_REQ;

	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery TFCC rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.TFCC);
}
static CLASS_ATTR_RO(asus_get_TFCC);

static ssize_t asus_get_TRM_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_TFCC_TRM_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_TFCC_TRM_REQ;

	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery TRM rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.TRM);
}
static CLASS_ATTR_RO(asus_get_TRM);

//--
static ssize_t asus_get_cell_voltage_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_cell_voltage_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_Cell_Voltage_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery cell voltage rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "cell1 voltage=%d, cell2 voltage=%d\n", ChgPD_Info.cell1_voltage, ChgPD_Info.cell2_voltage);
}
static CLASS_ATTR_RO(asus_get_cell_voltage);

static ssize_t asus_get_gauge_FW_compare_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_gauge_FW_compare_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_Gauge_FW_Compare_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to gauge_FW_compare rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", ChgPD_Info.gauge_FW_compare == 1 ? "TRUE" : "FALSE");
}
static CLASS_ATTR_RO(asus_get_gauge_FW_compare);

static ssize_t set_ASUS_media_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_set_ASUS_media_req msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	ASUS_media_en = tmp;
	if (ASUS_media_en != 1) {
		CHG_DBG_E("%s. NO action for asus_media\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_ASUS_media;
	msg.asus_media = 1;
	pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	CHG_DBG_E("%s. set asus_media\n", __func__);
	if (rc < 0) {
		pr_err("%s. Failed to write asus_media: %d\n", rc);
		return count;
	}

	CHG_DBG("%s. Set asus_media OK\n", __func__);

	return count;
}

static ssize_t set_ASUS_media_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ASUS_media_en);
}
static CLASS_ATTR_RW(set_ASUS_media);

//1016++
static ssize_t fg_2nd_gauge_SW_reset_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_set_fg_2nd_gauge_SW_reset_req msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	if (tmp != 1) {
		CHG_DBG_E("%s. NO action for fg_2nd_gauge_SW_reset\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_fg_2nd_gauge_SW_reset;
	msg.fg_2nd_gauge_SW_reset = tmp;
	ChgPD_Info.fg_2nd_gauge_SW_reset = tmp;
	pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	CHG_DBG_E("%s. set fg_2nd_gauge_SW_reset\n", __func__);
	if (rc < 0) {
		pr_err("%s. Failed to write fg_2nd_gauge_SW_reset: %d\n", rc);
		return count;
	}

	CHG_DBG("%s. Set fg_2nd_gauge_SW_reset OK\n", __func__);
	return count;
}

static ssize_t fg_2nd_gauge_SW_reset_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.fg_2nd_gauge_SW_reset);
}
static CLASS_ATTR_RW(fg_2nd_gauge_SW_reset);



static ssize_t fg_fake_Qmax0_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_set_fg_fake_Qmax0_req msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	if (tmp < 0) {
		CHG_DBG_E("%s. NO action for fg_fake_Qmax0_store\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_fg_fake_Qmax0;
	msg.fg_fake_Qmax0 = tmp;
	ChgPD_Info.fg_fake_Qmax0 = tmp;
	pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	CHG_DBG_E("%s. set fg_fake_Qmax0\n", __func__);
	if (rc < 0) {
		pr_err("%s. Failed to write fg_fake_Qmax0: %d\n", rc);
		return count;
	}
	CHG_DBG("%s. Set fg_fake_Qmax0 OK\n", __func__);
	return count;
}

static ssize_t fg_fake_Qmax0_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.fg_fake_Qmax0);
}
static CLASS_ATTR_RW(fg_fake_Qmax0);

static ssize_t fg_fake_Qmax1_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_set_fg_fake_Qmax1_req msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	if (tmp < 0) {
		CHG_DBG_E("%s. NO action for fg_fake_Qmax1_store\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_fg_fake_Qmax1;
	msg.fg_fake_Qmax1 = tmp;
	ChgPD_Info.fg_fake_Qmax1 = tmp;
	pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	CHG_DBG_E("%s. set fg_fake_Qmax1\n", __func__);
	if (rc < 0) {
		pr_err("%s. Failed to write fg_fake_Qmax1: %d\n", rc);
		return count;
	}
	CHG_DBG("%s. Set fg_fake_Qmax1 OK\n", __func__);
	return count;
}

static ssize_t fg_fake_Qmax1_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.fg_fake_Qmax1);
}
static CLASS_ATTR_RW(fg_fake_Qmax1);

static ssize_t fg_fake_SFR_CC_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_set_fg_fake_SFR_CC_req msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	if (tmp < 0) {
		CHG_DBG_E("%s. NO action for fg_fake_SFR_CC_store\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_fg_fake_SFR_CC;
	msg.fg_fake_SFR_CC = tmp;
	ChgPD_Info.fg_fake_SFR_CC = tmp;
	pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	CHG_DBG_E("%s. set fg_fake_SFR_CC\n", __func__);
	if (rc < 0) {
		pr_err("%s. Failed to write fg_fake_SFR_CC: %d\n", rc);
		return count;
	}
	CHG_DBG("%s. Set fg_fake_SFR_CC OK\n", __func__);
	return count;
}

static ssize_t fg_fake_SFR_CC_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.fg_fake_SFR_CC);
}
static CLASS_ATTR_RW(fg_fake_SFR_CC);
//1016--


static ssize_t set_ASUS_Gauge_ENABLE_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp;

	tmp = simple_strtol(buf, NULL, 10);	
	ChgPD_Info.Gauge_ENABLE=tmp;
	if (tmp != 1) {
		CHG_DBG_E("%s. NO action for asus_media\n", __func__);
		return count;
	}
	//worker
	if(ChgPD_Info.Gauge_ENABLE==1)
		schedule_delayed_work(&asus_set_Gauge_EN_delay_work, 120 * HZ);
	else
		asus_set_panelonoff_Gauge_ENABLE(ChgPD_Info.Gauge_ENABLE);
	return count;
}

static ssize_t set_ASUS_Gauge_ENABLE_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.Gauge_ENABLE);
}
static CLASS_ATTR_RW(set_ASUS_Gauge_ENABLE);
//[---] Addd the interface for accessing the BATTERY power supply

//gague_calibrate++
static ssize_t set_RM_Cal_Value_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp;
	tmp = simple_strtol(buf, NULL, 10);
	g_RM_Cal_Value = tmp;
	CHG_DBG_E("%s. set to %d mA\n", __func__,g_RM_Cal_Value);
	return count;
}

static ssize_t set_RM_Cal_Value_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", g_RM_Cal_Value);
}
static CLASS_ATTR_RW(set_RM_Cal_Value);

static ssize_t set_RM_Cal_Counter_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp;
	tmp = simple_strtol(buf, NULL, 10);
	g_RM_Cal_Counter = tmp;
	CHG_DBG_E("%s. set to %d mA\n", __func__,g_RM_Cal_Counter);
	return count;
}

static ssize_t set_RM_Cal_Counter_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", g_RM_Cal_Counter);
}
static CLASS_ATTR_RW(set_RM_Cal_Counter);

static ssize_t set_RM_Cal_Time_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp;
	tmp = simple_strtol(buf, NULL, 10);
	g_RM_Cal_time = tmp;
	CHG_DBG_E("%s. set to %d mA\n", __func__,g_RM_Cal_time);
	return count;
}
static ssize_t set_RM_Cal_Time_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", g_RM_Cal_time);
}
static CLASS_ATTR_RW(set_RM_Cal_Time);
//gague_calibrate--

//[+++] Add the interface for accesing the inforamtion of ChargerPD on ADSP
static ssize_t asus_get_PlatformID_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_platformID_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_ADSP_PLATFORM_ID;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get PlatformID rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.PlatformID);
}
static CLASS_ATTR_RO(asus_get_PlatformID);

static ssize_t asus_get_BattID_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_BattID_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OME_GET_BATT_ID;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get BattID rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.BATT_ID);
}
static CLASS_ATTR_RO(asus_get_BattID);

static ssize_t POGO_OTG_EN_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int rc, tmp;
	bool otg_en;
	tmp = simple_strtol(buf, NULL, 10);

	CHG_DBG("%s. Set POGO_OTG_EN : %d\n", __func__, tmp);
	otg_en = tmp;
	rc = gpio_direction_output(POGO_OTG_GPIO, otg_en);
	if (rc)
		pr_err("%s. Failed to control POGO_OTG_EN\n", __func__);
	return count;
}

static ssize_t POGO_OTG_EN_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
/* porting -
	bool POGO_OTG_status;
	POGO_OTG_status = gpio_get_value_cansleep(POGO_OTG_GPIO);

	return scnprintf(buf, PAGE_SIZE, "%d\n", POGO_OTG_status);
*/ //porting -
return scnprintf(buf, PAGE_SIZE, "POGO_OTG_EN_show\n");//porting +
}
static CLASS_ATTR_RW(POGO_OTG_EN);

static ssize_t get_usb_type_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	//struct power_supply *psy;
	struct psy_state *pst;
	int rc = 0 , val = 0;

	if (g_bcdev == NULL)
		return -1;
	CHG_DBG("%s\n", __func__);
	pst = &g_bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(g_bcdev, pst, 7);//7:USB_ADAP_TYPE
	if (!rc) {
		val = pst->prop[7];//7:USB_ADAP_TYPE
		CHG_DBG("%s. val : %d\n", __func__, val);
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", power_supply_usb_type_text[val]);
}
static CLASS_ATTR_RO(get_usb_type);

static ssize_t vbus_side_btm_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_VBUS_SRC_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_VBUS_SOURCE;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get VBUS_SOURCE rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.VBUS_SRC);
}
static CLASS_ATTR_RO(vbus_side_btm);

static ssize_t charger_limit_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_en_req req_msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_CHG_LIMIT;
	req_msg.enable = (bool) tmp;
	fac_chg_limit_en = (bool) tmp;//Tranfer this feature back to kernel
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	CHG_DBG("%s. enable : %d", __func__, req_msg.enable);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set CHG_LIMIT rc=%d\n", rc);
		return rc;
	}

	return count;
}

static ssize_t charger_limit_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_chg_limit_en_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_CHG_LIMIT;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get CHG_LIMIT rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.chg_limit_en);
}
static CLASS_ATTR_RW(charger_limit_en);

static ssize_t charger_limit_cap_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_cap_req req_msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_CHG_LIMIT_CAP;
	req_msg.cap = tmp;
	fac_chg_limit_cap = tmp;//Tranfer this feature back to kernel
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	CHG_DBG("%s. cap : %d", __func__, req_msg.cap);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set CHG_LIMIT_CAP rc=%d\n", rc);
		return rc;
	}

	return count;
}

static ssize_t charger_limit_cap_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_chg_limit_cap_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_CHG_LIMIT_CAP;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get CHG_LIMIT_CAP rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.chg_limit_cap);
}
static CLASS_ATTR_RW(charger_limit_cap);

static ssize_t usbin_suspend_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 1)
		node_usbin_suspend_flag = 1;
	else
		node_usbin_suspend_flag = 0;

	CHG_DBG("%s. node_usbin_suspend_flag : %d", __func__, tmp);
	monitor_charging_enable();

	return count;
}

static ssize_t usbin_suspend_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_usbin_suspend_en_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_USBIN_SUSPNED;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get USBIN_SUSPEND_EN rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.usbin_suspend_en);
}
static CLASS_ATTR_RW(usbin_suspend_en);

static ssize_t charging_suspend_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_charging_suspend_en_req req_msg = { { 0 } };
	int rc, tmp;

	tmp = simple_strtol(buf, NULL, 10);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_CHARGING_SUSPNED;
	req_msg.enable = tmp;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	CHG_DBG("%s. enable : %d", __func__, req_msg.enable);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set CHARGING_SUSPEND_EN rc=%d\n", rc);
		return rc;
	}

	return count;
}

static ssize_t charging_suspend_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_charging_suspend_en_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_CHARGING_SUSPNED;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get CHARGING_SUSPEND_EN rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.charging_suspend_en);
}
static CLASS_ATTR_RW(charging_suspend_en);

static ssize_t get_ChgPD_FW_Ver_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_ChgPD_FW_Ver_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_ChgPD_FW_VER;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("%s. Failed to get ChgPD_FW_Ver rc=%d\n", __func__, rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", ChgPD_Info.ChgPD_FW);
}
static CLASS_ATTR_RO(get_ChgPD_FW_Ver);
int asus_get_Batt_ID(void)
{
	struct oem_get_BattID_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OME_GET_BATT_ID;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get BattID rc=%d\n", rc);
		return rc;
	}
	return 0;
}
static ssize_t asus_get_fw_version_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_FW_version_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_FW_version;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get FW_version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", ChgPD_Info.firmware_version);
}
static CLASS_ATTR_RO(asus_get_fw_version);
static ssize_t asus_get_batt_temp_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct oem_get_batt_temp_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_Batt_temp;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get FW_version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.batt_temp);
}
static CLASS_ATTR_RO(asus_get_batt_temp);
//[---] Add the interface for accesing the inforamtion of ChargerPD on ADSP

//fg_soc_remap_scale++
static ssize_t fg_soc_remap_scale_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	//struct oem_set_ASUS_media_req msg = { { 0 } };
	//int rc, tmp;
	int tmp;
	tmp = simple_strtol(buf, NULL, 10);
	
	//fg_soc_remap_scale = tmp;
	adsp_bat_scale_1= tmp;
	
	/*
	if (ASUS_media_en != 1) {
		CHG_DBG("%s. NO action for asus_media\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_ASUS_media;
	msg.asus_media = 1;

	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	if (rc < 0) {
		pr_err("%s. Failed to write asus_media: %d\n", rc);
		return count;
	}
	*/
	//CHG_DBG_E("Set fg_soc_remap_scale = %d  OK\n",fg_soc_remap_scale);
	CHG_DBG_E("Set adsp_bat_scale_1 = %d  OK\n",adsp_bat_scale_1);
	return count;
}

static ssize_t fg_soc_remap_scale_show(struct class *c,
					struct class_attribute *attr, char *buf)
{	//CHG_DBG_E("fg_soc_remap_scale_show, fg_soc_remap_scale = %d\n",fg_soc_remap_scale);
	CHG_DBG_E("fg_soc_remap_scale_show, adsp_bat_scale_1 = %d\n",adsp_bat_scale_1);
    return scnprintf(buf, PAGE_SIZE, "%d\n", adsp_bat_scale_1);
	//adsp_bat_scale    //fg_soc_remap_scale
}
static CLASS_ATTR_RW(fg_soc_remap_scale);
//fg_soc_remap_scale--


//[---] Add the interface for usb thermal alert temp
static ssize_t once_usb_thermal_side_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	if (g_once_usb_thermal_side)
		return sprintf(buf, "FAIL\n");
	else
		return sprintf(buf, "PASS\n");
}
static CLASS_ATTR_RO(once_usb_thermal_side);

static ssize_t once_usb_thermal_btm_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	if (g_once_usb_thermal_btm)
		return sprintf(buf, "FAIL\n");
	else
		return sprintf(buf, "PASS\n");

}
static CLASS_ATTR_RO(once_usb_thermal_btm);
//[---] Add the interface for usb thermal alert temp

static ssize_t enter_ship_mode_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };

	int rc, tmp;
	bool ship_en;
	tmp = simple_strtol(buf, NULL, 10);
	ship_en = tmp;
	if (ship_en == 0) {
		CHG_DBG("%s. NO action for SHIP mode\n", __func__);
		return count;
	}
	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = 0x36;// = BC_SHIP_MODE_REQ_SET
	msg.ship_mode_type = 0;// = SHIP_MODE_PMIC
pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	if (rc < 0)
		pr_err("%s. Failed to write SHIP mode: %d\n", rc);
	CHG_DBG("%s. Set SHIP Mode OK\n", __func__);

	return count;
}

static ssize_t enter_ship_mode_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(enter_ship_mode);


static ssize_t set_shutdown_mode_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_set_Shutdown_mode_req msg = { { 0 } };
	int rc, tmp;
	bool shutdown_en;
	
	tmp = simple_strtol(buf, NULL, 10);
	shutdown_en = tmp;
	if (shutdown_en == 0) {
		CHG_DBG("%s. NO action for shutdown mode\n", __func__);
		return count;
	}
	msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = OEM_SET_Shutdown_mode;
	msg.shutdown_mode = 1;
pr_err("opcode=0x%x call battery_chg_write ", msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &msg, sizeof(msg));
	if (rc < 0)
		pr_err("%s. Failed to write shutdown mode: %d\n", rc);
	CHG_DBG("%s. Set shutdown Mode OK\n", __func__);
	
	return count;
}
	
static ssize_t set_shutdown_mode_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(set_shutdown_mode);

static ssize_t set_debugmask_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;
	u32 mask;

	mask = (u32) simple_strtol(buf, NULL, 16);

	pr_err(" mask= 0x%x\n", mask);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_DEBUG_MASK_REQ;
	req_msg.mode = mask;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("[BAT][CHG] Failed to set set_debugmask_store rc=%d\n", rc);
		return rc;
	}
	return count;
}

static ssize_t set_debugmask_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
 	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;
	u32 mask;

	mask = (u32) simple_strtol(buf, NULL, 16);

	pr_err(" mask= 0x%x\n", mask);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_DEBUG_MASK_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("[BAT][CHG] Failed to set set_debugmask_store rc=%d\n", rc);
		return rc;
	}
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", debug_mask);
}
static CLASS_ATTR_RW(set_debugmask);

static ssize_t set_virtualthermal_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;
	u32 mask;

	mask = (u32) simple_strtol(buf, NULL, 16);

	pr_err(" mask= 0x%x\n", mask);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_VIRTUAL_THERMAL_CHG_LIMIT_REQ;
	req_msg.mode = mask;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("[BAT][CHG] Failed to set set_virtualthermal_store rc=%d\n", rc);
		return rc;
	}
	return count;
}

static ssize_t set_virtualthermal_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(set_virtualthermal);

static ssize_t set_i_limit_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;
	u32 mask;

	mask = (u32) simple_strtol(buf, NULL, 10);

	pr_err(" set_i_limit= %d\n", mask);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OVERWRITE_I_LIMIT_REQ;
	req_msg.mode = mask;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("[BAT][CHG] Failed to set set_i_limit_store rc=%d\n", rc);
		return rc;
	}
	return count;
}

static ssize_t set_i_limit_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(set_i_limit);
static ssize_t set_v_limit_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;
	u32 mask;

	mask = (u32) simple_strtol(buf, NULL, 10);

	pr_err(" set_v_limit= %d\n", mask);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OVERWRITE_V_LIMIT_REQ;
	req_msg.mode = mask;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("[BAT][CHG] Failed to set set_v_limit_store rc=%d\n", rc);
		return rc;
	}
	return count;
}

static ssize_t set_v_limit_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(set_v_limit);

static ssize_t set_i_step_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc;
	u32 mask;

	mask = (u32) simple_strtol(buf, NULL, 10);

	pr_err(" set_i_step= %d\n", mask);
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OVERWRITE_I_STEP_REQ;
	req_msg.mode = mask;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("[BAT][CHG] Failed to set set_i_step_store rc=%d\n", rc);
		return rc;
	}
	return count;
}

static ssize_t set_i_step_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "No function\n");
}
static CLASS_ATTR_RW(set_i_step);
static ssize_t charger_limit_mode_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	u32 mode = 0, value = 0;
	sscanf(buf, "%d,%d", &mode, &value);
	asus_set_charger_limit_mode(mode, value);

	return count;
}

extern uint16_t vid_ext;
bool rt_chg_check_asus_vid(void) {
	pr_info("%s: vid_ext = 0x%04x\n", __func__, vid_ext);
	if(vid_ext==2821)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(rt_chg_check_asus_vid);

// Try to retrieve charger type
// 0: no charger
// 1: asus bottom 65W
// 2: asus bottom 30W
// 3: asus bottom others
// 4: asus side 65W
// 5: asus side 30W
// 6: asus side others
// 7: 65W
// 8: 30W
//
static ssize_t get_charging_source_type_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	if(rt_chg_check_asus_vid()) {	
	    //wait for charging enabled in adsp
	    msleep(100);
	    if(ASUS_CHARGER_TYPE_LEVEL3 == g_current_charging_level) {
		return scnprintf(buf, PAGE_SIZE, "1");
	    }
	    else if(ASUS_CHARGER_TYPE_LEVEL2 == g_current_charging_level) {
		return scnprintf(buf, PAGE_SIZE, "2");
	    }
	    else {
		return scnprintf(buf, PAGE_SIZE, "3");
            }
	}
	else {
	    if(ASUS_CHARGER_TYPE_LEVEL3 == g_current_charging_level) {
		return scnprintf(buf, PAGE_SIZE, "7");
	    }
	    else if(ASUS_CHARGER_TYPE_LEVEL2 == g_current_charging_level) {
		return scnprintf(buf, PAGE_SIZE, "8");
	    }
	    else {
		return scnprintf(buf, PAGE_SIZE, "0");
            }
	}
}
static CLASS_ATTR_RO(get_charging_source_type);

static ssize_t charger_limit_mode_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "No function\n");
	//Do this later.
	#if 0
	struct oem_chg_limit_mode_resp req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_CHG_LIMIT_MODE;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get CHG_LIMIT_MODE rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", chg_limit_mode);
	#endif
}
static CLASS_ATTR_RW(charger_limit_mode);
static ssize_t ultra_bat_life_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 0) {
		ultra_bat_life_flag = 0;
	} else if (tmp == 1) {
		ultra_bat_life_flag = 1;
		write_CHGLimit_value(0);
	}
	CHG_DBG("%s: ultra_bat_life_flag = %d\n", __func__, ultra_bat_life_flag);
	monitor_charging_enable();
	return count;
}

static ssize_t ultra_bat_life_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ultra_bat_life_flag);
}
static CLASS_ATTR_RW(ultra_bat_life);

static ssize_t get_adapter_vid_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	bool btm_vid = 0;
	bool vid = 0;

	btm_vid = rt_chg_check_asus_vid();
	vid = (g_ADAPTER_ID | btm_vid);
	return scnprintf(buf, PAGE_SIZE, "%d\n", vid);
}
static CLASS_ATTR_RO(get_adapter_vid);

static ssize_t smartchg_stop_charging_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 0) {
		smartchg_stop_flag = 0;
	} else if (tmp == 1) {
		smartchg_stop_flag = 1;
	}
	CHG_DBG("%s: smartchg_stop_flag : %d\n", __func__, smartchg_stop_flag);
	monitor_charging_enable();
	return count;
}

static ssize_t smartchg_stop_charging_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", smartchg_stop_flag);
}
static CLASS_ATTR_RW(smartchg_stop_charging);

static ssize_t bypass_stop_charging_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 0) {
		bypass_stop_flag = 0;
	} else if (tmp == 1) {
		bypass_stop_flag = 1;
	}
	CHG_DBG("%s: bypass_stop_flag : %d\n", __func__, bypass_stop_flag);
	//asus_set_charger_limit_mode(SET_BYPASS_CHG_MODE, bypass_stop_flag);
	monitor_charging_enable();
	return count;
}

static ssize_t bypass_stop_charging_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", bypass_stop_flag);
}
static CLASS_ATTR_RW(bypass_stop_charging);

static ssize_t demo_app_property_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 0) {
		demo_app_property_flag = 0;
	} else if (tmp == 1) {
		demo_app_property_flag = 1;
	}
	CHG_DBG("%s: demo_app_property_flag : %d\n", __func__, demo_app_property_flag);
	monitor_charging_enable();
	return count;
}
static ssize_t demo_app_property_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", demo_app_property_flag);
}
static CLASS_ATTR_RW(demo_app_property);

static ssize_t cn_demo_app_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 0) {
		cn_demo_app_flag = 0;
	} else if (tmp == 1) {
		cn_demo_app_flag = 1;
	}
	CHG_DBG("%s: cn_demo_app_flag : %d\n", __func__, cn_demo_app_flag);
	monitor_charging_enable();
	return count;
}
static ssize_t cn_demo_app_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cn_demo_app_flag);
}
static CLASS_ATTR_RW(cn_demo_app);

static ssize_t boot_completed_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;
	bool btm_vid = 0;

	tmp = simple_strtol(buf, NULL, 10);
	btm_vid = rt_chg_check_asus_vid();

	g_IsBootComplete = tmp;
	CHG_DBG("%s: g_IsBootComplete : %d, g_SWITCH_LEVEL : %d, g_ADAPTER_ID : %d, btm_vid : %d, vbus_src : %d", __func__, g_IsBootComplete, g_SWITCH_LEVEL, g_ADAPTER_ID, btm_vid, ChgPD_Info.VBUS_SRC);
	msleep(10);

	if (g_ADAPTER_ID == true)
		asus_extcon_set_state_sync(quickchg_extcon, Side_Port_Asus_VID);
	else if (btm_vid == true)
		asus_extcon_set_state_sync(quickchg_extcon, Bottom_Port_Asus_VID);
	else if (ChgPD_Info.VBUS_SRC == 2)
		asus_extcon_set_state_sync(quickchg_extcon, Side_Port_Not_Asus_VID_or_No_charger);
	else if (ChgPD_Info.VBUS_SRC == 1)
		asus_extcon_set_state_sync(quickchg_extcon, Bottom_Port_Not_Asus_VID_or_No_charger);

	asus_extcon_set_state_sync(quickchg_extcon, g_SWITCH_LEVEL);
	return count;
}
static ssize_t boot_completed_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_IsBootComplete);
}
static CLASS_ATTR_RW(boot_completed);
EXPORT_SYMBOL(g_IsBootComplete);

bool fix_time = false;
static ssize_t launchedtime_store(struct class *c,
                    struct class_attribute *attr,
                    const char *buf, size_t count)	
{
    u32 tmp;
    tmp = simple_strtol(buf, NULL, 10);
    ChgPD_Info.launchedtime = tmp/1000;
    fix_time = true;
    CHG_DBG("%s. set launched time : %d sec", __func__, ChgPD_Info.launchedtime);
	
    return count;	
}
static ssize_t launchedtime_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%ld\n", ChgPD_Info.launchedtime);
}
static CLASS_ATTR_RW(launchedtime);




static ssize_t enter_cos_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int tmp = 0;

	tmp = simple_strtol(buf, NULL, 10);

	if (tmp == 0) {
		g_Charger_mode = false;
	} else if (tmp == 1) {
		g_Charger_mode = true;
	}
	CHG_DBG("%s: g_Charger_mode : %d\n", __func__, g_Charger_mode);

 	if (g_Charger_mode) {
 		//In post-cs10, something blocks the device in the beginning.
 		//and it will be late to detect cable-out, so make a wakelock to avoid this duration.
 		wake_lock_timeout(&cos_wa_wake_lock, msecs_to_jiffies(60000));
 	}    
	return count;
}
static ssize_t enter_cos_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_Charger_mode);
}
static CLASS_ATTR_RW(enter_cos);
EXPORT_SYMBOL(g_Charger_mode);

static ssize_t get_emark_cable_show(struct class *c,
					struct class_attribute *attr, char *buf)
{

	struct oem_get_is_emark_cable_msg req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_IS_EMARK_CABLE;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get emark cable rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ChgPD_Info.is_emark_cable);
}
static CLASS_ATTR_RO(get_emark_cable);

static struct attribute *asuslib_class_attrs[] = {
	&class_attr_BTM_OTG_EN1.attr,
	&class_attr_pmi_mux_en.attr,
	&class_attr_asus_get_FG_SoC.attr,
	&class_attr_asus_get_cell_voltage.attr,
	&class_attr_asus_get_gauge_FW_compare.attr,
	&class_attr_asus_get_PlatformID.attr,
	&class_attr_asus_get_BattID.attr,
	&class_attr_POGO_OTG_EN.attr,
	&class_attr_get_usb_type.attr,
	&class_attr_vbus_side_btm.attr,
	&class_attr_charger_limit_en.attr,
	&class_attr_charger_limit_cap.attr,
	&class_attr_usbin_suspend_en.attr,
	&class_attr_charging_suspend_en.attr,
	&class_attr_get_ChgPD_FW_Ver.attr,
	&class_attr_asus_get_fw_version.attr,
	&class_attr_asus_get_batt_temp.attr,
	&class_attr_once_usb_thermal_side.attr,
	&class_attr_once_usb_thermal_btm.attr,
	&class_attr_enter_ship_mode.attr,
	&class_attr_get_charging_source_type.attr,
	&class_attr_set_shutdown_mode.attr,
	&class_attr_charger_limit_mode.attr,
	&class_attr_set_debugmask.attr,
	&class_attr_set_i_limit.attr,
	&class_attr_set_v_limit.attr,
	&class_attr_set_i_step.attr,
	&class_attr_set_virtualthermal.attr,
	&class_attr_set_ASUS_media.attr,
	&class_attr_ultra_bat_life.attr,
	&class_attr_get_adapter_vid.attr,
	&class_attr_smartchg_stop_charging.attr,
	&class_attr_demo_app_property.attr,
	&class_attr_cn_demo_app.attr,
	&class_attr_bypass_stop_charging.attr,
	&class_attr_boot_completed.attr,
	&class_attr_enter_cos.attr,
	&class_attr_get_emark_cable.attr,
	&class_attr_launchedtime.attr,
	&class_attr_fg_soc_remap_scale.attr,
	&class_attr_asus_get_TFCC.attr, 
	&class_attr_asus_get_TRM.attr, 
	&class_attr_set_ASUS_Gauge_ENABLE.attr,
	&class_attr_set_RM_Cal_Value.attr,
	&class_attr_set_RM_Cal_Counter.attr,
	&class_attr_set_RM_Cal_Time.attr,
	&class_attr_fg_2nd_gauge_SW_reset.attr, 
	&class_attr_fg_fake_Qmax0.attr, 
	&class_attr_fg_fake_Qmax1.attr, 
	&class_attr_fg_fake_SFR_CC.attr, 
	NULL,
};
ATTRIBUTE_GROUPS(asuslib_class);

struct class asuslib_class = {
	.name = "asuslib",
	.class_groups = asuslib_class_groups,
};

int asus_init_power_supply_prop(void) {

	// Initialize the power supply for usb properties
	if (!qti_phy_usb)
		qti_phy_usb = power_supply_get_by_name("usb");

	if (!qti_phy_usb) {
		pr_err("Failed to get usb power supply, rc=%d\n");
		return -ENODEV;
	}

	// Initialize the power supply for battery properties
	if (!qti_phy_bat)
		qti_phy_bat = power_supply_get_by_name("battery");

	if (!qti_phy_bat) {
		pr_err("Failed to get battery power supply, rc=%d\n");
		return -ENODEV;
	}
	return 0;
};

void handle_bot_cc_reset(u32 reset)
{
	pr_err("[CHG] handle bot cc reset=%d", reset);
	
	typec_disable_function(1);
	msleep(100);
	typec_disable_function(0);
	
	return;
}



int SET_BAT_SAFE_INFO_TO_ADSP(u32 mode,u32 max_chg_vol) {
    struct oem_set_bat_safe_resp req_msg = { { 0 } };
	int rc = 0;
 
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_BAT_SAFE_INFO_CHANGE;
	req_msg.bat_safe_info = mode; //porting: remove later
	req_msg.max_chg_vol = max_chg_vol;
	CHG_DBG_E("[BAT][CHG]%s  sizeof(req_msg) =%d\n", __func__, sizeof(req_msg));
    pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	CHG_DBG_E("[BAT][CHG]%s  bat_safe_info =%d; max_chg_vol=%d\n", __func__, req_msg.bat_safe_info, req_msg.max_chg_vol);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set BAT SAFE MODE to ADSP rc=%d\n", rc);
		return rc;
	}
	return 0;
}

int SET_BAT_RECORD_INFO_TO_ADSP(u32 scale) {
    struct oem_set_bat_record_resp req_msg = { { 0 } };
	int rc = 0;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_BAT_RECORD_INFO_CHANGE;
	req_msg.bat_record_scale = scale; //porting: remove later	
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	CHG_DBG_E("SET FROM KERENL TO ADSP [KERNEL], bat_scale =%d\n", req_msg.bat_record_scale);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to set BAT RECORD to ADSP rc=%d\n", rc);
		return rc;
	}
	return 0;
}

//--


static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct evtlog_context_resp_msg3	*evtlog_msg;
	struct oem_set_USB2_Client_resp *usb2_client_msg;
	struct oem_set_SideOTG_WA_resp *SideOTG_WA_msg;
	struct oem_set_Charger_Type_resp *Update_charger_type_msg;
	struct oem_evt_adsp_to_hlos_req *evt_adsp_to_hlos_msg;
	struct oem_set_Adapter_ID_resp *Update_adapter_id_msg;
	struct oem_set_side_vid_resp *Update_side_vid_msg;
	struct oem_set_side_pid_resp *Update_side_pid_msg;
	struct oem_set_fw_version_resp *Update_fw_version_msg;
	struct oem_set_bot_cc_reset_msg *bot_cc_reset_msg;
	struct oem_set_fan_frozen_enable_resp *fan_frozen_enable_msg;
	//struct oem_set_bat_safe_resp *Update_bat_safe_msg;  //no use
	struct pmic_glink_hdr *hdr = data;
	int rc;
	static int pre_chg_type = 0;
	static u32 pre_adapter_id = -1;
	static u32 pre_side_vid = 0;
	static u32 side_pid = 0;
	

    switch(hdr->opcode) {
    case OEM_ASUS_EVTLOG_PRINT_IND:
        if (len == sizeof(*evtlog_msg)) {
            evtlog_msg = data;
            pr_err("[adsp] evtlog= %s\n", evtlog_msg->buf);
            ASUSEvtlog("[BAT][ADSP]%s", evtlog_msg->buf);
        }
        break;
    case OEM_ASUS_EVTLOG_IND:
        if (len == sizeof(*evtlog_msg)) {
            evtlog_msg = data;
            pr_err("[adsp] evtlog= %s\n", evtlog_msg->buf);
        }
        break;
    case OEM_PD_EVTLOG_IND:
        if (len == sizeof(*evtlog_msg)) {
            evtlog_msg = data;
            pr_err("[PD] %s\n", evtlog_msg->buf);
        }
        break;
    case OEM_ASUS_BOT_CC_RESET_IND:
			pr_err("[adsp] OEM_BOT_CC_RESET_Ind\n");

			if (rt_chg_get_during_swap()) {
				CHG_DBG("Skip to send bot cc reset because swap\n");
    	} else {
				if(len == sizeof(*bot_cc_reset_msg)) {
	  	  	bot_cc_reset_msg = data;
	    		handle_bot_cc_reset(bot_cc_reset_msg->reset);
				}
			}
		break;
    case OEM_SET_USB2_CLIENT:
        if (len == sizeof(*usb2_client_msg)) {
            usb2_client_msg = data;
             CHG_DBG("%s OEM_SET_USB2_CLIENT. enable : %d\n", __func__, usb2_client_msg->on);
            if (dwc3_role_switch != NULL) {
                if (usb2_client_msg->on == 0 && rt_chg_get_during_swap())
                    CHG_DBG("Skip to send dwc3_role_switch because swap\n");
                else
                    dwc3_role_switch(usb2_client_msg->on);
            } else {
                CHG_DBG_E("%s OEM_SET_USB2_CLIENT. dwc3_role_switch = NULL\n", __func__);
            }
        } else {
            pr_err("Incorrect response length %zu for ome_set_USB2_client\n",
                len);
        }
        break;
    case OEM_SET_SideOTG_WA:
        if (len == sizeof(*SideOTG_WA_msg)) {
            SideOTG_WA_msg = data;
// porting -            CHG_DBG("%s OEM_SET_SideOTG_WA. enable : %d, HWID : %d\n", __func__, SideOTG_WA_msg->enable, g_ASUS_hwID);
// porting -            if (g_ASUS_hwID == HW_REV_SR || (g_ASUS_hwID >= HW_REV_ER2)) {
// porting -                CHG_DBG("%s. Skip to control SideOTG for SR or ER2 later\n", __func__);
// porting -                break;
// porting -            }
            if (gpio_is_valid(POGO_OTG_GPIO)) {
                rc = gpio_direction_output(POGO_OTG_GPIO, SideOTG_WA_msg->enable);
                if (rc)
                    pr_err("%s. Failed to control POGO_OTG_EN\n", __func__);
            } else {
                CHG_DBG_E("%s. POGO_OTG_GPIO is invalid\n", __func__);
            }
        } else {
            pr_err("Incorrect response length %zu for OEM_SET_SideOTG_WA\n",
                len);
        }
        break;
    case OEM_SET_CHARGER_TYPE_CHANGE:
        if (len == sizeof(*Update_charger_type_msg)) {
            Update_charger_type_msg = data;
            CHG_DBG("%s OEM_SET_CHARGER_TYPE_CHANGE. new type : %d, old type : %d\n", __func__, Update_charger_type_msg->charger_type, pre_chg_type);
            if (Update_charger_type_msg->charger_type != pre_chg_type) {
                switch (Update_charger_type_msg->charger_type) {
                case ASUS_CHARGER_TYPE_LEVEL0:
                    g_SWITCH_LEVEL = SWITCH_LEVEL0_DEFAULT;
                break;
                case ASUS_CHARGER_TYPE_LEVEL1:
                    g_SWITCH_LEVEL = SWITCH_LEVEL2_QUICK_CHARGING;
                    set_qc_stat(POWER_SUPPLY_STATUS_CHARGING);
                break;
                case ASUS_CHARGER_TYPE_LEVEL2:
                    g_SWITCH_LEVEL = SWITCH_LEVEL3_QUICK_CHARGING;
                    set_qc_stat(POWER_SUPPLY_STATUS_CHARGING);
                break;
                case ASUS_CHARGER_TYPE_LEVEL3:
                    g_SWITCH_LEVEL = SWITCH_LEVEL4_QUICK_CHARGING;
                    set_qc_stat(POWER_SUPPLY_STATUS_CHARGING);
                break;
				default:
					g_SWITCH_LEVEL = SWITCH_LEVEL0_DEFAULT;
				break;
                }
                if (qti_phy_bat)
				{
					if (power_supply_change_flag==1)
					{
						power_supply_changed(qti_phy_bat);
						//pr_err("power_supply_changed_1\n");
					}
				}
					
                    
            }
            pre_chg_type = Update_charger_type_msg->charger_type;
            g_current_charging_level = Update_charger_type_msg->charger_type;
        } else {
            pr_err("Incorrect response length %zu for OEM_SET_CHARGER_TYPE_CHANGE\n",
                len);
        }
        break;
        case OEM_EVT_ADSP_TO_HLOS_REQ:
        if (len == sizeof(*evt_adsp_to_hlos_msg)) {
            evt_adsp_to_hlos_msg = data;
            CHG_DBG("%s[OEM_EVT_ADSP_TO_HLOS_REQ] EVTID : 0x%x, value : %d\n", __func__
                    ,evt_adsp_to_hlos_msg->EvtID
                    ,evt_adsp_to_hlos_msg->value);
            if (evt_adsp_to_hlos_msg->EvtID == ADSP_HLOS_EVT_ADUIO_INVALID) {
                asus_set_invalid_audio_dongle(1, evt_adsp_to_hlos_msg->value);
            } else if (evt_adsp_to_hlos_msg->EvtID == ADSP_HLOS_EVT_VBUS_ATTACHED) {
                asus_set_vbus_attached_status(evt_adsp_to_hlos_msg->value);
            }
        }
        break;
    case OEM_SET_ADAPTER_ID_CHANGE:
        if (len == sizeof(*Update_adapter_id_msg)) {
            Update_adapter_id_msg = data;
            CHG_DBG("%s OEM_SET_ADAPTER_ID_CHANGE. new type : 0x%x, old type : 0x%x\n", __func__, Update_adapter_id_msg->adapter_id, pre_adapter_id);
            if (Update_adapter_id_msg->adapter_id != pre_adapter_id) {
                if (Update_adapter_id_msg->adapter_id == 0x0b05 || (pre_side_vid==0x040b && side_pid==0x0b05)) {
                    CHG_DBG("%s. Set VID quickchg_extcon state: 103\n", __func__);
                    if (g_IsBootComplete == 1)
                        asus_extcon_set_state_sync(quickchg_extcon, Side_Port_Asus_VID);
                    g_ADAPTER_ID = true;
                } else {
                    CHG_DBG("%s. Set VID quickchg_extcon state: 102\n", __func__);
                    if (g_IsBootComplete == 1)
                        asus_extcon_set_state_sync(quickchg_extcon, Side_Port_Not_Asus_VID_or_No_charger);
                    g_ADAPTER_ID = false;
                }

                if (qti_phy_bat)                    					
				{
					if (power_supply_change_flag==1)
					{
						power_supply_changed(qti_phy_bat);
						//pr_err("power_supply_changed_2\n");
					}
				}
            }
            pre_adapter_id = Update_adapter_id_msg->adapter_id;
        } else {
			pr_err("Incorrect response length %zu for OEM_SET_ADAPTER_ID_CHANGE\n",
                len);
        }
        break;
    case OEM_SET_SIDE_VID_CHANGE:
        if (len == sizeof(*Update_side_vid_msg)) {
            Update_side_vid_msg = data;
            CHG_DBG("%s OEM_SET_SIDE_VID_CHANGE. new type : 0x%x, old type : 0x%x\n", __func__, Update_side_vid_msg->side_vid, pre_side_vid);
            if (Update_side_vid_msg->side_vid != pre_side_vid) {
                if (Update_side_vid_msg->side_vid == 0x040B && !FANDG_PDID_detect) {
					//TODO: FAN DONGLE. VPH enable by GPIO_173 INBOX_DET
					Inbox_role_switch(1);
					CHG_DBG("%s OEM_SET_SIDE_VID_CHANGE. FANDG_PDID_detect = true\n", __func__);
					FANDG_PDID_detect = true;
                } else if (FANDG_PDID_detect){
					//TODO: Not FAN DONGLE
					Inbox_role_switch(0);
					FANDG_PDID_detect = false;
					CHG_DBG("%s OEM_SET_SIDE_VID_CHANGE. FANDG_PDID_detect = false\n", __func__);
                }
                if (qti_phy_bat)                  					
				{
					if (power_supply_change_flag==1)
					{
						power_supply_changed(qti_phy_bat);
						//pr_err("power_supply_changed_3\n");
					}
				}
            }			
            pre_side_vid = Update_side_vid_msg->side_vid;
        } else {
			pr_err("Incorrect response length %zu for OEM_SET_SIDE_VID_CHANGE\n",
                len);
        }
        break;        
    case OEM_SET_SIDE_PID_CHANGE:
        if (len == sizeof(*Update_side_pid_msg)) {
            Update_side_pid_msg = data;
            CHG_DBG("%s OEM_SET_SIDE_PID_CHANGE. pid : 0x%x", __func__, Update_side_pid_msg->side_pid);
            side_pid = Update_side_pid_msg->side_pid;
        } else {
			pr_err("Incorrect response length %zu for OEM_SET_SIDE_PID_CHANGE\n",
                len);
        }
        break;
    case OEM_SET_FW_VERSION_CHANGE:
        if (len == sizeof(*Update_fw_version_msg)) {
            Update_fw_version_msg = data;
            CHG_DBG("%s OEM_SET_FW_VERSION_CHANGE. fw_version : %d", __func__, Update_fw_version_msg->fw_version);
            g_fw_version = Update_fw_version_msg->fw_version;
        } else {
			pr_err("Incorrect response length %zu for OEM_SET_FW_VERSION_CHANGE\n",
                len);
        }
        break;
		/*
	case OEM_SET_BAT_SAFE_INFO_CHANGE:  //no use 
        if (len == sizeof(*Update_bat_safe_msg)) {
            Update_bat_safe_msg = data;
            CHG_DBG_E("%s OEM_SET_BAT_SAFE_INFO_CHANGE. bat_safe_mode : %d, max_chg_vol = %d", __func__, Update_bat_safe_msg->bat_safe_info,Update_bat_safe_msg->max_chg_vol);
            g_bat_safe_info = Update_bat_safe_msg->bat_safe_info;
			g_max_chg_vol = Update_bat_safe_msg->max_chg_vol;
        } else {
			pr_err("Incorrect response length %zu for OEM_SET_BAT_SAFE_INFO_CHANGE\n",
                len);
        }
        break;
		*/
	case OEM_SET_FAN_FROZEN_ENABLE_REQ:
        if (len == sizeof(*fan_frozen_enable_msg)) {
            fan_frozen_enable_msg = data;
            CHG_DBG("%s OEM_SET_FAN_FROZEN_ENABLE_REQ. enable : %d", __func__, fan_frozen_enable_msg->enable);
            
            if(fan_frozen_enable_msg->enable) {
				adapter_plug_in(1);
			}
			else {
				adapter_plug_in(0);
			}
        } else {
			pr_err("Incorrect response length %zu for OEM_SET_SIDE_PID_CHANGE\n",
                len);
        }
        break;
    default:
        pr_err("Unknown opcode2: %u\n", hdr->opcode);
        break;
    }

}

static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct oem_get_platformID_resp *platform_id_msg;
	struct oem_get_BattID_resp *batt_id_msg;
	struct oem_get_VBUS_SRC_resp *vbus_src_msg;
	struct oem_set_BTM_OTG_resp *btm_otg_resp_msg;
	struct oem_chg_limit_en_req *chg_limit_en_req_msg;
	struct oem_chg_limit_en_resp *chg_limit_en_resp_msg;
	struct oem_chg_limit_cap_req *chg_limit_cap_req_msg;
	struct oem_chg_limit_cap_resp *chg_limit_cap_resp_msg;
	struct oem_usbin_suspend_en_req *usbin_suspend_en_req_msg;
	struct oem_usbin_suspend_en_resp *usbin_suspend_en_resp_msg;
    struct oem_charging_suspend_en_req *charging_suspend_en_req_msg;
    struct oem_charging_suspend_en_resp *charging_suspend_en_resp_msg;
	struct oem_get_ChgPD_FW_Ver_resp *ChgPD_FW_Ver_msg;
	struct oem_set_hwid_to_ADSP_resp *set_HWID_To_ADSP_msg;
	struct oem_chg_limit_mode_req *chg_limit_mode_req_msg;
	struct oem_chg_limit_mode2_req *chg_limit_mode2_req_msg;
	struct oem_chg_limit_mode_resp *chg_limit_mode_resp_msg;
	struct pmic_glink_hdr *hdr = data;
	bool ack_set = false;
	struct oem_get_FW_version_resp *fw_version_msg;
	struct oem_get_batt_temp_resp *batt_temp_msg;
	struct oem_get_fg_soc_resp *fg_soc_msg;
	struct oem_get_cell_voltage_resp *cell_voltage_msg;
	struct oem_get_TFCC_TRM_resp *TFCC_TRM_msg;
	struct oem_get_lt_max_cell_resp *lt_max_cell_msg;
	struct oem_get_gauge_FW_compare_resp *gauge_FW_compare_msg;
	struct oem_set_ASUS_media_req *set_asus_media_msg;
	struct oem_get_cc_status_msg *get_cc_status_msg;
	struct oem_get_is_emark_cable_msg *get_is_emark_cable_msg;
	struct oem_set_Shutdown_mode_req *set_shutdown_mode_msg;
	struct oem_get_bat_record_resp *bat_record_msg;
	//1016++
	struct oem_set_fg_2nd_gauge_SW_reset_req *set_fg_2nd_gauge_SW_reset_msg;
	struct oem_set_fg_fake_Qmax0_req *set_fg_fake_Qmax0_msg;
	struct oem_set_fg_fake_Qmax1_req *set_fg_fake_Qmax1_msg;
	struct oem_set_fg_fake_SFR_CC_req *set_fg_fake_SFR_CC_msg;
	//1016--
	switch (hdr->opcode) {
	case OEM_GET_ADSP_PLATFORM_ID:
		if (len == sizeof(*platform_id_msg)) {
			platform_id_msg = data;
			ChgPD_Info.PlatformID = platform_id_msg->PlatID_version;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for asus_get_PlatformID\n",
				len);
		}
		break;
	case OME_GET_BATT_ID:
		if (len == sizeof(*batt_id_msg)) {
			batt_id_msg = data;
			ChgPD_Info.BATT_ID = batt_id_msg->Batt_ID;
			ack_set = true;
			if(ChgPD_Info.BATT_ID < 51000*1.15 && ChgPD_Info.BATT_ID > 51000*0.85)
				asus_extcon_set_state_sync(bat_id_extcon, 1);
			else if(ChgPD_Info.BATT_ID < 100000*1.15 && ChgPD_Info.BATT_ID > 100000*0.85)
				asus_extcon_set_state_sync(bat_id_extcon, 1);
			else if(ChgPD_Info.BATT_ID < 10000*1.15 && ChgPD_Info.BATT_ID > 10000*0.85)
				asus_extcon_set_state_sync(bat_id_extcon, 1);
			else
				asus_extcon_set_state_sync(bat_id_extcon, 0);
		} else {
			pr_err("Incorrect response length %zu for asus_get_BattID\n",
				len);
		}
		break;
	case OEM_GET_FW_version:
		if (len == sizeof(*fw_version_msg)) {
			fw_version_msg = data;
			strcpy(ChgPD_Info.firmware_version, fw_version_msg->fw_version);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for asus_FW_version\n",
				len);
		}
		break;
	case OEM_GET_Batt_temp:
		if (len == sizeof(*batt_temp_msg)) {
			batt_temp_msg = data;
			ChgPD_Info.batt_temp = batt_temp_msg->batt_temp;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for asus_batt_temp\n",
				len);
		}
		break;
	case OEM_GET_FG_SoC_REQ:
		if (len == sizeof(*fg_soc_msg)) {
			fg_soc_msg = data;
			ChgPD_Info.fg_real_soc = fg_soc_msg->fg_soc;
			pr_err("[BAT][CHG]%s,RSOC = %d\n", __func__, fg_soc_msg->fg_soc);	
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for asus_get_fg_soc\n",
				len);
		}
		break;
	case OEM_GET_Cell_Voltage_REQ:
		if (len == sizeof(*cell_voltage_msg)) {
			cell_voltage_msg = data;
			ChgPD_Info.cell1_voltage = cell_voltage_msg->cell1_voltage;
			ChgPD_Info.cell2_voltage = cell_voltage_msg->cell2_voltage;
			qti_cell1_voltage = cell_voltage_msg->cell1_voltage; 
			qti_cell2_voltage = cell_voltage_msg->cell2_voltage;
			ack_set = true;

		} else {
			pr_err("Incorrect response length %zu for asus_get_cell_voltage\n",
				len);
		}
		break;
	case OEM_GET_TFCC_TRM_REQ:
		if (len == sizeof(*TFCC_TRM_msg)) {
			TFCC_TRM_msg = data;
			ChgPD_Info.TFCC = TFCC_TRM_msg->TFCC;
			ChgPD_Info.TRM = TFCC_TRM_msg->TRM;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for asus_get_TFCC_TRM\n",
				len);
		}
		break;
	case OEM_GET_LT_Max_Cell_REQ:
		if (len == sizeof(*lt_max_cell_msg)) {
			lt_max_cell_msg = data;
			ChgPD_Info.lt_max_cell1 = lt_max_cell_msg->lt_max_cell1;
			ChgPD_Info.lt_max_cell2 = lt_max_cell_msg->lt_max_cell2;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for get lt_max_cell\n",
				len);
		}
		break;
	case OEM_SET_Shutdown_mode:
		if (len == sizeof(*set_shutdown_mode_msg)) {
			CHG_DBG("%s OEM_SET_Shutdown_mode successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_Shutdown_mode\n",
				len);
		}
		break;
	case OEM_GET_Gauge_FW_Compare_REQ:
		if (len == sizeof(*gauge_FW_compare_msg)) {
			gauge_FW_compare_msg = data;
			ChgPD_Info.gauge_FW_compare = gauge_FW_compare_msg->gauge_FW_compare;
			pr_err("%s. Gauge_FW_Compare : %d\n", __func__, gauge_FW_compare_msg->gauge_FW_compare);
			pr_err("%s. Gauge_FW_Compare : %d\n", __func__, ChgPD_Info.gauge_FW_compare);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for asus_get_Gauge_FW_Compare\n",
				len);
		}
		break;
		//1016++
	case OEM_SET_fg_2nd_gauge_SW_reset:
		if (len == sizeof(*set_fg_2nd_gauge_SW_reset_msg)) {
			set_fg_2nd_gauge_SW_reset_msg = data;
			ChgPD_Info.fg_2nd_gauge_SW_reset = set_fg_2nd_gauge_SW_reset_msg->fg_2nd_gauge_SW_reset;
			pr_err("%s. fg_2nd_gauge_SW_reset : %d\n", __func__, ChgPD_Info.fg_2nd_gauge_SW_reset);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for fg_2nd_gauge_SW_reset\n",len);
		}
		break;
	case OEM_SET_fg_fake_Qmax0:
		if (len == sizeof(*set_fg_fake_Qmax0_msg)) {
			set_fg_fake_Qmax0_msg = data;
			ChgPD_Info.fg_fake_Qmax0 = set_fg_fake_Qmax0_msg->fg_fake_Qmax0;
			pr_err("%s. fg_fake_Qmax0 : %d\n", __func__, ChgPD_Info.fg_fake_Qmax0);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for fg_fake_Qmax0\n",len);
		}
		break;
	case OEM_SET_fg_fake_Qmax1:
		if (len == sizeof(*set_fg_fake_Qmax1_msg)) {
			set_fg_fake_Qmax1_msg = data;
			ChgPD_Info.fg_fake_Qmax1 = set_fg_fake_Qmax1_msg->fg_fake_Qmax1;
			pr_err("%s. fg_fake_Qmax1 : %d\n", __func__, ChgPD_Info.fg_fake_Qmax1);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for fg_fake_Qmax1\n",len);
		}
		break;
	case OEM_SET_fg_fake_SFR_CC:
		if (len == sizeof(*set_fg_fake_SFR_CC_msg)) {
			set_fg_fake_SFR_CC_msg = data;
			ChgPD_Info.fg_fake_SFR_CC = set_fg_fake_SFR_CC_msg->fg_fake_SFR_CC;
			pr_err("%s. fg_fake_SFR_CC : %d\n", __func__, ChgPD_Info.fg_fake_SFR_CC);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for fg_fake_SFR_CC\n",len);
		}
		break;
		//1016--
	case OEM_SET_ASUS_media:
		if (len == sizeof(*set_asus_media_msg)) {
			CHG_DBG("%s OEM_SET_ASUS_media successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_CHG_LIMIT\n",
				len);
		}
		break;
	case OEM_GET_VBUS_SOURCE:
		if (len == sizeof(*vbus_src_msg)) {
			vbus_src_msg = data;
			ChgPD_Info.VBUS_SRC 	= vbus_src_msg->vbus_src;
			ack_set = true;
			g_vbus_plug = vbus_src_msg->vbus_src;
			CHG_DBG("%s g_vbus_plug = %d\n", __func__, g_vbus_plug);
			if (g_vbus_plug == 1)
				qti_charge_notify_device_charge();
			else
				qti_charge_notify_device_not_charge();
		} else {
			pr_err("Incorrect response length %zu for get VBUS_SRC\n",
				len);
		}
		break;
	case OEM_SET_BTM_OTG:
		if (len == sizeof(*btm_otg_resp_msg)) {
			CHG_DBG("%s OEM_SET_BTM_OTG successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_BTM_OTG\n",
				len);
		}
		break;
	case OEM_SET_CHG_LIMIT:
		if (len == sizeof(*chg_limit_en_req_msg)) {
			CHG_DBG("%s OEM_SET_CHG_LIMIT successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_CHG_LIMIT\n",
				len);
		}
		break;
	case OEM_GET_CHG_LIMIT:
		if (len == sizeof(*chg_limit_en_resp_msg)) {
			chg_limit_en_resp_msg = data;
			CHG_DBG("%s OEM_GET_CHG_LIMIT successfully\n", __func__);
			ChgPD_Info.chg_limit_en = chg_limit_en_resp_msg->value;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_CHG_LIMIT\n",
				len);
		}
		break;
	case OEM_SET_CHG_LIMIT_CAP:
		CHG_DBG("%s. OEM_SET_CHG_LIMIT_CAP. len :%d, sizeof : %d \n", __func__, len, sizeof(*chg_limit_cap_req_msg));
		if (len == sizeof(*chg_limit_cap_req_msg)) {
			CHG_DBG("%s OEM_SET_CHG_LIMIT_CAP successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_CHG_LIMIT_CAP\n",
				len);
		}
		break;
	case OEM_GET_CHG_LIMIT_CAP:
		CHG_DBG("%s. OEM_GET_CHG_LIMIT_CAP. len :%d, sizeof : %d \n", __func__, len, sizeof(*chg_limit_cap_resp_msg));
		if (len == sizeof(*chg_limit_cap_resp_msg)) {
			chg_limit_cap_resp_msg = data;
			CHG_DBG("%s OEM_GET_CHG_LIMIT_CAP successfully\n", __func__);
			ChgPD_Info.chg_limit_cap = chg_limit_cap_resp_msg->value;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_CHG_LIMIT_CAP\n",
				len);
		}
		break;
	case OEM_SET_USBIN_SUSPNED:
		if (len == sizeof(*usbin_suspend_en_req_msg)) {
			CHG_DBG("%s OEM_SET_USBIN_SUSPNED successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_USBIN_SUSPNED\n",
				len);
		}
		break;
	case OEM_GET_USBIN_SUSPNED:
		if (len == sizeof(*usbin_suspend_en_resp_msg)) {
			usbin_suspend_en_resp_msg = data;
			CHG_DBG("%s OEM_GET_USBIN_SUSPNED successfully\n", __func__);
			ChgPD_Info.usbin_suspend_en = usbin_suspend_en_resp_msg->value;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_USBIN_SUSPNED\n",
				len);
		}
		break;
    case OEM_SET_CHARGING_SUSPNED:
		if (len == sizeof(*charging_suspend_en_req_msg)) {
			CHG_DBG("%s OEM_SET_CHARGING_SUSPNED successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_CHARGING_SUSPNED\n",
				len);
		}
		break;
	case OEM_GET_CHARGING_SUSPNED:
		if (len == sizeof(*charging_suspend_en_resp_msg)) {
			charging_suspend_en_resp_msg = data;
			CHG_DBG("%s OEM_GET_CHARGING_SUSPNED successfully\n", __func__);
			ChgPD_Info.charging_suspend_en = charging_suspend_en_resp_msg->value;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_CHARGING_SUSPNED\n",
				len);
		}
		break;
	case OEM_GET_ChgPD_FW_VER:
		if (len == sizeof(*ChgPD_FW_Ver_msg)) {
			ChgPD_FW_Ver_msg = data;
			CHG_DBG("%s. ChgPD_FW : %s\n", __func__, ChgPD_FW_Ver_msg->ver);
			strcpy(ChgPD_Info.ChgPD_FW, ChgPD_FW_Ver_msg->ver);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_ChgPD_FW_VER\n",
				len);
		}
		break;
	case OEM_SET_HWID_TO_ADSP:
		if (len == sizeof(*set_HWID_To_ADSP_msg)) {
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_HWID_TO_ADSP\n",
				len);
		}
		break;
	case OEM_SET_CHG_LIMIT_MODE:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG_E("%s OEM_SET_CHG_LIMIT_MODE successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_CHG_LIMIT_MODE\n",
				len);
		}
		break;
	case OEM_SET_CHG_LIMIT_MODE2:
		if (len == sizeof(*chg_limit_mode2_req_msg)) {
			CHG_DBG("%s OEM_SET_CHG_LIMIT_MODE2 successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_CHG_LIMIT_MODE2\n",
				len);
		}
		break;
	case OEM_GET_CHG_LIMIT_MODE:
		if (len == sizeof(*chg_limit_mode_resp_msg)) {
			chg_limit_mode_resp_msg = data;
			CHG_DBG("%s OEM_GET_CHG_LIMIT_MODE successfully\n", __func__);
			chg_limit_mode = chg_limit_mode_resp_msg->value;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_CHG_LIMIT_MODE\n",
				len);
		}
		break;
	case OEM_PANELONOFF_CHG_LIMIT_REQ:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG("%s OEM_PANELONOFF_CHG_LIMIT_REQ successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_PANELONOFF_CHG_LIMIT_REQ\n",
				len);
		}
		break;
	case OEM_SET_DEBUG_MASK_REQ:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG("%s OEM_SET_DEBUG_MASK_REQ successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_SET_DEBUG_MASK_REQ\n",
				len);
		}
		break;
	case OEM_GET_DEBUG_MASK_REQ:
		if (len == sizeof(*chg_limit_mode_resp_msg)) {
			chg_limit_mode_resp_msg = data;
			CHG_DBG("%s OEM_GET_DEBUG_MASK_REQ successfully\n", __func__);
			debug_mask = chg_limit_mode_resp_msg->value;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_DEBUG_MASK_REQ\n",
				len);
		}
		break;
	case OEM_VIRTUAL_THERMAL_CHG_LIMIT_REQ:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG("%s OEM_VIRTUAL_THERMAL_CHG_LIMIT_REQ successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_VIRTUAL_THERMAL_CHG_LIMIT_REQ\n",
				len);
		}
		break;
	case OEM_OVERWRITE_I_LIMIT_REQ:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG("%s OEM_OVERWRITE_I_LIMIT_REQ successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_OVERWRITE_I_LIMIT_REQ\n",
				len);
		}
		break;
	case OEM_OVERWRITE_V_LIMIT_REQ:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG("%s OEM_OVERWRITE_V_LIMIT_REQ successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_OVERWRITE_V_LIMIT_REQ\n",
				len);
		}
		break;
	case OEM_OVERWRITE_I_STEP_REQ:
		if (len == sizeof(*chg_limit_mode_req_msg)) {
			CHG_DBG("%s OEM_OVERWRITE_I_STEP_REQ successfully\n", __func__);
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_OVERWRITE_I_STEP_REQ\n",
				len);
		}
		break;
	case OEM_GET_SIDEPORT_CC_STATUS_REQ:
		if (len == sizeof(*get_cc_status_msg)) {
			get_cc_status_msg = data;
			side_port_cc_status = get_cc_status_msg->side_cc_status;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for OEM_GET_SIDEPORT_CC_STATUS_REQ\n",
				len);
		}
		break;
	case OEM_GET_IS_EMARK_CABLE:
		if (len == sizeof(*get_is_emark_cable_msg)) {
			get_is_emark_cable_msg = data;
			ChgPD_Info.is_emark_cable = get_is_emark_cable_msg->is_emark_cable;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for get is emark_cable\n",
				len);
		}
		break;
	case OEM_GET_BAT_RECORD_REQ:
		if (len == sizeof(*bat_record_msg)) {
			bat_record_msg = data;
			adsp_bat_scale = bat_record_msg->bat_record_scale;
			g_bat_cycle_count	= bat_record_msg->bat_cycle_count;
			//CHG_DBG_E("GET FROM KERENL TO ADSP [KERNEL], adsp_bat_scale = %d, bat_cycle_count = %d\n",adsp_bat_scale,g_bat_cycle_count);
			ack_set = true;
		}
		else {
			pr_err("Incorrect response length %zu for OEM_GET_BAT_RECORD_REQ\n",
				len);
		}
		break;
	default:
		pr_err("Unknown opcode1: %u\n", hdr->opcode);
		ack_set = true;
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

static int asusBC_msg_cb(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;

	//pr_err("owner: %u type: %u opcode: %u len: %zu\n", hdr->owner, hdr->type, hdr->opcode, len);

	if (hdr->owner == PMIC_GLINK_MSG_OWNER_OEM) {
		if (hdr->type == MSG_TYPE_NOTIFICATION)
			handle_notification(g_bcdev, data, len);
		else
			handle_message(g_bcdev, data, len);
	}
	return 0;
}

static void asusBC_state_cb(void *priv, enum pmic_glink_state state)
{
	pr_err("Enter asusBC_state_cb\n");
}


int asus_set_invalid_audio_dongle(int src, int set) {
	static int side_value = 0, btm_value = 0;
	static int final_value = 0, pre_value = 0;

	if (src == 1)
		side_value = set;
	else if (src == 2)
		btm_value = set;

	final_value = side_value | btm_value;
	CHG_DBG("invalid_audio_dongle. src = %d, set = %d, final_value = %d\n", src, set, final_value);
	if (pre_value != final_value)
		asus_extcon_set_state_sync(audio_dongle_extcon, final_value);

	pre_value = final_value;

	return 0;
}
EXPORT_SYMBOL(asus_set_invalid_audio_dongle);

int asus_set_vbus_attached_status(int value) {
	//int ret = 0;
	if (value) {
		g_vbus_plug = 1;
		g_vbus_first = true;
		power_supply_change_flag = 0 ;
		ASUSEvtlog("[BAT][Ser]Cable Plug-in");
		qti_charge_notify_device_charge();
		schedule_delayed_work(&asus_min_check_work, 0);
		schedule_delayed_work(&power_supply_changed_delay_work, msecs_to_jiffies(500));
		//schedule_delayed_work(&asus_18W_workaround_work, msecs_to_jiffies(10000));
	} else {
		g_vbus_plug = 0;
		g_vbus_first = false;
		power_supply_change_flag = 1 ;
		ASUSEvtlog("[BAT][Ser]Cable Plug-out");
		qti_charge_notify_device_not_charge();
        if (g_bcdev) {
            schedule_work(&g_bcdev->usb_type_work);
        }
		//[+++]Reset the parameter for limiting the charging
		feature_stop_chg_flag = false;
		//bFactoryChgLimit = false;
		//[---]Reset the parameter for limiting the charging

		g_once_usb_thermal_side = 0;
		g_once_usb_thermal_btm = 0;

		//[+++]Update the status of thermal alert
		asus_update_thermal_result();
		//[---]Update the status of thermal alert

		//reset btm typec when btm typec is src 
		//and unpluging side usb cable
		typec_attached_src_reset();

		alarm_cancel(&bat_alarm);
		cancel_delayed_work(&asus_min_check_work);
		//cancel_delayed_work(&asus_18W_workaround_work);
		wake_unlock(&cable_resume_wake_lock);
	}
	CHG_DBG("%s: g_vbus_plug : %d \n", __func__, g_vbus_plug);
	return 0;
}

static DEFINE_SPINLOCK(bat_alarm_slock);
static enum alarmtimer_restart batAlarm_handler(struct alarm *alarm, ktime_t now)
{
	CHG_DBG("%s: batAlarm triggered\n", __func__);
	return ALARMTIMER_NORESTART;
}

int g_temp_THR_SIDE = 85000;
int g_recovery_temp_THR_SIDE = 75000;
int g_temp_THR_BTM = 70000;
int g_recovery_temp_THR_BTM = 60000;

int g_skin_temp_1 = 40000;
int g_recovery_skin_temp_1 = 38500;
int g_skin_temp_2 = 42000;
int g_recovery_skin_temp_2 = 40500;
int g_skin_temp_3 = 50000;
int g_recovery_skin_temp_3 = 45000;

int asus_thermal_side(void)
{
	int rc;
	int adc_temp;

	if (IS_ERR_OR_NULL(side_usb_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel not ready\n", __func__);
		return -ENXIO;
	}

	rc = iio_read_channel_processed(side_usb_temp_vadc_chan, &adc_temp);
	if (rc < 0)
		CHG_DBG_E("%s: iio_read_channel_processed fail\n", __func__);
	else
		CHG_DBG("%s: side_adc_temp = %d\n", __func__, adc_temp);

	if (adc_temp > g_temp_THR_SIDE && !usb_alert_side_flag) {
		usb_alert_side_flag = 1;
		g_once_usb_thermal_side = 1;
		//asus_set_charger_limit_mode(SET_SIDE_THM_ALT_MODE, 1);
	} else if (adc_temp < g_recovery_temp_THR_SIDE && usb_alert_side_flag) {
		usb_alert_side_flag = 0;
		//asus_set_charger_limit_mode(SET_SIDE_THM_ALT_MODE, 0);
	}
	return 0;
}

int asus_thermal_btm(void)
{
	int rc;
	int adc_temp;

	if (IS_ERR_OR_NULL(btm_usb_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel not ready\n", __func__);
		return -ENXIO;
	}

	rc = iio_read_channel_processed(btm_usb_temp_vadc_chan, &adc_temp);
	if (rc < 0)
		CHG_DBG_E("%s: iio_read_channel_processed fail\n", __func__);
	else
		CHG_DBG("%s: btm_adc_temp = %d\n", __func__, adc_temp);

	if (adc_temp > g_temp_THR_BTM && !usb_alert_btm_flag) {
		usb_alert_btm_flag = 1;
		g_once_usb_thermal_btm = 1;
		//asus_set_charger_limit_mode(SET_BTM_THM_ALT_MODE, 1);
	} else if (adc_temp < g_recovery_temp_THR_BTM && usb_alert_btm_flag) {
		usb_alert_btm_flag = 0;
		//asus_set_charger_limit_mode(SET_BTM_THM_ALT_MODE, 0);
	}

	return 0;
}

int asus_phone_skin_thermal(void)
{
	int rc;
	int volkey_fpc_temp;
	int batt_therm_temp;
	int skin_temp;

	if (IS_ERR_OR_NULL(volkey_fpc_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel not ready\n", __func__);
		return -ENXIO;
	}
	if (IS_ERR_OR_NULL(batt_therm_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel not ready\n", __func__);
		return -ENXIO;
	}
	// volkey_fpc_temp
	rc = iio_read_channel_processed(volkey_fpc_temp_vadc_chan, &volkey_fpc_temp);
	if (rc < 0)
		CHG_DBG_E("%s: iio_read_channel_processed fail\n", __func__);
	else
		CHG_DBG("%s: volkey_fpc_temp = %d\n", __func__, volkey_fpc_temp);

	// batt_therm_temp
	rc = iio_read_channel_processed(batt_therm_temp_vadc_chan, &batt_therm_temp);
	if (rc < 0)
		CHG_DBG_E("%s: iio_read_channel_processed fail\n", __func__);
	else
		CHG_DBG("%s: batt_therm_temp = %d\n", __func__, batt_therm_temp);

	skin_temp = (volkey_fpc_temp + batt_therm_temp) / 2;

	if ((skin_temp > g_skin_temp_1) && (g_last_skin_thermal_ac_state == 0))
		g_last_skin_thermal_ac_state = 1;
	else if ((skin_temp < g_recovery_skin_temp_1) && (g_last_skin_thermal_ac_state == 1))
		g_last_skin_thermal_ac_state = 0;

	if ((skin_temp > g_skin_temp_2) && (g_last_skin_thermal_ac_state == 1))
		g_last_skin_thermal_ac_state = 2;
	else if ((skin_temp < g_recovery_skin_temp_2) && (g_last_skin_thermal_ac_state == 2))
		g_last_skin_thermal_ac_state = 1;

	if ((skin_temp > g_skin_temp_3) && (g_last_skin_thermal_ac_state == 2))
		g_last_skin_thermal_ac_state = 3;
	else if ((skin_temp < g_recovery_skin_temp_3) && (g_last_skin_thermal_ac_state == 3))
		g_last_skin_thermal_ac_state = 2;

	CHG_DBG_E("%s: volkey_fpc_temp = %d, batt_therm_temp = %d, skin_temp = %d, g_last_skin_thermal_ac_state = %d, COS mode: %d\n"
			, __func__, volkey_fpc_temp, batt_therm_temp, skin_temp, g_last_skin_thermal_ac_state, g_Charger_mode);

	if (g_Charger_mode == true)
		skin_thermal_to_ADSP(g_last_skin_thermal_ac_state);

	return 0;
}

int skin_thermal_to_ADSP(int state)
{
	struct oem_chg_limit_mode_req req_msg = { { 0 } };
	int rc = 0;
	static int pre_state = 0;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_VIRTUAL_THERMAL_CHG_LIMIT_REQ;
	req_msg.mode = state;
	CHG_DBG_E("%s. state : %d, pre_state : %d", __func__, req_msg.mode, pre_state);

	if (req_msg.mode != pre_state) {
		pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
		rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
		if (rc < 0) {
			pr_err("[BAT][CHG] Failed to set set_virtualthermal, skin_thermal_to_ADSP rc=%d\n", rc);
			return rc;
		}
		pre_state = req_msg.mode;
	}
	return 0;
}

void asus_update_thermal_result(void)
{
	int ret = 0;
	//Update the status of thermal alert flag
	ret = asus_thermal_side();
	if (ret < 0)
		CHG_DBG_E("%s: asus_thermal_side fail\n", __func__);

	ret = asus_thermal_btm();
	if (ret < 0)
		CHG_DBG_E("%s: asus_thermal_btm fail\n", __func__);

	ret = asus_phone_skin_thermal();
	if (ret < 0)
		CHG_DBG_E("%s: asus_phone_skin_thermal fail\n", __func__);

	if (thermal_extcon == NULL) {
		CHG_DBG_E("%s: Skip to set thermal_extcon(NULL). \n", __func__);
		return;
	}

	//Update the status of thermal alert extcon
	CHG_DBG("%s: usb_alert_side_flag : %d, usb_alert_btm_flag : %d, g_vbus_plug : %d\n",
	__func__, usb_alert_side_flag, usb_alert_btm_flag, g_vbus_plug);
	if ((usb_alert_side_flag | usb_alert_btm_flag) && g_vbus_plug) {
		CHG_DBG("%s: Set thermal_extcon 2\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 2);
	} else if (usb_alert_side_flag && usb1_active) {
		CHG_DBG("%s: Set thermal_extcon 2\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 2);
	} else if (usb_alert_btm_flag && usb1_active) {
		CHG_DBG("%s: Set thermal_extcon 2\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 2);
	} else if (usb_alert_side_flag || usb_alert_btm_flag) {
		CHG_DBG("%s: Set thermal_extcon 1\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 1);
	} else {
		CHG_DBG("%s: Set thermal_extcon 0\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 0);
	}

	//Update the status of USBIN_SUSPEND
	if (usb_alert_side_flag != pre_usb_alert_side_flag)
		asus_set_charger_limit_mode(SET_SIDE_THM_ALT_MODE, usb_alert_side_flag);

	if (usb_alert_btm_flag != pre_usb_alert_btm_flag)
		asus_set_charger_limit_mode(SET_BTM_THM_ALT_MODE, usb_alert_btm_flag);
	
	pre_usb_alert_side_flag = usb_alert_side_flag;
	pre_usb_alert_btm_flag = usb_alert_btm_flag;

	return;
}

/*+++ Add demo app read ADF function +++*/
#define ADF_PATH "/ADF/ADF"
static bool ADF_check_status(void)
{
#if 0 //porting -
    char buf[32];
	struct file *fd;
	struct inode *inode;
	off_t fsize;
	loff_t pos;
	mm_segment_t old_fs;

//porting - 	if (g_Charger_mode)
//porting - 		return false;

	fd = filp_open(ADF_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fd)) {
        CHG_DBG("%s: OPEN (%s) failed\n", __func__, ADF_PATH);
		return -ENODATA;
    }

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	inode = fd->f_path.dentry->d_inode;
	fsize = inode->i_size;
	pos = 0;

	vfs_read(fd, buf, fsize, &pos);

	filp_close(fd, NULL);
	set_fs(old_fs);

	if (buf[3] == 1 || buf[3] == 2)
		return true;
	else
		return false;
#endif //porting -
return true; //porting -
}
/*--- Add demo app read ADF function--- */

void monitor_charging_enable(void)
{
	union power_supply_propval prop = {};
	int bat_capacity;
	int rc;
	bool demo_app_state_flag = 0;
	bool charging_pause = 0;
	bool feature_usbin_suspend = 0;
	static bool pre_charging_pause = 0;

	//Case 1 : For factory test, Has the highest priority to suspend USBIN
	if (node_usbin_suspend_flag) {
		CHG_DBG("%s: USBIN suspend because of node_usbin_suspend_flag = 1\n", __func__);
		usbin_suspend_to_ADSP(1);
		return;
	}

	//Get the capacity of the battery
	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (rc < 0)
		pr_err("Failed to get battery SOC, rc=%d\n", rc);
	bat_capacity = prop.intval;

	//Case 2 : For Factory test, limit the max battery SoC
	// Charger_limit_function for factory +++
	#if 0
	if (fac_chg_limit_en) {
		if (bat_capacity > fac_chg_limit_cap) {
			bFactoryChgLimit = true;
		} else if (bat_capacity <= (fac_chg_limit_cap - factory_recharger_delta)) {
			bFactoryChgLimit = false;
		}
	} else {
		bFactoryChgLimit = false;
	}
	#endif
	if (fac_chg_limit_en && bat_capacity >= fac_chg_limit_cap) {
		charging_pause = 1;
	}

	// Case 3 : Add Maximun Battery Lifespan +++
	//Check if the device reaches 48 hours in COS
 	if (g_Charger_mode) {
 		if (bat_capacity == 100 && !g_cos_over_full_flag) {
 			g_charger_mode_full_time ++;
 			if (g_charger_mode_full_time >= g_ultra_cos_spec_time) {
 				write_CHGLimit_value(1);
 				g_cos_over_full_flag = 1;
 			}
 		}
 	}
	// Add Maximun Battery Lifespan ---

	//[+++]Case 4 : For ww/cn demo app, limit the max battery SoC
	if (demo_app_property_flag)
		demo_app_state_flag = ADF_check_status();
	//[---]Case 4 : For ww/cn demo app, limit the max battery SoC

	if (ultra_bat_life_flag || g_cos_over_full_flag) {
		if (bat_capacity > 60) {
			feature_usbin_suspend = 1;
			feature_stop_chg_flag = true;
		} else if (bat_capacity >= (60 - factory_recharger_delta)) {
			feature_usbin_suspend = 0;
			feature_stop_chg_flag = true;
		} else {
			feature_usbin_suspend = 0;
			feature_stop_chg_flag = false;
		}
	} else if (cn_demo_app_flag || demo_app_state_flag) {
		if (bat_capacity > 80) {
			feature_usbin_suspend = 1;
			feature_stop_chg_flag = true;
		} else if (bat_capacity >= (80 - demo_recharge_delta)) {
			feature_usbin_suspend = 0;
			feature_stop_chg_flag = true;
		} else {
			feature_usbin_suspend = 0;
			feature_stop_chg_flag = false;
		}
	} else {
		feature_stop_chg_flag = false;
		feature_usbin_suspend = 0;
	}

	CHG_DBG("%s: smartchg_stop_flag : %d, bypass_stop_flag : %d\n"
	, __func__, smartchg_stop_flag, bypass_stop_flag);
	CHG_DBG("%s: bat_capacity : %d. ultra_bat_life_flag : %d, g_cos_over_full_flag : %d\n"
	, __func__, bat_capacity, ultra_bat_life_flag, g_cos_over_full_flag);
	CHG_DBG("%s: fac_chg_limit_en : %d, fac_chg_limit_cap : %d\n"
	, __func__, fac_chg_limit_en, fac_chg_limit_cap);
	CHG_DBG("%s: demo_app_property_flag : %d, demo_app_state_flag : %d, cn_demo_app_flag : %d\n"
	, __func__, demo_app_property_flag, demo_app_state_flag, cn_demo_app_flag);

	if (smartchg_stop_flag || bypass_stop_flag || feature_stop_chg_flag) {
		charging_pause = 1;
	}

	CHG_DBG("%s: charging_pause : %d, pre_charging_pause : %d\n", __func__, charging_pause, pre_charging_pause);
	if (charging_pause != pre_charging_pause) {
		asus_set_charger_limit_mode(SET_BYPASS_CHG_MODE, charging_pause);
		pre_charging_pause = charging_pause;
	}

	CHG_DBG("%s: g_once_usb_thermal_btm : %d, g_once_usb_thermal_side : %d, feature_usbin_suspend : %d\n",
			__func__, g_once_usb_thermal_btm, g_once_usb_thermal_side, feature_usbin_suspend);
	//Check if to set usbin_suspend to ADSP
	if (g_once_usb_thermal_btm || g_once_usb_thermal_side || feature_usbin_suspend) {
		usbin_suspend_to_ADSP(1);
	} else {
		usbin_suspend_to_ADSP(0);
	}
}

void asus_min_check_worker(struct work_struct *work)
{
	unsigned long batflags;
	struct timespec64 new_batAlarm_time;
	int RTCSetInterval = 60;
	//int rc;
	//bool thermal_extcon_state;

	if (!g_vbus_plug)
		return;

	if (!g_bcdev) {
		CHG_DBG_E("%s: driver not ready yet!\n", __func__);
		pm_relax(g_bcdev->dev);
		return;
	}

	//ASUS_BSP LiJen +++ send Side or BTM Plug-in extcon to framework 
	if(g_vbus_first && g_IsBootComplete){
		get_vbus_status_from_ADSP();	
		if(ChgPD_Info.VBUS_SRC == 2){
			CHG_DBG("%s Side Plug-in\n", __func__);
			asus_extcon_set_state_sync(quickchg_extcon, Side_Port_Not_Asus_VID_or_No_charger);
		}else if (ChgPD_Info.VBUS_SRC == 1){
			CHG_DBG("%s BTM Plug-in\n", __func__);
			asus_extcon_set_state_sync(quickchg_extcon, Bottom_Port_Not_Asus_VID_or_No_charger);
		}
		g_vbus_first = false;
	}
	//ASUS_BSP LiJen --- send Side or BTM Plug-in extcon to framework 

	//[+++]Update the thermal alert status
	#if 0
	rc = asus_thermal_side();
	if (rc < 0)
		CHG_DBG_E("%s: asus_thermal_side fail\n", __func__);

	rc = asus_thermal_btm();
	if (rc < 0)
		CHG_DBG_E("%s: asus_thermal_side fail\n", __func__);

	thermal_extcon_state = usb_alert_side_flag | usb_alert_btm_flag;
	if (g_vbus_plug && thermal_extcon_state) {
		CHG_DBG("%s: Set thermal_extcon 2\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 2);
	} else if (thermal_extcon_state) {
		CHG_DBG("%s: Set thermal_extcon 1\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 1);
	} else {
		CHG_DBG("%s: Set thermal_extcon 0\n", __func__);
		asus_extcon_set_state_sync(thermal_extcon, 0);
	}
	#endif
	asus_update_thermal_result();
	//[---]Update the thermal alert status

	ktime_get_coarse_real_ts64(&last_check_time_1m);
	new_batAlarm_time.tv_sec = last_check_time_1m.tv_sec + RTCSetInterval;

	spin_lock_irqsave(&bat_alarm_slock, batflags);
	alarm_start(&bat_alarm, timespec64_to_ktime(new_batAlarm_time));
	spin_unlock_irqrestore(&bat_alarm_slock, batflags);

	//monitor_charging_enable(jeita_charging_enable);
	monitor_charging_enable();//Always TRUE, JEITA is decided on ADSP

	schedule_delayed_work(&asus_min_check_work, msecs_to_jiffies(60000));

	pm_relax(g_bcdev->dev);
}

void asus_18W_workaround_worker(struct work_struct *work)
{
	int rc = 0;

	CHG_DBG("%s.\n", __func__);
	rc = asus_set_charger_limit_mode(SET_18W_WA_MODE, 0);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set asus_set_charger_limit_mode, rc=%d\n", __func__, rc);
}
//20230222++
void power_supply_changed_delay_worker(struct work_struct *work)
{
	if  (g_vbus_plug ==1) //attach
	{	
		power_supply_change_flag = 1 ;
		CHG_DBG_E("power_supply_changed_delay_worker, allow to do power_supply_change \n");//
	}
	cancel_delayed_work(&power_supply_changed_delay_work);
}//20230222--

//ASUS_BSP battery safety upgrade +++

static bool g_cyclecount_initialized = false;

// extern bool rtc_probe_done;
struct bat_safety_condition{
    unsigned long condition1_battery_time;
    unsigned long condition2_battery_time;
    unsigned long condition3_battery_time;
    unsigned long condition4_battery_time;
    int condition1_cycle_count;
    int condition2_cycle_count;
    unsigned long condition1_temp_vol_time;
    unsigned long condition2_temp_vol_time;
    unsigned long condition1_temp_time;
    unsigned long condition2_temp_time;
    unsigned long condition1_vol_time;
    unsigned long condition2_vol_time;
};

static struct bat_safety_condition safety_cond;

static struct CYCLE_COUNT_DATA g_cycle_count_data = {
    .magic = CYCLE_COUNT_DATA_MAGIC,
    .cycle_count=0,
    .battery_total_time = 0,
    .high_vol_total_time = 0,
    .high_temp_total_time = 0,
    .high_temp_vol_time = 0,
    .reload_condition = 0,
	.reset_flag = 0
};
struct delayed_work battery_safety_work;
struct delayed_work battery_soc_record_work;
extern unsigned long asus_qpnp_rtc_read_time(void);
extern bool rtc_probe_done;

#define Bat_SAFETY_STR_MAXLEN (256)
#define Bat_SAFETY_MAX_ITEM (32)
static struct mutex mA;

struct file_message {
    char *message;
    int read;
    int write;
};
//batinfo/bat_safety
struct file_message Batt_Safety = {
    .message = NULL,
    .read = 0,
    .write = 0
};
///batinfo/.bs
struct file_message Batt_bs = {
    .message = NULL,
    .read = 0,
    .write = 0
};
///batinfo/.bs
struct file_message Batt_percent = {
    .message = NULL,
    .read = 0,
    .write = 0
};
//sdcard/.bs
struct file_message Batt_sd_bs = {
    .message = NULL,
    .read = 0,
    .write = 0
};
//sdcard/bat_percent
struct file_message Batt_sd_percent = {
    .message = NULL,
    .read = 0,
    .write = 0
};
//ASUS_BSP battery safety upgrade ---

//ASUS_BSP battery safety upgrade +++
static void init_battery_safety(struct bat_safety_condition *cond)
{	CHG_DBG_E("%s: test \n", __func__);
    cond->condition1_battery_time = BATTERY_USE_TIME_CONDITION1;
    cond->condition2_battery_time = BATTERY_USE_TIME_CONDITION2;
    cond->condition3_battery_time = BATTERY_USE_TIME_CONDITION3;
    cond->condition4_battery_time = BATTERY_USE_TIME_CONDITION4;
    cond->condition1_cycle_count = CYCLE_COUNT_CONDITION1;
    cond->condition2_cycle_count = CYCLE_COUNT_CONDITION2;
    cond->condition1_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION1;
    cond->condition2_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION2;
    cond->condition1_temp_time = HIGH_TEMP_TIME_CONDITION1;
    cond->condition2_temp_time = HIGH_TEMP_TIME_CONDITION2;
    cond->condition1_vol_time = HIGH_VOL_TIME_CONDITION1;
    cond->condition2_vol_time = HIGH_VOL_TIME_CONDITION2;
}

static void set_full_charging_voltage(void)
{
    int rc = 0;
    int tmp = CELL_VOLT_EOC_CONDITION0;
    //pr_err("%s +++\n", __func__);

    if(0 == g_cycle_count_data.reload_condition){
        tmp = CELL_VOLT_EOC_CONDITION0;
    }else if(1 == g_cycle_count_data.reload_condition){
        tmp = CELL_VOLT_EOC_CONDITION1;
    }else if(2 == g_cycle_count_data.reload_condition){
        tmp = CELL_VOLT_EOC_CONDITION2;
    }else if(3 == g_cycle_count_data.reload_condition){
        tmp = CELL_VOLT_EOC_CONDITION3;
    }else if(4 == g_cycle_count_data.reload_condition){
        tmp = CELL_VOLT_EOC_CONDITION4;
    }

    if(0 != g_cycle_count_data.reload_condition){
        
		CHG_DBG_E("[BAT][CHG]%s. set_full_charging_voltage , max_chg_vol : %d, reload_condition : %d", __func__, tmp, g_cycle_count_data.reload_condition);
        //temporary disable
		//CHG_DBG_E("[BAT][CHG]%s. [test] don't set to adsp, max_chg_vol : %d, reload_condition : %d", __func__, tmp, g_cycle_count_data.reload_condition);
        SET_BAT_SAFE_INFO_TO_ADSP(g_cycle_count_data.reload_condition,tmp);
		//rc = oem_prop_write(BATTMAN_OEM_FV, &tmp, 1); //zen9  //need modify
		//asus_set_charger_limit_mode(mode, value);  // old
        if (rc < 0) {
            CHG_DBG_E("%s: Failed to set asus_set_charger_limit_mode, rc=%d\n", __func__, rc);
        }
    }
}

void Batinfo_WT(struct file_message *file_msg, char *buf, int length)
{
    // va_list args;
    char *buffer;
	//CHG_DBG_E("Batinfo_WT test\n");
	
    if (!in_interrupt() && !in_atomic() && !irqs_disabled()){
        mutex_lock(&mA);
    }

    buffer = &(file_msg->message)[(file_msg->write*Bat_SAFETY_STR_MAXLEN)];
    file_msg->write++;
    file_msg->write %= Bat_SAFETY_MAX_ITEM;

    if (!in_interrupt() && !in_atomic() && !irqs_disabled()){
        mutex_unlock(&mA);
    }

    memset(buffer, 0, Bat_SAFETY_STR_MAXLEN);
    if (buffer) {
        if(length > Bat_SAFETY_STR_MAXLEN)
            length = Bat_SAFETY_STR_MAXLEN;
        memcpy(buffer, buf, length);  //buf copy to buffer
    } else {
        printk("Batinfo_wr buffer cannot be allocated\n");
    }
}

static DECLARE_WAIT_QUEUE_HEAD(log_wait);

static int file_op(const char *filename, loff_t offset, char *buf, int length, int operation)
{
    int str_len = 0;
    char *pchar;
    struct file_message *file_msg;
    int readID = 0;

    if(!strcmp(filename, BAT_SAFETY_FILE_NAME))
    {
        file_msg = &Batt_Safety;
    }
    else if(!strcmp(filename, CYCLE_COUNT_FILE_NAME))
    {
        file_msg = &Batt_bs;
    }
    else if(!strcmp(filename, BAT_PERCENT_FILE_NAME))
    {
        file_msg = &Batt_percent;
    }
    else
    {
        return 0;
    }

    if(length > Bat_SAFETY_STR_MAXLEN)
        length = Bat_SAFETY_STR_MAXLEN;

    if(FILE_OP_READ == operation)
    {
        readID = file_msg->write - 1;
        if(readID < 0) return 0;
        str_len = length;
        pchar = &(file_msg->message)[(readID*Bat_SAFETY_STR_MAXLEN)];

        if (copy_to_user(buf, pchar, str_len)) {   //copy_to_user(*to,*from,unsign long)
            if (!str_len)
                str_len = -EFAULT;
        }
    }
    else if(FILE_OP_WRITE == operation) {
        Batinfo_WT(file_msg, buf, length);
        wake_up_interruptible(&log_wait);
    }

    return length;
}

static struct file_message* get_file_msg(struct file *file)
{
    struct file_message *file_msg;

    if(!strcmp(file->f_path.dentry->d_iname, "bat_safety_wr"))
    {
        file_msg = &Batt_Safety;
    }
    else if(!strcmp(file->f_path.dentry->d_iname, "bat_bs_wr"))
    {
        file_msg = &Batt_bs;
    }
    else if(!strcmp(file->f_path.dentry->d_iname, "bat_percent_wr"))
    {
        file_msg = &Batt_percent;
    }
	else if(!strcmp(file->f_path.dentry->d_iname, "bat_sd_bs_wr"))
    {
        file_msg = &Batt_sd_bs;
    }
    else if(!strcmp(file->f_path.dentry->d_iname, "bat_sd_percent_wr"))
    {
        file_msg = &Batt_sd_percent;
    }
    else
    {
        return NULL;
    }

    return file_msg;
}

static ssize_t bat_safety_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char messages[Bat_SAFETY_STR_MAXLEN];
	struct CYCLE_COUNT_DATA data_buf;
    struct file_message *file_msg;
    if (count > Bat_SAFETY_STR_MAXLEN)
            count = Bat_SAFETY_STR_MAXLEN;

    memset(messages, 0, sizeof(messages));
    //fix ++
	CHG_DBG_E("bat_safety_write++");
	CHG_DBG_E("file->f_path.dentry->d_iname = %s",file->f_path.dentry->d_iname);
    if(!strcmp(file->f_path.dentry->d_iname, "bat_bs_wr")) {

        if (copy_from_user((char*)&data_buf, buf, count)) {
			CHG_DBG_E("[BAT][CHG]%s, bat_bs_wr copy_from_user fail", __func__);
            return -EFAULT;
        }
 		CHG_DBG_E("[BAT][CHG]%s, magic:0x%x;cycle_count=%d;reload_condition=%d;high_temp_total_time=%lu;high_temp_vol_time=%lu;high_vol_total_time=%lu;battery_total_time=%lu;reset_flag=%d\n",
                __func__,data_buf.magic, data_buf.cycle_count, data_buf.reload_condition,
                data_buf.high_temp_total_time, data_buf.high_temp_vol_time,
                data_buf.high_vol_total_time, data_buf.battery_total_time, data_buf.reset_flag);

        if(data_buf.magic == CYCLE_COUNT_DATA_MAGIC) {
			CHG_DBG_E("[BAT][CHG]%s, Update current value!", __func__);
            g_cycle_count_data.cycle_count = data_buf.cycle_count;
            g_cycle_count_data.high_temp_total_time = data_buf.high_temp_total_time;
            g_cycle_count_data.high_temp_vol_time = data_buf.high_temp_vol_time;
            g_cycle_count_data.high_vol_total_time = data_buf.high_vol_total_time;
            g_cycle_count_data.reload_condition = data_buf.reload_condition;
            g_cycle_count_data.battery_total_time = data_buf.battery_total_time;
			g_cycle_count_data.reset_flag = data_buf.reset_flag;
            g_cyclecount_initialized = true;
        }

    } else {
        if (copy_from_user(messages, buf, count)) {
            CHG_DBG_E("[BAT][CHG]%s, copy_from_user fail", __func__);
            return -EFAULT;
        }   
		//fix --
		file_msg = get_file_msg(file);
		if(file_msg == NULL){
			CHG_DBG_E("[BAT][CHG]%s file_msg is NULL", __func__);
			return 0;
		}
		Batinfo_WT(file_msg, messages, count); 
	} 
    wake_up_interruptible(&log_wait);
    return count;
}


static ssize_t bat_safety_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    int str_len = 0;
    char *pchar;
    int error = 0;
    struct file_message *file_msg;
    file_msg = get_file_msg(file);

    error = wait_event_interruptible(log_wait,
                         file_msg->read != file_msg->write);
	CHG_DBG_E("bat_safety_read++");
    if(!(file_msg->read != file_msg->write)) return 0;
    mutex_lock(&mA);
    str_len = count;
    if(str_len > Bat_SAFETY_STR_MAXLEN)
        str_len = Bat_SAFETY_STR_MAXLEN;
    pchar = &(file_msg->message)[(file_msg->read*Bat_SAFETY_STR_MAXLEN)];
    file_msg->read++;
    file_msg->read %= Bat_SAFETY_MAX_ITEM;
    mutex_unlock(&mA);
	//CHG_DBG_E("pchar = %s, str_len = %d",pchar,str_len);
    if (copy_to_user(buf, pchar, str_len)) {
        if (!str_len)								
            str_len = -EFAULT;
    }

    return str_len;
}

static const struct proc_ops proc_bat_safety_operations = {
        .proc_write  = bat_safety_write,
        .proc_read   = bat_safety_read,
};

static int backup_bat_percentage(void)
{
    char buf[5]={0};
    int bat_percent = 0;
    int rc;

    if(0 == g_cycle_count_data.reload_condition){
        bat_percent = 0;
    }else if(3 == g_cycle_count_data.reload_condition){
        bat_percent = 95;
    }else if(4 == g_cycle_count_data.reload_condition){
        bat_percent = 90;
    }
    sprintf(buf, "%d\n", bat_percent);
    CHG_DBG_E("[BAT][CHG]%s bat_percent=%d; reload_condition=%d\n", __func__, bat_percent, g_cycle_count_data.reload_condition);

    rc = file_op(BAT_PERCENT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char *)&buf, sizeof(char)*5, FILE_OP_WRITE);
    if(rc<0)
        pr_err("%s:Write file:%s err!\n", __func__, BAT_PERCENT_FILE_NAME);

    return rc;
}

static int backup_bat_safety(void)
{
    char buf[120]={0};
    int rc;
	
	union power_supply_propval prop = {};

	if(g_Charger_mode == true){
		pr_err("backup_bat_safety, g_Charger_mode == true, no backup");
        return 0;
	}

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_CAPACITY, rc=%d\n", __func__, rc);
	else
		
		{//g_cycle_count_data.bat_soc= prop.intval;
		 qti_user_soc= prop.intval;
		}
	get_oem_bat_scale_from_ADSP();
		
	//g_cycle_count_data.bat_scale=adsp_bat_scale;
	adsp_bat_scale_1=adsp_bat_scale;
	CHG_DBG_E("[BAT][CHG]%s backup_bat_scale=%d\n", __func__, adsp_bat_scale_1);
	
	pr_err("backup_bat_safety,reset_flag = %d\n",g_cycle_count_data.reset_flag);
	
	if (g_cycle_count_data.reset_flag != 1)
		{	//g_cycle_count_data.battery_total_time = 0 ;
            g_cycle_count_data.high_temp_total_time = 0;
            g_cycle_count_data.high_temp_vol_time = 0;
            g_cycle_count_data.high_vol_total_time = 0;
			g_cycle_count_data.reset_flag = 1;
			pr_err("reset battery safety time record, reset_flag to 1\n");
		    ASUSEvtlog("[BAT][Ser]reset battery safety time record, reset_flag to 1");
		}
	
    sprintf(buf, "%lu,%d,%lu,%lu,%lu,%d\n",
        g_cycle_count_data.battery_total_time,
        g_cycle_count_data.cycle_count,
        g_cycle_count_data.high_temp_total_time,
        g_cycle_count_data.high_vol_total_time,
        g_cycle_count_data.high_temp_vol_time,
		g_cycle_count_data.reset_flag);

    rc = file_op(BAT_SAFETY_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char *)&buf, sizeof(char)*120, FILE_OP_WRITE);
    if(rc<0)
        pr_err("%s:Write file:%s err!\n", __func__, BAT_SAFETY_FILE_NAME);

    return rc;
}

static int request_reload_bat_safety(void)
{
    char buf[70]={0};
    int rc;
	CHG_DBG_E("[BAT][CHG]%s +++\n", __func__);

    sprintf(buf, REQUEST_RELOAD_BAT_SAFETY);

    rc = file_op(BAT_SAFETY_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char *)&buf, sizeof(char)*70, FILE_OP_WRITE);
    if(rc<0)	
        pr_err("Write file:%s err!\n", BAT_SAFETY_FILE_NAME);
    return rc;
}

static int reload_retry_count = 0;
static int init_batt_cycle_count_data(void)
{
    int rc = 0;
    struct CYCLE_COUNT_DATA buf;

# if 0
    /* Read cycle count data from emmc */
	rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char*)&buf, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_READ);
    if(rc < 0) {
        pr_err("Read cycle count file failed!\n");
        return rc;
    }
#endif
	reload_retry_count ++;
	if(g_Charger_mode == true)
	{
		pr_err("g_Charger_mode == true, bypass init_batt_cycle_count_data");
        return -1;
	}
	CHG_DBG_E("[BAT][CHG]%s SAFETY_init,cycle = %d;reload_cond = %d;high_temp_t = %lu;high_temp_vol_t = %lu;high_vol_t = %lu;battery_total_t = %lu;reset_flag = %d\n",
            __func__, buf.cycle_count,buf.reload_condition, buf.high_temp_total_time,buf.high_temp_vol_time,buf.high_vol_total_time,buf.battery_total_time,buf.reset_flag);

    /* Check data validation */
 	if((buf.magic != CYCLE_COUNT_DATA_MAGIC) && (g_Charger_mode == false)) {
        pr_err("data validation! buf.magic = %d, correct is %d, reload retry count : %d\n",
                buf.magic, CYCLE_COUNT_DATA_MAGIC, reload_retry_count);

        request_reload_bat_safety();

        if (reload_retry_count >= 10) {
            pr_err("Create new .bs file\n");
            file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
            	(char*)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
        }

	/*
    	if(buf.magic != CYCLE_COUNT_DATA_MAGIC) {
		pr_err("data validation! buf.magic = %d, correct is %d\n", buf.magic, CYCLE_COUNT_DATA_MAGIC);
        file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char*)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
		*/
        return -1;
    }else {
        /* Update current value */
        CHG_DBG_E("[BAT][CHG]%s Update current value!\n", __func__);
        g_cycle_count_data.cycle_count = buf.cycle_count;
        g_cycle_count_data.high_temp_total_time = buf.high_temp_total_time;
        g_cycle_count_data.high_temp_vol_time = buf.high_temp_vol_time;
        g_cycle_count_data.high_vol_total_time = buf.high_vol_total_time;
        g_cycle_count_data.reload_condition = buf.reload_condition;
        g_cycle_count_data.battery_total_time = buf.battery_total_time;
		g_cycle_count_data.reset_flag = buf.reset_flag;
		

        rc = backup_bat_percentage();
        if(rc < 0){
            pr_err("backup_bat_percentage failed!\n");
            return -1;
        }

#if 0
        rc = backup_bat_cyclecount();
        if(rc < 0){
            pr_err("backup_bat_cyclecount failed!\n");
            return -1;
        }
#endif

        rc = backup_bat_safety();
        if(rc < 0){
            pr_err("backup_bat_cyclecount failed!\n");
            return -1;
        }

        CHG_DBG_E("[BAT][CHG]%s cycle_count=%d;reload_condition=%d;high_temp_total_time=%lu;high_temp_vol_time=%lu;high_vol_total_time=%lu;battery_total_time=%lu\n",
            __func__, buf.cycle_count,buf.reload_condition, buf.high_temp_total_time,buf.high_temp_vol_time,buf.high_vol_total_time,buf.battery_total_time);
    }
    CHG_DBG_E("[BAT][CHG]%s Cycle count data initialize success!\n", __func__);

    g_cyclecount_initialized = true;
    set_full_charging_voltage();
    return 0;
}

static void write_back_cycle_count_data(void)
{
    int rc;

    backup_bat_percentage();
    //backup_bat_cyclecount();
    backup_bat_safety();
    //batt_safety_csc_backup();
	
    rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char *)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
    if(rc<0)
        pr_err("%s:Write file:%s err!\n", __func__, CYCLE_COUNT_FILE_NAME);
}

static void asus_reload_battery_profile(int value){

    //save current status
    write_back_cycle_count_data();

    //reloade battery
    //reload_battery_profile(chip);
    set_full_charging_voltage();

    CHG_DBG("[BAT][CHG]%s !!new profile is value=%d\n", __func__, value);
}

static void asus_judge_reload_condition(struct bat_safety_condition *safety_cond)
{
    int temp_condition = 0;
    int cycle_count = 0;
    // bool full_charge;
    unsigned long local_high_vol_time = g_cycle_count_data.high_vol_total_time;
    unsigned long local_high_temp_time = g_cycle_count_data.high_temp_total_time;
    //unsigned long local_high_temp_vol_time = g_cycle_count_data.high_temp_vol_time;
    unsigned long local_battery_total_time = g_cycle_count_data.battery_total_time;

    temp_condition = g_cycle_count_data.reload_condition;
    if(temp_condition >= 4){ //if condition=2 will return
        return ;
    }

    //only full charger can load new profile
    // full_charge = fg->charge_done;
    // if(!full_charge)
    //     return ;

	//add for media flag to bypass condition ++
	if(ASUS_media_en == 0)
	{
		//1.judge battery using total time
		if(local_battery_total_time >= safety_cond->condition4_battery_time){
			g_cycle_count_data.reload_condition = 4;
			goto DONE;
		}else if(local_battery_total_time >= safety_cond->condition3_battery_time &&
			local_battery_total_time < safety_cond->condition4_battery_time){
			g_cycle_count_data.reload_condition = 3;
		}
		else if(local_battery_total_time >= safety_cond->condition2_battery_time &&
			local_battery_total_time < safety_cond->condition3_battery_time){
			g_cycle_count_data.reload_condition = 2;
		}else if(local_battery_total_time >= safety_cond->condition1_battery_time &&
			local_battery_total_time < safety_cond->condition2_battery_time){
			g_cycle_count_data.reload_condition = 1;
		}

		//2. judge battery cycle count
		cycle_count = g_cycle_count_data.cycle_count;
	#if 0 //disable reloade condition with cycle_count
		if(cycle_count >= chip->condition2_cycle_count){
			g_cycle_count_data.reload_condition = 2;
			goto DONE;
		}else if(cycle_count >= chip->condition1_cycle_count &&
			cycle_count < chip->condition2_cycle_count){
			g_cycle_count_data.reload_condition = 1;
		}
	#endif
	#if 0 //disable reloade condition with high_temp_vol
		//3. judge high temp and voltage condition
		if(local_high_temp_vol_time >= fg->condition2_temp_vol_time){
			g_cycle_count_data.reload_condition = 2;
			goto DONE;
		}else if(local_high_temp_vol_time >= fg->condition1_temp_vol_time &&
			local_high_temp_vol_time < fg->condition2_temp_vol_time){
			g_cycle_count_data.reload_condition = 1;
		}
	#endif

		//4. judge high temp condition
		if(local_high_temp_time >= safety_cond->condition2_temp_time){
			g_cycle_count_data.reload_condition = 4;
			goto DONE;
		}else if(local_high_temp_time >= safety_cond->condition1_temp_time &&
			local_high_temp_time < safety_cond->condition2_temp_time){
			g_cycle_count_data.reload_condition = 3;
		}

		//5. judge high voltage condition
		if(local_high_vol_time >= safety_cond->condition2_vol_time){
			g_cycle_count_data.reload_condition = 4;
			goto DONE;
		}else if(local_high_vol_time >= safety_cond->condition1_vol_time &&
			local_high_vol_time < safety_cond->condition2_vol_time){
			g_cycle_count_data.reload_condition = 3;
		}
	}
	
DONE:
	if(ASUS_media_en == 1)
	{
		//do nothing, just calculate time
		pr_err("%s local test, asus media flag = 1, bypass battery safety upgrade condition\n", __func__);
	}
	else if (ASUS_media_en == 0)
	{
		if(temp_condition != g_cycle_count_data.reload_condition)
		{ asus_reload_battery_profile(g_cycle_count_data.reload_condition);}
	}
}

unsigned long last_battery_total_time = 0;
unsigned long last_high_temp_time = 0;
unsigned long last_high_vol_time = 0;
unsigned long last_high_temp_vol_time = 0;

static void calculation_time_fun(int type)
{
    unsigned long now_time;
    unsigned long temp_time = 0;
	struct timespec64  mtNow;
    //now_time = asus_qpnp_rtc_read_time();
	mtNow = ktime_to_timespec64(ktime_get_boottime());
	now_time = mtNow.tv_sec;
    pr_err("%s +++, now_time = %d\n", __func__,now_time);

    if(now_time < 0){
        pr_err("asus read rtc time failed!\n");
        return ;
    }

    switch(type){
        case TOTOL_TIME_CAL_TYPE:
            if(0 == last_battery_total_time){
                last_battery_total_time = now_time;
				//g_cycle_count_data.battery_total_time += now_time;
                CHG_DBG("[BAT][CHG]%s now_time=%lu;last_battery_total_time=%lu\n", __func__, now_time, g_cycle_count_data.battery_total_time);
            }else{
                temp_time = now_time - last_battery_total_time;
                if(temp_time > 0)
                    g_cycle_count_data.battery_total_time += temp_time;
                last_battery_total_time = now_time;
            }
        break;

        case HIGH_VOL_CAL_TYPE:
            if(0 == last_high_vol_time){
                last_high_vol_time = now_time;
				 //g_cycle_count_data.high_vol_total_time += now_time;
                CHG_DBG("[BAT][CHG]%s now_time=%lu;high_vol_total_time=%lu\n", __func__, now_time, g_cycle_count_data.high_vol_total_time);
            }else{
                temp_time = now_time - last_high_vol_time;
                if(temp_time > 0)
                    g_cycle_count_data.high_vol_total_time += temp_time;
                last_high_vol_time = now_time;
            }
        break;

        case HIGH_TEMP_CAL_TYPE:
            if(0 == last_high_temp_time){
                last_high_temp_time = now_time;
				//g_cycle_count_data.high_temp_total_time += now_time;
                CHG_DBG("[BAT][CHG]%s now_time=%lu;high_temp_total_time=%lu\n", __func__, now_time, g_cycle_count_data.high_temp_total_time);
            }else{
                temp_time = now_time - last_high_temp_time;
                if(temp_time > 0)
                    g_cycle_count_data.high_temp_total_time += temp_time;
                last_high_temp_time = now_time;
            }
        break;

        case HIGH_TEMP_VOL_CAL_TYPE:
            if(0 == last_high_temp_vol_time){
                last_high_temp_vol_time = now_time;
				//g_cycle_count_data.high_temp_vol_time += now_time;
                CHG_DBG("[BAT][CHG]%s now_time=%lu;high_temp_vol_time=%lu\n", __func__, now_time, g_cycle_count_data.high_temp_vol_time);
            }else{
                temp_time = now_time - last_high_temp_vol_time;
                if(temp_time > 0)
                    g_cycle_count_data.high_temp_vol_time += temp_time;
                last_high_temp_vol_time = now_time;
            }
        break;
    }
}

static int write_test_value = 0;

static void update_battery_safe()
{
    int rc;
    int temp;
    int capacity;
    union power_supply_propval prop = {};

    //CHG_DBG_E("[BAT][CHG]%s +++", __func__);

    if(g_asuslib_init != true){
        pr_err("asuslib init is not ready");
        return;
    }

    if(g_cyclecount_initialized != true){
        rc = init_batt_cycle_count_data();
        if(rc < 0){
            pr_err("cyclecount is not initialized");
            return;
        }
    }

    rc = power_supply_get_property(qti_phy_bat,
        POWER_SUPPLY_PROP_TEMP, &prop);
    if (rc < 0) {
        pr_err("Error in getting battery temp, rc=%d\n", rc);
    }
    temp= prop.intval;

    rc = power_supply_get_property(qti_phy_bat,
        POWER_SUPPLY_PROP_CAPACITY, &prop);
    if (rc < 0) {
        pr_err("Error in getting capacity, rc=%d\n", rc);
    }
    capacity = prop.intval;

    rc = power_supply_get_property(qti_phy_bat,
        POWER_SUPPLY_PROP_CYCLE_COUNT, &prop);
    if (rc < 0) {
        pr_err("Error in getting cycle count, rc=%d\n", rc);
    }
	//g_cycle_count_data.cycle_count = prop.intval;
	g_cycle_count_data.cycle_count = g_bat_cycle_count;

    CHG_DBG_E("[BAT][CHG]%s temp=%d, capacity=%d, cycle_count=%d", __func__, temp, capacity, g_cycle_count_data.cycle_count);

    //check data
    calculation_time_fun(TOTOL_TIME_CAL_TYPE);

    if(capacity == FULL_CAPACITY_VALUE){
        calculation_time_fun(HIGH_VOL_CAL_TYPE);
    }else{
        last_high_vol_time = 0; //exit high vol
    }

    if(temp >= HIGHER_TEMP){
        calculation_time_fun(HIGH_TEMP_CAL_TYPE);
    }else{
        last_high_temp_time = 0; //exit high temp
    }

    if(temp >= HIGH_TEMP && capacity == FULL_CAPACITY_VALUE){
        calculation_time_fun(HIGH_TEMP_VOL_CAL_TYPE);
    }else{
        last_high_temp_vol_time = 0; //exit high temp and vol
    }

    asus_judge_reload_condition(&safety_cond);
    write_back_cycle_count_data();
	CHG_DBG_E("[BAT][CHG]%s, cycle_count=%d;reload_condition=%d;high_temp_total_time=%lu;high_temp_vol_time=%lu;high_vol_total_time=%lu;battery_total_time=%lu;reset_flag=%d\n",
            __func__, g_cycle_count_data.cycle_count, g_cycle_count_data.reload_condition,
            g_cycle_count_data.high_temp_total_time, g_cycle_count_data.high_temp_vol_time,
            g_cycle_count_data.high_vol_total_time, g_cycle_count_data.battery_total_time,g_cycle_count_data.reset_flag);
    //CHG_DBG("[BAT][CHG]%s ---", __func__);
}

void battery_safety_worker(struct work_struct *work)
{
	CHG_DBG_E("[BAT][CHG]%s +++", __func__);
	update_battery_safe();
    if (g_cyclecount_initialized != true && reload_retry_count <= 20) {
        cancel_delayed_work(&battery_safety_work);
        schedule_delayed_work(&battery_safety_work, 5 * HZ);
    } else {
        cancel_delayed_work(&battery_safety_work);
        schedule_delayed_work(&battery_safety_work, BATTERY_SAFETY_UPGRADE_TIME * HZ);
    }
}

void battery_soc_record_worker(struct work_struct *work)
{
	battery_soc_record_worker_counter++;			
 	CHG_DBG_E("[BAT][CHG]%s, worker counter=%d \n", __func__,battery_soc_record_worker_counter);
    cancel_delayed_work(&battery_soc_record_work);
	//delay one cycle to trigger power_supply_changed
	if(stop_battery_soc_record_worker_flag==1)
	{
		CHG_DBG_E("[BAT][CHG] do power_supply_changed\n" );	
		power_supply_changed(qti_phy_bat);	
		stop_battery_soc_record_worker_flag = 0;
		return;
	}
	if ( (adsp_bat_scale_1 != 0 ) && (adsp_bat_scale_1 < 101 ) && stop_battery_soc_record_worker_flag ==0)
	{
		stop_battery_soc_record_worker_flag = 1 ;
		// write file adsp scale to adsp	
		CHG_DBG_E("[BAT][CHG]%s battery_soc_record_worker,adsp_bat_scale_1 != adsp_bat_scale, adsp_bat_scale_1=%d\n", __func__,adsp_bat_scale_1);					
		SET_BAT_RECORD_INFO_TO_ADSP(adsp_bat_scale_1);
		//power_supply_changed(qti_phy_bat);	
	}
	if (stop_battery_soc_record_worker_flag == 0 && battery_soc_record_worker_counter < 25)
    {	
		schedule_delayed_work(&battery_soc_record_work, 1 * HZ); //1 * HZ  =100				
	}
	if (stop_battery_soc_record_worker_flag == 1 && battery_soc_record_worker_counter < 25)
    {	
		schedule_delayed_work(&battery_soc_record_work, 50); //500ms			
	}		
}
//ASUS_BSP battery safety upgrade ---

static int cycle_count_proc_show(struct seq_file *buf, void *data)
{
    seq_printf(buf, "---show cycle count value---\n");

#if 0  //for debug
    get_asus_cycle_count(&g_cycle_count_data.cycle_count);

    seq_printf(buf,"cycle[%d,%d,%d,%d,%d,%d,%d,%d]\n",
        g_fgChip->counter->count[0],g_fgChip->counter->count[1],g_fgChip->counter->count[2],
        g_fgChip->counter->count[3],g_fgChip->counter->count[4],g_fgChip->counter->count[5],
        g_fgChip->counter->count[6],g_fgChip->counter->count[7]);
#endif

    seq_printf(buf, "cycle count:%d\n", g_cycle_count_data.cycle_count);

    return 0;
}

static int cycle_count_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, cycle_count_proc_show, NULL);
}

static const struct proc_ops cycle_count_fops = {
    .proc_open = cycle_count_proc_open,
    .proc_read = seq_read,
    .proc_release = single_release,
};

static int batt_safety_proc_show(struct seq_file *buf, void *data)
{
    int rc =0;

    rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
    (char *)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
    if(rc < 0 )
        CHG_DBG_E("[BAT][CHG]%s write cycle count file error\n", __func__);

    seq_printf(buf, "---show battery safety value---\n");
    seq_printf(buf, "cycle_count:%d\n", g_cycle_count_data.cycle_count);
    seq_printf(buf, "battery_total_time:%lu\n", g_cycle_count_data.battery_total_time);
    seq_printf(buf, "high_temp_total_time:%lu\n", g_cycle_count_data.high_temp_total_time);
    seq_printf(buf, "high_vol_total_time:%lu\n", g_cycle_count_data.high_vol_total_time);
    seq_printf(buf, "high_temp_vol_time:%lu\n", g_cycle_count_data.high_temp_vol_time);
    seq_printf(buf, "reload_condition:%d\n", g_cycle_count_data.reload_condition);
	seq_printf(buf, "reset_flag:%d\n", g_cycle_count_data.reset_flag);

    return 0;
}
static int batt_safety_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, batt_safety_proc_show, NULL);
}

static ssize_t batt_safety_proc_write(struct file *file,const char __user *buffer,size_t count,loff_t *pos)
{
    int value=0;
    unsigned long time = 0;
    char buf[30] = {0};
    size_t buf_size;
    char *start = buf;

    buf_size = min(count, (size_t)(sizeof(buf)-1));
    if (copy_from_user(buf, buffer, buf_size)) {
        CHG_DBG_E("[BAT][CHG]%s Failed to copy from user\n", __func__);
        return -EFAULT;
    }
    buf[buf_size] = 0;

    sscanf(start, "%d", &value);
    while (*start++ != ' ');
    sscanf(start, "%lu", &time);

    write_test_value = value;

    switch(value){
        case 1:
            g_cycle_count_data.battery_total_time = time;
        break;
        case 2:
            g_cycle_count_data.cycle_count = (int)time;
        break;
        case 3:
            g_cycle_count_data.high_temp_vol_time = time;
        break;
        case 4:
            g_cycle_count_data.high_temp_total_time = time;
        break;
        case 5:
            g_cycle_count_data.high_vol_total_time = time;
        break;
		case 6:
            g_cycle_count_data.reset_flag = time;
        break;
		
        default:
            CHG_DBG("[BAT][CHG]%s input error!Now return\n", __func__);
            return count;
    }
    asus_judge_reload_condition(&safety_cond);
    CHG_DBG("[BAT][CHG]%s value=%d;time=%lu\n", __func__, value, time);

    return count;
}

static const struct proc_ops batt_safety_fops = {
    .proc_open = batt_safety_proc_open,
    .proc_read = seq_read,
    .proc_write = batt_safety_proc_write,
    .proc_release = single_release,
};

static int condition_value_proc_show(struct seq_file *buf, void *data)
{
    seq_printf(buf, "---show condition value---\n");
    seq_printf(buf, "condition1 battery time %lu\n", safety_cond.condition1_battery_time);
    seq_printf(buf, "condition2 battery time %lu\n", safety_cond.condition2_battery_time);
    seq_printf(buf, "condition3 battery time %lu\n", safety_cond.condition3_battery_time);
    seq_printf(buf, "condition4 battery time %lu\n", safety_cond.condition4_battery_time);
    seq_printf(buf, "condition1 cycle count %d\n", safety_cond.condition1_cycle_count);
    seq_printf(buf, "condition2 cycle count %d\n", safety_cond.condition2_cycle_count);
    seq_printf(buf, "condition1 temp time %lu\n", safety_cond.condition1_temp_time);
    seq_printf(buf, "condition2 temp time %lu\n", safety_cond.condition2_temp_time);
    seq_printf(buf, "condition1 temp&vol time %lu\n", safety_cond.condition1_temp_vol_time);
    seq_printf(buf, "condition2 temp&vol time %lu\n", safety_cond.condition2_temp_vol_time);
    seq_printf(buf, "condition1 vol time %lu\n", safety_cond.condition1_vol_time);
    seq_printf(buf, "condition2 vol time %lu\n", safety_cond.condition2_vol_time);

    return 0;
}

static int condition_value_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, condition_value_proc_show, NULL);
}

static ssize_t condition_value_proc_write(struct file *file,const char __user *buffer,size_t count,loff_t *pos)
{ 
      int value = 0;
    unsigned long condition1_time = 0;
    unsigned long condition2_time = 0;
    unsigned long condition3_time = 0;
    unsigned long condition4_time = 0;
    char buf[320];
    char *start = buf;

    if (copy_from_user(buf, buffer, count-1)) {
        CHG_DBG_E("[BAT][CHG]%s Failed to copy from user\n", __func__);
        return -EFAULT;
    }
    buf[count] = 0;

    sscanf(start, "%d", &value);

    if(value == 1 && start - buf < count){
        while (*start++ != ' '){};
        sscanf(start, "%lu", &condition1_time);
        while (*start++ != ' '){};
        sscanf(start, "%lu", &condition2_time); 										  
        while (*start++ != ' '){};
        sscanf(start, "%lu", &condition3_time);
        while (*start++ != ' '){};
        sscanf(start, "%lu", &condition4_time);
	  

        if(condition2_time <= condition1_time || condition3_time <= condition2_time || condition4_time <= condition3_time){
            CHG_DBG_E("[BAT][CHG]%s input value error,please input correct value! condition1_time= %d, condition2_time= %d, condition3_time= %d, condition4_time= %d\n", __func__,
                    condition1_time, condition2_time, condition3_time, condition4_time);		
			return count;
        }
    }
    else if(value != 0 && start - buf < count){
        while (*start++ != ' '){};
        sscanf(start, "%lu", &condition1_time);
        while (*start++ != ' '){};
        sscanf(start, "%lu", &condition2_time);

        if(condition2_time <= condition1_time){
            CHG_DBG_E("[BAT][CHG]%s input value error,please input correct value! condition1_time= %d, condition2_time= %d\n", __func__,
                    condition1_time, condition2_time);
            return count;
        }
    }

    switch(value){
        case 0:
            init_battery_safety(&safety_cond);
            g_cycle_count_data.reload_condition = 0;
        break;
        case 1:
            safety_cond.condition1_battery_time = condition1_time;
            safety_cond.condition2_battery_time = condition2_time;
            safety_cond.condition3_battery_time = condition3_time;
            safety_cond.condition4_battery_time = condition4_time;
        break;
        case 2:
            safety_cond.condition1_cycle_count = (int)condition1_time;
            safety_cond.condition2_cycle_count = (int)condition2_time;
        break;
        case 3:
            safety_cond.condition1_temp_vol_time = condition1_time;
            safety_cond.condition2_temp_vol_time = condition2_time;
        break;
        case 4:
            safety_cond.condition1_temp_time = condition1_time;
            safety_cond.condition2_temp_time = condition2_time;
        break;
        case 5:
            safety_cond.condition1_vol_time = condition1_time;
            safety_cond.condition2_vol_time = condition2_time;
        break;
    }
    CHG_DBG_E("[BAT][CHG]%s value=%d;condition1_time=%lu;condition2_time=%lu\n", __func__, value, condition1_time, condition2_time);
    if (value == 1)
        CHG_DBG_E("[BAT][CHG]%s value=%d;condition3_time=%lu;condition4_time=%lu\n", __func__, value, condition3_time, condition4_time);
	
    return count;
};

static const struct proc_ops condition_value_fops = {
    .proc_open = condition_value_proc_open,
    .proc_read = seq_read,
    .proc_write = condition_value_proc_write,
    .proc_release = single_release,
};
//0426
static int batt_safety_csc_proc_show(struct seq_file *buf, void *data)
{
    int rc =0;

    rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
    (char *)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
    if(rc < 0 )
        pr_err("[BAT][CHG]%s write cycle count file error\n", __func__);

    // seq_printf(buf, "---show battery safety value---\n");
    // seq_printf(buf, "cycle_count:%d\n", g_cycle_count_data.cycle_count);
    // seq_printf(buf, "battery_total_time:%lu\n", g_cycle_count_data.battery_total_time);
    // seq_printf(buf, "high_temp_total_time:%lu\n", g_cycle_count_data.high_temp_total_time);
    // seq_printf(buf, "high_vol_total_time:%lu\n", g_cycle_count_data.high_vol_total_time);
    // seq_printf(buf, "high_temp_vol_time:%lu\n", g_cycle_count_data.high_temp_vol_time);
    // seq_printf(buf, "reload_condition:%d\n", g_cycle_count_data.reload_condition);
	pr_err("[BAT][CHG]%s ++\n", __func__);

    seq_printf(buf, "%d,%d,%d,%d,%d",g_cycle_count_data.battery_total_time,
                                        g_cycle_count_data.cycle_count,
                                        g_cycle_count_data.high_temp_total_time,
                                        g_cycle_count_data.high_vol_total_time,
                                        g_cycle_count_data.high_temp_vol_time);


    return 0;
}
static int batt_safety_csc_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, batt_safety_csc_proc_show, NULL);
}

static int batt_safety_csc_erase(void){
    int rc =0;
    char buf[1]={0};

    g_cycle_count_data.battery_total_time = 0;
    g_cycle_count_data.cycle_count = 0;
    g_cycle_count_data.high_temp_total_time = 0;
    g_cycle_count_data.high_temp_vol_time = 0;
    g_cycle_count_data.high_vol_total_time = 0;
    g_cycle_count_data.reload_condition = 0;

    rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
    (char *)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
    if(rc < 0 )
        pr_err("[BAT][CHG]%s Write file:%s err!\n", __func__, CYCLE_COUNT_FILE_NAME);

    rc = file_op(BAT_PERCENT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char *)&buf, sizeof(char), FILE_OP_WRITE);
    if(rc<0)
        pr_err("[BAT][CHG]%s Write file:%s err!\n", __func__, BAT_PERCENT_FILE_NAME);

    pr_err("[BAT][CHG]%s Done! rc(%d)\n", __func__,rc);
    return rc;
}

int batt_safety_csc_backup(void){
    int rc = 0;
    struct CYCLE_COUNT_DATA buf;
//  char buf2[1]={0};

    rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char*)&buf, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_READ);
    if(rc < 0) {
        pr_err("[BAT][CHG]%s Read cycle count file failed!\n", __func__);
        return rc;
    }

    rc = file_op(CYCLE_COUNT_SD_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char *)&buf, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
    if(rc < 0 )
        pr_err("[BAT][CHG]%s Write cycle count file failed!\n", __func__);
#if 0
    rc = file_op(BAT_PERCENT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char*)&buf2, sizeof(char), FILE_OP_READ);
    if(rc < 0) {
        pr_err("[BAT][CHG]%s Read cycle count percent file failed!\n");
        return rc;
    }

    rc = file_op(BAT_PERCENT_SD_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
    (char *)&buf2, sizeof(char), FILE_OP_WRITE);
    if(rc < 0 )
        pr_err("[BAT][CHG]%s Write cycle count percent file failed!\n");
#endif
    pr_err("[BAT][CHG]%s Done!\n", __func__);
    return rc;
}

static int batt_safety_csc_restore(void){
    int rc = 0;
    struct CYCLE_COUNT_DATA buf;
    char buf2[1]={0};

    rc = file_op(CYCLE_COUNT_SD_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char*)&buf, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_READ);
    if(rc < 0) {
        pr_err("[BAT][CHG]%s Read cycle count file failed!\n", __func__);
        return rc;
    }

    rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
    (char *)&buf, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
    if(rc < 0 )
        pr_err("[BAT][CHG]%s Write cycle count file failed!\n", __func__);

    rc = file_op(BAT_PERCENT_SD_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
        (char*)&buf2, sizeof(char), FILE_OP_READ);
    if(rc < 0) {
        pr_err("[BAT][CHG]%s Read cycle count percent file failed!\n", __func__);
        return rc;
    }

    rc = file_op(BAT_PERCENT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
    (char *)&buf2, sizeof(char), FILE_OP_WRITE);
    if(rc < 0 )
        pr_err("[BAT][CHG]%s Write cycle count percent file failed!\n", __func__);

    init_batt_cycle_count_data();
    pr_err("[BAT][CHG]%s Done! rc(%d)\n", __func__,rc);
    return rc;
}

static void batt_safety_csc_stop(void){

    cancel_delayed_work(&battery_safety_work);
    pr_err("[BAT][CHG]%s Done! \n", __func__);
}

static void batt_safety_csc_start(void){

    schedule_delayed_work(&battery_safety_work, 0);
    pr_err("[BAT][CHG]%s Done! \n", __func__);
}

static ssize_t batt_safety_csc_proc_write(struct file *file,const char __user *buffer,size_t count,loff_t *pos)
{
    int value=0;
    char buf[2] = {0};
    size_t buf_size;
    char *start = buf;

    buf_size = min(count, (size_t)(sizeof(buf)-1));
    if (copy_from_user(buf, buffer, buf_size)) {
        pr_err("[BAT][CHG]%s Failed to copy from user\n", __func__);
        return -EFAULT;
    }
    buf[buf_size] = 0;

    sscanf(start, "%d", &value);

    switch(value){
        case 0: //erase
            batt_safety_csc_erase();
        break;
        case 1: //backup /persist to /sdcard
            batt_safety_csc_backup();
        break;
        case 2: //resotre /sdcard from /persist
            batt_safety_csc_restore();
        break;
        case 3://stop battery safety upgrade
            batt_safety_csc_stop();
            break;
        case 4: //start safety upgrade
            batt_safety_csc_start();
            break;
        case 5: // disable battery health debug log
            // batt_health_upgrade_debug_enable(false);
            break;
        case 6: // enable battery health debug log
            // batt_health_upgrade_debug_enable(true);
            break;
        case 7: // disable battery health upgrade
            // batt_health_upgrade_enable(false);
            break;
        case 8: // enable battery health upgrade
            // batt_health_upgrade_enable(true);
            break;
        case 9: //initial battery safety upgrade
            init_batt_cycle_count_data();
            break;
        default:
            pr_err("[BAT][CHG]%s input error!Now return\n", __func__);
            return count;
    }

    return count;
}

static const struct proc_ops batt_safety_csc_fops = {
    .proc_open = batt_safety_csc_proc_open,
    .proc_read = seq_read,
    .proc_write = batt_safety_csc_proc_write,
    .proc_release = single_release,
};
//0426
static void create_batt_cycle_count_proc_file(void)
{
    struct proc_dir_entry *asus_batt_cycle_count_dir = proc_mkdir("Batt_Cycle_Count", NULL);
    void *start;
    int ret = 0;

    struct proc_dir_entry *asus_bat_safety_proc_file = proc_create("bat_safety_wr", S_IRWXUGO,
        asus_batt_cycle_count_dir, &proc_bat_safety_operations);
    struct proc_dir_entry *asus_bat_bs_proc_file = proc_create("bat_bs_wr", S_IRWXUGO,
        asus_batt_cycle_count_dir, &proc_bat_safety_operations);
    struct proc_dir_entry *asus_bat_percent_proc_file = proc_create("bat_percent_wr", S_IRWXUGO,
        asus_batt_cycle_count_dir, &proc_bat_safety_operations);
	struct proc_dir_entry *asus_bat_sd_bs_proc_file = proc_create("bat_sd_bs_wr", S_IRWXUGO,
        asus_batt_cycle_count_dir, &proc_bat_safety_operations);
    struct proc_dir_entry *asus_bat_sd_percent_proc_file = proc_create("bat_sd_percent_wr", S_IRWXUGO,
        asus_batt_cycle_count_dir, &proc_bat_safety_operations);

    struct proc_dir_entry *asus_batt_cycle_count_proc_file = proc_create("cycle_count", 0666,
        asus_batt_cycle_count_dir, &cycle_count_fops);
    struct proc_dir_entry *asus_batt_batt_safety_proc_file = proc_create("batt_safety", 0666,
        asus_batt_cycle_count_dir, &batt_safety_fops);
	struct proc_dir_entry *asus_batt_batt_safety_csc_proc_file = proc_create("batt_safety_csc", 0666,
        asus_batt_cycle_count_dir, &batt_safety_csc_fops);
    struct proc_dir_entry *asus_batt_safety_condition_proc_file = proc_create("condition_value", 0666,
        asus_batt_cycle_count_dir, &condition_value_fops);

    if (!asus_batt_cycle_count_dir)
        CHG_DBG_E("[BAT][CHG]%s batt_cycle_count_dir create failed!\n", __func__);
    if (!asus_bat_safety_proc_file)
        CHG_DBG_E("[BAT][CHG]%s asus_bat_safety_proc_file create failed!\n", __func__);
    if (!asus_bat_bs_proc_file)
        CHG_DBG_E("[BAT][CHG]%s asus_bat_bs_proc_file create failed!\n", __func__);
    if (!asus_bat_percent_proc_file)
        CHG_DBG_E("[BAT][CHG]%s asus_bat_percent_proc_file create failed!\n", __func__);
	if (!asus_bat_sd_bs_proc_file)
        pr_info("[BAT][CHG]%s asus_bat_sd_bs_proc_file create failed!\n", __func__);
    if (!asus_bat_sd_percent_proc_file)
        pr_info("[BAT][CHG]%s asus_bat_sd_percent_proc_file create failed!\n", __func__);
    if (!asus_batt_cycle_count_proc_file)
        CHG_DBG_E("[BAT][CHG]%s batt_cycle_count_proc_file create failed!\n", __func__);
    if(!asus_batt_batt_safety_proc_file)
        CHG_DBG_E("[BAT][CHG]%s batt_safety_proc_file create failed!\n", __func__);
	if(!asus_batt_batt_safety_csc_proc_file)
        CHG_DBG_E("[BAT][CHG]%s batt_safety_csc_proc_file create failed!\n", __func__);
    if (!asus_batt_safety_condition_proc_file)
        printk(" create asus_batt_safety_condition_proc_file failed!\n");

    start = kzalloc((Bat_SAFETY_MAX_ITEM * Bat_SAFETY_STR_MAXLEN), GFP_KERNEL);
    if (!start) {
        ret = -ENOMEM;
        return ;
    }
    Batt_Safety.message = (char *)start;

    start = kzalloc((Bat_SAFETY_MAX_ITEM * Bat_SAFETY_STR_MAXLEN), GFP_KERNEL);
    if (!start) {
        ret = -ENOMEM;
        return ;
    }
    Batt_bs.message = (char *)start;

    start = kzalloc((Bat_SAFETY_MAX_ITEM * Bat_SAFETY_STR_MAXLEN), GFP_KERNEL);
    if (!start) {
        ret = -ENOMEM;
        return ;
    }
    Batt_percent.message = (char *)start;

	start = kzalloc((Bat_SAFETY_MAX_ITEM * Bat_SAFETY_STR_MAXLEN), GFP_KERNEL);
    if (!start) {
        ret = -ENOMEM;
        return ;
    }
    Batt_sd_bs.message = (char *)start;

    start = kzalloc((Bat_SAFETY_MAX_ITEM * Bat_SAFETY_STR_MAXLEN), GFP_KERNEL);
    if (!start) {
        ret = -ENOMEM;
        return ;
    }
    Batt_sd_percent.message = (char *)start;
}

static int reboot_shutdown_prep(struct notifier_block *this,
                  unsigned long event, void *ptr)
{
    switch(event) {
    case SYS_RESTART:
    case SYS_POWER_OFF:
        /* Write data back to emmc */
		if(g_cyclecount_initialized != true){
			pr_err("cyclecount is not initialized");
            break;
		}
		else if(g_Charger_mode == true)
		{
			pr_err("g_Charger_mode == true, no backup");
            break;
		}
        write_back_cycle_count_data();		
        break;
    default:
        break;
    }
    return NOTIFY_DONE;
}

/*  Call back function for reboot notifier chain  */
static struct notifier_block reboot_blk = {
    .notifier_call  = reboot_shutdown_prep,
};
//ASUS_BSP battery safety upgrade ---


#if 0
static int charge_notifier_callback(struct notifier_block *self,
                                 unsigned long event, void *data)
{
	static unsigned long pre_event = 0;

	CHG_DBG("%s: start, event:%lu\n", __func__, event);

	if (pre_event == event) {
		CHG_DBG("%s: same event %lu, return\n", __func__, event);
		return 0;
	}
	pre_event = event;

    switch (event) {
    case QTI_POWER_SUPPLY_CHARGED:
		CHG_DBG("%s: vbus plugin\n", __func__);
		g_vbus_plug = 1;
		schedule_delayed_work(&asus_min_check_work, 0);
        break;
    case QTI_POWER_SUPPLY_UNCHARGED:
		CHG_DBG("%s: vbus plugout\n", __func__);
		g_vbus_plug = 0;
		alarm_cancel(&bat_alarm);
		cancel_delayed_work(&asus_min_check_work);
		break;
    }
	return 0;
}
#endif

#define USB_BUS_ADD			0x0003
#define USB_BUS_REMOVE		0x0004
static int host_notifier_callback(struct notifier_block *self,
                                 unsigned long event, void *data)
{
	struct usb_bus *ubus = data;
	char usb1[]="a600000.dwc3";
	char usb2[]="a800000.dwc3";
	char devname[15];
	//int ret = 0;

	if(event >= 3){
		if(ubus->controller->parent) {
			strcpy(devname, dev_name(ubus->controller->parent));
			if(!strcmp(devname, usb1)){
				if (event == USB_BUS_ADD){
					usb1_active = 1;
				}
				else if (event == USB_BUS_REMOVE){
					usb1_active = 0;
				}
			}

			if(!strcmp(devname, usb2)){
				if (event == USB_BUS_ADD){
					usb2_active = 1;
				}
				else if (event == USB_BUS_REMOVE){
					usb2_active = 0;
				}
			}
		}
	}
	CHG_DBG("%s.  usb1_active = %d, usb2_active = %d, event = %lu\n", __func__, usb1_active, usb2_active, event);

	//[+++]Update the status of thermal alert
	asus_update_thermal_result();
	//[---]Update the status of thermal alert
	return NOTIFY_DONE;
}

#define CHECK_MINIMUM_INTERVAL (30)
#define REPORT_CAPACITY_POLLING_TIME (180)
int asus_chg_resume(struct device *dev)
{
	struct timespec64  mtNow;
	int nextCheckInterval;

	pm_stay_awake(g_bcdev->dev);

	ktime_get_coarse_real_ts64(&mtNow);

	if (mtNow.tv_sec - last_check_time_3m.tv_sec >= REPORT_CAPACITY_POLLING_TIME) {
		cancel_delayed_work(&asus_update_batt_status_work);
		schedule_delayed_work(&asus_update_batt_status_work, 0);
	}

	if (g_vbus_plug) {
		/* If next check time less than 30s, do check (next check time = last check time + 60s) */
		nextCheckInterval = 60 - (mtNow.tv_sec - last_check_time_1m.tv_sec);
		CHG_DBG("%s: nextCheckInterval = %d\n", __func__, nextCheckInterval);
		if (nextCheckInterval <= CHECK_MINIMUM_INTERVAL) {
			cancel_delayed_work(&asus_min_check_work);
			schedule_delayed_work(&asus_min_check_work, 0);
		} else {
			cancel_delayed_work(&asus_min_check_work);
			schedule_delayed_work(&asus_min_check_work, msecs_to_jiffies(nextCheckInterval));
			pm_relax(g_bcdev->dev);
		}
		wake_lock_timeout(&cable_resume_wake_lock, msecs_to_jiffies(1500));
	} else {
		pm_relax(g_bcdev->dev);
	}
	return 0;
}
EXPORT_SYMBOL(asus_chg_resume);

//ASUS BSP : Show "+" on charging icon +++
void asus_set_qc_state_worker(struct work_struct *work)
{
	if (g_IsBootComplete == 1 || g_Charger_mode == true)
		asus_extcon_set_state_sync(quickchg_extcon, g_SWITCH_LEVEL);
}

void set_qc_stat(int status)
{
	if (quickchg_extcon == NULL || g_asuslib_init == 0 || (g_IsBootComplete == 0 && g_Charger_mode == false)) {
		CHG_DBG("%s: quickchg_extcon or g_asuslib_init or not BootComplete Ready\n", __func__);
		return;
	}

	if (!g_vbus_plug) {
		cancel_delayed_work(&asus_set_qc_state_work);
		asus_extcon_set_state_sync(quickchg_extcon, SWITCH_LEVEL0_DEFAULT);
		CHG_DBG("%s: status: %d, switch: %d\n", __func__, status, SWITCH_LEVEL0_DEFAULT);
		return;
	}

	switch (status) {
	//"qc" stat happends in charger mode only, refer to smblib_get_prop_batt_status
	case POWER_SUPPLY_STATUS_CHARGING:
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	case POWER_SUPPLY_STATUS_FULL:
		CHG_DBG("%s: status: %d, switch: %d\n", __func__, status, g_SWITCH_LEVEL);
		cancel_delayed_work(&asus_set_qc_state_work);
		//if (g_Charger_mode)
			schedule_delayed_work(&asus_set_qc_state_work, 0);
		//else
		//schedule_delayed_work(&asus_set_qc_state_work, msecs_to_jiffies(3000));
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(set_qc_stat);
//ASUS BSP : Show "+" on charging icon ---

static int print_battery_status(void)
{
	union power_supply_propval prop = {};
	int rc = 0;
	char battInfo[256];
	char battInfo2[256];

	int bat_cap = 0, bat_fcc = 0, bat_vol = 0, bat_cur = 0, bat_temp = 0, chg_sts = 0, bat_health = 0, charge_counter = 0, soh = 0;
//porting -	char UTSInfo[256]; //ASUS_BSP add to printk the WIFI hotspot & QXDM UTS event
	struct psy_state *pst = &g_bcdev->psy_list[PSY_TYPE_BATTERY];

	get_oem_batt_temp_from_ADSP();
	get_oem_cell_volt_from_ADSP();
	get_oem_TFCC_TRM_from_ADSP();
	get_oem_gauge_soc_from_ADSP();
	get_oem_lt_max_cell_from_ADSP();

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_CAPACITY, rc=%d\n", __func__, rc);
	else
		bat_cap = prop.intval;

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CHARGE_FULL, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_CHARGE_FULL, rc=%d\n", __func__, rc);
	else
		bat_fcc = prop.intval;

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_VOLTAGE_NOW, rc=%d\n", __func__, rc);
	else
		bat_vol = prop.intval;

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_CURRENT_NOW, rc=%d\n", __func__, rc);
	else
	{
		bat_cur = prop.intval;
		g_bat_cur=bat_cur/1000;
	}

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_TEMP, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_TEMP, rc=%d\n", __func__, rc);
	else
		bat_temp = prop.intval;

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_STATUS, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_STATUS, rc=%d\n", __func__, rc);
	else
	{
		chg_sts = prop.intval;
		g_chg_sts= chg_sts;
	}
	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_HEALTH, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_HEALTH, rc=%d\n", __func__, rc);
	else
		bat_health = prop.intval;

	rc = power_supply_get_property(qti_phy_bat, POWER_SUPPLY_PROP_CHARGE_COUNTER, &prop);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get POWER_SUPPLY_PROP_CHARGE_COUNTER, rc=%d\n", __func__, rc);
	else
		{
		charge_counter = prop.intval;
		battery_RM = charge_counter/1000;
		}
	rc = read_property_id(g_bcdev, pst, 5); //5: BATT_SOH
	if (rc < 0)
		CHG_DBG_E("%s: Failed to get SOH, rc=%d\n", __func__, rc);
	else
		soh = pst->prop[5]; //5: BATT_SOH

	snprintf(battInfo, sizeof(battInfo), "report Capacity ==>%d, FCC:%dmAh, RM:%dmAh, BMS:%d, V:%dmV, Vcell1:%dmV, Vcell2:%dmV, Cur:%dmA",
		bat_cap,
		bat_fcc/1000,
		charge_counter/1000,
		ChgPD_Info.fg_real_soc,
		bat_vol/1000,
		ChgPD_Info.cell1_voltage,
		ChgPD_Info.cell2_voltage,
		bat_cur/1000);
	snprintf(battInfo, sizeof(battInfo), "%sgaugeTemp:%d.%dC, pmicTemp:%dC, BATID:%d, CHG_Status:%d(%s), QC_Extcon:%d(%s), BAT_HEALTH:%s\n",
		battInfo,
		bat_temp/10,
		bat_temp%10,
		ChgPD_Info.batt_temp,
		ChgPD_Info.BATT_ID,
		chg_sts,
		charging_stats[chg_sts],
		g_SWITCH_LEVEL,
		qc_extcon_type[g_SWITCH_LEVEL],
		health_type[bat_health]);

	ASUSEvtlog("[BAT][Ser]%s", battInfo);

	snprintf(battInfo2, sizeof(battInfo2), "report Capacity2 ==>%d, TFCC:%dmAh, TRM:%dmAh, SOH:%d, LT1:%d, LT2:%d\n",
		bat_cap,
		ChgPD_Info.TFCC,
		ChgPD_Info.TRM,
		soh,
		ChgPD_Info.lt_max_cell1,
		ChgPD_Info.lt_max_cell2);

	ASUSEvtlog("[BAT][Ser]%s", battInfo2);

	//ASUS_BSP +++ add to printk the WIFI hotspot & QXDM UTS event
//porting -	snprintf(UTSInfo, sizeof(UTSInfo), "WIFI_HS=%d, QXDM=%d", g_wifi_hs_en, g_qxdm_en);
//porting -	ASUSEvtlog("[UTS][Status]%s", UTSInfo);
	//ASUS_BSP --- add to printk the WIFI hotspot & QXDM UTS event

	ktime_get_coarse_real_ts64(&last_check_time_3m);

	return 0;
}

void  asus_update_batt_status_worker(struct work_struct *work)
{
	int ret = 0;
	

	ret = print_battery_status();
	if (ret < 0)
		CHG_DBG_E("%s: print_battery_status fail\n", __func__);

	if (!g_vbus_plug) {
		#if 0
		ret = asus_thermal_side();
		if (ret < 0)
			CHG_DBG_E("%s: asus_thermal_side fail\n", __func__);

		ret = asus_thermal_btm();
		if (ret < 0)
			CHG_DBG_E("%s: asus_thermal_side fail\n", __func__);

		if (usb_alert_side_flag | usb_alert_btm_flag) {
			CHG_DBG("%s: Set thermal_extcon 1\n", __func__);
			asus_extcon_set_state_sync(thermal_extcon, 1);
		} else {
			CHG_DBG("%s: Set thermal_extcon 0\n", __func__);
			asus_extcon_set_state_sync(thermal_extcon, 0);
		}
		#endif
		asus_update_thermal_result();
	}





	


	schedule_delayed_work(&asus_update_batt_status_work, msecs_to_jiffies(180000));
}
//[---]Add for ASUSEvtlog print battery status regularly

void get_vbus_status_from_ADSP(void)
{
	struct oem_get_VBUS_SRC_req req_msg = {};
	int rc = 0;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_VBUS_SOURCE;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get VBUS_SOURCE rc=%d\n", rc);
		return ;
	}
}
EXPORT_SYMBOL(get_vbus_status_from_ADSP);

void usbin_suspend_to_ADSP(int enable)
{
	struct oem_usbin_suspend_en_req req_msg = { { 0 } };
	int rc = 0;
	static int pre_enable = 0;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_SET_USBIN_SUSPNED;
	req_msg.enable = enable;

	CHG_DBG("%s. enable : %d, pre_enable : %d", __func__, req_msg.enable, pre_enable);
	if (req_msg.enable != pre_enable) {
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
		rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
		if (rc < 0) {
			pr_err("Failed to set USBIN_SUSPEND_EN rc=%d\n", rc);
			return;
		}
		pre_enable = req_msg.enable;
	}
}

void get_oem_batt_temp_from_ADSP(void)
{
	struct oem_get_batt_temp_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_Batt_temp;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get FW_version rc=%d\n", rc);
		return;
	}
}

void get_oem_cell_volt_from_ADSP(void)
{
	struct oem_get_cell_voltage_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_Cell_Voltage_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery cell voltage rc=%d\n", rc);
		return;
	}
}

void get_oem_TFCC_TRM_from_ADSP(void)
{
	struct oem_get_TFCC_TRM_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_TFCC_TRM_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery TFCC TRM rc=%d\n", rc);
		return;
	}
}

void get_oem_gauge_soc_from_ADSP(void)
{
	struct oem_get_fg_soc_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_FG_SoC_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery SO rc=%d\n", rc);
		return;
	}
}

void get_oem_lt_max_cell_from_ADSP(void)
{
	struct oem_get_lt_max_cell_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_LT_Max_Cell_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to lt_max_cell rc=%d\n", rc);
		return;
	}
}

void get_oem_bat_scale_from_ADSP(void)
{
	struct oem_get_bat_record_resp req_msg = {};
	int rc;
	
	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_BAT_RECORD_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get battery scale rc=%d\n", rc);
		return;
	}
	
}


/* +++ Add Maximun Battery Lifespan +++ */
#define CHG_LIMIT_PATH	"/asdf/CHGLimit"
void write_CHGLimit_value(int input)
{
#if 0 // remove due to GKI issue 
	struct file *fp = NULL;
	mm_segment_t old_fs;
//porting -	loff_t pos_lsts = 0;
	char buf[8] = "";

	sprintf(buf, "%d", input);

	fp = filp_open(CHG_LIMIT_PATH, O_RDWR | O_CREAT | O_SYNC, 0666);
	if (IS_ERR_OR_NULL(fp)) {
		CHG_DBG_E("%s: open (%s) fail\n", __func__, CHG_LIMIT_PATH);
		return;
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

//porting -	vfs_write(fp, buf, 8, &pos_lsts);

	set_fs(old_fs);
	filp_close(fp, NULL);

	CHG_DBG("%s : %s\n", __func__, buf);
#endif	
}
/* --- Add Maximun Battery Lifespan --- */

//[+++] Add the interface for audio driver request to set PMIC LCM status
int audio_req_set_lcm_mode(bool enable) {
	int rc = 0;

	CHG_DBG("%s. enable : %d\n", __func__, enable);
	rc = asus_set_charger_limit_mode(AUDIO_REQ_SET_LCM_MODE, enable);
	return rc;
}
EXPORT_SYMBOL(audio_req_set_lcm_mode);
//[---] Add the interface for audio driver request to set PMIC LCM status

void get_cc_status_from_ADSP(void)
{
	struct oem_get_batt_temp_req req_msg = {};
	int rc;

	req_msg.hdr.owner = PMIC_GLINK_MSG_OWNER_OEM;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_GET_SIDEPORT_CC_STATUS_REQ;
pr_err("opcode=0x%x call battery_chg_write ", req_msg.hdr.opcode);
	rc = battery_chg_write(g_bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get CC_status rc=%d\n", rc);
		return;
	}
}
EXPORT_SYMBOL(get_cc_status_from_ADSP);

int asuslib_init(void) {
	int rc = 0;
	struct pmic_glink_client_data client_data = { };
	struct pmic_glink_client	*client;
	

	printk(KERN_ERR "%s +++\n", __func__);
	// Initialize the necessary power supply
	rc = asus_init_power_supply_prop();
	if (rc < 0) {
		pr_err("Failed to init power_supply chains\n");
		return rc;
	}

	// Register the class node
	rc = class_register(&asuslib_class);
	if (rc) {
		pr_err("%s: Failed to register asuslib class\n", __func__);
		return -1;
	}

	//Register GPIO for PMI_MUX
/* porting -
	PMI_MUX_GPIO = of_get_named_gpio(g_bcdev->dev->of_node, "PMI_MUX_EN", 0);
	rc = gpio_request(PMI_MUX_GPIO, "PMI_MUX_EN");
	if (rc) {
		pr_err("%s: Failed to initalize the PMI_MUX_EN\n", __func__);
		return -1;
	}
	POGO_OTG_GPIO = of_get_named_gpio(g_bcdev->dev->of_node, "POGO_OTG_EN", 0);
	rc = gpio_request(POGO_OTG_GPIO, "POGO_OTG_EN");
	if (rc) {
		pr_err("%s: Failed to initalize the POGO_OTG_EN\n", __func__);
		return -1;
	}
*/
	alarm_init(&bat_alarm, ALARM_REALTIME, batAlarm_handler);
	INIT_DELAYED_WORK(&asus_min_check_work, asus_min_check_worker);
	INIT_DELAYED_WORK(&power_supply_changed_delay_work, power_supply_changed_delay_worker); //20230222
	INIT_DELAYED_WORK(&asus_18W_workaround_work, asus_18W_workaround_worker);
    msm_dwc3_register_switch(&dwc3_role_switch);

	//battery safety upgrade
	mutex_init(&mA);
	pr_err("local check! adsp_bat_scale_1 = %d\n",adsp_bat_scale_1);
    INIT_DELAYED_WORK(&battery_safety_work, battery_safety_worker);
  
	INIT_DELAYED_WORK(&battery_soc_record_work, battery_soc_record_worker);
    
	init_battery_safety(&safety_cond);
    //init_batt_cycle_count_data();
    create_batt_cycle_count_proc_file();
    register_reboot_notifier(&reboot_blk);
    schedule_delayed_work(&battery_safety_work, 60 * HZ);   //30


	schedule_delayed_work(&battery_soc_record_work, 1* HZ); //3 org=8 //porting -


	//[+++] Init the PMIC-GLINK
	client_data.id = PMIC_GLINK_MSG_OWNER_OEM;
	client_data.name = "asus_BC";
	client_data.msg_cb = asusBC_msg_cb;
	client_data.priv = g_bcdev;
	client_data.state_cb = asusBC_state_cb;
	client = pmic_glink_register_client(g_bcdev->dev, &client_data);
	if (IS_ERR(client)) {
		rc = PTR_ERR(client);
		if (rc != -EPROBE_DEFER)
			dev_err(g_bcdev->dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}
	//[---] Init the PMIC-GLINK

	//[+++] Init the info structure of ChargerPD from ADSP
	ChgPD_Info.PlatformID = 0;
	ChgPD_Info.BATT_ID = 0;
	ChgPD_Info.VBUS_SRC = 0;
	ChgPD_Info.chg_limit_en = 0;
	ChgPD_Info.chg_limit_cap = 0;
	ChgPD_Info.usbin_suspend_en = 0;
    ChgPD_Info.charging_suspend_en = 0;
	strcpy(ChgPD_Info.firmware_version , st_battery_name);
	//[---] Init the info structure of ChargerPD from ADSP

	get_vbus_status_from_ADSP();

	bat_extcon = extcon_asus_dev_allocate();
	if (IS_ERR(bat_id_extcon)) {
		rc = PTR_ERR(bat_extcon);
	}
	bat_extcon->name = "battery";
	
	rc = extcon_asus_dev_register(bat_extcon);
	if (rc < 0)
		printk(KERN_ERR "[BAT][CHG] failed to register bat_extcon device rc=%d\n", rc);

	bat_id_extcon = extcon_asus_dev_allocate();
	if (IS_ERR(bat_id_extcon)) {
		rc = PTR_ERR(bat_id_extcon);
	}
	bat_id_extcon->name = "battery_id";

	rc = extcon_asus_dev_register(bat_id_extcon);
	if (rc < 0)
		printk(KERN_ERR "[BAT][CHG] failed to register bat_id_extcon device rc=%d\n", rc);

	//[+++]Register the extcon for quick_charger
	quickchg_extcon = extcon_asus_dev_allocate();
	if (IS_ERR(quickchg_extcon)) {
		rc = PTR_ERR(quickchg_extcon);
		printk(KERN_ERR "[BAT][CHG] failed to allocate ASUS quickchg extcon device rc=%d\n", rc);
	}
//	#if defined ASUS_ZS673KS_PROJECT || defined ASUS_PICASSO_PROJECT
	quickchg_extcon->name = "quick_charging";
//	#endif
	rc = extcon_asus_dev_register(quickchg_extcon);
	if (rc < 0)
		printk(KERN_ERR "[BAT][CHG] failed to register ASUS quickchg extcon device rc=%d\n", rc);
		
	INIT_DELAYED_WORK(&asus_set_qc_state_work, asus_set_qc_state_worker);
	//[---]Register the extcon for quick_charger

	//[+++]Register the extcon for thermal alert
	thermal_extcon = extcon_asus_dev_allocate();
	if (IS_ERR(thermal_extcon)) {
		rc = PTR_ERR(thermal_extcon);
		printk(KERN_ERR "[BAT][CHG] failed to allocate ASUS thermal alert extcon device rc=%d\n", rc);
	}
	thermal_extcon->name = "usb_connector";	
	rc = extcon_asus_dev_register(thermal_extcon);
	if (rc < 0)
		printk(KERN_ERR "[BAT][CHG] failed to register ASUS thermal alert extcon device rc=%d\n", rc);
	//[---]Register the extcon for thermal alert

	//[+++]Register the extcon for invalid audio donlge
	audio_dongle_extcon = extcon_asus_dev_allocate();
	if (IS_ERR(audio_dongle_extcon)) {
		rc = PTR_ERR(audio_dongle_extcon);
		printk(KERN_ERR "[BAT][CHG] failed to allocate audio dongle extcon device rc=%d\n", rc);
	}
	audio_dongle_extcon->name = "invalid_dongle";

	rc = extcon_asus_dev_register(audio_dongle_extcon);
	if (rc < 0)
		printk(KERN_ERR "[BAT][CHG] failed to register audio dongle extcon device rc=%d\n", rc);
	//[---]Register the extcon for thermal alert

	asus_get_Batt_ID();

	//[+++] Init usb temp vadc channel

	side_usb_temp_vadc_chan = devm_iio_channel_get(g_bcdev->dev, "pm8550_amux_thm6_gpio2");
	if (IS_ERR_OR_NULL(side_usb_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel_get fail\n", __func__);
	}
	btm_usb_temp_vadc_chan = devm_iio_channel_get(g_bcdev->dev, "pm8550b_amux3_gpio6");
	if (IS_ERR_OR_NULL(btm_usb_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel_get fail\n", __func__);
	}
	volkey_fpc_temp_vadc_chan = devm_iio_channel_get(g_bcdev->dev, "pm8550_amux_thm5");
	if (IS_ERR_OR_NULL(volkey_fpc_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel_get fail\n", __func__);
	}
	batt_therm_temp_vadc_chan = devm_iio_channel_get(g_bcdev->dev, "pm8550b_amux_thm1_batt_therm");
	if (IS_ERR_OR_NULL(batt_therm_temp_vadc_chan)) {
		CHG_DBG_E("%s: iio_channel_get fail\n", __func__);
	}

	#if 0 // Use Glink to notify
	//register USB_Online notify
	charge_notify.notifier_call = charge_notifier_callback;
	qti_charge_register_notify(&charge_notify);
	#endif
	//[---] Init usb temp vadc channel

	//[+++] Register the notifier to receive the host change
	host_notify.notifier_call = host_notifier_callback;
	usb_register_notify(&host_notify);
	//[---] Register the notifier to receive the host change

    //PASS_HWID_TO_ADSP();

	wake_lock_init(&cable_resume_wake_lock, g_bcdev->dev, "cable_resume_wake_lock");
	wake_lock_init(&cos_wa_wake_lock, g_bcdev->dev, "cos_wa_wake_lock");   
	//register drm notifier
	INIT_DELAYED_WORK(&asus_set_panelonoff_current_work, asus_set_panelonoff_current_worker);
	INIT_DELAYED_WORK(&asus_set_panelonoff_Gauge_EN_work, asus_set_panelonoff_Gauge_EN_worker);
	INIT_DELAYED_WORK(&asus_set_Gauge_EN_delay_work, asus_set_Gauge_EN_delay_worker);
	INIT_DELAYED_WORK(&asus_update_batt_status_work, asus_update_batt_status_worker);
	schedule_delayed_work(&asus_update_batt_status_work, msecs_to_jiffies(5000));


//porting -	create_uts_status_proc_file(); //ASUS_BSP LiJen add to printk the WIFI hotspot & QXDM UTS event

	CHG_DBG("%s:g_vbus_plug=%d\n", __func__, g_vbus_plug);
	if (g_vbus_plug) {
		schedule_delayed_work(&asus_min_check_work, 0);
	}
#if 0 //porting -
	if (g_Charger_mode)
		ASUSEvtlog("[BAT][CHG] Enter COS");
	else
		ASUSEvtlog("[BAT][CHG] Enter MOS");
#endif		
	g_asuslib_init = 1;
	CHG_DBG_E("Load the asuslib_init Succesfully\n");
	return rc;
}
EXPORT_SYMBOL(asuslib_init);

int asuslib_deinit(void) {
	//int rc;

	wake_lock_destroy(&cable_resume_wake_lock);
 	wake_lock_destroy(&cos_wa_wake_lock);
	class_unregister(&asuslib_class);
	return 0;
}
EXPORT_SYMBOL(asuslib_deinit);

MODULE_LICENSE("GPL v2");
