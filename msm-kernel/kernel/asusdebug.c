/* 
 *  ASUS debug mechanisms 
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/rtc.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/init_syscalls.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#include <linux/sched.h>
#include <linux/msm_rtb.h>
#include <linux/stacktrace.h>
#include <linux/asusdebug.h>

#include <soc/qcom/minidump.h>
#include <asm/syscall_wrapper.h>

#include <linux/rtc.h>
#include "locking/rtmutex_common.h"

#define AID_SDCARD_RW 1015

static int bootcount = 0;
module_param(bootcount, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(bootcount, "bootcount");

char evtlog_bootup_reason[100];
EXPORT_SYMBOL(evtlog_bootup_reason);
char evtlog_poweroff_reason[100];
EXPORT_SYMBOL(evtlog_poweroff_reason);
char evtlog_warm_reset_reason[100];

#ifndef ASUS_GKI_BUILD
phys_addr_t PRINTK_BUFFER_PA = 0x9B800000;
void *PRINTK_BUFFER_VA;
#endif

/*
 * rtc read time
 */
extern struct timezone sys_tz;
int asus_rtc_read_time(struct rtc_time *tm)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	ts.tv_sec -= sys_tz.tz_minuteswest * 60;
	rtc_time64_to_tm(ts.tv_sec, tm);
	printk("now %04d%02d%02d-%02d%02d%02d, tz=%d\r\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, sys_tz.tz_minuteswest);
	return 0;
}
EXPORT_SYMBOL(asus_rtc_read_time);

char messages[256];

#ifndef ASUS_GKI_BUILD
//static mm_segment_t oldfs;
static void initKernelEnv(void)
{
#if 0
	oldfs = get_fs();
	set_fs(KERNEL_DS);
#endif
}

static void deinitKernelEnv(void)
{
#if 0
	set_fs(oldfs);
#endif
}

void save_last_shutdown_log(char *filename)
{
	char *last_shutdown_log;
	struct file *file_handle;
	unsigned long long t;
	unsigned long nanosec_rem;
	char buffer[] = {"Kernel panic"};
	int i;
	ulong *printk_buffer_slot2_addr = (ulong *)PRINTK_BUFFER_SLOT2;

	t = cpu_clock(0);
	nanosec_rem = do_div(t, 1000000000);
	last_shutdown_log = (char *)PRINTK_BUFFER_VA;

	printk_buffer_slot2_addr = (ulong *)PRINTK_BUFFER_SLOT2;
	sprintf(messages, ASUS_ASDF_BASE_DIR "LastShutdown_%lu.%06lu.txt",
		(unsigned long)t,
		nanosec_rem / 1000);

	initKernelEnv();

	file_handle = filp_open(messages, O_CREAT | O_RDWR | O_SYNC, 0);
	if (!IS_ERR(file_handle)) {
		kernel_write(file_handle, (unsigned char *)last_shutdown_log, PRINTK_BUFFER_SLOT_SIZE, &file_handle->f_pos);
		filp_close(file_handle, NULL);
		for(i=0; i<PRINTK_BUFFER_SLOT_SIZE; i++) {
			// Check if it is kernel panic
			if (strncmp((last_shutdown_log + i), buffer, strlen(buffer)) == 0)
				ASUSEvtlog("[Reboot] Kernel panic\n");
			break;
		}
	} else {
		printk("[ASDF] save_last_shutdown_error: [%d]\n", file_handle);
	}

	deinitKernelEnv();
}

void get_last_shutdown_log(void)
{
	ulong *printk_buffer_slot2_addr;

	printk_buffer_slot2_addr = (ulong *)PRINTK_BUFFER_SLOT2;
	printk("get_last_shutdown_log: printk_buffer_slot2=%x, value=0x%lx\n", printk_buffer_slot2_addr, *printk_buffer_slot2_addr);
	if (*printk_buffer_slot2_addr == (ulong)PRINTK_BUFFER_MAGIC)
		save_last_shutdown_log("LastShutdown");
//	printk_buffer_rebase();
}
EXPORT_SYMBOL(get_last_shutdown_log);
#endif

static struct mutex mA;

static char g_Asus_Eventlog[ASUS_EVTLOG_MAX_ITEM][ASUS_EVTLOG_STR_MAXLEN];
static int g_Asus_Eventlog_read = 0;
static int g_Asus_Eventlog_write = 0;

