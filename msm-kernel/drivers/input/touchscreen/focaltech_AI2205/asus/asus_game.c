#include "focaltech_core.h"
#include "asus_tp.h"

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
int atr_slot[MAX_ATR_TOUCH_NUMBER] = {0}; // The slots store (key_id + 1), for the key_id start from 0
int atr_slot_x[MAX_ATR_TOUCH_NUMBER] = {0};
int atr_slot_y[MAX_ATR_TOUCH_NUMBER] = {0};

u8 slidling_sen = 0x0,sliding_acy = 0x0, touch_acy = 0x0, sliding_smo = 0x0;
u8 Rcoefleft = 0x0A, RcoefRight=0x0A;
int pre_angle = 0;
bool wait_resume = false;

int pre_report_rate = 1; //default 300Hz

/*****************************************************************************
* 1.Static function prototypes
*******************************************************************************/
static u16 shex_to_u16(const char *hex_buf, int size)
{
    int i;
    int base = 1;
    int value = 0;
    char single;

    for (i = size - 1; i >= 0; i--) {
        single = hex_buf[i];

        if ((single >= '0') && (single <= '9')) {
            value += (single - '0') * base;
        } else if ((single >= 'a') && (single <= 'z')) {
            value += (single - 'a' + 10) * base;
        } else if ((single >= 'A') && (single <= 'Z')) {
            value += (single - 'A' + 10) * base;
        } else {
            return -EINVAL;
        }
		base *= 10;
    }

    return value;
}

void reconfig_game_reg(bool reconfig) {
    int ret1 = 0 , ret2 = 0, ret3 = 0, ret4 = 0, i = 0;
    u8 value = 0x0;
    
    FTS_INFO("Reconfig game reg");
    mutex_lock(&fts_data->reg_lock);
    for (i = 0; i < 6; i++) {
		ret1 = fts_write_reg(FTS_REG_SLIDING_SENSITIVITY, slidling_sen);
		msleep(5);
		fts_read_reg(FTS_REG_SLIDING_SENSITIVITY, &value);
		if (value == slidling_sen){
			if (fts_data->log_level >= 2)
			FTS_DEBUG("set FTS_REG_SLIDING_SENSITIVITY 0x%x success",slidling_sen);
			break;
		} else {
			FTS_DEBUG("set FTS_REG_SLIDING_SENSITIVITY 0x%x failed,retry count %d",value, i);
			msleep(5);
		}
    }
	    
    for (i = 0; i < 6; i++) {
		ret2 = fts_write_reg(FTS_REG_SLIDING_PRECISION, sliding_acy);
		msleep(5);
		fts_read_reg(FTS_REG_SLIDING_PRECISION, &value);
		if (value == sliding_acy){
			if (fts_data->log_level >= 2)
			FTS_DEBUG("set FTS_REG_SLIDING_PRECISION 0x%x success",sliding_acy);
			break;
		} else {
			FTS_DEBUG("set FTS_REG_SLIDING_PRECISION 0x%x failed,retry count %d",value, i);
			msleep(5);
		}
    }

    for (i = 0; i < 6; i++) {
		ret3 = fts_write_reg(FTS_REG_TOUCH_SENSITIVITY, touch_acy);
		msleep(5);
		fts_read_reg(FTS_REG_TOUCH_SENSITIVITY, &value);
		if (value == touch_acy){
			if (fts_data->log_level >= 2)
			FTS_DEBUG("set FTS_REG_TOUCH_SENSITIVITY 0x%x success",touch_acy);
			break;
		} else {
			FTS_DEBUG("set FTS_REG_TOUCH_SENSITIVITY 0x%x failed,retry count %d",value, i);
			msleep(5);
		}
    }

    for (i = 0; i < 6; i++) {
		ret4 = fts_write_reg(FTS_REG_SLIDING_SMOOTHNESS, sliding_smo);
		msleep(5);
		fts_read_reg(FTS_REG_SLIDING_SMOOTHNESS, &value);
		if (value == sliding_smo){
			if (fts_data->log_level >= 2)
			FTS_DEBUG("set FTS_REG_SLIDING_SMOOTHNESS 0x%x success",sliding_smo);
			break;
		} else {
			FTS_DEBUG("set FTS_REG_SLIDING_SMOOTHNESS 0x%x failed,retry count %d",value, i);
			msleep(5);
		}
    }
    mutex_unlock(&fts_data->reg_lock);

}

