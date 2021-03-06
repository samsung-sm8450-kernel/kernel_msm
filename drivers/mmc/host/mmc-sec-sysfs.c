// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Specific feature
 *
 * Copyright (C) 2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *      Storage Driver <storage.sec@samsung.com>
 */

#include <linux/sysfs.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/mmc/mmc.h>

#include "mmc-sec-sysfs.h"

/* for sd_card_dev : begin */
static ssize_t t_flash_detect_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);

	if (!mmc_gpio_get_cd(host)) {
		if (sd_op_info.sd_slot_type == SEC_HYBRID_SD_SLOT) {
			pr_debug("SD slot tray Removed.\n");
			return sprintf(buf, "Notray\n");
		}
	}
	if (host->card) {
		pr_debug("External sd: card inserted.\n");
		return sprintf(buf, "Insert\n");
	}
	pr_debug("External sd: card removed.\n");
	return sprintf(buf, "Remove\n");
}
static DEVICE_ATTR(status, 0444, t_flash_detect_show, NULL);

static ssize_t sd_detect_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	dev_info(dev, "%s : CD count is = %u\n", __func__, sd_op_info.card_detect_cnt);
	return sprintf(buf, "%u\n", sd_op_info.card_detect_cnt);
}
static DEVICE_ATTR(cd_cnt, 0444, sd_detect_cnt_show, NULL);

static ssize_t sd_detect_maxmode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	const char *uhs_bus_speed_mode = "";

	if (host->caps & MMC_CAP_UHS_SDR104)
		uhs_bus_speed_mode = "SDR104";
	else if (host->caps & MMC_CAP_UHS_DDR50)
		uhs_bus_speed_mode = "DDR50";
	else if (host->caps & MMC_CAP_UHS_SDR50)
		uhs_bus_speed_mode = "SDR50";
	else if (host->caps & MMC_CAP_UHS_SDR25)
		uhs_bus_speed_mode = "SDR25";
	else if (host->caps & MMC_CAP_UHS_SDR12)
		uhs_bus_speed_mode = "SDR12";
	else
		uhs_bus_speed_mode = "HS";

	dev_info(dev, "%s: Max supported Host Speed Mode = %s\n",
			__func__, uhs_bus_speed_mode);
	return sprintf(buf, "%s\n", uhs_bus_speed_mode);
}
static DEVICE_ATTR(max_mode, 0444, sd_detect_maxmode_show, NULL);

static ssize_t sd_detect_curmode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card;
	const char *uhs_bus_speed_mode = "";
	static const char *const uhs_speeds[] = {
		[UHS_SDR12_BUS_SPEED]	= "SDR12",
		[UHS_SDR25_BUS_SPEED]	= "SDR25",
		[UHS_SDR50_BUS_SPEED]	= "SDR50",
		[UHS_SDR104_BUS_SPEED]	= "SDR104",
		[UHS_DDR50_BUS_SPEED]	= "DDR50",
	};

	if (host && host->card) {
		card = host->card;

		if (mmc_card_uhs(card))
			uhs_bus_speed_mode = uhs_speeds[sd_op_info.sd_bus_speed];
		else if (mmc_card_hs(card))
			uhs_bus_speed_mode = "HS";
		else
			uhs_bus_speed_mode = "DS";
	} else
		uhs_bus_speed_mode = "No Card";

	dev_info(dev, "%s: Current SD Card Speed = %s\n",
			__func__, uhs_bus_speed_mode);
	return sprintf(buf, "%s\n", uhs_bus_speed_mode);
}
static DEVICE_ATTR(current_mode, 0444, sd_detect_curmode_show, NULL);

static ssize_t sd_detect_curphase_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);

	if (host && host->card)
		if (sd_op_info.sd_bus_speed == UHS_SDR104_BUS_SPEED)
			return sprintf(buf, "%d\n", (int)sd_op_info.saved_tuning_phase);

	return sprintf(buf, "%d\n", -1);
}
static DEVICE_ATTR(current_phase, 0444, sd_detect_curphase_show, NULL);

