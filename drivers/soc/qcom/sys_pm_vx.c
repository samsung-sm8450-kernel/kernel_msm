// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved. */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define MAX_QMP_MSG_SIZE	96
#define MODE_AOSS		0xaa
#define MODE_CXPC		0xcc
#define MODE_DDR		0xdd
#define MODE_STR(m)		(m == MODE_CXPC ? "CXPC" :	\
				(m == MODE_AOSS ? "AOSS" :	\
				(m == MODE_DDR  ? "DDR"  : "")))

#define VX_MODE_MASK_TYPE		0xFF
#define VX_MODE_MASK_LOGSIZE		0xFF
#define VX_MODE_SHIFT_LOGSIZE		8
#define VX_FLAG_MASK_DUR		0xFFFF
#define VX_FLAG_MASK_TS			0xFF
#define VX_FLAG_SHIFT_TS		16
#define VX_FLAG_MASK_FLUSH_THRESH	0xFF
#define VX_FLAG_SHIFT_FLUSH_THRESH	24

#define read_word(base, itr) ({					\
		u32 v;						\
		v = le32_to_cpu(readl_relaxed(base + itr));	\
		pr_debug("Addr:%p val:%#x\n", base + itr, v);	\
		itr += sizeof(u32);				\
		/* Barrier to enssure sequential read */	\
		smp_rmb();					\
		v;						\
		})

struct vx_header {
	struct {
		u16 unused;
		u8 logsize;
		u8 type;
	} mode;
	struct {
		u8 flush_threshold;
		u8 ts_shift;
		u16 dur_ms;
	} flags;
};

struct vx_data {
	u32 ts;
	u32 *drv_vx;
};

struct vx_log {
	struct vx_header header;
	struct vx_data *data;
	int loglines;
};

struct vx_platform_data {
	void __iomem *base;
	struct dentry *vx_file;
	size_t ndrv;
	const char **drvs;
};

static const char * const drv_names_lahaina[] = {
	"TZ", "HYP", "HLOS", "L3", "SECPROC", "AUDIO", "SENSOR", "AOP",
	"DEBUG", "GPU", "DISPLAY", "COMPUTE", "MDM SW", "MDM HW", "WLAN RF",
	"WLAN BB", "DDR AUX", "ARC CPRF",
	""
};

static const char * const drv_names_waipio[] = {
	"TZ", "HYP", "HLOS", "L3", "SECPROC", "AUDIO", "SENSOR", "AOP",
	"DEBUG", "GPU", "DISPLAY", "COMPUTE_DSP", "TIME_SW", "TIME_HW",
	"MDM SW", "MDM HW", "WLAN RF", "WLAN BB", "DDR AUX", "ARC CPRF",
	""
};

static int read_vx_data(struct vx_platform_data *pd, struct vx_log *log)
{
	void __iomem *base = pd->base;
	struct vx_header *hdr = &log->header;
	struct vx_data *data;
	u32 *vx, val, itr = 0;
	int i, j, k;

	val = read_word(base, itr);
	if (!val)
		return -ENOENT;

	hdr->mode.type = val & VX_MODE_MASK_TYPE;
	hdr->mode.logsize = (val >> VX_MODE_SHIFT_LOGSIZE) &
				    VX_MODE_MASK_LOGSIZE;

	val = read_word(base, itr);
	if (!val)
		return -ENOENT;

	hdr->flags.dur_ms = val & VX_FLAG_MASK_DUR;
	hdr->flags.ts_shift = (val >> VX_FLAG_SHIFT_TS) & VX_FLAG_MASK_TS;
	hdr->flags.flush_threshold = (val >> VX_FLAG_SHIFT_FLUSH_THRESH) &
					     VX_FLAG_MASK_FLUSH_THRESH;

	data = kcalloc(hdr->mode.logsize, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < hdr->mode.logsize; i++) {
		data[i].ts = read_word(base, itr);
		if (!data[i].ts)
			break;
		data[i].ts <<= hdr->flags.ts_shift;
		vx = kcalloc(ALIGN(pd->ndrv, 4), sizeof(*vx), GFP_KERNEL);
		if (!vx)
			goto no_mem;

		for (j = 0; j < pd->ndrv;) {
			val = read_word(base, itr);
			for (k = 0; k < 4; k++)
				vx[j++] = val >> (8 * k) & 0xFF;
		}
		data[i].drv_vx = vx;
	}

	log->data = data;
	log->loglines = i;

	return 0;
no_mem:
	for (j = 0; j < i; j++)
		kfree(data[j].drv_vx);
	kfree(data);

	return -ENOMEM;
}