void set_rotation_mode(void)
{
    struct fts_ts_data *ts_data = fts_data;
    int ret;
    u8 mode = 0;
    
// rotation to 0    
    if (ts_data->rotation_angle == ANGLE_0) {
      FTS_DEBUG("Rotation angle 0");
      ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 0x00);
      msleep(5);
      fts_read_reg(FTS_REG_EDGEPALM_MODE_EN, &mode);
      if (mode!=0x00) {
	FTS_INFO("Set to angle 0 fail reg read 0x%x",mode);
      }
    }
// rotation to 90
    if (ts_data->rotation_angle == ANGLE_90) {
      FTS_DEBUG("Rotation angle 90");
      ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 0x01);
      msleep(5);
      fts_read_reg(FTS_REG_EDGEPALM_MODE_EN, &mode);
      if (mode!=0x01) {
	FTS_INFO("Set to angle 90 fail reg read 0x%x",mode);
      }
    }

// rotation to 270
    if (ts_data->rotation_angle == ANGLE_270) {
      FTS_DEBUG("Rotation angle 270");
      ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 0x02);
      msleep(5);
      fts_read_reg(FTS_REG_EDGEPALM_MODE_EN, &mode);
      if (mode!=0x02) {
	FTS_INFO("Set to angle 270 fail reg read 0x%x",mode);
      }
    }    
}

void disable_edge_palm(void) {
    int ret;
    u8 mode = 0;
    
    if (fts_data->suspended) {
      wait_resume = true;
      return;
    }

    ret = fts_write_reg(FTS_REG_EDGEPALM_MODE_EN, 0x00);
    msleep(5);
    fts_read_reg(FTS_REG_EDGEPALM_MODE_EN, &mode);
    if (mode!=0x00) {
	FTS_INFO("Set to angle 0 fail reg read 0x%x",mode);
    } else
        FTS_INFO("Edge palm disabled");
 
    
    wait_resume = false;
}

void set_edge_palm(void) {
    struct fts_ts_data *ts_data = fts_data;
    u8 l_val = 0 , r_val = 0;
    int retl = 0, retr = 0;
    u8 r_set = 0, l_set = 0;
    
    if (fts_data->suspended) {
      wait_resume = true;
      return;
    }

    if (fts_data->edge_palm_enable == 0) { // game genie set edge palm disable
	mutex_lock(&fts_data->reg_lock);
	retl = fts_write_reg(FTS_REG_EDGEPALM_LEFT, 0x0);
	l_set = 0x0;
	msleep(5);
	retr = fts_write_reg(FTS_REG_EDGEPALM_RIGHT,0x0);
	r_set = 0x0;
	mutex_unlock(&fts_data->reg_lock);
    }
    
    if (fts_data->edge_palm_enable == 1) { // game genie set edge palm enable
        mutex_lock(&fts_data->reg_lock);
      	if (ts_data->rotation_angle == ANGLE_0)  {
	    retl = fts_write_reg(FTS_REG_EDGEPALM_LEFT, 0x0);
	    l_set = 0x0;
	    msleep(5);
	    retr = fts_write_reg(FTS_REG_EDGEPALM_RIGHT,0x0);
	    r_set = 0x0;
	}

	if ((ts_data->rotation_angle == ANGLE_90) || (ts_data->rotation_angle == ANGLE_270))  {
	    retl = fts_write_reg(FTS_REG_EDGEPALM_LEFT, Rcoefleft);
	    l_set = Rcoefleft;
	    msleep(5);
	    retr = fts_write_reg(FTS_REG_EDGEPALM_RIGHT, RcoefRight);
	    r_set = RcoefRight;
	}
	mutex_unlock(&fts_data->reg_lock);
    }    
      
    if (fts_data->edge_palm_enable == 2) { // not in game mode or never set
        mutex_lock(&fts_data->reg_lock);
      	if (ts_data->rotation_angle == ANGLE_0)  {
	    retl = fts_write_reg(FTS_REG_EDGEPALM_LEFT, 0x0);
	    l_set = 0x0;
	    msleep(5);   
	    retr = fts_write_reg(FTS_REG_EDGEPALM_RIGHT,0x0);
	    r_set = 0x0;
	}

	if ((ts_data->rotation_angle == ANGLE_90) || (ts_data->rotation_angle == ANGLE_270))  {
	    retl = fts_write_reg(FTS_REG_EDGEPALM_LEFT, 0x0A);
	    l_set = 0x0A;
	    msleep(5);  
	    retr = fts_write_reg(FTS_REG_EDGEPALM_RIGHT, 0x0A);
	    r_set = 0x0A;
	}
	mutex_unlock(&fts_data->reg_lock);
    }
    
    mutex_lock(&fts_data->reg_lock);    
    fts_read_reg(FTS_REG_EDGEPALM_LEFT, &l_val);
    fts_read_reg(FTS_REG_EDGEPALM_RIGHT, &r_val);
    mutex_unlock(&fts_data->reg_lock);
    
    if ((l_set != l_val) || (r_set!= r_val)) {
        FTS_INFO("Set edge palm error set (L:%x, R:%x), read (L:%x, R:%x)",l_set,r_set,l_val,r_val);
    } else {
        if (fts_data->log_level >= 2)
	    FTS_INFO("game mode: %d Set rotation reg to %d , palm range left %x right %x",ts_data->game_mode,ts_data->rotation_angle,l_val,r_val);	  
    }
    wait_resume = false;
}

