// SPDX-License-Identifier: BSD-2-Clause
/*
 * Microchip MCP7940x/MCP7941x battery-backed I2C real-time clock.
 */
#include <drivers/i2c.h>
#include <drivers/mcp7940x.h>
#include <drivers/rtc.h>
#include <kernel/delay.h>
#include <kernel/dt_driver.h>
#include <kernel/mutex.h>
#include <malloc.h>
#include <sys/queue.h>
#include <trace.h>
#include <util.h>

#define MCP7940X_RTCSEC		0x00
#define MCP7940X_RTCWKDAY	0x03
#define MCP7940X_NUM_TIME_REGS	7

#define RTCSEC_ST		BIT(7)	/* oscillator start */
#define RTCHOUR_12H		BIT(6)
#define RTCHOUR_PM		BIT(5)
#define RTCWKDAY_OSCRUN		BIT(5)
#define RTCWKDAY_VBATEN		BIT(3)
#define RTCWKDAY_WDAY_MASK	GENMASK_32(2, 0)
#define RTCMTH_MASK		GENMASK_32(4, 0)

#define OSC_STOP_TIMEOUT_US	10000

struct mcp7940x {
	struct i2c_dev *i2c_dev;
	struct mutex lock;	/* serializes the set time sequence */
	STAILQ_ENTRY(mcp7940x) link;
};

static STAILQ_HEAD(, mcp7940x) mcp7940x_list =
	STAILQ_HEAD_INITIALIZER(mcp7940x_list);
static unsigned int mcp7940x_num;

static struct mcp7940x *mcp7940x_get(unsigned int idx)
{
	struct mcp7940x *dev = NULL;

	STAILQ_FOREACH(dev, &mcp7940x_list, link)
		if (!idx--)
			return dev;

	return NULL;
}

unsigned int mcp7940x_count(void)
{
	return mcp7940x_num;
}

static uint8_t bcd2bin(uint8_t v)
{
	return (v & 0x0F) + (v >> 4) * 10;
}

static uint8_t bin2bcd(uint32_t v)
{
	return ((v / 10) << 4) | (v % 10);
}

static TEE_Result get_time(struct mcp7940x *dev, struct mcp7940x_time *tm)
{
	uint8_t r[MCP7940X_NUM_TIME_REGS] = { };
	TEE_Result res = TEE_ERROR_GENERIC;

	mutex_lock(&dev->lock);
	res = i2c_bus_read_block_raw(dev->i2c_dev, MCP7940X_RTCSEC, r,
				     sizeof(r));
	mutex_unlock(&dev->lock);
	if (res)
		return res;

	/* Never set since power loss: time is meaningless */
	if (!(r[0] & RTCSEC_ST))
		return TEE_ERROR_BAD_STATE;

	tm->sec = bcd2bin(r[0] & 0x7F);
	tm->min = bcd2bin(r[1] & 0x7F);
	if (r[2] & RTCHOUR_12H)
		tm->hour = bcd2bin(r[2] & 0x1F) % 12 +
			   ((r[2] & RTCHOUR_PM) ? 12 : 0);
	else
		tm->hour = bcd2bin(r[2] & 0x3F);
	tm->wday = ((r[3] & RTCWKDAY_WDAY_MASK) + 6) % 7;
	tm->mday = bcd2bin(r[4] & 0x3F);
	tm->mon = bcd2bin(r[5] & RTCMTH_MASK) - 1;
	tm->year = bcd2bin(r[6]) + 2000;

	return TEE_SUCCESS;
}

