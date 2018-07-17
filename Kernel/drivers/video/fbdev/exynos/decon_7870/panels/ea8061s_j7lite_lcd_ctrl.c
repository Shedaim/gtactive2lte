/*
 * drivers/video/decon_7870/panels/ea8061s_j7lite_lcd_ctrl.c
 *
 * Samsung SoC MIPI LCD CONTROL functions
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/of_device.h>
#include <video/mipi_display.h>
#include "../dsim.h"
#include "dsim_panel.h"
#include "../decon.h"
#include "../decon_notify.h"

#if defined(CONFIG_PANEL_EA8061S_J7VE)
#include "ea8061s_j7lite_param.h"
#endif

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
#include "mdnie.h"
#include "mdnie_lite_table_j7lite.h"
#endif

#ifdef CONFIG_DISPLAY_USE_INFO
#include "dpui.h"

#define	DPUI_VENDOR_NAME	"MAGNA"
#define DPUI_MODEL_NAME		"AMS549HZ26"
#endif

#define LEVEL_IS_HBM(brightness)		(brightness == EXTEND_BRIGHTNESS)

#define DSI_WRITE(cmd, size)		do {				\
	ret |= dsim_write_hl_data(lcd, cmd, size);			\
	if (ret < 0) {							\
		dev_err(&lcd->ld->dev, "%s: failed to write %s\n", __func__, #cmd);	\
		ret = -EPERM;						\
		goto exit;						\
	}								\
} while (0)

#ifdef SMART_DIMMING_DEBUG
#define smtd_dbg(format, arg...)	printk(format, ##arg)
#else
#define smtd_dbg(format, arg...)
#endif

#define get_bit(value, shift, size)	((value >> shift) & (0xffffffff >> (32 - size)))

union aor_info {
	u32 value;
	struct {
		u8 aor_1;
		u8 aor_2;
		u16 reserved;
	};
};

union elvss_info {
	u32 value;
	struct {
		u8 mpscon;
		u8 offset;
		u8 offset_base;
		u8 tset;
	};
};

union acl_info {
	u32 value;
	struct {
		u8 enable;
		u8 frame_avg;
		u8 start_point;
		u8 percent;
	};
};

struct lcd_info {
	unsigned int			bl;
	unsigned int			brightness;
	unsigned int			acl_enable;
	unsigned int			siop_enable;
	unsigned int			current_bl;
	union elvss_info		current_elvss;
	union aor_info			current_aor;
	union acl_info			current_acl;
	unsigned int			current_hbm;
	unsigned char			current_tset;
	unsigned int			state;

	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct device			svc_dev;
	struct dynamic_aid_param_t	daid;

	unsigned char			*tset_param;
	unsigned char			elvss_table[IBRIGHTNESS_MAX][TEMP_MAX][ELVSS_CMD_CNT];
	unsigned char			gamma_table[IBRIGHTNESS_MAX][GAMMA_CMD_CNT];

	unsigned char			(*aor_table)[AID_CMD_CNT];
	unsigned char			**acl_table;
	unsigned char			**opr_table;
	unsigned char			**hbm_table;

	int				temperature;
	unsigned int			temperature_index;

	union {
		u32			value;
		unsigned char		id[LDI_LEN_ID];
	} id_info;
	unsigned char			mtp[LDI_LEN_MTP];
	unsigned char			date[LDI_LEN_DATE];
	unsigned int			coordinate[2];

	unsigned char			dump_info[3];
	unsigned int			adaptive_control;
	int						lux;
	struct class			*mdnie_class;

	struct dsim_device		*dsim;
	struct mutex			lock;

	struct pinctrl			*pins;
	struct pinctrl_state	*pins_state[2];
	struct notifier_block	fb_notifier;

#ifdef CONFIG_DISPLAY_USE_INFO
	struct notifier_block	dpui_notif;
#endif
};

static int pinctrl_enable(struct lcd_info *lcd, int enable)
{
	int ret = 0;

	if (!IS_ERR_OR_NULL(lcd->pins_state[enable])) {
		ret = pinctrl_select_state(lcd->pins, lcd->pins_state[enable]);
		if (ret)
			dev_err(&lcd->ld->dev, "%s: pinctrl_select_state %d: %d\n", __func__, enable, ret);
	}

	return ret;
}

static int dsim_write_hl_data(struct lcd_info *lcd, const u8 *cmd, u32 cmdSize)
{
	int ret = 0;
	int retry = 5;
	struct panel_private *priv = &lcd->dsim->priv;

	if (!priv->lcdConnected)
		return ret;

try_write:
	if (cmdSize == 1)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_SHORT_WRITE, cmd[0], 0);
	else if (cmdSize == 2)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, cmd[0], cmd[1]);
	else
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_LONG_WRITE, (unsigned long)cmd, cmdSize);

	if (ret != 0) {
		if (--retry)
			goto try_write;
		else
			dev_err(&lcd->ld->dev, "%s: fail. cmd: %x, ret: %d\n", __func__, cmd[0], ret);
	}

	return ret;
}

static int dsim_read_hl_data(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf)
{
	int ret = 0;
	int retry = 4;
	struct panel_private *priv = &lcd->dsim->priv;

	if (!priv->lcdConnected)
		return size;

try_read:
	ret = dsim_read_data(lcd->dsim, MIPI_DSI_DCS_READ, (u32)addr, size, buf);
	dev_info(&lcd->ld->dev, "%s: addr: %x, ret: %d\n", __func__, addr, ret);
	if (ret != size) {
		if (--retry)
			goto try_read;
		else
			dev_err(&lcd->ld->dev, "%s: fail. addr: %x, ret: %d\n", __func__, addr, ret);
	}

	return ret;
}

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE) || defined(CONFIG_LCD_DOZE_MODE)
static int dsim_write_set(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0, i;

	for (i = 0; i < num; i++) {
		if (seq[i].cmd) {
			ret = dsim_write_hl_data(lcd, seq[i].cmd, seq[i].len);
			if (ret != 0) {
				dev_info(&lcd->ld->dev, "%s: %dth fail\n", __func__, i);
				return ret;
			}
		}
		if (seq[i].sleep)
			usleep_range(seq[i].sleep * 1000, seq[i].sleep * 1000);
	}
	return ret;
}
#endif

static int dsim_read_hl_data_offset(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf, u32 offset)
{
	unsigned char wbuf[] = {0xB0, 0};
	int ret = 0;

	wbuf[1] = offset;

	DSI_WRITE(wbuf, ARRAY_SIZE(wbuf));

	ret = dsim_read_hl_data(lcd, addr, size, buf);

	if (ret < 1)
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);

exit:
	return ret;
}

#ifdef CONFIG_PANEL_AID_DIMMING
static void dsim_panel_gamma_ctrl(struct lcd_info *lcd, u8 force)
{
	u8 *gamma = NULL;
	int ret = 0;

	gamma = lcd->gamma_table[lcd->bl];
	if (gamma == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get gamma\n", __func__);
		goto exit;
	}

	if (force)
		goto gamma_update;
	else if (lcd->current_bl != lcd->bl)
		goto gamma_update;
	else
		goto exit;

gamma_update:
	DSI_WRITE(gamma, GAMMA_CMD_CNT);

exit:
	return;
}

static void dsim_panel_aid_ctrl(struct lcd_info *lcd, u8 force)
{
	u8 *aid = NULL;
	int ret = 0;
	union aor_info aor_value;

	aid = lcd->aor_table[lcd->bl];
	if (aid == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get aid value\n", __func__);
		goto exit;
	}

	aor_value.aor_1 = aid[LDI_OFFSET_AOR_1];
	aor_value.aor_2 = aid[LDI_OFFSET_AOR_2];

	DSI_WRITE(aid, AID_CMD_CNT);
	lcd->current_aor.value = aor_value.value;

exit:
	return;
}

static void dsim_panel_set_elvss(struct lcd_info *lcd, u8 force)
{
	u8 *elvss = NULL;
	int ret = 0;
	union elvss_info elvss_value;
	unsigned char tset;

	elvss = lcd->elvss_table[lcd->bl][lcd->temperature_index];
	if (elvss == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get elvss value\n", __func__);
		goto exit;
	}

	tset = ((lcd->temperature < 0) ? BIT(7) : 0) | abs(lcd->temperature);
	elvss_value.mpscon = elvss[LDI_OFFSET_ELVSS_1];
	elvss_value.offset = elvss[LDI_OFFSET_ELVSS_2];
	elvss_value.offset_base = elvss[LDI_OFFSET_ELVSS_3];
	elvss_value.tset = tset;

	if (force)
		goto elvss_update;
	else if (lcd->current_elvss.value != elvss_value.value)
		goto elvss_update;
	else
		goto exit;

elvss_update:
	DSI_WRITE(elvss, ELVSS_CMD_CNT);
	lcd->current_elvss.value = elvss_value.value;
	dev_info(&lcd->ld->dev, "elvss: %x\n", lcd->current_elvss.value);

exit:
	return;
}

static int dsim_panel_set_acl(struct lcd_info *lcd, int force)
{
	int ret = 0, opr_status = OPR_STATUS_15P, acl_status = ACL_STATUS_ON;
	union acl_info acl_value;

	if (lcd->acl_enable)
		goto acl_update;

	opr_status = brightness_opr_table[lcd->adaptive_control][lcd->brightness];
	acl_status = !!opr_status;

	acl_value.enable = lcd->acl_table[acl_status][LDI_OFFSET_ACL];
	acl_value.percent = lcd->opr_table[opr_status][LDI_OFFSET_OPR_1];

	if (force)
		goto acl_update;
	else if (lcd->current_acl.value != acl_value.value)
		goto acl_update;
	else
		goto exit;

acl_update:
	DSI_WRITE(lcd->acl_table[acl_status], ACL_CMD_CNT);
	DSI_WRITE(lcd->opr_table[opr_status], OPR_CMD_CNT);
	lcd->current_acl.value = acl_value.value;
	dev_info(&lcd->ld->dev, "acl: %x, brightness: %d, adaptive_control: %d\n", lcd->current_acl.value, lcd->brightness, lcd->adaptive_control);

exit:
	return ret;
}

static int dsim_panel_set_tset(struct lcd_info *lcd, int force)
{
	int ret = 0;
	unsigned char tset = 0;

	tset = ((lcd->temperature < 0) ? BIT(7) : 0) | abs(lcd->temperature);

	if (force || lcd->tset_param[LDI_OFFSET_TSET] != tset) {
		lcd->tset_param[LDI_OFFSET_TSET] = tset;
		DSI_WRITE(lcd->tset_param, TSET_CMD_CNT);
		dev_info(&lcd->ld->dev, "temperature: %d, tset: %d\n", lcd->temperature, tset);
	}
exit:
	return ret;
}

static int dsim_panel_set_hbm(struct lcd_info *lcd, int force)
{
	int ret = 0, level = LEVEL_IS_HBM(lcd->brightness);

	if (force || lcd->current_hbm != lcd->hbm_table[level][LDI_OFFSET_HBM]) {
		lcd->current_hbm = lcd->hbm_table[level][LDI_OFFSET_HBM];
		DSI_WRITE(lcd->hbm_table[level], HBM_CMD_CNT);
		dev_info(&lcd->ld->dev, "hbm: %d, brightness: %d\n", lcd->current_hbm, lcd->brightness);
	}

exit:
	return ret;
}

static int low_level_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;

	DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	DSI_WRITE(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

	dsim_panel_gamma_ctrl(lcd, force);

	dsim_panel_aid_ctrl(lcd, force);

	dsim_panel_set_elvss(lcd, force);

	dsim_panel_set_acl(lcd, force);

	dsim_panel_set_hbm(lcd, force);

	DSI_WRITE(SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));

	dsim_panel_set_tset(lcd, force);

	DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	DSI_WRITE(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));

exit:
	return 0;
}

static int get_backlight_level_from_brightness(int brightness)
{
	return brightness_table[brightness];
}
#endif

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	lcd->bl = get_backlight_level_from_brightness(lcd->brightness);

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel state is %d\n", __func__, lcd->state);
		goto exit;
	}

	ret = low_level_set_brightness(lcd, force);
	if (ret)
		dev_err(&lcd->ld->dev, "%s: failed to set brightness : %d\n", __func__, index_brightness_table[lcd->bl]);

	lcd->current_bl = lcd->bl;

	dev_info(&lcd->ld->dev, "brightness: %d, bl: %d, nit: %d, lx: %d\n", lcd->brightness, lcd->bl, index_brightness_table[lcd->bl], lcd->lux);
exit:
	mutex_unlock(&lcd->lock);

	return ret;
}

static int panel_get_brightness(struct backlight_device *bd)
{
	struct lcd_info *lcd = bl_get_data(bd);

	return index_brightness_table[lcd->bl];
}

static int panel_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	struct lcd_info *lcd = bl_get_data(bd);

	if (lcd->state == PANEL_STATE_RESUMED) {
		ret = dsim_panel_set_brightness(lcd, 0);
		if (ret) {
			dev_err(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);
			goto exit;
		}
	}

exit:
	return ret;
}

static const struct backlight_ops panel_backlight_ops = {
	.get_brightness = panel_get_brightness,
	.update_status = panel_set_brightness,
};


#ifdef CONFIG_PANEL_AID_DIMMING
static void init_dynamic_aid(struct lcd_info *lcd)
{
	lcd->daid.vreg = VREG_OUT_X1000;
	lcd->daid.iv_tbl = index_voltage_table;
	lcd->daid.iv_max = IV_MAX;
	lcd->daid.mtp = kzalloc(IV_MAX * CI_MAX * sizeof(int), GFP_KERNEL);
	lcd->daid.gamma_default = gamma_default;
	lcd->daid.formular = gamma_formula;
	lcd->daid.vt_voltage_value = vt_voltage_value;

	lcd->daid.ibr_tbl = index_brightness_table;
	lcd->daid.ibr_max = IBRIGHTNESS_MAX;

	lcd->daid.offset_color = (const struct rgb_t(*)[])offset_color;
	lcd->daid.iv_ref = index_voltage_reference;
	lcd->daid.m_gray = m_gray;
}

/* V255(msb is seperated) ~ VT -> VT ~ V255(msb is not seperated) and signed bit */
static void reorder_reg2mtp(u8 *reg, int *mtp)
{
	int j, c, v;

	for (c = 0, j = 0; c < CI_MAX; c++, j++) {
		if (reg[j++] & 0x01)
			mtp[(IV_MAX-1)*CI_MAX+c] = reg[j] * (-1);
		else
			mtp[(IV_MAX-1)*CI_MAX+c] = reg[j];
	}

	for (v = IV_MAX - 2; v >= 0; v--) {
		for (c = 0; c < CI_MAX; c++, j++) {
			if (reg[j] & 0x80)
				mtp[CI_MAX*v+c] = (reg[j] & 0x7F) * (-1);
			else
				mtp[CI_MAX*v+c] = reg[j];
		}
	}
}