void set_report_rate (void) {
    struct fts_ts_data *ts_data = fts_data;
    int ret = 0 , i = 0;
    u8 rate = 0;
    
    if (fts_data->report_rate != REPORT_RATE_2) {
	mutex_lock(&fts_data->reg_lock);
	ret = fts_write_reg(FTS_REG_REPORT_RATE_BURST, 0x00);
	msleep(20);
	mutex_unlock(&fts_data->reg_lock);
    }
    
    if (fts_data->report_rate == REPORT_RATE_0) {
	mutex_lock(&fts_data->reg_lock);      
	for (i = 0; i < 6; i++) {
	    ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x00);
	    msleep(20);
	    ret = fts_read_reg(FTS_REG_REPORT_RATE, &rate);
	    if (rate!=0x00){
	      FTS_DEBUG("set report rate to min fail rate 0x%X , retry %d",rate, i);
	      msleep(20);
	    } else {
      	      ts_data->report_rate = REPORT_RATE_0;
	      if (fts_data->log_level >= 2)
		  FTS_DEBUG("set report rate to min rate %X",rate);
	      break;
	    }
	}
	mutex_unlock(&fts_data->reg_lock);
	return;
    }
    
    if (fts_data->report_rate == REPORT_RATE_1) {
	mutex_lock(&fts_data->reg_lock);      
	for (i = 0; i < 6; i++) {
	    ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x24);
	    msleep(20);
	    ret = fts_read_reg(FTS_REG_REPORT_RATE, &rate);
	    if (rate!=0x24){
	      FTS_DEBUG("set report rate to default fail rate 0x%X , retry %d",rate, i);
	      msleep(20);
	    } else {
      	      ts_data->report_rate = REPORT_RATE_1;
	      if (fts_data->log_level >= 2)
		  FTS_DEBUG("set report rate to default rate %X",rate);
	      break;
	    }
	}
	mutex_unlock(&fts_data->reg_lock);
	return;
    } 

    
    if (fts_data->report_rate == REPORT_RATE_2) {
	mutex_lock(&fts_data->reg_lock);      
	for (i = 0; i < 6; i++) {
	    ret = fts_write_reg(FTS_REG_REPORT_RATE_BURST, 0x01);
	    msleep(20);
	    ret = fts_read_reg(FTS_REG_REPORT_RATE_BURST, &rate);
	    if (rate!=0x01){
	      FTS_DEBUG("set report rate to max fail rate 0x%X , retry %d",rate, i);
	      msleep(20);
	    } else {
      	      ts_data->report_rate = REPORT_RATE_2;
	      if (fts_data->log_level >= 2)
		  FTS_DEBUG("set report rate to max rate %X",rate);
	      break;
	    }
	}
	mutex_unlock(&fts_data->reg_lock);
	return;
    }      
}

void set_sub_noise_mode(bool enable) {
    int ret = 0 , i = 0;
    u8 mode = 0;

    if (enable) {
        mutex_lock(&fts_data->reg_lock);
        for (i = 0; i < 6; i++) {
            ret = fts_write_reg(FTS_REG_SUBNOISE_MODE, 0x01);
            msleep(20);
            ret = fts_read_reg(FTS_REG_SUBNOISE_MODE, &mode);
            if (mode!=0x01){
                FTS_DEBUG("enter sub noise mode fail, mode 0x%X , retry %d",mode, i);
                msleep(20);
            } else {
                fts_data->sub_noise = ENABLE;
                FTS_DEBUG("enter sub noise mode %X",mode);
                break;
            }
        }
        mutex_unlock(&fts_data->reg_lock);
        return;
    } else {
        mutex_lock(&fts_data->reg_lock);
        for (i = 0; i < 6; i++) {
            ret = fts_write_reg(FTS_REG_SUBNOISE_MODE, 0x00);
            msleep(20);
            ret = fts_read_reg(FTS_REG_SUBNOISE_MODE, &mode);
            if (mode!=0x00){
                FTS_DEBUG("exit sub noise mode fail, mode 0x%X , retry %d",mode, i);
                msleep(20);
            } else {
                fts_data->sub_noise = DISABLE;
                FTS_DEBUG("exit sub noise mode %X",mode);
                break;
            }
        }
        mutex_unlock(&fts_data->reg_lock);
        return;
    }
}