static void show_vx_data(struct vx_platform_data *pd, struct vx_log *log,
			 struct seq_file *seq)
{
	int i, j;
	struct vx_header *hdr = &log->header;
	struct vx_data *data;
	u32 prev;
	bool from_exit = false;

	seq_printf(seq, "Mode           : %s\n"
			"Duration (ms)  : %u\n"
			"Time Shift     : %u\n"
			"Flush Threshold: %u\n"
			"Max Log Entries: %u\n",
			MODE_STR(hdr->mode.type),
			hdr->flags.dur_ms,
			hdr->flags.ts_shift,
			hdr->flags.flush_threshold,
			hdr->mode.logsize);

	seq_puts(seq, "Timestamp|");

	for (i = 0; i < pd->ndrv; i++)
		seq_printf(seq, "%*s|", 8, pd->drvs[i]);
	seq_puts(seq, "\n");

	for (i = 0; i < log->loglines; i++) {
		data = &log->data[i];
		seq_printf(seq, "%*x|", 9, data->ts);
		/* An all-zero line indicates we entered LPM */
		for (j = 0, prev = data->drv_vx[0]; j < pd->ndrv; j++)
			prev |= data->drv_vx[j];
		if (!prev) {
			if (!from_exit) {
				seq_printf(seq, "%s Enter\n", MODE_STR(hdr->mode.type));
				from_exit = true;
			} else {
				seq_printf(seq, "%s Exit\n", MODE_STR(hdr->mode.type));
				from_exit = false;
			}
			continue;
		}
		for (j = 0; j < pd->ndrv; j++)
			seq_printf(seq, "%*u|", 8, data->drv_vx[j]);
		seq_puts(seq, "\n");
	}
}

static int vx_show(struct seq_file *seq, void *data)
{
	struct vx_platform_data *pd = seq->private;
	struct vx_log log;
	int ret;
	int i;

	/*
	 * Read the data into memory to allow for
	 * post processing of data and present it
	 * cleanly.
	 */
	ret = read_vx_data(pd, &log);
	if (ret)
		return ret;

	show_vx_data(pd, &log, seq);

	for (i = 0; i < log.loglines; i++)
		kfree(log.data[i].drv_vx);
	kfree(log.data);

	return 0;
}

#if IS_ENABLED(CONFIG_SEC_PM)
struct vx_platform_data *local_pd;

