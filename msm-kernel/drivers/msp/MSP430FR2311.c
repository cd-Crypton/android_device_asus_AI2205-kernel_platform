/* Copyright (C) 2018 Vishay MCU Microsystems Limited
 * Author: Randy Change <Randy_Change@asus.com>
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

#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/jiffies.h>
#include <linux/of_gpio.h>

#include "MSP430FR2311.h"
#define MSP430FR2311_I2C_NAME "msp430fr2311"

#include <drm/drm_panel.h>
#include <linux/soc/qcom/panel_event_notifier.h>

#define ZEN7   1

#define D(x...) pr_info(x)

#define I2C_RETRY_COUNT 3
#define UPDATE_FW_RETRY_COUNT 3

#define MCU_POLLING_DELAY 5000
#define MSP430_READY_I2C 0x35

struct MSP430FR2311_info *mcu_info;
static struct mutex MSP430FR2311_control_mutex;
static struct mutex MSP430FR2311_openclose_mutex;

enum eMCUState {
	MCU_TBD,
	MCU_EMPTY,
	MCU_PROGRAMMING,
	MCU_WAIT_POWER_READY,
	MCU_CHECKING_READY,
	MCU_READY,
	MCU_LOOP_TEST,
	MCU_PROGRAMMING_RETRY
} MCUState = MCU_TBD;

enum door_status DoorStatus=DOOR_UNKNOWN;

static signed char iCloseCounter=0;
static signed char iOpenCounter=0;
int (*fManualMode)(int , int , int );

enum UserK_State {
	UserK_TBD = 0,
	UserK_Start,
	UserK_rMCU_Ready,
	UserK_SaveK,
	UserK_NoSaveK,
	UserK_AbortK,
	UserK_Reset,
	UserK_Finish
}UserKState = UserK_TBD;

static void mcu_cal_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(cal_work, mcu_cal_work);
static void mcu_do_work_later(struct work_struct *work);
static DECLARE_DELAYED_WORK(report_work, mcu_do_work_later);
static void akm_INT_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(interrupt_work, akm_INT_work);
static struct work_struct rotate_work;
static void openclose_check(void);
static struct work_struct msp_resume_work;
static int openclose;
uint8_t akm_temp[8];
uint8_t akm_Ktemp[20];
unsigned int int_count=0;
extern int judgeDoorState(int fac);

void mStopConditionInit(void);

//static struct drm_panel *active_panel;
//static int drm_check_dt(struct device *dev);
static void msp430_register_for_panel_events(struct device *dev);
static void msp430_panel_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *event, void *client_data);

#define DEFAULT_POWERDOWNDURATION 30000
int powerDownDuration = DEFAULT_POWERDOWNDURATION;
bool bShowStopInfoOnce=1;
bool bProbeFail=1;
bool bPowerDone=0;

typedef enum{
    MOTOR_ANGLE,
    MOTOR_ROTATE,
    MOTOR_FORCE,
    MOTOR_DRVINT,
    MOTOR_GET_ANGLE,
    MOTOR_COPY_MCU_FILE,
    MOTOR_K_FINISH,
}MotorOpCode;

typedef enum{
    ROTATE_FINISH,
    ROTATE_CANCEL,
    ROTATE_STOP,
    ROTATE_FAIL,
}RotateStopReason;

//Backup S
#include <linux/mm.h>
#include <linux/syscalls.h>
#define MCU_BAKEUP_FILE_NAME "/motor_fw1/mcu_bak"
#define MCU_BAKEUP_RAW_FILE_NAME "/motor_fw1/mcu_raw"
#define MCU_BAKEUP_USER_FILE_NAME "/motor_fw2/mcu_bak2"
#define MCU_BAKEUP_USER_RAW_FILE_NAME "/motor_fw2/mcu_raw2"
#define CAL_DATA_OFFSET	0
#define FILE_OP_WRITE 1
#define FILE_OP_READ  0

#define CalLength	(24+80+6)
#define RawLength	40
typedef union {
	uint8_t Cal_val[CalLength];
	struct
	{
		int16_t Cal_offSetPoint[2];		//4 bytes
		int16_t Cal_gain[2];			//4 bytes
		float 	Cal_zeroBias;			//4 bytes
		float 	Cal_rotAngle[1];		//4 bytes
		uint16_t AKMThrd[4];			//8 bytes BOP1Y_15_0/BRP1Y_15_0/BOP1Z_15_0/BRP1Z_15_0.
		float 	Cal3CorrectionA[10][2];		//80 bytes
		int16_t Cal_firstMAGdata[3]; 	//6 bytes
	} CalParam;
}CAL_TABLE;

CAL_TABLE fcal_val;
CAL_TABLE fcal_val_UserK;
CAL_TABLE fcal_val_rPhone;
CAL_TABLE fcal_val_rMCU;

static int backup_mcu_cal_thed(void);
static int read_mcu_cal_thed(void);
static int Zen7_MSP430FR2311_wI2CtoMCU(uint8_t *buf, uint8_t len);
int Zen7_MSP430FR2311_rI2CtoCPU(uint8_t cmd, uint8_t *rBuf, uint8_t len);
static void bak_raw(void);
int ManualMode_AfterAndPR2_NOSS(int dir, int angle, int speed);

//user K
static char IsFileNull(const char *filename);
static void UserCaliReset(void);
static int Backup_user_cal_thed(void);
static int read_usek_cal3threshlod(void);
static int CompareCaliData(char *filename);

//rog6 k
int open_z=65536, open_x=65536, close_z=65536, close_x=65536;

#define bit0	0x01
#define bit1	0x02
#define bit2	0x04
#define bit3	0x08
#define bit4	0x10
#define bit5	0x20
#define bit6	0x40
#define bit7	0x80

/* MASK operation macros */
#define SET_MASK(reg, bit_mask) 	    ((reg)|=(bit_mask))
#define CLEAR_MASK(reg, bit_mask) 	    ((reg)&=(~(bit_mask)))
#define IS_MASK_SET(reg, bit_mask) 	    (((reg)&(bit_mask))!=0)
#define IS_MASK_CLEAR(reg, bit_mask)    (((reg)&(bit_mask))==0)
//Backup E

static int cal_cmd = 0;						//0:factory K; 1:user K.
static unsigned char FacOrUserClaFlg = 0;	//0:factory K; 1:user K.

static void waitDelayAndShowText(char * s) {
	int i=10;
	D("[MSP430][%s] (%s) command, wait for %d second.", __func__, s, i);
	return;
	
	for (i=10;i>0;i--) {
		printk(" %d...", i);
//			for (delayLoop=0;delayLoop<1000;delayLoop++)
//				udelay(500);
		msleep(500);
	}
	D("[MSP430][%s] issue\n", __func__);
}

int iProgrammingCounter=0;
int iProgrammingFail=0;
void dumpI2CData(char *s, uint8_t slave_addr, uint8_t* writeBuffer, uint32_t numOfWriteBytes ) {
	char buf[10];
	char line[1024];
	uint8_t loop_i;

	line[0]=0;
	for(loop_i=0; loop_i<numOfWriteBytes; loop_i++) {
		sprintf(buf, " %X", writeBuffer[loop_i]);
		strcat(line, buf);
	}

	printk("[MSP430][%s] %s [0x%X] : len=%d, %s", __func__, s, slave_addr, numOfWriteBytes, line);
}

bool MSP430_I2CWriteReadA(uint8_t slave_addr, uint8_t* writeBuffer, uint32_t numOfWriteBytes, uint8_t* readBuffer, uint32_t numOfReadBytes)
{
	uint8_t loop_i;
	__u8* rxDMA;
	__u8* txDMA;
	int ret = -1;

	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = numOfWriteBytes,
			.buf = writeBuffer,
		},
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = numOfReadBytes,
			.buf = readBuffer,
		},
	};

	if (!mcu_info->i2c_client->adapter)
		return false;

//	printk("[MSP430][%s] slave_addr = 0x%x, numOfWriteBytes = 0x%x, numOfReadBytes = 0x%x\n", __func__, slave_addr, numOfWriteBytes, numOfReadBytes);
	if (numOfWriteBytes>32||numOfReadBytes>=32) {
		rxDMA = kzalloc(sizeof(uint8_t)*numOfReadBytes, GFP_DMA | GFP_KERNEL);
		if (!rxDMA)
			return false;
		txDMA = kzalloc(sizeof(uint8_t)*numOfWriteBytes, GFP_DMA | GFP_KERNEL);
		if (!txDMA)
			return false;
		//memcpy(rxDMA, readBuffer, numOfReadBytes);
		memcpy(txDMA, writeBuffer, numOfWriteBytes);
		msgs[0].buf=txDMA;
		msgs[1].buf=rxDMA;
	}
	dumpI2CData("I2C_WR_w", msgs[0].addr,  writeBuffer, numOfWriteBytes);

	for(loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {		
		//if (i2c_transfer(mcu_info->i2c_client->adapter, msgs, 2) > 0)
		//	break;
		ret = i2c_transfer(mcu_info->i2c_client->adapter, msgs, 2);
//		printk("[MSP430][%s] i2c_transfer, ret %d\n", __func__, ret);
		if(ret > 0)
			break;

		/*check intr GPIO when i2c error*/
		//if (loop_i == 0 || loop_i == I2C_RETRY_COUNT -1)
		//	printk("[MSP430][%s] slaveAddr:0x%x, err %d, \n", __func__, slave_addr, ret);
		msleep(10);
	}
		
	if(loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[MSP430][%s] slaveAddr:0x%x, retry over %d, err %d\n", __func__, slave_addr, I2C_RETRY_COUNT, ret);
	
		if (numOfWriteBytes>32||numOfReadBytes>=32) {
			memcpy(readBuffer, rxDMA, numOfReadBytes);
			kfree(rxDMA);
			kfree(txDMA);				
		}

		dumpI2CData("I2C_WR_r i2c err,", msgs[1].addr, readBuffer, numOfReadBytes);
		return false;
	}

	if(numOfWriteBytes>32||numOfReadBytes>=32) {
		memcpy(readBuffer, rxDMA, numOfReadBytes);
		kfree(rxDMA);
		kfree(txDMA);
	}

	dumpI2CData("I2C_WR_r", msgs[1].addr, readBuffer, numOfReadBytes);
    return true;
}

bool MSP430_I2CWriteReadA_Nolog (uint8_t slave_addr, uint8_t* writeBuffer, uint32_t numOfWriteBytes, uint8_t* readBuffer, uint32_t numOfReadBytes)
{
	uint8_t loop_i;
	__u8* rxDMA;
	__u8* txDMA;
	int ret = -1;

	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = numOfWriteBytes,
			.buf = writeBuffer,
		},
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = numOfReadBytes,
			.buf = readBuffer,
		}, 
	};

	if (!mcu_info->i2c_client->adapter)
		return false;

//	printk("[MSP430][%s] slave_addr= 0x%x, numOfWriteBytes = 0x%x, numOfReadBytes = 0x%x\n", __func__, slave_addr, numOfWriteBytes, numOfReadBytes);
	if (numOfWriteBytes>32||numOfReadBytes>=32) {
		rxDMA = kzalloc(sizeof(uint8_t)*numOfReadBytes, GFP_DMA | GFP_KERNEL);
		if (!rxDMA)
			return false;
		txDMA = kzalloc(sizeof(uint8_t)*numOfWriteBytes, GFP_DMA | GFP_KERNEL);
		if (!txDMA)
			return false;
		//memcpy(rxDMA, readBuffer, numOfReadBytes);
		memcpy(txDMA, writeBuffer, numOfWriteBytes);
		msgs[0].buf=txDMA;
		msgs[1].buf=rxDMA;
	}
	
//	dumpI2CData("I2C_WR_w", msgs[0].addr,  writeBuffer, numOfWriteBytes);
	for(loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		//if (i2c_transfer(mcu_info->i2c_client->adapter, msgs, 2) > 0)
		//	break;
		ret = i2c_transfer(mcu_info->i2c_client->adapter, msgs, 2);
//		printk("[MSP430][%s] i2c_transfer, ret %d\n", __func__, ret);
		if(ret > 0)
			break;

		/*check intr GPIO when i2c error*/
		//if (loop_i == 0 || loop_i == I2C_RETRY_COUNT -1)
		//	printk("[MSP430][%s] slaveAddr:0x%x, err %d, \n", __func__, slave_addr, ret);
		msleep(10);
	}

	if(loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[MSP430][%s] slaveAddr:0x%x, retry over %d, err %d\n", __func__, slave_addr, I2C_RETRY_COUNT, ret);

		if(numOfWriteBytes>32 || numOfReadBytes>=32){
			memcpy(readBuffer, rxDMA, numOfReadBytes);
			kfree(rxDMA);
			kfree(txDMA);
		}

		dumpI2CData("I2C_WR_r i2c err,", msgs[1].addr, readBuffer, numOfReadBytes);
		return false;
	}

	if(numOfWriteBytes>32 || numOfReadBytes>=32) {
		memcpy(readBuffer, rxDMA, numOfReadBytes);
		kfree(rxDMA);
		kfree(txDMA);
	}

//	dumpI2CData("I2C_WR_r", msgs[1].addr, readBuffer, numOfReadBytes);
	return true;
}

bool MSP430_I2CWriteRead (uint8_t* writeBuffer, uint32_t numOfWriteBytes, uint8_t* readBuffer, uint32_t numOfReadBytes)
{
//	printk("[MSP430][%s] numberOfBytes = 0x%x, numOfReadBytes = 0x%x\n", __func__, numOfWriteBytes, numOfReadBytes);
	return MSP430_I2CWriteReadA_Nolog(mcu_info->slave_addr, writeBuffer, numOfWriteBytes, readBuffer, numOfReadBytes);
}

bool MSP430_I2CAction(struct i2c_msg* msgs, uint8_t slave_addr, uint8_t* buffer, uint32_t numberOfBytes)
{
	int ret= -1;
	uint8_t loop_i;

	if (!mcu_info->i2c_client->adapter) {
		printk("[MSP430][%s] adapter NULL!\n",__func__);
		return false;
	}
	if (!msgs) {
		printk("[MSP430][%s] msgs NULL!\n",__func__);
		return false;
	}

	//printk("[MSP430][%s] slave_addr = 0x%x, numberOfBytes = 0x%x\n", __func__, slave_addr, numberOfBytes);
	for(loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(mcu_info->i2c_client->adapter, msgs, 1) > 0)
			break;
/*
		ret = i2c_transfer(mcu_info->i2c_client->adapter, msgs, 1);
		printk("[MSP430][%s] i2c_transfer, ret %d\n", __func__, ret);
		if(ret > 0)
			break;
*/
		/*check intr GPIO when i2c error*/
		//if (loop_i == 0 || loop_i == I2C_RETRY_COUNT -1)
		//	printk("[MSP430][%s] slaveAddr:0x%x, err %d, \n", __func__, slave_addr, ret);
		msleep(10);
	}
	
	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[MSP430][%s] slaveAddr:0x%x, retry over %d, err %d\n", __func__, slave_addr, I2C_RETRY_COUNT, ret);
		return false;
	}

	return true;
}

bool MSP430_I2CRead(uint8_t slave_addr, uint8_t* buffer, uint32_t numberOfBytes)
{
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = numberOfBytes,
			.buf = buffer,
		},
	};

	//printk("[MSP430][%s] slave_addr= 0x%x, numberOfBytes = 0x%x\n", __func__, slave_addr, numberOfBytes);
	if( MSP430_I2CAction(msgs, slave_addr, buffer, numberOfBytes)) {
		dumpI2CData("I2C_r", slave_addr, buffer, numberOfBytes);
		return true;
	};
	return false;
}

bool MSP430_I2CRead_NoLog(uint8_t slave_addr, uint8_t* buffer, uint32_t numberOfBytes)
{
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = numberOfBytes,
			.buf = buffer,
		},
	};

	//printk("[MSP430][%s] slave_addr = 0x%x, numberOfBytes = 0x%x\n", __func__, slave_addr, numberOfBytes);
	if( MSP430_I2CAction(msgs, slave_addr, buffer, numberOfBytes)) {
		//dumpI2CData("I2C_r", slave_addr, buffer, numberOfBytes);
		return true;
	};
	return false;
}

bool MSP430_I2CWriteA(uint8_t slave_addr, uint8_t* buffer, uint32_t numberOfBytes)
{
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = numberOfBytes,
			.buf = buffer,
		},
	};

	//printk("[MSP430][%s] slave_addr = 0x%x, numberOfBytes = 0x%x\n", __func__, slave_addr, numberOfBytes);
	dumpI2CData("I2C_w", slave_addr, buffer, numberOfBytes);
	return MSP430_I2CAction(msgs, slave_addr, buffer, numberOfBytes);
}

bool MSP430_I2CWriteA_NoLog(uint8_t slave_addr, uint8_t* buffer, uint32_t numberOfBytes)
{
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = numberOfBytes,
			.buf = buffer,
		},
	};
	
	//printk("[MSP430][%s] slave_addr = 0x%x, numberOfBytes = 0x%x\n", __func__, slave_addr, numberOfBytes);
	//dumpI2CData("I2C_w", slave_addr, buffer, numberOfBytes);
	return MSP430_I2CAction(msgs, slave_addr, buffer, numberOfBytes);
}

bool MSP430_I2CWrite(uint8_t* buffer, uint32_t numberOfBytes)
{
//	printk("[MSP430][%s] numberOfBytes = 0x%x\n", __func__, numberOfBytes);
	return MSP430_I2CWriteA(mcu_info->slave_addr, buffer, numberOfBytes);
}

