// SPDX-License-Identifier: BSD-2-Clause
/*
 * NXP PCF8591 I2C 4-channel 8-bit ADC (single-ended inputs) and 8-bit DAC.
 */
#include <drivers/i2c.h>
#include <drivers/pcf8591.h>
#include <kernel/dt_driver.h>
#include <kernel/mutex.h>
#include <malloc.h>
#include <sys/queue.h>
#include <trace.h>
#include <util.h>

#define PCF8591_CTRL_DAC_EN	BIT(6)

struct pcf8591 {
	struct i2c_dev *i2c_dev;
	struct mutex lock;
	uint8_t dac_ctrl;	/* every control byte also sets the DAC state */
	STAILQ_ENTRY(pcf8591) link;
};

static STAILQ_HEAD(, pcf8591) pcf8591_list =
	STAILQ_HEAD_INITIALIZER(pcf8591_list);
static unsigned int pcf8591_num;

static struct pcf8591 *pcf8591_get(unsigned int idx)
{
	struct pcf8591 *dev = NULL;

	STAILQ_FOREACH(dev, &pcf8591_list, link)
		if (!idx--)
			return dev;

	return NULL;
}

unsigned int pcf8591_count(void)
{
	return pcf8591_num;
}

TEE_Result pcf8591_read_adc(unsigned int idx, unsigned int channel,
			    uint8_t *val)
{
	struct pcf8591 *dev = pcf8591_get(idx);
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t b[2] = { };
	uint8_t ctrl = 0;

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;
	if (channel >= PCF8591_NUM_CHANNELS)
		return TEE_ERROR_BAD_PARAMETERS;

	mutex_lock(&dev->lock);
	ctrl = dev->dac_ctrl | channel;
	res = i2c_write(dev->i2c_dev, &ctrl, 1);
	/* First byte is the previous conversion; the second is ours */
	if (!res)
		res = i2c_read(dev->i2c_dev, b, sizeof(b));
	mutex_unlock(&dev->lock);

	if (!res)
		*val = b[1];

	return res;
}

TEE_Result pcf8591_write_dac(unsigned int idx, uint8_t val, bool enable)
{
	struct pcf8591 *dev = pcf8591_get(idx);
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t b[2] = { };

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;

	mutex_lock(&dev->lock);
	b[0] = enable ? PCF8591_CTRL_DAC_EN : 0;
	b[1] = val;
	res = i2c_write(dev->i2c_dev, b, sizeof(b));
	if (!res)
		dev->dac_ctrl = b[0];
	mutex_unlock(&dev->lock);

	return res;
}

static TEE_Result pcf8591_probe(struct i2c_dev *i2c_dev,
				const void *fdt __unused, int node __unused,
				const void *compat_data __unused)
{
	struct pcf8591 *dev = calloc(1, sizeof(*dev));

	if (!dev)
		return TEE_ERROR_OUT_OF_MEMORY;

	dev->i2c_dev = i2c_dev;
	mutex_init(&dev->lock);
	STAILQ_INSERT_TAIL(&pcf8591_list, dev, link);
	pcf8591_num++;

	DMSG("pcf8591 #%u at %#"PRIx16, pcf8591_num - 1, i2c_dev->addr);

	return TEE_SUCCESS;
}

static const struct dt_device_match pcf8591_match_table[] = {
	{ .compatible = "nxp,pcf8591" },
	{ }
};

DEFINE_I2C_DEV_DRIVER(pcf8591, pcf8591_match_table, pcf8591_probe);
