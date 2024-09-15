#include <linux/proc_fs.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/delay.h>

#include "i2cbsl.h"
#include "MSP430FR2311.h"

#define PROC_ENTRY_NAME "asus_motor"
#define PROC_ENTRY_NUM 22
static struct proc_dir_entry *proc_handle[PROC_ENTRY_NUM];

#define PROC_MOTOR_POWER        "motor_power"
#define PROC_MOTOR_MANUAL_MODE  "motor_manual"
#define PROC_MOTOR_AUTO_MODE    "motor_auto"
#define PROC_MOTOR_STOP         "motor_stop"
#define	PROC_MOTOR_ATD_STATUS	"motor_atd_status"
#define	PROC_MOTOR_PROBE_STATUS "motor_probe_status"
#define PROC_MOTOR_PARAM_MODE   "motor_param"
#define PROC_MOTOR_ANGLE_MODE   "motor_angle"
#define PROC_MOTOR_DRV_MODE     "motor_drv"
#define PROC_MOTOR_AKM_MODE     "motor_akm"
#define PROC_MOTOR_AKM_RAW_Z    "motor_akm_raw_z"
#define PROC_MOTOR_AKM_RAW_Y    "motor_akm_raw_y"
#define PROC_MOTOR_AKM_RAW_X    "motor_akm_raw_x"
#define PROC_MOTOR_INT          "motor_int"
#define PROC_MOTOR_DOOR_STATE   "motor_door_state"
#define PROC_MOTOR_DOOR_STATE_FAC   "motor_door_state_fac"
#define PROC_ROG6_K             "rog6_k"
#define PROC_MOTOR_K    		"motor_k"
#define PROC_WQ_RUN    			"motor_wq_run"
#define PROC_MOTOR_STATE	    "motor_state"
#define PROC_MOTOR_KANGLE	    "motor_tk_angle"
#define PROC_MOTOR_FWVER	    "motor_fwver"

static struct proc_dir_entry *MSP430FR2311_proc_root;
static struct MSP430FR2311_info * motor_ctrl = NULL;

unsigned char g_motor_status = 0;

uint8_t g_motor_power_state = 0;
uint8_t g_motor_mode = 255;
uint16_t g_motor_dir = 255;
uint16_t g_motor_degree = 255;
uint16_t g_motor_speed = 255;

static uint8_t g_atd_status = 0;	//fail

extern int open_z, open_x, close_z, close_x;
extern unsigned int int_count;
extern enum door_status DoorStatus;

static int motor_atd_status_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", g_atd_status);
	g_atd_status = 0;	//default is failure

	return 0;
}

static int motor_atd_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_atd_status_proc_read, NULL);
}

static ssize_t motor_atd_status_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t rc;
	char messages[16]="";
	uint32_t val;

	rc = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)){
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%d", &val);
	switch(val)
	{
		case 0:
			g_atd_status = 0;
			break;
		case 1:
			g_atd_status = 1;
			break;
		default:
			g_atd_status = 1;
	}

	printk("[MSP430][%s] ATD status changed to %d\n", __func__, g_atd_status);
	return rc;
}

static const struct proc_ops motor_atd_status_fops = {
	.proc_open = motor_atd_status_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_atd_status_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_solo_power_read(struct seq_file *buf, void *v)
{
    printk("[MSP430][%s] g_motor_power_state = %d\n", __func__, g_motor_power_state);
	seq_printf(buf, "%d\n", g_motor_power_state);
	return 0;
}

static int motor_solo_power_open(struct inode *inode, struct  file *file)
{
	return single_open(file, motor_solo_power_read, NULL);
}

static ssize_t motor_solo_power_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	int val;
	int rc;

	ret_len = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)){
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	if(val == 0){
		if(g_motor_power_state == 1){

			rc = MSP430FR2311_power_control(0);
			if (rc) {
				printk("[MSP430][%s] motor power off fail rc = %d\n", __func__, rc);
			} else{
				g_motor_power_state = 0;
				printk("[MSP430][%s] Motor POWER DOWN\n", __func__);
			}

		}else {
			printk("[MSP430][%s] Motor already power donw, ignored.\n", __func__);
		}
	}else {
		if(g_motor_power_state == 0){	
			rc = MSP430FR2311_power_control(1);
			if (rc) {
				printk("[MSP430][%s] motor power up fail rc = %d\n", __func__, rc);
			}else {
				g_motor_power_state = 1;
				printk("[MSP430][%s] Motor POWER UP\n", __func__);
			}
		}else {
			printk("[MSP430][%s] Motor already power up, ignored.\n", __func__);
		}
	}

	return ret_len;
}

