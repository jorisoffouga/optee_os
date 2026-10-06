// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2026, Joris Offouga
 *
 * i.MX Cortex-M remote processor control, used by the remoteproc PTA:
 * - i.MX7: Cortex-M4, started and stopped through SRC_M4RCR like Linux
 *   imx_rproc ("fsl,imx7d-cm4");
 * - i.MX8MP: Cortex-M7, with the start/stop sequences of the TF-A
 *   IMX_SIP_SRC service (plat/imx/imx8m/gpc_common.c).
 */

#include <drivers/imx_remoteproc.h>
#include <imx-regs.h>
#include <initcall.h>
#include <io.h>
#include <kernel/cache_helpers.h>
#include <kernel/delay.h>
#include <kernel/panic.h>
#include <kernel/tee_misc.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <string.h>
#include <trace.h>
#include <util.h>

/*
 * struct imx_rproc_mem - Memory region of the remote processor
 * @da: device address, from the remote processor memory map
 * @pa: physical address, from the Cortex-A memory map
 * @size: byte size of the region
 *
 * A region can be listed several times, once per device address alias.
 */
struct imx_rproc_mem {
	paddr_t da;
	paddr_t pa;
	size_t size;
};

#if defined(CFG_MX7)

#define SRC_M4RCR			0x0C
#define SRC_M4RCR_ENABLE_M4		BIT32(3)
#define SRC_M4RCR_SW_M4P_RST		BIT32(2)
#define SRC_M4RCR_SW_M4C_RST		BIT32(1)
#define SRC_M4RCR_SW_M4C_NON_SCLR_RST	BIT32(0)
#define SRC_M4RCR_MASK			GENMASK_32(3, 0)

/* Platform and core reset pulses, then the core runs */
#define SRC_M4RCR_START			(SRC_M4RCR_ENABLE_M4 | \
					 SRC_M4RCR_SW_M4P_RST | \
					 SRC_M4RCR_SW_M4C_RST)
/* Core held in reset */
#define SRC_M4RCR_STOP			(SRC_M4RCR_ENABLE_M4 | \
					 SRC_M4RCR_SW_M4C_RST | \
					 SRC_M4RCR_SW_M4C_NON_SCLR_RST)

/* DDR code alias, from the M4: 0x10000000 is DDR 0x80000000 */
#define M4_DDR_CODE_DA			0x10000000
#define M4_DDR_CODE_PA			0x80000000
#define M4_DDR_CODE_SIZE		0x0FFF0000

register_phys_mem(MEM_AREA_IO_SEC, SRC_BASE, SRC_SIZE);

static const struct imx_rproc_mem imx_rproc_mems[] = {
	/* OCRAM_S, at address 0 for the boot vector */
	{ .da = 0x00000000, .pa = 0x00180000, .size = 0x8000 },
	{ .da = 0x00180000, .pa = 0x00180000, .size = 0x8000 },
	/* OCRAM, code and data aliases */
	{ .da = 0x00900000, .pa = 0x00900000, .size = 0x20000 },
	{ .da = 0x20200000, .pa = 0x00900000, .size = 0x20000 },
	/* TCML, TCMU */
	{ .da = 0x1FFF8000, .pa = 0x007F8000, .size = 0x8000 },
	{ .da = 0x20000000, .pa = 0x00800000, .size = 0x8000 },
#if CFG_IMX_REMOTEPROC_DDR_SIZE
	/* DDR, data alias */
	{
		.da = CFG_IMX_REMOTEPROC_DDR_START,
		.pa = CFG_IMX_REMOTEPROC_DDR_START,
		.size = CFG_IMX_REMOTEPROC_DDR_SIZE,
	},
#if CFG_IMX_REMOTEPROC_DDR_START + CFG_IMX_REMOTEPROC_DDR_SIZE <= \
	M4_DDR_CODE_PA + M4_DDR_CODE_SIZE
	/* DDR, code alias */
	{
		.da = CFG_IMX_REMOTEPROC_DDR_START - M4_DDR_CODE_PA +
		      M4_DDR_CODE_DA,
		.pa = CFG_IMX_REMOTEPROC_DDR_START,
		.size = CFG_IMX_REMOTEPROC_DDR_SIZE,
	},
#endif
#endif
};

static vaddr_t src_base(void)
{
	return core_mmu_get_va(SRC_BASE, MEM_AREA_IO_SEC, SRC_SIZE);
}

static TEE_Result imx_rproc_hw_start(void)
{
	/* The M4 boots from the vector table at its address 0 (OCRAM_S) */
	io_clrsetbits32(src_base() + SRC_M4RCR, SRC_M4RCR_MASK,
			SRC_M4RCR_START);

	return TEE_SUCCESS;
}

static TEE_Result imx_rproc_hw_stop(void)
{
	io_clrsetbits32(src_base() + SRC_M4RCR, SRC_M4RCR_MASK,
			SRC_M4RCR_STOP);

	return TEE_SUCCESS;
}