static ssize_t sdcard_summary_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card;
	const char *uhs_bus_speed_mode = "";
	static const char *const uhs_speeds[] = {
		[UHS_SDR12_BUS_SPEED]	= "SDR12",
		[UHS_SDR25_BUS_SPEED]	= "SDR25",
		[UHS_SDR50_BUS_SPEED]	= "SDR50",
		[UHS_SDR104_BUS_SPEED]	= "SDR104",
		[UHS_DDR50_BUS_SPEED]	= "DDR50",
	};
	static const char *const unit[] = {"KB", "MB", "GB", "TB"};
	unsigned int size, serial;
	int digit = 1;
	char ret_size[6];

	if (host && host->card) {
		card = host->card;

		/* MANID */
		/* SERIAL */
		serial = card->cid.serial & (0x0000FFFF);

		/*SIZE*/
		if (card->csd.read_blkbits == 9)		/* 1 Sector = 512 Bytes */
			size = (card->csd.capacity) >> 1;
		else if (card->csd.read_blkbits == 11)		/* 1 Sector = 2048 Bytes */
			size = (card->csd.capacity) << 1;
		else						/* 1 Sector = 1024 Bytes */
			size = card->csd.capacity;

		if (size >= 380000000 && size <= 410000000) {	/* QUIRK 400GB SD Card */
			sprintf(ret_size, "400GB");
		} else if (size >= 190000000 && size <= 210000000) {	/* QUIRK 200GB SD Card */
			sprintf(ret_size, "200GB");
		} else {
			while ((size >> 1) > 0) {
				size = size >> 1;
				digit++;
			}
			sprintf(ret_size, "%d%s", 1 << (digit%10), unit[digit/10]);
		}

		/* SPEEDMODE */
		if (mmc_card_uhs(card))
			uhs_bus_speed_mode = uhs_speeds[sd_op_info.sd_bus_speed];
		else if (mmc_card_hs(card))
			uhs_bus_speed_mode = "HS";
		else
			uhs_bus_speed_mode = "DS";

		/* SUMMARY */
		dev_info(dev, "MANID : 0x%02X, SERIAL : %04X, SIZE : %s, SPEEDMODE : %s\n",
				card->cid.manfid, serial, ret_size, uhs_bus_speed_mode);
		return sprintf(buf, "\"MANID\":\"0x%02X\",\"SERIAL\":\"%04X\""	\
				",\"SIZE\":\"%s\",\"SPEEDMODE\":\"%s\",\"NOTI\":\"%d\"\n",
				card->cid.manfid, serial, ret_size, uhs_bus_speed_mode,
				sd_status_err_info[host->index].noti_cnt);
	} else {
		/* SUMMARY : No SD Card Case */
		return sprintf(buf, "\"MANID\":\"NoCard\",\"SERIAL\":\"NoCard\""	\
				",\"SIZE\":\"NoCard\",\"SPEEDMODE\":\"NoCard\""		\
				",\"NOTI\":\"NoCard\"\n");
	}
}
static DEVICE_ATTR(sdcard_summary, 0444, sdcard_summary_show, NULL);

static inline void mmc_check_error_count(struct mmc_sec_err_info *err_log,
		unsigned long long *total_c_cnt, unsigned long long *total_t_cnt)
{
	int i = 0;

	//Only sbc(0,1)/cmd(2,3)/data(4,5) is checked.
	for (i = 0; i < 6; i++) {
		if (err_log[i].err_type == -EILSEQ && *total_c_cnt < U64_MAX)
			*total_c_cnt += err_log[i].count;
		if (err_log[i].err_type == -ETIMEDOUT && *total_t_cnt < U64_MAX)
			*total_t_cnt += err_log[i].count;
	}
}

#define SEC_MMC_STATUS_ERR_INFO_GET_VALUE(member) ({	\
		err_status_info->member = err_status->member - err_status_backup->member; })

static inline void mmc_check_error_count_calc_current(int host_idx,
		unsigned long long *total_c_cnt, unsigned long long *total_t_cnt,
		struct mmc_sec_status_err_info *err_status_info)
{
	struct mmc_sec_err_info *err_log = sd_err_info[host_idx];
	struct mmc_sec_err_info *err_log_backup = sd_err_info_backup[host_idx];
	struct mmc_sec_status_err_info *err_status = &sd_status_err_info[host_idx];
	struct mmc_sec_status_err_info *err_status_backup = &sd_status_err_info_backup[host_idx];
	int i = 0;