/* VT ~ V255(msb is not seperated) -> V255(msb is seperated) ~ VT */
/* array idx zero (reg[0]) is reserved for gamma command address (0xCA) */
static void reorder_gamma2reg(int *gamma, u8 *reg)
{
	int j, c, v;
	int *pgamma;

	v = IV_MAX - 1;
	pgamma = &gamma[v * CI_MAX];
	for (c = 0, j = 1; c < CI_MAX; c++, pgamma++) {
		if (*pgamma & 0x100)
			reg[j++] = 1;
		else
			reg[j++] = 0;

		reg[j++] = *pgamma & 0xff;
	}

	for (v = IV_MAX - 2; v > IV_VT; v--) {
		pgamma = &gamma[v * CI_MAX];
		for (c = 0; c < CI_MAX; c++, pgamma++)
			reg[j++] = *pgamma;
	}

	v = IV_VT;
	pgamma = &gamma[v * CI_MAX];
	reg[j++] = pgamma[CI_RED] << 4 | pgamma[CI_GREEN];
	reg[j++] = pgamma[CI_BLUE];
}

static void init_mtp_data(struct lcd_info *lcd, u8 *mtp_data)
{
	int i, c;
	int *mtp = lcd->daid.mtp;
	u8 tmp[IV_MAX * CI_MAX + CI_MAX] = {0, };

	memcpy(tmp, mtp_data, LDI_LEN_MTP);

	reorder_reg2mtp(tmp, mtp);

	smtd_dbg("MTP_Offset_Value\n");
	for (i = 0; i < IV_MAX; i++) {
		for (c = 0; c < CI_MAX; c++)
			smtd_dbg("%4d ", mtp[i*CI_MAX+c]);
		smtd_dbg("\n");
	}
}

