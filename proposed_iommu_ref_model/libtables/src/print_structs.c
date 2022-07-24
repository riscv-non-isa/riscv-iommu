// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#include <inttypes.h>
#include "tables_api.h"
void
print_dev_context(
    device_context_t *DC, uint32_t device_id) {
    printf("DEVICE_CONTEXT:\n");
    printf("    DEVICE_ID : 0x%x\n", device_id);
    printf("    TC        : EN_ATS:%d, EN_PRI:%d, T2GPA:%d, DTF:%d, PDTV:%d, PRPR:%d\n",
                DC->tc.EN_ATS, DC->tc.EN_PRI, DC->tc.T2GPA, DC->tc.DTF, DC->tc.PDTV, DC->tc.PRPR);
    switch ( DC->iohgatp.MODE ) {
        case IOHGATP_Sv57x4:
            printf("    IOHGATP   : MODE:Sv57x4 GSCID:%x PPN:%"PRIx64" \n", 
                DC->iohgatp.GSCID, (uint64_t)DC->iohgatp.PPN);
        break;
        case IOHGATP_Sv48x4:
            printf("    IOHGATP   : MODE:Sv48x4 GSCID:%x PPN:%"PRIx64" \n",
                DC->iohgatp.GSCID, (uint64_t)DC->iohgatp.PPN);
        break;
        case IOHGATP_Sv39x4:
            printf("    IOHGATP   : MODE:Sv39x4 GSCID:%x PPN:%"PRIx64" \n",
                DC->iohgatp.GSCID, (uint64_t)DC->iohgatp.PPN);
        break;
        case IOHGATP_Sv32x4:
            printf("    IOHGATP   : MODE:Sv32x4 GSCID:%x PPN:%"PRIx64" \n",
                DC->iohgatp.GSCID, (uint64_t)DC->iohgatp.PPN);
        break;
        case IOHGATP_Bare:
            printf("    IOHGATP   : MODE:Bare\n");
        break;
    }
    if ( DC->tc.PDTV == 1 ) {
        switch ( DC->fsc.pdtp.MODE ) {
            case PD20:
                printf("    FSC.PDTP  : MODE:PD20, PPN:%"PRIx64"\n", (uint64_t)DC->fsc.pdtp.PPN);
            break;
            case PD17:
                printf("    FSC.PDTP  : MODE:PD17, PPN:%"PRIx64"\n", (uint64_t)DC->fsc.pdtp.PPN);
            break;
            case PD8:
                printf("    FSC.PDTP  : MODE:PD8, PPN:%"PRIx64"\n", (uint64_t)DC->fsc.pdtp.PPN);
            break;
            case PDTP_Bare:
                printf("    FSC.PDTP  : MODE:Bare\n");
            break;
        }
    } else {
        switch ( DC->fsc.iosatp.MODE ) {
            case IOSATP_Sv57:
                printf("    FSC.IOSATP: MODE:Sv57 PPN:%"PRIx64" \n", (uint64_t)DC->fsc.iosatp.PPN);
            break;
            case IOSATP_Sv48:
                printf("    FSC.IOSATP: MODE:Sv48 PPN:%"PRIx64" \n", (uint64_t)DC->fsc.iosatp.PPN);
            break;
            case IOSATP_Sv39:
                printf("    FSC.IOSATP: MODE:Sv39 PPN:%"PRIx64" \n", (uint64_t)DC->fsc.iosatp.PPN);
            break;
            case IOSATP_Sv32:
                printf("    FSC.IOSATP: MODE:Sv32 PPN:%"PRIx64" \n", (uint64_t)DC->fsc.iosatp.PPN);
            break;
            case IOSATP_Bare:
                printf("    FSC.IOSATP: MODE:Bare\n");
            break;
        }
    }
    printf("    TA        :  PSCID:%x\n", DC->ta.PSCID);
    printf("    MSIPTP    :  MODE:%d, PPN:%"PRIx64"\n", DC->msiptp.MODE, (uint64_t)DC->msiptp.PPN);
    printf("    MSI       :  MASK:%"PRIx64"\n", DC->msi_addr_mask);
    printf("    MSI       :  PAT:%"PRIx64"\n", DC->msi_addr_pattern);
}
void
print_process_context(
    process_context_t *PC, uint32_t device_id, uint32_t process_id) {
    printf("PROCESS_CONTEXT:\n");
    printf("       DID, PID: 0x%x 0x%x\n", device_id, process_id);
    switch ( PC->fsc.iosatp.MODE ) {
        case IOSATP_Sv57:
            printf("    FSC.IOSATP: MODE:Sv57 PPN:%"PRIx64" \n", (uint64_t)PC->fsc.iosatp.PPN);
        break;
        case IOSATP_Sv48:
            printf("    FSC.IOSATP: MODE:Sv48 PPN:%"PRIx64" \n", (uint64_t)PC->fsc.iosatp.PPN);
        break;
        case IOSATP_Sv39:
            printf("    FSC.IOSATP: MODE:Sv39 PPN:%"PRIx64" \n", (uint64_t)PC->fsc.iosatp.PPN);
        break;
        case IOSATP_Sv32:
            printf("    FSC.IOSATP: MODE:Sv32 PPN:%"PRIx64" \n", (uint64_t)PC->fsc.iosatp.PPN);
        break;
        case IOSATP_Bare:
            printf("    FSC.IOSATP: MODE:Bare\n");
        break;
    }
    printf("    TA        :  PSCID:%x, ENS:%d, SUM:%d\n", PC->ta.PSCID, PC->ta.ENS, PC->ta.SUM);
}
