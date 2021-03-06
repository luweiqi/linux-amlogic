/*
 * drivers/amlogic/media/vout/backlight/bl_extern/i2c_lp8556.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/amlogic/i2c-amlogic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/amlogic/media/vout/lcd/aml_bl_extern.h>
#include <linux/amlogic/media/vout/lcd/aml_bl.h>
#include "bl_extern.h"


#define BL_EXTERN_INDEX			0
#define BL_EXTERN_NAME			"i2c_lp8556"
#define BL_EXTERN_TYPE			BL_EXTERN_I2C

#define BL_EXTERN_CMD_SIZE        4

static unsigned int bl_status;
static struct i2c_client *i2c_lp8556_client;

static unsigned char init_on_table[] = {
	0x00, 0xa1, 0x76, 0x00,//hight bit(8~11)(0~0X66e set backlight)
	0x00, 0xa0, 0x66, 0x00,//low bit(0~7)  20mA
	0x00, 0x16, 0x1F, 0x00,// 5channel LED enable 0x1F
	0x00, 0xa9, 0xA0, 0x00,//VBOOST_MAX 25V
	0x00, 0x9e, 0x12, 0x00,
	0x00, 0xa2, 0x23, 0x00,
	/*0x03 pwm+I2c set brightness,0x5 I2c set brightness*/
	0x00, 0x01, 0x05, 0x00,
	0xff, 0x00, 0x00, 0x00, //ending
};

static unsigned char init_off_table[] = {
	0xff, 0x00, 0x00, 0x00, //ending
};

static int i2c_lp8556_write(struct i2c_client *i2client,
		unsigned char *buff, unsigned int len)
{
	int ret = 0;
	struct i2c_msg msg[] = {
		{
			.addr = i2client->addr,
			.flags = 0,
			.len = len,
			.buf = buff,
		}
	};

	ret = i2c_transfer(i2client->adapter, msg, 1);
	if (ret < 0)
		BLEXERR("i2c write failed [addr 0x%02x]\n", i2client->addr);

	return ret;
}

static int i2c_lp8556_power_cmd(unsigned char *init_table)
{
	int i = 0, len;
	int ret = 0;
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();

	len = BL_EXTERN_CMD_SIZE;
	while (i <= BL_EXTERN_INIT_TABLE_MAX) {
		if (init_table[i] == BL_EXTERN_INIT_END) {
			break;
		} else if (init_table[i] == BL_EXTERN_INIT_NONE) {
			/* do nothing, only for delay */
		} else if (init_table[i] == BL_EXTERN_INIT_CMD) {
			ret = i2c_lp8556_write(i2c_lp8556_client,
				&init_table[i+1], (len-2));
		} else {
			BLEXERR("%s(%d: %s): power_type %d is invalid\n",
				__func__, bl_extern->config.index,
				bl_extern->config.name, bl_extern->config.type);
		}
		if (init_table[i+len-1] > 0)
			mdelay(init_table[i+len-1]);
		i += len;
	}

	return ret;
}

static int i2c_lp8556_power_ctrl(int flag)
{
	int ret = 0;
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();

	if (flag)
		ret = i2c_lp8556_power_cmd(init_on_table);
	else
		ret = i2c_lp8556_power_cmd(init_off_table);

	BLEX("%s(%d: %s): %d\n",
		__func__, bl_extern->config.index,
		bl_extern->config.name, flag);
	return ret;
}

static int i2c_lp8556_power_on(void)
{
	int ret;

	bl_status = 1;
	ret = i2c_lp8556_power_ctrl(1);

	return ret;

}

static int i2c_lp8556_power_off(void)
{
	int ret;

	bl_status = 0;
	ret = i2c_lp8556_power_ctrl(0);
	return ret;

}