static int init_gamma(struct lcd_info *lcd)
{
	int i, j;
	int ret = 0;
	int **gamma;

	/* allocate memory for local gamma table */
	gamma = kzalloc(IBRIGHTNESS_MAX * sizeof(int *), GFP_KERNEL);
	if (!gamma) {
		pr_err("failed to allocate gamma table\n");
		ret = -ENOMEM;
		goto err_alloc_gamma_table;
	}

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		gamma[i] = kzalloc(IV_MAX*CI_MAX * sizeof(int), GFP_KERNEL);
		if (!gamma[i]) {
			pr_err("failed to allocate gamma\n");
			ret = -ENOMEM;
			goto err_alloc_gamma;
		}
	}

	/* pre-allocate memory for gamma table */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		memcpy(&lcd->gamma_table[i], SEQ_GAMMA_CONDITION_SET, GAMMA_CMD_CNT);

	/* calculate gamma table */
	init_mtp_data(lcd, lcd->mtp);
	dynamic_aid(lcd->daid, gamma);

	/* relocate gamma order */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		reorder_gamma2reg(gamma[i], lcd->gamma_table[i]);

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		smtd_dbg("Gamma [%3d] = ", lcd->daid.ibr_tbl[i]);
		for (j = 0; j < GAMMA_CMD_CNT; j++)
			smtd_dbg("%4d ", lcd->gamma_table[i][j]);
		smtd_dbg("\n");
	}

	/* free local gamma table */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		kfree(gamma[i]);
	kfree(gamma);

	return 0;

