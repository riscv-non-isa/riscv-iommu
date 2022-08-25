// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
ddt_cache_t ddt_cache[2];
pdt_cache_t pdt_cache[2];
tlb_t       tlb[2];

// Cache a device context
void
cache_ioatc_dc(
    uint32_t device_id, device_context_t *DC) {
    uint8_t i, replace = 0;
    
    for ( i = 0; i < 1; i++ ) {
        if ( ddt_cache[i].valid == 0 ) {
            replace = i; 
            break;
        }
        if ( ddt_cache[i].lru == 1 ) 
            replace = i;
    }
    ddt_cache[0].lru = (replace == 0) ? 0 : 1;
    ddt_cache[1].lru = (replace == 0) ? 1 : 0;
    ddt_cache[replace].DC = *DC;
    ddt_cache[replace].DID = device_id;
    ddt_cache[replace].valid = 1;
    return;
}

// Lookup IOATC for a device context
uint8_t
lookup_ioatc_dc(
    uint32_t device_id, device_context_t *DC) {
    uint8_t i;
    for ( i = 0; i < 1; i++ ) {
        if ( ddt_cache[i].valid == 1 && ddt_cache[i].DID == device_id ) {
            *DC = ddt_cache[i].DC;
            ddt_cache[i].lru = 0;
            if ( i == 0 ) 
                ddt_cache[1].lru = 1;
            else
                ddt_cache[0].lru = 1;
            return IOATC_HIT;
        }
    }
    return IOATC_MISS;
}
// Cache a process context
void
cache_ioatc_pc(
    uint32_t device_id, uint32_t process_id, process_context_t *PC) {
    uint8_t i, replace = 0;
    
    for ( i = 0; i < 1; i++ ) {
        if ( pdt_cache[i].valid == 0 ) {
            replace = i; 
            break;
        }
        if ( pdt_cache[i].lru == 1 ) 
            replace = i;
    }
    pdt_cache[0].lru = (replace == 0) ? 0 : 1;
    pdt_cache[1].lru = (replace == 0) ? 1 : 0;
    pdt_cache[replace].PC = *PC;
    pdt_cache[replace].DID = device_id;
    pdt_cache[replace].PID = process_id;
    pdt_cache[replace].valid = 1;
    return;
}
// Lookup IOATC for a process context
uint8_t
lookup_ioatc_pc(
    uint32_t device_id, uint32_t process_id, process_context_t *PC) {
    uint8_t i;
    for ( i = 0; i < 1; i++ ) {
        if ( pdt_cache[i].valid == 1 && 
             pdt_cache[i].DID == device_id &&
             pdt_cache[i].PID == process_id ) {
            *PC = pdt_cache[i].PC;
            pdt_cache[i].lru = 0;
            if ( i == 0 ) 
                pdt_cache[1].lru = 1;
            else
                pdt_cache[0].lru = 1;
            return 1;
        }
    }
    return 0;
}
// Cache a translation in the IOATC
void
cache_ioatc_iotlb(
    uint64_t vpn, uint8_t  GV, uint8_t  PSCV, uint32_t GSCID, uint32_t PSCID,
    uint8_t  VS_R, uint8_t  VS_W, uint8_t  VS_X, uint8_t U, uint8_t  G, uint8_t VS_D, uint8_t  PBMT,
    uint8_t  G_R, uint8_t  G_W, uint8_t  G_X, uint8_t G_D,
    uint64_t PPN, uint8_t  S) {

    uint8_t i, replace = 0;

    for ( i = 0; i < 1; i++ ) {
        if ( tlb[i].valid == 0 ) {
            replace = i; 
            break;
        }
        if ( tlb[i].lru == 1 ) replace = i;
    }

    // Fill the tags
    tlb[replace].vpn   = vpn;
    tlb[replace].GV    = GV;
    tlb[replace].PSCV  = PSCV;
    tlb[replace].GSCID = GSCID;
    tlb[replace].PSCID = PSCID;
    // Fill VS stage attributes
    tlb[replace].VS_R  = VS_R;
    tlb[replace].VS_W  = VS_W;
    tlb[replace].VS_X  = VS_X;
    tlb[replace].VS_D  = VS_D;
    tlb[replace].U     = U;
    tlb[replace].G     = G;
    tlb[replace].PBMT  = PBMT;
    // Fill G stage attributes
    tlb[replace].G_R   = G_R;
    tlb[replace].G_W   = G_W;
    tlb[replace].G_X   = G_X;
    tlb[replace].G_D   = G_D;
    // PPN and size
    tlb[replace].PPN   = PPN;
    tlb[replace].S     = S;
    tlb[replace].valid = 1;
    return;
}

