// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Anshul Dalal <anshulusr@gmail.com>
 *
 * Driver for Adafruit Mini I2C Gamepad
 *
 * Based on the work of:
 *	Oleh Kravchenko (Sparkfun Qwiic Joystick driver)
 *
 * Product page: https://www.adafruit.com/product/5743
 * Firmware and hardware sources: https://github.com/adafruit/Adafruit_Seesaw
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* clang-format off */
#define SEESAW_DEVICE_NAME	"seesaw_gamepad"

#define SEESAW_STATUS_BASE	0
#define SEESAW_GPIO_BASE	1
#define SEESAW_ADC_BASE		9

#define SEESAW_GPIO_DIRCLR_BULK	3
#define SEESAW_GPIO_BULK	4
#define SEESAW_GPIO_BULK_SET	5
#define SEESAW_GPIO_PULLENSET	11

#define SEESAW_STATUS_HW_ID	1
#define SEESAW_STATUS_SWRST	127

#define SEESAW_ADC_OFFSET	7

#define BUTTON_A	5
#define BUTTON_B	1
#define BUTTON_X	6
#define BUTTON_Y	2
#define BUTTON_START	16
#define BUTTON_SELECT	0

#define ANALOG_X	14
#define ANALOG_Y	15

#define SEESAW_JOYSTICK_MAX_AXIS	1023
#define SEESAW_JOYSTICK_FUZZ		2
#define SEESAW_JOYSTICK_FLAT		4

#define SEESAW_GAMEPAD_POLL_INTERVAL	16
#define SEESAW_GAMEPAD_POLL_MIN		8
#define SEESAW_GAMEPAD_POLL_MAX		32
/* clang-format on */

u32 BUTTON_MASK = (1UL << BUTTON_A) | (1UL << BUTTON_B) | (1UL << BUTTON_X) |
		  (1UL << BUTTON_Y) | (1UL << BUTTON_START) |
		  (1UL << BUTTON_SELECT);

struct seesaw_gamepad {
	char physical_path[32];
	unsigned char hardware_id;
	struct input_dev *input_dev;
	struct i2c_client *i2c_client;
};

struct seesaw_data {
	__be16 x;
	__be16 y;
	u8 button_a, button_b, button_x, button_y, button_start, button_select;
};

static int seesaw_read_data(struct i2c_client *client, struct seesaw_data *data)
{
	int err;
	unsigned char write_buf[2] = { SEESAW_GPIO_BASE, SEESAW_GPIO_BULK };
	unsigned char read_buf[4];

	err = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (err < 0)
		return err;
	if (err != sizeof(write_buf))
		return -EIO;
	err = i2c_master_recv(client, read_buf, sizeof(read_buf));
	if (err < 0)
		return err;
	if (err != sizeof(read_buf))
		return -EIO;
	u32 result = ((u32)read_buf[0] << 24) | ((u32)read_buf[1] << 16) |
		     ((u32)read_buf[2] << 8) | (u32)read_buf[3];
	data->button_a = !(result & (1UL << BUTTON_A));
	data->button_b = !(result & (1UL << BUTTON_B));
	data->button_x = !(result & (1UL << BUTTON_X));
	data->button_y = !(result & (1UL << BUTTON_Y));
	data->button_start = !(result & (1UL << BUTTON_START));
	data->button_select = !(result & (1UL << BUTTON_SELECT));


	write_buf[0] = SEESAW_ADC_BASE;
	write_buf[1] = SEESAW_ADC_OFFSET + ANALOG_X;
	err = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (err < 0)
		return err;
	if (err != sizeof(write_buf))
		return -EIO;
	err = i2c_master_recv(client, (char *)&data->x, sizeof(data->x));
	if (err < 0)
		return err;
	if (err != sizeof(data->x))
		return -EIO;
	/*
	 * ADC reads left as max and right as 0, must be reversed since kernel
	 * expects reports in opposite order.
	 */
	data->x = SEESAW_JOYSTICK_MAX_AXIS - be16_to_cpu(data->x);

	write_buf[1] = SEESAW_ADC_OFFSET + ANALOG_Y;
	err = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (err < 0)
		return err;
	if (err != sizeof(write_buf))
		return -EIO;
	err = i2c_master_recv(client, (char *)&data->y, sizeof(data->y));
	if (err < 0)
		return err;
	if (err != sizeof(data->y))
		return -EIO;
	data->y = be16_to_cpu(data->y);

	return 0;
}

static void seesaw_poll(struct input_dev *input)
{
	struct seesaw_gamepad *private = input_get_drvdata(input);
	struct seesaw_data data;
	int err;

	err = seesaw_read_data(private->i2c_client, &data);
	if (err != 0)
		return;

	input_report_abs(input, ABS_X, data.x);
	input_report_abs(input, ABS_Y, data.y);
	input_report_key(input, BTN_A, data.button_a);
	input_report_key(input, BTN_B, data.button_b);
	input_report_key(input, BTN_X, data.button_x);
	input_report_key(input, BTN_Y, data.button_y);
	input_report_key(input, BTN_START, data.button_start);
	input_report_key(input, BTN_SELECT, data.button_select);
	input_sync(input);
}