/*
 * Hold the M4 in reset until the firmware is authenticated and loaded. Only
 * the SRC is accessed: the M4 TCM clock is enabled later, by Linux imx_rproc.
 */
static void imx_rproc_hw_init(void)
{
	imx_rproc_hw_stop();
}

#elif defined(CFG_MX8MP)

#define IOMUXC_GPR_BASE			0x30340000
#define IOMUXC_GPR22			0x58
#define IOMUXC_GPR22_CM7_CPUWAIT	BIT32(0)

#define SRC_M7_BASE			0x30390000
#define SRC_M7RCR			0x0C
#define SRC_M7RCR_ENABLE_M7		BIT32(3)
#define SRC_M7RCR_SW_M7P_RST		BIT32(2)
#define SRC_M7RCR_SW_M7C_RST		BIT32(1)
#define SRC_M7RCR_MASK			GENMASK_32(3, 0)

#define GPC_BASE			0x303A0000
/* Bit 0: M7 SLEEPHOLDREQn, active low */
#define GPC_M7_SLEEPHOLD		0x2C
#define GPC_M7_SLEEPHOLDREQN		BIT32(0)
#define GPC_LPS_CPU1			0xEC
#define GPC_LPS_CPU1_SLEEPHOLDACKN	BIT32(1)
#define GPC_LPS_CPU1_LPM_MASK		GENMASK_32(25, 24)

#define IMX_RPROC_TIMEOUT_US		10000

register_phys_mem_pgdir(MEM_AREA_IO_SEC, IOMUXC_GPR_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, SRC_M7_BASE, SMALL_PAGE_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, GPC_BASE, SMALL_PAGE_SIZE);

static const struct imx_rproc_mem imx_rproc_mems[] = {
	/* ITCM */
	{ .da = 0x00000000, .pa = 0x007E0000, .size = 0x20000 },
	/* DTCM */
	{ .da = 0x20000000, .pa = 0x00800000, .size = 0x20000 },
#if CFG_IMX_REMOTEPROC_DDR_SIZE
	/* DDR, same address for both cores */
	{
		.da = CFG_IMX_REMOTEPROC_DDR_START,
		.pa = CFG_IMX_REMOTEPROC_DDR_START,
		.size = CFG_IMX_REMOTEPROC_DDR_SIZE,
	},
#endif
};

static vaddr_t gpr_base(void)
{
	return core_mmu_get_va(IOMUXC_GPR_BASE, MEM_AREA_IO_SEC,
			       SMALL_PAGE_SIZE);
}

static vaddr_t src_base(void)
{
	return core_mmu_get_va(SRC_M7_BASE, MEM_AREA_IO_SEC, SMALL_PAGE_SIZE);
}

static vaddr_t gpc_base(void)
{
	return core_mmu_get_va(GPC_BASE, MEM_AREA_IO_SEC, SMALL_PAGE_SIZE);
}

static TEE_Result imx_rproc_hw_start(void)
{
	/* The M7 boots from the vector table at its address 0 (ITCM) */
	io_setbits32(src_base() + SRC_M7RCR, SRC_M7RCR_ENABLE_M7);
	io_clrbits32(gpr_base() + IOMUXC_GPR22, IOMUXC_GPR22_CM7_CPUWAIT);

	return TEE_SUCCESS;
}

static TEE_Result imx_rproc_hw_stop(void)
{
	vaddr_t gpc = gpc_base();
	vaddr_t src = src_base();
	uint32_t val = 0;

	/* Hold the M7 bus accesses unless it is already in WAIT/STOP */
	if (!(io_read32(gpc + GPC_LPS_CPU1) & GPC_LPS_CPU1_LPM_MASK)) {
		io_clrbits32(gpc + GPC_M7_SLEEPHOLD, GPC_M7_SLEEPHOLDREQN);
		if (IO_READ32_POLL_TIMEOUT(gpc + GPC_LPS_CPU1, val,
					   !(val & GPC_LPS_CPU1_SLEEPHOLDACKN),
					   1, IMX_RPROC_TIMEOUT_US))
			IMSG("M7 not in WFI, forcing the stop");
	}

	io_setbits32(gpr_base() + IOMUXC_GPR22, IOMUXC_GPR22_CM7_CPUWAIT);
	io_setbits32(gpc + GPC_M7_SLEEPHOLD, GPC_M7_SLEEPHOLDREQN);

	/* Reset the M7 core and platform, the reset bits self-clear */
	io_setbits32(src + SRC_M7RCR, SRC_M7RCR_ENABLE_M7 |
		     SRC_M7RCR_SW_M7P_RST | SRC_M7RCR_SW_M7C_RST);
	if (IO_READ32_POLL_TIMEOUT(src + SRC_M7RCR, val,
				   (val & SRC_M7RCR_MASK) ==
				   SRC_M7RCR_ENABLE_M7,
				   1, IMX_RPROC_TIMEOUT_US)) {
		EMSG("M7 reset timeout, SRC_M7RCR %#"PRIx32, val);
		return TEE_ERROR_BUSY;
	}

	return TEE_SUCCESS;
}

