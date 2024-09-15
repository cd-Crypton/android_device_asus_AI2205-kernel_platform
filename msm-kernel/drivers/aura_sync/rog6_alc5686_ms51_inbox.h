#include <linux/leds.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <drm/drm_panel.h>
#include <linux/soc/qcom/panel_event_notifier.h>

#define RGB_MAX 21   //for rainbow color setting
#define FEATURE_WRITE_COMMAND_SIZE 6
#define FEATURE_READ_COMMAND_SIZE 3
#define FEATURE_WRITE_LONG_COMMAND_SIZE 52
#define FEATURE_READ_LONG_COMMAND_SIZE 14

#define POWER_OTG 0
#define POWER_VBAT 1

#define SPROM_SIZE 36

struct inbox_drvdata {
	struct led_classdev led;
	void *notifier_cookie;
};

//extern bool g_Charger_mode;
static bool g_Charger_mode=false;
static int g_FAN_ID = 0;

struct mutex ms51_mutex;
struct mutex update_lock;

struct hidraw *rog6_inbox_hidraw;
//EXPORT_SYMBOL_GPL(rog6_inbox_hidraw);

static struct workqueue_struct *fan7_wq = NULL;
static struct work_struct fan7_cooler_work;

extern int usbhid_set_raw_report(struct hid_device *hid, unsigned int reportnum,
                                __u8 *buf, size_t count, unsigned char rtype);
extern int usbhid_get_raw_report(struct hid_device *hid, unsigned char report_number, __u8 *buf, size_t count,
               unsigned char report_type);

extern void FANDG_connect(int val);
extern bool FANDG_USBID_detect;
extern void vph_output_side_port(int val);
extern int vph_get_status(void);
extern int g_HWID;
extern uint8_t gInboxPowerSwap;
extern int gPanelStatus;

int inbox7_power_source_swap(int direction);
static int pd_set_cooler_stage(int stage,bool force_set);

// Choose I2C addr
static u8 IC_switch;

struct mutex pd_lock;
struct mutex hid_command_lock;
struct mutex power_lock;

enum i2c_address_list
{
	addr_0x16 = 1,
	addr_0x18 = 2,
	addr_0x75 = 3,
	addr_0x40 = 4,
	addr_0x41 = 5,
	addr_0x64 = 6,
	addr_0x28 = 7,
	addr_0x28_long = 8,
	addr_set_codec_cali = 9,
	addr_get_codec_cali = 10,
	addr_MPS = 11,
	addr_keyboard_mode = 12,
	addr_ohter = 255,
};

enum pd_reg_0x13_mask
{
	PCB_ID2 = 0x10,
	DOOR_EN = 0x20,
	DET_INT = 0x40,
	WAKEUP_PDIC = 0x80,
};

enum pd_reg_0x14_mask
{
	EN_SW1 = 0x01,
	EN_SW2 = 0x02,
	EN_SW4 = 0x04,
	EN_SW6 = 0x08,
	EN_SW7 = 0x10,
	EN_COOL = 0x20,
	LOGO_EN = 0x40,
	PCB_ID1 = 0x80,
};

enum pd_reg
{
	VENDOR_ID_MSB = 0x00,
	VENDOR_ID_LSB = 0x01,
	DEVICE_ID_MSB = 0x02,
	DEVICE_ID_LSB = 0x03,
	VERSION_0 = 0x04,
	VERSION_1 = 0x05,
	VERSION_2 = 0x06,
	VERSION_3 = 0x07,
	ISP_MODE = 0x0F,
	GPIO_GP0 = 0x11,
	GPIO_GP1 = 0x13,
	GPIO_GP2 = 0x14,
};
enum fan7_ID
{
	FAN7_ID_PR2 = 0x00,
	FAN7_ID_PR = 0x01,
	FAN7_ID_ER = 0x02,
	FAN7_ID_SR = 0x03,
};
enum fan7_OV
{
	OV1 = 0,
	OV2 = 1,
	OV3 = 2,
	OV4 = 3,
	OV_ALL = 4,
};

struct rpm_table_t {
	int pwm_val;
	int fan_rpm;
};

static u8 g_led_on = 0;
static u8 g_door_on = 0;
static u8 g_logo_on = 0;
static u8 g_cooling_en = 0;
static u8 g_cooling_stage = 0;
static u8 g_TH_meaure = 0;

//  For 2Leds MS51
static u8 g_2led_mode = 0;
static u8 g_2led_mode2 = 0;
static u8 g_2led_apply = 0;
static u8 key_state = 0;

static u32 g_2led_red_max;
static u32 g_2led_green_max;
static u32 g_2led_blue_max;
static u32 g_2led_red;
static u32 g_2led_green;
static u32 g_2led_blue;
static u32 g_2led_speed;

//  For 3Leds MS51
static u8 g_3led_mode = 0;
static u8 g_3led_mode2 = 0;
static u8 g_3led_apply = 0;

static u32 g_3led_red_max;
static u32 g_3led_green_max;
static u32 g_3led_blue_max;
static u32 g_3led_red;
static u32 g_3led_green;
static u32 g_3led_blue;
static u32 g_3led_speed;
static u32 g_aura_lpm;
static u32 g_mps_lpm;
static u8 g_Aura_ic_updating = 0; //1: 2led is updating, 2: 3led is updating

static int g_Adapter_online = 0;
static int g_bypass_charging = 0;
static int g_Power_source;
static bool g_PD_updating = false;
static int g_delay_ms = 8;
static u32 g_latch_status;

static int pd_byte_read_reg(int reg_addr, u8 *val);
static int pd_byte_write_reg(u8 reg_addr, u8 val);
static int pd_wakeup(int enable);
static int aura_lpm_enable(int enable);
static u8 mps_lpm_mode_enable(int enable);
static void reset_status(void);
