// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"

void 
report_fault(uint16_t cause, uint64_t iotval, uint64_t iotval2, uint8_t TTYP, uint8_t dtf,
             uint32_t device_id, uint8_t pid_valid, uint32_t process_id, uint8_t priv_req) {
    fault_rec_t frec;
    uint32_t fqh;
    uint32_t fqt;
    uint64_t fqb;
    uint64_t frec_addr;
    uint8_t status;

    // The fault-queue enable bit enables the fault-queue when set to 1. 
    // The fault-queue is active if fqon reads 1. 
    if ( g_reg_file.fqcsr.fqon == 0 || g_reg_file.fqcsr.fqen == 0 )
        return;

    // The fqmf bit is set to 1 if the IOMMU encounters an access fault
    // when storing a fault record to the fault queue. The fault-record that
    // was attempted to be written is discarded and no more fault records
    // are generated until software clears fqmf bit by writing 1 to the bit.
    // An interrupt is generated if enabled and not already pending (i.e.
    // ipsr.fip == 1) and not masked (i.e. fqsr.fie == 0).
    if ( g_reg_file.fqcsr.fqmf == 1 )
        return;

    // The fault-queue-overflow bit is set to 1 if the IOMMU needs to
    // queue a fault record but the fault-queue is full (i.e., fqt == fqh - 1)
    // The fault-record is discarded and no more fault records are
    // generated till software clears fqof by writing 1 to the bit. An
    // interrupt is generated if not already pending (i.e. ipsr.fip == 1)
    // and not masked (i.e. fqsr.fie == 0)
    if ( g_reg_file.fqcsr.fqof == 1 )
        return;

    // Setting the disable-translation-fault - DTF - bit to 1 disables reporting of 
    // faults encountered in the address translation process. Setting DTF to 1 does not 
    // disable error responses from being generated to the device in response to faulting 
    // transactions. Setting DTF to 1 does not disable reporting of faults from the IOMMU 
    // that are not related to the address translation process. The faults that are not
    // reported when DTF is 1 are listed in Table 8
    // |CAUSE | Description                         | Reported if `DTF` is 1?
    // |0     | Instruction address misaligned      | No
    // |1     | Instruction access fault            | No
    // |4     | Read address misaligned             | No
    // |5     | Read access fault                   | No
    // |6     | Write/AMO address misaligned        | No
    // |7     | Write/AMO access fault              | No
    // |12    | Instruction page fault              | No
    // |13    | Read page fault                     | No
    // |15    | Write/AMO page fault                | No
    // |20    | Instruction guest page fault        | No
    // |21    | Read guest-page fault               | No
    // |23    | Write/AMO guest-page fault          | No
    // |256   | All inbound transactions disallowed | Yes
    // |257   | DDT entry load access fault         | Yes
    // |258   | DDT entry not valid                 | Yes
    // |259   | DDT entry misconfigured             | Yes
    // |260   | Transaction type disallowed         | No
    // |261   | MSI PTE load access fault           | No
    // |262   | MSI PTE not valid                   | No
    // |263   | MSI PTE misconfigured               | No
    // |264   | MRIF access fault                   | No
    // |265   | PDT entry load access fault         | No
    // |266   | PDT entry not valid                 | No
    // |267   | PDT entry misconfigured             | No
    // |268   | DDT data corruption                 | Yes
    // |269   | PDT data corruption                 | No
    // |270   | MSI PT data corruption              | No
    // |271   | MSI MRIF data corruption            | No
    // |272   | Internal datapath error             | Yes
    // |273   | IOMMU MSI write access fault        | Yes
    // |274   | S/VS/G-stage PT data corruption     | No
    // The `CAUSE` encodings 274 through 2047 are reserved for future standard use and
    // the encodings 2048 through 4095 are designated for custom use.
    if ( (dtf == 1) && (cause != 256) &&  (cause != 257) && 
         (cause != 258) && (cause != 259) && (cause != 268) && 
         (cause != 272) && (cause != 273) ) {
        return;
    }

    // DID holds the device_id of the transaction. 
    // If PV is 0, then PID and PRIV are 0. If PV is 1, the PID
    // holds a process_id of the transaction and if the privilege 
    // of the transaction was Supervisor then PRIV bit is 1 else its 0. 
    frec.DID = device_id;
    if ( pid_valid ) {
        frec.PID = process_id;
        frec.PV = pid_valid;
        frec.PRIV = priv_req;
    } else {
        frec.PID = 0;
        frec.PV = 0;
        frec.PRIV = 0;
    }
    frec.reserved = 0;
    frec.custom = 0;
    frec.iotval = iotval;
    frec.iotval2 = iotval2;
    frec.TTYP = TTYP;
    frec.CAUSE = cause;

    // Fault/Event queue is an in-memory queue data structure used to report events
    // and faults raised when processing transactions. Each fault record is 32 bytes.
    // The PPN of the base of this in-memory queue and the size of the queue is 
    // configured into a memorymapped register called fault-queue base (fqb).
    // The tail of the fault-queue resides in a IOMMU controlled read-only 
    // memory-mapped register called fqt. The fqt is an index into the next fault 
    // record that IOMMU will write in the fault-queue.
    // Subsequent to writing the record, the IOMMU advances the fqt by 1. The head of
    // the fault-queue resides in a read/write memory-mapped software controlled 
    // register called fqh. The fqh is an index into the next fault record that SW 
    // should process next. Subsequent to processing fault record(s) software advances
    // the fqh by the count of the number of fault records processed. If fqh == fqt, the
    // fault-queue is empty. If fqt == (fqh - 1) the fault-queue is full.
    fqh = g_reg_file.fqh.index;
    fqt = g_reg_file.fqt.index;
    fqb = g_reg_file.fqb.ppn;
    if ( ((fqt + 1) & ((1UL << (g_reg_file.fqb.log2szm1 + 1)) - 1)) == fqh ) {
        g_reg_file.fqcsr.fqof = 1;
        generate_interrupt(FAULT_QUEUE);
        return;
    }
    // The IOMMU may be unable to report faults through the fault-queue due to error 
    // conditions such as the fault-queue being full or the IOMMU encountering access 
    // faults when attempting to access the queue memory. A memory-mapped fault control 
    // and status register (fqcsr) holds information about such faults. If the fault-queue
    // full condition is detected the IOMMU sets a fault-queue overflow (fqof)
    // bit in fqcsr. If the IOMMU encounters a fault in accessing the fault-queue memory, 
    // the IOMMU sets a fault-queue memory access fault (fqmf) bit in fqcsr. While either
    // error bits are set in fqcsr, the IOMMU discards the record that led to the fault
    // and all further fault records. When an error bit is in the fqcsr changes state 
    // from 0 to 1 or when a new fault record is produced in the fault-queue, fault
    // interrupt pending (fip) bit is set in the fqcsr.
    frec_addr = ((fqb * 4096) | (fqt * 32));
    status = write_memory((char *)&frec, frec_addr, 32);
    if ( (status & ACCESS_FAULT) || (status & DATA_CORRUPTION) ) {
        g_reg_file.fqcsr.fqmf = 1;
    } else {
        fqt = (fqt + 1) & ((1UL << (g_reg_file.fqb.log2szm1 + 1)) - 1);
        g_reg_file.fqt.index = fqt;
    }
    generate_interrupt(FAULT_QUEUE);
    return;
}
