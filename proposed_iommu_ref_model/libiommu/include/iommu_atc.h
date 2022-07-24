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
    uint64_t iova;
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
    uint8_t  lru; 
    uint8_t  valid;
} tlb_t;
// Device directory cache
typedef struct {
    device_context_t DC;
    uint32_t         DID;
    uint8_t          lru; 
    uint8_t          valid;
} ddt_cache_t;
// Process directory cache
typedef struct {
    process_context_t PC;
    uint32_t          DID;
    uint32_t          PID;
    uint8_t           lru; 
    uint8_t           valid;
} pdt_cache_t;

// Only implemented for size = 2
#define DDT_CACHE_SIZE 2
#define PDT_CACHE_SIZE 2
#define TLB_SIZE       2

#define IOATC_MISS  0
#define IOATC_HIT   1
#define IOATC_FAULT 2

extern ddt_cache_t ddt_cache[DDT_CACHE_SIZE];
extern pdt_cache_t pdt_cache[PDT_CACHE_SIZE];
extern tlb_t       tlb[TLB_SIZE];
extern void 
cache_ioatc_iotlb(uint64_t addr, uint8_t  GV, uint8_t  PSCV, uint32_t GSCID, uint32_t PSCID,
    uint8_t  VS_R, uint8_t  VS_W, uint8_t  VS_X, uint8_t U, uint8_t  G, uint8_t  VS_D, uint8_t PBMT,
    uint8_t  G_R, uint8_t  G_W, uint8_t  G_X, uint8_t D_D, uint64_t PPN, uint8_t  S);

extern uint8_t 
lookup_ioatc_iotlb( uint64_t iova, uint8_t priv, uint8_t is_read, uint8_t is_write, uint8_t is_exec,
    uint8_t SUM, uint8_t PSCV, uint32_t PSCID, uint8_t GV, uint16_t GSCID, 
    uint32_t *cause, uint64_t *resp_pa, uint64_t *page_sz,
    uint8_t *R, uint8_t *W, uint8_t *X, uint8_t *G, uint8_t *PBMT);

extern uint8_t 
lookup_ioatc_dc(uint32_t device_id, device_context_t *DC);

extern void 
cache_ioatc_dc(uint32_t device_id, device_context_t *DC);

extern uint8_t 
lookup_ioatc_pc(uint32_t device_id, uint32_t process_id, process_context_t *PC);

extern void
cache_ioatc_pc(uint32_t device_id, uint32_t process_id, process_context_t *PC);

#endif // __IOMMU_ATC_H__

