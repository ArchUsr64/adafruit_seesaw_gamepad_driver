#include <Wire.h>
enum {
	SEESAW_STATUS_BASE = 0x00,
	SEESAW_GPIO_BASE = 0x01,
	SEESAW_ADC_BASE = 0x09,
};

// GPIO module function address registers
enum {
	SEESAW_GPIO_DIRCLR_BULK = 0x03,
	SEESAW_GPIO_BULK = 0x04,
	SEESAW_GPIO_BULK_SET = 0x05,
	SEESAW_GPIO_PULLENSET = 0x0B,
};

// status module function address registers
enum {
	SEESAW_STATUS_HW_ID = 0x01,
	SEESAW_STATUS_SWRST = 0x7F,
};

// ADC module function address registers
enum {
	SEESAW_ADC_CHANNEL_OFFSET = 0x07,
};

#define SEESAW_I2C_ADDRESS 0x50

#define BUTTON_X 6
#define BUTTON_Y 2
#define BUTTON_A 5
#define BUTTON_B 1
#define BUTTON_SELECT 0
#define BUTTON_START 16
uint32_t button_mask = (1UL << BUTTON_X) | (1UL << BUTTON_Y) |
		       (1UL << BUTTON_START) | (1UL << BUTTON_A) |
		       (1UL << BUTTON_B) | (1UL << BUTTON_SELECT);

bool write(uint8_t *buffer, uint8_t size)
{
	Wire.beginTransmission(SEESAW_I2C_ADDRESS);
	Wire.write(buffer, size);
	return Wire.endTransmission() == 0;
};

void read(uint8_t *buffer, uint8_t size)
{
	Wire.requestFrom(SEESAW_I2C_ADDRESS, size);
	for (int i = 0; i < size; i++)
		buffer[i] = Wire.read();
}

void set_pin_mode_input_pullup(uint32_t pins)
{
	uint8_t buffer[6] = { SEESAW_GPIO_BASE,	     SEESAW_GPIO_DIRCLR_BULK,
			      (uint8_t)(pins >> 24), (uint8_t)(pins >> 16),
			      (uint8_t)(pins >> 8),  (uint8_t)pins };
	write(buffer, 6);
	buffer[1] = SEESAW_GPIO_PULLENSET;
	write(buffer, 6);
	buffer[1] = SEESAW_GPIO_BULK_SET;
	write(buffer, 6);
}

void setup()
{
	Wire.begin();
	Serial.begin(9600);

	while (!Serial) {
		delay(10);
	}

	Serial.println("Gamepad QT example!");

	// Software reset the registers
	{
		uint8_t buffer[] = { SEESAW_STATUS_BASE, SEESAW_STATUS_SWRST,
				     0xFF };
		write(buffer, 3);
		delay(10);
	}

	// Get hardware id
	{
		uint8_t buffer[] = { SEESAW_STATUS_BASE, SEESAW_STATUS_HW_ID };
		write(buffer, 2);
		uint8_t hwid;
		read(&hwid, 1);
		Serial.print("Hardware ID received: 0x");
		Serial.println(hwid, HEX);
		delay(10);
	}

	set_pin_mode_input_pullup(button_mask);
}

int last_x = 0, last_y = 0;

uint16_t analog_read(uint8_t pin)
{
	uint8_t buffer[] = { SEESAW_ADC_BASE, SEESAW_ADC_CHANNEL_OFFSET + pin };
	write(buffer, 2);
	uint16_t read_value;
	read((uint8_t *)&read_value, 2);
	read_value = (read_value >> 8) | (read_value << 8);
	delay(1);
	return read_value;
}

uint32_t digital_read_bulk(uint32_t pins)
{
	uint8_t write_buffer[] = { SEESAW_GPIO_BASE, SEESAW_GPIO_BULK };
	write(write_buffer, 2);
	delay(1);
	uint8_t buffer[4];
	read(buffer, 4);
	uint32_t ret = ((uint32_t)buffer[0] << 24) |
		       ((uint32_t)buffer[1] << 16) |
		       ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
	return ret & pins;
}

void loop()
{
	delay(10);

	// Reverse x/y values to match joystick orientation
	int x = 1023 - analog_read(14);
	int y = 1023 - analog_read(15);

	if ((abs(x - last_x) > 0) || (abs(y - last_y) > 0)) {
		Serial.print("x: ");
		Serial.print(x);
		Serial.print(", ");
		Serial.print("y: ");
		Serial.println(y);
		last_x = x;
		last_y = y;
	}

	uint32_t buttons = digital_read_bulk(button_mask);

	if (!(buttons & (1UL << BUTTON_A))) {
		Serial.println("Button A pressed");
	}
	if (!(buttons & (1UL << BUTTON_B))) {
		Serial.println("Button B pressed");
	}
	if (!(buttons & (1UL << BUTTON_Y))) {
		Serial.println("Button Y pressed");
	}
	if (!(buttons & (1UL << BUTTON_X))) {
		Serial.println("Button X pressed");
	}
	if (!(buttons & (1UL << BUTTON_SELECT))) {
		Serial.println("Button SELECT pressed");
	}
	if (!(buttons & (1UL << BUTTON_START))) {
		Serial.println("Button START pressed");
	}
}