/*void report_last_used_atr_slot(void)
{
	int i = 0;
	for(i = MAX_ATR_TOUCH_NUMBER -1; i >= 0 ; i--)
	{
		if(atr_slot[i] != 0) //find first empty slot
		{
			struct input_dev *input_dev = fts_data->input_dev;
			input_mt_slot(input_dev, i + MAX_I2C_TOUCH_NUMBER);
			report_random_xy(input_dev,atr_slot_x[i],atr_slot_y[i]);
            input_sync(input_dev);
			FTS_INFO("report_last_used_atr_slot report %d down x=%d ,y=%d ",i + MAX_I2C_TOUCH_NUMBER,atr_slot_x[i],atr_slot_y[i]);
			return;
		}
	}
	return;
}*/

void clear_atr_slot(){
    int i = 0;
    for(i = MAX_ATR_TOUCH_NUMBER -1; i >= 0 ; i--){
		atr_slot[i] = 0;
    }
}

void ATR_touch(int key_id,int action, int x, int y, int random) // Tingyi
{
	static int random_x = 0, random_y = 0, random_major = 0, random_pressure = 1;
	struct input_dev *input_dev = fts_data->input_dev;
	int first_empty_slot = -1;
	int i;

	static bool first_continue_flag = false;
	x = x * 16;
	y = y * 16;
	
	FTS_INFO("keymapping id=%d, action=%d, x=%d, y=%d", key_id, action,  x/16,  y/16);
	mutex_lock(&input_dev->mutex);
	mutex_lock(&fts_data->report_mutex);

	if(action) //press, find first slot or find last slot;
	{
		for(i = MAX_ATR_TOUCH_NUMBER -1; i >= 0 ; i--)
		{
			if(first_empty_slot == -1 && atr_slot[i] == 0) //find first empty slot
				first_empty_slot = i;
			if(atr_slot[i] == (key_id + 1)) //if the last key_id has been pressed, keep reporting same slot
				first_empty_slot = i;
		}
//		FTS_INFO("keymapping ATR_touch press found slot %d", first_empty_slot);
		if(first_empty_slot != -1) // found an available slot
		{
			fts_data->atr_press = true;
			if (fts_data->log_level >= 2) {
				if(atr_slot[first_empty_slot] ==0)
					FTS_INFO("keymapping report %d down x=%d ,y=%d ",first_empty_slot,x,y);
			    FTS_INFO("slot %d", first_empty_slot + MAX_I2C_TOUCH_NUMBER);

			}

			if(!random) {
				x += random_x;
				y += random_y;
			}

			input_mt_slot(input_dev, first_empty_slot + MAX_I2C_TOUCH_NUMBER);
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
			if (fts_data->realtime == 1) {
				input_set_timestamp(input_dev, fts_data->atr_received);
			}
			input_report_abs(input_dev, ABS_MT_PRESSURE, 0x3f + random_pressure);
			input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 0x09 + random_major);
			input_report_abs(input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
			atr_slot_x[first_empty_slot] = x;
			atr_slot_y[first_empty_slot] = y;
//			FTS_INFO("[ATR_N_R] ID %d active %d x %d y %d p %d m %d", first_empty_slot + 10, action, x, y, 0x3f + random_pressure, 0x09 + random_major);

			if(!fts_data->finger_press) {
				if(first_continue_flag == false) {
		//			FTS_INFO("no touch BTN down , atr touch down");
					input_report_key(input_dev, BTN_TOUCH, 1);
					if (random == 1)
						first_continue_flag = true;
				}
			}

		    input_sync(input_dev);

			atr_slot[first_empty_slot] = key_id + 1; // save finger id in slot

		}
	} 
	else //release
	{
		for(i = MAX_ATR_TOUCH_NUMBER -1; i >= 0 ; i--)
		{
			if(atr_slot[i] == (key_id + 1)) //find the released slot
			{
				first_empty_slot = i;
				break;
			}
		}
		if (fts_data->log_level >= 2)
			FTS_INFO("keymapping  release slot %d", first_empty_slot);
		if(first_empty_slot >= 0)
		{
			input_mt_slot(input_dev, first_empty_slot + MAX_I2C_TOUCH_NUMBER);
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
//			FTS_INFO("[ATR_N_R] ID %d active %d x %d y %d p %d m %d", first_empty_slot + 10, action, 0, 0, 0, 0);
			input_sync(input_dev);
			atr_slot[first_empty_slot] = 0;
		}
	}

	for(i = MAX_ATR_TOUCH_NUMBER -1; i >= 0 ; i--)
	{
	    if (atr_slot[i] != 0) //find the released slot
		break;
	}   
	if(i < 0) // all button up
	{
		fts_data->atr_press = false;
		if(first_continue_flag == true) {
			first_continue_flag = false;
		}
		if(!fts_data->finger_press)
		{
			if (fts_data->log_level >= 2)
				FTS_INFO("no touch and atr BTN down, all BTN up");
			input_report_key(input_dev, BTN_TOUCH, 0);
			input_sync(input_dev);
		}
	}

    if (jiffies & 0x01) {
        random_x = ((jiffies & 0x1FF)-255); //-255~256
    } else {
        random_y = ((jiffies & 0x1FF)-255); //-255~256
    }

    random_major = ((jiffies & 0x07)-7); //-7~0
    random_pressure = 1;

	mutex_unlock(&fts_data->report_mutex);
	mutex_unlock(&input_dev->mutex);
	schedule();
}