err_alloc_gamma:
	while (i > 0) {
		kfree(gamma[i-1]);
		i--;
	}
	kfree(gamma);
err_alloc_gamma_table:
	return ret;
}
#endif

static int ea8061s_read_info(struct lcd_info *lcd, u8 reg, u32 len, u8 *buf)
{
	int ret = 0, i;

	ret = dsim_read_hl_data(lcd, reg, len, buf);

	if (ret != len) {
		dev_err(&lcd->ld->dev, "%s: fail: %02xh\n", __func__, reg);
		ret = -EINVAL;
		goto exit;
	}

	smtd_dbg("%s: %02xh\n", __func__, reg);
	for (i = 0; i < len; i++)
		smtd_dbg("%02dth value is %02x, %3d\n", i + 1, buf[i], buf[i]);

exit:
	return ret;
}

static int ea8061s_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	ret = ea8061s_read_info(lcd, LDI_REG_ID, LDI_LEN_ID, lcd->id_info.id);
	if (ret < 0) {
		priv->lcdConnected = 0;
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

static int ea8061s_read_mtp(struct lcd_info *lcd)
{
	int ret = 0;

	ret = ea8061s_read_info(lcd, LDI_REG_MTP, LDI_LEN_MTP, lcd->mtp);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

static int ea8061s_read_coordinate(struct lcd_info *lcd)
{
	int ret = 0;
	unsigned char buf[LDI_LEN_COORDINATE + LDI_LEN_DATE] = {0,};

	ret = ea8061s_read_info(lcd, LDI_REG_COORDINATE, ARRAY_SIZE(buf), buf);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

	lcd->coordinate[0] = buf[0] << 8 | buf[1];	/* X */
	lcd->coordinate[1] = buf[2] << 8 | buf[3];	/* Y */

	memcpy(lcd->date, &buf[LDI_LEN_COORDINATE], LDI_LEN_DATE);

exit:
	return ret;
}

static int ea8061s_read_elvss(struct lcd_info *lcd, unsigned char *buf)
{
	int ret = 0;

	ret = ea8061s_read_info(lcd, LDI_REG_ELVSS, LDI_LEN_ELVSS, buf);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

static int ea8061s_init_elvss(struct lcd_info *lcd, u8 *elvss_data)
{
	int i, temp, ret = 0;

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		for (temp = 0; temp < TEMP_MAX; temp++) {
			/* Duplicate with reading value from DDI */
			memcpy(&lcd->elvss_table[i][temp][1], elvss_data, LDI_LEN_ELVSS);

			lcd->elvss_table[i][temp][0] = elvss_mpscon_offset_data[i][0];
			lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_1] = elvss_mpscon_offset_data[i][LDI_OFFSET_ELVSS_1];
			lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_2] = elvss_mpscon_offset_data[i][LDI_OFFSET_ELVSS_2];
			if (elvss_otp_data[i].nit && elvss_otp_data[i].not_otp[temp] != -1)
				lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_3] = elvss_otp_data[i].not_otp[temp];
		}
	}

	return ret;
}

static int ea8061s_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	/* 2. Display Off (28h) */
	DSI_WRITE(SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));

	/* 3. Wait 20ms */
	usleep_range(20000, 21000);

	/* 4. Sleep In (10h) */
	DSI_WRITE(SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));

	/* 5. Wait 100ms */
	msleep(100);

exit:
	return ret;
}

static int ea8061s_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	/* 14. Display On(29h) */
	DSI_WRITE(SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));

exit:
	return ret;
}