char gFWVersion[4];
//Manual mode.
static uint16_t LEAD_DELTA = 57;
uint16_t ConstSpeedMode[]={0, 120, 120, 120, 120, 120, 120, 255, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint16_t defaultManualSpeed[22] = {0, 0, 8, 73, 10, 73, 20, 73, 120, 0, 160, 0, 4, 73, 6, 73, 28, 73, 4, 73, 4, 73};	//For convenience match to IMS speed, fill byte0 and byte1.

//Factory note.
uint16_t TightenMode[]={1, 4, 4, 4, 4, 4, 4, 100, 0, 0, 0, 0, 0, 73, 73, 73, 73, 73, 73};
uint16_t SmallAngle[]={1, 4, 4, 4, 4, 4, 4, 6, 0, 0, 0, 0, 0, 73, 73, 73, 73, 73, 73};	//for camera apk

//Auto mode(0_180/180_0).
uint16_t ConvertFRQMode[][19]={
	{0, 120, 120, 56, 28, 20, 12, 85, 47, 2, 2, 2, 40, 0, 0, 73, 73, 73, 73},    //normal open
	{1, 120, 120, 56, 28, 20, 12, 85, 34, 2, 3, 3, 50, 0, 0, 73, 73, 73, 73},    //normal close
	{0, 120, 0, 0, 0, 0, 0, 225, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 254},             //fast open
	{1, 120, 0, 0, 0, 0, 0, 225, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 254},             //fast close
};

//Free angle, total step = FS + SS.	
static uint16_t CONVERT_FRQ_FS_STEP[]={310, 300};
static uint16_t CONVERT_FRQ_SS_STEP[]={40, 50};

//Angle < 10.
uint16_t ConvertFRQModeForSmallAngle[]={0, 8, 8, 8, 8, 8, 8, 50, 0, 0, 0, 0, 0, 73, 73, 73, 73, 73, 73};

//Drop mode.
uint16_t AutoEmergencyMode[] = {0, 120, 120, 160, 160, 180, 12, 150, 0, 100, 0, 90, 10, 0, 0, 0, 0, 0, 73};
uint16_t AutoWarmUpMode[] = {0, 80, 80, 80, 80, 80, 120, 255, 55, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0};
uint16_t AutoWarmUpMode2[] = {0, 120, 120, 120, 120, 120, 120, 255, 55, 0, 0, 0, 90, 0, 0, 0, 0, 0, 0};

//Main memory erase password.
uint8_t bslPassword[32] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,	\
						   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

//Motor stop condition and cali command.
uint16_t StopCondition[]={1, 179, 4, 0, 73, 4, 0, 73};
uint16_t CaliCmd[]={4, 2, 0};

static int parse_param(char* buf, const char* title, uint16_t* target, int count) {
	#define PARSE_DIG_PARAM_1 "%d"
	uint16_t cali_item[25];
	char parseLine[255];
	int i=0, matched_param=0;
	char value[50];

	snprintf(parseLine, sizeof(parseLine), "%s=%s", title, PARSE_DIG_PARAM_1);

	if (sscanf(buf, parseLine, &cali_item[i])>0) {
		target[i]=cali_item[i];
		matched_param++;
		snprintf(parseLine, sizeof(parseLine), "%s=" PARSE_DIG_PARAM_1, title, cali_item[i]);
		buf += strlen(parseLine);
//		printk("[MSP430][%s] detail %s\n", __func__, parseLine);

		for(i=1; i<count; i++) {
//			printk("[MSP430][%s] detail >>>%s\n", __func__, buf);
			if(sscanf(buf," %d", &cali_item[i])>0) {
				target[i]=cali_item[i];
				matched_param++;
				snprintf(parseLine, sizeof(parseLine),  " " PARSE_DIG_PARAM_1, cali_item[i]);
				buf += strlen(parseLine);
//				printk("[MSP430][%s] detail %s\n", __func__, parseLine);
			}else
				break;
		}

		//debug
		i = 0;
		if(matched_param) {
			snprintf(parseLine, sizeof(parseLine), "%s(%d)=" PARSE_DIG_PARAM_1, title, matched_param, cali_item[i]);
			for(i=1; i<matched_param; i++) {
				snprintf(value, sizeof(value), " " PARSE_DIG_PARAM_1, cali_item[i]);
				strncat(parseLine, value, sizeof(parseLine)-1);
			}
			printk("[MSP430][%s] read %s\n", __func__, parseLine);
		}
	}else {
//		printk("[MSP430][%s] No match %s\n", __func__, title);
	}

	return matched_param;
}

static void process_cali_item(char* buf, ssize_t bufLen) {
/*
AUTO_DEFAULT        =100 100 100 100 100 100 0 0 0 4 4 6
AUTO_EMERGENCY=100 100 100 100 100 100 0 0 0 4 4 6
MANUAL_DEFAULT=100 100 100 100 100 100 0 0 0 4 4 6
MANUAL_LEAD_DELTA=53
MANUAL_GEAR_RATIO=166/100
*/
	char* start_buf=buf;

	printk("[MSP430][%s] +\n", __func__);

	do {
		while(buf[0]==0xd || buf[0]==0xa) {
			buf++;
			//debug
			//D("[MSP430] >>> %s", buf);
		}

		parse_param(buf, "CONST_FRQ_SPEED", &ConstSpeedMode[1], 18);
		parse_param(buf, "CONVERT_FRQ_SMALL_ANGLE", &ConvertFRQModeForSmallAngle[1], 18);
		parse_param(buf, "CONVERT_FRQ_UP", &ConvertFRQMode[0][1], 18);
		parse_param(buf, "CONVERT_FRQ_DOWN", &ConvertFRQMode[1][1], 18);
		parse_param(buf, "CONVERT_FRQ_FAST_UP", &ConvertFRQMode[2][1], 18);
		parse_param(buf, "CONVERT_FRQ_FAST_DOWN", &ConvertFRQMode[3][1], 18);
		parse_param(buf, "AUTO_EMERGENCY", &AutoEmergencyMode[1], 18);
		parse_param(buf, "WARM_UP_STEP", &AutoWarmUpMode[1], 18);
		parse_param(buf, "WARM_UP_STEP_2", &AutoWarmUpMode2[1], 18);
		parse_param(buf, "MANUAL_LEAD_DELTA", &LEAD_DELTA, 1);
		parse_param(buf, "CONVERT_FRQ_UP_SS_STEP", &CONVERT_FRQ_SS_STEP[0], 1);
		parse_param(buf, "CONVERT_FRQ_DOWN_SS_STEP", &CONVERT_FRQ_SS_STEP[1], 1);
		parse_param(buf, "CONVERT_FRQ_UP_FS_STEP", &CONVERT_FRQ_FS_STEP[0], 1);
		parse_param(buf, "CONVERT_FRQ_DOWN_FS_STEP", &CONVERT_FRQ_FS_STEP[1], 1);
		parse_param(buf, "MANUAL_SPEED", &defaultManualSpeed[2], 20);
		parse_param(buf, "TIGHTEN_STEP", &TightenMode[1], 18);
		parse_param(buf, "STOP_CONDITION", &StopCondition[0], 8);
		parse_param(buf, "CALI_CMD", &CaliCmd[0], 3);
		parse_param(buf, "S_ANGLE", &SmallAngle[1], 18);

		//parse_param(buf, "PASSWORD", (uint16_t*)(&bslPassword[0]), 32);

		while(*buf != 0xa) {
			buf++;
			if(buf-start_buf > bufLen) {
				buf=NULL;
				break;
			}
		}

	} while(buf != NULL);

	printk("[MSP430][%s] -\n", __func__);
}

extern bool read_kernel_file(const char*, void (*)(char*, ssize_t) );
void read_cali_file(void) {
	#ifdef ASUS_FTM_BUILD
		const char mcu_cali[]= {"mcu_cali_factory"};
	#else
		const char mcu_cali[]= {"mcu_cali"};
	#endif

	printk("[MSP430][%s]\n", __func__);
	read_kernel_file(mcu_cali,  process_cali_item);
}

static uint8_t invokeString[8] = {0xCA, 0xFE, 0xDE, 0xAD, 0xBE, 0xEF, 0xBA, 0xBE};
static uint8_t resetVector[2];
static uint32_t resetVectorValue;

#include "i2cbsl.h"
#include "firmware_parser.h"

static int MSP43FR2311_Go_BSL_Mode(void) {

	printk("[MSP430][%s]\n", __func__);
	waitDelayAndShowText("simulator bsl protocol");
	
	gpio_set_value(mcu_info->mcu_reset, 0);
	msleep(20);
	gpio_set_value(mcu_info->mcu_test, 0);
	msleep(20);
	gpio_set_value(mcu_info->mcu_test, 1);
	msleep(20);
	gpio_set_value(mcu_info->mcu_test, 0);
	msleep(20);
	gpio_set_value(mcu_info->mcu_test, 1);
	msleep(20);
	gpio_set_value(mcu_info->mcu_reset, 1);
	msleep(20);
	gpio_set_value(mcu_info->mcu_test, 0);
	msleep(10);

	D("[MSP430] INFO: Invoking the BSL .\n");
	msleep(50);	//Wait mcu hw go to bsl mode ready.
	return MSP430BSL_invokeBSL(invokeString, 8);			
}

static int MSP43FR2311_Update_Firmware_Load_File(bool bLoadFromFile) {
	int res=0, uu=0, ii=0;
	int delayLoop;
	tMSPMemorySegment* tTXTFile=NULL;
	MSP430FR2311_power_control(1);

	for(delayLoop=0; delayLoop<3; delayLoop++) {
		res = MSP43FR2311_Go_BSL_Mode();
		if( res == MSP430_STATUS_OPERATION_OK ) {
			printk("[MSP430] Go to bsl mode successful!\n");
			break;
		}
	}
	
	if(res!= MSP430_STATUS_OPERATION_OK) {
		printk("[MSP430] Go to bsl mode fail!\n");
		//msleep(5000);
		goto BSLCleanUp;
	}

	D("[MSP430] INFO: opening mcu_firmware.txt... ");
	
	MCUState=MCU_PROGRAMMING;
	iProgrammingCounter++;
	if(bLoadFromFile) {
		tTXTFile = read_firmware_file();	//zen7 used.
	}else {
		tTXTFile = MSP430BSL_parseTextFile();
	}

	/* Sleeping after the invoke */
//	for (delayLoop=0;delayLoop<1000;delayLoop++)
//	udelay(2000);
#if 0 	
	/* Issuing a mass reset  */
	D("[MSP430] INFO: Issuing a mass reset\n");
	if(MSP430BSL_massErase() != MSP430_STATUS_OPERATION_OK)
	{
		printk("[MSP430][%s] ERROR: Could not issue mass erase!\n", __func__);
		for(delayLoop=0;delayLoop<1000;delayLoop++)
			udelay(5000);

		goto BSLCleanUp;
	}

	/* Sleeping after the mass reset */
	for(delayLoop=0;delayLoop<1000;delayLoop++)
		udelay(2000);

	/* Unlocking the device */
#endif
#if 1	
	//memset(bslPassword, 0xFF, 32);
	for(ii=0; ii<3; ii++) {
		D("[MSP430] INFO: Unlocking the device \n");
		waitDelayAndShowText("Erase main memory");
		if(MSP430BSL_unlockDevice(bslPassword) != MSP430_STATUS_OPERATION_OK )
		{
			printk("[MSP430][%s] unlock device Cnt:%d\n", __func__, ii);	
			if(ii >= 3)	
				printk("[MSP430][%s] ERROR: Could not unlock device! Cnt:%d\n", __func__, ii);

//			goto BSLCleanUp;
		}
	}
#endif

	waitDelayAndShowText("Programming all");
	/* Programming all memory segments */
	for(uu=0; uu<5; uu++) {
		D("[MSP430] INFO: Programming attempt number %d\n", uu);

//		tTXTFile = firmwareImage;
		while(tTXTFile != NULL) {
			printk("[MSP430] MCU txtfile assign to %p", tTXTFile);
			D("[MSP430] INFO: Programming @0x%x with %d bytes of data...0x%X, 0x%X, 0x%X ", 
						tTXTFile->ui32MemoryStartAddr, 
						tTXTFile->ui32MemoryLength,
						tTXTFile->ui8Buffer[0],
						tTXTFile->ui8Buffer[1],
						tTXTFile->ui8Buffer[2]
			);

			/* Programming the memory segment */
			ii = 0;
			while(ii < tTXTFile->ui32MemoryLength) {
				if((tTXTFile->ui32MemoryLength - ii) > 128) {
					res = MSP430BSL_sendData(tTXTFile->ui8Buffer + ii, tTXTFile->ui32MemoryStartAddr + ii, 128);
					ii += 128;
				}else {
					res = MSP430BSL_sendData(tTXTFile->ui8Buffer + ii, tTXTFile->ui32MemoryStartAddr + ii, (tTXTFile->ui32MemoryLength - ii));
					ii = tTXTFile->ui32MemoryLength;
				}

				if(res != MSP430_STATUS_OPERATION_OK){
					printk("[MSP430][%s] FAIL!ERROR: Programming address 0x%x (Code 0x%x).\n", __func__, tTXTFile->ui32MemoryStartAddr + ii, res);
					break;
				}
			}

			if(res != MSP430_STATUS_OPERATION_OK)
				break;

			D("[MSP430] done!\n");	
			tTXTFile = tTXTFile->pNextSegment;
		}

		if(res == MSP430_STATUS_OPERATION_OK)
		{
			D("[MSP430] INFO: Programmed all memory locations successfully.\n");
			break;
		}else {
		}
	}

	if(uu>5)
		goto BSLCleanUp;

	/* Resetting the device */
	D("[MSP430] INFO: Resetting the device.\n");
	res = MSP430BSL_readData(resetVector, MSP430_RESET_VECTOR_ADDR, 2);

	if(res != MSP430_STATUS_OPERATION_OK)
	{
		printk("[MSP430][%s] ERROR: Could not read reset vector address!\n", __func__);
		msleep(5000);
		goto BSLCleanUp;
	}

	resetVectorValue = (resetVector[1] << 8) | resetVector[0]; 
	D("[MSP430] INFO: Reset vector read as 0x%x\n", resetVectorValue);
	res = MSP430BSL_setProgramCounter(resetVectorValue);
	
	if(res != MSP430_STATUS_OPERATION_OK) {
		printk("[MSP430][%s] ERROR: Could not set program counter!\n", __func__);
		msleep(5000);
		goto BSLCleanUp;
	}

	printk("[MSP430][%s] INFO: Firmware updated without issue, %d/%d.\n", __func__, iProgrammingFail, iProgrammingCounter);

	//If update FW success, clear fail count.
	iProgrammingFail = 0;

	D("[MSP430] INFO: power off the device.\n");
	MSP430FR2311_power_control(0);

	msleep(5);
	printk("[MSP430][%s] INFO: re-power on the device.\n", __func__);
	MSP430FR2311_power_control(1);

	//copy file to asdf folder.
	//report_motor_event(MOTOR_COPY_MCU_FILE, 2);	// /factory/mcu_bak --> asdf.
	msleep(3000);	//Delay to wait MCU i2c slave init ok.

	//Re-write backup calibration and threadhold data to MCU.
	MCUState=MCU_READY; 	//Cmd 0xbf logic also will set MCU_READY when this function end, so can set MCU_READY early here.
	//read_mcu_cal_thed();
	if(IsFileNull(MCU_BAKEUP_USER_FILE_NAME) == 1) {
		read_usek_cal3threshlod();	//read motor_fw2 file to mcu.
	}else {
		read_mcu_cal_thed();		//read factory file to mcu.
	}
	
	return res;

BSLCleanUp:
//	MSP430BSL_cleanUpPointer(firmwareImage);
	iProgrammingFail++;
	printk("[MSP430][%s] INFO: Programmed fail, %d/%d.\n", __func__, iProgrammingFail, iProgrammingCounter);

	//UPDATE_FW_RETRY_COUNT
	if(iProgrammingFail >= UPDATE_FW_RETRY_COUNT){
		printk("[MSP430][%s] Try 3 times to update new FW, the result is also fail.\n", __func__);
		MCUState = MCU_READY;	//although update fail, but need let user can control MCU.
		iProgrammingFail = 0;	//Clear for next time also can try 3 times.
	}
	
	//Zen7, power off/on when go to bsl mode fail.
	MSP430FR2311_power_control(0);

	msleep(5);
	printk("[MSP430][%s] re-power on the device when run bsl logic fail.\n", __func__);
	MSP430FR2311_power_control(1);

	msleep(200);

	if(iProgrammingFail < UPDATE_FW_RETRY_COUNT) {
		printk("[MSP430][%s] Re-try flash.\n", __func__);
		MCUState = MCU_PROGRAMMING_RETRY;	
	}

	return res;
}

static int MSP43FR2311_Update_Firmware(void) {
	int rc=0;

	//if (g_ASUS_hwID == HW_REV_ER ) return MSP43FR2311_Update_Firmware_Load_File(0);
	printk("[MSP430][%s]\n", __func__);

	rc=MSP43FR2311_Update_Firmware_Load_File(1);
	if(rc != MSP430_STATUS_INVOKE_FAIL) {
			MSP430BSL_cleanUpPointer();
	} 
	return rc;
}

int MSP430FR2311_Get_Steps(void) {
	char getsteps[] = { 0xAA, 0x55, 0x10};
	char steps[] = { 0, 0};
//	int i=0;

	printk("[MSP430][%s]\n", __func__);
//	for(i=0;i<2;i++, msleep(10)) 
	if( !MSP430_I2CWriteA(MSP430_READY_I2C, getsteps, sizeof(getsteps)) || !MSP430_I2CRead(MSP430_READY_I2C, steps, sizeof(steps)) ) {
		printk("[MSP430][%s] I2C error!\n", __func__);
		return -1;
	}

	printk("[MSP430][%s] Current Steps = %d\n", __func__, steps[1] << 1);
	return steps[1];
}

void MSP430FR2311_wakeup(uint8_t enable) {

	//printk("[MSP430][%s] %d\n",__func__, enable);
	if(enable) {
		//power up
		iOpenCounter++;
		if(iOpenCounter==1) {
			//if (g_ASUS_hwID == HW_REV_ER  || g_ASUS_hwID == HW_REV_SR) {
			//} else {
			//	gpio_set_value(mcu_info->mcu_wakeup, 0);
				//msleep(1);
			//}
		}
	}else {
		iCloseCounter++;
		if(iCloseCounter==1) {
			queue_delayed_work(mcu_info->mcu_wq, &report_work, msecs_to_jiffies(powerDownDuration));
		}else {
			mod_delayed_work(mcu_info->mcu_wq, &report_work, msecs_to_jiffies(powerDownDuration));
		}
	}
}

int MSP430FR2311_Get_Version(char * version) {
	char i2cfwversion[] = {0xAA, 0x55, 0x0A};
	unsigned char i;
	
	printk("[MSP430][%s]\n",__func__);
	MSP430FR2311_wakeup(1);
	for(i=0; i<3; i++) {
		if( !MSP430_I2CWriteA(MSP430_READY_I2C, i2cfwversion, sizeof(i2cfwversion)) || !MSP430_I2CWriteReadA(MSP430_READY_I2C, i2cfwversion, sizeof(i2cfwversion), version, 4)) {
			printk("[MSP430][%s] I2C error!", __func__);
			MSP430FR2311_wakeup(0);
			return -1;
		}

		if(gFWVersion[0] == 20){	//Compare with 20 because make sure that: MCU version format in fw is 20 xx xx xx.
			i = 3;
		}else {
			printk("[MSP430] get error fw version: 0x%02x 0x%02x 0x%02x 0x%02x.\n", gFWVersion[0], gFWVersion[1], gFWVersion[2], gFWVersion[3]);
		}
	}
	MSP430FR2311_wakeup(0);

	printk("[MSP430][%s] get fw version: %02d %02d %02d %02d\n", __func__, (int)gFWVersion[0], (int)gFWVersion[1], (int)gFWVersion[2], (int)gFWVersion[3]);
	return 0;
}

//#define MCU_SHOW_INFO_IN_SETTING
#ifdef MCU_SHOW_INFO_IN_SETTING
//#include "../power/supply/qcom/fg-core.h"
//#include "../power/supply/qcom/fg-reg.h"
//#include "../power/supply/qcom/fg-alg.h"
#include <linux/extcon-provider.h>

//extern void asus_extcon_set_fnode_name(struct extcon_dev *edev, const char *fname);
//extern void asus_extcon_set_name(struct extcon_dev *edev, const char *name);

struct extcon_dev *mcu_ver_extcon;
char mcuVersion[13];
static const unsigned int asus_motor_extcon_cable[] = {
	EXTCON_NONE,
};

void registerMCUVersion(void) {
	int rc=0;

	mcu_ver_extcon = extcon_dev_allocate(asus_motor_extcon_cable);
	if(IS_ERR(mcu_ver_extcon)) {
		rc = PTR_ERR(mcu_ver_extcon);
		printk("[MSP430][%s] failed to allocate ASUS mcu_ver_extcon device rc=%d\n", __func__, rc);
	}
	//asus_extcon_set_fnode_name(mcu_ver_extcon, "mcu");

	rc = extcon_dev_register(mcu_ver_extcon);
	if (rc < 0) {
		printk("[MSP430][%s] failed to register ASUS mcu_ver_extcon device rc=%d\n", __func__, rc);
	}
	sprintf(mcuVersion, "%d%02d%02d%02X",  gFWVersion[0],gFWVersion[1],gFWVersion[2],gFWVersion[3]);
	//asus_extcon_set_name(mcu_ver_extcon, mcuVersion);
}
#endif

int MSP430FR2311_Check_Version(void) {

	printk("[MSP430][%s]\n", __func__);

	memset(gFWVersion, 0x0, sizeof(gFWVersion));
	if(MSP430FR2311_Get_Version(gFWVersion) == MSP430_STATUS_OPERATION_OK) {
		tMSPMemorySegment* tTXTFile = NULL;
		
		//if (g_ASUS_hwID == HW_REV_ER) {
		//	tTXTFile = MSP430BSL_parseTextFile();
		//} else {
			tTXTFile = read_firmware_file();	//zen7 used.
		//}
		if(tTXTFile == NULL) {
			printk("[MSP430][%s] read firmware file error, can not to check firmware version\n", __func__);
			
//			printk("[MSP430][%s] seLinux security issue, workaround update firmware manually\n", __func__);
//			MCUState=MCU_READY;			
//			g_motor_status = 1; //probe success
			
			return MSP430_STATUS_TXTFILE_ERROR;
		}

		printk("[MSP430][%s] Firmware version=%d%02d%02d%02X, new version=%d%02d%02d%02X\n", __func__,
			gFWVersion[0], gFWVersion[1], gFWVersion[2], gFWVersion[3],
			tTXTFile->ui8Buffer[0],
			tTXTFile->ui8Buffer[2],
			tTXTFile->ui8Buffer[4],
			tTXTFile->ui8Buffer[6]);

		if (tTXTFile->ui8Buffer[1]||
			tTXTFile->ui8Buffer[3]||
			tTXTFile->ui8Buffer[5]||
			tTXTFile->ui8Buffer[7]) {
			printk("[MSP430][%s] Firmware formate is incorrect\n", __func__); 					
		}
		
		if (
			tTXTFile->ui8Buffer[0]*100000
			+tTXTFile->ui8Buffer[2]*1000
			+tTXTFile->ui8Buffer[4]*10
			+tTXTFile->ui8Buffer[6] ==
			gFWVersion[0]*100000
			+gFWVersion[1]*1000 
			+gFWVersion[2]*10 
			+gFWVersion[3]
			) {

			MCUState=MCU_READY;	//loopCounter=2 and MCUState=4, switch to MCU_READY.			
			g_motor_status = 1; //probe success
			printk("[MSP430][%s] fw is newest, not need to update.\n", __func__); 
			
			//if (g_ASUS_hwID != HW_REV_ER) {
				read_cali_file();
			//}

#ifdef MCU_SHOW_INFO_IN_SETTING
			registerMCUVersion();
#endif			
		}else {
			printk("[MSP430][%s] Firmware need to be updated.\n", __func__); 
		}

		//if (g_ASUS_hwID != HW_REV_ER) {
			MSP430BSL_cleanUpPointer();
		//} 

	} else {
		printk("[MSP430][%s] Firmware version get error.\n", __func__);
		return MSP430_STATUS_I2C_NOT_FOUND;
	}
	return 0;
}

int loopCounter=0;
int totalLoopCounter=0;

void mcu_loop_test(void) {
	int delay=1500;

//	printk("[MSP430] do loop test, loop=%d, wake up=%d", loop_i, loop_i&1);
//	gpio_set_value(mcu_info->mcu_wakeup, loop_i&1);

	printk("[MSP430][%s] do loop test, loop = %d/%d", __func__, loopCounter, totalLoopCounter);
	AutoEmergencyMode[0] = 0;
	MSP430FR2311_Set_ParamMode(AutoEmergencyMode);
	if(AutoEmergencyMode[1] < 600) {
		delay = delay*600/AutoEmergencyMode[1];
	}

	msleep(delay);
	AutoEmergencyMode[0]=1;
	MSP430FR2311_Set_ParamMode(AutoEmergencyMode);
	msleep(delay);
}

int MSP430FR2311_Pulldown_Drv_Power(void) {
	/*
	char MSP430PullDownDrvMode[]={0xAA, 0x55, 0x0E, 0x00};	

	if(MCUState<MCU_READY) {
		printk("[MSP430] Not ready!, state=%d", MCUState);
		return -MCUState;
	}
	if(!MSP430_I2CWriteA(MSP430_READY_I2C, MSP430PullDownDrvMode, sizeof(MSP430PullDownDrvMode))) {
		printk("[MSP430][%s] I2C error!", __func__);
		return -1;
	}
	*/
	//D("[MSP430FR2311][%s]\n", __func__);
	return 0;
}


void mcu_do_later_power_down(void) {
	//printk("[MSP430][%s]\n", __func__);

	mutex_lock(&MSP430FR2311_control_mutex);
	if(iCloseCounter != 0) {
		MSP430FR2311_Pulldown_Drv_Power();
//		gpio_set_value(mcu_info->mcu_wakeup, 1);
		printk("[MSP430][%s] motor power down, OpenClient=%d, CloseClient=%d", __func__, iOpenCounter, iCloseCounter);	
		iCloseCounter=0;
		iOpenCounter=0;
	} else {
		printk("[MSP430][%s] motor power down Ignore, OpenClient=%d, CloseClient=%d", __func__, iOpenCounter, iCloseCounter);	
	}
	mutex_unlock(&MSP430FR2311_control_mutex);		
}

static unsigned char scinitFlg = 0;
static void mcu_do_work_later(struct work_struct *work)
{
	printk("[MSP430][%s] loopCounter=%d, state=%d.\n", __func__, loopCounter, MCUState);
	loopCounter++;

	//Patch for EVB board, reduce redundancy log. Need remove in furture.
	if((loopCounter >= 5) && (MCUState == MCU_EMPTY)){
		printk("[MSP430][%s] quit.\n", __func__);
		return;
	}

	if(MCUState != MCU_READY) {
		if(MCUState == MCU_EMPTY) {
			if(MSP430FR2311_Check_Version()) {
				printk("[MSP430][%s] Fail.\n", __func__);
			}else {
				if(MCUState == MCU_READY) {		//If first time check FW don't need update.
					cancel_delayed_work(&report_work);
					queue_delayed_work(mcu_info->mcu_wq, &report_work, 100);	//Cue to run mStopConditionInit().
					return;
				}
			}
		}

		if(MCUState == MCU_CHECKING_READY) {
			if( (MSP430FR2311_Check_Version() != 0) && (loopCounter < UPDATE_FW_RETRY_COUNT)) {	//if this condition true, no any help in this handle.
				MCUState=MCU_PROGRAMMING;
				queue_delayed_work(mcu_info->mcu_wq, &report_work, mcu_info->mcu_polling_delay);
			}else {
				if(MCUState == MCU_READY){		//If check FW has been update success.
					cancel_delayed_work(&report_work);
					queue_delayed_work(mcu_info->mcu_wq, &report_work, 100);	//Cue to run mStopConditionInit().
					return;
				}
			}
			return;
		}
		/*
		if(MCUState == MCU_WAIT_POWER_READY) {
			D("[MSP430] power ready");
			MCUState = MCU_READY;
			return;
		}

		if(MCUState == MCU_LOOP_TEST) {
			//loop i2c
			if(loopCounter < totalLoopCounter) {
				mcu_loop_test();
				queue_delayed_work(mcu_info->mcu_wq, &report_work, mcu_info->mcu_polling_delay/2); //60 secs
			}else {
				MCUState = MCU_READY;
			}
			return;
		}*/

		if(MCUState == MCU_PROGRAMMING){
			cancel_delayed_work(&report_work);
			queue_delayed_work(mcu_info->mcu_wq, &report_work, msecs_to_jiffies(5000));	//Patch for 191 cmd.
			return;
		}else {
			if((MCUState != MCU_READY) && (MSP43FR2311_Update_Firmware() == 0)) {	//If update FW fail, will retry 3 times in function MSP43FR2311_Update_Firmware().
				MCUState = MCU_CHECKING_READY;
			}
		}

		//finally, we do not update firmware successfully, do this again
		if((MCUState <= MCU_CHECKING_READY) && (loopCounter < UPDATE_FW_RETRY_COUNT)) {
			cancel_delayed_work(&report_work);
			queue_delayed_work(mcu_info->mcu_wq, &report_work, 100);	//Let MCUState change to MCU_READY in next cycle (function: MSP430FR2311_Check_Version()).
		}else {
			printk("[MSP430] FATAL, mcu firmware update fail!!!! loopCounter=%d, state=%d.\n", loopCounter, MCUState);
		}
	}else {
		printk("[MSP430][%s] scinitFlg = %d.\n", __func__, scinitFlg);
		if(scinitFlg == 0) {
			scinitFlg = 1;	//Only init once when MCU_READY.
			mStopConditionInit();
		}
		mcu_do_later_power_down();
	}
}

static int mcu_open(struct inode *inode, struct file *file)
{
	//D("[MSP430][%s]\n", __func__);
	return 0;
}

static int mcu_release(struct inode *inode, struct file *file)
{
	//D("[MSP430][%s]\n", __func__);
	return 0;
}

static long mcu_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int auto_mode = 0;
	int ret = 0;
	char nameMotor[ASUS_MOTOR_NAME_SIZE];
	motorDrvManualConfig_t data;
	motorCal gAngle;
	uint8_t wBuf[9] = {0xAA, 0x55, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	//ALLEN: float gPrecision = 3.456; 
	unsigned int gPrecision = 0;
	unsigned int sbuf[2];
	MICRO_STEP microstep;
	int special_angle = 0;
	int JudgeStopConditionFlg = 0;	
	int UserKThreshold = 0;

	printk("[MSP430][%s] cmd %d\n", __func__, _IOC_NR(cmd));

	switch(cmd)
	{
		case ASUS_MOTOR_DRV_AUTO_MODE:
			//if(openclose_worker_running) {
			//	printk("[MSP430][%s] Openclose Work is running %d, Skip this command.\n", __func__, openclose_worker_running);
			//	goto end;
			//}

			ret = copy_from_user(&auto_mode, (int __user*)arg, sizeof(auto_mode));
			//printk("[MSP430][%s] auto_mode:%d.\n", __func__, auto_mode);
			if(ret < 0) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_DRV_AUTO_MODE, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}

			ret = MSP430FR2311_Set_AutoMode(auto_mode);
			if(ret < 0)
				printk("[MSP430] Set AutoMode failed\n");

			openclose = auto_mode;
			openclose_check();
			break;

		case ASUS_MOTOR_DRV_MANUAL_MODE:
			ret = copy_from_user(&data, (int __user*)arg, sizeof(data));
			if(ret < 0) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_DRV_MANUAL_MODE, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}

			printk("[MSP430][%s] manual mode dir:%d, angle:%d, speed:%d.\n",  __func__, data.dir, data.angle, data.speed);
			ret = MSP430FR2311_Set_ManualMode(data.dir, data.angle, data.speed);
			if(ret < 0)
				printk("[MSP430][%s] Set ManualMode failed\n", __func__);

			break;

		case ASUS_MOTOR_DRV_MANUAL_MODE_NOSS:
			ret = copy_from_user(&data, (int __user*)arg, sizeof(data));
			if(ret < 0) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_DRV_MANUAL_MODE_NOSS, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}

			printk("[MSP430][%s] NOSS manual mode dir:%d, angle:%d, speed:%d.\n",  __func__, data.dir, data.angle, data.speed);
			ret = ManualMode_AfterAndPR2_NOSS(data.dir, data.angle, data.speed);
			if(ret < 0)
				printk("[MSP430][%s] Set NOSS ManualMode failed\n", __func__);

			break;

		case ASUS_MOTOR_DRV_AUTO_MODE_WITH_ANGLE:
			ret = copy_from_user(&data, (int __user*)arg, sizeof(data));
			if(ret < 0 )
			{
				printk("[MSP430][%s] cmd = ASUS_MOTOR_DRV_MANUAL_MODE, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}
			ret = MSP430FR2311_Set_AutoModeWithAngle(data.dir, data.angle);
			if(ret < 0)
				printk("[MSP430][%s] Set AutoModeWithAngle failed\n", __func__);
		
			break;

		case ASUS_MOTOR_DRV_STOP:
			ret = MSP430FR2311_Stop();
			if(ret < 0)
				printk("[MSP430][%s] Stop Motor failed\n", __func__);
			break;
			
		case ASUS_MOTOR_DRV_GET_STEPS:
			ret = MSP430FR2311_Get_Steps();
			if(ret < 0) {
				printk("[MSP430][%s] Get Motor steps failed\n", __func__);
				goto end;
			}

			ret = copy_to_user((int __user*)arg, &ret, sizeof(ret));
			break;

		case ASUS_MOTOR_DRV_GET_DOOR_STATE:
			ret = judgeDoorState(0);
			printk("[MSP430][%s] Door state = %d\n", __func__, ret);
			ret = copy_to_user((int __user*)arg, &ret, sizeof(ret));
			break;

		case ASUS_MOTOR_DRV_GET_NAME:
		    snprintf(nameMotor, sizeof(nameMotor), "%s", ASUS_MOTOR_DRV_DEV_PATH);
			D("[MSP430][%s] cmd = MODULE_NAME, name = %s\n", __func__, nameMotor);
			ret = copy_to_user((int __user*)arg, &nameMotor, sizeof(nameMotor));
			break;
			
		case ASUS_MOTOR_DRV_CLOSE:
			D("[MSP430][%s] ASUS_MOTOR_DRV_CLOSE+++, ask mcu power down immediately\n", __func__);			
			flush_delayed_work(&report_work);
			D("[MSP430][%s] ASUS_MOTOR_DRV_CLOSE---, ask mcu power down immediately\n", __func__);			
			break;

		case ASUS_MOTOR_DRV_SET_ANGLE:
			ret = copy_from_user(&gAngle, (int __user*)arg, sizeof(gAngle));
			if(ret < 0) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_DRV_SET_ANGLE, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}
			
			printk("[MSP430][%s] phone set angle:%d %d.\n",  __func__, gAngle.integer, gAngle.decimals);
			wBuf[2] = 0x81;
			wBuf[3] = (gAngle.integer >> 8);
			wBuf[4] = (gAngle.integer&0x00FF);
			wBuf[5] = (gAngle.decimals >> 8);
			wBuf[6] = (gAngle.decimals&0x00FF);
			ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, sizeof(wBuf));
			if(ret < 0)
				printk("[MSP430] Set angle to mcu failed\n");

			break;

		case ASUS_MOTOR_CALIBRATION:
			ret = copy_from_user(&cal_cmd, (int __user*)arg, sizeof(cal_cmd));			
			if(ret < 0) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_CALIBRATION, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}

			if(cal_cmd == 0)
				FacOrUserClaFlg = 0;	//Fac
			else
				FacOrUserClaFlg = 1;	//User

			printk("[MSP430][%s] phone set calibration:%d %d.\n", __func__, cal_cmd, FacOrUserClaFlg);

			wBuf[2] = 0x61;
			wBuf[3] = CaliCmd[0];
			wBuf[4] = CaliCmd[1];
			wBuf[5] = CaliCmd[2];
			ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, 6);		//2header + 4bytes.
			if(ret < 0) {
				UserKState = UserK_TBD;
				FacOrUserClaFlg = 0;	//default factory K.
				printk("[MSP430] Motor calibration failed.\n");
			}else {
				if(FacOrUserClaFlg == 1)	//Need add judge condition (&& UserKState == UserK_TBD)  ????
					UserKState = UserK_Start;
			}
			break;

		case ASUS_MOTOR_GET_MICRO_STEP:
		    //snprintf(nameMotor, sizeof(nameMotor), "%s", ASUS_MOTOR_DRV_DEV_PATH);
		    //ALLEN: gPrecision = 0.144*4*SmallAngle[7];
			gPrecision = 144*4*SmallAngle[7];
			sbuf[0] = (unsigned int)gPrecision/1000;
			sbuf[1] = gPrecision%1000;

			printk("[MSP430][%s] min_angle:%d.%d\n", __func__, sbuf[0], sbuf[1]);
			ret = copy_to_user((int __user*)arg, &sbuf, sizeof(microstep));
			break;

		case ASUS_MOTOR_SPECIAL_ANGLE:
			ret = copy_from_user(&special_angle, (int __user*)arg, sizeof(special_angle));
			printk("[MSP430][%s] phone set special_angle:%d.\n", __func__, special_angle);
			if(ret < 0 ){
				printk("[MSP430][%s] cmd = ASUS_MOTOR_SPECIAL_ANGLE, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}
			
			wBuf[2] = 0xFE;
			wBuf[3] = special_angle;
			ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, 4);		//2header + 2bytes.
			if(ret < 0)
				printk("[MSP430] Motor special_angle failed.\n");
			break;

		case ASUS_MOTOR_RESET_USER_CAL:
			printk("[MSP430][%s] phone reset calibration value, UserKState:%d.\n", __func__, UserKState);
			UserKState = UserK_Reset;
			queue_delayed_work(mcu_info->cal_wq, &cal_work, 20);  //20ms
			break;

		case ASUS_MOTOR_SAVE_USER_CAL:
			printk("[MSP430][%s] phone save calibration value, UserKState:%d.\n", __func__, UserKState);

			if(UserKState == UserK_rMCU_Ready) {
				UserKState = UserK_SaveK;
				queue_delayed_work(mcu_info->cal_wq, &cal_work, 20);	
			}
			break;

		case ASUS_MOTOR_NOTSAVE_USER_CAL:
			printk("[MSP430][%s] phone don't save calibration value, UserKState:%d.\n", __func__, UserKState);
			if(UserKState == UserK_rMCU_Ready) {
				UserKState = UserK_NoSaveK;
				queue_delayed_work(mcu_info->cal_wq, &cal_work, 20);	
			}
			break;
		
		case ASUS_MOTOR_ABROT_USER_CAL:
			printk("[MSP430][%s] phone abrot calibration, UserKState:%d.\n", __func__, UserKState);
			if(UserKState == UserK_Start) {
				UserKState = UserK_AbortK;
				queue_delayed_work(mcu_info->cal_wq, &cal_work, 20);	
			}
			break;	

		case ASUS_MOTOR_JUDGEANGLECONDITION:
			ret = copy_from_user(&JudgeStopConditionFlg, (int __user*)arg, sizeof(JudgeStopConditionFlg));			
			if(ret < 0 ) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_JUDGEANGLECONDITION, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}

			wBuf[2] = 0x87;
			wBuf[3] = JudgeStopConditionFlg;	//1:MCU will not use judge angle to stop motor rotate.  0:use judge angle to stop motor.
			ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, 4); 	//2header + 2bytes.
			if(ret < 0)
				printk("[MSP430] ioctl disable judge angle failed.\n");

			printk("[MSP430][%s] phone set jasc:%d, %s jasc function.\n", __func__, JudgeStopConditionFlg, JudgeStopConditionFlg?"not use":"use");
			break;

		case ASUS_MOTOR_SETUSERKTHRESHOLD:
			ret = copy_from_user(&UserKThreshold, (int __user*)arg, sizeof(UserKThreshold));			
			if(ret < 0) {
				printk("[MSP430][%s] cmd = ASUS_MOTOR_SETUSERKTHRESHOLD, copy_from_user error(%d)\n", __func__, ret);
				goto end;
			}

			wBuf[2] = 0x88;
			wBuf[3] = UserKThreshold;	
			ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, 4); 	//2header + 2bytes.
			if(ret < 0)
				printk("[MSP430] UserKThreshold write failed.\n");

			printk("[MSP430][%s] phone set UserKThreshold:%d.\n", __func__, UserKThreshold);
			break;

		case ASUS_MOTOR_MCU_STATE:
			ret = MCUState;
			ret = copy_to_user((int __user*)arg, &ret, sizeof(ret));

			printk("[MSP430][%s] phone get mcu state:%d, ret:%d.\n", __func__, MCUState, ret);
			break;

		default:
			printk("[MSP430][%s] invalid cmd %d\n", __func__, _IOC_NR(cmd));
			return -EINVAL;
	}