/*
 * attribute functions
 */
static ssize_t keymapping_touch_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
    bool stat = 0;
    if (fts_data->atr_press)
	stat = true;
    else
	stat = false;

    return sprintf(buf, "%d", stat);
}

static ssize_t keymapping_touch_store(struct device *dev,
				      struct device_attribute *attr, const char *buf, size_t count)
{
	struct fts_ts_data *ts_data = fts_data;
	int id = 0, action = 0, x = 0, y = 0, random = 0, minus = 0;
	if (fts_data->log_level >= 2)
	    FTS_INFO("keymapping cmd buf: %s len=%d", buf, count);

	if(ts_data->suspended)
	    FTS_INFO("Touch is supended");



	if ((count > 16) || (count < 14)){
	    FTS_INFO("Invalid cmd buffer %d", count);
	    return -EINVAL;
	}
	
	if (fts_data->realtime == 1)
	    fts_data->atr_received =  ktime_get();
	
	if (count == 14) {
		id = buf[0] - '0';
		action = buf[1] - '0';
		random = buf[2] - '0';
		
		minus = buf[3];
		x =  shex_to_u16(buf + 4, 4);
		if(minus == '-')
			x = -x;

		minus = buf[8];
		y =  shex_to_u16(buf + 9, 4);
		if(minus == '-')
			y = -y;
	} else if (count == 15) {
		id = shex_to_u16(buf, 2);
		action = buf[2] - '0';
		random = buf[3] - '0';

		minus = buf[4];
		x =  shex_to_u16(buf + 5, 4);
		if(minus == '-')
			x = -x;

		minus = buf[9];
		y =  shex_to_u16(buf + 10, 4);
		if(minus == '-')
			y = -y;
	}
	if (fts_data->log_level >= 2)
	    FTS_INFO("keymapping ID=%d ACTION=%d X=%d Y=%d RANDOM=%d", id, action, x, y, random);
	ATR_touch(id, action, x, y, random);

	return count;
}

static ssize_t fts_game_mode_show (struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    struct fts_ts_data *ts_data = fts_data;
    struct input_dev *input_dev = ts_data->input_dev;
    u8 value = 0x0;
    
    mutex_lock(&input_dev->mutex);
    
    fts_read_reg(FTS_REG_GMAE_MODE, &value);
    count = snprintf(buf + count, PAGE_SIZE, "Game mode reg 0x%x Game Mode:%s\n",
                     value,ts_data->game_mode ? "On" : "Off");
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_game_mode_store(
    struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct fts_ts_data *ts_data = fts_data;
    int ret;
    if (FTS_SYSFS_ECHO_ON(buf)) {
        if (!ts_data->game_mode) {
            FTS_DEBUG("enter game mode");
            ts_data->game_mode = ENABLE;
	    ret = fts_write_reg(FTS_REG_GMAE_MODE, 0x01);
	    if (ts_data->power_saving_mode) {
		FTS_DEBUG("system under power saving mode, rise report rate for game");
		fts_data->report_rate = REPORT_RATE_1; //300Hz
		fts_data->pre_report_rate =1;
		set_report_rate();
	    }
        }
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        if (ts_data->game_mode) {
            FTS_DEBUG("exit game mode");
            ts_data->game_mode = DISABLE;
	    ret = fts_write_reg(FTS_REG_GMAE_MODE, 0x00);
	    fts_data->edge_palm_enable = 2;
	    if (ts_data->power_saving_mode) {
		FTS_DEBUG("system under power saving mode, decrease report rate for game");
		fts_data->report_rate = REPORT_RATE_0; //120Hz
		fts_data->pre_report_rate =0;
		set_report_rate();
	    }
        }
    }

    FTS_DEBUG("game mode:%d", ts_data->game_mode);
    return count;
}

static ssize_t fts_rotation_mode_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d \n", fts_data->rotation_angle);
}