static TEE_Result set_time(struct mcp7940x *dev,
			   const struct mcp7940x_time *tm)
{
	uint8_t r[MCP7940X_NUM_TIME_REGS] = { };
	TEE_Result res = TEE_ERROR_GENERIC;
	uint64_t timeout = 0;
	uint8_t wkday = 0;

	if (tm->year < 2000 || tm->year > 2099 || tm->mon > 11 ||
	    !tm->mday || tm->mday > 31 || tm->wday > 6 || tm->hour > 23 ||
	    tm->min > 59 || tm->sec > 59)
		return TEE_ERROR_BAD_PARAMETERS;

	mutex_lock(&dev->lock);

	/* Stop the oscillator and wait for OSCRUN to clear before writing */
	res = i2c_smbus_write_byte_data(dev->i2c_dev, MCP7940X_RTCSEC, 0);
	if (res)
		goto out;

	timeout = timeout_init_us(OSC_STOP_TIMEOUT_US);
	while (true) {
		res = i2c_smbus_read_byte_data(dev->i2c_dev, MCP7940X_RTCWKDAY,
					       &wkday);
		if (res || !(wkday & RTCWKDAY_OSCRUN))
			break;
		if (timeout_elapsed(timeout)) {
			res = TEE_ERROR_BUSY;
			break;
		}
		udelay(100);
	}
	if (res)
		goto out;

	r[0] = bin2bcd(tm->sec) | RTCSEC_ST;
	r[1] = bin2bcd(tm->min);
	r[2] = bin2bcd(tm->hour);			/* 24-hour mode */
	r[3] = (tm->wday + 1) | RTCWKDAY_VBATEN;	/* keep on battery */
	r[4] = bin2bcd(tm->mday);
	r[5] = bin2bcd(tm->mon + 1);
	r[6] = bin2bcd(tm->year - 2000);

	res = i2c_bus_write_block_raw(dev->i2c_dev, MCP7940X_RTCSEC, r,
				      sizeof(r));
out:
	mutex_unlock(&dev->lock);

	return res;
}

TEE_Result mcp7940x_get_time(unsigned int idx, struct mcp7940x_time *tm)
{
	struct mcp7940x *dev = mcp7940x_get(idx);

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;

	return get_time(dev, tm);
}

TEE_Result mcp7940x_set_time(unsigned int idx, const struct mcp7940x_time *tm)
{
	struct mcp7940x *dev = mcp7940x_get(idx);

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;

	return set_time(dev, tm);
}

#ifdef CFG_MCP7940X_SYSTEM_RTC
static TEE_Result rtc_op_get_time(struct rtc *rtc __unused,
				  struct optee_rtc_time *tm)
{
	struct mcp7940x_time t = { };
	TEE_Result res = mcp7940x_get_time(0, &t);

	if (res)
		return res;

	*tm = (struct optee_rtc_time)RTC_TIME(t.year, t.mon, t.mday, t.wday,
					      t.hour, t.min, t.sec, 0);

	return TEE_SUCCESS;
}

static TEE_Result rtc_op_set_time(struct rtc *rtc __unused,
				  struct optee_rtc_time *tm)
{
	struct mcp7940x_time t = {
		.year = tm->tm_year, .mon = tm->tm_mon, .mday = tm->tm_mday,
		.wday = tm->tm_wday, .hour = tm->tm_hour, .min = tm->tm_min,
		.sec = tm->tm_sec,
	};

	return mcp7940x_set_time(0, &t);
}

static const struct rtc_ops mcp7940x_rtc_ops = {
	.get_time = rtc_op_get_time,
	.set_time = rtc_op_set_time,
};

static struct rtc mcp7940x_rtc = {
	.ops = &mcp7940x_rtc_ops,
	.range_min = RTC_TIME(2000, 0, 1, 6, 0, 0, 0, 0),
	.range_max = RTC_TIME(2099, 11, 31, 4, 23, 59, 59, 999),
};

static void register_system_rtc(void)
{
	/* Only the first instance backs the OP-TEE system RTC */
	if (mcp7940x_num != 1)
		return;

	if (rtc_device) {
		EMSG("mcp7940x: a system RTC is already registered");
		return;
	}

	rtc_register(&mcp7940x_rtc);
}
#else
static void register_system_rtc(void)
{
}
#endif /* CFG_MCP7940X_SYSTEM_RTC */

static TEE_Result mcp7940x_probe(struct i2c_dev *i2c_dev,
				 const void *fdt __unused, int node __unused,
				 const void *compat_data __unused)
{
	struct mcp7940x *dev = calloc(1, sizeof(*dev));

	if (!dev)
		return TEE_ERROR_OUT_OF_MEMORY;

	dev->i2c_dev = i2c_dev;
	mutex_init(&dev->lock);
	STAILQ_INSERT_TAIL(&mcp7940x_list, dev, link);
	mcp7940x_num++;

	register_system_rtc();

	DMSG("mcp7940x #%u at %#"PRIx16, mcp7940x_num - 1, i2c_dev->addr);

	return TEE_SUCCESS;
}

static const struct dt_device_match mcp7940x_match_table[] = {
	{ .compatible = "microchip,mcp7940x" },
	{ .compatible = "microchip,mcp7941x" },
	{ }
};

DEFINE_I2C_DEV_DRIVER(mcp7940x, mcp7940x_match_table, mcp7940x_probe);
