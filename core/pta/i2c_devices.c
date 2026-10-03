// SPDX-License-Identifier: BSD-2-Clause
/*
 * PTA exposing the I2C devices of the secure device tree to user TAs.
 */
#include <drivers/at24.h>
#include <drivers/lm75.h>
#include <drivers/mcp23008.h>
#include <drivers/mcp7940x.h>
#include <drivers/pcf8591.h>
#include <kernel/pseudo_ta.h>
#include <kernel/ts_manager.h>
#include <kernel/user_ta.h>
#include <pta_i2c_devices.h>
#include <string.h>

#define PTA_NAME "i2c_devices.pta"

#define PT_VAL_IN	TEE_PARAM_TYPE_VALUE_INPUT
#define PT_VAL_OUT	TEE_PARAM_TYPE_VALUE_OUTPUT
#define PT_MEM_IN	TEE_PARAM_TYPE_MEMREF_INPUT
#define PT_MEM_OUT	TEE_PARAM_TYPE_MEMREF_OUTPUT
#define PT_NONE		TEE_PARAM_TYPE_NONE

#define PT_IN_IN	TEE_PARAM_TYPES(PT_VAL_IN, PT_VAL_IN, PT_NONE, PT_NONE)
#define PT_IN_OUT	TEE_PARAM_TYPES(PT_VAL_IN, PT_VAL_OUT, PT_NONE, PT_NONE)
#define PT_IN_MEMIN	TEE_PARAM_TYPES(PT_VAL_IN, PT_MEM_IN, PT_NONE, PT_NONE)
#define PT_IN_MEMOUT	TEE_PARAM_TYPES(PT_VAL_IN, PT_MEM_OUT, PT_NONE, PT_NONE)

static TEE_Result cmd_count(TEE_Param p[TEE_NUM_PARAMS])
{
	switch (p[0].value.a) {
	case PTA_I2C_DEVICES_CLASS_TEMP:
		p[1].value.a = lm75_count();
		break;
	case PTA_I2C_DEVICES_CLASS_GPIO:
		p[1].value.a = mcp23008_count();
		break;
	case PTA_I2C_DEVICES_CLASS_RTC:
		p[1].value.a = mcp7940x_count();
		break;
	case PTA_I2C_DEVICES_CLASS_EEPROM:
		p[1].value.a = at24_count();
		break;
	case PTA_I2C_DEVICES_CLASS_ADC:
		p[1].value.a = pcf8591_count();
		break;
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}

	return TEE_SUCCESS;
}