static ssize_t fts_rotation_mode_store(
    struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        bool reconfig = false;
	if (buf[0] == '1') {
	  if (pre_angle != 1)
	      reconfig = true;	  
	  fts_data->rotation_angle = ANGLE_90;
	  pre_angle = 1;
	} else if (buf[0] == '2') {
  	  if (pre_angle != 2)
	      reconfig = true;	  
	  fts_data->rotation_angle = ANGLE_270;
	  pre_angle = 2;
	} else if (buf[0] == '0') {
	  if (pre_angle != 0)
	      reconfig = true;	  
	  fts_data->rotation_angle = ANGLE_0;
	  pre_angle = 0;
	}
	
	if (reconfig) {
	    set_rotation_mode();
	    if ((fts_data->edge_palm_enable==1) || (fts_data->edge_palm_enable==2)) { // rotation when game playing 
		set_edge_palm();
	    }
	    if (fts_data->edge_palm_enable == 0){
	        disable_edge_palm();
	    }
	}
	
	return count;
}

static ssize_t game_settings_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    u8 sli_sen = 0 , sli_acy = 0,tp_acy = 0, sli_smo = 0;
    
    fts_read_reg(FTS_REG_SLIDING_SENSITIVITY, &sli_sen);
    fts_read_reg(FTS_REG_SLIDING_PRECISION, &sli_acy);
    fts_read_reg(FTS_REG_TOUCH_SENSITIVITY, &tp_acy);
    fts_read_reg(FTS_REG_SLIDING_SMOOTHNESS, &sli_smo);
    FTS_INFO("game settings read %03X.%03X.%03X.%03X", slidling_sen,sliding_acy,tp_acy,sli_smo);
    
    return sprintf(buf, "%03X.%03X.%03X.%03X\n", slidling_sen,sliding_acy,tp_acy,sli_smo);
}

static ssize_t game_settings_store(
    struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char game_settings[29];
	int ret1,ret2,ret3,ret4;
	u8 value = 0x0;
	int i = 0;

	memset(game_settings, 0, sizeof(game_settings));
	sprintf(game_settings, "%s", buf);
	game_settings[count-1] = '\0';
	
	FTS_INFO("game_settings %s count %d ",game_settings,count);
	
	if(count > 17){
            return -EINVAL;
	}
	
	slidling_sen = (u16)shex_to_u16(game_settings +0, 3);
	sliding_acy = (u16)shex_to_u16(game_settings +4, 3);
	touch_acy = (u16)shex_to_u16(game_settings +8, 3);
	sliding_smo  = (u16)shex_to_u16(game_settings +12, 3);
	if (fts_data->log_level >= 2)
	    FTS_INFO("slidling_sen 0x%03X, sliding_acy 0x%03X touch_acy 0x%03X sliding_smo 0x%03X", slidling_sen,sliding_acy,touch_acy,sliding_smo);
	
	if (fts_data->suspended) {
	    wait_resume = true;
	    return count;
	}
	
	if (!fts_data->suspended) {
	    mutex_lock(&fts_data->reg_lock);
//	    fts_irq_disable();
	    for (i = 0; i < 6; i++) {
			ret1 = fts_write_reg(FTS_REG_SLIDING_SENSITIVITY, slidling_sen);
			msleep(5);
			fts_read_reg(FTS_REG_SLIDING_SENSITIVITY, &value);
			if (value == slidling_sen){
				if (fts_data->log_level >= 2)
				FTS_DEBUG("set FTS_REG_SLIDING_SENSITIVITY 0x%x success",slidling_sen);
				break;
			} else {
				FTS_DEBUG("set FTS_REG_SLIDING_SENSITIVITY 0x%x failed,retry count %d",value, i);
				msleep(5);
			}
	    }
	    
		for (i = 0; i < 6; i++) {
			ret2 = fts_write_reg(FTS_REG_SLIDING_PRECISION, sliding_acy);
			msleep(5);
			fts_read_reg(FTS_REG_SLIDING_PRECISION, &value);
			if (value == sliding_acy){
				if (fts_data->log_level >= 2)
				FTS_DEBUG("set FTS_REG_SLIDING_PRECISION 0x%x success",sliding_acy);
				break;
			} else {
				FTS_DEBUG("set FTS_REG_SLIDING_PRECISION 0x%x failed,retry count %d",value, i);
				msleep(5);
			}
	    }

		for (i = 0; i < 6; i++) {
			ret3 = fts_write_reg(FTS_REG_TOUCH_SENSITIVITY, touch_acy);
			msleep(5);
			fts_read_reg(FTS_REG_TOUCH_SENSITIVITY, &value);
			if (value == touch_acy){
				if (fts_data->log_level >= 2)
					FTS_DEBUG("set FTS_REG_TOUCH_SENSITIVITY 0x%x success",touch_acy);
				break;
			} else {
				FTS_DEBUG("set FTS_REG_TOUCH_SENSITIVITY 0x%x failed,retry count %d",value, i);
				msleep(5);
			}
	    }

		for (i = 0; i < 6; i++) {
			ret4 = fts_write_reg(FTS_REG_SLIDING_SMOOTHNESS, sliding_smo);
			msleep(5);
			fts_read_reg(FTS_REG_SLIDING_SMOOTHNESS, &value);
			if (value == sliding_smo){
				if (fts_data->log_level >= 2)
					FTS_DEBUG("set FTS_REG_SLIDING_SMOOTHNESS 0x%x success",sliding_smo);
				break;
			} else {
				FTS_DEBUG("set FTS_REG_SLIDING_SMOOTHNESS 0x%x failed,retry count %d",value, i);
				msleep(5);
			}
	    }
//	    fts_irq_enable();
	    mutex_unlock(&fts_data->reg_lock);
	}
	wait_resume = false;
	return count;
}

