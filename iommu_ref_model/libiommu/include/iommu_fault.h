// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_FAULT_H__
#define __IOMMU_FAULT_H__

// The TTYP field reports inbound transaction type
// Fault record `TTYP` field encodings
// |TTYP   | Description
// |0      | None. Fault not caused by an inbound transaction.
// |1      | Untranslated read for execute transaction
// |2      | Untranslated read transaction
// |3      | Untranslated write/AMO transaction
// |4      | Reserved
// |5      | Translated read for execute transaction
// |6      | Translated read transaction
// |7      | Translated write/AMO transaction
// |8      | PCIe ATS Translation Request
// |9      | Message Request
// |10 - 31| Reserved
// |31 - 63| Reserved for custom use
#define TTYPE_NONE                                0
#define UNTRANSLATED_READ_FOR_EXECUTE_TRANSACTION 1
#define UNTRANSLATED_READ_TRANSACTION             2
#define UNTRANSLATED_WRITE_AMO_TRANSACTION        3
#define TRANSLATED_READ_FOR_EXECUTE_TRANSACTION   5
#define TRANSLATED_READ_TRANSACTION               6
#define TRANSLATED_WRITE_AMO_TRANSACTION          7
#define PCIE_ATS_TRANSLATION_REQUEST              8
#define PCIE_MESSAGE_REQUEST                      9


// Fault-queue record
// bits:    11:0: 'CAUSE'
// bits:   31:12: 'PID'
// bits:      32: 'PV'
// bits:      33: 'PRIV'
// bits:   39:34: 'TTYP'
// bits:   63:40: 'DID'
// bits:   95:64: 'for custom use'
// bits:  127:96: 'reserved'
// bits: 191:128: 'iotval'
// bits: 255:192: 'iotval2'
typedef union {
    struct {
        uint64_t CAUSE:12;
        uint64_t PID:20;
        uint64_t PV:1;
        uint64_t PRIV:1;
        uint64_t TTYP:6;
        uint64_t DID:24;
        uint32_t custom;
        uint32_t reserved;
        uint64_t iotval;
        uint64_t iotval2;
    };
    uint64_t raw[4];
} fault_rec_t;
#define ACCESS_FAULT    0x01
#define DATA_CORRUPTION 0x02

#define GST_PAGE_FAULT       0x21
#define GST_ACCESS_FAULT     0x22
#define GST_DATA_CORRUPTION  0x23

#define FQ_ENTRY_SZ sizeof(fault_rec_t)

extern void report_fault(uint16_t cause, uint64_t iotval, uint64_t iotval2, uint8_t TTYP, uint8_t dtf,
                  uint32_t device_id, uint8_t pid_valid, uint32_t process_id, uint8_t priv_req);
#endif // __IOMMU_FAULT_H__