static const struct proc_ops motor_solo_power_fops = {
	.proc_open = motor_solo_power_open,
	.proc_read = seq_read,
	.proc_write = motor_solo_power_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_manual_proc_read(struct seq_file *buf, void *v)
{
	printk("[MSP430][%s] g_motor_dir %d g_motor_degree %d g_motor_speed %d\n", __func__, g_motor_dir, g_motor_degree, g_motor_speed);

	seq_printf(buf, "[MSP430][%s] g_motor_dir %d g_motor_degree %d g_motor_speed %d\n", __func__, g_motor_dir, g_motor_degree, g_motor_speed);
	return 0;
}

static int motor_manual_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_manual_proc_read, NULL);
}

static ssize_t motor_manual_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16] = "";
	uint16_t val[3];
	int rc = 0;

	ret_len = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d %d %d",&val[0], &val[1], &val[2]);
    g_motor_dir = val[0];
	g_motor_degree = val[1];
	g_motor_speed = val[2];

    switch(g_motor_dir)
    {
		case 0:
			printk("[MSP430][%s] Motor toward up\n", __func__);
			break;
		case 1:
			printk("[MSP430][%s] Motor toward down\n", __func__);
			break;
		default:
			printk("[MSP430][%s] Not supported command %d\n", __func__, g_motor_dir);
			rc = -1;
	}

	printk("[MSP430][%s] degree %d speed %d\n", __func__, g_motor_degree, g_motor_speed);
	if(rc < 0 || g_motor_degree < 0 || g_motor_degree > 180){
		g_atd_status = 0;
    }else {
		if(g_motor_speed > 10)
			g_motor_speed = 10;

		//do motor manual here
	    rc = MSP430FR2311_Set_ManualMode(g_motor_dir, g_motor_degree, g_motor_speed);	//speed set to 5 by default
		if(rc < 0)
			g_atd_status = 0;
		else
			g_atd_status = 1;
	}

	return ret_len;
}

static const struct proc_ops motor_manual_mode_fops = {
	.proc_open = motor_manual_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_manual_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_auto_proc_read(struct seq_file *buf, void *v)
{
	printk("[MSP430][%s] g_motor_dir %d g_motor_degree %d\n", __func__, g_motor_dir, g_motor_degree);
	seq_printf(buf, "[MSP430][%s] g_motor_dir %d g_motor_degree %d\n", __func__, g_motor_dir, g_motor_degree);
	return 0;
}

static int motor_auto_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_auto_proc_read, NULL);
}

static ssize_t motor_auto_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16] = "";
	uint16_t val[2];
	int rc = 0;

	printk("[MSP430][%s] auto mode = %d, Angle = %d\n", __func__, val[0], val[1]);

	ret_len = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	//do motor auto here
	sscanf(messages,"%d %d",&val[0], &val[1]);
	rc = MSP430FR2311_Set_AutoModeWithAngle(val[0], val[1]);
	if(rc < 0)
		g_atd_status = 0;
	else
		g_atd_status = 1;

	return ret_len;
}

static const struct proc_ops motor_auto_mode_fops = {
	.proc_open = motor_auto_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_auto_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_stop_proc_read(struct seq_file *buf, void *v)
{
	printk("[MSP430][%s] g_motor_dir %d g_motor_degree %d\n", __func__, g_motor_dir, g_motor_degree);
	seq_printf(buf, "[MSP430][%s] g_motor_dir %d g_motor_degree %d\n", __func__, g_motor_dir, g_motor_degree);
	return 0;
}

static int motor_stop_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_stop_proc_read, NULL);
}

static ssize_t motor_stop_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint16_t val;
	int rc = 0;

    printk("[MSP430][%s] degree %d\n", __func__, g_motor_degree);

	ret_len = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

    //do motor stop here
	sscanf(messages,"%d",&val);
    rc = MSP430FR2311_Stop();
	if(rc < 0)
		g_atd_status = 0;
	else
		g_atd_status = 1;

	return ret_len;
}

static const struct proc_ops motor_stop_mode_fops = {
	.proc_open = motor_stop_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_stop_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_probe_status_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", g_motor_status);
	return 0;
}

static int motor_probe_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_probe_status_proc_read, NULL);
}

