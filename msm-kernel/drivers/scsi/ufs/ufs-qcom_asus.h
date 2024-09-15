#include <linux/device.h>
#include "ufs-qcom.h"

void ufshcd_add_sysfs_nodes(struct ufs_qcom_host *host);
void ufshcd_remove_sysfs_nodes(struct ufs_qcom_host *host);

int asus_ufshcd_system_resume(struct device *dev);
void asus_ufshcd_resume_complete(struct device *dev);
int asus_ufshcd_runtime_resume(struct device *dev);

int asus_ufshcd_system_suspend(struct device *dev);

void asus_ufs_init(struct ufs_qcom_host *host);