static int seesaw_probe(struct i2c_client *client)
{
	int err;
	struct seesaw_gamepad *private;
	unsigned char register_reset[] = { SEESAW_STATUS_BASE,
					   SEESAW_STATUS_SWRST, 0xFF };
	unsigned char get_hw_id[] = { SEESAW_STATUS_BASE, SEESAW_STATUS_HW_ID };

	err = i2c_master_send(client, register_reset, sizeof(register_reset));
	if (err < 0)
		return err;
	if (err != sizeof(register_reset))
		return -EIO;
	mdelay(10);

	private = devm_kzalloc(&client->dev, sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	err = i2c_master_send(client, get_hw_id, sizeof(get_hw_id));
	if (err < 0)
		return err;
	if (err != sizeof(get_hw_id))
		return -EIO;
	err = i2c_master_recv(client, &private->hardware_id, 1);
	if (err < 0)
		return err;
	if (err != 1)
		return -EIO;

	dev_dbg(&client->dev, "Adafruit Seesaw Gamepad, Hardware ID: %02x\n",
		private->hardware_id);

	private->i2c_client = client;
	scnprintf(private->physical_path, sizeof(private->physical_path),
		  "i2c/%s", dev_name(&client->dev));
	i2c_set_clientdata(client, private);

	private->input_dev = devm_input_allocate_device(&client->dev);
	if (!private->input_dev)
		return -ENOMEM;

	private->input_dev->id.bustype = BUS_I2C;
	private->input_dev->name = "Adafruit Seesaw Gamepad";
	private->input_dev->phys = private->physical_path;
	input_set_drvdata(private->input_dev, private);
	input_set_abs_params(private->input_dev, ABS_X, 0,
			     SEESAW_JOYSTICK_MAX_AXIS, SEESAW_JOYSTICK_FUZZ,
			     SEESAW_JOYSTICK_FLAT);
	input_set_abs_params(private->input_dev, ABS_Y, 0,
			     SEESAW_JOYSTICK_MAX_AXIS, SEESAW_JOYSTICK_FUZZ,
			     SEESAW_JOYSTICK_FLAT);
	input_set_capability(private->input_dev, EV_KEY, BTN_A);
	input_set_capability(private->input_dev, EV_KEY, BTN_B);
	input_set_capability(private->input_dev, EV_KEY, BTN_X);
	input_set_capability(private->input_dev, EV_KEY, BTN_Y);
	input_set_capability(private->input_dev, EV_KEY, BTN_START);
	input_set_capability(private->input_dev, EV_KEY, BTN_SELECT);

	err = input_setup_polling(private->input_dev, seesaw_poll);
	if (err) {
		dev_err(&client->dev, "failed to set up polling: %d\n", err);
		return err;
	}

	input_set_poll_interval(private->input_dev,
				SEESAW_GAMEPAD_POLL_INTERVAL);
	input_set_max_poll_interval(private->input_dev,
				    SEESAW_GAMEPAD_POLL_MAX);
	input_set_min_poll_interval(private->input_dev,
				    SEESAW_GAMEPAD_POLL_MIN);

	err = input_register_device(private->input_dev);
	if (err) {
		dev_err(&client->dev, "failed to register joystick: %d\n", err);
		return err;
	}

	/* Set Pin Mode to input and enable pull-up resistors */
	unsigned char pin_mode[] = { SEESAW_GPIO_BASE,	SEESAW_GPIO_DIRCLR_BULK,
				     BUTTON_MASK >> 24, BUTTON_MASK >> 16,
				     BUTTON_MASK >> 8,	BUTTON_MASK };
	err = i2c_master_send(client, pin_mode, sizeof(pin_mode));
	pin_mode[1] = SEESAW_GPIO_PULLENSET;
	err |= i2c_master_send(client, pin_mode, sizeof(pin_mode));
	pin_mode[1] = SEESAW_GPIO_BULK_SET;
	err |= i2c_master_send(client, pin_mode, sizeof(pin_mode));
	if (err < 0)
		return err;
	if (err != sizeof(pin_mode))
		return -EIO;

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id of_seesaw_match[] = {
	{
		.compatible = "adafruit,seesaw_gamepad",
	},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, of_seesaw_match);
#endif /* CONFIG_OF */

static const struct i2c_device_id seesaw_id_table[] = { { KBUILD_MODNAME, 0 },
							{ /* Sentinel */ } };
MODULE_DEVICE_TABLE(i2c, seesaw_id_table);

static struct i2c_driver seesaw_driver = {
	.driver = {
		.name = SEESAW_DEVICE_NAME,
		.of_match_table = of_match_ptr(of_seesaw_match),
	},
	.id_table = seesaw_id_table,
	.probe		= seesaw_probe,
};
module_i2c_driver(seesaw_driver);

MODULE_AUTHOR("Anshul Dalal <anshulusr@gmail.com>");
MODULE_DESCRIPTION("Adafruit Mini I2C Gamepad driver");
MODULE_LICENSE("GPL");