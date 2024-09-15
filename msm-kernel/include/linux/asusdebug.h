/////////////////////////////////////////////////////////////////////////////////////////////
////                  ASUS Debugging mechanism
/////////////////////////////////////////////////////////////////////////////////////////////
#ifndef __ASUSDEBUG_H__
#define __ASUSDEBUG_H__

#ifndef ASUS_GKI_BUILD
extern phys_addr_t PRINTK_BUFFER_PA;
extern void *PRINTK_BUFFER_VA;
#endif

#define PRINTK_BUFFER_SIZE      (0x00400000)
#define PRINTK_BUFFER_MAGIC     (0xFEEDBEEF)
#define PRINTK_BUFFER_SLOT_SIZE (0x00040000)

#ifndef ASUS_GKI_BUILD
#define PRINTK_BUFFER_SLOT1     (PRINTK_BUFFER_VA)
#define PRINTK_BUFFER_SLOT2     ((void *)((ulong)PRINTK_BUFFER_VA + (ulong)PRINTK_BUFFER_SLOT_SIZE))
//#define PHONE_HANG_LOG_BUFFER   ((void *)((ulong)PRINTK_BUFFER_VA + (ulong)2*PRINTK_BUFFER_SLOT_SIZE ) - (ulong)0x3FC00)
#define PHONE_HANG_LOG_SIZE     (SZ_1M + PRINTK_BUFFER_SLOT_SIZE*2 + 0x3FC00 + SZ_8K)
#define LAST_KMSG_SIZE          (SZ_16K)
#endif

/////////////////////////////////////////////////////////////////////////////////////////////
////                  Eventlog mask mechanism
/////////////////////////////////////////////////////////////////////////////////////////////
#define ASUS_ASDF_BASE_DIR "/asdf/"
#define ASUS_EVTLOG_PATH ASUS_ASDF_BASE_DIR"ASUSEvtlog"
#define ASUS_EVTLOG_STR_MAXLEN (256)
#define ASUS_EVTLOG_MAX_ITEM (64)

#define ASUS_USB_THERMAL_ALERT "ASUS_thermal_alert"
#define ASUS_VBUS_LOW_IMPEDANCE  "ASUS_VBUS_low_impedance"
#define ASUS_AICL_SUSPEND "ASUS_AICL_suspend"
#define ASUS_JEITA_HARD_HOT "ASUS_JEITA_hard_hot"
#define ASUS_JEITA_HARD_COLD "ASUS_JEITA_hard_cold"
#define ASUS_USB_WATER_INVADE "ASUS_USB_water_invade"
#define ASUS_OUTPUT_OVP "ASUS_Output_OVP"

#ifndef ASUS_GKI_BUILD
void save_last_shutdown_log(char *filename);
void get_last_shutdown_log(void);
#endif

void ASUSEvtlog(const char *fmt, ...);
#endif