// Lookup a translation in the IOATC
uint8_t
lookup_ioatc_iotlb(
    uint64_t iova,
    uint8_t priv, uint8_t is_read, uint8_t is_write, uint8_t is_exec,
    uint8_t SUM, uint8_t PSCV, uint32_t PSCID, uint8_t GV, uint16_t GSCID, 
    uint32_t *cause, uint64_t *resp_pa, uint64_t *page_sz,
    uint8_t *R, uint8_t *W, uint8_t *X, uint8_t *G, uint8_t *PBMT) {

    uint8_t i, hit;
    uint64_t vpn = iova / PAGESIZE;

    hit = 0xFF;
    for ( i = 0; i < 1; i++ ) {
        if ( tlb[i].valid == 1 && 
             tlb[i].GV == GV && tlb[i].GSCID == GSCID && 
             tlb[i].PSCV == PSCV && tlb[i].PSCID == PSCID &&
             match_address_range(vpn, tlb[i].vpn, tlb[i].S) ) {
            hit = i;
            break;
        }
    }
    if ( hit == 0xFF ) return IOATC_MISS;

    // Age the entries
    tlb[0].lru = (hit == 0) ? 0 : 1;
    tlb[1].lru = (hit == 0) ? 1 : 0;

    // Check S/VS stage permissions
    if ( is_exec  && (tlb[hit].VS_X == 0) ) return IOATC_FAULT;
    if ( is_read  && (tlb[hit].VS_R == 0) ) return IOATC_FAULT;
    if ( is_write && (tlb[hit].VS_W == 0) ) return IOATC_FAULT;
    if ( (priv == U_MODE) && (tlb[hit].U == 0) ) return IOATC_FAULT;
    if ( is_exec && (priv == S_MODE) && (tlb[hit].U == 1) ) return IOATC_FAULT;
    if ( (priv == S_MODE) && !is_exec && SUM == 0 && tlb[hit].U == 1 ) return IOATC_FAULT;

    // Check G stage permissions
    if ( (is_exec  && (tlb[hit].G_X == 0)) ||
         (is_read  && (tlb[hit].G_R == 0)) ||
         (is_write && (tlb[hit].G_W == 0)) ) {
        // More commonly, implementations contain address-translation caches that 
        // map guest virtual addresses directly to supervisor physical addresses, 
        // removing a level of indirection. 
        // If a G-stage permission fault is detected then such caches may not have
        // GPA to report in the iotval2. A common technique is to treat it as a 
        // TLB miss and trigger a page walk such that the GPA can be reported if 
        // the fault is actually detected again by the G-stage page tables
        tlb[hit].valid = 0;
        return IOATC_MISS;
    }
    // If memory access is a store and VS/S or G stage D bit is 0 then mark
    // TLB entry as invalid so it returns a miss to trigger a page walk
    if ( (tlb[hit].VS_D == 0 || tlb[hit].G_D == 0) && is_write == 1 ) {
        tlb[hit].valid = 0;
        return IOATC_MISS;
    }
    *page_sz = ((tlb[hit].S == 0) ? 1 : (tlb[hit].PPN ^ (tlb[hit].PPN + 1))) + 1;
    *page_sz = *page_sz * PAGESIZE;
    *resp_pa = ((tlb[hit].PPN * PAGESIZE) & ~(*page_sz - 1)) | (iova & (*page_sz - 1));
    *R = tlb[hit].VS_R & tlb[hit].G_R;
    *W = tlb[hit].VS_W & tlb[hit].G_W;
    *X = tlb[hit].VS_X & tlb[hit].G_X;
    *PBMT = tlb[hit].PBMT;
    *G = tlb[hit].G;
    return IOATC_HIT;
}
