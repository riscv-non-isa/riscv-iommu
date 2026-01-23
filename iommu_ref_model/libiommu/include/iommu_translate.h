// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_TRANSLATE_H__
#define __IOMMU_TRANSLATE_H__

#ifndef RVI_IOMMU_NO_SHORT_NAMES
#define PAGESIZE    RVI_IOMMU_PAGESIZE
#define U_MODE      RVI_IOMMU_U_MODE
#define S_MODE      RVI_IOMMU_S_MODE
#define PMA         RVI_IOMMU_PMA
#define NC          RVI_IOMMU_NC
#define IO          RVI_IOMMU_IO
#endif /* RVI_IOMMU_NO_SHORT_NAMES */

#define RVI_IOMMU_PAGESIZE 4096UL
#define RVI_IOMMU_U_MODE   0
#define RVI_IOMMU_S_MODE   1
#define RVI_IOMMU_PMA      0
#define RVI_IOMMU_NC       1
#define RVI_IOMMU_IO       2
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
        uint64_t reserved:5;
        uint64_t rsw60t59b:2;
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
        uint64_t reserved:5;
        uint64_t rsw60t59b:2;
        uint64_t PBMT:2;
        uint64_t N:1;
    };
    uint64_t raw;
} gpte_t;
typedef union {
    struct {
        uint64_t V:1;
        uint64_t M:2;
        uint64_t other:60;
        uint64_t C:1;
        uint64_t upperQW:64;
    };
    struct {
        uint64_t V:1;
        uint64_t M:2;
        uint64_t reserved0:7;
        uint64_t PPN:44;
        uint64_t reserved:9;
        uint64_t C:1;
        uint64_t ignored;
    } translate_rw;
    struct {
        uint64_t V:1;
        uint64_t M:2;
        uint64_t reserved1:4;
        uint64_t MRIF_ADDR_55_9:47;
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
locate_device_context(iommu_t *iommu, device_context_t *DC, uint32_t device_id, uint8_t pid_valid,
                      uint32_t process_id, uint32_t *cause);

extern uint8_t
locate_process_context(iommu_t *iommu, process_context_t *PC, device_context_t *DC,
                       uint32_t device_id, uint32_t process_id, uint32_t *cause,
                       uint64_t *iotval2, uint8_t TTYP, uint8_t is_orig_read,
                       uint8_t is_orig_write, uint8_t is_orig_exec);

extern uint8_t
two_stage_address_translation(
    iommu_t *iommu, uint64_t iova, uint8_t TTYP, uint32_t DID, uint8_t is_read,
    uint8_t is_write, uint8_t is_exec,
    uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID,
    iosatp_t iosatp, uint8_t priv, uint8_t SUM, uint8_t SADE,
    uint8_t GV, uint32_t GSCID, iohgatp_t iohgatp, uint8_t GADE, uint8_t SXL,
    uint32_t *cause, uint64_t *iotval2, uint64_t *pa,
    uint64_t *page_sz, pte_t *vs_pte, uint32_t rcid, uint32_t mcid, uint8_t be);

extern uint8_t
second_stage_address_translation(
    iommu_t *iommu, uint64_t gpa, uint8_t check_access_perms, uint32_t DID,
    uint8_t is_read, uint8_t is_write, uint8_t is_exec, uint8_t is_implicit,
    uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID,
    uint8_t GV, uint32_t GSCID, iohgatp_t iohgatp, uint8_t GADE, uint8_t SADE, uint8_t SXL,
    uint64_t *pa, uint64_t *gst_page_sz, gpte_t *gpte, uint32_t rcid, uint32_t mcid);

extern uint8_t
msi_address_translation(
    iommu_t *iommu, uint64_t gpa, uint8_t is_exec, device_context_t *DC,
    uint8_t *is_msi, uint8_t *is_mrif, uint32_t *mrif_nid, uint64_t *dest_mrif_addr,
    uint32_t *cause, uint64_t *iotval2, uint64_t *pa,
    uint64_t *page_sz, gpte_t *g_pte, uint8_t check_access_perms, uint32_t rcid,
    uint32_t mcid);

#endif // __IOMMU_TRANSLATE_H__