static const struct proc_ops motor_probe_status_fops = {
	.proc_open = motor_probe_status_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

extern char gFWVersion[];
static int motor_param_proc_read(struct seq_file *buf, void *v)
{
	int rc = MSP430FR2311_Set_AutoMode(221);

	if(rc == 0) {
		seq_printf(buf, "[MSP430] Firmware version=%d%02d%02d%02X\n", gFWVersion[0], gFWVersion[1], gFWVersion[2], gFWVersion[3]);
	}else {
		seq_printf(buf, "[MSP430] Firmware version fail!!\n"); 
	}

	return rc;
}

static int motor_param_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_param_proc_read, NULL);
}

static ssize_t motor_param_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
#define MAX_PARAM_MSG_LEN 100
	char messages[MAX_PARAM_MSG_LEN]="";
	char Zen7messages[MAX_PARAM_MSG_LEN]="";
	uint16_t val[20];
	uint16_t Zen7Val[20];	//Dir freq*6 step*6 mode*6 EndFlage(0xFD).
	int rc = 0;

	if(len > MAX_PARAM_MSG_LEN)
		len = MAX_PARAM_MSG_LEN;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}else{
		//Delete will lead to Zen7Val's value error.
		memcpy(Zen7messages, messages, sizeof(messages));
	}

	//sscanf(messages,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d",&val[0], &val[1], &val[2],&val[3], &val[4], &val[5],&val[6], &val[7], &val[8],&val[9], &val[10], &val[11], &val[12], &val[13]);
	sscanf(messages,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &val[0], &val[1], &val[2], &val[3], &val[4], &val[5], &val[6], &val[7], &val[8], &val[9], &val[10], &val[11], &val[12], &val[13], &val[14], &val[15], &val[16], &val[17], &val[18], &val[19]);

	printk("[MSP430][%s] dump param write = %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", __func__, val[0], val[1], val[2],val[3], val[4], val[5],val[6], val[7], val[8],val[9], val[10], val[11], val[12], val[13]);

	if (val[13] != 0xfe) {
		//Make sure that, zen7's 'mode value range is 0~10.
		if(1){	//(((val[13]&0x0F) >= 0) && ((val[13]&0x0F) <= 10))
			//sscanf(messages,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&Zen7Val[0], &Zen7Val[1], &Zen7Val[2], &Zen7Val[3], &Zen7Val[4], &Zen7Val[5], &Zen7Val[6], &Zen7Val[7], &Zen7Val[8], &Zen7Val[9], &Zen7Val[10], &Zen7Val[11], &Zen7Val[12], &Zen7Val[13], &Zen7Val[14], &Zen7Val[15], &Zen7Val[16], &Zen7Val[17], &Zen7Val[18], &Zen7Val[19]);
			memcpy(Zen7Val, val, sizeof(val));
			printk("[MSP430][%s] long cmd = %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", __func__,
				Zen7Val[0], Zen7Val[1], Zen7Val[2], Zen7Val[3], Zen7Val[4], Zen7Val[5], Zen7Val[6], Zen7Val[7], Zen7Val[8], Zen7Val[9], Zen7Val[10], Zen7Val[11], Zen7Val[12], 
				Zen7Val[13], Zen7Val[14], Zen7Val[15], Zen7Val[16], Zen7Val[17], Zen7Val[18], Zen7Val[19]);

			if(Zen7Val[19] != 0xfe){
				printk("[MSP430][%s] long cmd's param error, syntax error should be 20 parameters with 254 final end\n", __func__);
				rc=-1;
			}else {
				//Zen7 format.
				rc = Zen7_MSP430FR2311_Set_ParamMode(Zen7Val);
			}
		}else{
			printk("[MSP430][%s] read param error, syntax error should be 13 parameters with 254 final end\n", __func__);
			rc=-1;
		}
	} else {
		rc = MSP430FR2311_Set_ParamMode(val); //speed set to 5 by default
	}

	if(rc < 0)
	    g_atd_status = 0;
    else
	    g_atd_status = 1;

	return len;
}
static const struct proc_ops motor_param_mode_fops = {
	.proc_open = motor_param_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_param_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//===============Zen7===============
//Proc note for read angle sendor's angle raw data.
extern uint8_t dAngle[4];
static int motor_angle_proc_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x62;
	int rc = Zen7_MSP430FR2311_DealAngle(&cmd, 1);

	if (rc == 0) {
		seq_printf(buf, "[MSP430] Angle raw data:%02X %02X %02X %02X\n", dAngle[0], dAngle[1], dAngle[2], dAngle[3]);
	} else {
		seq_printf(buf, "[MSP430] Get angle fail!\n");
	}

	return 0;
}