#ifndef ASUS_GKI_BUILD
static int g_bEventlogEnable = 1;
static struct file *g_hfileEvtlog = NULL;
static struct workqueue_struct *ASUSEvtlog_workQueue;
static void do_write_event_worker(struct work_struct *work);
static DECLARE_WORK(eventLog_Work, do_write_event_worker);

static void do_write_event_worker(struct work_struct *work)
{
	char buffer[256];
	loff_t size;

	memset(buffer, 0, sizeof(char) * 256);

	if (IS_ERR_OR_NULL(g_hfileEvtlog)) {
		g_hfileEvtlog = filp_open(ASUS_EVTLOG_PATH ".txt", O_CREAT | O_RDWR | O_SYNC, 0666);
//		ksys_chown(ASUS_EVTLOG_PATH ".txt", AID_SDCARD_RW, AID_SDCARD_RW);

		if (IS_ERR_OR_NULL(g_hfileEvtlog)) {
			printk("[Debug] 1st open ASUSEvtlog failed!\n");
			return;
		}

		size = vfs_llseek(g_hfileEvtlog, 0, SEEK_END);
#if 0
		if (size >= SZ_4M) {
			filp_close(g_hfileEvtlog, NULL);
//			init_link(ASUS_EVTLOG_PATH "_old.txt", ASUS_EVTLOG_PATH "_old1.txt");
//			init_unlink(ASUS_EVTLOG_PATH "_old.txt");
//			sys_rename1(ASUS_EVTLOG_PATH ".txt", ASUS_EVTLOG_PATH "_old.txt");

			g_hfileEvtlog = filp_open(ASUS_EVTLOG_PATH ".txt", O_CREAT | O_RDWR | O_SYNC, 0666);
//			ksys_chown(ASUS_EVTLOG_PATH ".txt", AID_SDCARD_RW, AID_SDCARD_RW);
		}
#endif

		snprintf(buffer, sizeof(buffer),
				"\n\n---------------System Boot----%s----BootCount:%d-----\n"
				"[Shutdown] Reset Trigger: %s ###### \n"
				"###### Reset Type: %s ######\n",
				ASUS_SW_VER,
				bootcount,
				evtlog_poweroff_reason,
				evtlog_bootup_reason);

		kernel_write(g_hfileEvtlog, buffer, strlen(buffer), &g_hfileEvtlog->f_pos);
		size = vfs_llseek(g_hfileEvtlog, 0, SEEK_END);
		filp_close(g_hfileEvtlog, NULL);
	}

	if (!IS_ERR_OR_NULL(g_hfileEvtlog)) {
		int str_len;
		char *pchar;

		g_hfileEvtlog = filp_open(ASUS_EVTLOG_PATH ".txt", O_CREAT | O_RDWR | O_SYNC, 0666);
//		ksys_chown(ASUS_EVTLOG_PATH ".txt", AID_SDCARD_RW, AID_SDCARD_RW);

		if (IS_ERR_OR_NULL(g_hfileEvtlog)) {
			printk("[Debug] Open ASUSEvtlog failed!\n");
			return;
		}

		size = vfs_llseek(g_hfileEvtlog, 0, SEEK_END);
#if 0
		if (size >= SZ_4M) {
			filp_close(g_hfileEvtlog, NULL);
//			init_link(ASUS_EVTLOG_PATH "_old.txt", ASUS_EVTLOG_PATH "_old1.txt");
//			init_unlink(ASUS_EVTLOG_PATH "_old.txt");
//			sys_rename1(ASUS_EVTLOG_PATH ".txt", ASUS_EVTLOG_PATH "_old.txt");

			g_hfileEvtlog = filp_open(ASUS_EVTLOG_PATH ".txt", O_CREAT | O_RDWR | O_SYNC, 0666);
//			ksys_chown(ASUS_EVTLOG_PATH ".txt", AID_SDCARD_RW, AID_SDCARD_RW);
		}
#endif

		while (g_Asus_Eventlog_read != g_Asus_Eventlog_write) {
			mutex_lock(&mA);
			str_len = strlen(g_Asus_Eventlog[g_Asus_Eventlog_read]);
			pchar = g_Asus_Eventlog[g_Asus_Eventlog_read];
			g_Asus_Eventlog_read++;
			g_Asus_Eventlog_read %= ASUS_EVTLOG_MAX_ITEM;
			mutex_unlock(&mA);

			if (pchar[str_len - 1] != '\n') {
				if (str_len + 1 >= ASUS_EVTLOG_STR_MAXLEN)
					str_len = ASUS_EVTLOG_STR_MAXLEN - 2;
				pchar[str_len] = '\n';
				pchar[str_len + 1] = '\0';
			}

			kernel_write(g_hfileEvtlog, pchar, strlen(pchar), &g_hfileEvtlog->f_pos);
			//sys_fsync(g_hfileEvtlog);
		}

		size = vfs_llseek(g_hfileEvtlog, 0, SEEK_END);
		filp_close(g_hfileEvtlog, NULL);
	}
}

