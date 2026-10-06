/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026, Joris Offouga
 */

#ifndef __DRIVERS_IMX_REMOTEPROC_H
#define __DRIVERS_IMX_REMOTEPROC_H

#include <stdbool.h>
#include <stdint.h>
#include <tee_api_types.h>
#include <types_ext.h>

/* ID of the Cortex-M remote processor (M4 on i.MX7, M7 on i.MX8MP) */
#define IMX_MCU_RPROC_ID 0

/*
 * imx_rproc_is_valid() - Tell whether a remote processor ID is supported
 * @rproc_id: unique identifier of the remote processor
 */
bool imx_rproc_is_valid(uint32_t rproc_id);

/*
 * imx_rproc_da_to_pa() - Convert a remote processor device address into the
 * physical address seen by the Cortex-A cores
 * @rproc_id: unique identifier of the remote processor
 * @da: device address, from the remote processor memory map
 * @size: byte size of the memory area
 * @pa: output physical address
 */
TEE_Result imx_rproc_da_to_pa(uint32_t rproc_id, paddr_t da, size_t size,
			      paddr_t *pa);

/*
 * imx_rproc_map() - Map a remote processor memory area in OP-TEE
 * @rproc_id: unique identifier of the remote processor
 * @pa: physical address of the memory area
 * @size: byte size of the memory area
 * @va: output virtual address
 */
TEE_Result imx_rproc_map(uint32_t rproc_id, paddr_t pa, size_t size,
			 void **va);

/*
 * imx_rproc_unmap() - Clean the data cache and unmap a memory area mapped
 * with imx_rproc_map()
 * @rproc_id: unique identifier of the remote processor
 * @va: virtual address of the memory area
 * @size: byte size of the memory area
 */
TEE_Result imx_rproc_unmap(uint32_t rproc_id, void *va, size_t size);

/*
 * imx_rproc_start() - Release the remote processor from its wait state
 * @rproc_id: unique identifier of the remote processor
 */
TEE_Result imx_rproc_start(uint32_t rproc_id);

/*
 * imx_rproc_stop() - Stop and reset the remote processor
 * @rproc_id: unique identifier of the remote processor
 */
TEE_Result imx_rproc_stop(uint32_t rproc_id);

/*
 * imx_rproc_clean_up_memories() - Clear the remote processor memories
 * @rproc_id: unique identifier of the remote processor
 */
TEE_Result imx_rproc_clean_up_memories(uint32_t rproc_id);

#endif /* __DRIVERS_IMX_REMOTEPROC_H */