end:
	return ret;
}

static const struct file_operations mcu_fops = {
	.owner = THIS_MODULE,
	.open = mcu_open,
	.release = mcu_release,
	.unlocked_ioctl = mcu_ioctl
};

struct miscdevice mcu_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "asusMotoDrv",
	.fops = &mcu_fops
};

static int mcu_setup(void)
{
	int ret;
	printk("[MSP430][%s]\n", __func__);

	ret = misc_register(&mcu_misc);
	if(ret < 0) {
		printk("[MSP430][%s] could not register mcu misc device\n", __func__);
	}

	return ret;
}

#define F_Status_AKMAngleChange			bit0
#define F_Status_MotorRoateState		bit1
#define F_Status_AKMThreadInt	 		bit2
#define F_Status_MotorKMCUCal	 	 	bit3
#define F_Status_MotorKMCUThred			bit4
#define F_Status_MotorRoateForceStop	bit5	//#define F_Status_DrvInt	 	 			bit5
#define F_Status_ReqG_Angle 			bit6
#define F_Status_ReqG_Timeout			bit7

#define extract_mcu_len	2
static void extract_mcu_data(void){
	unsigned char rBuf[6] = {0,0,0,0,0,0};
	uint8_t CmdBuf[3] = {0xAA, 0x55, 0xE0};
	uint8_t EventState = 0;

	//printk("[MSP430][%s]\n",__func__);
	CmdBuf[2] = 0xE0;
	if(!bPowerDone) {
		printk("[MSP430][%s] opening power not done, ignore this interrupt!\n", __func__);
		return;
	}

	if (!MSP430_I2CWriteA_NoLog(MSP430_READY_I2C, CmdBuf, sizeof(CmdBuf)) || !MSP430_I2CRead_NoLog(MSP430_READY_I2C, rBuf, extract_mcu_len)) {
		printk("[MSP430][%s] i2c read error!\n", __func__);
		return;
	}else {
		EventState = rBuf[0];

		if(IS_MASK_SET(EventState, F_Status_AKMAngleChange)){		//report angle.
			CmdBuf[2] = 0xE1;
			if(!MSP430_I2CWriteA_NoLog(MSP430_READY_I2C, CmdBuf, sizeof(CmdBuf)) || !MSP430_I2CRead_NoLog(MSP430_READY_I2C, rBuf, 6)) {
				printk("[MSP430][%s] cmd:0xE1 i2c read error!\n", __func__);
			}else {
				//report_motor_event(MOTOR_ANGLE, ((rBuf[2]<<24) | (rBuf[3]<<16) | (rBuf[4]<<8)| rBuf[5]));
			}
			//printk("[MSP430][%s] angle:%x.\n", __func__, ((rBuf[2]<<24) | (rBuf[3]<<16) | (rBuf[4]<<8)| rBuf[5]));
		}

		if(IS_MASK_SET(EventState, F_Status_MotorRoateState)) {		//roate finish.
//			report_motor_event(MOTOR_ROTATE, ROTATE_FINISH);
			printk("[MSP430][%s] INT:Rotate Finish.\n", __func__);
			return;
		}

		if(IS_MASK_SET(EventState, F_Status_AKMThreadInt)) {		//akm interrupt.
			printk("[MSP430][%s] INT:akm interrupt trigger\n", __func__);
			cancel_delayed_work(&interrupt_work);
			queue_delayed_work(mcu_info->interrupt_wq, &interrupt_work, msecs_to_jiffies(2000));  // delay 2s
		}

		//Notify IMS force stop cmd success. 			  
		if(IS_MASK_SET(EventState, F_Status_MotorRoateForceStop)){		//roate force stop.
			//report_motor_event(MOTOR_ROTATE, ROTATE_STOP);
			printk("[MSP430][%s] INT:Phone force rotate stop trigger.\n", __func__);
		}

		if(FacOrUserClaFlg == 0) {
			if(IS_MASK_SET(EventState, F_Status_MotorKMCUCal)) {		//Calibration and threadhold data backup.
				if(!Zen7_MSP430FR2311_rI2CtoCPU(0x70, &(fcal_val.Cal_val[0]), CalLength)) {				
					backup_mcu_cal_thed();
					bak_raw();
					//report_motor_event(MOTOR_K_FINISH, 0);
					printk("[MSP430][%s] INT:backup calibration data trigger.\n", __func__);
				}else
					printk("[MSP430][%s] INT i2c backup calibration read error!\n", __func__);			
			}

			if(IS_MASK_SET(EventState, F_Status_MotorKMCUThred)) {		//Calibration and threadhold data backup.
				if(!Zen7_MSP430FR2311_rI2CtoCPU(0x70, &(fcal_val.Cal_val[0]), CalLength)){				
					backup_mcu_cal_thed();
					printk("[MSP430][%s] INT:backup theadhold data trigger.\n", __func__);
				}else
					printk("[MSP430][%s] INT i2c backup theadhold read error!\n", __func__);			
			}
		}else{	//User K
			if(IS_MASK_SET(EventState, F_Status_MotorKMCUCal)) {		//Calibration and threadhold data backup.
				if(UserKState == UserK_Start){
					if(!Zen7_MSP430FR2311_rI2CtoCPU(0x70, &(fcal_val_UserK.Cal_val[0]), CalLength)) {				
						UserKState = UserK_rMCU_Ready;
						//report_motor_event(MOTOR_K_FINISH, 0);
						printk("[MSP430][%s] INT:backup userK calibration data trigger.\n", __func__);
					}else
						printk("[MSP430][%s] INT read userK calibration data error!\n", __func__);

				}else {
					UserKState = UserK_TBD;
					printk("[MSP430] INT: missing UserK interrupt.\n");
				}

				FacOrUserClaFlg = 0;	//Default fac K.
			}
		}
		/*
		//fault_status_reg, diag_status1_reg, diag_status2_reg.			      
		if(IS_MASK_SET(EventState, F_Status_DrvInt)){		//Drv interrupt.
			report_motor_event(MOTOR_DRVINT, ((rBuf[2]<<16) | (rBuf[3]<<8) | rBuf[4]));
			printk("[MSP430] INT:Drv interrupt trigger.\n");
		}*/
		
		//Notify IMS that MCU need get current angle.			      
		if(IS_MASK_SET(EventState, F_Status_ReqG_Angle)) {		//get g-sensor's angle.
			//report_motor_event(MOTOR_GET_ANGLE, 0);
			printk("[MSP430][%s] INT:Get g-sensor's angle trigger.\n", __func__);
		}

		//Notify IMS that MCU get current angle timeout.			      
		if(IS_MASK_SET(EventState, F_Status_ReqG_Timeout)){		//get g-sensor's angle timeout.
			//report_motor_event(MOTOR_GET_ANGLE, 1);
			printk("[MSP430][%s] INT:Get g-sensor's angle timeout trigger.\n", __func__);
		}
	}
}