static int motor_angle_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_angle_proc_read, NULL);
}

static ssize_t motor_angle_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16] = "";
	uint16_t val[4];
	int rc = 0;

	ret_len = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d %d %d %d", &val[0], &val[1], &val[2], &val[3]);
	printk("[MSP430][%s] val[0]:%d, val[1]:%d, val[2]:%d, val[3]:%d\n", __func__, val[0], val[1], val[2], val[3]);

	if(val[0] == 0x61)
		rc = Zen7_MSP430FR2311_DealAngle(val, 4);	//Extend cali cmd to 4 bytes.
	else
		rc = Zen7_MSP430FR2311_DealAngle(val, 3);

	if(rc < 0)
		g_atd_status = 0;
	else
		g_atd_status = 1;

	return ret_len;
}

static const struct proc_ops motor_angle_mode_fops = {
	.proc_open = motor_angle_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_angle_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//Proc note for r/w re-driver IC.
extern uint8_t drv_state[7];
static int motor_drv_proc_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x21;
	int rc = Zen7_MSP430FR2311_wrDrv(&cmd, 1);

	if (rc == 0) {
		seq_printf(buf, "[MSP430] drv state:%02X %02X %02X %02X %02X %02X %02X\n", drv_state[0], drv_state[1], drv_state[2], drv_state[3], drv_state[4], drv_state[5], drv_state[6]);
	}else {
		seq_printf(buf, "[MSP430] Get drv_state fail!\n");
	}

	return 0;
}

static int motor_drv_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, motor_drv_proc_read, NULL);
}

static ssize_t motor_drv_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint16_t val[5];
	int rc = 0;

	ret_len = len;
	if(len > 16)
		len = 16;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d %d %d %d %d",&val[0], &val[1], &val[2], &val[3] ,&val[4]);
	switch(val[0]){
		case 0x20:
			printk("[MSP430][%s] val[0]:%d\n", __func__, val[0]);
			rc = Zen7_MSP430FR2311_wrDrv(val, 1);	
			break;

		case 0x40:
			printk("[MSP430][%s] val[0]:%d, val[1]:%d, val[2]:%d, val[3]:%d, val[4]:%d\n", __func__, val[0], val[1], val[2], val[3], val[4]);
			rc = Zen7_MSP430FR2311_wrDrv(val, 5);
			break;

		case 0x60:
			printk("[MSP430][%s] val[0]:%d, val[1]:%d\n", __func__, val[0], val[1]);
			rc = Zen7_MSP430FR2311_wrDrv(val, 2);
			break;

		default:
			printk("[MSP430][%s] param error!\n",  __func__);
			rc=-1;
			break;
	}

	if(rc < 0)
		g_atd_status = 0;
	else
		g_atd_status = 1;

	return ret_len;
}

static const struct proc_ops motor_drv_mode_fops = {
	.proc_open = motor_drv_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_drv_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//Proc note for r/w angle IC.
extern uint8_t akm_temp[8];
static int motor_akm_proc_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x67;
	int rc = Zen7_MSP430FR2311_wrAKM(&cmd, 1);

	if (rc == 0) {
		seq_printf(buf, "[MSP430] akm raw data(reg:0x17): %02X %02X %02X %02X %02X %02X %02X %02X\n", akm_temp[0], akm_temp[1], akm_temp[2], akm_temp[3], akm_temp[4], akm_temp[5], akm_temp[6], akm_temp[7]);
	}else {
		seq_printf(buf, "[MSP430] Get akm raw data fail!\n"); 
	}

	return 0;
}

static int motor_akm_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_akm_proc_read, NULL);
}

