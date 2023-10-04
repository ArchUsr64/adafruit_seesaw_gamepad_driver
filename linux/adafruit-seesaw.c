#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

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
#define BUTTON_X 6
#define BUTTON_Y 2
#define BUTTON_A 5
#define BUTTON_B 1
#define BUTTON_SELECT 0
#define BUTTON_START 16

// Gamepad Analog Stick pin map
#define ANALOG_X 14
#define ANALOG_Y 15

// Bit-mask for getting the values for GPIO pins
u32 BUTTON_MASK = (1UL << BUTTON_X) | (1UL << BUTTON_Y) |
		  (1UL << BUTTON_START) | (1UL << BUTTON_A) |
		  (1UL << BUTTON_B) | (1UL << BUTTON_SELECT);

#define SEESAW_I2C_ADDRESS 0x50

#define I2C_AVAILABLE_BUS 1

struct seesaw_data {
	__be16 x;
	__be16 y;
	u8 button_x, button_y, button_a, button_b, button_select, button_start;
};


static unsigned char read(struct i2c_client *client)
{
	unsigned char ret;
	i2c_master_recv(client, &ret, 1);
	return ret;
}

// Called once the device has been found on the i2c adapter
static int seesaw_probe(struct i2c_client *client)
{
	if (client->addr != SEESAW_I2C_ADDRESS) {
		pr_info("Invalid Address: %d\n", client->addr);
	}
	// Software reset the registers
	{
		unsigned char buf[] = { 0x00, 0x7F, 0xFF };
		i2c_master_send(client, buf, 3);
		mdelay(10);
	}

	// Get hardware ID
	{
		unsigned char buf[] = { 0x00, 0x01 };
		i2c_master_send(client, buf, 2);
		pr_info("Read HWID: %02x\n", read(client));
		mdelay(10);
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
		mdelay(10);
		buf[1] = SEESAW_GPIO_PULLENSET;
		i2c_master_send(client, buf, 6);
		mdelay(10);
		buf[1] = SEESAW_GPIO_BULK_SET;
		i2c_master_send(client, buf, 6);
		mdelay(10);
	}

	for (int i = 0; i < 100; i++) {
		// Read Buttons
		unsigned char buf[] = { SEESAW_GPIO_BASE, SEESAW_GPIO_BULK };
		i2c_master_send(client, buf, 2);
		mdelay(10);
		unsigned char read_buf[4];
		i2c_master_recv(client, read_buf, 4);
		u32 result = ((u32)read_buf[0] << 24) |
			     ((u32)read_buf[1] << 16) |
			     ((u32)read_buf[2] << 8) | (u32)read_buf[3];
		if (!(result & (1UL << BUTTON_A))) {
			pr_info("Pressed button A\n");
		}
		if (!(result & (1UL << BUTTON_B))) {
			pr_info("Pressed button B\n");
		}
		if (!(result & (1UL << BUTTON_X))) {
			pr_info("Pressed button X\n");
		}
		if (!(result & (1UL << BUTTON_Y))) {
			pr_info("Pressed button Y\n");
		}
		if (!(result & (1UL << BUTTON_SELECT))) {
			pr_info("Pressed button Select\n");
		}
		if (!(result & (1UL << BUTTON_START))) {
			pr_info("Pressed button Start\n");
		}
		mdelay(10);

		int x, y;
		// Read Analog Stick X
		{
			char buf[] = { SEESAW_ADC_BASE,
				       SEESAW_ADC_OFFSET + ANALOG_X };
			i2c_master_send(client, buf, 2);
			mdelay(10);
			// Potential Endianness issue here
			u16 read_value;
			// Device expects a big endian value
			i2c_master_recv(client, (char *)&read_value, 2);
			read_value = (read_value >> 8) | (read_value << 8);
			x = 1023 - read_value;
			mdelay(10);
		}
		// Read Analog Stick Y
		{
			char buf[] = { SEESAW_ADC_BASE,
				       SEESAW_ADC_OFFSET + ANALOG_Y };
			i2c_master_send(client, buf, 2);
			mdelay(10);
			// Potential Endianness issue here
			u16 read_value;
			i2c_master_recv(client, (char *)&read_value, 2);
			read_value = (read_value >> 8) | (read_value << 8);
			y = 1023 - read_value;
			mdelay(10);
		}
		printk("X: %d, Y: %d\n", x, y);
		mdelay(100);
	}
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id of_seesaw_match[] = {
	{
		.compatible = "adafruit,seesaw_gamepad",
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_seesaw_match);
#endif /* CONFIG_OF */

static const struct i2c_device_id seesaw_id_table[] = { { SEESAW_DEVICE_NAME,
							  0 },
							{} };
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