void ASUSEvtlog(const char *fmt, ...)
{
	va_list args;
	char *buffer;

	if (g_bEventlogEnable == 0)
		return;
	if (!in_interrupt() && !in_atomic() && !irqs_disabled())
		mutex_lock(&mA);

	buffer = g_Asus_Eventlog[g_Asus_Eventlog_write];
	g_Asus_Eventlog_write++;
	g_Asus_Eventlog_write %= ASUS_EVTLOG_MAX_ITEM;

	if (!in_interrupt() && !in_atomic() && !irqs_disabled())
		mutex_unlock(&mA);

	memset(buffer, 0, ASUS_EVTLOG_STR_MAXLEN);
	if (buffer) {
		struct timespec64 ts;

		ktime_get_raw_ts64(&ts);
		sprintf(buffer, "(%ld) :", ts.tv_sec);
//		sprintf(buffer, "(%ld)%04d-%02d-%02d %02d:%02d:%02d :", ts.tv_sec, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		va_start(args, fmt);
		vscnprintf(buffer + strlen(buffer), ASUS_EVTLOG_STR_MAXLEN - strlen(buffer), fmt, args);
		va_end(args);
		printk("%s", buffer);
		queue_work(ASUSEvtlog_workQueue, &eventLog_Work);
	} else {
		printk("ASUSEvtlog buffer cannot be allocated\n");
	}
}
#endif
//#else
#ifdef ASUS_GKI_BUILD
static int g_bEventlogEnable = 0;

static DECLARE_WAIT_QUEUE_HEAD(log_wait);
void ASUSEvtlog(const char *fmt, ...)
{
	va_list args;
	void *buffer;

	if (!in_interrupt() && !in_atomic() && !irqs_disabled())
		mutex_lock(&mA);

	buffer = g_Asus_Eventlog[g_Asus_Eventlog_write];
	g_Asus_Eventlog_write++;
	g_Asus_Eventlog_write %= ASUS_EVTLOG_MAX_ITEM;

	if (g_bEventlogEnable == 0){
		printk("ASUSEvtlog: ASUS_GKI_BUILD = %d\n", ASUS_GKI_BUILD);
		snprintf(buffer, ASUS_EVTLOG_STR_MAXLEN,
			"\n\n---------------System Boot----%s---BootCount:%d------\n"
			"[Shutdown] Reset Trigger: %s ###### \n"
			"###### Reset Type: %s ######\n",
			ASUS_SW_VER,
			bootcount,
			evtlog_poweroff_reason,
			evtlog_bootup_reason
		);
		g_bEventlogEnable = 1;
		buffer = g_Asus_Eventlog[g_Asus_Eventlog_write];
		g_Asus_Eventlog_write++;
		g_Asus_Eventlog_write %= ASUS_EVTLOG_MAX_ITEM;
	}

	if (!in_interrupt() && !in_atomic() && !irqs_disabled())
		mutex_unlock(&mA);

	memset(buffer, 0, ASUS_EVTLOG_STR_MAXLEN);
	if (buffer) {
		struct timespec64 ts;

		ktime_get_raw_ts64(&ts);
		sprintf(buffer, "(%ld) :", ts.tv_sec);
//		sprintf(buffer, "(%ld)%04d-%02d-%02d %02d:%02d:%02d :", ts.tv_sec, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		va_start(args, fmt);
		vscnprintf(buffer + strlen(buffer), ASUS_EVTLOG_STR_MAXLEN - strlen(buffer), fmt, args);
		va_end(args);
		printk("%s", buffer);
		if(g_Asus_Eventlog_write % 2 == 0)
			wake_up_interruptible(&log_wait);
	} else {
		printk("ASUSEvtlog buffer cannot be allocated\n");
	}
}
#endif
EXPORT_SYMBOL(ASUSEvtlog);

