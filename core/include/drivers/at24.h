/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AT24 family I2C EEPROMs (24c01 .. 24c512), probed from the secure DT.
 * Instances are numbered 0..n-1 in DT order.
 */
#ifndef __DRIVERS_AT24_H
#define __DRIVERS_AT24_H

#include <compiler.h>
#include <stddef.h>
#include <stdint.h>
#include <tee_api_types.h>

#ifdef CFG_AT24
unsigned int at24_count(void);
TEE_Result at24_get_size(unsigned int idx, size_t *size);
TEE_Result at24_read(unsigned int idx, size_t offset, uint8_t *buf,
		     size_t len);
TEE_Result at24_write(unsigned int idx, size_t offset, const uint8_t *buf,
		      size_t len);
#else
static inline unsigned int at24_count(void)
{
	return 0;
}

static inline TEE_Result at24_get_size(unsigned int idx __unused,
				       size_t *size __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result at24_read(unsigned int idx __unused,
				   size_t offset __unused,
				   uint8_t *buf __unused, size_t len __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result at24_write(unsigned int idx __unused,
				    size_t offset __unused,
				    const uint8_t *buf __unused,
				    size_t len __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

#endif /* __DRIVERS_AT24_H */
