#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#define DEVICE_NAME "Adafruit Seesaw"
#define DEVICE_I2C_ADDRESS 0x50
#define I2C_AVAILABLE_BUS 1

static struct i2c_adapter *adapter = NULL;
static struct i2c_client *seesaw = NULL;
static const struct i2c_device_id seesaw_id[] = { { DEVICE_NAME, 0 }, {} };
MODULE_DEVICE_TABLE(i2c, seesaw_id);

static struct i2c_board_info seesaw_info = { I2C_BOARD_INFO(
	DEVICE_NAME, DEVICE_I2C_ADDRESS) };

static unsigned char read(struct i2c_client *client)
{
	unsigned char ret;
	i2c_master_recv(client, &ret, 1);
	return ret;
}

// Called once the device has been found on the i2c adapter
static int seesaw_probe(struct i2c_client *client)
{
	// Software reset the registers
	unsigned char buf[] = { 0x00, 0x7F, 0xFF };
	i2c_master_send(client, buf, 3);
	mdelay(10);

	// Get hardware ID
	unsigned char buf2[] = { 0x00, 0x01 };
	i2c_master_send(client, buf2, 2);
	pr_info("Read HWID: %02x\n", read(client));
	mdelay(10);

	return 0;
}

static void seesaw_remove(struct i2c_client *client)
{
	pr_info("Device removed!");
}

static struct i2c_driver seesaw_driver = {
	.driver = {
	.name = DEVICE_NAME,
	.owner = THIS_MODULE,
},
// For testing with the Raspberry Pi on kernel v6.1
#ifndef _ARCH_X86_TLBBATCH_H
	.probe_new	= seesaw_probe,
#else
	.probe		= seesaw_probe,
#endif
	.remove = seesaw_remove,
	.id_table = seesaw_id,
};

static int __init seesaw_driver_init(void)
{
	pr_info("Module Started\n");
	adapter = i2c_get_adapter(I2C_AVAILABLE_BUS);
	seesaw = i2c_new_client_device(adapter, &seesaw_info);
	i2c_add_driver(&seesaw_driver);
	return 0;
}

static void __exit seesaw_driver_exit(void)
{
	i2c_unregister_device(seesaw);
	i2c_del_driver(&seesaw_driver);

	pr_info("Module Exitted!\n");
}

module_init(seesaw_driver_init);
module_exit(seesaw_driver_exit);
MODULE_LICENSE("GPL v2");