static ssize_t asusevtlog_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char messages[256];

	if (count > 256)
		count = 256;

	memset(messages, 0, sizeof(messages));
	if (copy_from_user(messages, buf, count))
		return -EFAULT;

	ASUSEvtlog("%s", messages);
#ifdef ASUS_GKI_BUILD
	wake_up_interruptible(&log_wait);
#endif
	return count;
}

#ifdef ASUS_GKI_BUILD
static ssize_t asusevtlog_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
	int str_len = 0;
	char *pchar;
	int error = 0;
	error = wait_event_interruptible(log_wait,
						 g_Asus_Eventlog_read != g_Asus_Eventlog_write);

	if(!(g_Asus_Eventlog_read != g_Asus_Eventlog_write)) return 0;
	mutex_lock(&mA);
	str_len = strlen(g_Asus_Eventlog[g_Asus_Eventlog_read]);
	pchar = g_Asus_Eventlog[g_Asus_Eventlog_read];
	g_Asus_Eventlog_read++;
	g_Asus_Eventlog_read %= ASUS_EVTLOG_MAX_ITEM;
	mutex_unlock(&mA);

	if (str_len + 1 >= ASUS_EVTLOG_STR_MAXLEN){
				str_len = ASUS_EVTLOG_STR_MAXLEN - 2;
	}
	pchar[str_len] = '\n';
	pchar[str_len+1] = '\0';
	if (copy_to_user(buf, pchar, strlen(pchar))) {
		if (!str_len)
			str_len = -EFAULT;
	}

        return strlen(pchar);
}
#endif

static ssize_t evtlogswitch_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (strncmp(buf, "0", 1) == 0) {
		ASUSEvtlog("ASUSEvtlog disable !!");
		printk("ASUSEvtlog disable !!\n");
#ifndef ASUS_GKI_BUILD
		flush_work(&eventLog_Work);
#endif
		g_bEventlogEnable = 0;
	}
	if (strncmp(buf, "1", 1) == 0) {
		g_bEventlogEnable = 1;
		ASUSEvtlog("ASUSEvtlog enable !!");
		printk("ASUSEvtlog enable !!\n");
	}

	return count;
}

/*
 * For asusdebug
 */
static int asusdebug_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int asusdebug_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t asusdebug_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	return 0;
}

#include <linux/reboot.h>
extern int rtc_ready;
int asus_asdf_set = 0;
int g_startlog = 0;
static ssize_t asusdebug_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	u8 messages[256] = { 0 };

	if (count > 256)
		count = 256;
	if (copy_from_user(messages, buf, count))
		return -EFAULT;

	if (strncmp(messages, "panic", strlen("panic")) == 0) {
		panic("panic test");
	} else if (strncmp(messages, "startlog", strlen("startlog")) == 0) {
		g_startlog = 1;
		printk("[Debug] startlog = %d\n", g_startlog);
	} else if (strncmp(messages, "stoplog", strlen("stoplog")) == 0) {
		g_startlog = 0;
		printk("[Debug] startlog = %d\n", g_startlog);
	} else if (strncmp(messages, "get_asdf_log",
			   strlen("get_asdf_log")) == 0) {
#ifndef ASUS_GKI_BUILD
		ulong *printk_buffer_slot2_addr;

		printk_buffer_slot2_addr = (ulong *)PRINTK_BUFFER_SLOT2;
		printk("[ASDF] printk_buffer_slot2_addr=%x, value=0x%lx\n", printk_buffer_slot2_addr, *printk_buffer_slot2_addr);

		if (!asus_asdf_set) {
			asus_asdf_set = 1;
			get_last_shutdown_log();
			printk("[ASDF] get_last_shutdown_log: printk_buffer_slot2_addr=%x, value=0x%lx\n", printk_buffer_slot2_addr, *printk_buffer_slot2_addr);

			(*printk_buffer_slot2_addr) = (ulong)PRINTK_BUFFER_MAGIC;
		}
#endif
	}

	return count;
}

static ssize_t asusdebugponpoff_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char *buff = NULL;
    int rsize = 0;
    buff = kzalloc(256, GFP_KERNEL);
    if (!buff) return -ENOMEM;

    rsize = sprintf(buff, "POFF(%s),PON(%s)\n", evtlog_poweroff_reason, evtlog_bootup_reason);
    printk("[ABSP][asusdebugponpoff_read] Read POFF/PON reason[%s]\n", buff);

    ret = simple_read_from_buffer(buf, count, ppos, buff, rsize);
    kfree(buff);

    return ret;
}