static TEE_Result cmd_temp_get(TEE_Param p[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_ERROR_GENERIC;
	int32_t mc = 0;

	res = lm75_read_temp(p[0].value.a, &mc);
	if (!res)
		p[1].value.a = (uint32_t)mc;

	return res;
}

static TEE_Result cmd_gpio_config(TEE_Param p[TEE_NUM_PARAMS])
{
	uint32_t flags = p[1].value.a;
	TEE_Result res = TEE_ERROR_GENERIC;

	res = mcp23008_set_pullup(p[0].value.a, p[0].value.b,
				  flags & PTA_I2C_DEVICES_GPIO_PULLUP);
	if (res)
		return res;

	return mcp23008_set_direction(p[0].value.a, p[0].value.b,
				      flags & PTA_I2C_DEVICES_GPIO_OUTPUT);
}

static TEE_Result cmd_gpio_get(TEE_Param p[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_ERROR_GENERIC;
	bool high = false;

	res = mcp23008_get_value(p[0].value.a, p[0].value.b, &high);
	if (!res)
		p[1].value.a = high;

	return res;
}

static TEE_Result cmd_rtc_get(TEE_Param p[TEE_NUM_PARAMS])
{
	struct pta_i2c_devices_time out = { };
	struct mcp7940x_time tm = { };
	TEE_Result res = TEE_ERROR_GENERIC;

	if (p[1].memref.size < sizeof(out)) {
		p[1].memref.size = sizeof(out);
		return TEE_ERROR_SHORT_BUFFER;
	}

	res = mcp7940x_get_time(p[0].value.a, &tm);
	if (res)
		return res;

	out = (struct pta_i2c_devices_time){
		.year = tm.year, .mon = tm.mon, .mday = tm.mday,
		.wday = tm.wday, .hour = tm.hour, .min = tm.min,
		.sec = tm.sec,
	};
	memcpy(p[1].memref.buffer, &out, sizeof(out));
	p[1].memref.size = sizeof(out);

	return TEE_SUCCESS;
}

static TEE_Result cmd_rtc_set(TEE_Param p[TEE_NUM_PARAMS])
{
	struct pta_i2c_devices_time in = { };
	struct mcp7940x_time tm = { };

	if (p[1].memref.size != sizeof(in))
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(&in, p[1].memref.buffer, sizeof(in));
	tm = (struct mcp7940x_time){
		.year = in.year, .mon = in.mon, .mday = in.mday,
		.wday = in.wday, .hour = in.hour, .min = in.min,
		.sec = in.sec,
	};

	return mcp7940x_set_time(p[0].value.a, &tm);
}

static TEE_Result cmd_eeprom_size(TEE_Param p[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_ERROR_GENERIC;
	size_t size = 0;

	res = at24_get_size(p[0].value.a, &size);
	if (!res)
		p[1].value.a = size;

	return res;
}

static TEE_Result cmd_adc_read(TEE_Param p[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t val = 0;

	res = pcf8591_read_adc(p[0].value.a, p[0].value.b, &val);
	if (!res)
		p[1].value.a = val;

	return res;
}

static TEE_Result cmd_dac_write(TEE_Param p[TEE_NUM_PARAMS])
{
	if (p[1].value.a > UINT8_MAX)
		return TEE_ERROR_BAD_PARAMETERS;

	return pcf8591_write_dac(p[0].value.a, p[1].value.a, p[1].value.b);
}

static TEE_Result open_session(uint32_t pt __unused,
			       TEE_Param p[TEE_NUM_PARAMS] __unused,
			       void **sess __unused)
{
	struct ts_session *s = ts_get_calling_session();

	/* Normal world clients must go through a TA */
	if (!s || !is_user_ta_ctx(s->ctx))
		return TEE_ERROR_ACCESS_DENIED;

	return TEE_SUCCESS;
}

static TEE_Result invoke_command(void *sess __unused, uint32_t cmd,
				 uint32_t pt, TEE_Param p[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = 0;

	switch (cmd) {
	case PTA_I2C_DEVICES_CMD_COUNT:
	case PTA_I2C_DEVICES_CMD_TEMP_GET:
	case PTA_I2C_DEVICES_CMD_GPIO_GET:
	case PTA_I2C_DEVICES_CMD_EEPROM_SIZE:
	case PTA_I2C_DEVICES_CMD_ADC_READ:
		exp_pt = PT_IN_OUT;
		break;
	case PTA_I2C_DEVICES_CMD_GPIO_CONFIG:
	case PTA_I2C_DEVICES_CMD_GPIO_SET:
	case PTA_I2C_DEVICES_CMD_DAC_WRITE:
		exp_pt = PT_IN_IN;
		break;
	case PTA_I2C_DEVICES_CMD_RTC_GET:
	case PTA_I2C_DEVICES_CMD_EEPROM_READ:
		exp_pt = PT_IN_MEMOUT;
		break;
	case PTA_I2C_DEVICES_CMD_RTC_SET:
	case PTA_I2C_DEVICES_CMD_EEPROM_WRITE:
		exp_pt = PT_IN_MEMIN;
		break;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	switch (cmd) {
	case PTA_I2C_DEVICES_CMD_COUNT:
		return cmd_count(p);
	case PTA_I2C_DEVICES_CMD_TEMP_GET:
		return cmd_temp_get(p);
	case PTA_I2C_DEVICES_CMD_GPIO_CONFIG:
		return cmd_gpio_config(p);
	case PTA_I2C_DEVICES_CMD_GPIO_SET:
		return mcp23008_set_value(p[0].value.a, p[0].value.b,
					  p[1].value.a);
	case PTA_I2C_DEVICES_CMD_GPIO_GET:
		return cmd_gpio_get(p);
	case PTA_I2C_DEVICES_CMD_RTC_GET:
		return cmd_rtc_get(p);
	case PTA_I2C_DEVICES_CMD_RTC_SET:
		return cmd_rtc_set(p);
	case PTA_I2C_DEVICES_CMD_EEPROM_SIZE:
		return cmd_eeprom_size(p);
	case PTA_I2C_DEVICES_CMD_EEPROM_READ:
		return at24_read(p[0].value.a, p[0].value.b,
				 p[1].memref.buffer, p[1].memref.size);
	case PTA_I2C_DEVICES_CMD_EEPROM_WRITE:
		return at24_write(p[0].value.a, p[0].value.b,
				  p[1].memref.buffer, p[1].memref.size);
	case PTA_I2C_DEVICES_CMD_ADC_READ:
		return cmd_adc_read(p);
	case PTA_I2C_DEVICES_CMD_DAC_WRITE:
		return cmd_dac_write(p);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

pseudo_ta_register(.uuid = PTA_I2C_DEVICES_UUID, .name = PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS,
		   .open_session_entry_point = open_session,
		   .invoke_command_entry_point = invoke_command);