static irqreturn_t mcu_interrupt_handler(int irq, void *dev_id)
{
	if(openclose_worker_running) {
		printk("[MSP430][%s] Openclose Work is running %d, Skip interrupt.\n", __func__, openclose_worker_running);
		return IRQ_HANDLED;
	}

	printk("[MSP430][%s] L2H trigger.\n", __func__);

	//Only L2H level be dealt.
	if(gpio_get_value(mcu_info->mcu_int) == 1) {
		extract_mcu_data();
	}

	return IRQ_HANDLED;
}

static int init_irq(void){
	int ret = 0;

	/* GPIO to IRQ */
	mcu_info->mcu_irq = gpio_to_irq(mcu_info->mcu_int);
	
	if(mcu_info->mcu_irq < 0) {
		printk("[MSP430][%s] gpio_to_irq ERROR, irq=%d.\n", __func__, mcu_info->mcu_irq);
	}else {
		printk("[MSP430][%s] gpio_to_irq IRQ %d successed on GPIO:%d\n", __func__, mcu_info->mcu_irq, mcu_info->mcu_int);
	}

	ret = request_threaded_irq(mcu_info->mcu_irq, NULL, mcu_interrupt_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "mcu_int", mcu_info);
	if(ret < 0) {
		free_irq(mcu_info->mcu_irq, mcu_info);
		printk("[MSP430][%s] request_irq() ERROR %d.\n", __func__, ret);
	}else {
		printk("[MSP430][%s] Enable irq !! \n", __func__);
		enable_irq_wake(mcu_info->mcu_irq);
	}

	return 0;	
}

static int initial_MSP430FR2311_gpio(void)
{
	int ret;

	printk("[MSP430][%s] +\n", __func__);
	ret = gpio_request(mcu_info->mcu_reset, "msp430_reset");
	if(ret < 0) {
		printk("[MSP430][%s] gpio %d request failed (%d)\n", __func__, mcu_info->mcu_reset, ret);
		return ret;
	}

	ret = gpio_request(mcu_info->mcu_test, "msp430_test");
	if(ret < 0) {
		printk("[MSP430][%s] gpio %d request failed (%d)\n", __func__, mcu_info->mcu_test, ret);
		return ret;
	}

	ret = gpio_direction_output(mcu_info->mcu_reset, 0);
	if(ret < 0) {
		printk("[MSP430][%s] fail to set gpio %d as input (%d)\n", __func__, mcu_info->mcu_reset, ret);
		gpio_free(mcu_info->mcu_reset);
		return ret;
	}
	gpio_set_value(mcu_info->mcu_reset, 0);
	printk("[MSP430][%s] mcu_reset set LOW.\n", __func__);

	ret = gpio_direction_output(mcu_info->mcu_test, 0);
	if(ret < 0) {
		printk("[MSP430][%s] fail to set gpio %d as input (%d)\n", __func__, mcu_info->mcu_test, ret);
		gpio_free(mcu_info->mcu_test);
		return ret;
	}
	gpio_set_value(mcu_info->mcu_test, 0);
	printk("[MSP430][%s] mcu_test set LOW.\n", __func__);

/*
	ret = gpio_request(mcu_info->mcu_wakeup, "msp430_wakeup");
	if (ret < 0) {
		printk("[MSP430][%s] gpio %d request failed (%d)\n",
			__func__, mcu_info->mcu_wakeup, ret);
		return ret;
	}
	
	ret = gpio_direction_output(mcu_info->mcu_wakeup, 0);
	if (ret < 0) {
		printk("[MSP430][%s] fail to set gpio %d as input (%d)\n",
			__func__, mcu_info->mcu_wakeup, ret);
	gpio_free(mcu_info->mcu_wakeup);
	return ret;
	}
*/
	ret = gpio_request(mcu_info->mcu_int, "msp430_int");
	if(ret < 0) {
		printk("[MSP430][%s] gpio %d request failed (%d)\n", __func__, mcu_info->mcu_int, ret);
		return ret;
	}
	
	ret = gpio_direction_input(mcu_info->mcu_int);
	if(ret < 0) {
		printk("[MSP430][%s] fail to set gpio %d as input (%d)\n", __func__, mcu_info->mcu_int, ret);
		gpio_free(mcu_info->mcu_int);
		return ret;
	}

	printk("[MSP430][%s] -\n", __func__);
	return ret;
}
	
int MCU_I2C_power_control(bool enable)
{
	int ret = 0;

	if(enable) {
		gpio_set_value(mcu_info->mcu_power, 1);
	}else {
		gpio_set_value(mcu_info->mcu_power, 0);
	}

	printk("[MSP430][%s] Try to :%s, mcu_power_gpio=%d\n", __func__, enable?"enable":"disable", gpio_get_value(mcu_info->mcu_power));
	return ret;
}

int MSP430FR2311_power_control(uint8_t enable)
{
	int ret = 0;

	bPowerDone = 0;
	if(!enable) {
		//power down
		//gpio_set_value(mcu_info->mcu_wakeup, 1);
		printk("[MSP430][%s] mcu_reset %d, mcu_test 0.\n", __func__, enable);
		gpio_set_value(mcu_info->mcu_reset, enable);
		gpio_set_value(mcu_info->mcu_test, 0);
		MCU_I2C_power_control(enable);
	}

	g_motor_power_state = enable;
	if(enable) {
		//power up
		printk("[MSP430][%s] mcu_reset %d, mcu_test 0.\n", __func__, enable);
		gpio_set_value(mcu_info->mcu_test, 0);
		//gpio_set_value(mcu_info->mcu_wakeup, 0);

		MCU_I2C_power_control(enable);
		msleep(30);
		gpio_set_value(mcu_info->mcu_reset, enable);
	}

	bPowerDone = 1;
	return ret;
}

int FrqConvertMode(int mode, int angle, int speed) {
	uint16_t MotorDefault[]={0, 120, 120, 56, 28, 20, 12, 255, 47, 2, 2, 2, 40, 0, 0, 73, 73, 73, 73};
	uint16_t FS_steps = 0;
	int gearStep = LEAD_DELTA;
	int dir = (mode > 1)?(mode-2):(mode);

	printk("[MSP430][%s] mode %d, dir %d, angle %d, speed %d\n", __func__, mode, dir, angle, speed);
	memcpy(MotorDefault, ConvertFRQMode[mode], sizeof(MotorDefault));
	
	if(MCUState != MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d", __func__, MCUState);
		return -MCUState;
	}

	if(angle != 180) {
		if(angle <= 10) {	 //ASUS_MOTOR_DRV_AUTO_MODE_WITH_ANGLE, angle range is less than 10 degree.
			memcpy(MotorDefault, ConvertFRQModeForSmallAngle, sizeof(MotorDefault));	
			MotorDefault[0] = dir;

		}else {	//ASUS_MOTOR_DRV_AUTO_MODE_WITH_ANGLE, angle range is 10~180 degree.
			FS_steps = (CONVERT_FRQ_FS_STEP[dir])*angle/180;	
			//Step 0 + Step 1 + step5 equal total expect steps, other step don't move(steps = 0).		
			if(FS_steps <= 255) {
				MotorDefault[7]  = FS_steps;
				MotorDefault[8]  = 0;
				MotorDefault[12] = (gearStep+CONVERT_FRQ_SS_STEP[dir]);	//(gearStep+CONVERT_FRQ_SS_STEP[dir])*angle/180;
			}else {
				MotorDefault[7] = 255;
				MotorDefault[8] = (FS_steps - 255);
				MotorDefault[12]= (gearStep+CONVERT_FRQ_SS_STEP[dir]);	//(gearStep+CONVERT_FRQ_SS_STEP[dir])*angle/180;
			}

			MotorDefault[9]  = 0;
			MotorDefault[10] = 0;
			MotorDefault[11] = 0;
			printk("[MSP430][%s] FS_steps:%d\n", __func__, FS_steps);
		}
	}else{
		//ASUS_MOTOR_DRV_AUTO_MODE or ASUS_MOTOR_DRV_AUTO_MODE_WITH_ANGLE(in condition:(mode==1 || mode==2) and angle==180).
	}
/*
	printk("[MSP430][%s] auto control (Dir:%d, Angle:%d), param=%d, %d %d %d %d %d %d, %d %d %d %d %d %d, %d %d %d %d %d %d", __func__, dir, angle, MotorDefault[0],
		MotorDefault[1],MotorDefault[2],MotorDefault[3],MotorDefault[4],MotorDefault[5],MotorDefault[6],	\
		MotorDefault[7],MotorDefault[8],MotorDefault[9],MotorDefault[10],MotorDefault[11],MotorDefault[12],	\
		MotorDefault[13],MotorDefault[14],MotorDefault[15],MotorDefault[16],MotorDefault[17],MotorDefault[18]);	
*/
	return Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);
}

static void rotate_worker(struct work_struct *data)
{
	int i;
	uint16_t MotorDefault[] = {1, 120, 120, 120, 120, 120, 120, 135, 135, 135, 135, 135, 135, 0, 0, 0, 0, 0, 0};
	printk("[MSP430][%s]\n",__func__);

	for(i=0; i<10; i++) {
		Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);
		msleep(1000);
	}
}

static void openclose_check(void)
{
	int i, raw_z, raw_x, criteria;
	uint8_t wBuf[3] = {0xAA, 0x55, 0x66};

	mutex_lock(&MSP430FR2311_openclose_mutex);
	printk("[MSP430][%s] Rotate Start.\n",__func__);
	openclose_worker_running = true;

	if(1 == openclose) { //Motor_Test 0 0
		raw_z = open_z;
		raw_x = open_x;
		criteria = 4000000; // For shipping, < 2000
	}else if(2==openclose) { //Motor_Test 0 1
		raw_z = close_z;
		raw_x = close_x;
		criteria = 160000; // For shipping, < 400
	}else if(3==openclose) { //Motor_Test 0 2
		raw_z = open_z;
		raw_x = open_x;
		criteria = 90000; // For factory, < 300
	}else if(4==openclose) { //Motor_Test 0 3
		raw_z = close_z;
		raw_x = close_x;
		criteria = 90000; // For factory, < 300
	}else {
		raw_z = 65536;
		raw_x = 65536;
	}

	for(i=0; i<20; i++) {
		int z, x, delta_z, delta_x;
		uint16_t value;

		Zen7_MSP430FR2311_wI2CtoMCU(wBuf, 3);
		msleep(10);
		Zen7_MSP430FR2311_rI2CtoCPU(0x67, akm_temp, sizeof(akm_temp));

		value = (akm_temp[1]<<8)+akm_temp[2];
		z = (value > 32767)? value-65536 : value;
		delta_z = z - raw_z;
		value = (akm_temp[5]<<8)+akm_temp[6];
		x = (value > 32767)? value-65536 : value;
		delta_x = x - raw_x;

		printk("[MSP430][%s] openclose=%d, z=%d,x=%d,delta_z=%d,delta_x=%d\n",__func__,openclose,z,x,delta_z,delta_x);
		delta_z *= delta_z;
		delta_x *= delta_x;

		if(2==openclose)
			delta_z = 0;

		if((delta_z + delta_x) > criteria) {
			msleep(200);
		}else {
			if(1==openclose) {
				DoorStatus = DOOR_OPEN;
			}else if(2==openclose) {
				DoorStatus = DOOR_CLOSE;
			}
			report_motor_event(MOTOR_ROTATE, ROTATE_FINISH);
			printk("[MSP430][%s] Rotate Finish.\n",__func__);
			break;
		}
	}

	if(20 == i) {
		report_motor_event(MOTOR_ROTATE, ROTATE_FAIL);
		printk("[MSP430][%s] Rotate Fail!!\n",__func__);
	}

	openclose_worker_running = false;
	mutex_unlock(&MSP430FR2311_openclose_mutex);
}

static void msp_resume_worker(struct work_struct *data)
{
	uint16_t angle_init[25]={101, 7, 32, 6, 224, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int ret = regulator_enable(mcu_info->angle_vdd);
	if(ret)
		printk("[MSP430][%s] enable angle_vdd failed, err=%d", __func__, ret);
	else
		printk("[MSP430][%s] enable angle_vdd success!", __func__);
	MSP430FR2311_power_control(1);
	msleep(1000); //Delay to wait MCU i2c slave init ok.
	Zen7_MSP430FR2311_wrAKM(angle_init, 25);
}

int MSP430FR2311_Set_AutoModeWithAngle(int mode, int angle) {
	printk("[MSP430][%s] mode %d, angle %d\n", __func__, mode, angle);

	if(mode == 0xbf) {
		printk("[MSP430] Burn firmware");
		if(MSP43FR2311_Update_Firmware_Load_File(1) == MSP430_STATUS_OPERATION_OK) {
			MCUState=MCU_READY;
			MSP430BSL_cleanUpPointer();
		}
		return 0;
	} 

	if(mode == 0xdd) {
		MSP430FR2311_Check_Version();
		return 0;
	}

	if(mode == 222) {	//0xDE
		read_cali_file();
		return 0;
	}

	if(mode == 242) {	//0xE0
	 	int rc = 0;
		uint16_t MotorDefault[] = {1, 4, 4, 4, 4, 4, 4, 100, 0, 0, 0, 0, 0, 73, 73, 73, 73, 73, 73};
	
		memcpy(MotorDefault, TightenMode, sizeof(MotorDefault));
		rc= Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);

		msleep(5000);
		return rc;
	}

	//For camera app +++
	if(mode == 243) { 	//0 to 180
	 	int rc = 0;
		uint16_t MotorDefault[] = {0, 4, 4, 4, 4, 4, 4, 6, 0, 0, 0, 0, 0, 73, 73, 73, 73, 73, 73};	
	
		memcpy(MotorDefault, SmallAngle, sizeof(MotorDefault));
		MotorDefault[0] = 0;
		rc= Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);
		return rc;
	}

	if(mode == 244) { 	//180 to 0
	 	int rc = 0;
		uint16_t MotorDefault[]={1, 4, 4, 4, 4, 4, 4, 6, 0, 0, 0, 0, 0, 73, 73, 73, 73, 73, 73};	

		memcpy(MotorDefault, SmallAngle, sizeof(MotorDefault));
		MotorDefault[0] = 1;
		rc= Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);
		return rc;
	}
	//For camera app ---

	//For ATD motor on
	if(mode == 245) {
		schedule_work(&rotate_work);
		return 0;
	}

   	if(mode == 1 || mode == 2)
		return FrqConvertMode(--mode, angle, 6);

	if(mode == 3 || mode == 4) {
		mode--;
		return FrqConvertMode(mode, angle, 6);
	}
/*
#ifdef ALLEN
	if(mode == 3 || mode == 4){ 
		AutoEmergencyMode[0] = mode-3;
		powerDownDuration = DEFAULT_POWERDOWNDURATION;

		return Zen7_MSP430FR2311_Set_ParamMode(AutoEmergencyMode);
	}

	if(mode == 5 || mode == 6){ 
		AutoWarmUpMode[0] = mode-5;
		powerDownDuration = DEFAULT_POWERDOWNDURATION;

		return Zen7_MSP430FR2311_Set_ParamMode(AutoWarmUpMode);
	}

	if(mode == 7 || mode == 8){ 
		AutoWarmUpMode2[0] = mode - 7;
		powerDownDuration = DEFAULT_POWERDOWNDURATION;
	
		return Zen7_MSP430FR2311_Set_ParamMode(AutoWarmUpMode2);
	}
#endif
*/
	printk("[MSP430][%s] Not supported mode for %d", __func__, mode);
	return -1;

/*
#if 0
	char MSP430AutoMode[]={0xaa, 0x55, 0x04, 0x01, 0, 0};
	printk("[MSP430FR2311][%s] +\n", __func__);
	MSP430FR2311_wakeup(1);
	if(MCUState != MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d\n", __func__, MCUState);
		return -MCUState;
	}

	MSP430AutoMode[3]=mode;
	if (!MSP430_I2CWriteA(MSP430_READY_I2C, MSP430AutoMode, sizeof(MSP430AutoMode))) {
		//if (MCUState != MCU_LOOP_TEST)  MSP430FR2311_power_control(0);
		MSP430FR2311_wakeup(0);

		printk("[MSP430][%s] I2C error!\n", __func__);
		return -1;
	}
	
	//if (MCUState != MCU_LOOP_TEST) MSP430FR2311_power_control(0);
	MSP430FR2311_wakeup(0);

	printk("[MSP430FR2311][%s] -\n", __func__);
	return 0;
#endif
*/
}

