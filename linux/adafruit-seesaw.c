#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/bits.h>

#define SEESAW_DEVICE_NAME "seesaw_gamepad"

// Base registers
#define SEESAW_STATUS_BASE 0
#define SEESAW_GPIO_BASE 1
#define SEESAW_ADC_BASE 9

// GPIO module function address registers
#define SEESAW_GPIO_DIRCLR_BULK 3
#define SEESAW_GPIO_BULK 4
#define SEESAW_GPIO_BULK_SET 5
#define SEESAW_GPIO_PULLENSET 11

// Status module function address registers
#define SEESAW_STATUS_HW_ID 1
#define SEESAW_STATUS_SWRST 127

// ADC module function address registers
#define SEESAW_ADC_OFFSET 7

// Gamepad buttons to GPIO pin map
#define BUTTON_A 5
#define BUTTON_B 1
#define BUTTON_X 6
#define BUTTON_Y 2
#define BUTTON_START 16
#define BUTTON_SELECT 0

// Bit-mask for getting the values for GPIO pins
u32 BUTTON_MASK = (1UL << BUTTON_A) | (1UL << BUTTON_B) | (1UL << BUTTON_X) |
		  (1UL << BUTTON_Y) | (1UL << BUTTON_START) |
		  (1UL << BUTTON_SELECT);

// Gamepad Analog Stick pin map
#define ANALOG_X 14
#define ANALOG_Y 15

#define SEESAW_JOYSTICK_MAX_AXIS 1023
#define SEESAW_JOYSTICK_FUZZ 2
#define SEESAW_JOYSTICK_FLAT 4

#define SEESAW_GAMEPAD_POLL_INTERVAL 16
#define SEESAW_GAMEPAD_POLL_MIN 8
#define SEESAW_GAMEPAD_POLL_MAX 32

#define SEESAW_I2C_ADDRESS 0x50

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
	// Read Buttons
	unsigned char buf[2] = { SEESAW_GPIO_BASE, SEESAW_GPIO_BULK };
	err = i2c_master_send(client, buf, 2);
	if (err < 0)
		return err;
	if (err != sizeof(buf))
		return -EIO;
	unsigned char read_buf[4];
	err = i2c_master_recv(client, read_buf, 4);
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

	buf[0] = SEESAW_ADC_BASE;
	buf[1] = SEESAW_ADC_OFFSET + ANALOG_X;
	// Read Analog Stick X
	{
		err = i2c_master_send(client, buf, 2);
		if (err < 0)
			return err;
		if (err != sizeof(buf))
			return -EIO;
		err = i2c_master_recv(client, (char*)&data->x, 2);
		if (err < 0)
			return err;
		if (err != 2)
			return -EIO;
		data->x = SEESAW_JOYSTICK_MAX_AXIS  - be16_to_cpu(data->x);
	}
	buf[1] = SEESAW_ADC_OFFSET + ANALOG_Y;
	// Read Analog Stick Y
	{
		err = i2c_master_send(client, buf, 2);
		if (err < 0)
			return err;
		if (err != sizeof(buf))
			return -EIO;
		err = i2c_master_recv(client, (char*)&data->y, 2);
		if (err < 0) {
			return err;
		}
		if (err != 2) {
			return -EIO;
		}
		data->y = SEESAW_JOYSTICK_MAX_AXIS - be16_to_cpu(data->y);
	}
	return 0;
}

static void seesaw_poll(struct input_dev *input)
{
	struct seesaw_gamepad *private = input_get_drvdata(input);
	struct seesaw_data data;
	int err;
	err = seesaw_read_data(private->i2c_client, &data);
	if (err != 0) {
		return;
	}

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

// Called once the device has been found on the i2c adapter
static int seesaw_probe(struct i2c_client *client)
{
	struct seesaw_gamepad *private;
	int err;

	if (client->addr != SEESAW_I2C_ADDRESS)
		return -EIO;
	// Software reset the registers
	{
		unsigned char buf[] = { SEESAW_STATUS_BASE, SEESAW_STATUS_SWRST,
					0xFF };
		err = i2c_master_send(client, buf, 3);
		if (err < 0)
			return err;
		if (err != sizeof(buf))
			return -EIO;
	}

	private = devm_kzalloc(&client->dev, sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	// Get hardware ID
	{
		unsigned char buf[] = { SEESAW_STATUS_BASE,
					SEESAW_STATUS_HW_ID };
		err = i2c_master_send(client, buf, 2);
		i2c_master_recv(client, &private->hardware_id, 1);
	}

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
	input_set_capability(private->input_dev, EV_KEY, BTN_X);
	input_set_capability(private->input_dev, EV_KEY, BTN_Y);
	input_set_capability(private->input_dev, EV_KEY, BTN_A);
	input_set_capability(private->input_dev, EV_KEY, BTN_B);
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

	// Set Pin Mode to PULLUP
	{
		unsigned char buf[] = { SEESAW_GPIO_BASE,
					SEESAW_GPIO_DIRCLR_BULK,
					(unsigned char)(BUTTON_MASK >> 24),
					(unsigned char)(BUTTON_MASK >> 16),
					(unsigned char)(BUTTON_MASK >> 8),
					(unsigned char)BUTTON_MASK };
		i2c_master_send(client, buf, 6);
		buf[1] = SEESAW_GPIO_PULLENSET;
		i2c_master_send(client, buf, 6);
		buf[1] = SEESAW_GPIO_BULK_SET;
		i2c_master_send(client, buf, 6);
	}

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

static const struct i2c_device_id seesaw_id_table[] = { { SEESAW_DEVICE_NAME,
							  0 },
							{ /* Sentinel */ } };
MODULE_DEVICE_TABLE(i2c, seesaw_id_table);

static struct i2c_driver seesaw_driver = {
	.driver = {
		.name = SEESAW_DEVICE_NAME,
		.of_match_table = of_match_ptr(of_seesaw_match),
		.owner = THIS_MODULE,
	},
	.id_table = seesaw_id_table,
// For testing with the Raspberry Pi on kernel v6.1
#ifndef _ARCH_X86_TLBBATCH_H
	.probe_new	= seesaw_probe,
#else
	.probe		= seesaw_probe,
#endif
};
module_i2c_driver(seesaw_driver);

MODULE_LICENSE("GPL v2");