static int ea8061s_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);
	usleep_range(5000, 6000);

	/* Test Key Enable */
	DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	DSI_WRITE(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

	/* common setting */
	DSI_WRITE(SEQ_HSYNC_GEN_ON, ARRAY_SIZE(SEQ_HSYNC_GEN_ON));
	DSI_WRITE(SEQ_SOURCE_SLEW, ARRAY_SIZE(SEQ_SOURCE_SLEW));
	DSI_WRITE(SEQ_AID_SET, ARRAY_SIZE(SEQ_AID_SET));
	DSI_WRITE(SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	DSI_WRITE(SEQ_S_WIRE, ARRAY_SIZE(SEQ_S_WIRE));

	DSI_WRITE(SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	msleep(20);

	DSI_WRITE(SEQ_GAMMA_CONDITION_SET, ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET));
	DSI_WRITE(SEQ_AID_SET, ARRAY_SIZE(SEQ_AID_SET));
	DSI_WRITE(SEQ_ELVSS_SET, ARRAY_SIZE(SEQ_ELVSS_SET));
	DSI_WRITE(SEQ_ACL_OFF, ARRAY_SIZE(SEQ_ACL_OFF));
	DSI_WRITE(SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	DSI_WRITE(SEQ_TSET, ARRAY_SIZE(SEQ_TSET));
	msleep(120);

	DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	DSI_WRITE(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));

exit:
	return ret;

}

static int ea8061s_read_init_info(struct lcd_info *lcd)
{
	int ret = 0;
	unsigned char elvss_data[LDI_LEN_ELVSS] = {0,};

	ret |= ea8061s_read_id(lcd);

	DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));

	ret |= ea8061s_read_mtp(lcd);
	ret |= ea8061s_read_coordinate(lcd);
	ret |= ea8061s_read_elvss(lcd, elvss_data);
	ret |= ea8061s_init_elvss(lcd, elvss_data);
	DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));

exit:
	return ret;
}

#ifdef CONFIG_DISPLAY_USE_INFO
static int panel_dpui_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct lcd_info *lcd = NULL;
	struct dpui_info *dpui = data;
	char tbuf[MAX_DPUI_VAL_LEN];
	int size;

	if (dpui == NULL) {
		pr_err("%s: dpui is null\n", __func__);
		return 0;
	}

	lcd = container_of(self, struct lcd_info, dpui_notif);

	size = snprintf(tbuf, MAX_DPUI_VAL_LEN, "%04d%02d%02d %02d%02d%02d",
			((lcd->date[0] & 0xF0) >> 4) + 2011, lcd->date[0] & 0xF, lcd->date[1] & 0x1F,
			lcd->date[2] & 0x1F, lcd->date[3] & 0x3F, lcd->date[4] & 0x3F);
	set_dpui_field(DPUI_KEY_MAID_DATE, tbuf, size);

	size = snprintf(tbuf, MAX_DPUI_VAL_LEN, "%d", lcd->id_info.id[0]);
	set_dpui_field(DPUI_KEY_LCDID1, tbuf, size);
	size = snprintf(tbuf, MAX_DPUI_VAL_LEN, "%d", lcd->id_info.id[1]);
	set_dpui_field(DPUI_KEY_LCDID2, tbuf, size);
	size = snprintf(tbuf, MAX_DPUI_VAL_LEN, "%d", lcd->id_info.id[2]);
	set_dpui_field(DPUI_KEY_LCDID3, tbuf, size);
	size = snprintf(tbuf, MAX_DPUI_VAL_LEN, "%s_%s", DPUI_VENDOR_NAME, DPUI_MODEL_NAME);
	set_dpui_field(DPUI_KEY_DISP_MODEL, tbuf, size);

	size = snprintf(tbuf, MAX_DPUI_VAL_LEN, "0x%02X%02X%02X%02X%02X",
			lcd->code[0], lcd->code[1], lcd->code[2], lcd->code[3], lcd->code[4]);
	set_dpui_field(DPUI_KEY_CHIPID, tbuf, size);

	return 0;
}
#endif /* CONFIG_DISPLAY_USE_INFO */

static int fb_notifier_callback(struct notifier_block *self,
			unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	struct lcd_info *lcd = NULL;
	int fb_blank;

	switch (event) {
	case FB_EVENT_BLANK:
	case FB_EARLY_EVENT_BLANK:
		break;
	default:
		return NOTIFY_DONE;
	}

	lcd = container_of(self, struct lcd_info, fb_notifier);

	fb_blank = *(int *)evdata->data;

	dev_info(&lcd->ld->dev, "%s: %02lx, %d\n", __func__, event, fb_blank);

	if (evdata->info->node != 0)
		return 0;

	if (event == FB_EARLY_EVENT_BLANK && fb_blank == FB_BLANK_POWERDOWN)
		pinctrl_enable(lcd, 0);
	else if (event == FB_EVENT_BLANK && fb_blank == FB_BLANK_POWERDOWN)
		pinctrl_enable(lcd, 0);
	else if (event == FB_EVENT_BLANK && fb_blank == FB_BLANK_UNBLANK) {
		pinctrl_enable(lcd, 1);
		ea8061s_displayon(lcd);
	}

	return 0;
}

static int ea8061s_register_notifier(struct lcd_info *lcd)
{
	int ret = 0;
	struct device_node *np;
	struct platform_device *pdev;

#ifdef CONFIG_DISPLAY_USE_INFO
	lcd->dpui_notif.notifier_call = panel_dpui_notifier_callback;
	ret = dpui_logging_register(&lcd->dpui_notif, DPUI_TYPE_PANEL);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to register dpui notifier callback\n", __func__);
		goto exit;
	}
