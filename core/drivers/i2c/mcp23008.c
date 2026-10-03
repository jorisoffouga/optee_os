// SPDX-License-Identifier: BSD-2-Clause
/*
 * Microchip MCP23008 8-bit I2C GPIO expander.
 *
 * Optional DT properties: "reset-gpios" (the RESET line is pulsed at
 * probe) and "pinctrl-0" (applied before using the reset GPIO).
 */
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <drivers/mcp23008.h>
#include <drivers/pinctrl.h>
#include <dt-bindings/gpio/gpio.h>
#include <kernel/delay.h>
#include <kernel/dt_driver.h>
#include <kernel/mutex.h>
#include <malloc.h>
#include <sys/queue.h>
#include <trace.h>
#include <util.h>

#define MCP23008_IODIR		0x00 /* 1 = input */
#define MCP23008_GPPU		0x06
#define MCP23008_GPIO		0x09
#define MCP23008_OLAT		0x0A

#define MCP23008_RESET_US	10	/* datasheet: 1 us min pulse */

struct mcp23008 {
	struct i2c_dev *i2c_dev;
	struct mutex lock;	/* serializes read-modify-write cycles */
	struct gpio_chip chip;
	STAILQ_ENTRY(mcp23008) link;
};

static STAILQ_HEAD(, mcp23008) mcp23008_list =
	STAILQ_HEAD_INITIALIZER(mcp23008_list);
static unsigned int mcp23008_num;

static struct mcp23008 *mcp23008_get(unsigned int idx)
{
	struct mcp23008 *dev = NULL;

	STAILQ_FOREACH(dev, &mcp23008_list, link)
		if (!idx--)
			return dev;

	return NULL;
}

unsigned int mcp23008_count(void)
{
	return mcp23008_num;
}

static TEE_Result update_reg(struct mcp23008 *dev, uint8_t reg, uint8_t mask,
			     uint8_t val)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t cur = 0;

	mutex_lock(&dev->lock);
	res = i2c_smbus_read_byte_data(dev->i2c_dev, reg, &cur);
	if (!res)
		res = i2c_smbus_write_byte_data(dev->i2c_dev, reg,
						(cur & ~mask) | (val & mask));
	mutex_unlock(&dev->lock);

	return res;
}

static TEE_Result update_pin(unsigned int idx, unsigned int pin, uint8_t reg,
			     bool set)
{
	struct mcp23008 *dev = mcp23008_get(idx);

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;
	if (pin >= MCP23008_NUM_PINS)
		return TEE_ERROR_BAD_PARAMETERS;

	return update_reg(dev, reg, BIT(pin), set ? BIT(pin) : 0);
}

TEE_Result mcp23008_set_direction(unsigned int idx, unsigned int pin,
				  bool output)
{
	return update_pin(idx, pin, MCP23008_IODIR, !output);
}

TEE_Result mcp23008_set_pullup(unsigned int idx, unsigned int pin,
			       bool enable)
{
	return update_pin(idx, pin, MCP23008_GPPU, enable);
}

TEE_Result mcp23008_set_value(unsigned int idx, unsigned int pin, bool high)
{
	return update_pin(idx, pin, MCP23008_OLAT, high);
}

TEE_Result mcp23008_get_value(unsigned int idx, unsigned int pin, bool *high)
{
	struct mcp23008 *dev = mcp23008_get(idx);
	TEE_Result res = TEE_ERROR_GENERIC;
	uint8_t val = 0;

	if (!dev)
		return TEE_ERROR_ITEM_NOT_FOUND;
	if (pin >= MCP23008_NUM_PINS)
		return TEE_ERROR_BAD_PARAMETERS;

	res = i2c_smbus_read_byte_data(dev->i2c_dev, MCP23008_GPIO, &val);
	if (!res)
		*high = val & BIT(pin);

	return res;
}

#ifdef CFG_DRIVERS_GPIO
/* GPIO framework callbacks cannot report bus errors: log them */
static struct mcp23008 *chip_to_dev(struct gpio_chip *chip)
{
	return container_of(chip, struct mcp23008, chip);
}

static enum gpio_dir gpio_op_get_direction(struct gpio_chip *chip,
					   unsigned int pin)
{
	struct mcp23008 *dev = chip_to_dev(chip);
	uint8_t val = 0;

	if (i2c_smbus_read_byte_data(dev->i2c_dev, MCP23008_IODIR, &val))
		EMSG("mcp23008: read IODIR failed");

	return (val & BIT(pin)) ? GPIO_DIR_IN : GPIO_DIR_OUT;
}

static void gpio_op_set_direction(struct gpio_chip *chip, unsigned int pin,
				  enum gpio_dir dir)
{
	if (update_reg(chip_to_dev(chip), MCP23008_IODIR, BIT(pin),
		       dir == GPIO_DIR_IN ? BIT(pin) : 0))
		EMSG("mcp23008: set direction failed");
}

