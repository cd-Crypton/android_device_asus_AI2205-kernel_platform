/*
 * Dummy Charger driver for test RT1711
 *
 * Copyright (c) 2012 Marvell International Ltd.
 * Author:	Jeff Chang <jeff_chagn@mrichtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/usb/role.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/usb.h>
#include "../usb/typec/pd_rt1715/inc/tcpci.h"
#include "../usb/typec/pd_rt1715/inc/tcpm.h"
#include "../usb/typec/pd_rt1715/inc/rt1711h.h"
#include "../usb/typec/pd_rt1715/inc/pd_policy_engine.h"

int tcpc_pd_state;
EXPORT_SYMBOL(tcpc_pd_state);

uint8_t gamepad_active = 0;
EXPORT_SYMBOL(gamepad_active);

extern uint16_t vid_ext;
struct notifier_block	host_nb;
struct delayed_work rdo_work;
struct workqueue_struct *rdo_wq;
extern struct rt_charger_info *g_info;
static void rt1715_send_rdo(struct work_struct *work);

//Owner
#define MSG_OWNER_RT			32784 //add MSG_OWNER_RT for RT1715
//Type
#define MSG_TYPE_RT_ADSP_NOTIFY		0xF0
#define MSG_TYPE_ADSP_RT_NOTIFY		0xF1
//Opcode
#define MSG_OPCODE_RT_PDO		0xF2
#define MSG_OPCODE_RT_CUST_SRC	0xF3
#define MSG_OPCODE_RT_RDO		0xF4
#define MSG_OPCODE_RT_APSD		0xF5
//#define MSG_OPCODE_RT_SIDE_OTG	0xF6

#define RT_WAIT_TIME_MS			1000

struct rt1715_pdo_resp_msg {
	struct pmic_glink_hdr	hdr;
	u8	tcpc_pd_state;
	u8	nr;
	u32	pdos[PDO_MAX_NR];
	u32	vid;
	u8	gamepad_active;
};

struct rt1715_cust_src_resp_msg {
	struct pmic_glink_hdr	hdr;
	u8 cust_src;
};


struct rt1715_apsd_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32	value;
};

//struct rt1715_side_otg_resp_msg {
	//struct pmic_glink_hdr	hdr;
	//u8	side_otg_en;
//};

struct rt1715_resp_msg {
	struct pmic_glink_hdr	hdr;
};

struct rt1715_rdo_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32 selected_pos;
	u32 request_voltage_mv;
	u32 request_current_ma;
	u32 bPPSSelected;
};

int rt_chg_get_during_swap(void) {
	struct tcpc_device *tcpc;
	tcpc = tcpc_dev_get_by_name("typec");

	if (!tcpc) {
		pr_info("[PD] %s: get rt1711-tcpc fail\n", __func__);
	}

	pr_info("[PD] %s: during swap = %d\n", __func__, tcpc->pd_port.pe_data.during_swap);
	return tcpc->pd_port.pe_data.during_swap;
}
EXPORT_SYMBOL(rt_chg_get_during_swap);

int typec_disable_function(bool disable) {
    struct tcpc_device *tcpc;
    tcpc = tcpc_dev_get_by_name("typec");

    tcpm_typec_disable_function(tcpc, disable);
    pr_info("[PD] %s enter, disable= %d\n", __func__, disable);
    return 0;
}
EXPORT_SYMBOL(typec_disable_function);

/*
 * reset btm typec when btm typec is src
 * and unpluging side usb cable
 */
int typec_attached_src_reset(void) {
    struct tcpc_device *tcpc;
    u8 cc_state;
    tcpc = tcpc_dev_get_by_name("typec");
    cc_state = tcpm_inquire_typec_attach_state(tcpc);

    // cc_state = TYPEC_ATTACHED_SRC = 2
    if(cc_state == 2 && gamepad_active == 0) {
		pr_info("[PD] %s \n", __func__);
		tcpm_typec_disable_function(tcpc, 1);
		msleep(100);
		tcpm_typec_disable_function(tcpc, 0);
	}
    return 0;
}
EXPORT_SYMBOL(typec_attached_src_reset);

