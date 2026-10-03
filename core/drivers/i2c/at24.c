// SPDX-License-Identifier: BSD-2-Clause
/*
 * AT24 family I2C EEPROMs.
 *
 * Devices up to 2 KiB (24c01 .. 24c16) use a one-byte word address and
 * expose 256-byte blocks at consecutive I2C addresses; larger ones use a
 * two-byte word address. Writes never cross a page.
 *
 * DT properties (Linux at24 binding): "pagesize", "size", "read-only".
 */
#include <drivers/at24.h>
#include <drivers/i2c.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <kernel/mutex.h>
#include <malloc.h>
#include <string.h>
#include <sys/queue.h>
#include <trace.h>
#include <util.h>

#define AT24_BLOCK_SIZE		256
#define AT24_MAX_PAGE_SIZE	128
#define AT24_WRITE_TIMEOUT_US	25000

struct at24_cfg {
	size_t size;
	size_t page_size;
};

struct at24 {
	struct i2c_dev *i2c_dev;
	struct mutex lock;
	size_t size;
	size_t page_size;
	bool addr16;		/* two-byte word address */
	bool read_only;
	STAILQ_ENTRY(at24) link;
};

static STAILQ_HEAD(, at24) at24_list = STAILQ_HEAD_INITIALIZER(at24_list);
static unsigned int at24_num;

static struct at24 *at24_get(unsigned int idx)
{
	struct at24 *dev = NULL;

	STAILQ_FOREACH(dev, &at24_list, link)
		if (!idx--)
			return dev;

	return NULL;
}

unsigned int at24_count(void)
{
	return at24_num;
}

TEE_Result at24_get_size(unsigned int idx, size_t *size)
{
	struct at24 *dev = at24_get(idx);

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;

	*size = dev->size;

	return TEE_SUCCESS;
}

/*
 * Return the I2C target for @offset in @target and fill the word address
 * in @word, returning its length.
 */
static size_t at24_address(struct at24 *dev, size_t offset,
			   struct i2c_dev *target, uint8_t word[2])
{
	*target = *dev->i2c_dev;

	if (dev->addr16) {
		word[0] = offset >> 8;
		word[1] = offset;
		return 2;
	}

	target->addr += offset / AT24_BLOCK_SIZE;
	word[0] = offset % AT24_BLOCK_SIZE;

	return 1;
}

/* Max bytes from @offset without leaving the current block */
static size_t at24_block_left(struct at24 *dev, size_t offset)
{
	if (dev->addr16)
		return dev->size - offset;

	return AT24_BLOCK_SIZE - offset % AT24_BLOCK_SIZE;
}

/* The device does not acknowledge while an internal write cycle runs */
static TEE_Result at24_wait_write(struct i2c_dev *target)
{
	uint64_t timeout = timeout_init_us(AT24_WRITE_TIMEOUT_US);
	uint8_t dummy = 0;

	while (i2c_read(target, &dummy, 1)) {
		if (timeout_elapsed(timeout))
			return TEE_ERROR_BUSY;
		udelay(500);
	}

	return TEE_SUCCESS;
}

static bool at24_range_ok(struct at24 *dev, size_t offset, size_t len)
{
	return offset <= dev->size && len <= dev->size - offset;
}

TEE_Result at24_read(unsigned int idx, size_t offset, uint8_t *buf,
		     size_t len)
{
	struct at24 *dev = at24_get(idx);
	TEE_Result res = TEE_SUCCESS;
	struct i2c_dev target = { };
	uint8_t word[2] = { };
	size_t word_len = 0;
	size_t chunk = 0;

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;
	if (!at24_range_ok(dev, offset, len))
		return TEE_ERROR_BAD_PARAMETERS;

	mutex_lock(&dev->lock);
	while (len) {
		chunk = MIN(len, at24_block_left(dev, offset));
		word_len = at24_address(dev, offset, &target, word);

		res = i2c_write(&target, word, word_len);
		if (!res)
			res = i2c_read(&target, buf, chunk);
		if (res)
			break;

		offset += chunk;
		buf += chunk;
		len -= chunk;
	}
	mutex_unlock(&dev->lock);

	return res;
}