	//Only sbc(0,1)/cmd(2,3)/data(4,5) is checked.
	for (i = 0; i < 6; i++) {
		if (err_log[i].err_type == -EILSEQ && *total_c_cnt < U64_MAX)
			*total_c_cnt += (err_log[i].count - err_log_backup[i].count);
		if (err_log[i].err_type == -ETIMEDOUT && *total_t_cnt < U64_MAX)
			*total_t_cnt += (err_log[i].count - err_log_backup[i].count);
	}

	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(ge_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(cc_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(ecc_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(wp_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(oor_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(halt_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(cq_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(rpmb_cnt);
	SEC_MMC_STATUS_ERR_INFO_GET_VALUE(noti_cnt);
}

#define SEC_MMC_STATUS_ERR_INFO_BACKUP(member) ({		\
		err_status_backup->member = err_status->member;	})

static inline void mmc_backup_err_info(int host_idx)
{
	struct mmc_sec_err_info *err_log = sd_err_info[host_idx];
	struct mmc_sec_err_info *err_log_backup = sd_err_info_backup[host_idx];
	struct mmc_sec_status_err_info *err_status = &sd_status_err_info[host_idx];
	struct mmc_sec_status_err_info *err_status_backup = &sd_status_err_info_backup[host_idx];
	int i = 0;

	// save current error count
	for (i = 0; i < MAX_ERR_LOG_INDEX; i++)
		err_log_backup[i].count = err_log[i].count;

	SEC_MMC_STATUS_ERR_INFO_BACKUP(ge_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(cc_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(ecc_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(wp_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(oor_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(halt_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(cq_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(rpmb_cnt);
	SEC_MMC_STATUS_ERR_INFO_BACKUP(noti_cnt);
}

static ssize_t mmc_error_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int host_idx = host->index;
	struct mmc_sec_err_info *err_log = sd_err_info[host_idx];
	u64 total_c_cnt = 0;
	u64 total_t_cnt = 0;
	int total_len = 0;
	int i = 0;

	if (!card) {
		total_len = snprintf(buf, PAGE_SIZE, "It's no card..\n");
		goto out;
	}

	total_len += snprintf(buf, PAGE_SIZE,
			"type : err    status: first_issue_time:  last_issue_time:      count\n");

	for (i = 0; i < MAX_ERR_LOG_INDEX; i++) {
		total_len += snprintf(buf + total_len, PAGE_SIZE - total_len,
				"%5s:%4d 0x%08x %16llu, %16llu, %10d\n",
				err_log[i].type, err_log[i].err_type,
				err_log[i].status,
				err_log[i].first_issue_time,
				err_log[i].last_issue_time,
				err_log[i].count);
	}

	mmc_check_error_count(err_log, &total_c_cnt, &total_t_cnt);

	total_len += snprintf(buf + total_len, PAGE_SIZE - total_len,
			"GE:%d,CC:%d,ECC:%d,WP:%d,OOR:%d,CRC:%lld,TMO:%lld,HALT:%d,CQEN:%d,RPMB:%d\n",
			sd_status_err_info[host_idx].ge_cnt,
			sd_status_err_info[host_idx].cc_cnt,
			sd_status_err_info[host_idx].ecc_cnt,
			sd_status_err_info[host_idx].wp_cnt,
			sd_status_err_info[host_idx].oor_cnt,
			total_c_cnt, total_t_cnt,
			sd_status_err_info[host_idx].halt_cnt,
			sd_status_err_info[host_idx].cq_cnt,
			sd_status_err_info[host_idx].rpmb_cnt);

 out:
	return total_len;
}

static DEVICE_ATTR(err_count, 0444, mmc_error_count_show, NULL);
/* for sd_card_dev : end */

/* for sd_info_dev : begin */
static ssize_t sd_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int host_idx = host->index;
	struct mmc_sec_err_info *err_log = sd_err_info[host_idx];
	u64 total_cnt = 0;
	int len = 0;
	int i = 0;

	if (!card) {
		len = snprintf(buf, PAGE_SIZE, "no card\n");
		goto out;
	}

	for (i = 0; i < 6; i++) {
		if (total_cnt < U64_MAX)
			total_cnt += err_log[i].count;
	}
	len = snprintf(buf, PAGE_SIZE, "%lld\n", total_cnt);

out:
	return len;
}
static DEVICE_ATTR(sd_count, 0444, sd_count_show, NULL);

static ssize_t sd_cid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int len = 0;

	if (!card) {
		len = snprintf(buf, PAGE_SIZE, "no card\n");
		goto out;
	}

	len = snprintf(buf, PAGE_SIZE,
			"%08x%08x%08x%08x\n",
			card->raw_cid[0], card->raw_cid[1],
			card->raw_cid[2], card->raw_cid[3]);
out:
	return len;
}
static DEVICE_ATTR(data, 0444, sd_cid_show, NULL);

static ssize_t sd_health_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int host_idx = host->index;
	struct mmc_sec_err_info *err_log = sd_err_info[host_idx];
	u64 total_c_cnt = 0;
	u64 total_t_cnt = 0;
	int len = 0;

	if (!card) {
		//There should be no spaces in 'No Card'(Vold Team).
		len = snprintf(buf, PAGE_SIZE, "NOCARD\n");
		goto out;
	}

	mmc_check_error_count(err_log, &total_c_cnt, &total_t_cnt);

	if (sd_status_err_info[host_idx].ge_cnt > 100 ||
			sd_status_err_info[host_idx].ecc_cnt > 0 ||
			sd_status_err_info[host_idx].wp_cnt > 0 ||
			sd_status_err_info[host_idx].oor_cnt > 10 ||
			total_t_cnt > 100 || total_c_cnt > 100)
		len = snprintf(buf, PAGE_SIZE, "BAD\n");
	else
		len = snprintf(buf, PAGE_SIZE, "GOOD\n");

out:
	return len;
}
static DEVICE_ATTR(fc, 0444, sd_health_show, NULL);
/* for sd_info_dev : end */

/* for sd_data_dev : begin */
static ssize_t sd_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int host_idx = host->index;
	struct mmc_sec_status_err_info status_err_info;
	u64 total_c_cnt = 0;
	u64 total_t_cnt = 0;
	int len = 0;

	memset(&status_err_info, 0, sizeof(struct mmc_sec_status_err_info));

	if (!card) {
		len = snprintf(buf, PAGE_SIZE,
			"\"GE\":\"0\",\"CC\":\"0\",\"ECC\":\"0\",\"WP\":\"0\"," \
			"\"OOR\":\"0\",\"CRC\":\"0\",\"TMO\":\"0\"\n");
		goto out;
	}

	mmc_check_error_count_calc_current(host_idx, &total_c_cnt, &total_t_cnt, &status_err_info);

	len = snprintf(buf, PAGE_SIZE,
		"\"GE\":\"%d\",\"CC\":\"%d\",\"ECC\":\"%d\",\"WP\":\"%d\"," \
		"\"OOR\":\"%d\",\"CRC\":\"%lld\",\"TMO\":\"%lld\"\n",
		status_err_info.ge_cnt,
		status_err_info.cc_cnt,
		status_err_info.ecc_cnt,
		status_err_info.wp_cnt,
		status_err_info.oor_cnt,
		total_c_cnt, total_t_cnt);
out:
	return len;
}

static ssize_t sd_data_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mmc_host *host = dev_get_drvdata(dev);
	struct mmc_card *card = host->card;
	int host_idx = host->index;

	if (!card)
		return -ENODEV;

	if ((buf[0] != 'C' && buf[0] != 'c') || (count != 1))
		return -EINVAL;

	mmc_backup_err_info(host_idx);

	return count;
}

static DEVICE_ATTR(sd_data, 0664, sd_data_show, sd_data_store);
/* for sd_data_dev : end */

static struct attribute *sd_card_attributes[] = {
	&dev_attr_status.attr,
	&dev_attr_cd_cnt.attr,
	&dev_attr_max_mode.attr,
	&dev_attr_current_mode.attr,
	&dev_attr_current_phase.attr,
	&dev_attr_sdcard_summary.attr,
	&dev_attr_err_count.attr,
	NULL
};

static struct attribute_group sd_card_attribute_group = {
	.attrs = sd_card_attributes,
};

static struct attribute *sd_info_attributes[] = {
	&dev_attr_sd_count.attr,
	&dev_attr_data.attr,
	&dev_attr_fc.attr,
	NULL
};

static struct attribute_group sd_info_attribute_group = {
	.attrs = sd_info_attributes,
};

static struct attribute *sd_data_attributes[] = {
	&dev_attr_sd_data.attr,
	NULL
};

static struct attribute_group sd_data_attribute_group = {
	.attrs = sd_data_attributes,
};

void mmc_sec_init_sysfs(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int ret = 0;

	if (sd_card_dev == NULL && !strcmp(host->hw_name, "8804000.sdhci")) {
		pr_debug("%s : Change sysfs Card Detect\n", __func__);

		sd_card_dev = sec_device_create(mmc, "sdcard");
		if (IS_ERR(sd_card_dev)) {
			pr_err("%s : Failed to create device!\n", __func__);
			goto failed_sdcard;
		}

		ret = sysfs_create_group(&sd_card_dev->kobj,
				&sd_card_attribute_group);
		if (ret)
			dev_err(mmc_dev(mmc), "%s: Failed to create sd_card sysfs group (err = %d)\n",
					__func__, ret);
	}
failed_sdcard:

	if (sd_info_dev == NULL && !strcmp(host->hw_name, "8804000.sdhci")) {
		sd_info_dev = sec_device_create(mmc, "sdinfo");
		if (IS_ERR(sd_info_dev)) {
			pr_err("%s : Failed to create device!\n", __func__);
			goto failed_sdinfo;
		}

		ret = sysfs_create_group(&sd_info_dev->kobj,
				&sd_info_attribute_group);
		if (ret)
			dev_err(mmc_dev(mmc), "%s: Failed to create sd_info sysfs group (err = %d)\n",
					__func__, ret);
	}
failed_sdinfo:

	if (sd_data_dev == NULL && !strcmp(host->hw_name, "8804000.sdhci")) {
		sd_data_dev = sec_device_create(mmc, "sddata");
		if (IS_ERR(sd_data_dev)) {
			pr_err("%s : Failed to create device!\n", __func__);
			return;
		}

		ret = sysfs_create_group(&sd_data_dev->kobj,
				&sd_data_attribute_group);
		if (ret)
			dev_err(mmc_dev(mmc), "%s: Failed to create sd_data sysfs group (err = %d)\n",
					__func__, ret);
	}
}
