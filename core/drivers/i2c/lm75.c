// SPDX-License-Identifier: BSD-2-Clause
/*
 * LM75 family I2C temperature sensors.
 */
#include <drivers/i2c.h>
#include <drivers/lm75.h>
#include <kernel/dt_driver.h>
#include <malloc.h>
#include <sys/queue.h>
#include <trace.h>

#define LM75_REG_TEMP	0x00

struct lm75 {
	struct i2c_dev *i2c_dev;
	unsigned int res_bits;	/* temperature resolution */
	STAILQ_ENTRY(lm75) link;
};

static STAILQ_HEAD(, lm75) lm75_list = STAILQ_HEAD_INITIALIZER(lm75_list);
static unsigned int lm75_num;

static struct lm75 *lm75_get(unsigned int idx)
{
	struct lm75 *dev = NULL;

	STAILQ_FOREACH(dev, &lm75_list, link)
		if (!idx--)
			return dev;

	return NULL;
}

unsigned int lm75_count(void)
{
	return lm75_num;
}

TEE_Result lm75_read_temp(unsigned int idx, int32_t *milli_celsius)
{
	struct lm75 *dev = lm75_get(idx);
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t b[2] = { };
	int16_t raw = 0;

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;

	res = i2c_bus_read_block_raw(dev->i2c_dev, LM75_REG_TEMP, b,
				     sizeof(b));
	if (res)
		return res;

	/* Left-justified two's complement, LSB = 2^-(res_bits - 8) degC */
	raw = (int16_t)((b[0] << 8) | b[1]) >> (16 - dev->res_bits);
	*milli_celsius = (raw * 1000) >> (dev->res_bits - 8);

	return TEE_SUCCESS;
}

static TEE_Result lm75_probe(struct i2c_dev *i2c_dev,
			     const void *fdt __unused, int node __unused,
			     const void *compat_data)
{
	struct lm75 *dev = calloc(1, sizeof(*dev));

	if (!dev)
		return TEE_ERROR_OUT_OF_MEMORY;

	dev->i2c_dev = i2c_dev;
	dev->res_bits = (uintptr_t)compat_data;
	STAILQ_INSERT_TAIL(&lm75_list, dev, link);
	lm75_num++;

	DMSG("lm75 #%u at %#"PRIx16, lm75_num - 1, i2c_dev->addr);

	return TEE_SUCCESS;
}

static const struct dt_device_match lm75_match_table[] = {
	{ .compatible = "national,lm75", .compat_data = (void *)9 },
	{ .compatible = "national,lm75a", .compat_data = (void *)11 },
	{ .compatible = "nxp,lm75a", .compat_data = (void *)11 },
	{ }
};

DEFINE_I2C_DEV_DRIVER(lm75, lm75_match_table, lm75_probe);