static ssize_t rise_report_rate_show (struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 rate = 0;
    u8 rate_burst = 0;
    int report_rate = 0;
    fts_read_reg(FTS_REG_REPORT_RATE, &rate);
    fts_read_reg(FTS_REG_REPORT_RATE_BURST, &rate_burst);

    if (rate_burst == 0x01)
        report_rate = 720;
    else if (rate == 0x24)
        report_rate = 360;
    else
        report_rate = 120;
    count = snprintf(buf + count, PAGE_SIZE, "%d\n",
                     report_rate);

    return count;
}

static ssize_t rise_report_rate_store(
    struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{  
        bool reconfig = false;
	if (buf[0] == '0') {
	  if (fts_data->pre_report_rate != 0)
	      reconfig = true;
	  fts_data->report_rate = REPORT_RATE_0; //120Hz
	  fts_data->pre_report_rate = 0;
	  fts_data->power_saving_mode = true;
	} else if (buf[0] == '1') {
  	  if (fts_data->pre_report_rate != 1)
	      reconfig = true;  
	  fts_data->report_rate = REPORT_RATE_1; //300Hz
	  fts_data->power_saving_mode = false;
	  fts_data->pre_report_rate =1;
	} else if (buf[0] == '2') {
	  if (fts_data->pre_report_rate != 2)
	      reconfig = true;	  
	  fts_data->report_rate = REPORT_RATE_2;//560Hz
	  fts_data->power_saving_mode = false;
	  fts_data-> pre_report_rate = 2;
	}
  
    if (reconfig)  
        set_report_rate();
    if (fts_data->log_level >= 2)
	FTS_DEBUG("Report rate set to:%d", fts_data->report_rate);
    return count;
}


static ssize_t edge_settings_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    u8 l_val = 0 , r_val = 0;
    
    fts_read_reg(FTS_REG_EDGEPALM_LEFT, &l_val);
    fts_read_reg(FTS_REG_EDGEPALM_RIGHT, &r_val);
    FTS_INFO("palm range left %x right %x",l_val,r_val);	
    
    return sprintf(buf, "%03d.%03d\n", l_val,r_val);
}