#endif

	np = of_find_node_with_property(NULL, "lcd_info");
	np = of_parse_phandle(np, "lcd_info", 0);
	pdev = of_platform_device_create(np, NULL, lcd->dsim->dev);

	lcd->pins = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(lcd->pins)) {
		dev_info(&lcd->ld->dev, "%s: devm_pinctrl_get\n", __func__);
		goto exit;
	}

	lcd->pins_state[0] = pinctrl_lookup_state(lcd->pins, "off");
	lcd->pins_state[1] = pinctrl_lookup_state(lcd->pins, "on");
	if (IS_ERR_OR_NULL(lcd->pins_state[0]) || IS_ERR_OR_NULL(lcd->pins_state[1])) {
		dev_info(&lcd->ld->dev, "%s: pinctrl_lookup_state\n", __func__);
		goto exit;
	}

	lcd->fb_notifier.notifier_call = fb_notifier_callback;
	decon_register_notifier(&lcd->fb_notifier);

	dev_info(&lcd->ld->dev, "%s: done\n", __func__);

exit:
	return ret;
}

static int ea8061s_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;

	dev_info(&lcd->ld->dev, "%s: was called\n", __func__);

	priv->lcdConnected = 1;

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->dsim = dsim;
	lcd->state = PANEL_STATE_RESUMED;

	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->siop_enable = 0;
	lcd->adaptive_control = ACL_STATUS_ON;
	lcd->lux = -1;

	lcd->acl_table = ACL_TABLE;
	lcd->opr_table = OPR_TABLE;
	lcd->aor_table = AOR_TABLE;
	lcd->hbm_table = HBM_TABLE;
	lcd->tset_param = SEQ_TSET;

	ret = ea8061s_read_init_info(lcd);
	if (!priv->lcdConnected) {
		dev_err(&lcd->ld->dev, "%s: lcd was not connected\n", __func__);
		goto exit;
	}

#ifdef CONFIG_PANEL_AID_DIMMING
	init_dynamic_aid(lcd);
	init_gamma(lcd);
#endif

	dsim_panel_set_brightness(lcd, 1);

	dev_info(&lcd->ld->dev, "%s: done\n", __func__);
exit:
	return ret;
}
static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "SDC_%02X%02X%02X\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%x %x %x\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static ssize_t brightness_table_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i, bl;
	char *pos = buf;

	for (i = 0; i <= EXTEND_BRIGHTNESS; i++) {
		bl = get_backlight_level_from_brightness(i);
		pos += sprintf(pos, "%3d %3d\n", i, index_brightness_table[bl]);
	}

	return pos - buf;
}

static ssize_t temperature_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[] = "-15, -14, 0, 1\n";

	strcat(buf, temp);
	return strlen(buf);
}

static ssize_t temperature_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value, rc, temperature_index = 0;

	rc = kstrtoint(buf, 0, &value);

	if (rc < 0)
		return rc;
	else {
		switch (value) {
		case 1:
			temperature_index = TEMP_ABOVE_MINUS_00_DEGREE;
			break;
		case 0:
		case -14:
			temperature_index = TEMP_ABOVE_MINUS_15_DEGREE;
			break;
		case -15:
			temperature_index = TEMP_BELOW_MINUS_15_DEGREE;
			break;
		default:
			dev_info(dev, "%s: %d is invalid\n", __func__, value);
			return -EINVAL;
		}

		mutex_lock(&lcd->lock);
		lcd->temperature = value;
		lcd->temperature_index = temperature_index;
		mutex_unlock(&lcd->lock);

		if (lcd->state == PANEL_STATE_RESUMED)
			dsim_panel_set_brightness(lcd, 1);

		dev_info(dev, "%s: %d, %d, %d\n", __func__, value, lcd->temperature, lcd->temperature_index);
	}

	return size;
}

static ssize_t color_coordinate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%u, %u\n", lcd->coordinate[0], lcd->coordinate[1]);

	return strlen(buf);
}

static ssize_t manufacture_date_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	u16 year;
	u8 month, day, hour, min, sec;
	u16 ms;

	year = ((lcd->date[0] & 0xF0) >> 4) + 2011;
	month = lcd->date[0] & 0xF;
	day = lcd->date[1] & 0x1F;
	hour = lcd->date[2] & 0x1F;
	min = lcd->date[3] & 0x3F;
	sec = lcd->date[4];
	ms = (lcd->date[5] << 8) | lcd->date[6];

	sprintf(buf, "%04d, %02d, %02d, %02d:%02d:%02d.%04d\n", year, month, day, hour, min, sec, ms);

	return strlen(buf);
}

static ssize_t cell_id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		lcd->date[0], lcd->date[1], lcd->date[2], lcd->date[3], lcd->date[4],
		lcd->date[5], lcd->date[6], (lcd->coordinate[0] & 0xFF00) >> 8, lcd->coordinate[0] & 0x00FF,
		(lcd->coordinate[1] & 0xFF00) >> 8, lcd->coordinate[1] & 0x00FF);

	return strlen(buf);
}

#ifdef CONFIG_PANEL_AID_DIMMING
static void show_aid_log(struct lcd_info *lcd)
{
	u8 temp[256];
	int i, j, k;
	int *mtp;

	mtp = lcd->daid.mtp;
	for (i = 0, j = 0; i < IV_MAX; i++, j += CI_MAX) {
		if (i == 0)
			dev_info(&lcd->ld->dev, "MTP Offset VT   : %4d %4d %4d\n",
				mtp[j + CI_RED], mtp[j + CI_GREEN], mtp[j + CI_BLUE]);
		else
			dev_info(&lcd->ld->dev, "MTP Offset V%3d : %4d %4d %4d\n",
				lcd->daid.iv_tbl[i], mtp[j + CI_RED], mtp[j + CI_GREEN], mtp[j + CI_BLUE]);
	}

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		memset(temp, 0, sizeof(temp));
		for (j = 1; j < GAMMA_CMD_CNT; j++) {
			if (j == 1 || j == 3 || j == 5)
				k = lcd->gamma_table[i][j++] * 256;
			else
				k = 0;
			snprintf(temp + strnlen(temp, 256), 256, " %3d", lcd->gamma_table[i][j] + k);
		}

		dev_info(&lcd->ld->dev, "nit : %3d  %s\n", lcd->daid.ibr_tbl[i], temp);
	}

	dev_info(&lcd->ld->dev, "%s\n", __func__);
}