int rt1715_glink_write(struct rt_charger_info *info, void *data, int len)
{
	int rc;

	if (info == NULL) {
		pr_err("[PD] %s: get info fail\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&info->rw_lock);
	reinit_completion(&info->ack);
	rc = pmic_glink_write(info->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&info->ack,
					msecs_to_jiffies(RT_WAIT_TIME_MS));
		if (!rc) {
			pr_err("[PD] %s time out sending message\n", __func__);
			mutex_unlock(&info->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&info->rw_lock);

	return rc;
}

void rt1715_pdo_notify(void) {

	int rc = -1;
	int i, ret;
	struct rt1715_pdo_resp_msg rt1715_pdo_msg = {};
	struct tcpm_remote_power_cap remote_cap;
	struct tcpc_device *tcpc;

	//get pdo +++
	tcpc = tcpc_dev_get_by_name("typec");

	if (!tcpc) {
		pr_info("[PD] %s: get rt1711-tcpc fail\n", __func__);
		return;
	}

	ret = tcpm_get_remote_power_cap(tcpc, &remote_cap);
	if(ret < 0) {
		pr_info("[PD] %s: Get remote power cap fail, error number = %d\n", __func__, ret);
		return;
	}
	rt1715_pdo_msg.nr = remote_cap.nr;
	for (i = 0; i < remote_cap.nr; i++)
		rt1715_pdo_msg.pdos[i] = tcpc->pd_port.pe_data.remote_src_cap.pdos[i];
	//get pdo ---

	rt1715_pdo_msg.hdr.owner = MSG_OWNER_RT;
	rt1715_pdo_msg.hdr.type = MSG_TYPE_RT_ADSP_NOTIFY;
	rt1715_pdo_msg.hdr.opcode = MSG_OPCODE_RT_PDO;
	rt1715_pdo_msg.tcpc_pd_state = tcpc_pd_state;
	rt1715_pdo_msg.vid = vid_ext;
	rt1715_pdo_msg.gamepad_active = gamepad_active;

	pr_info("[PD] %s tcpc_pd_state = %d\n", __func__, tcpc_pd_state);
	rc = rt1715_glink_write(g_info, &rt1715_pdo_msg, sizeof(rt1715_pdo_msg));
	if (rc < 0) {
		pr_err("[PD] %s Error in sending message rc=%d\n", __func__, rc);
		return;
	}

}
EXPORT_SYMBOL(rt1715_pdo_notify);

void rt1715_cust_src_notify(u8 enable) {

	int rc = -1;
	struct rt1715_cust_src_resp_msg rt1715_cust_src_msg = {};

	rt1715_cust_src_msg.hdr.owner = MSG_OWNER_RT;
	rt1715_cust_src_msg.hdr.type = MSG_TYPE_RT_ADSP_NOTIFY;
	rt1715_cust_src_msg.hdr.opcode = MSG_OPCODE_RT_CUST_SRC;
	rt1715_cust_src_msg.cust_src = enable;

	rc = rt1715_glink_write(g_info, &rt1715_cust_src_msg, sizeof(rt1715_cust_src_msg));
	if (rc < 0) {
		pr_err("[PD] %s Error in sending message rc=%d\n", __func__, rc);
		return;
	}

}
EXPORT_SYMBOL(rt1715_cust_src_notify);

static void rt1715_apsd_notify(struct notifier_block *nb) {

	int rc = -1;
	struct rt1715_apsd_resp_msg rt1715_apsd_msg = {};
	struct rt_charger_info *info = container_of(nb, struct rt_charger_info, nb);

	if (info == NULL) {
		pr_err("[PD] %s: get info fail\n", __func__);
		return;
	}

	rt1715_apsd_msg.hdr.owner = MSG_OWNER_RT;
	rt1715_apsd_msg.hdr.type = MSG_TYPE_RT_ADSP_NOTIFY;
	rt1715_apsd_msg.hdr.opcode = MSG_OPCODE_RT_APSD;
	rt1715_apsd_msg.value = 1;

	rc = rt1715_glink_write(info, &rt1715_apsd_msg, sizeof(rt1715_apsd_msg));
	if (rc < 0) {
		pr_err("[PD] %s Error in sending message rc=%d\n", __func__, rc);
		return;
	}

}

static void handle_message(struct rt_charger_info *info, void *data,
				size_t len)
{
	struct rt1715_resp_msg *resp_msg = data;
	struct rt1715_pdo_resp_msg *adsp_pdo_msg;
	struct rt1715_cust_src_resp_msg *adsp_cust_src_msg;
	struct rt1715_rdo_resp_msg *adsp_rdo_msg;
	struct rt1715_apsd_resp_msg *adsp_apsd_msg;
	//struct rt1715_side_otg_resp_msg *adsp_side_otg_msg;
	int i;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case MSG_OPCODE_RT_PDO:
		if (len == sizeof(*adsp_pdo_msg)) {
			adsp_pdo_msg = data;
			ack_set = true;
			pr_info("[PD] %s adsp_pdo_msg tcpc_pd_state = %d, vid = 0x%04x, gamepad_active = %d\n",
					    __func__, adsp_pdo_msg->tcpc_pd_state, adsp_pdo_msg->vid, adsp_pdo_msg->gamepad_active);
			for (i = 0; i < adsp_pdo_msg->nr; i++) {
				pr_info("[PD] %s nr = %d, pdos = 0x%x\n", __func__, i, adsp_pdo_msg->pdos[i]);
			}
		} else {
			pr_info("[PD] %s Incorrect response length %zu for MSG_OPCODE_RT_PDO\n", __func__, len);
		}
		break;
	case MSG_OPCODE_RT_CUST_SRC:
		if (len == sizeof(*adsp_cust_src_msg)) {
			adsp_cust_src_msg = data;
			ack_set = true;
			pr_info("[PD] %s adsp_cust_src_msg cust_src = %d\n", __func__, adsp_cust_src_msg->cust_src);
		} else {
			pr_info("[PD] %s Incorrect response length %zu for MSG_OPCODE_RT_CUST_SRC\n", __func__, len);
		}
		break;
	case MSG_OPCODE_RT_RDO:
		if (len == sizeof(*adsp_rdo_msg)) {
			adsp_rdo_msg = data;
			ack_set = true;
			pr_info("[PD] %s adsp_rdo_msg\n", __func__);
			g_info->select_pos = adsp_rdo_msg->selected_pos;
			g_info->rdo_mv = adsp_rdo_msg->request_voltage_mv;
			g_info->rdo_ma = adsp_rdo_msg->request_current_ma;
			g_info->fixed_pdo = !(adsp_rdo_msg->bPPSSelected);

			if(!rdo_wq) {
				pr_err("[PD] %s: rdo_wq work queue init fail\n", __func__);
			} else {
				queue_delayed_work(rdo_wq, &rdo_work, 0);
			}
		} else {
			pr_info("[PD] %s Incorrect response length %zu for MSG_OPCODE_RT_RDO\n", __func__, len);
 		}
 		break;
 	case MSG_OPCODE_RT_APSD:
		if (len == sizeof(*adsp_apsd_msg)) {
			adsp_apsd_msg = data;
			ack_set = true;
			pr_info("[PD] %s adsp_apsd_msg value = %d\n",adsp_apsd_msg->value);
		} else {
			pr_info("[PD] %s Incorrect response length %zu for MSG_OPCODE_RT_APSD\n", len);
		}
		break;
	//case MSG_OPCODE_RT_SIDE_OTG:
		//if (len == sizeof(*adsp_side_otg_msg)) {
			//adsp_side_otg_msg = data;
			//ack_set = true;
			//pr_info("[PD] %s adsp_side_otg_msg value = %d\n",adsp_side_otg_msg->side_otg_en);
		//} else {
			//pr_info("[PD] %s Incorrect response length %zu for MSG_OPCODE_RT_SIDE_OTG\n", len);
		//}
		//break;
	default:
		ack_set = true;
		pr_info("[PD] %s Unknown opcode: %u\n", __func__, resp_msg->hdr.opcode);
		break;
	}

	if (ack_set)
		complete(&info->ack);

}

static int rt_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct rt_charger_info *info = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	if (hdr->owner == MSG_OWNER_RT){
		switch (hdr->type){
			case MSG_TYPE_ADSP_RT_NOTIFY:
				handle_message(info, data, len);
				break;
			default:
				break;
		}
	}

	return 0;
}

static void rt_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	//struct rt_charger_info *info = priv;
	pr_debug("rt_chg_state_cb: %d\n", state);
}

