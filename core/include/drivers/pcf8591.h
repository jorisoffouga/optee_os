/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * NXP PCF8591 I2C 4-channel 8-bit ADC and 8-bit DAC, probed from the
 * secure DT. Instances are numbered 0..n-1 in DT order.
 */
#ifndef __DRIVERS_PCF8591_H
#define __DRIVERS_PCF8591_H

#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <tee_api_types.h>

#define PCF8591_NUM_CHANNELS	4

#ifdef CFG_PCF8591
unsigned int pcf8591_count(void);
TEE_Result pcf8591_read_adc(unsigned int idx, unsigned int channel,
			    uint8_t *val);
TEE_Result pcf8591_write_dac(unsigned int idx, uint8_t val, bool enable);
#else
static inline unsigned int pcf8591_count(void)
{
	return 0;
}

static inline TEE_Result pcf8591_read_adc(unsigned int idx __unused,
					  unsigned int channel __unused,
					  uint8_t *val __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result pcf8591_write_dac(unsigned int idx __unused,
					   uint8_t val __unused,
					   bool enable __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

#endif /* __DRIVERS_PCF8591_H */