#define MAX_PARAM_MSG_LEN 100
static ssize_t motor_akm_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char messages[MAX_PARAM_MSG_LEN]="";
	char Zen7messages[MAX_PARAM_MSG_LEN]="";
	uint16_t val[30];
	int rc = 0;

	if(len>MAX_PARAM_MSG_LEN)
		len=MAX_PARAM_MSG_LEN;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}else{
		//Delete will lead to Zen7Val's value error.
		memcpy(Zen7messages, messages, sizeof(messages));
	}

	//Max length(25): cmd + 24bytes.
	sscanf(Zen7messages,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&val[0], &val[1], &val[2], &val[3], &val[4], &val[5], &val[6], &val[7], &val[8], &val[9], &val[10], &val[11], &val[12], &val[13], &val[14], &val[15], &val[16], &val[17], &val[18], &val[19], &val[20], &val[21], &val[22], &val[23], &val[24]);
	/*
	sscanf(messages,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &val[0],	\
									  &val[1], &val[2], &val[3], &val[4], &val[5], &val[6], &val[7], &val[8],	\
									  &val[9], &val[10], &val[11], &val[12], &val[13], &val[14], &val[15], &val[16],	\
									  &val[17], &val[18], &val[19], &val[20], &val[21], &val[22], &val[23], &val[24]);
	*/
	switch(val[0]){
		case 0x65:
			printk("[MSP430][%s] val[0]:%d, val[1]:%d, val[2]:%d, val[3]:%d, val[4]:%d ... val[21]:%d, val[22]:%d, val[23]:%d, val[24]:%d.\n", __func__,
						val[0], val[1], val[2], val[3], val[4], val[21], val[22], val[23], val[24]);

			rc = Zen7_MSP430FR2311_wrAKM(val, 25);
			break;

		case 0x66:
			printk("[MSP430][%s] val[0]:%d\n", __func__, val[0]);
			rc = Zen7_MSP430FR2311_wrAKM(val, 1);
			break;

		case 0x68:
			printk("[MSP430][%s] val[0]:%d\n", __func__, val[0]);
			rc = Zen7_MSP430FR2311_wrAKM(val, 1);
			break;

		default:
			printk("[MSP430][%s] param error!",  __func__);
			rc=-1;
			break;
	}

	if(rc < 0)
		g_atd_status = 0;
	else
		g_atd_status = 1;

	return len;
}

static const struct proc_ops motor_akm_mode_fops = {
	.proc_open = motor_akm_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_akm_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_akm_raw_z_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x67;
	uint16_t value;
	int rc = Zen7_MSP430FR2311_wrAKM(&cmd, 1);

	if(rc == 0) {
		value = (akm_temp[1] << 8) + akm_temp[2];
		seq_printf(buf, "%d\n", (value > 32767 ? value-65536 : value));
	}else {
		seq_printf(buf, "[MSP430] Get akm raw data z fail!\n");
	}

	return 0;
}

static int motor_akm_raw_z_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_akm_raw_z_read, NULL);
}

static const struct proc_ops motor_akm_raw_z_fops = {
	.proc_open = motor_akm_raw_z_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_akm_raw_y_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x67;
	uint16_t value;
	int rc = Zen7_MSP430FR2311_wrAKM(&cmd, 1);

	if(rc == 0) {
		value = (akm_temp[3] << 8) + akm_temp[4];
		seq_printf(buf, "%d\n", (value > 32767 ? value-65536 : value));
	}else {
		seq_printf(buf, "[MSP430] Get akm raw data y fail!\n");
	}

	return 0;
}

static int motor_akm_raw_y_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_akm_raw_y_read, NULL);
}

static const struct proc_ops motor_akm_raw_y_fops = {
	.proc_open = motor_akm_raw_y_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_akm_raw_x_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x67;
	uint16_t value;
	int rc = Zen7_MSP430FR2311_wrAKM(&cmd, 1);

	if(rc == 0) {
		value = (akm_temp[5] << 8) + akm_temp[6];
		seq_printf(buf, "%d\n", (value > 32767 ? value-65536 : value));
	} else {
		seq_printf(buf, "[MSP430] Get akm raw data x fail!\n");
	}

	return 0;
}

static int motor_akm_raw_x_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_akm_raw_x_read, NULL);
}

static const struct proc_ops motor_akm_raw_x_fops = {
	.proc_open = motor_akm_raw_x_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int rog6_k_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "[MSP430] rog6 k raw data %d %d %d %d\n", open_z, open_x, close_z, close_x);
	return 0;
}

static int rog6_k_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, rog6_k_proc_read, NULL);
}

static ssize_t rog6_k_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char messages[MAX_PARAM_MSG_LEN]="";
	int val[6];

	if(len > MAX_PARAM_MSG_LEN)
		len = MAX_PARAM_MSG_LEN;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d %d %d %d %d %d",&val[0], &val[1], &val[2], &val[3], &val[4], &val[5]);
	printk("[MSP430][%s] val = %d %d %d %d %d %d\n", __func__, val[0], val[1], val[2], val[3], val[4], val[5]);

	if (val[0] < 32768)
		open_z = val[0];
	if (val[2] < 32768)
		open_x = val[2];
	if (val[3] < 32768)
		close_z = val[3];
	if (val[5] < 32768)
		close_x = val[5];

	return len;
}