TEE_Result at24_write(unsigned int idx, size_t offset, const uint8_t *buf,
		      size_t len)
{
	uint8_t frame[2 + AT24_MAX_PAGE_SIZE] = { };
	struct at24 *dev = at24_get(idx);
	TEE_Result res = TEE_SUCCESS;
	struct i2c_dev target = { };
	size_t word_len = 0;
	size_t chunk = 0;

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;
	if (dev->read_only)
		return TEE_ERROR_ACCESS_DENIED;
	if (!at24_range_ok(dev, offset, len))
		return TEE_ERROR_BAD_PARAMETERS;

	mutex_lock(&dev->lock);
	while (len) {
		/* Never cross a page: the address would wrap inside it */
		chunk = MIN(len, dev->page_size - offset % dev->page_size);
		word_len = at24_address(dev, offset, &target, frame);
		memcpy(frame + word_len, buf, chunk);

		res = i2c_write(&target, frame, word_len + chunk);
		if (!res)
			res = at24_wait_write(&target);
		if (res)
			break;

		offset += chunk;
		buf += chunk;
		len -= chunk;
	}
	mutex_unlock(&dev->lock);

	return res;
}

static TEE_Result at24_probe(struct i2c_dev *i2c_dev, const void *fdt,
			     int node, const void *compat_data)
{
	const struct at24_cfg *cfg = compat_data;
	struct at24 *dev = NULL;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return TEE_ERROR_OUT_OF_MEMORY;

	dev->i2c_dev = i2c_dev;
	dev->size = fdt_read_uint32_default(fdt, node, "size", cfg->size);
	dev->page_size = fdt_read_uint32_default(fdt, node, "pagesize",
						 cfg->page_size);
	dev->read_only = dt_have_prop(fdt, node, "read-only");
	dev->addr16 = dev->size > 2048;

	if (!dev->size || !dev->page_size || !IS_POWER_OF_TWO(dev->page_size)) {
		free(dev);
		return TEE_ERROR_BAD_PARAMETERS;
	}
	dev->page_size = MIN(dev->page_size, (size_t)AT24_MAX_PAGE_SIZE);

	mutex_init(&dev->lock);
	STAILQ_INSERT_TAIL(&at24_list, dev, link);
	at24_num++;

	DMSG("at24 #%u at %#"PRIx16": %zu bytes, page %zu", at24_num - 1,
	     i2c_dev->addr, dev->size, dev->page_size);

	return TEE_SUCCESS;
}

#define AT24_CFG(_name, _size, _page) \
	static const struct at24_cfg at24_cfg_ ## _name = { \
		.size = (_size), .page_size = (_page), \
	}

AT24_CFG(24c01, 128, 8);
AT24_CFG(24c02, 256, 8);
AT24_CFG(24c04, 512, 16);
AT24_CFG(24c08, 1024, 16);
AT24_CFG(24c16, 2048, 16);
AT24_CFG(24c32, 4096, 32);
AT24_CFG(24c64, 8192, 32);
AT24_CFG(24c128, 16384, 64);
AT24_CFG(24c256, 32768, 64);
AT24_CFG(24c512, 65536, 128);

static const struct dt_device_match at24_match_table[] = {
	{ .compatible = "atmel,24c01", .compat_data = &at24_cfg_24c01 },
	{ .compatible = "atmel,24c02", .compat_data = &at24_cfg_24c02 },
	{ .compatible = "atmel,24c04", .compat_data = &at24_cfg_24c04 },
	{ .compatible = "atmel,24c08", .compat_data = &at24_cfg_24c08 },
	{ .compatible = "atmel,24c16", .compat_data = &at24_cfg_24c16 },
	{ .compatible = "atmel,24c32", .compat_data = &at24_cfg_24c32 },
	{ .compatible = "atmel,24c64", .compat_data = &at24_cfg_24c64 },
	{ .compatible = "atmel,24c128", .compat_data = &at24_cfg_24c128 },
	{ .compatible = "atmel,24c256", .compat_data = &at24_cfg_24c256 },
	{ .compatible = "atmel,24c512", .compat_data = &at24_cfg_24c512 },
	{ }
};

DEFINE_I2C_DEV_DRIVER(at24, at24_match_table, at24_probe);