int MSP430FR2311_Set_AutoMode(int mode) {
	printk("[MSP430][%s] mode %d\n", __func__, mode);
	return MSP430FR2311_Set_AutoModeWithAngle(mode, 180);
}

int MSP430FR2311_Set_ParamMode(const uint16_t* vals) {
	char MSP430ParamMode[]={0xAA, 0x55, 0x0C, 0x00, 0x4B, 0x50, 0x57, 0x64, 0x57, 0x50, 0x0F, 0x1E, 0x32, 0x8C, 0xAF, 0xBE};
	int i=0;

	printk("[MSP430][%s] +\n", __func__);

	#ifdef ENABLE_LOOP_TEST
	if(vals[0] == -1 || vals[0] == 65535) {
		memcpy(AutoEmergencyMode, vals, 13*sizeof (uint16_t));
		return 0;
	}

	if(vals[0] >= 2 && vals[0] <= 60000) {
		memcpy(AutoEmergencyMode, vals, 13*sizeof (uint16_t));
		MCUState = MCU_LOOP_TEST;		
		loopCounter = 0;
		totalLoopCounter = vals[0] + 1;
		queue_delayed_work(mcu_info->mcu_wq, &report_work, mcu_info->mcu_polling_delay);	
		return 0;
	}
	#endif

	mutex_lock(&MSP430FR2311_control_mutex);
	MSP430FR2311_wakeup(1);
	if(MCUState < MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d", __func__, MCUState);
		mutex_unlock(&MSP430FR2311_control_mutex);
		return -MCUState;
	}

	powerDownDuration=0;
	for(i=1; i<7; i++) {
		uint16_t speed = vals[i] << 2;
		//const int defaultSpeedDuration=1200000;
		if(vals[i] < 50) {  //micro wave step
			switch(vals[i] ) {
				case 49:
					speed=88;
					break;
				case 39:
					speed=116;
					break;
				case 25:
					speed=165;
				break;
				case 20:
					speed=193;
				break;
				case 17:
					speed=232;
				break;
				case 8:
					speed=385;
				break;						
				case 7:
					speed=575;
				break;						
			}
			MSP430ParamMode[i+3] = vals[i] >> 2;
		}else {
			MSP430ParamMode[i+3] = (vals[i] << 2)/50 + 50;
		}
		powerDownDuration += (vals[i+6] - ((i==1)? 0 : vals[i+5]) )*2400*500/speed/300;
		printk("[MSP430][%s] Power duration += (%d-%d)*2400*500/%d/300 =   %d\n", __func__, vals[i+6], ((i==1)?0:vals[i+5]), speed, powerDownDuration);
	};

	printk("[MSP430][%s] Power duration=%d(ms), reference only\n", __func__, powerDownDuration);
	powerDownDuration = DEFAULT_POWERDOWNDURATION;
	bShowStopInfoOnce = 1;
		
	MSP430ParamMode[3] = vals[0];
	MSP430ParamMode[10]= vals[7] >>1;
	MSP430ParamMode[11]= vals[8] >>1;
	MSP430ParamMode[12]= vals[9] >>1;
	MSP430ParamMode[13]= vals[10]>>1;
	MSP430ParamMode[14]= vals[11]>>1;
	MSP430ParamMode[15]= vals[12]>>1;
		
	printk("[MSP430] dump param=%d %d %d %d %d %d %d %d %d %d %d %d %d 254", vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7], vals[8], vals[9], vals[10], vals[11], vals[12]);

	if(!MSP430_I2CWriteA(MSP430_READY_I2C, MSP430ParamMode, sizeof(MSP430ParamMode))) {
		//if (MCUState != MCU_LOOP_TEST)	MSP430FR2311_power_control(0);
		MSP430FR2311_wakeup(0);
		mutex_unlock(&MSP430FR2311_control_mutex);
	
		printk("[MSP430][%s] I2C error!", __func__);
		return -1;
	}

	//if (MCUState != MCU_LOOP_TEST) MSP430FR2311_power_control(0);
	MSP430FR2311_wakeup(0);
	mutex_unlock(&MSP430FR2311_control_mutex);
	
	printk("[MSP430][%s] -\n", __func__);
	return 0;
}

inline int MSP430FR2311_Set_ManualMode(int dir, int angle, int speed) {
	printk("[MSP430][%s] dir %d, angle %d, speed %d\n", __func__, dir, angle , speed);
	return (*fManualMode)(dir, angle, speed);
}

static char old_dir = -1;
int ManualMode_AfterAndPR2(int dir, int angle, int speed) {
	uint16_t MotorDefault[] = {0, 120, 120, 120, 120, 120, 120, 255, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint16_t manual_steps = 0;

	printk("[MSP430][%s] dir %d, angle %d, speed %d\n", __func__, dir, angle ,speed);
	memcpy(MotorDefault, ConstSpeedMode, sizeof(MotorDefault));
	if(MCUState != MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d", __func__, MCUState);
		return -MCUState;
	}
	
	MotorDefault[0] = dir;
	if(angle != 180) {
		int gearStep;
		gearStep = 0;

		if(old_dir != dir) {
			gearStep += LEAD_DELTA;
			//old_dir=dir;
		} 	
		
		//Not use 0 instead of formula. I wish that: every step can be set different speed in manual mode, but it seems IMS only set one speed.
		manual_steps = (CONVERT_FRQ_FS_STEP[dir])*angle/180;	//Maybe need modify ???
		if(manual_steps <= 255) {
			MotorDefault[7]  = manual_steps;
			MotorDefault[8]  = 0;
			MotorDefault[12] = (gearStep+CONVERT_FRQ_SS_STEP[dir]);
		}else {
			MotorDefault[7] = 255;
			MotorDefault[8] = (manual_steps - 255);
			MotorDefault[12]= (gearStep+CONVERT_FRQ_SS_STEP[dir]);
		}

		MotorDefault[9] = 0;
		MotorDefault[10]= 0;
		MotorDefault[11]= 0;
		printk("[MSP430][%s] PR2 manual control (dir:%d, angle:%d, speed:%d, manual_steps:%d, gear_step:%d).", __func__, dir, angle, speed, manual_steps, gearStep);
	}else{
		printk("[MSP430][%s] PR2 manual control (dir:%d, angle:%d, speed:%d).", __func__, dir, angle, speed);
	}
	old_dir=dir;

	if(speed <= 10) {//micro-step process
		int i=0;

		for (i=0; i<6; i++){
			MotorDefault[i+1] = defaultManualSpeed[2*speed];
			MotorDefault[i+13]= defaultManualSpeed[(2*speed) + 1];
		}		 

	}else{
		printk("[MSP430][%s] don't support this(%d) manual speed.", __func__, speed);
	}

	return Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);
}

int ManualMode_AfterAndPR2_NOSS(int dir, int angle, int speed) {
	uint16_t MotorDefault[]={0, 120, 120, 120, 120, 120, 120, 255, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint16_t manual_steps = 0;
	int gearStep = 0;

	printk("[MSP430][%s] dir %d, angle %d, speed %d\n", __func__, dir, angle , speed);
	memcpy(MotorDefault, ConstSpeedMode, sizeof(MotorDefault));
	if (MCUState!=MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d", __func__, MCUState);
		return -MCUState;
	}

	MotorDefault[0] = dir;
	if(old_dir != dir) {
		gearStep += LEAD_DELTA;
	}

	//Not use 0 instead of formula. I wish that: every step can be set different speed in manual mode, but it seems IMS only set one speed.
	manual_steps = (CONVERT_FRQ_FS_STEP[dir])*angle/180;	//Maybe need modify ???
	if(manual_steps <= 255) {
		MotorDefault[7] = manual_steps;
		MotorDefault[8] = 0;
		MotorDefault[12]= (gearStep);
	}else {
		MotorDefault[7] = 255;
		MotorDefault[8] = (manual_steps - 255);
		MotorDefault[12]= (gearStep);
	}

	MotorDefault[9] = 0;
	MotorDefault[10]= 0;
	MotorDefault[11]= 0;
	printk("[MSP430] NOSS PR2 manual control (dir:%d, angle:%d, speed:%d, manual_steps:%d, gear_step:%d).", dir, angle, speed, manual_steps, gearStep);

	old_dir=dir;
	if(speed <= 10) { //micro-step process

		int i=0;
		for(i=0; i<6; i++) {
			MotorDefault[i+1] = defaultManualSpeed[2*speed];
			MotorDefault[i+13]= defaultManualSpeed[(2*speed) + 1];
		}

	}else {
		printk("[MSP430][%s] don't support this(%d) manual speed.", __func__, speed);
	}

	return Zen7_MSP430FR2311_Set_ParamMode(MotorDefault);
}

int MSP430FR2311_Stop(void) {
	char MSP430Stop[]={0xAA, 0x55, 0x08, 00, 00};

	D("[MSP430][%s] +\n", __func__);
	if(MCUState < MCU_CHECKING_READY) {
		printk("[MSP430][%s] Not ready!, state=%d", __func__, MCUState);
		return -MCUState;
	}

	mutex_lock(&MSP430FR2311_control_mutex);
	MSP430FR2311_wakeup(1);
	powerDownDuration=DEFAULT_POWERDOWNDURATION;
	if(!MSP430_I2CWriteA(MSP430_READY_I2C, MSP430Stop, sizeof(MSP430Stop))) {
		MSP430FR2311_wakeup(0);
		mutex_unlock(&MSP430FR2311_control_mutex);
		printk("[MSP430][%s] I2C error!\n", __func__);
		return -1;
	}

	if(bShowStopInfoOnce) {
		bShowStopInfoOnce=0;
		MSP430FR2311_Get_Steps();
	}

	MSP430FR2311_wakeup(0);
	mutex_unlock(&MSP430FR2311_control_mutex);

	printk("[MSP430][%s] -\n", __func__);
	return 0;	
}

static int MSP430FR2311_power_init(void)
{
	int ret = 0;
	printk("[MSP430][%s] Request VDD.", __func__);

	ret = gpio_request(mcu_info->mcu_power, "msp430_VDD");
	if(ret < 0) {
		printk("[MSP430][%s] gpio %d request failed (%d)\n", __func__, mcu_info->mcu_power, ret);
		return ret;
	}

	ret = gpio_direction_output(mcu_info->mcu_power, 0);
	if(ret < 0) {
		printk("[MSP430][%s] fail to set gpio %d as input (%d)\n", __func__, mcu_info->mcu_power, ret);
		gpio_free(mcu_info->mcu_power);
		return ret;
	}

	ret = regulator_enable(mcu_info->angle_vdd);
	if(ret)
		printk("[MSP430][%s] enable vdd regulator failed, err=%d", __func__, ret);
	else
		printk("[MSP430][%s] enable vdd regulator sucess!", __func__);

	return ret;
}

// Sleep work
static void msp430_sleep_work(struct work_struct *work)
{
	int door_state;
	bool sleep_enable = false;

	// Detect Door state
	door_state = judgeDoorState(0);
	if(!door_state) // 0 : close, 1 : open
		sleep_enable = true;
	else if(door_state == 1)
		sleep_enable = false;
	else
		printk("[MSP430][%s] door_state %d abnormal!!\n", __func__, door_state);

	// Panel status priority is Fisrt.
	if(panel_status) // 0 : panel off, 1 : panel on
		sleep_enable = false;

	printk("[MSP430][%s] Door %s, Panel %s, Sleep %s.\n", __func__, (!door_state)?"close":"open", (panel_status)?"on":"off", (sleep_enable)?"enable":"disable");
	if(sleep_enable){
		int ret = 0;
		if(regulator_is_enabled(mcu_info->angle_vdd)) {
			MSP430FR2311_power_control(0);
			ret = regulator_disable(mcu_info->angle_vdd);
			if(ret)
				printk("[MSP430][%s] disable angle_vdd failed, err=%d", __func__, ret);
			else
				printk("[MSP430][%s] disable angle_vdd success!", __func__);
		}
	}
}

static int MSP430FR2311_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 temp_val;
	int rc;

	D("[MSP430][%s]\n", __func__);

	rc = of_get_named_gpio_flags(np, "MCU,power-gpios", 0, NULL);
	if(rc < 0) {
		dev_err(dev, "Unable to read mcu power pin number\n");
		return rc;
	}else {
		mcu_info->mcu_power= rc;
 	    D("[MSP430][%s] GET mcu power PIN =%d\n", __func__, rc);   
	}

	rc = of_get_named_gpio_flags(np, "MCU,mcureset-gpios", 0, NULL);
	if(rc < 0) {
		dev_err(dev, "Unable to read mcureset pin number\n");
		return rc;
	}else {
		mcu_info->mcu_reset= rc;
		D("[MSP430][%s] GET mcu reset PIN =%d\n", __func__, rc);   
	}

	rc = of_get_named_gpio_flags(np, "MCU,mcutest-gpios", 0, NULL);
	if(rc < 0)	{
		dev_err(dev, "Unable to read mcutest pin number\n");
		return rc;
	}else {
		mcu_info->mcu_test= rc;
		D("[MSP430][%s] GET mcu test PIN=%d \n", __func__, rc);   
	}

	rc = of_get_named_gpio_flags(np, "MCU,mcuint-gpios", 0, NULL);
	if(rc < 0) {
		dev_err(dev, "Unable to read mcu int pin number\n");
		return rc;
	}else {
		mcu_info->mcu_int = rc;
		D("[MSP430][%s] GET mcu int PIN =%d\n", __func__, rc);   
	}

	rc = of_property_read_u32(np, "MCU,slave_address", &temp_val);
	if(rc) {
		dev_err(dev, "Unable to read slave_address\n");
		return rc;
	}else {
		mcu_info->slave_addr = (uint8_t)temp_val;
		printk("[MSP430][%s] slave_addr = 0x%x\n", __func__, mcu_info->slave_addr);
	}

	mcu_info->angle_vdd = devm_regulator_get(dev, "angle-vdd");
	if(IS_ERR_OR_NULL(mcu_info->angle_vdd)) {
		rc = PTR_ERR(mcu_info->angle_vdd);
		printk("[MSP430][%s] Failed to get regulator angle-vdd, err=%d", __func__);
		return rc;
	}
	rc = regulator_set_voltage(mcu_info->angle_vdd, 1800000, 1800000);
	if(rc) {
		printk("[MSP430][%s] regulator_set_voltage failed, err=%d\n", __func__, rc);
		return rc;
	} else
		printk("[MSP430][%s] set vdd to 1.8v succussed\n", __func__);
#if 1
	mcu_info->pinctrl = devm_pinctrl_get(dev);
	if(IS_ERR(mcu_info->pinctrl)) {
		if(PTR_ERR(mcu_info->pinctrl) == -EPROBE_DEFER) {
			printk("[MSP430][%s] pinctrl not ready\n", __func__);
			rc = -EPROBE_DEFER;
			goto err_reset_gpio_request;
		}
		printk("[MSP430][%s] Target does not use pinctrl\n", __func__);
		mcu_info->pinctrl = NULL;
		rc = -EINVAL;
		goto err_reset_gpio_request;
	}

	mcu_info->pinctrl_state = pinctrl_lookup_state(mcu_info->pinctrl, "msp_active");
	if(IS_ERR(mcu_info->pinctrl_state)) {
		printk("[MSP430][%s] Cannot find pinctrl state\n", __func__);
		goto err_reset_gpio_request;
	}else {
		printk("[MSP430][%s] select msp_active state\n", __func__);
		pinctrl_select_state(mcu_info->pinctrl, mcu_info->pinctrl_state);
	}

err_reset_gpio_request:
#else
	printk("[MSP430][%s] skip pinctrl!!!\n", __func__);
#endif

	D("[MSP430][%s] PARSE OK \n", __func__);
	return 0;
}

static int msp_gpio_get_value(int gpio)
{
	int ret = gpio_request(gpio, "id");
	if (ret < 0) {
		printk("[MSP430] %s: gpio %d request failed (%d)\n",
			__func__, gpio, ret);
		return -1;
	}
	ret = gpio_get_value(gpio);
	gpio_free(gpio);
	return ret;
}

static bool checkMCUsupport(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0;
	int hw_id0=-1, hw_id1=-1, hw_id2=-1, hw_id3=-1, prj_id0=-1, prj_id1=-1, prj_id2=-1;

	ret = of_get_named_gpio_flags(np, "hw-id0-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read hw id0 pin number\n");
		return true;
	} 
	else
		hw_id0 = msp_gpio_get_value(ret);

	ret = of_get_named_gpio_flags(np, "hw-id1-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read hw id0 pin number\n");
		return true;
	} 
	else
		hw_id1 = msp_gpio_get_value(ret);

	ret = of_get_named_gpio_flags(np, "hw-id2-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read hw id0 pin number\n");
		return true;
	} 
	else
		hw_id2 = msp_gpio_get_value(ret);

	ret = of_get_named_gpio_flags(np, "hw-id3-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read hw id0 pin number\n");
		return true;
	} 
	else
		hw_id3 = msp_gpio_get_value(ret);

	ret = of_get_named_gpio_flags(np, "prj-id0-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read prj id0 pin number\n");
		return true;
	} 
	else
		prj_id0 = msp_gpio_get_value(ret);

	ret = of_get_named_gpio_flags(np, "prj-id1-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read prj id1 pin number\n");
		return true;
	} 
	else
		prj_id1 = msp_gpio_get_value(ret);

	ret = of_get_named_gpio_flags(np, "prj-id2-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(dev, "Unable to read prj id2 pin number\n");
		return true;
	} 
	else
		prj_id2 = msp_gpio_get_value(ret);
	printk("[MSP430] %s: hw_id0=%d hw_id1=%d hw_id2=%d hw_id3=%d prj_id0=%d prj_id1=%d prj_id2=%d\n", __func__, hw_id0, hw_id1, hw_id2, hw_id3, prj_id0, prj_id1, prj_id2);

	mcu_info->pinctrl_state = pinctrl_lookup_state(mcu_info->pinctrl, "msp_suspend1");
	if(IS_ERR(mcu_info->pinctrl_state)) {
		printk("[MSP430][%s] Cannot find pinctrl state\n", __func__);
	}else {
		printk("[MSP430][%s] select msp_suspend1 state\n", __func__);
		pinctrl_select_state(mcu_info->pinctrl, mcu_info->pinctrl_state);
	}
	mcu_info->pinctrl_state = pinctrl_lookup_state(mcu_info->pinctrl, "msp_suspend2");
	if(IS_ERR(mcu_info->pinctrl_state)) {
		printk("[MSP430][%s] Cannot find pinctrl state\n", __func__);
	}else {
		printk("[MSP430][%s] select msp_suspend2 state\n", __func__);
		pinctrl_select_state(mcu_info->pinctrl, mcu_info->pinctrl_state);
	}

	if ((hw_id0<0)||(hw_id1<0)||(hw_id2<0)||(hw_id3<0)||(prj_id0<0)||(prj_id1<0)||(prj_id2<0))
		return true;
	else {
		if ( ((hw_id0==0)&&(hw_id1==0)&&(hw_id2==0)&&(hw_id3==1))   //ER2
		  || ((hw_id0==1)&&(hw_id1==0)&&(hw_id2==0)&&(hw_id3==1))   //PR1
		  || ((hw_id0==1)&&(hw_id1==1)&&(hw_id2==0)&&(hw_id3==1)) ) //MP
			if ((prj_id0==0)&&(prj_id1==1)&&(prj_id2==0)) //id1: Ultimate
				return true;
			else
				return false;
		else
			return true;
	}
}