/*
 * Keep the M7 in its wait state (GPR22 reset value) until the firmware is
 * authenticated and loaded. The M7 TCM is not accessed here: its clock is
 * enabled later, by Linux imx_rproc.
 */
static void imx_rproc_hw_init(void)
{
	io_setbits32(gpr_base() + IOMUXC_GPR22, IOMUXC_GPR22_CM7_CPUWAIT);
}

#else
#error "CFG_IMX_REMOTEPROC: unsupported SoC"
#endif

static const struct imx_rproc_mem *find_mem_by_pa(paddr_t pa, size_t size)
{
	size_t i = 0;

	for (i = 0; i < ARRAY_SIZE(imx_rproc_mems); i++)
		if (core_is_buffer_inside(pa, size, imx_rproc_mems[i].pa,
					  imx_rproc_mems[i].size))
			return imx_rproc_mems + i;

	return NULL;
}

bool imx_rproc_is_valid(uint32_t rproc_id)
{
	return rproc_id == IMX_MCU_RPROC_ID;
}

TEE_Result imx_rproc_da_to_pa(uint32_t rproc_id, paddr_t da, size_t size,
			      paddr_t *pa)
{
	size_t i = 0;

	if (!imx_rproc_is_valid(rproc_id))
		return TEE_ERROR_BAD_PARAMETERS;

	for (i = 0; i < ARRAY_SIZE(imx_rproc_mems); i++) {
		const struct imx_rproc_mem *mem = imx_rproc_mems + i;

		if (core_is_buffer_inside(da, size, mem->da, mem->size)) {
			*pa = mem->pa + da - mem->da;
			return TEE_SUCCESS;
		}
	}

	return TEE_ERROR_ACCESS_DENIED;
}

TEE_Result imx_rproc_map(uint32_t rproc_id, paddr_t pa, size_t size,
			 void **va)
{
	if (!imx_rproc_is_valid(rproc_id))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!find_mem_by_pa(pa, size))
		return TEE_ERROR_ACCESS_DENIED;

	*va = core_mmu_add_mapping(MEM_AREA_RAM_NSEC, pa, size);
	if (!*va) {
		EMSG("Can't map region %#"PRIxPA" size %zu", pa, size);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

TEE_Result imx_rproc_unmap(uint32_t rproc_id, void *va, size_t size)
{
	paddr_t pa = virt_to_phys(va);

	if (!imx_rproc_is_valid(rproc_id) || !pa)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!find_mem_by_pa(pa, size))
		return TEE_ERROR_ACCESS_DENIED;

	/* The Cortex-M does not snoop the Cortex-A caches */
	dcache_clean_range(va, size);

	if (core_mmu_remove_mapping(MEM_AREA_RAM_NSEC, va, size)) {
		EMSG("Can't unmap region %#"PRIxPA" size %zu", pa, size);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

TEE_Result imx_rproc_start(uint32_t rproc_id)
{
	if (!imx_rproc_is_valid(rproc_id))
		return TEE_ERROR_BAD_PARAMETERS;

	return imx_rproc_hw_start();
}

TEE_Result imx_rproc_stop(uint32_t rproc_id)
{
	if (!imx_rproc_is_valid(rproc_id))
		return TEE_ERROR_BAD_PARAMETERS;

	return imx_rproc_hw_stop();
}

TEE_Result imx_rproc_clean_up_memories(uint32_t rproc_id)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	void *va = NULL;
	size_t i = 0;

	if (!imx_rproc_is_valid(rproc_id))
		return TEE_ERROR_BAD_PARAMETERS;

	/* Aliases are cleared once per alias, harmless */
	for (i = 0; i < ARRAY_SIZE(imx_rproc_mems); i++) {
		const struct imx_rproc_mem *mem = imx_rproc_mems + i;

		res = imx_rproc_map(rproc_id, mem->pa, mem->size, &va);
		if (res)
			return res;

		memset(va, 0, mem->size);

		res = imx_rproc_unmap(rproc_id, va, mem->size);
		if (res)
			return res;
	}

	return TEE_SUCCESS;
}

static TEE_Result imx_rproc_init(void)
{
	size_t i = 0;

	for (i = 0; i < ARRAY_SIZE(imx_rproc_mems); i++)
		if (core_is_buffer_intersect(imx_rproc_mems[i].pa,
					     imx_rproc_mems[i].size,
					     CFG_TZDRAM_START, CFG_TZDRAM_SIZE))
			panic("Remote processor memory overlaps OP-TEE memory");

	imx_rproc_hw_init();

	/*
	 * The Cortex-M is a non-secure bus master and its memories are
	 * accessible from the normal world: the firmware is authenticated
	 * when loaded but can still be modified afterwards.
	 */
	IMSG("Warning: the remoteproc memories are not protected by firewall");

	return TEE_SUCCESS;
}

driver_init(imx_rproc_init);