//ASUS_BSP: perftop_++
#define MAX_BUFFER_SIZE PAGE_SIZE
//#define MAX_BUFFER_SIZE 2048

static char g_perftop_data[MAX_BUFFER_SIZE];
static size_t g_perftop_data_size = 0;

static ssize_t asusperftop_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t space_available = (MAX_BUFFER_SIZE - *ppos);

    if (count > space_available)
    {
        printk("%s: g_perftop_data is not enough space!\n", __func__);

        return -ENOMEM;
    }

    if ( copy_from_user( g_perftop_data + *ppos , buf, count ) )
    {
        printk("%s: copy_from_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    g_perftop_data_size = *ppos;

    return count;
}

static ssize_t asusperftop_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{	
    if (*ppos >= g_perftop_data_size)
    {
        return 0;
    }

    if ( (*ppos + count) > g_perftop_data_size )
    {
        count = g_perftop_data_size - *ppos;
    }

    if ( copy_to_user(buf, g_perftop_data + *ppos, count) )
    {
        printk("%s: copy_to_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    return count;
}

//////////
#define APPNAME_MAX_BUFFER_SIZE 256

static char g_perftop_app_data[APPNAME_MAX_BUFFER_SIZE];
static size_t g_perftop_app_data_size = 0;

static ssize_t asusperftop_app_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t space_available = (APPNAME_MAX_BUFFER_SIZE - *ppos);

    if (count > space_available)
    {
        printk("%s: g_perftop_app_data_size is not enough space!\n", __func__);

        return -ENOMEM;
    }

    if ( copy_from_user( g_perftop_app_data + *ppos , buf, count ) )
    {
        printk("%s: copy_from_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    g_perftop_app_data_size = *ppos;

    return count;
}

static ssize_t asusperftop_app_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{	
    if (*ppos >= g_perftop_app_data_size)
    {
        return 0;
    }

    if ( (*ppos + count) > g_perftop_app_data_size )
    {
        count = g_perftop_app_data_size - *ppos;
    }

    if ( copy_to_user(buf, g_perftop_app_data + *ppos, count) )
    {
        printk("%s: copy_to_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    return count;
}

//////////
#define GETPID_MAX_BUFFER_SIZE 8

static char g_perftop_getpid_data[GETPID_MAX_BUFFER_SIZE];
static size_t g_perftop_getpid_data_size = 0;

static ssize_t asusperftop_getpid_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t space_available = (GETPID_MAX_BUFFER_SIZE - *ppos);

    if (count > space_available)
    {
        printk("%s: g_perftop_getpid_data_size is not enough space!\n", __func__);

        return -ENOMEM;
    }

    if ( copy_from_user( g_perftop_getpid_data + *ppos , buf, count ) )
    {
        printk("%s: copy_from_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    g_perftop_getpid_data_size = *ppos;

    return count;
}

static ssize_t asusperftop_getpid_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{	
    if (*ppos >= g_perftop_getpid_data_size)
    {
        return 0;
    }

    if ( (*ppos + count) > g_perftop_getpid_data_size )
    {
        count = g_perftop_getpid_data_size - *ppos;
    }

    if ( copy_to_user(buf, g_perftop_getpid_data + *ppos, count) )
    {
        printk("%s: copy_to_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    return count;
}

//////////
#define PID_MAX_BUFFER_SIZE 32

static char g_perftop_pid_data[PID_MAX_BUFFER_SIZE];
static size_t g_perftop_pid_data_size = 0;

static ssize_t asusperftop_pid_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t space_available = (PID_MAX_BUFFER_SIZE - *ppos);

    if (count > space_available)
    {
        printk("%s: g_perftop_pid_data_size is not enough space!\n", __func__);

        return -ENOMEM;
    }

    if ( copy_from_user( g_perftop_pid_data + *ppos , buf, count ) )
    {
        printk("%s: copy_from_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    g_perftop_pid_data_size = *ppos;

    return count;
}

static ssize_t asusperftop_pid_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{	
    if (*ppos >= g_perftop_pid_data_size)
    {
        return 0;
    }

    if ( (*ppos + count) > g_perftop_pid_data_size )
    {
        count = g_perftop_pid_data_size - *ppos;
    }

    if ( copy_to_user(buf, g_perftop_pid_data + *ppos, count) )
    {
        printk("%s: copy_to_user -EFAULT!\n", __func__);

        return -EFAULT;
    }

    *ppos += count;

    return count;
}
//ASUS_BSP: perftop_--

static const struct proc_ops proc_asusevtlog_operations = {
	.proc_write	= asusevtlog_write,
#ifdef ASUS_GKI_BUILD
	.proc_read   = asusevtlog_read,
#endif
};

static const struct proc_ops proc_evtlogswitch_operations = {
	.proc_write	= evtlogswitch_write,
};

static const struct proc_ops proc_asusdebug_operations = {
	.proc_read		= asusdebug_read,
	.proc_write		= asusdebug_write,
	.proc_open		= asusdebug_open,
	.proc_release	= asusdebug_release,
};

static const struct proc_ops proc_asusdebugponpoff_operations = {
	.proc_read		= asusdebugponpoff_read,
};

static const struct proc_ops proc_asusdebugprop_operations = {
	.proc_read	   = asusdebug_read,
	.proc_write	  = asusdebug_write,
	.proc_open	   = asusdebug_open,
	.proc_release	= asusdebug_release,
};

//ASUS_BSP: perftop_++
static const struct proc_ops proc_asusperftop_operations = {
	.proc_write	= asusperftop_write,
	.proc_read  = asusperftop_read,
};

static const struct proc_ops proc_asusperftop_app_operations = {
	.proc_write	= asusperftop_app_write,
	.proc_read  = asusperftop_app_read,
};

static const struct proc_ops proc_asusperftop_getpid_operations = {
	.proc_write	= asusperftop_getpid_write,
	.proc_read  = asusperftop_getpid_read,
};

static const struct proc_ops proc_asusperftop_pid_operations = {
	.proc_write	= asusperftop_pid_write,
	.proc_read  = asusperftop_pid_read,
};
//ASUS_BSP: perftop_--

/*
 * Minidump Log - LastShutdownCrash
 */
#ifndef ASUS_GKI_BUILD
static void  register_minidump_log_buf(void)
{
	struct md_region md_entry;

	/*Register logbuf to minidump, first idx would be from bss section */
	strlcpy(md_entry.name, "KLOGDMSG", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t) (PRINTK_BUFFER_VA);
	md_entry.phys_addr = (uintptr_t) (PRINTK_BUFFER_PA);
	md_entry.size = PRINTK_BUFFER_SLOT_SIZE;
	if (msm_minidump_add_region(&md_entry))
		pr_err("Failed to add logbuf in Minidump\n");

}
#endif

static int __init proc_asusdebug_init(void)
{

	proc_create("asusdebug", S_IALLUGO, NULL, &proc_asusdebug_operations);
	proc_create("asusdebug-prop", S_IALLUGO, NULL, &proc_asusdebugprop_operations);
	proc_create("asusevtlog", S_IRWXUGO, NULL, &proc_asusevtlog_operations);
	proc_create("asusevtlog-switch", S_IRWXUGO, NULL, &proc_evtlogswitch_operations);
	proc_create("asusdebug-ponpoff", S_IALLUGO, NULL, &proc_asusdebugponpoff_operations);
//ASUS_BSP: perftop_++
	proc_create("asusperftop", S_IRWXUGO, NULL, &proc_asusperftop_operations);
	proc_create("asusperftopapp", S_IRWXUGO, NULL, &proc_asusperftop_app_operations);
	proc_create("asusperftop_getpid", S_IRWXUGO, NULL, &proc_asusperftop_getpid_operations);
	proc_create("asusperftop_pid", S_IRWXUGO, NULL, &proc_asusperftop_pid_operations);
//ASUS_BSP: perftop_--

	mutex_init(&mA);

#ifndef ASUS_GKI_BUILD
	PRINTK_BUFFER_VA = ioremap(PRINTK_BUFFER_PA, PRINTK_BUFFER_SIZE);
	register_minidump_log_buf();
	ASUSEvtlog_workQueue = create_singlethread_workqueue("ASUSEVTLOG_WORKQUEUE");
#endif

return 0;
}
module_init(proc_asusdebug_init);
#ifndef ASUS_GKI_BUILD
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
MODULE_DESCRIPTION("ASUS Debug Mechanisms");
MODULE_LICENSE("GPL v2");