static void rt1715_send_rdo(struct work_struct *work) 
{
	int ret = 10;
	uint8_t max_power_policy = 0x21;

	pr_info("[PD] %s pos = %d, mv = %d, ma = %d, fixed_pdo = %d", 
		__func__, g_info->select_pos, g_info->rdo_mv, g_info->rdo_ma, g_info->fixed_pdo);

	if (g_info->tcpc->pd_port.pe_pd_state != PE_SNK_READY){
		pr_info("[PD] %s pd state is not ready\n", __func__);
		return;
	}

	if (g_info->fixed_pdo == 1){
		/* reqeust fixed pdo */
		tcpm_set_pd_charging_policy(g_info->tcpc, max_power_policy, NULL);
		tcpm_dpm_pd_request(g_info->tcpc, g_info->rdo_mv, g_info->rdo_ma, NULL);
	}
	else{
		/* request APDO */
		ret = tcpm_set_apdo_charging_policy(g_info->tcpc, DPM_CHARGING_POLICY_PPS, g_info->rdo_mv, g_info->rdo_ma, NULL);
	}
}

static int rt_charger_probe(struct platform_device *pdev)
{
	struct rt_charger_info *info;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	int ret;

	pr_info("[PD] %s\n", __func__);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;

	client_data.id = MSG_OWNER_RT;
	client_data.name = "RT_charger";
	client_data.msg_cb = rt_chg_callback;
	client_data.priv = info;
	client_data.state_cb = rt_chg_state_cb;

	info->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(info->client)) {
		ret = PTR_ERR(info->client);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				ret);
		return ret;
	}

	mutex_init(&info->rw_lock);
	init_completion(&info->ack);

	platform_set_drvdata(pdev, info);

	/* Get tcpc device by tcpc_device'name */
	info->tcpc = tcpc_dev_get_by_name("typec");
	if (!info->tcpc) {
		dev_err(&pdev->dev, "get rt1711-tcpc fail\n");
		//power_supply_unregister(&info->chg);
		return -ENODEV;
	}

	g_info = info;

	rdo_wq = create_singlethread_workqueue("rt1715_send_rdo");
	INIT_DELAYED_WORK(&rdo_work, rt1715_send_rdo);

	ret = tcpm_inquire_typec_attach_state(info->tcpc);
	if (ret == TYPEC_ATTACHED_SNK && !tcpm_inquire_pd_connected(info->tcpc)) {
		/*
		 * Send notify to apsd to re-run BC1.2 for
		 * adb function can't work issue when bottom usb plugin
		 * usb cable and boot. Only send notify when device is SINk and
		 * source device doesn't support PD.
		 */
		pr_info("[PD] %s: send apsd notify\n", __func__);
		rt1715_apsd_notify(&info->nb);
	} else {
		/*
		 * Trigger disable/enable typec function for otg or non-PD adapter
		 * can't work issue when bottom usb plugin otg cable or non-PD adapter and boot
		 * Only trigger disable/enable typec function when bottom usb plugin
		 * otg cable and non-PD adapte.
		 */
		pr_info("[PD] %s: disable/enable typec function\n", __func__);
		tcpm_typec_disable_function(info->tcpc, 1);
		msleep(200);
		tcpm_typec_disable_function(info->tcpc, 0);
	}

	pr_info("[PD] %s: OK!\n", __func__);
	return 0;
}

static int rt_charger_remove(struct platform_device *pdev)
{
	//struct rt_charger_info *info = platform_get_drvdata(pdev);

	//power_supply_unregister(info->chg);
	return 0;
}

static struct of_device_id rt_match_table[] = {
	{.compatible = "richtek,rt-charger",},
};

static struct platform_driver rt_charger_driver = {
	.driver = {
		.name = "rt-charger",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt_charger_probe,
	.remove = rt_charger_remove,
};

static int __init rt_chg_init(void)
{
	int ret;

	ret = platform_driver_register(&rt_charger_driver);
	if(ret)
		pr_info("[PD] %s: unable to register driver (%d)\n", __func__, ret);

	return ret;
}

static void __exit rt_chg_exit(void)
{
	platform_driver_unregister(&rt_charger_driver);
}
late_initcall(rt_chg_init);
module_exit(rt_chg_exit);

MODULE_DESCRIPTION("Dummy Charger driver  for kernel-4.9");
MODULE_LICENSE("GPL");