static int MSP430FR2311_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;

	printk("[MSP430][%s] +\n", __func__);

	mcu_info = kzalloc(sizeof(struct MSP430FR2311_info), GFP_KERNEL);
	if (!mcu_info)
		return -ENOMEM;

	/*D("[MSP430][%s] client->irq = %d\n", __func__, client->irq);*/

	mcu_info->i2c_client = client;

	mcu_info->mcu_reset=-1;
	mcu_info->mcu_test=-1;
	i2c_set_clientdata(client, mcu_info);
	mcu_info->mcu_polling_delay = msecs_to_jiffies(MCU_POLLING_DELAY);

	asus_motor_init(mcu_info);

	if( MSP430FR2311_parse_dt(&client->dev) < 0 ){
		ret = -EBUSY;
		goto err_platform_data_null;
	}

	if (!checkMCUsupport(&client->dev)) {
		printk("[MSP430][%s] No MCU chip, abort probe!!!\n", __func__);
		ret = -ENODEV;
		goto err_platform_data_null;
	}

	ret = MSP430FR2311_power_init();
	if(ret < 0) {
		printk("[MSP430][%s] set power fail\n", __func__);
	}

	ret= initial_MSP430FR2311_gpio();
	if(ret < 0) {
		printk("[MSP430] fail to initial MSP430FR2155 (%d)\n", ret);
		goto err_platform_data_null;
	}

	ret = MSP430FR2311_power_control(1);
	if(ret < 0) {
		printk("[MSP430][%s] enable power fail\n", __func__);
		goto err_platform_data_null;
	}

//	if (MSP430FR2311_Check_Version() && MSP43FR2311_Go_BSL_Mode()) {
		MCUState=MCU_EMPTY;
//		printk("[MSP430][%s] Fail\n", __func__);
//		goto err_initial_MSP430FR2311_gpio;
//	}

	mutex_init(&MSP430FR2311_control_mutex);
	mutex_init(&MSP430FR2311_openclose_mutex);

	ret = mcu_setup();
	if(ret < 0) {
		printk("[MSP430][%s] mcu_setup error!!\n", __func__);
		goto err_mcu_setup;
	}

	init_irq();  

//	if (MCUState!=MCU_READY) {
		mcu_info->mcu_wq = create_singlethread_workqueue("MSP430FR2311_wq");
		if(!mcu_info->mcu_wq) {
			printk("[MSP430][%s] can't create workqueue\n", __func__);
			ret = -ENOMEM;
			goto err_create_singlethread_workqueue;
		}

		mcu_info->cal_wq = create_singlethread_workqueue("mcu_cal_wq");
		if(!mcu_info->cal_wq) {
			printk("[MSP430][%s] can't create cal_wq workqueue\n", __func__);
			ret = -ENOMEM;
			goto err_create_singlethread_workqueue;
		}

		mcu_info->interrupt_wq = create_singlethread_workqueue("mcu_interrupt_wq");
		if(!mcu_info->interrupt_wq) {
			printk("[MSP430][%s] can't create interrupt_wq workqueue\n", __func__);
			ret = -ENOMEM;
			goto err_create_singlethread_workqueue;
		}

		//Delay more time to wait vendor partion ready.
		queue_delayed_work(mcu_info->mcu_wq, &report_work, msecs_to_jiffies(2000));	//mcu_info->mcu_polling_delay
//	}

//	ret = MSP430FR2311_setup();
//	if (ret < 0) {
//		printk("[MSP430][%s]: MSP430FR2311_setup error!\n", __func__);
//		goto err_MSP430FR2311_setup;
//	}

	mcu_info->MSP430FR2311_class = class_create(THIS_MODULE, "TI_mcu");
	if(IS_ERR(mcu_info->MSP430FR2311_class)) {
		ret = PTR_ERR(mcu_info->MSP430FR2311_class);
		mcu_info->MSP430FR2311_class = NULL;
		goto err_create_class;
	}

	mcu_info->mcu_dev = device_create(mcu_info->MSP430FR2311_class, NULL, 0, "%s", "mcu");
	if (unlikely(IS_ERR(mcu_info->mcu_dev))) {
		ret = PTR_ERR(mcu_info->mcu_dev);
		mcu_info->mcu_dev = NULL;
		goto err_create_mcu_device;
	}	

//	ret = drm_check_dt(&client->dev);
//	if (ret) 
//		printk("[MSP430] parse drm-panel fail\n");
//	printk("[MSP430][%s] msp430_register_for_panel_events +++\n", __func__);
	msp430_register_for_panel_events(&client->dev);
//	printk("[MSP430][%s] msp430_register_for_panel_events ---\n", __func__);

// Init Sleep workqueue
	mcu_info->sleep_workqueue = alloc_ordered_workqueue("msp430_sleep_workqueue", 0);
	if (!mcu_info->sleep_workqueue) {
		printk("[MSP430][%s] sleep_workqueue alloc failed.\n", __func__);
	}
	INIT_DELAYED_WORK(&mcu_info->sleep_delayed_work, msp430_sleep_work);

// Init wake lock
	mcu_info->suspend_lock = wakeup_source_register(NULL, "msp430_wakelock");
	if (!mcu_info->suspend_lock) {
		printk("[MSP430][%s] wakeup source init failed.\n", __func__);
	}

// Init Rotate Work
	INIT_WORK(&rotate_work, rotate_worker);

// Init Resume Work
	INIT_WORK(&msp_resume_work, msp_resume_worker);

	fManualMode = ManualMode_AfterAndPR2;
	printk("[MSP430][%s] Probe success.\n", __func__);

	bProbeFail = 0;
	return ret;

err_create_mcu_device:
	device_destroy(mcu_info->MSP430FR2311_class, mcu_info->mcu_dev->devt);
	class_destroy(mcu_info->MSP430FR2311_class);

err_create_class:
	if (mcu_info->mcu_wq) destroy_workqueue(mcu_info->mcu_wq);
	if (mcu_info->cal_wq) destroy_workqueue(mcu_info->cal_wq);

err_create_singlethread_workqueue:
err_mcu_setup:
	mutex_destroy(&MSP430FR2311_control_mutex);
	mutex_destroy(&MSP430FR2311_openclose_mutex);
	misc_deregister(&mcu_misc); //lightsensor_setup
	//err_initial_MSP430FR2311_gpio:
	gpio_free(mcu_info->mcu_reset); 
	gpio_free(mcu_info->mcu_test); 

err_platform_data_null:
	kfree(mcu_info);
	g_motor_status = 0; //probe fail
	return ret;
}

static int MSP430FR2311_remove(struct i2c_client *client)
{
	int ret = 0;
	printk("[MSP430][%s] +\n", __func__);

	MSP430FR2311_power_control(0);
	regulator_disable(mcu_info->angle_vdd);
	regulator_put(mcu_info->angle_vdd);

	device_destroy(mcu_info->MSP430FR2311_class, mcu_info->mcu_dev->devt);
	class_destroy(mcu_info->MSP430FR2311_class);

	if (/*active_panel && */mcu_info->notifier_cookie){
		panel_event_notifier_unregister(mcu_info->notifier_cookie);
	}

	if (mcu_info->mcu_wq) destroy_workqueue(mcu_info->mcu_wq);
	if (mcu_info->cal_wq) destroy_workqueue(mcu_info->cal_wq);
	if (mcu_info->interrupt_wq) destroy_workqueue(mcu_info->interrupt_wq);

	mutex_destroy(&MSP430FR2311_control_mutex);
	mutex_destroy(&MSP430FR2311_openclose_mutex);
	misc_deregister(&mcu_misc); //lightsensor_setup

	//Release all IRQ & GPIO
	free_irq(mcu_info->mcu_irq, mcu_info);
	gpio_free(mcu_info->mcu_reset); 
	gpio_free(mcu_info->mcu_test); 
	gpio_free(mcu_info->mcu_power);
	gpio_free(mcu_info->mcu_int);

	kfree(mcu_info);
	g_motor_status = 0; //probe fail

	printk("[MSP430][%s] -\n", __func__);
	return ret;
}

static const struct i2c_device_id MSP430FR2311_i2c_id[] = {
	{MSP430FR2311_I2C_NAME, 0},
	{}
};

static struct of_device_id MSP430FR2311_match_table[] = {
	{ .compatible = "MCU,MSP430FR2311"},
	{ },
};

/*static int drm_check_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int i = 0;
	int count = 0;
	int retry = 10;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;
	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0) {
		printk("[MSP430] find drm_panel count(%d) fail", count);
		return -ENODEV;
	}
	for (retry = 10;retry >0;retry--){
		printk("[MSP430] retry count(%d)", retry);
		for (i = 0; i < count; i++) {
			node = of_parse_phandle(np, "panel", i);
			panel = of_drm_find_panel(node);
			of_node_put(node);
			if (!IS_ERR(panel)) {
				printk("[MSP430] find drm_panel successfully");
				active_panel = panel;
				return 0;
			}
		}
		msleep(2000);
    }
    printk("[MSP430] no find drm_panel");
    return -ENODEV;
}*/
static void msp430_panel_notifier_callback(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification, void *client_data)
{
//	printk("[MSP430][%s] +\n", __func__);
	if (!notification) {
		printk("[MSP430] Invalid notification\n");
		return;
	}
//	printk("[MSP430] Notification type:%d, early_trigger:%d",
//			notification->notif_type,
//			notification->notif_data.early_trigger);
	if(!notification->notif_data.early_trigger){ //early_trigger=0 and early_trigger=1 will call once each event, ignore one.
		return;
	}
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		printk("[MSP430] DRM_PANEL_EVENT_UNBLANK\n");
		cancel_delayed_work(&mcu_info->sleep_delayed_work);
		panel_status = 1;
		if (!regulator_is_enabled(mcu_info->angle_vdd)) {
			schedule_work(&msp_resume_work);
		}
		break;
	case DRM_PANEL_EVENT_BLANK:
		printk("[MSP430] DRM_PANEL_EVENT_BLANK\n");
		__pm_wakeup_event(mcu_info->suspend_lock, WAKELOCK_HOLD_TIME);
		panel_status = 0;
		queue_delayed_work(mcu_info->sleep_workqueue, &mcu_info->sleep_delayed_work,msecs_to_jiffies(2000));
		break;
	default:
//		printk("[MSP430] notification serviced\n");
		break;
	}
}
static void msp430_register_for_panel_events(struct device *dev)
{
	struct device_node *dp = dev->of_node;
	const char *touch_type;
	int rc = 0;
	void *cookie = NULL;

	rc = of_property_read_string(dp, "drm,type",
						&touch_type);
	if (rc) {
		printk("[MSP430][%s] No type\n", __func__);
		return;
	}
	if (strcmp(touch_type, "primary")) {
		printk("[MSP430][%s] Invalid touch type\n", __func__);
		return;
	}
	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_MSP430, 0 /*active_panel*/,
			&msp430_panel_notifier_callback, (void *)dp);
	if (!cookie) {
		printk("[MSP430][%s] Failed to register for panel events\n", __func__);
		return;
	}
	//printk("[MSP430] registered for panel notifications panel: 0x%x\n",
	//		active_panel);
	mcu_info->notifier_cookie = cookie;
}

#ifdef CONFIG_PM_SLEEP
static int mcu_suspend(struct device *dev)
{
//	struct mcu_info *mpi;
//	mpi = dev_get_drvdata(dev);
#if 1
	printk("[MSP430][%s]\n", __func__);
#else
	printk("[MSP430][%s] go to power off\n", __func__);
	MSP430FR2311_power_control(0);
#endif
	return 0;
}

static int mcu_resume(struct device *dev)
{
//	struct mcu_info *mpi;
//	mpi = dev_get_drvdata(dev);
#if 1
	printk("[MSP430][%s]\n", __func__);
#else
	printk("[MSP430][%s] go to power on\n", __func__);
	MSP430FR2311_power_control(1);
#endif
	return 0;
}
#endif

static UNIVERSAL_DEV_PM_OPS(mcu_pm, mcu_suspend, mcu_resume, NULL);

static struct i2c_driver MSP430FR2311_driver = {
	.id_table = MSP430FR2311_i2c_id,
	.probe = MSP430FR2311_probe,
	.remove = MSP430FR2311_remove,
	.driver = {
		.name = MSP430FR2311_I2C_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm = &mcu_pm,	
#endif
		.of_match_table = of_match_ptr(MSP430FR2311_match_table),     
	},
};

static int __init MSP430FR2311_init(void)
{
	int ret;

	ret = i2c_add_driver(&MSP430FR2311_driver);
	if (ret)
		printk("[MSP430][%s] TI MSP430 driver init failed.\n", __func__);
	else
		printk("[MSP430][%s] TI MSP430 driver init success.\n", __func__);
	
	return ret;
}

static void __exit MSP430FR2311_exit(void)
{
	i2c_del_driver(&MSP430FR2311_driver);
}

module_init(MSP430FR2311_init);
module_exit(MSP430FR2311_exit);

MODULE_AUTHOR("Randy Change <randy_change@asus.com>");
MODULE_DESCRIPTION("MCU MSP430FR2311 micro processor Driver");
MODULE_LICENSE("GPL v2");

//==========================================Zen7========================================
//=====
static int file_op(const char *filename, loff_t offset, char *buf, int length, int operation)
{
	return -1;
/*
#if 0
	struct file *filep;
	mm_segment_t old_fs;
	int ret = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if(FILE_OP_READ == operation)
		filep= filp_open(filename, O_RDONLY | O_CREAT | O_SYNC, 0777);
	else if(FILE_OP_WRITE == operation)
		filep= filp_open(filename, O_RDWR | O_CREAT | O_SYNC, 0777);
	else {
		////ksys_close(filep);
		set_fs(old_fs);
		printk("[MSP430] Unknown partition op err!\n");
		return -1;
	}

	if(IS_ERR(filep)) {
		//ksys_close(filep);
		set_fs(old_fs);
		printk("[MSP430] open %s err! error code:%d\n", filename, filep);
		return -1;
	}else {
		printk("[MSP430] open %s success!\n", filename);
	}

	//ksys_chown(filename, 1015, 1015);	//#define AID_SDCARD_RW 1015
	if(FILE_OP_READ == operation)
		ret = kernel_read(filep, buf, length, &offset);
	else if(FILE_OP_WRITE == operation) {
		ret = kernel_write(filep, buf, length, &offset);
		vfs_fsync(filep, 0);
	}

	filp_close(filep, NULL);
	set_fs(old_fs);
	printk("[MSP430][%s] %s length:%d ret:%d.\n", __func__, operation?"w":"r", length, ret);

	return length;
#endif
*/ 
}

//Backup 110 bytes to mcu_bak.
static int backup_mcu_cal_thed(void)
{
	char buf[400]={0};
	char MCUBuf[10];
	unsigned char i;
	int rc;

	printk("[MSP430][%s]\n", __func__);
	for(i=0; i<CalLength; i++) {
		sprintf(MCUBuf, " %X", fcal_val.Cal_val[i]);
		strcat(buf, MCUBuf);		
	}

	rc = file_op(MCU_BAKEUP_FILE_NAME, CAL_DATA_OFFSET, (char *)&buf, 3*CalLength*sizeof(char), FILE_OP_WRITE);
	if(rc < 0)
		printk("[MSP430][%s] Write file:%s err!\n", __func__, MCU_BAKEUP_FILE_NAME);

	return rc;
}

