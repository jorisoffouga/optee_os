/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Microchip MCP23008 8-bit I2C GPIO expander, probed from the secure DT.
 * Instances are numbered 0..n-1 in DT order. With CFG_DRIVERS_GPIO each
 * instance is also a GPIO provider for other secure DT nodes.
 */
#ifndef __DRIVERS_MCP23008_H
#define __DRIVERS_MCP23008_H

#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <tee_api_types.h>

#define MCP23008_NUM_PINS	8

#ifdef CFG_MCP23008
unsigned int mcp23008_count(void);
TEE_Result mcp23008_set_direction(unsigned int idx, unsigned int pin,
				  bool output);
TEE_Result mcp23008_set_pullup(unsigned int idx, unsigned int pin,
			       bool enable);
TEE_Result mcp23008_set_value(unsigned int idx, unsigned int pin, bool high);
TEE_Result mcp23008_get_value(unsigned int idx, unsigned int pin, bool *high);
#else
static inline unsigned int mcp23008_count(void)
{
	return 0;
}

static inline TEE_Result mcp23008_set_direction(unsigned int idx __unused,
						 unsigned int pin __unused,
						 bool output __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result mcp23008_set_pullup(unsigned int idx __unused,
					      unsigned int pin __unused,
					      bool enable __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result mcp23008_set_value(unsigned int idx __unused,
					     unsigned int pin __unused,
					     bool high __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result mcp23008_get_value(unsigned int idx __unused,
					     unsigned int pin __unused,
					     bool *high __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

#endif /* __DRIVERS_MCP23008_H */
