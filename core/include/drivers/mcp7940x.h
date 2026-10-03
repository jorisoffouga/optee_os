/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Microchip MCP7940x/MCP7941x I2C real-time clock, probed from the secure
 * DT. Instances are numbered 0..n-1 in DT order. With
 * CFG_MCP7940X_SYSTEM_RTC the first instance is registered as the OP-TEE
 * system RTC.
 */
#ifndef __DRIVERS_MCP7940X_H
#define __DRIVERS_MCP7940X_H

#include <compiler.h>
#include <stdint.h>
#include <tee_api_types.h>

/* year 2000..2099, mon 0..11, mday 1..31, wday 0..6 (Sunday = 0) */
struct mcp7940x_time {
	uint32_t year;
	uint32_t mon;
	uint32_t mday;
	uint32_t wday;
	uint32_t hour;
	uint32_t min;
	uint32_t sec;
};

#ifdef CFG_MCP7940X
unsigned int mcp7940x_count(void);
TEE_Result mcp7940x_get_time(unsigned int idx, struct mcp7940x_time *tm);
TEE_Result mcp7940x_set_time(unsigned int idx,
			     const struct mcp7940x_time *tm);
#else
static inline unsigned int mcp7940x_count(void)
{
	return 0;
}

static inline TEE_Result mcp7940x_get_time(unsigned int idx __unused,
					    struct mcp7940x_time *tm __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

static inline TEE_Result
mcp7940x_set_time(unsigned int idx __unused,
		  const struct mcp7940x_time *tm __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

#endif /* __DRIVERS_MCP7940X_H */
