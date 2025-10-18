// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_H__
#define __IOMMU_H__
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct iommu_t iommu_t;

#include "iommu_registers.h"
#include "iommu_data_structures.h"
#include "iommu_req_rsp.h"
#include "iommu_fault.h"
#include "iommu_translate.h"
#include "iommu_utils.h"
#include "iommu_interrupt.h"
#include "iommu_command_queue.h"
#include "iommu_ats.h"
#include "iommu_atc.h"
#include "iommu_hpm.h"
#include "iommu_ref_api.h"

typedef struct iommu_t {
    // from iommu_command_queue.c
    uint8_t command_queue_stall_for_itag;
    uint8_t ats_inv_req_timeout;
    uint8_t iofence_wait_pending_inv;
    uint8_t iofence_pending_PR, iofence_pending_PW, iofence_pending_AV, iofence_pending_WSI_BIT;
    uint64_t iofence_pending_ADDR;
    uint32_t iofence_pending_DATA;

    uint8_t pending_inval_req_DSV;
    uint8_t pending_inval_req_DSEG;
    uint16_t pending_inval_req_RID;
    uint8_t pending_inval_req_PV;
    uint32_t pending_inval_req_PID;
    uint64_t pending_inval_req_PAYLOAD;

    // iommu_reg.c
    // IOMMU register file
    iommu_regs_t reg_file;
    // Register offset to size mapping
    uint8_t offset_to_size[4096];
    // Global parameters of the design
    uint8_t num_hpm;
    uint8_t hpmctr_bits;
    uint8_t eventID_limit;
    uint8_t num_vec_bits;
    uint8_t gxl_writeable;
    uint8_t fctl_be_writeable;
    uint8_t max_iommu_mode;
    uint8_t fill_ats_trans_in_ioatc;
    uint32_t max_devid_mask;
    uint8_t trans_for_debug;
    uint64_t sv57_bare_pg_sz;
    uint64_t sv48_bare_pg_sz;
    uint64_t sv39_bare_pg_sz;
    uint64_t sv32_bare_pg_sz;
    uint64_t sv57x4_bare_pg_sz;
    uint64_t sv48x4_bare_pg_sz;
    uint64_t sv39x4_bare_pg_sz;
    uint64_t sv32x4_bare_pg_sz;
    iommu_qosid_t iommu_qosid_mask;

    // from iommu_atc.
    ddt_cache_t ddt_cache[DDT_CACHE_SIZE];
    pdt_cache_t pdt_cache[PDT_CACHE_SIZE];
    tlb_t       tlb[TLB_SIZE];
    uint32_t    dc_lru_time;
    uint32_t    pc_lru_time;
    uint32_t    tlb_lru_time;

    itag_tracker_t itag_tracker[MAX_ITAGS];
    uint8_t msi_pending[16];
} iommu_t;

#endif