static int i2c_lp8556_set_level(unsigned int level)
{
	unsigned char tData[3];
	int ret = 0;
	struct aml_bl_drv_s *bl_drv = aml_bl_get_driver();
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();
	unsigned int level_max, level_min;
	unsigned int dim_max, dim_min;

	if (bl_drv == NULL)
		return -1;
	level_max = bl_drv->bconf->level_max;
	level_min = bl_drv->bconf->level_min;
	dim_max = bl_extern->config.dim_max;
	dim_min = bl_extern->config.dim_min;
	level = dim_min - ((level - level_min) * (dim_min - dim_max)) /
			(level_max - level_min);
	level &= 0xff;

	if (bl_status) {
		tData[0] = 0x0;
		tData[1] = level;
		ret = i2c_lp8556_write(i2c_lp8556_client, tData, 2);
	}
	return ret;
}

static int i2c_lp8556_update(void)
{
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();

	if (bl_extern == NULL) {
		BLEXERR("%s driver is null\n", BL_EXTERN_NAME);
		return -1;
	}

	if (bl_extern->config.type == BL_EXTERN_MAX) {
		bl_extern->config.index = BL_EXTERN_INDEX;
		bl_extern->config.type = BL_EXTERN_TYPE;
		strcpy(bl_extern->config.name, BL_EXTERN_NAME);
	}

	bl_extern->device_power_on = i2c_lp8556_power_on;
	bl_extern->device_power_off = i2c_lp8556_power_off;
	bl_extern->device_bri_update = i2c_lp8556_set_level;

	return 0;
}

static int bl_extern_i2c_lp8556_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		BLEXERR("%s: functionality check failed\n", __func__);
	else
		i2c_lp8556_client = client;

	BLEX("%s OK\n", __func__);
	return 0;
}

static int bl_extern_i2c_lp8556_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id bl_extern_i2c_lp8556_id[] = {
	{BL_EXTERN_NAME, 0},
	{ }
};

static struct i2c_driver i2c_lp8556_driver = {
	.probe    = bl_extern_i2c_lp8556_probe,
	.remove   = bl_extern_i2c_lp8556_remove,
	.id_table = bl_extern_i2c_lp8556_id,
	.driver = {
		.name = BL_EXTERN_NAME,
		.owner = THIS_MODULE,
	},
};

int i2c_lp8556_probe(void)
{
	int ret = 0;
	struct i2c_board_info i2c_info;
	struct i2c_adapter *adapter;
	struct i2c_client *i2c_client;
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();

	memset(&i2c_info, 0, sizeof(i2c_info));
	adapter = i2c_get_adapter(bl_extern->config.i2c_bus);
	if (!adapter) {
		BLEXERR("%s failed to get i2c adapter\n",
			bl_extern->config.name);
		return -1;
	}

	strncpy(i2c_info.type, bl_extern->config.name, I2C_NAME_SIZE);
	i2c_info.addr = bl_extern->config.i2c_addr;
	i2c_info.platform_data = &bl_extern->config;
	i2c_info.flags = 0;
	if (i2c_info.addr > 0x7f) {
		BLEXERR("%s invalid i2c address: 0x%02x\n",
			bl_extern->config.name, bl_extern->config.i2c_addr);
		return -1;
	}
	i2c_client = i2c_new_device(adapter, &i2c_info);
	if (!i2c_client) {
		BLEXERR("%s failed to new i2c device\n",
			bl_extern->config.name);
	} else {
		if (lcd_debug_print_flag) {
			BLEX("%s new i2c device succeed\n",
				bl_extern->config.name);
		}
	}

	if (!i2c_lp8556_client) {
		ret = i2c_add_driver(&i2c_lp8556_driver);
		if (ret) {
			BLEXERR("%s add i2c_driver failed\n",
				bl_extern->config.name);
			return -1;
		}
	}

	ret = i2c_lp8556_update();

	BLEX("%s: %d\n", __func__, ret);
	return ret;
}

int i2c_lp8556_remove(void)
{
	return 0;
}