static const struct proc_ops rog6_k_fops = {
	.proc_open = rog6_k_proc_open,
	.proc_read = seq_read,
	.proc_write = rog6_k_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_int_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", int_count);
	return 0;
}

static int motor_int_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_int_read, NULL);
}

static const struct proc_ops motor_int_fops = {
	.proc_open = motor_int_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int judgeDoorState(int fac)
{
	uint16_t cmd = 0x66;
	int z, x, delta_z, delta_x, criteria_open, criteria_close;

	printk("[MSP430][%s] fac = %d.\n", __func__, fac);
	if (1 == fac) {
		criteria_open = 90000;
		criteria_close = 90000;
	} else {
		criteria_open = 4000000;
		criteria_close = 160000;
	}

	Zen7_MSP430FR2311_wrAKM(&cmd, 1);
	msleep(10);
	cmd = 0x67;
	Zen7_MSP430FR2311_wrAKM(&cmd, 1);

	z = (akm_temp[1] << 8) + akm_temp[2];
	z = (z > 32767)? z-65536 : z;
	x = (akm_temp[5] << 8) + akm_temp[6];
	x = (x > 32767)? x-65536 : x;

	delta_z = z - open_z;
	delta_x = x - open_x;
	delta_z *= delta_z;
	delta_x *= delta_x;
	if ((delta_z+delta_x) < criteria_open)
		return DOOR_OPEN;

	delta_z = z - close_z;
	delta_x = x - close_x;
	//delta_z *= delta_z;
	delta_z = 0;
	delta_x *= delta_x;

	if ((delta_z+delta_x) < criteria_close)
		return DOOR_CLOSE;
	else
		return DOOR_UNKNOWN;
}
EXPORT_SYMBOL_GPL(judgeDoorState);

static int motor_door_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "Recorded door state=%d, Real=%d\n", DoorStatus, judgeDoorState(0));
	return 0;
}

static int motor_door_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_door_read, NULL);
}

static ssize_t motor_door_open_update(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint16_t val;

	ret_len = len;
	if(len > MAX_PARAM_MSG_LEN)
		len = MAX_PARAM_MSG_LEN;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

    //do motor stop here
	sscanf(messages,"%d",&val);

	if(val)
		DoorStatus = judgeDoorState(0);

    printk("[MSP430][%s] Update DoorStatus to %d\n", __func__, DoorStatus);
	return ret_len;
}

static const struct proc_ops motor_door_state_fops = {
	.proc_open = motor_door_open,
	.proc_read = seq_read,
	.proc_write = motor_door_open_update,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_door_fac_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "Recorded (factory) door state =%d, Real=%d\n", DoorStatus, judgeDoorState(1));
	return 0;
}

static int motor_door_fac_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_door_fac_read, NULL);
}

static ssize_t motor_door_fac_open_update(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint16_t val;

	ret_len = len;
	if(len > MAX_PARAM_MSG_LEN)
		len = MAX_PARAM_MSG_LEN;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

    //do motor stop here
	sscanf(messages,"%d",&val);

	if(val)
		DoorStatus = judgeDoorState(1);

    printk("[MSP430][%s] Update DoorStatus to %d\n", __func__, DoorStatus);
	return ret_len;
}

static const struct proc_ops motor_door_state_fac_fops = {
	.proc_open = motor_door_fac_open,
	.proc_read = seq_read,
	.proc_write = motor_door_fac_open_update,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//Proc note for motorK.
extern uint8_t k_temp[2];
static int motor_k_proc_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x86;
	int rc = Zen7_MSP430FR2311_wrMotorK(&cmd, 1);

	if(rc == 0) {
		seq_printf(buf, "[MSP430][%s] akm cal overflow cnt:%02X %02X\n", __func__, k_temp[0], k_temp[1]);
	}else {
		seq_printf(buf, "[MSP430][%s] Get motorK overflow count fail!\n", __func__);
	}

	return 0;
}

static int motor_k_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_k_proc_read, NULL);
}

static ssize_t motor_k_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	return len;
}

static const struct proc_ops motor_k_mode_fops = {
	.proc_open = motor_k_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_k_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//Proc note for wq_run.
uint8_t wq_temp = 0;
static int wq_run_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "[MSP430] wq_temp:%02X\n", wq_temp);
	return 0;
}

static int wq_run_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, wq_run_proc_read, NULL);
}

static ssize_t wq_run_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[4] = "";
	uint16_t val[2];

	ret_len = len;
	if(len > 4)
		len = 4;

	if(copy_from_user(messages, buff, len)) {
		printk("[MSP430][%s] command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d", &val[0]);
	if(val[0] == 1) {
		WQ_Trigger();
	}

	return ret_len;
}