static int debug_show_vx_data(struct vx_platform_data *pd, struct vx_log *log)
{
	int i, j;
	struct vx_header *hdr = &log->header;
	struct vx_data *data;
	char buf[255];
	char *buf_ptr = buf;
	u32 prev;
	bool log_is_full = false;
	int need_restart = 0;

	printk(KERN_INFO "PM: CXSD blocker\nTimestamp|\n");

	if (log->loglines == 42) {
		log_is_full = true;
		need_restart = 1;
		i = 0;
	} else {
		i = (log->loglines < 14) ? 0 : (log->loglines -14);
	}

	for ( ; i < log->loglines; i++) {
		if (log_is_full) {
			/* Show only multiples of 3 indexes */
			if (i%3 != 0) continue;
		}
		
		data = &log->data[i];
		buf_ptr += sprintf(buf_ptr, "%*x|", 9, data->ts);

		/* An all-zero line indicates we entered LPM */
		for (j = 0, prev = data->drv_vx[0]; j < pd->ndrv; j++)
			prev |= data->drv_vx[j];
		if (!prev) {
			buf_ptr += sprintf(buf_ptr, "%s Enter\n", MODE_STR(hdr->mode.type));
			printk(KERN_INFO "%s", buf);
			need_restart = 1;

			buf[0] = '\0';
			buf_ptr = buf;
			continue;
		}

		/* Show non-zero items only */
		for (j = 0; j < pd->ndrv; j++) {
			if(data->drv_vx[j])
				buf_ptr += sprintf(buf_ptr, " %s (%u)", pd->drvs[j], data->drv_vx[j]);
		}
		buf_ptr += sprintf(buf_ptr, "\n");
		printk(KERN_INFO "%s", buf);

		buf[0] = '\0';
		buf_ptr = buf;
	}

	return need_restart;
}

int debug_vx_show(void)
{
	struct vx_log log;
	int ret;
	int i;

	/*
	 * Read the data into memory to allow for
	 * post processing of data and present it
	 * cleanly.
	 */
	ret = read_vx_data(local_pd, &log);
	if (ret)
		return ret;

	ret = debug_show_vx_data(local_pd, &log);

	for (i = 0; i < log.loglines; i++)
		kfree(log.data[i].drv_vx);
	kfree(log.data);

	return ret;
}
EXPORT_SYMBOL(debug_vx_show);
#endif /* CONFIG_SEC_PM */

static int open_vx(struct inode *inode, struct file *file)
{
	return single_open(file, vx_show, inode->i_private);
}

static const struct file_operations sys_pm_vx_fops = {
	.open = open_vx,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int vx_create_debug_nodes(struct vx_platform_data *pd)
{
	struct dentry *pf;

	pf = debugfs_create_file("sys_pm_violators", 0400, NULL,
				 pd, &sys_pm_vx_fops);
	if (!pf)
		return -EINVAL;

	pd->vx_file = pf;

	return 0;
}

static const struct of_device_id drv_match_table[] = {
	{ .compatible = "qcom,sys-pm-lahaina",
	  .data = drv_names_lahaina },
	{ .compatible = "qcom,sys-pm-waipio",
	  .data = drv_names_waipio },
	{ }
};

static int vx_probe(struct platform_device *pdev)
{
	const struct of_device_id *match_id;
	struct vx_platform_data *pd;
	const char **drvs;
	int i, ret;

	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->base = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR_OR_NULL(pd->base))
		return PTR_ERR(pd->base);

	match_id = of_match_node(drv_match_table, pdev->dev.of_node);
	if (!match_id)
		return -ENODEV;

	drvs = (const char **)match_id->data;
	for (i = 0; ; i++) {
		const char *name = (const char *)drvs[i];

		if (!name[0])
			break;
	}
	pd->ndrv = i;
	pd->drvs = drvs;

#if IS_ENABLED(CONFIG_SEC_PM)
	local_pd = pd;
#endif /* CONFIG_SEC_PM */

	ret = vx_create_debug_nodes(pd);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pd);

	return 0;
}

static int vx_remove(struct platform_device *pdev)
{
	struct vx_platform_data *pd = platform_get_drvdata(pdev);

	debugfs_remove(pd->vx_file);

	return 0;
}

static const struct of_device_id vx_table[] = {
	{ .compatible = "qcom,sys-pm-violators" },
	{ }
};

static struct platform_driver vx_driver = {
	.probe = vx_probe,
	.remove = vx_remove,
	.driver = {
		.name = "sys-pm-violators",
		.of_match_table = vx_table,
	},
};
module_platform_driver(vx_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM System PM Violators Driver");
MODULE_ALIAS("platform:msm_sys_pm_vx");