//Read 110 bytes to mcu.
static int read_mcu_cal_thed(void)
{
	char buf[400]={0};
	int rc;
	unsigned char i;
	uint8_t wBuf[CalLength+3] = {0xAA, 0x55, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	
	printk("[MSP430][%s] +\n", __func__);

	for(i=0; i<CalLength; i++)
		fcal_val.Cal_val[i] = 0;

	rc = file_op(MCU_BAKEUP_FILE_NAME, CAL_DATA_OFFSET, (char *)&buf, 3*CalLength*sizeof(char), FILE_OP_READ);
	if(rc < 0) {
		printk("%s:read file:%s err!\n", __FUNCTION__, MCU_BAKEUP_FILE_NAME);
		return rc;
	}

	//printk("[MSP430]:%s\n", buf);

	sscanf(buf, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", \
		&(fcal_val.Cal_val[0]), &(fcal_val.Cal_val[1]), &(fcal_val.Cal_val[2]), &(fcal_val.Cal_val[3]), &(fcal_val.Cal_val[4]),	\
		&(fcal_val.Cal_val[5]), &(fcal_val.Cal_val[6]), &(fcal_val.Cal_val[7]), &(fcal_val.Cal_val[8]), &(fcal_val.Cal_val[9]),	\
		&(fcal_val.Cal_val[10]), &(fcal_val.Cal_val[11]), &(fcal_val.Cal_val[12]), &(fcal_val.Cal_val[13]), &(fcal_val.Cal_val[14]),	\
		&(fcal_val.Cal_val[15]), &(fcal_val.Cal_val[16]), &(fcal_val.Cal_val[17]), &(fcal_val.Cal_val[18]), &(fcal_val.Cal_val[19]),	\
		&(fcal_val.Cal_val[20]), &(fcal_val.Cal_val[21]), &(fcal_val.Cal_val[22]), &(fcal_val.Cal_val[23]),	\
		&(fcal_val.Cal_val[24]), &(fcal_val.Cal_val[25]), &(fcal_val.Cal_val[26]), &(fcal_val.Cal_val[27]), &(fcal_val.Cal_val[28]), &(fcal_val.Cal_val[29]), &(fcal_val.Cal_val[30]), &(fcal_val.Cal_val[31]), &(fcal_val.Cal_val[32]), &(fcal_val.Cal_val[33]),	\
		&(fcal_val.Cal_val[34]), &(fcal_val.Cal_val[35]), &(fcal_val.Cal_val[36]), &(fcal_val.Cal_val[37]), &(fcal_val.Cal_val[38]), &(fcal_val.Cal_val[39]), &(fcal_val.Cal_val[40]), &(fcal_val.Cal_val[41]), &(fcal_val.Cal_val[42]), &(fcal_val.Cal_val[43]),	\
		&(fcal_val.Cal_val[44]), &(fcal_val.Cal_val[45]), &(fcal_val.Cal_val[46]), &(fcal_val.Cal_val[47]), &(fcal_val.Cal_val[48]), &(fcal_val.Cal_val[49]), &(fcal_val.Cal_val[50]), &(fcal_val.Cal_val[51]), &(fcal_val.Cal_val[52]), &(fcal_val.Cal_val[53]),	\
		&(fcal_val.Cal_val[54]), &(fcal_val.Cal_val[55]), &(fcal_val.Cal_val[56]), &(fcal_val.Cal_val[57]), &(fcal_val.Cal_val[58]), &(fcal_val.Cal_val[59]), &(fcal_val.Cal_val[60]), &(fcal_val.Cal_val[61]), &(fcal_val.Cal_val[62]), &(fcal_val.Cal_val[63]),	\
		&(fcal_val.Cal_val[64]), &(fcal_val.Cal_val[65]), &(fcal_val.Cal_val[66]), &(fcal_val.Cal_val[67]), &(fcal_val.Cal_val[68]), &(fcal_val.Cal_val[69]), &(fcal_val.Cal_val[70]), &(fcal_val.Cal_val[71]), &(fcal_val.Cal_val[72]), &(fcal_val.Cal_val[73]),	\
		&(fcal_val.Cal_val[74]), &(fcal_val.Cal_val[75]), &(fcal_val.Cal_val[76]), &(fcal_val.Cal_val[77]), &(fcal_val.Cal_val[78]), &(fcal_val.Cal_val[79]), &(fcal_val.Cal_val[80]), &(fcal_val.Cal_val[81]), &(fcal_val.Cal_val[82]), &(fcal_val.Cal_val[83]),	\
		&(fcal_val.Cal_val[84]), &(fcal_val.Cal_val[85]), &(fcal_val.Cal_val[86]), &(fcal_val.Cal_val[87]), &(fcal_val.Cal_val[88]), &(fcal_val.Cal_val[89]), &(fcal_val.Cal_val[90]), &(fcal_val.Cal_val[91]), &(fcal_val.Cal_val[92]), &(fcal_val.Cal_val[93]),	\
		&(fcal_val.Cal_val[94]), &(fcal_val.Cal_val[95]), &(fcal_val.Cal_val[96]), &(fcal_val.Cal_val[97]), &(fcal_val.Cal_val[98]), &(fcal_val.Cal_val[99]), &(fcal_val.Cal_val[100]), &(fcal_val.Cal_val[101]), &(fcal_val.Cal_val[102]), &(fcal_val.Cal_val[103]),	\
		&(fcal_val.Cal_val[104]), &(fcal_val.Cal_val[105]), &(fcal_val.Cal_val[106]), &(fcal_val.Cal_val[107]), &(fcal_val.Cal_val[108]), &(fcal_val.Cal_val[109]));	

	printk("[MSP430][%s] %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x...%x %x %x %x, %x %x %x %x %x %x.\n", __func__, \
		fcal_val.Cal_val[0], fcal_val.Cal_val[1], fcal_val.Cal_val[2], fcal_val.Cal_val[3], fcal_val.Cal_val[4],	\
		fcal_val.Cal_val[5], fcal_val.Cal_val[6], fcal_val.Cal_val[7], fcal_val.Cal_val[8], fcal_val.Cal_val[9],	\
		fcal_val.Cal_val[10], fcal_val.Cal_val[11], fcal_val.Cal_val[12], fcal_val.Cal_val[13], fcal_val.Cal_val[14],	\
		fcal_val.Cal_val[15], fcal_val.Cal_val[16], fcal_val.Cal_val[17], fcal_val.Cal_val[18], fcal_val.Cal_val[19],	\
		fcal_val.Cal_val[20], fcal_val.Cal_val[21], fcal_val.Cal_val[22], fcal_val.Cal_val[23],	\
		fcal_val.Cal_val[100], fcal_val.Cal_val[101], fcal_val.Cal_val[102], fcal_val.Cal_val[103],	\
		fcal_val.Cal_val[104], fcal_val.Cal_val[105], fcal_val.Cal_val[106], fcal_val.Cal_val[107], fcal_val.Cal_val[108], fcal_val.Cal_val[109]);

	for(i=0; i<CalLength; i++)
		wBuf[3+i] = fcal_val.Cal_val[i];

	Zen7_MSP430FR2311_wI2CtoMCU(wBuf, sizeof(wBuf));

	printk("[MSP430][%s] -\n", __func__);
	return rc;
}

static int backup_angle_raw(unsigned char index, unsigned char *wRawBuf)
{
	char buf[200]={0};
	int rc;
	char MCUBuf[10];
	unsigned char i;

	printk("[MSP430][%s] \n", __func__);
	/*	
	sprintf(buf, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", \
		wRawBuf[0], wRawBuf[1], wRawBuf[2], wRawBuf[3], wRawBuf[4],	\
		wRawBuf[5], wRawBuf[6], wRawBuf[7], wRawBuf[8], wRawBuf[9],	\
		wRawBuf[10], wRawBuf[11], wRawBuf[12], wRawBuf[13], wRawBuf[14],	\
		wRawBuf[15], wRawBuf[16], wRawBuf[17], wRawBuf[18], wRawBuf[19]);	
	*/
	for(i=0; i<RawLength; i++){
		sprintf(MCUBuf, " %X", wRawBuf[i]);
		strcat(buf, MCUBuf);		
	}

	rc = file_op(MCU_BAKEUP_RAW_FILE_NAME, index*3*RawLength*sizeof(char), (char *)&buf, 3*RawLength*sizeof(char), FILE_OP_WRITE);
	if (rc < 0)
		printk("[MSP430][%s] Write file:%s err!\n", __FUNCTION__, MCU_BAKEUP_RAW_FILE_NAME);

	return rc;
}

//Backup 800+120 bytes to mcu_raw.
static void bak_raw(void){
	unsigned char i = 0;
	unsigned char rRawBuf[RawLength];
	uint8_t wBuf[4] = {0xAA, 0x55, 0x80, 0x00};

	printk("[MSP430][%s]\n",__func__);
	for(i=0; i<23; i++) {	//20 bufferMag + 3 rawAK9970Corr.
		//0xAA 0x55 0x80 index, let mcu prepare 20 bytes raw data.
		wBuf[3] = i;
		Zen7_MSP430FR2311_wI2CtoMCU(wBuf, sizeof(wBuf));

		//read and write file.
		if(MSP430_I2CRead(MSP430_READY_I2C, rRawBuf, RawLength)) {
			backup_angle_raw(i, rRawBuf);
		}else
			printk("[MSP430][%s] i2c raw backup (%d) read error!\n", __func__, i);
	}

	//copy file to factory folder.
	//report_motor_event(MOTOR_COPY_MCU_FILE, 3);	// /asdf/mcu_raw --> factory.
	printk("[MSP430][%s] done.\n", __func__);
}

//==========================================================================for User K
//Check file is null. 0:null, 1:not null.
static char IsFileNull(const char *filename){
	return 0;
/*
#if 0
	struct kstat stat;
	struct file *fp;
	mm_segment_t fs;
	int rc;

	fp = filp_open(filename, O_RDWR, 0644);
	if (IS_ERR(fp)) {
		printk("Open file error!\n");
		return -1;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	rc = vfs_getattr(&fp->f_path, &stat, STATX_INO, AT_STATX_SYNC_AS_STAT);

	filp_close(fp, NULL);
	set_fs(fs);
	if(stat.size <= 0)
		return 0;
	else
		return 1;
#endif
*/
}

//Read motor_fw2 110 bytes to mcu.
static int read_usek_cal3threshlod(void)
{
	char buf[400]={0};
	int rc;
	unsigned char i;
	uint8_t wBuf[CalLength+3] = {0xAA, 0x55, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	printk("[MSP430][%s]\n", __func__);

	for(i=0; i<CalLength; i++)
		fcal_val_UserK.Cal_val[i] = 0;

	rc = file_op(MCU_BAKEUP_USER_FILE_NAME, CAL_DATA_OFFSET, (char *)&buf, 3*CalLength*sizeof(char), FILE_OP_READ);
	if(rc < 0) {
		printk("[MSP430][%s] read file:%s err!\n", __func__, MCU_BAKEUP_USER_FILE_NAME);
		return rc;
	}

	sscanf(buf, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", \
		&(fcal_val_UserK.Cal_val[0]), &(fcal_val_UserK.Cal_val[1]), &(fcal_val_UserK.Cal_val[2]), &(fcal_val_UserK.Cal_val[3]), &(fcal_val_UserK.Cal_val[4]),	\
		&(fcal_val_UserK.Cal_val[5]), &(fcal_val_UserK.Cal_val[6]), &(fcal_val_UserK.Cal_val[7]), &(fcal_val_UserK.Cal_val[8]), &(fcal_val_UserK.Cal_val[9]),	\
		&(fcal_val_UserK.Cal_val[10]), &(fcal_val_UserK.Cal_val[11]), &(fcal_val_UserK.Cal_val[12]), &(fcal_val_UserK.Cal_val[13]), &(fcal_val_UserK.Cal_val[14]),	\
		&(fcal_val_UserK.Cal_val[15]), &(fcal_val_UserK.Cal_val[16]), &(fcal_val_UserK.Cal_val[17]), &(fcal_val_UserK.Cal_val[18]), &(fcal_val_UserK.Cal_val[19]),	\
		&(fcal_val_UserK.Cal_val[20]), &(fcal_val_UserK.Cal_val[21]), &(fcal_val_UserK.Cal_val[22]), &(fcal_val_UserK.Cal_val[23]),	\
		&(fcal_val_UserK.Cal_val[24]), &(fcal_val_UserK.Cal_val[25]), &(fcal_val_UserK.Cal_val[26]), &(fcal_val_UserK.Cal_val[27]), &(fcal_val_UserK.Cal_val[28]), &(fcal_val_UserK.Cal_val[29]), &(fcal_val_UserK.Cal_val[30]), &(fcal_val_UserK.Cal_val[31]), &(fcal_val_UserK.Cal_val[32]), &(fcal_val_UserK.Cal_val[33]),	\
		&(fcal_val_UserK.Cal_val[34]), &(fcal_val_UserK.Cal_val[35]), &(fcal_val_UserK.Cal_val[36]), &(fcal_val_UserK.Cal_val[37]), &(fcal_val_UserK.Cal_val[38]), &(fcal_val_UserK.Cal_val[39]), &(fcal_val_UserK.Cal_val[40]), &(fcal_val_UserK.Cal_val[41]), &(fcal_val_UserK.Cal_val[42]), &(fcal_val_UserK.Cal_val[43]),	\
		&(fcal_val_UserK.Cal_val[44]), &(fcal_val_UserK.Cal_val[45]), &(fcal_val_UserK.Cal_val[46]), &(fcal_val_UserK.Cal_val[47]), &(fcal_val_UserK.Cal_val[48]), &(fcal_val_UserK.Cal_val[49]), &(fcal_val_UserK.Cal_val[50]), &(fcal_val_UserK.Cal_val[51]), &(fcal_val_UserK.Cal_val[52]), &(fcal_val_UserK.Cal_val[53]),	\
		&(fcal_val_UserK.Cal_val[54]), &(fcal_val_UserK.Cal_val[55]), &(fcal_val_UserK.Cal_val[56]), &(fcal_val_UserK.Cal_val[57]), &(fcal_val_UserK.Cal_val[58]), &(fcal_val_UserK.Cal_val[59]), &(fcal_val_UserK.Cal_val[60]), &(fcal_val_UserK.Cal_val[61]), &(fcal_val_UserK.Cal_val[62]), &(fcal_val_UserK.Cal_val[63]),	\
		&(fcal_val_UserK.Cal_val[64]), &(fcal_val_UserK.Cal_val[65]), &(fcal_val_UserK.Cal_val[66]), &(fcal_val_UserK.Cal_val[67]), &(fcal_val_UserK.Cal_val[68]), &(fcal_val_UserK.Cal_val[69]), &(fcal_val_UserK.Cal_val[70]), &(fcal_val_UserK.Cal_val[71]), &(fcal_val_UserK.Cal_val[72]), &(fcal_val_UserK.Cal_val[73]),	\
		&(fcal_val_UserK.Cal_val[74]), &(fcal_val_UserK.Cal_val[75]), &(fcal_val_UserK.Cal_val[76]), &(fcal_val_UserK.Cal_val[77]), &(fcal_val_UserK.Cal_val[78]), &(fcal_val_UserK.Cal_val[79]), &(fcal_val_UserK.Cal_val[80]), &(fcal_val_UserK.Cal_val[81]), &(fcal_val_UserK.Cal_val[82]), &(fcal_val_UserK.Cal_val[83]),	\
		&(fcal_val_UserK.Cal_val[84]), &(fcal_val_UserK.Cal_val[85]), &(fcal_val_UserK.Cal_val[86]), &(fcal_val_UserK.Cal_val[87]), &(fcal_val_UserK.Cal_val[88]), &(fcal_val_UserK.Cal_val[89]), &(fcal_val_UserK.Cal_val[90]), &(fcal_val_UserK.Cal_val[91]), &(fcal_val_UserK.Cal_val[92]), &(fcal_val_UserK.Cal_val[93]),	\
		&(fcal_val_UserK.Cal_val[94]), &(fcal_val_UserK.Cal_val[95]), &(fcal_val_UserK.Cal_val[96]), &(fcal_val_UserK.Cal_val[97]), &(fcal_val_UserK.Cal_val[98]), &(fcal_val_UserK.Cal_val[99]), &(fcal_val_UserK.Cal_val[100]), &(fcal_val_UserK.Cal_val[101]), &(fcal_val_UserK.Cal_val[102]), &(fcal_val_UserK.Cal_val[103]),	\
		&(fcal_val_UserK.Cal_val[104]), &(fcal_val_UserK.Cal_val[105]), &(fcal_val_UserK.Cal_val[106]), &(fcal_val_UserK.Cal_val[107]), &(fcal_val_UserK.Cal_val[108]), &(fcal_val_UserK.Cal_val[109]));	

	printk("[MSP430][%s] %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x...%x %x %x %x, %x %x %x %x %x %x.\n", __func__, \
		fcal_val_UserK.Cal_val[0], fcal_val_UserK.Cal_val[1], fcal_val_UserK.Cal_val[2], fcal_val_UserK.Cal_val[3], fcal_val_UserK.Cal_val[4],	\
		fcal_val_UserK.Cal_val[5], fcal_val_UserK.Cal_val[6], fcal_val_UserK.Cal_val[7], fcal_val_UserK.Cal_val[8], fcal_val_UserK.Cal_val[9],	\
		fcal_val_UserK.Cal_val[10], fcal_val_UserK.Cal_val[11], fcal_val_UserK.Cal_val[12], fcal_val_UserK.Cal_val[13], fcal_val_UserK.Cal_val[14],	\
		fcal_val_UserK.Cal_val[15], fcal_val_UserK.Cal_val[16], fcal_val_UserK.Cal_val[17], fcal_val_UserK.Cal_val[18], fcal_val_UserK.Cal_val[19],	\
		fcal_val_UserK.Cal_val[20], fcal_val_UserK.Cal_val[21], fcal_val_UserK.Cal_val[22], fcal_val_UserK.Cal_val[23],	\
		fcal_val_UserK.Cal_val[100], fcal_val_UserK.Cal_val[101], fcal_val_UserK.Cal_val[102], fcal_val_UserK.Cal_val[103],	\
		fcal_val_UserK.Cal_val[104], fcal_val_UserK.Cal_val[105], fcal_val_UserK.Cal_val[106], fcal_val_UserK.Cal_val[107], fcal_val_UserK.Cal_val[108], fcal_val_UserK.Cal_val[109]);

	for(i=0; i<CalLength; i++)
		wBuf[3+i] = fcal_val_UserK.Cal_val[i];

	Zen7_MSP430FR2311_wI2CtoMCU(wBuf, sizeof(wBuf));
	return rc;
}

//Read MCU calibration data to compare with phone stored.
static int CompareCaliData(char *filename)
{
	char buf[400]={0};
	int rc;
	unsigned char i;
	uint8_t wBuf[CalLength+3] = {0xAA, 0x55, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t wFlg = 0;

	printk("[MSP430][%s] filename %s\n", __func__, filename);

	for(i=0; i<CalLength; i++)
		fcal_val_rPhone.Cal_val[i] = 0;

	rc = file_op(filename, CAL_DATA_OFFSET, (char *)&buf, 3*CalLength*sizeof(char), FILE_OP_READ);
	if(rc < 0) {
		printk("[MSP430][%s] read file:%s err!\n", __func__, filename);
		return rc;
	}

	sscanf(buf, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", \
		&(fcal_val_rPhone.Cal_val[0]), &(fcal_val_rPhone.Cal_val[1]), &(fcal_val_rPhone.Cal_val[2]), &(fcal_val_rPhone.Cal_val[3]), &(fcal_val_rPhone.Cal_val[4]),	\
		&(fcal_val_rPhone.Cal_val[5]), &(fcal_val_rPhone.Cal_val[6]), &(fcal_val_rPhone.Cal_val[7]), &(fcal_val_rPhone.Cal_val[8]), &(fcal_val_rPhone.Cal_val[9]),	\
		&(fcal_val_rPhone.Cal_val[10]), &(fcal_val_rPhone.Cal_val[11]), &(fcal_val_rPhone.Cal_val[12]), &(fcal_val_rPhone.Cal_val[13]), &(fcal_val_rPhone.Cal_val[14]),	\
		&(fcal_val_rPhone.Cal_val[15]), &(fcal_val_rPhone.Cal_val[16]), &(fcal_val_rPhone.Cal_val[17]), &(fcal_val_rPhone.Cal_val[18]), &(fcal_val_rPhone.Cal_val[19]),	\
		&(fcal_val_rPhone.Cal_val[20]), &(fcal_val_rPhone.Cal_val[21]), &(fcal_val_rPhone.Cal_val[22]), &(fcal_val_rPhone.Cal_val[23]),	\
		&(fcal_val_rPhone.Cal_val[24]), &(fcal_val_rPhone.Cal_val[25]), &(fcal_val_rPhone.Cal_val[26]), &(fcal_val_rPhone.Cal_val[27]), &(fcal_val_rPhone.Cal_val[28]), &(fcal_val_rPhone.Cal_val[29]), &(fcal_val_rPhone.Cal_val[30]), &(fcal_val_rPhone.Cal_val[31]), &(fcal_val_rPhone.Cal_val[32]), &(fcal_val_rPhone.Cal_val[33]),	\
		&(fcal_val_rPhone.Cal_val[34]), &(fcal_val_rPhone.Cal_val[35]), &(fcal_val_rPhone.Cal_val[36]), &(fcal_val_rPhone.Cal_val[37]), &(fcal_val_rPhone.Cal_val[38]), &(fcal_val_rPhone.Cal_val[39]), &(fcal_val_rPhone.Cal_val[40]), &(fcal_val_rPhone.Cal_val[41]), &(fcal_val_rPhone.Cal_val[42]), &(fcal_val_rPhone.Cal_val[43]),	\
		&(fcal_val_rPhone.Cal_val[44]), &(fcal_val_rPhone.Cal_val[45]), &(fcal_val_rPhone.Cal_val[46]), &(fcal_val_rPhone.Cal_val[47]), &(fcal_val_rPhone.Cal_val[48]), &(fcal_val_rPhone.Cal_val[49]), &(fcal_val_rPhone.Cal_val[50]), &(fcal_val_rPhone.Cal_val[51]), &(fcal_val_rPhone.Cal_val[52]), &(fcal_val_rPhone.Cal_val[53]),	\
		&(fcal_val_rPhone.Cal_val[54]), &(fcal_val_rPhone.Cal_val[55]), &(fcal_val_rPhone.Cal_val[56]), &(fcal_val_rPhone.Cal_val[57]), &(fcal_val_rPhone.Cal_val[58]), &(fcal_val_rPhone.Cal_val[59]), &(fcal_val_rPhone.Cal_val[60]), &(fcal_val_rPhone.Cal_val[61]), &(fcal_val_rPhone.Cal_val[62]), &(fcal_val_rPhone.Cal_val[63]),	\
		&(fcal_val_rPhone.Cal_val[64]), &(fcal_val_rPhone.Cal_val[65]), &(fcal_val_rPhone.Cal_val[66]), &(fcal_val_rPhone.Cal_val[67]), &(fcal_val_rPhone.Cal_val[68]), &(fcal_val_rPhone.Cal_val[69]), &(fcal_val_rPhone.Cal_val[70]), &(fcal_val_rPhone.Cal_val[71]), &(fcal_val_rPhone.Cal_val[72]), &(fcal_val_rPhone.Cal_val[73]),	\
		&(fcal_val_rPhone.Cal_val[74]), &(fcal_val_rPhone.Cal_val[75]), &(fcal_val_rPhone.Cal_val[76]), &(fcal_val_rPhone.Cal_val[77]), &(fcal_val_rPhone.Cal_val[78]), &(fcal_val_rPhone.Cal_val[79]), &(fcal_val_rPhone.Cal_val[80]), &(fcal_val_rPhone.Cal_val[81]), &(fcal_val_rPhone.Cal_val[82]), &(fcal_val_rPhone.Cal_val[83]),	\
		&(fcal_val_rPhone.Cal_val[84]), &(fcal_val_rPhone.Cal_val[85]), &(fcal_val_rPhone.Cal_val[86]), &(fcal_val_rPhone.Cal_val[87]), &(fcal_val_rPhone.Cal_val[88]), &(fcal_val_rPhone.Cal_val[89]), &(fcal_val_rPhone.Cal_val[90]), &(fcal_val_rPhone.Cal_val[91]), &(fcal_val_rPhone.Cal_val[92]), &(fcal_val_rPhone.Cal_val[93]),	\
		&(fcal_val_rPhone.Cal_val[94]), &(fcal_val_rPhone.Cal_val[95]), &(fcal_val_rPhone.Cal_val[96]), &(fcal_val_rPhone.Cal_val[97]), &(fcal_val_rPhone.Cal_val[98]), &(fcal_val_rPhone.Cal_val[99]), &(fcal_val_rPhone.Cal_val[100]), &(fcal_val_rPhone.Cal_val[101]), &(fcal_val_rPhone.Cal_val[102]), &(fcal_val_rPhone.Cal_val[103]),	\
		&(fcal_val_rPhone.Cal_val[104]), &(fcal_val_rPhone.Cal_val[105]), &(fcal_val_rPhone.Cal_val[106]), &(fcal_val_rPhone.Cal_val[107]), &(fcal_val_rPhone.Cal_val[108]), &(fcal_val_rPhone.Cal_val[109]));	

	printk("[MSP430][%s] %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x...%x %x %x %x, %x %x %x %x %x %x.\n", __func__, \
		fcal_val_rPhone.Cal_val[0], fcal_val_rPhone.Cal_val[1], fcal_val_rPhone.Cal_val[2], fcal_val_rPhone.Cal_val[3], fcal_val_rPhone.Cal_val[4],	\
		fcal_val_rPhone.Cal_val[5], fcal_val_rPhone.Cal_val[6], fcal_val_rPhone.Cal_val[7], fcal_val_rPhone.Cal_val[8], fcal_val_rPhone.Cal_val[9],	\
		fcal_val_rPhone.Cal_val[10], fcal_val_rPhone.Cal_val[11], fcal_val_rPhone.Cal_val[12], fcal_val_rPhone.Cal_val[13], fcal_val_rPhone.Cal_val[14],	\
		fcal_val_rPhone.Cal_val[15], fcal_val_rPhone.Cal_val[16], fcal_val_rPhone.Cal_val[17], fcal_val_rPhone.Cal_val[18], fcal_val_rPhone.Cal_val[19],	\
		fcal_val_rPhone.Cal_val[20], fcal_val_rPhone.Cal_val[21], fcal_val_rPhone.Cal_val[22], fcal_val_rPhone.Cal_val[23],	\
		fcal_val_rPhone.Cal_val[100], fcal_val_rPhone.Cal_val[101], fcal_val_rPhone.Cal_val[102], fcal_val_rPhone.Cal_val[103],	\
		fcal_val_rPhone.Cal_val[104], fcal_val_rPhone.Cal_val[105], fcal_val_rPhone.Cal_val[106], fcal_val_rPhone.Cal_val[107], fcal_val_rPhone.Cal_val[108], fcal_val_rPhone.Cal_val[109]);

	for(i=0; i<CalLength; i++) {
		wBuf[3+i] = fcal_val_rPhone.Cal_val[i];

		if(fcal_val_rPhone.Cal_val[i] != fcal_val_rMCU.Cal_val[i]) {
			wFlg = 1;
			printk("[MSP430][%s] i:%d, rPhone:%x, rMCU:%x.\n", __func__, i, fcal_val_rPhone.Cal_val[i], fcal_val_rMCU.Cal_val[i]);
		}
	}

	if(wFlg == 0) {
		printk("[MSP430][%s] no need update phone's cali data to mcu.\n", __func__);
	}else
		Zen7_MSP430FR2311_wI2CtoMCU(wBuf, sizeof(wBuf));

	printk("[MSP430][%s] end.\n", __func__);
	return rc;
}

void file_unlink(const char *filename)
{
/*
#if 0
	struct file *filp;
	mm_segment_t old_fs;
	struct inode *parent_inode;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	filp= filp_open(filename, O_RDWR | O_CREAT | O_SYNC, 0777);
	parent_inode = filp->f_path.dentry->d_parent->d_inode;
	inode_lock(parent_inode);
	vfs_unlink(parent_inode, filp->f_path.dentry, NULL);
	inode_unlock(parent_inode);

	filp_close(filp, NULL);
	set_fs(old_fs);
#endif
*/
}

//Reset user calibration data
static void UserCaliReset(void){
	printk("[MSP430][%s] !!!!!!!!!!!!!!!!!!\n", __func__);
	file_unlink(MCU_BAKEUP_USER_FILE_NAME);
	read_mcu_cal_thed();	//read factory file to mcu.
	printk("[MSP430][%s] done.\n", __func__);
}

//Backup 110 bytes to mcu_bak.
static int Backup_user_cal_thed(void)
{
	char buf[400]={0};
	char MCUBuf[10];
	unsigned char i;
	int rc;

	printk("[MSP430][%s]\n", __func__);

	for(i=0; i<CalLength; i++) {
		sprintf(MCUBuf, " %X", fcal_val_UserK.Cal_val[i]);
		strcat(buf, MCUBuf);		
	}

	rc = file_op(MCU_BAKEUP_USER_FILE_NAME, CAL_DATA_OFFSET, (char *)&buf, 3*CalLength*sizeof(char), FILE_OP_WRITE);
	if (rc<0)
		printk("[MSP430][%s] write file:%s err!\n", __func__, MCU_BAKEUP_USER_FILE_NAME);
	
	return rc;
}

static void mcu_cal_work(struct work_struct *work){
	unsigned char i;

	printk("[MSP430][%s] UserKState:%d.\n", __func__, UserKState);

	if(UserKState == UserK_SaveK){
		Backup_user_cal_thed();
		UserKState = UserK_TBD;
		return;
	}

	if(UserKState == UserK_NoSaveK){
		for(i=0; i<CalLength; i++)
			fcal_val_UserK.Cal_val[i] = 0;		
		
		UserKState = UserK_TBD;
		if(IsFileNull(MCU_BAKEUP_USER_FILE_NAME) == 1) {
			read_usek_cal3threshlod();	//read motor_fw2 file to mcu.
		}else {
			read_mcu_cal_thed();		//read factory file to mcu.
		}

		return;
	}

	if(UserKState == UserK_Reset) {
		UserCaliReset();
		UserKState = UserK_TBD;
		return;
	}
	
	if(UserKState == UserK_AbortK) {
		for(i=0; i<CalLength; i++)
			fcal_val_UserK.Cal_val[i] = 0;	
				
		UserKState = UserK_TBD;
		if(IsFileNull(MCU_BAKEUP_USER_FILE_NAME) == 1){
			read_usek_cal3threshlod();	//read motor_fw2 file to mcu.
		}else {
			read_mcu_cal_thed();		//read factory file to mcu.
		}	

		return;
	}
}

static void akm_INT_work(struct work_struct *work){
	int TmpStatus;

	//printk("[MSP430][%s] +\n", __func__);
	TmpStatus = judgeDoorState(0);
	printk("[MSP430][%s] DoorStatus %d, judgeDoorState(0) %d\n", __func__, DoorStatus, TmpStatus);
	int_count++;

	if ((DoorStatus == DOOR_CLOSE) && (TmpStatus == DOOR_UNKNOWN)){
		printk("[MSP430][%s] MOTOR_FORCE open !!!\n", __func__);
		report_motor_event(MOTOR_FORCE, 0x0);
	}
	return;
}
//=====

int Zen7_MSP430FR2311_Set_ParamMode(const uint16_t* vals) {
	unsigned char i = 0;
	//Total len 22: 0xAA 0x55 0x0C dir freq*6 step*6 mode*6.
	unsigned char MSP430ParamMode[]={0xAA, 0x55, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	//printk("[MSP430][%s] +\n", __func__);
	//Copy dir freq*6 step*6 mode*6.
	for(i=0; i<19; i++) {
		MSP430ParamMode[3+i] = (unsigned char)vals[i];
	}
/*
	printk("[MSP430][%s] vals=%d, %d %d %d %d %d %d, %d %d %d %d %d %d, %d %d %d %d %d %d\n", __func__,
			vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7], vals[8], vals[9], vals[10], vals[11], vals[12],
			vals[13], vals[14], vals[15], vals[16], vals[17], vals[18]);
*/
	printk("[MSP430][%s] dump param=%d, %d %d %d %d %d %d, %d %d %d %d %d %d, %d %d %d %d %d %d\n", __func__,
			MSP430ParamMode[3], MSP430ParamMode[4], MSP430ParamMode[5], MSP430ParamMode[6], MSP430ParamMode[7], MSP430ParamMode[8], MSP430ParamMode[9], 
			MSP430ParamMode[10], MSP430ParamMode[11], MSP430ParamMode[12], MSP430ParamMode[13], MSP430ParamMode[14], MSP430ParamMode[15],
			MSP430ParamMode[16], MSP430ParamMode[17], MSP430ParamMode[18], MSP430ParamMode[19], MSP430ParamMode[20], MSP430ParamMode[21]);

	mutex_lock(&MSP430FR2311_control_mutex);
	MSP430FR2311_wakeup(1);
	if(MCUState < MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d\n", __func__, MCUState);
		mutex_unlock(&MSP430FR2311_control_mutex);
		return -MCUState;
	}

	//Not ure this time's value.
	powerDownDuration=DEFAULT_POWERDOWNDURATION;
	bShowStopInfoOnce=1;

	if(!MSP430_I2CWriteA(MSP430_READY_I2C, MSP430ParamMode, sizeof(MSP430ParamMode))) {
		MSP430FR2311_wakeup(0);
		mutex_unlock(&MSP430FR2311_control_mutex);

		printk("[MSP430][%s] I2C error!\n", __func__);
		return -1;
	}

	MSP430FR2311_wakeup(0);
	mutex_unlock(&MSP430FR2311_control_mutex);

	//printk("[MSP430][%s] -\n", __func__);
	return 0;
}

//Zen7 I2C write format: 0xAA 0x55 cmd xx....
static int Zen7_MSP430FR2311_wI2CtoMCU(uint8_t *buf, uint8_t len){

	printk("[MSP430][%s] cmd:0x%x\n", __func__, buf[2]);
	mutex_lock(&MSP430FR2311_control_mutex);
	MSP430FR2311_wakeup(1);
	if(MCUState<MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d\n", __func__, MCUState);
		mutex_unlock(&MSP430FR2311_control_mutex);
		return -MCUState;
	}
	
	if(!MSP430_I2CWriteA(MSP430_READY_I2C, buf, len)) {
		MSP430FR2311_wakeup(0);
		mutex_unlock(&MSP430FR2311_control_mutex);

		printk("[MSP430][%s] I2C error!\n", __func__);
		return -1;
	}

	MSP430FR2311_wakeup(0);
	mutex_unlock(&MSP430FR2311_control_mutex);

	return 0;
}

//Zen7 I2C read Format. Protocol: s slave_add(w) 0xAA 0x55 cmd stop; msleep(100); s slave_add(r) xx... stop;  
uint8_t dAngle[4];
int Zen7_MSP430FR2311_rI2CtoCPU(uint8_t cmd, uint8_t *rBuf, uint8_t len) {
	uint8_t CmdBuf[3] = {0xAA, 0x55, 0x00};
	//uint8_t i =0;

	CmdBuf[2] = cmd;
	printk("[MSP430][%s] cmd:0x%x\n", __func__, CmdBuf[2]);

	MSP430FR2311_wakeup(1);
	if(MCUState<MCU_READY) {
		printk("[MSP430][%s] Not ready!, state=%d\n", __func__, MCUState);
		mutex_unlock(&MSP430FR2311_control_mutex);
		return -MCUState;
	}
	MSP430_I2CWriteA(MSP430_READY_I2C, CmdBuf, sizeof(CmdBuf));
	//msleep(3);	//Delay to wait MCU prepare data.
	if(!MSP430_I2CRead(MSP430_READY_I2C, rBuf, len)) {
		printk("[MSP430][%s] I2C error!", __func__);
		MSP430FR2311_wakeup(0);
		return -1;
	}
	MSP430FR2311_wakeup(0);

	//for(i=0; i<len; i++){
	//	printk("%x", rBuf[i]);
	//}

	return 0;
}

//Proc note for deal with angle cal and read angle. 
int Zen7_MSP430FR2311_DealAngle(uint16_t *buf, uint8_t len) {
	int rc = 0;
	uint8_t wBuf[7] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t i;
	uint8_t CmdBuf[3] = {0xAA, 0x55, 0x00};
	uint8_t rAngle[6];

	printk("[MSP430][%s] +\n",__func__);
	for(i=0; i<len; i++) {
		wBuf[2+i] = (unsigned char)buf[i];
	}

	switch(wBuf[2]){
		//Angle sensor cal.
		case 0x61:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Read angle raw data.
		case 0x62:
			//rc = Zen7_MSP430FR2311_rI2CtoCPU(0x62, dAngle, sizeof(dAngle));
			
			//In order to reduce printf cat angle log.
			MSP430FR2311_wakeup(1);
			CmdBuf[2] = 0x62;
			MSP430_I2CWriteA_NoLog(MSP430_READY_I2C, CmdBuf, sizeof(CmdBuf));
			//msleep(20);	//Delay to wait MCU prepare data.
			if(!MSP430_I2CRead_NoLog(MSP430_READY_I2C, rAngle, sizeof(rAngle))) {
				printk("[MSP430][%s] I2C error!", __func__);
				MSP430FR2311_wakeup(0);
				rc = -1;
			}

			dAngle[0] = rAngle[2];
			dAngle[1] = rAngle[3];
			dAngle[2] = rAngle[4];
			dAngle[3] = rAngle[5];
			MSP430FR2311_wakeup(0);
			break;
		
		//Set MCU auto report angle.	// no used now
		case 0x63:
			printk("[MSP430][%s] wBuf[2]=0x%02x, FW not support now.", __func__, wBuf[2]);
			//rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Set angle stop thredhold. Format: echo 0x64 Upthreshold(179) Downthreshold(1) > motor_angle.	// no used now
		case 0x64:
			printk("[MSP430][%s] wBuf[2]=0x%02x, FW not support now.", __func__, wBuf[2]);
			//rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Dis(1)/En(0) judge angle stop condition. echo 135 disable(1) > motor_angle.
		case 0x87:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Specified angle. echo 254 x > motor_angle.
		case 0xFE:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;
		
		default:
			printk("[MSP430][%s] param fail!\n", __func__);
			rc = -1;
			break;
	}

    return rc;
}

//Proc note handle for set drv param.
uint8_t drv_state[7];
int Zen7_MSP430FR2311_wrDrv(uint16_t        *buf, uint8_t len){
	int rc = 0;
	uint8_t wBuf[7] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00};	//len 7 is cmd 0x40's length.
	uint8_t i;

	printk("[MSP430][%s]\n", __func__);
	for(i=0; i<len; i++) {
		wBuf[2+i] = (unsigned char)buf[i];
	}

	switch(wBuf[2])
	{
		//Update drv state.
		case 0x20:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Read drv state.
		case 0x21:
			rc = Zen7_MSP430FR2311_rI2CtoCPU(0x21, drv_state, sizeof(drv_state));
			break;

		//Write drv. 0xAA 0x55 0x40 , SPI_REG_CTRL1, SPI_REG_CTRL2, SPI_REG_CTRL3, SPI_REG_CTRL4 // not used now
		case 0x40:
			printk("[MSP430][%s] wBuf[2]=0x%02x, FW not support now.", __func__, wBuf[2]);
			//rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));	//2 is header 0xAA 0x55.
			break;

		//Write vref. 0xAA  0x55  0x60  0x32(Vref  duty=50), 0x64 is 100%.
		case 0x60:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		default:
			printk("[MSP430][%s] param fail!\n", __func__);
			rc = -1;
			break;
	}

	return rc;
}

//Proc note handle for set akm AK09973D param.
int Zen7_MSP430FR2311_wrAKM(uint16_t *buf, uint8_t len){
	int rc = 0;
	uint8_t wBuf[27] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};	//len 27 is cmd 0x65's length.
	uint8_t i;

	for(i=0; i<len; i++){
		wBuf[2+i] = (unsigned char)buf[i];
	}

	printk("[MSP430][%s] cmd:%d\n", __func__, wBuf[2]);
	switch(wBuf[2]){
		//Write akm. 0xAA 0x55 0x65 + 24bytes threadhold.
		//SW reset & init & enable interrupt on akm
		case 0x65:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));	//2 is header 0xAA 0x55.
			break;

		//Update akm raw data to MCU buffer.
		case 0x66:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Read akm raw data from MCU buffer. Didn't read REG[0x17] from akm
		case 0x67:
			rc = Zen7_MSP430FR2311_rI2CtoCPU(0x67, akm_temp, sizeof(akm_temp));
			break;

		//Disable akm interrupt
		case 0x68:
			rc = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, (2+len));
			break;

		//Read raw_data, (BOP1Y_15_0, BOP1Z_15_0), (BOP1Y_15_0, BRP1Z_15_0), (BRP1Y_15_0, BOP1Z_15_0), (BRP1Y_15_0, BRP1Z_15_0).
		case 0x69:
			rc = Zen7_MSP430FR2311_rI2CtoCPU(0x69, akm_Ktemp, sizeof(akm_Ktemp));
			break;

		default:
			printk("[MSP430][%s] param fail!\n", __func__);
			rc = -1;
			break;
	}

	return rc;
}

//Proc note handle for motor_k.
uint8_t k_temp[2];
int Zen7_MSP430FR2311_wrMotorK(uint16_t *buf, uint8_t len){
	int rc = 0;
	uint8_t wBuf[10] = {0xAA, 0x55, 0x86};	//len 3 is cmd 0x86's length.
	uint8_t i;

	printk("[MSP430][%s]\n", __func__);
	for(i=0; i<len; i++) {
		wBuf[2+i] = (unsigned char)buf[i];
	}
	
	switch(wBuf[2]){
		//Read motor_k. 0xAA 0x55 0x86 xx xx.
		case 0x86:
			rc = Zen7_MSP430FR2311_rI2CtoCPU(0x86, k_temp, sizeof(k_temp));
			break;

		default:
			printk("[MSP430][%s] param fail!\n", __func__);
			rc = -1;
			break;
	}

	return rc;
}

//Report motor event to IMS.
#define KEY_MCU_CODE  "KEY_MCU_CODE"	/*Send SubSys UEvent+*/
#define KEY_MCU_VALUE "KEY_MCU_VALUE"	/*Send SubSys UEvent+*/

void report_motor_event(uint8_t OpCode, uint32_t value){
	char mKey_Buf[64];
	char mValue_Buf[512];
	char *envp[] = {mKey_Buf, mValue_Buf, NULL };

	printk("[MSP430][%s] OpCode 0x%x, value 0x%x\n", __func__, OpCode, value);

	snprintf(mKey_Buf, sizeof(mKey_Buf), "%s=%d", KEY_MCU_CODE, OpCode);
	snprintf(mValue_Buf, sizeof(mValue_Buf), "%s=%d", KEY_MCU_VALUE, value);

	if(kobject_uevent_env(&((mcu_misc.this_device)->kobj), KOBJ_CHANGE, envp) != 0) {
		printk("[MSP430][%s] kobject_uevent_env fail...\n", __func__);
	}else {
		//printk("[MSP430][%s] kobject_uevent_env ok.\n", __func__);
	}
}

//Init Motor stop condition
void mStopConditionInit(void){
	uint8_t wBuf[11] = {0xAA, 0x55, 0x85, 1, 179, 4, 0, 73, 4, 0, 73};	
	int ret = 0;

	printk("[MSP430][%s]\n", __func__);

	wBuf[3] = StopCondition[0];
	wBuf[4] = StopCondition[1];
	wBuf[5] = StopCondition[2];
	wBuf[6] = StopCondition[3];
	wBuf[7] = StopCondition[4];
	wBuf[8] = StopCondition[5];
	wBuf[9] = StopCondition[6];
	wBuf[10] = StopCondition[7];

	ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, sizeof(wBuf));		//2header + 6bytes + 3bytes.
	if(ret < 0)
		printk("[MSP430][%s] mStopConditionInit failed.\n", __func__);
	else
		printk("[MSP430][%s] mStopConditionInit ok.\n", __func__);

	#ifdef ASUS_FTM_BUILD
		wBuf[2] = 0x87;
		wBuf[3] = 1;	//Enable
		ret = Zen7_MSP430FR2311_wI2CtoMCU(wBuf, 4);		//2header + 2bytes.
		if(ret < 0)
			printk("[MSP430][%s] disable judge angle failed.\n", __func__);
		else
			printk("[MSP430][%s] disable judge angle ok.\n", __func__);
	#endif

	//Compare MCU's calibration data with phone backed.
	if(!Zen7_MSP430FR2311_rI2CtoCPU(0x70, &(fcal_val_rMCU.Cal_val[0]), CalLength)) {
		if(IsFileNull(MCU_BAKEUP_USER_FILE_NAME) == 1){
			printk("[MSP430][%s] CompareCaliData userk %d.\n", __func__, scinitFlg);
			CompareCaliData(MCU_BAKEUP_USER_FILE_NAME);		//read userk file to mcu.
		}else {
			if(IsFileNull(MCU_BAKEUP_FILE_NAME) == 1) {
				printk("[MSP430][%s] CompareCaliData fctoryk %d.\n", __func__, scinitFlg);
				CompareCaliData(MCU_BAKEUP_FILE_NAME);			//read factory file to mcu.
			}
		}
	}else
		printk("[MSP430][%s] read calibration data from mcu error!\n", __func__);					
}

//========================================================Trigger WQ========================================
void WQ_Trigger(void){
	printk("[MSP430][%s]\n", __func__);
	if (1 == bProbeFail) {
		printk("[MSP430][%s] probe fail. Don't queue work to avoid data abort!\n", __func__);
		return;
	}

	if(mcu_info->mcu_wq) {
		cancel_delayed_work(&report_work);
		queue_delayed_work(mcu_info->mcu_wq, &report_work, msecs_to_jiffies(10));
		printk("[MSP430][%s] success.\n", __func__);
	}
}

unsigned char KdState(void){
	return MCUState;
}