static enum gpio_level gpio_op_get_value(struct gpio_chip *chip,
					 unsigned int pin)
{
	struct mcp23008 *dev = chip_to_dev(chip);
	uint8_t val = 0;

	if (i2c_smbus_read_byte_data(dev->i2c_dev, MCP23008_GPIO, &val))
		EMSG("mcp23008: read GPIO failed");

	return (val & BIT(pin)) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

static void gpio_op_set_value(struct gpio_chip *chip, unsigned int pin,
			      enum gpio_level level)
{
	if (update_reg(chip_to_dev(chip), MCP23008_OLAT, BIT(pin),
		       level == GPIO_LEVEL_HIGH ? BIT(pin) : 0))
		EMSG("mcp23008: set value failed");
}

static TEE_Result gpio_op_configure(struct gpio_chip *chip, struct gpio *gpio)
{
	return update_reg(chip_to_dev(chip), MCP23008_GPPU, BIT(gpio->pin),
			  (gpio->dt_flags & GPIO_PULL_UP) ? BIT(gpio->pin) : 0);
}

static void gpio_op_put(struct gpio_chip *chip __unused, struct gpio *gpio)
{
	free(gpio);
}

static const struct gpio_ops mcp23008_gpio_ops = {
	.get_direction = gpio_op_get_direction,
	.set_direction = gpio_op_set_direction,
	.get_value = gpio_op_get_value,
	.set_value = gpio_op_set_value,
	.configure = gpio_op_configure,
	.put = gpio_op_put,
};

static TEE_Result mcp23008_dt_get_gpio(struct dt_pargs *pargs, void *data,
				       struct gpio **out_gpio)
{
	struct mcp23008 *dev = data;
	struct gpio *gpio = NULL;
	TEE_Result res = TEE_ERROR_GENERIC;

	res = gpio_dt_alloc_pin(pargs, &gpio);
	if (res)
		return res;

	if (gpio->pin >= MCP23008_NUM_PINS) {
		free(gpio);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	gpio->chip = &dev->chip;
	*out_gpio = gpio;

	return TEE_SUCCESS;
}

static TEE_Result register_gpio_provider(struct mcp23008 *dev,
					 const void *fdt, int node)
{
	if (!dt_have_prop(fdt, node, "gpio-controller"))
		return TEE_SUCCESS;

	dev->chip.ops = &mcp23008_gpio_ops;

	return gpio_register_provider(fdt, node, mcp23008_dt_get_gpio, dev);
}
#else
static TEE_Result register_gpio_provider(struct mcp23008 *dev __unused,
					 const void *fdt __unused,
					 int node __unused)
{
	return TEE_SUCCESS;
}
#endif /* CFG_DRIVERS_GPIO */

static TEE_Result apply_default_pinctrl(const void *fdt, int node)
{
	struct pinctrl_state *state = NULL;
	TEE_Result res = TEE_ERROR_GENERIC;

	res = pinctrl_get_state_by_name(fdt, node, "default", &state);
	if (res == TEE_ERROR_ITEM_NOT_FOUND || res == TEE_ERROR_NOT_SUPPORTED)
		return TEE_SUCCESS;
	if (res)
		return res;

	res = pinctrl_apply_state(state);
	pinctrl_free_state(state);

	return res;
}

/* Pulse the optional RESET line and leave the device out of reset */
static TEE_Result hw_reset(const void *fdt, int node)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	struct gpio *reset = NULL;

	res = apply_default_pinctrl(fdt, node);
	if (res)
		return res;

	/* Logical high = reset asserted, whatever the line polarity */
	res = gpio_dt_cfg_by_index(fdt, node, 0, "reset", GPIO_OUT_HIGH,
				   &reset);
	if (res == TEE_ERROR_ITEM_NOT_FOUND || res == TEE_ERROR_NOT_SUPPORTED)
		return TEE_SUCCESS;
	if (res)
		return res;

	udelay(MCP23008_RESET_US);
	gpio_set_value(reset, GPIO_LEVEL_LOW);
	udelay(MCP23008_RESET_US);
	gpio_put(reset);

	return TEE_SUCCESS;
}

static TEE_Result mcp23008_probe(struct i2c_dev *i2c_dev, const void *fdt,
				 int node, const void *compat_data __unused)
{
	struct mcp23008 *dev = NULL;
	TEE_Result res = TEE_ERROR_GENERIC;

	/* May defer until the GPIO and pin controllers are probed */
	res = hw_reset(fdt, node);
	if (res)
		return res;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return TEE_ERROR_OUT_OF_MEMORY;

	dev->i2c_dev = i2c_dev;
	mutex_init(&dev->lock);

	res = register_gpio_provider(dev, fdt, node);
	if (res) {
		free(dev);
		return res;
	}

	STAILQ_INSERT_TAIL(&mcp23008_list, dev, link);
	mcp23008_num++;

	DMSG("mcp23008 #%u at %#"PRIx16, mcp23008_num - 1, i2c_dev->addr);

	return TEE_SUCCESS;
}

static const struct dt_device_match mcp23008_match_table[] = {
	{ .compatible = "microchip,mcp23008" },
	{ }
};

DEFINE_I2C_DEV_DRIVER(mcp23008, mcp23008_match_table, mcp23008_probe);