static const struct proc_ops wq_run_mode_fops = {
	.proc_open = wq_run_proc_open,
	.proc_read = seq_read,
	.proc_write = wq_run_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//Proc note for kernel state.
extern unsigned char KdState(void);
static int motor_state_proc_read(struct seq_file *buf, void *v)
{
	unsigned char MCU_State = 0;

	MCU_State = KdState();
	seq_printf(buf, "[MSP430] State : %02X\n", MCU_State);

	return 0;
}

static int motor_state_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_state_proc_read, NULL);
}

static ssize_t motor_state_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	return len;
}

static const struct proc_ops motor_state_fops = {
	.proc_open = motor_state_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_state_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//Proc note for read threshold k angle.
extern uint8_t akm_Ktemp[20];
static int motor_Kangle_proc_read(struct seq_file *buf, void *v)
{
	uint16_t cmd = 0x69;
	int rc = Zen7_MSP430FR2311_wrAKM(&cmd, 1);

	if(rc == 0) {
		seq_printf(buf, "[MSP430][%s] tk: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __func__,
					akm_Ktemp[0], akm_Ktemp[1], akm_Ktemp[2], akm_Ktemp[3], akm_Ktemp[4], akm_Ktemp[5], akm_Ktemp[6], akm_Ktemp[7], akm_Ktemp[8], akm_Ktemp[9], akm_Ktemp[10], akm_Ktemp[11], akm_Ktemp[12], akm_Ktemp[13], akm_Ktemp[14], akm_Ktemp[15], akm_Ktemp[16], akm_Ktemp[17], akm_Ktemp[18], akm_Ktemp[19]);
	}else {
		seq_printf(buf, "[MSP430][%s] Get Kangle fail!\n", __func__);
	}

	return 0;
}

static int motor_Kangle_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_Kangle_proc_read, NULL);
}

static ssize_t motor_Kangle_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	return len;
}

static const struct proc_ops motor_Kangle_fops = {
	.proc_open = motor_Kangle_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_Kangle_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int motor_fwver_proc_read(struct seq_file *buf, void *v)
{
	u8 FWversion[4] = {0};
	int rc = 0;

	rc = MSP430FR2311_Get_Version(FWversion);
	if(rc == MSP430_STATUS_OPERATION_OK) {
		seq_printf(buf, "[MSP430][%s] FW VER: %02d %02d %02d %02d\n", __func__, FWversion[0], FWversion[1], FWversion[2], FWversion[3]);
	} else {
		seq_printf(buf, "[MSP430][%s] Get FW VER error!! 0x%02x\n", __func__, rc);
	}

	return 0;
}

static int motor_fwver_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, motor_fwver_proc_read, NULL);
}

static ssize_t motor_fwver_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	return len;
}

