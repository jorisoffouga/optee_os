/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * LM75 family I2C temperature sensors, probed from the secure DT.
 * Instances are numbered 0..n-1 in DT order.
 */
#ifndef __DRIVERS_LM75_H
#define __DRIVERS_LM75_H

#include <compiler.h>
#include <stdint.h>
#include <tee_api_types.h>

#ifdef CFG_LM75
unsigned int lm75_count(void);
TEE_Result lm75_read_temp(unsigned int idx, int32_t *milli_celsius);
#else
static inline unsigned int lm75_count(void)
{
	return 0;
}

static inline TEE_Result lm75_read_temp(unsigned int idx __unused,
					int32_t *milli_celsius __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

#endif /* __DRIVERS_LM75_H */