static ssize_t edge_settings_store(
    struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char edge_settings[10];
	
	memset(edge_settings, 0, sizeof(edge_settings));
	sprintf(edge_settings, "%s", buf);
	edge_settings[count-1] = '\0';
	
//	FTS_INFO("edge_settings %s count %d ",edge_settings,count);
	
	if(count > 9){
            return -EINVAL;
	}
	
	if (fts_data->suspended) {
	    return count;
	}
	
	Rcoefleft = (u16)shex_to_u16(edge_settings +0, 3);
	RcoefRight = (u16)shex_to_u16(edge_settings +4, 3);
	if (fts_data->log_level >= 2)
	    FTS_INFO("Rcoefleft 0x%X,RcoefRight 0x%X",Rcoefleft,RcoefRight);
	if (!fts_data->suspended) {
	    if (Rcoefleft > 10 || RcoefRight > 10) {
		fts_data->edge_palm_enable = 0;
	    } else
		fts_data->edge_palm_enable = 1;
	}
	
	if (fts_data->edge_palm_enable == 0){
	    disable_edge_palm();
	} else {
	    set_rotation_mode();
	    set_edge_palm();
	}
	return count;
}

static ssize_t fts_extra_config_store(
    struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct fts_ts_data *ts_data = fts_data;

    ts_data->extra_reconfig = buf[0]-'0';

    FTS_DEBUG("Extra touch extra config:%d", ts_data->extra_reconfig);

    if (ts_data->extra_reconfig == 1) {
        FTS_DEBUG("Reconfig touch size");
    }

    if (ts_data->extra_reconfig == 2) {
        if (!ts_data->sub_noise) {
            FTS_DEBUG("Reconfig touch frequency");
            set_sub_noise_mode(true);
        }
    }

    if (ts_data->extra_reconfig == 0) {
        FTS_DEBUG("Exit Extra mode");
        if (ts_data->sub_noise)
            set_sub_noise_mode(false);

    }
    return count;
}

static DEVICE_ATTR(keymapping_touch, S_IRUGO | S_IWUSR, keymapping_touch_show, keymapping_touch_store);
static DEVICE_ATTR(fts_game_mode, S_IRUGO | S_IWUSR, fts_game_mode_show, fts_game_mode_store);
static DEVICE_ATTR(fts_rotation_mode, S_IRUGO | S_IWUSR, fts_rotation_mode_show, fts_rotation_mode_store);
static DEVICE_ATTR(game_settings, S_IRUGO | S_IWUSR, game_settings_show, game_settings_store);
static DEVICE_ATTR(rise_report_rate, S_IRUGO | S_IWUSR, rise_report_rate_show, rise_report_rate_store);
static DEVICE_ATTR(edge_settings, S_IRUGO | S_IWUSR, edge_settings_show, edge_settings_store);
static DEVICE_ATTR(fts_extra_config, S_IRUGO | S_IWUSR, NULL, fts_extra_config_store);
/* add your attr in here*/
static struct attribute *fts_attributes[] = {
    &dev_attr_keymapping_touch.attr,
    &dev_attr_fts_game_mode.attr,
    &dev_attr_fts_rotation_mode.attr,
    &dev_attr_game_settings.attr,
    &dev_attr_rise_report_rate.attr,
    &dev_attr_edge_settings.attr,
    &dev_attr_fts_extra_config.attr,
    NULL
};

static struct attribute_group asus_game_attribute_group = {
    .attrs = fts_attributes
};

void asus_game_recovery(struct fts_ts_data *ts_data) 
{
    int ret;
    if (ts_data->game_mode == ENABLE) {
      set_rotation_mode();
      ret = fts_write_reg(FTS_REG_GMAE_MODE, 0x01);
      if (wait_resume) {
	FTS_INFO("Game mode setting recovery");
	set_edge_palm();
      }
    }
   
    reconfig_game_reg(true);

}

void report_rate_recovery(struct fts_ts_data *ts_data) 
{
    if (fts_data->report_rate != REPORT_RATE_1) {
	set_report_rate();
    }
}

int asus_game_create_sysfs(struct fts_ts_data *ts_data)
{
    int ret = 0;

    ret = sysfs_create_group(&ts_data->dev->kobj, &asus_game_attribute_group);
    if (ret) {
        FTS_ERROR("[EX]: asus_create_group() failed!!");
        sysfs_remove_group(&ts_data->dev->kobj, &asus_game_attribute_group);
        return -ENOMEM;
    } else {
        FTS_INFO("[EX]: asus_create_group() succeeded!!");
    }
    
    ts_data->game_mode = DISABLE;
    ts_data->rotation_angle = 0;
    ts_data->report_rate = REPORT_RATE_1;
    fts_data->edge_palm_enable = 2;
    fts_data->pre_report_rate = 1;
    fts_data->sub_noise = DISABLE;
    
    mutex_init(&ts_data->reg_lock);
    return ret;
}

int asus_game_remove_sysfs(struct fts_ts_data *ts_data)
{
    sysfs_remove_group(&ts_data->dev->kobj, &asus_game_attribute_group);
    return 0;
}