static const struct proc_ops motor_fwver_fops = {
	.proc_open = motor_fwver_proc_open,
	.proc_read = seq_read,
	.proc_write = motor_fwver_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//===============Zen7===============
/*
static void create_proc_file(const char *PATH,const struct proc_ops* f_ops)
{
	struct proc_dir_entry *pde;

	pde = proc_create(PATH, 0666, NULL, f_ops);
	if(pde) {
		printk("[MSP430][%s] create(%s) done\n", __func__, PATH);
	}else {
		printk("[MSP430][%s] create(%s) failed!\n", __func__, PATH);
	}
}
*/

static void create_motor_proc_files_factory(void)
{
	printk("[MSP430][%s]\n", __func__);
	MSP430FR2311_proc_root = proc_mkdir(PROC_ENTRY_NAME, NULL);

    proc_handle[0] = proc_create_data(PROC_MOTOR_POWER, 0666, MSP430FR2311_proc_root, &motor_solo_power_fops, NULL);
    proc_handle[1] = proc_create_data(PROC_MOTOR_MANUAL_MODE, 0666, MSP430FR2311_proc_root, &motor_manual_mode_fops, NULL);
    proc_handle[2] = proc_create_data(PROC_MOTOR_AUTO_MODE, 0666, MSP430FR2311_proc_root, &motor_auto_mode_fops, NULL);
    proc_handle[3] = proc_create_data(PROC_MOTOR_STOP, 0666, MSP430FR2311_proc_root, &motor_stop_mode_fops, NULL);
    proc_handle[4] = proc_create_data(PROC_MOTOR_ATD_STATUS, 0666, MSP430FR2311_proc_root, &motor_atd_status_fops, NULL);
    proc_handle[5] = proc_create_data(PROC_MOTOR_PROBE_STATUS, 0666, MSP430FR2311_proc_root, &motor_probe_status_fops, NULL);
    proc_handle[6] = proc_create_data(PROC_MOTOR_PARAM_MODE, 0666, MSP430FR2311_proc_root, &motor_param_mode_fops, NULL);
    proc_handle[7] = proc_create_data(PROC_MOTOR_ANGLE_MODE, 0666, MSP430FR2311_proc_root, &motor_angle_mode_fops, NULL);
    proc_handle[8] = proc_create_data(PROC_MOTOR_DRV_MODE, 0666, MSP430FR2311_proc_root, &motor_drv_mode_fops, NULL);
    proc_handle[9] = proc_create_data(PROC_MOTOR_AKM_MODE, 0666, MSP430FR2311_proc_root, &motor_akm_mode_fops, NULL);
    proc_handle[10] = proc_create_data(PROC_MOTOR_AKM_RAW_Z, 0666, MSP430FR2311_proc_root, &motor_akm_raw_z_fops, NULL);
    proc_handle[11] = proc_create_data(PROC_MOTOR_AKM_RAW_Y, 0666, MSP430FR2311_proc_root, &motor_akm_raw_y_fops, NULL);
    proc_handle[12] = proc_create_data(PROC_MOTOR_AKM_RAW_X, 0666, MSP430FR2311_proc_root, &motor_akm_raw_x_fops, NULL);
    proc_handle[13] = proc_create_data(PROC_MOTOR_INT, 0666, MSP430FR2311_proc_root, &motor_int_fops, NULL);
    proc_handle[14] = proc_create_data(PROC_MOTOR_DOOR_STATE, 0666, MSP430FR2311_proc_root, &motor_door_state_fops, NULL);
    proc_handle[15] = proc_create_data(PROC_MOTOR_DOOR_STATE_FAC, 0666, MSP430FR2311_proc_root, &motor_door_state_fac_fops, NULL);
    proc_handle[16] = proc_create_data(PROC_ROG6_K, 0666, MSP430FR2311_proc_root, &rog6_k_fops, NULL);
    proc_handle[17] = proc_create_data(PROC_MOTOR_K, 0666, MSP430FR2311_proc_root, &motor_k_mode_fops, NULL);
    proc_handle[18] = proc_create_data(PROC_WQ_RUN, 0666, MSP430FR2311_proc_root, &wq_run_mode_fops, NULL);
    proc_handle[19] = proc_create_data(PROC_MOTOR_STATE, 0666, MSP430FR2311_proc_root, &motor_state_fops, NULL);
    proc_handle[20] = proc_create_data(PROC_MOTOR_KANGLE, 0666, MSP430FR2311_proc_root, &motor_Kangle_fops, NULL);
    proc_handle[21] = proc_create_data(PROC_MOTOR_FWVER, 0666, MSP430FR2311_proc_root, &motor_fwver_fops, NULL);
}

void asus_motor_init(struct MSP430FR2311_info * ctrl)
{
	if(ctrl)
		motor_ctrl = ctrl;
	else
	{
		printk("[MSP430] msm_ois_ctrl_t passed in is NULL!\n");
		return;
	}
	create_motor_proc_files_factory();
}

static void remove_motor_proc_files_factory(void)
{
	//int i=0;
	printk("[MSP430][%s]\n", __func__);
/*
	for (i=0;i<PROC_ENTRY_NUM;i++){
		if (proc_handle[i])
			proc_remove(proc_handle[i]);
	}
*/
	remove_proc_entry(PROC_MOTOR_POWER, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_MANUAL_MODE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_AUTO_MODE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_STOP, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_ATD_STATUS, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_PROBE_STATUS, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_PARAM_MODE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_ANGLE_MODE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_DRV_MODE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_AKM_MODE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_AKM_RAW_Z, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_AKM_RAW_Y, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_AKM_RAW_X, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_INT, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_DOOR_STATE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_DOOR_STATE_FAC, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_ROG6_K, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_K, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_WQ_RUN, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_STATE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_KANGLE, MSP430FR2311_proc_root);
	remove_proc_entry(PROC_MOTOR_FWVER, MSP430FR2311_proc_root);

	proc_remove(MSP430FR2311_proc_root);
}

void asus_motor_remove(void)
{
	remove_motor_proc_files_factory();
}
