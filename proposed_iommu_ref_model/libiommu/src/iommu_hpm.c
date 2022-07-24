// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
void
count_events(
    uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID, 
    uint8_t DID, uint8_t GSCV, uint32_t GSCID, uint16_t eventID) {
    uint8_t i;
    uint32_t mask;
    uint64_t count;

    // IOMMU implements a performance-monitoring unit
    // if capabilities.hpm == 1
    if ( g_reg_file.capabilities.hpm == 0 ) return;

    for ( i = 0; i < g_num_hpm; i++ ) {
        // The performance-monitoring counter inhibits is a 32-bits WARL 
        // register where that contains bits to inhibit the corresponding 
        // counters from counting. Bit X when set inhibits counting in 
        // iohpmctrX and bit 0 inhibits counting in iohpmcycles.
        if ( g_reg_file.iocountinh.raw & (1UL << i) ) continue;

        // Counter is not inhibited check if it matches
        // These performance-monitoring event registers are 64-bit RW 
        // registers. When a transaction processed by the IOMMU causes an 
        // event that is programmed to count in a counter then the counter is
        // incremented. In addition to matching events the event selector may 
        // be programmed with additional filters based on device_id, process_id, 
        // GSCID, and PSCID such that the counter is incremented conditionally 
        // based on the transaction matching these additional filters. When such 
        // device_id based filtering is used, the match may be configured to be 
        // a precise match or a partial match. A partial match allows a 
        // transactions with a range of IDs to be counted by the counter.
        if ( g_reg_file.iohpmevt[i].eventID != eventID ) continue;

        // When filtering by device_id or GSCID is selected and the event supports 
        // ID based filtering, the DMASK field can be used to configure a partial 
        // match. When DMASK is set to 1, partial matching of the DID_GSCID is 
        // performed for the transaction. The lower bits of the DID_GSCID all the 
        // way to the first low order 0 bit (including the 0 bit position itself) 
        // are masked.
        // The following example illustrates the use of DMASK and filtering by `device_id`.
        // | *DMASK* | *DID_GSCID*                  | *Comment*
        // | 0       | yyyyyyyy  yyyyyyyy  yyyyyyyy | One specific seg:bus:dev:func
        // | 1       | yyyyyyyy  yyyyyyyy  yyyyy011 | seg:bus:dev - any func
        // | 1       | yyyyyyyy  yyyyyyyy  01111111 | seg:bus - any dev:func
        // | 1       | yyyyyyyy  01111111  11111111 | seg - any bus:dev:func
        mask = g_reg_file.iohpmevt[i].did_gscid + 1;
        mask = mask ^ g_reg_file.iohpmevt[i].did_gscid;
        mask = ~mask;
        // If DMASK is 0, then all 24-bits must match
        mask = (g_reg_file.iohpmevt[i].dmask == 1) ? mask : 0xFFFFFF;

        // IDT - Filter ID Type: This field indicates the type of ID to
        // filter on. 
        if ( g_reg_file.iohpmevt[i].idt == 0 ) {
            // When 0, the DID_GSCID field holds a device_id and the 
            // PID_PSCID field holds a process_id. 
            if ( g_reg_file.iohpmevt[i].pv_pscv == 1 ) {
                if ( PV == 0 ) continue;
                if ( g_reg_file.iohpmevt[i].pid_pscid != PID ) continue;
            }
            if ( g_reg_file.iohpmevt[i].dv_gscv == 1 ) {
                if ( (g_reg_file.iohpmevt[i].did_gscid & mask) != (DID & mask) ) {
                    continue;
                }
            }
        }
        if ( g_reg_file.iohpmevt[i].idt == 1 ) {
            // When 1, the DID_GSCID field holds a GSCID and PID_PSCID 
            // field holds a PSCID.
            if ( g_reg_file.iohpmevt[i].pv_pscv == 1 ) {
                if ( PSCV == 0 ) continue;
                if ( g_reg_file.iohpmevt[i].pid_pscid != PSCID ) continue;
            }
            if ( g_reg_file.iohpmevt[i].dv_gscv == 1 ) {
                if ( GSCV == 0 ) continue;
                if ( (g_reg_file.iohpmevt[i].did_gscid & mask) != (GSCID & mask) ) continue;
            }
        }
        // Counter is not inhibited and all filters pass
        count = g_reg_file.iohpmctr[i].counter + 1;
        g_reg_file.iohpmctr[i].counter = (count & ((1UL << g_hpmctr_bits) - 1));
        if ( count & (1UL << g_hpmctr_bits) ) {
            // The OF bit is set when the corresponding iohpmctr* overflows, 
            // and remains set until cleared by software. Since iohpmctr* 
            // values are unsigned values, overflow is defined as unsigned 
            // overflow. Note that there is no loss of information after an 
            // overflow since the counter wraps around and keeps counting 
            // while the sticky OF bit remains set. If an iohpmctr* overflows 
            // while the associated OF bit is zero, then a HPM Counter Overflow 
            // interrupt is generated. If the OF bit is one, then no interrupt 
            // request is generated. Consequently the OF bit also functions as 
            // a count overflow interrupt disable for the associated iohpmctr*.
            // A pending HPM Counter Overflow interrupt (OR of all iohpmctr* 
            // overflows) is and reported through ipsr register.
            if ( g_reg_file.iohpmevt[i].of == 0 ) {
                g_reg_file.iohpmevt[i].of = 1;
                generate_interrupt(HPM);
            }
        }
    }
    return;
}
