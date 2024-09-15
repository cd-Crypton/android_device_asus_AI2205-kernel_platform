#ifndef _GAMEPAD_GU200A_HID_H_
#define _GAMEPAD_GU200A_HID_H_

// CMD ID
//#define ASUS_GAMEPAD_GET_RED_PWM            0x01
//#define ASUS_GAMEPAD_GET_GREEN_PWM          0x02
//#define ASUS_GAMEPAD_GET_BLUE_PWM           0x03
#define ASUS_GAMEPAD_GET_MODE               0xA1
#define ASUS_GAMEPAD_GET_FRAME              0x81
#define ASUS_GAMEPAD_GET_SPEED              0xA2
#define ASUS_GAMEPAD_GET_BRIGHTNESS         0xA3
#define ASUS_GAMEPAD_GET_RANDOM             0xA4
#define ASUS_GAMEPAD_GET_SLEEP_MODE         0xB1
#define ASUS_GAMEPAD_GU200A_GET_ROG_BUTTON	0xC0
//#define ASUS_GAMEPAD_GET_LED_ON             0x09
//#define ASUS_GAMEPAD_GET_FWMODE             0x0A
//#define ASUS_GAMEPAD_GET_FW_VERSION         0x07

//#define ASUS_GAMEPAD_SET_RED_PWM            0x81
//#define ASUS_GAMEPAD_SET_GREEN_PWM          0x82
//#define ASUS_GAMEPAD_SET_BLUE_PWM           0x83
#define ASUS_GAMEPAD_CMDID_NONE               0xFF
#define ASUS_GAMEPAD_SET_MODE               0x21
#define ASUS_GAMEPAD_SET_SLEEP_MODE         0x31
#define ASUS_GAMEPAD_SET_FRAME              0x01
#define ASUS_GAMEPAD_SET_APPLY              0x2F
#define ASUS_GAMEPAD_SET_SPEED              0x22
#define ASUS_GAMEPAD_SET_BRIGHTNESS         0x23
#define ASUS_GAMEPAD_SET_RANDOM             0x24
//#define ASUS_GAMEPAD_SET_LED_ON             0x89
#define ASUS_GAMEPAD_GU200A_SET_ROG_BUTTON	0x20

// CMD GROUP
#define ASUS_GAMEPAD_GU200A_AURA_MODE       0x80
#define ASUS_GAMEPAD_GU200A_AURA_SPEED      0x81
#define ASUS_GAMEPAD_GU200A_ROG_BUTTON_MODE	0x82
#define ASUS_GAMEPAD_GU200A_KEY_MAPPING		0x83
#define ASUS_GAMEPAD_GU200A_AURA_BRIGHTNESS 0x84
#define ASUS_GAMEPAD_GU200A_RESET 			0x85
#define ASUS_GAMEPAD_GU200A_AURA_RANDOM     0x87
#define ASUS_GAMEPAD_GU200A_AURA_SLEEP_MODE 0x89
#define ASUS_GAMEPAD_GU200A_AURA_FRAME      0xF0
#define ASUS_GAMEPAD_GU200A_COLOR_ASSIGN     0xC0
#define ASUS_GAMEPAD_GU200A_COLOR_APPLY      0xE0
#define ASUS_GAMEPAD_GU200A_GET_COLOR        0xC1
#define ASUS_GAMEPAD_GU200A_GET_RIGHT_FW_VER 0xE0
#define ASUS_GAMEPAD_GU200A_GET_LEFT_FW_VER  0xF0

#define ASUS_GAMEPAD_GU200A_REPORT_ID       0x80
#define ASUS_GAMEPAD_GU200A_FLASH_REPORT_ID 0xE8
#define ASUS_GAMEPAD_GU200A_CMD_LEN         0x03
#define ASUS_GAMEPAD_GU200A_CMD_LONG_LEN    0x0E
#define ASUS_GAMEPAD_GU200A_COLOR_CMD_LEN   0x0E
#define ASUS_GAMEPAD_GU200A_FLASH_CMD_LEN   0x40

#define RGB_MAX 21   //for rainbow color setting
#define DELAY_MS  25
#define BUFFER_LENGTH 10

// For Read FW
//#define ASUS_GAMEPAD_FW_REPORT_ID           0xA1
//#define ASUS_GAMEPAD_GET_MIDDLE_FW_VERSION  0x0C
//#define ASUS_GAMEPAD_GET_RIGHT_FW_VERSION   0x0D
//#define ASUS_GAMEPAD_GET_BT_FW_VERSION      0x0E

enum Color_pos
{
	RED_UPPER_LEFT 		= 0,
	GREEN_UPPER_LEFT 	= 1,
	BLUE_UPPER_LEFT 	= 2,
	RED_LOWER_LEFT 		= 3,
	GREEN_LOWER_LEFT 	= 4,
	BLUE_LOWER_LEFT 	= 5,
	RED_UPPER_RIGHT		= 6,
	GREEN_UPPER_RIGHT 	= 7,
	BLUE_UPPER_RIGHT 	= 8,
	RED_LOWER_RIGHT		= 9,
	GREEN_LOWER_RIGHT 	= 10,
	BLUE_LOWER_RIGHT 	= 11,
	COLOR_MAX			=12,
};
#endif

char button_name[21][40] = {{"none"},
							{"up"},
							{"right"},
							{"down"},
							{"left"},
							{"A"},
							{"B"},
							{"X"},
							{"Y"},
							{"L1"},
							{"R1"},
							{"THB_L"},
							{"THB_R"},
							{"ROG"},
							{"MENU"},
							{"HOME"},
							{"VIEW"},
							{"M1"},
							{"M2"},
							{"L2"},
							{"R2"}};

char rog_button_effect[6][40] = {{"Unknowed"},
								{"Screenshots_and_Video"},
								{"Back_To_Universal_App"},
								{"Button_Remapping_activator"},
								{"Change_Lighting_Effect"},
								{"Back_To_Armoury_Crate_App"}};

extern void GamePad_connect(int val);

static struct mutex gamepad_mutex;
static int mode2_state=0;
static int apply_state=0;
static bool DebugFlag = false;

static u32 g_mode;
static u32 g_speed;
static u32 g_led_on;
static u32 g_brightness;
static u32 g_key;
static u32 g_rog_button;
static u32 g_fw_mode;

static char g_raw_event_str_array[BUFFER_LENGTH][30] = {0};  	//this is the string array to store raw_event string
static char *g_raw_event_ptr_store = g_raw_event_str_array[0];  //this is the pointer to STORE string in g_raw_event_str_array[]
static char *g_raw_event_ptr_send = g_raw_event_str_array[0];  	//this is the pointer to SEND string in g_raw_event_str_array[]
static bool g_uevent_send_working = false;
static bool g_store_reverse = false; //store_ptr lapped send_ptr in reverse buffer

//four area light for GU200A
static u8 g_color_array[COLOR_MAX];

static struct hidraw *gamepad_hidraw;

struct gamepad_drvdata {
	struct led_classdev led;
	struct workqueue_struct		*send_event_workqueue;
	struct work_struct			send_event_work;
};