static ssize_t aid_log_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	show_aid_log(lcd);

	return strlen(buf);
}
#endif

static ssize_t dump_register_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	char *pos = buf;
	u8 reg, len, offset;
	int ret, i;
	u8 *dump = NULL;

	reg = lcd->dump_info[0];
	len = lcd->dump_info[1];
	offset = lcd->dump_info[2];

	if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
		goto exit;

	dump = kcalloc(len, sizeof(u8), GFP_KERNEL);

	if (lcd->state == PANEL_STATE_RESUMED) {
		DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
		DSI_WRITE(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

		if (offset)
			ret = dsim_read_hl_data_offset(lcd, reg, len, dump, offset);
		else
			ret = dsim_read_hl_data(lcd, reg, len, dump);

		if (ret < 0) {
			dev_err(&lcd->ld->dev, "%s: failed to read, %x, %d, %d\n", __func__, reg, len, offset);
			goto exit;
		}

		DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
		DSI_WRITE(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));
	}

	pos += sprintf(pos, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		pos += sprintf(pos, "%2d: %02x\n", i + offset + 1, dump[i]);
	pos += sprintf(pos, "- [%02X]\n", reg);

	dev_info(&lcd->ld->dev, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		dev_info(&lcd->ld->dev, "%2d: %02x\n", i + offset + 1, dump[i]);
	dev_info(&lcd->ld->dev, "- [%02X]\n", reg);

	kfree(dump);
exit:
	return pos - buf;
}

static ssize_t dump_register_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	unsigned int reg, len, offset;
	int ret;

	ret = sscanf(buf, "%8x %8d %8d", &reg, &len, &offset);

	if (ret == 2)
		offset = 0;

	dev_info(dev, "%s: %x %d %d\n", __func__, reg, len, offset);

	if (ret < 0)
		return ret;
	else {
		if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
			return -EINVAL;

		lcd->dump_info[0] = reg;
		lcd->dump_info[1] = len;
		lcd->dump_info[2] = offset;
	}

	return size;
}

static ssize_t adaptive_control_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%d\n", lcd->adaptive_control);

	return strlen(buf);
}

static ssize_t adaptive_control_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int rc, value;
	struct lcd_info *lcd = dev_get_drvdata(dev);

	rc = kstrtouint(buf, 0, &value);

	if (rc < 0)
		return rc;

	if (value < ACL_STATUS_MAX && lcd->adaptive_control != value) {
		dev_info(&lcd->ld->dev, "%s: %d, %d\n", __func__, lcd->adaptive_control, value);
		mutex_lock(&lcd->lock);
		lcd->adaptive_control = value;
		mutex_unlock(&lcd->lock);
		if (lcd->state == PANEL_STATE_RESUMED)
			dsim_panel_set_brightness(lcd, 1);
	}

	return size;
}

static ssize_t lux_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%d\n", lcd->lux);

	return strlen(buf);
}

static ssize_t lux_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = kstrtoint(buf, 0, &value);

	if (rc < 0)
		return rc;

	if (lcd->lux != value) {
		mutex_lock(&lcd->lock);
		lcd->lux = value;
		mutex_unlock(&lcd->lock);

		attr_store_for_each(lcd->mdnie_class, attr->attr.name, buf, size);
	}

	return size;
}
#ifdef CONFIG_DISPLAY_USE_INFO
/*
 * HW PARAM LOGGING SYSFS NODE
 */
static ssize_t dpui_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	update_dpui_log(DPUI_LOG_LEVEL_INFO);
	get_dpui_log(buf, DPUI_LOG_LEVEL_INFO);

	dev_info(dev, "%s: %s\n", __func__, buf);

	return strlen(buf);
}

/*
 * [DEV ONLY]
 * HW PARAM LOGGING SYSFS NODE
 */
static ssize_t dpui_dbg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	update_dpui_log(DPUI_LOG_LEVEL_DEBUG);
	get_dpui_log(buf, DPUI_LOG_LEVEL_DEBUG);

	dev_info(dev, "%s: %s\n", __func__, buf);

	return strlen(buf);
}

static DEVICE_ATTR(dpui, 0440, dpui_show, NULL);
static DEVICE_ATTR(dpui_dbg, 0440, dpui_dbg_show, NULL);
#endif

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(cell_id, 0444, cell_id_show, NULL);
static DEVICE_ATTR(brightness_table, 0444, brightness_table_show, NULL);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);
static DEVICE_ATTR(color_coordinate, 0444, color_coordinate_show, NULL);
static DEVICE_ATTR(manufacture_date, 0444, manufacture_date_show, NULL);
static DEVICE_ATTR(aid_log, 0444, aid_log_show, NULL);
static DEVICE_ATTR(dump_register, 0644, dump_register_show, dump_register_store);
static DEVICE_ATTR(adaptive_control, 0664, adaptive_control_show, adaptive_control_store);
static DEVICE_ATTR(lux, 0644, lux_show, lux_store);
static DEVICE_ATTR(SVC_OCTA, 0444, cell_id_show, NULL);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_cell_id.attr,
	&dev_attr_temperature.attr,
	&dev_attr_color_coordinate.attr,
	&dev_attr_manufacture_date.attr,
	&dev_attr_aid_log.attr,
	&dev_attr_dump_register.attr,
	&dev_attr_brightness_table.attr,
	&dev_attr_adaptive_control.attr,
	&dev_attr_lux.attr,
