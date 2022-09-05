// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_TRANSLATE_H__
#define __IOMMU_TRANSLATE_H__

#define PAGESIZE 4096UL
#define U_MODE   0
#define S_MODE   1
#define PMA      0
#define NC       1
#define IO       2
typedef union {
    struct {
        uint64_t V:1;
        uint64_t R:1;
        uint64_t W:1;
        uint64_t X:1;
        uint64_t U:1;
        uint64_t G:1;
        uint64_t A:1;
        uint64_t D:1;
        uint64_t RSW:2;
        uint64_t PPN:44;
        uint64_t reserved:7;
        uint64_t PBMT:2;
        uint64_t N:1;
    };
    uint64_t raw;
} pte_t;
typedef union {
    struct {
        uint64_t V:1;
        uint64_t R:1;
        uint64_t W:1;
        uint64_t X:1;
        uint64_t U:1;
        uint64_t G:1;
        uint64_t A:1;
        uint64_t D:1;
        uint64_t RSW:2;
        uint64_t PPN:44;
        uint64_t reserved:7;
        uint64_t PBMT:2;
        uint64_t N:1;
    };
    uint64_t raw;
} gpte_t;
typedef union {
    struct {
        uint64_t V:1;
        uint64_t reserved0:1;
        uint64_t W:1;
        uint64_t other:60;
        uint64_t C:1;
        uint64_t upperQW:64;
    };
    struct {
        uint64_t V:1;
        uint64_t reserved0:1;
        uint64_t W:1;
        uint64_t PPN:44;
        uint64_t reserved1:9;
        uint64_t C:1;
        uint64_t ignored;
    } write_through;
    struct {
        uint64_t V:1;
        uint64_t reserved0:1;
        uint64_t W:1;
        uint64_t reserved1:4;
        uint64_t MRIF_ADDR:47;
        uint64_t reserved2:9;
        uint64_t C:1;
        uint64_t N90:10;
        uint64_t NPPN:44;
        uint64_t reserved3:6;
        uint64_t N10:1;
        uint64_t reserved4:3;
    } mrif;
    uint64_t raw[2];
} msipte_t;

extern uint8_t 
locate_device_context(device_context_t *DC, uint32_t device_id, uint8_t pid_valid, 
                      uint32_t process_id, uint32_t *cause);

extern uint8_t 
locate_process_context(process_context_t *PC, device_context_t *DC, 
                       uint32_t device_id, uint32_t process_id, uint32_t *cause, 
                       uint64_t *iotval2, uint8_t TTYP);

extern uint8_t
s_vs_stage_address_translation(
    uint64_t iova,
    uint8_t priv, uint8_t is_read, uint8_t is_write, uint8_t is_exec,
    uint8_t SUM, iosatp_t iosatp, uint32_t PSCID, iohgatp_t iohgatp, 
    uint32_t *cause, uint64_t *iotval2, uint64_t *resp_pa, uint64_t *page_sz,
    uint8_t *R, uint8_t *W, uint8_t *X, uint8_t *G, uint8_t *PBMT, uint8_t *UNTRANSLATED_ONLY,
    uint8_t pid_valid, uint32_t process_id, uint32_t device_id, uint8_t TTYP, uint8_t T2GPA,
    uint8_t SADE, uint8_t GADE);

extern uint8_t
g_stage_address_translation(
    uint64_t gpa, uint8_t is_read, uint8_t is_write, uint8_t is_exec, uint8_t implicit,
    iohgatp_t iohgatp, uint32_t *cause, uint64_t *iotval2, 
    uint64_t *resp_pa, uint64_t *gst_page_sz,
    uint8_t *GR, uint8_t *GW, uint8_t *GX, uint8_t *GD, uint8_t *GPBMT,
    uint8_t pid_valid, uint32_t process_id, uint8_t PSCV, uint32_t PSCID, uint32_t device_id,
    uint8_t GV, uint32_t GSCID, uint8_t TTYP, uint8_t GADE);

extern uint8_t
msi_address_translation(
    uint64_t iova, uint32_t msi_write_data, addr_type_t at, device_context_t *DC,
    uint32_t *cause, uint64_t *resp_pa, uint8_t *R, uint8_t *W, uint8_t *U, 
    uint8_t *is_msi, uint8_t *is_unsup, uint8_t *is_mrif_wr, uint32_t *mrif_nid,
    uint8_t pid_valid, uint32_t process_id, uint8_t PSCV, uint32_t PSCID, uint32_t device_id,
    uint8_t GV, uint32_t GSCID);

#endif // __IOMMU_TRANSLATE_H__
