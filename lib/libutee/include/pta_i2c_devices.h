/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * PTA giving Trusted Applications access to the I2C devices described in
 * the OP-TEE secure device tree. Only user TAs may open a session.
 *
 * Devices of a class are numbered 0..n-1 in device tree order; the index
 * is always passed in params[0].value.a.
 */
#ifndef __PTA_I2C_DEVICES_H
#define __PTA_I2C_DEVICES_H

#include <stdint.h>

#define PTA_I2C_DEVICES_UUID { 0xae57d410, 0xedd6, 0x4097, \
	{ 0x8f, 0xfc, 0xbb, 0x27, 0xac, 0x19, 0x0f, 0x20 } }

/* Device classes for PTA_I2C_DEVICES_CMD_COUNT */
#define PTA_I2C_DEVICES_CLASS_TEMP	0	/* LM75 */
#define PTA_I2C_DEVICES_CLASS_GPIO	1	/* MCP23008 */
#define PTA_I2C_DEVICES_CLASS_RTC	2	/* MCP7940x */
#define PTA_I2C_DEVICES_CLASS_EEPROM	3	/* AT24 */
#define PTA_I2C_DEVICES_CLASS_ADC	4	/* PCF8591 */

/* year 2000..2099, mon 0..11, mday 1..31, wday 0..6 (Sunday = 0) */
struct pta_i2c_devices_time {
	uint32_t year;
	uint32_t mon;
	uint32_t mday;
	uint32_t wday;
	uint32_t hour;
	uint32_t min;
	uint32_t sec;
};

/*
 * Number of probed devices of a class
 * [in]		value[0].a	PTA_I2C_DEVICES_CLASS_*
 * [out]	value[1].a	Count
 */
#define PTA_I2C_DEVICES_CMD_COUNT		0

/*
 * Temperature sensor: read temperature
 * [in]		value[0].a	Device index
 * [out]	value[1].a	Temperature in milli-degC (int32_t)
 */
#define PTA_I2C_DEVICES_CMD_TEMP_GET		1

/*
 * GPIO expander: configure a pin
 * [in]		value[0].a	Device index
 * [in]		value[0].b	Pin
 * [in]		value[1].a	PTA_I2C_DEVICES_GPIO_* flags
 */
#define PTA_I2C_DEVICES_CMD_GPIO_CONFIG		2
#define PTA_I2C_DEVICES_GPIO_OUTPUT		(1U << 0)
#define PTA_I2C_DEVICES_GPIO_PULLUP		(1U << 1)

/*
 * GPIO expander: drive an output pin
 * [in]		value[0].a	Device index
 * [in]		value[0].b	Pin
 * [in]		value[1].a	0 = low, else high
 */
#define PTA_I2C_DEVICES_CMD_GPIO_SET		3

/*
 * GPIO expander: read a pin level
 * [in]		value[0].a	Device index
 * [in]		value[0].b	Pin
 * [out]	value[1].a	0 = low, 1 = high
 */
#define PTA_I2C_DEVICES_CMD_GPIO_GET		4

/*
 * RTC: read time
 * [in]		value[0].a	Device index
 * [out]	memref[1]	struct pta_i2c_devices_time
 *
 * TEE_ERROR_BAD_STATE: clock never set since it lost power
 */
#define PTA_I2C_DEVICES_CMD_RTC_GET		5

/*
 * RTC: set time (also starts the oscillator)
 * [in]		value[0].a	Device index
 * [in]		memref[1]	struct pta_i2c_devices_time
 */
#define PTA_I2C_DEVICES_CMD_RTC_SET		6

/*
 * EEPROM: size in bytes
 * [in]		value[0].a	Device index
 * [out]	value[1].a	Size
 */
#define PTA_I2C_DEVICES_CMD_EEPROM_SIZE		7

/*
 * EEPROM: read
 * [in]		value[0].a	Device index
 * [in]		value[0].b	Offset
 * [out]	memref[1]	Data, size = number of bytes to read
 */
#define PTA_I2C_DEVICES_CMD_EEPROM_READ		8

/*
 * EEPROM: write
 * [in]		value[0].a	Device index
 * [in]		value[0].b	Offset
 * [in]		memref[1]	Data to write
 */
#define PTA_I2C_DEVICES_CMD_EEPROM_WRITE	9

/*
 * ADC: read a channel
 * [in]		value[0].a	Device index
 * [in]		value[0].b	Channel
 * [out]	value[1].a	Sample 0..255
 */
#define PTA_I2C_DEVICES_CMD_ADC_READ		10

/*
 * DAC: drive the output
 * [in]		value[0].a	Device index
 * [in]		value[1].a	Value 0..255
 * [in]		value[1].b	0 = disable output, else enable
 */
#define PTA_I2C_DEVICES_CMD_DAC_WRITE		11

#endif /* __PTA_I2C_DEVICES_H */