#ifdef CONFIG_DISPLAY_USE_INFO
	&dev_attr_dpui.attr,
	&dev_attr_dpui_dbg.attr,
#endif
	NULL,
};

static const struct attribute_group lcd_sysfs_attr_group = {
	.attrs = lcd_sysfs_attributes,
};

static void lcd_init_svc(struct lcd_info *lcd)
{
	struct device *dev = &lcd->svc_dev;
	struct kobject *top_kobj = &lcd->ld->dev.kobj.kset->kobj;
	struct kernfs_node *kn = kernfs_find_and_get(top_kobj->sd, "svc");
	struct kobject *svc_kobj = NULL;
	char *buf, *path = NULL;
	int ret = 0;

	svc_kobj = kn ? kn->priv : kobject_create_and_add("svc", top_kobj);
	if (!svc_kobj)
		return;

	buf = kzalloc(PATH_MAX, GFP_KERNEL);
	if (buf) {
		path = kernfs_path(svc_kobj->sd, buf, PATH_MAX);
		dev_info(&lcd->ld->dev, "%s: %s %s\n", __func__, path, !kn ? "create" : "");
		kfree(buf);
	}

	dev->kobj.parent = svc_kobj;
	dev_set_name(dev, "OCTA");
	dev_set_drvdata(dev, lcd);
	ret = device_register(dev);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: device_register fail\n", __func__);
		return;
	}

	device_create_file(dev, &dev_attr_SVC_OCTA);

	if (kn)
		kernfs_put(kn);
}

static void lcd_init_sysfs(struct lcd_info *lcd)
{
	int ret = 0;

	ret = sysfs_create_group(&lcd->ld->dev.kobj, &lcd_sysfs_attr_group);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add lcd sysfs\n");

	lcd_init_svc(lcd);
}


#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
int mdnie_lite_send_seq(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel state is %d\n", __func__, lcd->state);
		ret = -EIO;
		goto exit;
	}

	ret = dsim_write_set(lcd, seq, num);

exit:
	mutex_unlock(&lcd->lock);

	return ret;
}

int mdnie_lite_read(struct lcd_info *lcd, u8 addr, u8 *buf, u32 size)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel state is %d\n", __func__, lcd->state);
		ret = -EIO;
		goto exit;
	}

	ret = dsim_read_hl_data(lcd, addr, size, buf);

exit:
	mutex_unlock(&lcd->lock);

	return ret;
}
#endif


static int dsim_panel_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct lcd_info *lcd;

	dsim->priv.par = lcd = kzalloc(sizeof(struct lcd_info), GFP_KERNEL);
	if (!lcd) {
		pr_err("%s: failed to allocate for lcd\n", __func__);
		ret = -ENOMEM;
		goto probe_err;
	}

	dsim->lcd = lcd->ld = lcd_device_register("panel", dsim->dev, lcd, NULL);
	if (IS_ERR(lcd->ld)) {
		pr_err("%s: failed to register lcd device\n", __func__);
		ret = PTR_ERR(lcd->ld);
		goto probe_err;
	}

	lcd->bd = backlight_device_register("panel", dsim->dev, lcd, &panel_backlight_ops, NULL);
	if (IS_ERR(lcd->bd)) {
		pr_err("%s: failed to register backlight device\n", __func__);
		ret = PTR_ERR(lcd->bd);
		goto probe_err;
	}

	mutex_init(&lcd->lock);

	ret = ea8061s_probe(dsim);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);
		goto probe_err;
	}

	lcd_init_sysfs(lcd);

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
	mdnie_register(&lcd->ld->dev, lcd, (mdnie_w)mdnie_lite_send_seq, (mdnie_r)mdnie_lite_read, lcd->coordinate, &tune_info);
	lcd->mdnie_class = get_mdnie_class();
#endif

	ea8061s_register_notifier(lcd);

	dev_info(&lcd->ld->dev, "%s: %s: done\n", kbasename(__FILE__), __func__);
probe_err:
	return ret;
}

static int dsim_panel_displayon(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s: %d\n", __func__, lcd->state);

	if (lcd->state == PANEL_STATE_SUSPENED) {
		ret = ea8061s_init(lcd);
		if (ret) {
			dev_info(&lcd->ld->dev, "%s: failed to panel init, %d\n", __func__, ret);
			goto displayon_err;
		}
		dsim_panel_set_brightness(lcd, 1);
		ret = ea8061s_displayon(lcd);
	}

displayon_err:
	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_RESUMED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "-%s: %d\n", __func__, priv->lcdConnected);

	return ret;
}

static int dsim_panel_suspend(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s: %d\n", __func__, lcd->state);

	if (lcd->state == PANEL_STATE_SUSPENED)
		goto suspend_err;

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENDING;
	mutex_unlock(&lcd->lock);

	ret = ea8061s_exit(lcd);
	if (ret) {
		dev_info(&lcd->ld->dev, "%s: failed to panel exit, %d\n", __func__, ret);
		goto suspend_err;
	}

suspend_err:
	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "-%s: %d\n", __func__, priv->lcdConnected);

	return ret;
}

struct mipi_dsim_lcd_driver ea8061s_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

