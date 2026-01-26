// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_ATC_H__
#define __IOMMU_ATC_H__
// Contents of this file are not architectural
// IOMMU TLB
typedef struct {
    // Tags
    uint64_t vpn;
    uint8_t  GV;
    uint8_t  PSCV;
    uint32_t GSCID;
    uint32_t PSCID;
    // Attributes from VS stage page tables
    uint8_t  VS_R;
    uint8_t  VS_W;
    uint8_t  VS_X;
    uint8_t  PBMT;
    uint8_t  G;
    uint8_t  U;
    uint8_t  VS_D;
    // Attributes from G stage page tables
    uint8_t  G_R;
    uint8_t  G_W;
    uint8_t  G_X;
    uint8_t  G_D;
    // PPN and size
    uint64_t PPN;
    uint8_t  S;
    uint32_t lru;
    uint8_t  valid;
    // Whether is an MSI translation
    uint8_t  IS_MSI;
} tlb_t;
// Device directory cache
typedef struct {
    device_context_t DC;
    uint32_t         DID;
    uint32_t         lru;
    uint8_t          valid;
} ddt_cache_t;
// Process directory cache
typedef struct {
    process_context_t PC;
    uint32_t          DID;
    uint32_t          PID;
    uint32_t          lru;
    uint8_t           valid;
} pdt_cache_t;

#ifndef RVI_IOMMU_NO_SHORT_NAMES
#define DDT_CACHE_SIZE  RVI_IOMMU_DDT_CACHE_SIZE
#define PDT_CACHE_SIZE  RVI_IOMMU_PDT_CACHE_SIZE
#define TLB_SIZE        RVI_IOMMU_TLB_SIZE
#define IOATC_MISS      RVI_IOMMU_IOATC_MISS
#define IOATC_HIT       RVI_IOMMU_IOATC_HIT
#define IOATC_FAULT     RVI_IOMMU_IOATC_FAULT
#endif /* RVI_IOMMU_NO_SHORT_NAMES */

#define RVI_IOMMU_DDT_CACHE_SIZE 2
#define RVI_IOMMU_PDT_CACHE_SIZE 2
#define RVI_IOMMU_TLB_SIZE       2

#define RVI_IOMMU_IOATC_MISS  0
#define RVI_IOMMU_IOATC_HIT   1
#define RVI_IOMMU_IOATC_FAULT 2

extern void
cache_ioatc_iotlb(
    iommu_t *iommu,
    uint64_t vpn, uint8_t  GV, uint8_t  PSCV, uint32_t GSCID, uint32_t PSCID,
    pte_t *vs_pte, gpte_t *g_pte, uint64_t PPN, uint8_t S, uint8_t is_msi);

extern uint8_t
lookup_ioatc_iotlb(
    iommu_t *iommu,
    uint64_t iova, uint8_t check_access_perms,
    uint8_t priv, uint8_t is_read, uint8_t is_write, uint8_t is_exec,
    uint8_t SUM, uint8_t PSCV, uint32_t PSCID, uint8_t GV, uint16_t GSCID,
    uint32_t *cause, uint64_t *resp_pa, uint64_t *page_sz,
    pte_t *vs_pte, gpte_t *g_pteu, uint8_t *is_msi);

extern uint8_t
lookup_ioatc_dc(iommu_t *iommu, uint32_t device_id, device_context_t *DC);

extern void
cache_ioatc_dc(iommu_t *iommu, uint32_t device_id, device_context_t *DC);

extern uint8_t
lookup_ioatc_pc(iommu_t *iommu, uint32_t device_id, uint32_t process_id, process_context_t *PC);

extern void
cache_ioatc_pc(iommu_t *iommu, uint32_t device_id, uint32_t process_id, process_context_t *PC);

#endif // __IOMMU_ATC_H__
