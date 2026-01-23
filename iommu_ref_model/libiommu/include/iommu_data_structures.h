// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_DATA_STRUCTURES_H__
#define __IOMMU_DATA_STRUCTURES_H__

#ifndef RVI_IOMMU_NO_SHORT_NAMES
#define IOHGATP_Bare        RVI_IOMMU_IOHGATP_Bare
#define IOHGATP_Sv32x4      RVI_IOMMU_IOHGATP_Sv32x4
#define IOHGATP_Sv39x4      RVI_IOMMU_IOHGATP_Sv39x4
#define IOHGATP_Sv48x4      RVI_IOMMU_IOHGATP_Sv48x4
#define IOHGATP_Sv57x4      RVI_IOMMU_IOHGATP_Sv57x4
#define IOSATP_Bare         RVI_IOMMU_IOSATP_Bare
#define IOSATP_Sv32         RVI_IOMMU_IOSATP_Sv32
#define IOSATP_Sv39         RVI_IOMMU_IOSATP_Sv39
#define IOSATP_Sv48         RVI_IOMMU_IOSATP_Sv48
#define IOSATP_Sv57         RVI_IOMMU_IOSATP_Sv57
#define PDTP_Bare           RVI_IOMMU_PDTP_Bare
#define PD8                 RVI_IOMMU_PD8
#define PD17                RVI_IOMMU_PD17
#define PD20                RVI_IOMMU_PD20
#define MSIPTP_Off          RVI_IOMMU_MSIPTP_Off
#define MSIPTP_Flat         RVI_IOMMU_MSIPTP_Flat
#define BASE_FORMAT_DC_SIZE RVI_IOMMU_BASE_FORMAT_DC_SIZE
#define EXT_FORMAT_DC_SIZE  RVI_IOMMU_EXT_FORMAT_DC_SIZE
#endif /* RVI_IOMMU_NO_SHORT_NAMES */

// Translation control (`tc`) field
typedef union {
    struct {
        // `DC` is valid if the `V` bit is 1; If it is 0, all other bits in `DC` are
        // don't-care and may be freely used by software.
        uint64_t V:1;

        // If the IOMMU supports PCIe ATS specification (see `capabilities` register),
        // the `EN_ATS` bit is used to enable ATS transaction processing. If `EN_ATS`
        // is set to 1, IOMMU supports the following inbound transactions; otherwise
        // they are treated as unsupported transactions.
        // * Translated read for execute transaction
        // * Translated read transaction
        // * Translated write/AMO transaction
        // * PCIe ATS Translation Request
        // * PCIe ATS Invalidation Completion Message
        uint64_t EN_ATS:1;

        // If `EN_PRI` bit is 0, then PCIe "Page Request" messages from the device are
        // invalid requests. A "Page Request" message received from a device is responded to
        // with a "Page Request Group Response" message. Normally, a software handler
        // generates this response message. However, under some conditions the IOMMU itself
        // may generate a response.
        uint64_t EN_PRI:1;

        // If the `EN_ATS` bit is 1 and the `T2GPA` bit is set to 1 the IOMMU returns a GPA
        // , instead of a SPA, as the translation of an IOVA in response to a  PCIe ATS
        // Translation Request from the device.  In this mode of operations, the ATC in the
        // device caches a GPA as a translation for an IOVA and uses the GPA as the address
        // in subsequent translated memory access transactions. Usually translated requests
        // use a SPA and need no further translation to be performed by the IOMMU. However
        // when `T2GPA` is 1, translated requests from a device use a GPA and are
        // translated by the IOMMU using the G-stage page table to a SPA. The `T2GPA`
        // control enables a hypervisor to contain DMA from a device, even if the device
        // misuses the ATS capability and attempts to access memory that is not associated
        // with the VM.
        uint64_t T2GPA:1;

        // Setting the disable-translation-fault - `DTF` - bit to 1 disables reporting of
        // faults encountered in the address translation process. Setting `DTF` to 1 does
        // not disable error responses from being generated to the device in response to
        // faulting transactions. Setting `DTF` to 1 does not disable reporting of faults
        // from the IOMMU that are not related to the address translation process. The
        // faults that are not reported when `DTF` is 1 are listed in <<FAULT_CAUSE>>.
        uint64_t DTF:1;

        // The `fsc` field of `DC` holds the context for first-stage translations (S-stage
        // or VS-stage). If the `PDTV` bit is 1, the field holds the PPN of the root page
        // of PDT.  If the `PDTV` bit is 0 and `iohgatp.MODE` is `Bare`, the `fsc` field
        // holds the PPN of the root page of a S-stage page table (i.e. `iosatp`).
        // if the `PDTV` bit is 0 and `iohgatp.MODE` is not `Bare`, the `fsc` field holds
        // the PPN of the root page of a VS-stage page table (i.e. `iovsatp`).
        // The `PDTV` is expected to be set to 1 when `DC` is associated with a device
        // that supports multiple process contexts and thus generates a valid `process_id`
        // with its memory accesses. For PCIe, for example, if the request has a PASID
        // then the PASID is used as the `process_id`.
        uint64_t PDTV:1;

        // For IOMMU generated "Page Request Group Response"
        // messages the PRG-response-PASID-required (`PRPR`) bit when set to 1 indicates
        // that the IOMMU response message should include a PASID if the associated
        // "Page Request" had a PASID.
        uint64_t PRPR:1;

        // The IOMMU supports the 1 setting of GADE and SADE bits if capabilities.AMO is 1.
        // When capabilities.AMO is 0, these bits are reserved. If GADE is 1, the IOMMU
        // updates A and D bits in G-stage PTEs atomically. If GADE is 0, the IOMMU causes
        // a guest-page-fault corresponding to the original access type if A bit is 0 or if
        // the memory access is a store and the D bit is 0.
        uint64_t GADE:1;

        // If SADE is 1, the IOMMU updates A and D bits in S/VS-stage PTEs atomically. If
        // SADE is 0, the IOMMU causes a page-fault corresponding to the original access type
        // if A bit is 0 or if the memory access is a store and the D bit is 0.
        uint64_t SADE:1;

        // When PDTV is 1, the DPE bit may set to 1 to enable the use of 0 as the default
        // value of process_id for translating requests without a valid process_id. When PDTV is
        // 0, the DPE bit is reserved for future standard extension.
        uint64_t DPE:1;

        // If SBE is 0, implicit memory accesses to PDT entries and S/VS-stage PTEs are
        // little-endian else they are big-endian. The supported values of SBE are the same as
        // that of the fctl.BE field.
        uint64_t SBE:1;

        // The SXL field controls the supported paged virtual-memory schemes as defined in
        // Table 3. If fctl.GXL is 1 then SXL field must be 1; otherwise the legal values for the
        // SXL field are the same as that of the fctl.GXL.
        // When SXL is 1, the following rules apply:
        // • If the S/VS-stage page table is not Bare then a page fault corresponding to
        //   the original access type occurs if the IOVA has bits set beyond bit 31.
        // • If the G-stage page table is not Bare, then a guest page fault corresponding
        //   to the original access type occurs if the incoming GPA has bits set beyond bit 33.
        uint64_t SXL:1;

        uint64_t reserved0:12;
        uint64_t custom:8;
        uint64_t reserved1:32;
    };
    uint64_t raw;
} tc_t;

// The `iohgatp` field holds the PPN of the root G-stage page table and a
// virtual machine identified by a guest soft-context ID (`GSCID`), to facilitate
// address-translation fences on a per-virtual-machine basis. If multiple devices
// are associated to a VM with a common G-stage page table, the hypervisor is
// expected to program the same `GSCID` in each `iohgatp`. The `MODE` field is used
// to select the G-stage address translation scheme.

// The G-stage page table format and `MODE` encoding follow the format defined by
// the privileged specification.
#define RVI_IOMMU_IOHGATP_Bare         0
#define RVI_IOMMU_IOHGATP_Sv32x4       8
#define RVI_IOMMU_IOHGATP_Sv39x4       8
#define RVI_IOMMU_IOHGATP_Sv48x4       9
#define RVI_IOMMU_IOHGATP_Sv57x4       10
// Implementations are not required to support all defined mode settings for
// `iohgatp`. The IOMMU only needs to support the modes also supported by the MMU
// in the harts integrated into the system or a subset thereof.
typedef union {
    struct {
        uint64_t PPN:44;
        uint64_t GSCID:16;
        uint64_t MODE:4;
    };
    uint64_t raw;
} iohgatp_t;

// Translation attributes
typedef union {
    struct {
        // The `PSCID` field of `ta` provides the process soft-context ID that identifies
        // the address-space of the process. `PSCID` facilitates address-translation
        // fences on a per-address-space basis. The `PSCID` field in `ta` is used as the
        // address-space ID if `PDTV` is 0 and the `iosatp`/`iovsatp` `MODE` field is not
        // `Bare`.
        uint64_t reserved0:12;
        uint64_t PSCID:20;
        uint64_t reserved1:8;
        // The RCID and MCID fields are added by the QoS ID extension. If
        // capabilities.QOSID is 0, these bits are reserved and must be set to 0.
        // IOMMU-initiated requests for accessing the following data structures
        // use the value configured in the RCID and MCID fields of DC.ta.
        // - Process directory table (PDT)
        // - Second-stage page table
        // - First-stage page table
        // - MSI page table
        // - Memory-resident interrupt file (MRIF)
        // The RCID and MCID configured in DC.ta are provided to the IO bridge on
        // successful address translations. The IO bridge should associate these QoS IDs
        // with device-initiated requests.
        uint64_t rcid:12;
        uint64_t mcid:12;
    };
    uint64_t raw;
} ta_t;

#define RVI_IOMMU_IOSATP_Bare 0
#define RVI_IOMMU_IOSATP_Sv32 8
#define RVI_IOMMU_IOSATP_Sv39 8
#define RVI_IOMMU_IOSATP_Sv48 9
#define RVI_IOMMU_IOSATP_Sv57 10
// IO SATP
typedef union {
    struct {
        uint64_t PPN:44;
        uint64_t reserved:16;
        uint64_t MODE:4;
    };
    uint64_t raw;
} iosatp_t;
// First Stage context
#define RVI_IOMMU_PDTP_Bare 0
#define RVI_IOMMU_PD8       1
#define RVI_IOMMU_PD17      2
#define RVI_IOMMU_PD20      3
typedef union {
    // If `PDTV` is 0, the `fsc` field in `DC` holds the `iosatp` (when `iohgatp MODE`
    // is `Bare`) or the `iovsatp` (when `iohgatp MODE` is not `Bare`) that provide the
    // controls for S-stage page table or VS-stage address translation and protection
    // respectively.
    // IO (Virtual)Supervisor addr. translation and prot. (`iovsatp`/`iosatp`) field (when `PDTV` is 0)
    //  {bits:  43:0: 'PPN'
    //  {bits: 59:44: 'reserved'
    //  {bits: 63:60: 'MODE'
    // The encoding of the `iosatp`/`iovsatp` `MODE` field are as the same as the
    // encoding for `MODE` field in the `satp` CSR.
    iosatp_t iosatp;

    // When `PDTV` is 1, the `fsc` field holds the process-directory table pointer
    // (`pdtp`). When the device supports multiple process contexts, selected by the
    // `process_id`, the PDT is used to determine the S/VS-stage page table and
    // associated `PSCID` for virtual address translation and protection.
    // The `pdtp` field holds the PPN of the root PDT and the `MODE` field that
    // determines the number of levels of the PDT.
    // Process-directory table pointer (`pdtp`) field (when `PDTV` is 1)
    //  {bits:  43:0: 'PPN'
    //  {bits: 59:44: 'reserved'
    //  {bits: 63:60: 'MODE'
    // When two-stage address translation is active (`iohgatp.MODE != Bare`), the `PPN`
    // field holds a guest PPN.  The GPA of the root PDT is then converted by guest
    // physical address translation, as controlled by the `iohgatp`, into a supervisor
    // physical address. Translating addresses of root PDT root through G-stage page
    // tables, allows the PDT to be held in memory allocated by the guest OS and allows
    // the guest OS to directly edit the PDT to associate a virtual-address space
    // identified by a VS-stage page table with a `process_id`.
    struct {
        uint64_t PPN:44;
        uint64_t reserved:16;
        // Encoding of `pdtp.MODE` field
        // |Value | Name     | Description
        // | 0    | `Bare`   | No translation or protection. First stage translation is
        //                     not enabled.
        // | 1    | `PD20`   | 20-bit process ID enabled. The directory has 3 levels.
        //                     The root PDT has 8 entries and the next non-leaf
        //                     level has 512 entries. The leaf level has 256 entries.
        // | 2    | `PD17`   | 17-bit process ID enabled. The directory has 2 levels.
        //                     The root PDT page has 512 entries and leaf level has
        //                     256 entries. The bits 19:17 of `process_id` must be 0.
        // | 3    | `PD8`    | 8-bit process ID enabled. The directory has 1 levels with
        //                     256 entries.The bits 19:8 of `process_id` must be 0.
        // | 4-13 | --       | Reserved
        // |14-15 | --       | Custome

        uint64_t MODE:4;
    } pdtp;

    uint64_t raw;
} fsc_t;


// MSI page table pointer
// Encoding of `msiptp` `MODE` field
// |Value | Name     | Description
// | 0    | `Off`    | Recognition of accesses to a virtual interrupt file using
//                     MSI address mask and pattern is not performed.
// | 1    | `Flat`   | Flat MSI page table
// | 2-13 | --       | Reserved
// |14-15 | --       | Custom

#define RVI_IOMMU_MSIPTP_Off  0
#define RVI_IOMMU_MSIPTP_Flat 1
typedef union {
    struct {
        uint64_t PPN:44;
        uint64_t reserved:16;
        uint64_t MODE:4;
    };
    uint64_t raw;
} msiptp_t;

// The MSI address mask (msi_addr_mask) and pattern (msi_addr_pattern) fields are used to
// recognize certain memory writes from the device as being MSIs and to identify the 4-KiB
// pages of virtual interrupt files in the guest physical address space of the relevant VM.
// An incoming 32-bit write made by a device is recognized as an MSI write to a virtual
// interrupt file if the destination guest physical page matches the supplied address
// pattern in all bit positions that are zeros in the supplied address mask. In detail, a
// write to guest physical address A is recognized as an MSI to a virtual interrupt file if:
// (A >> 12) & ~msi_addr_mask = (msi_addr_pattern & ~msi_addr_mask)
// where >> 12 represents shifting right by 12 bits, an ampersand (&) represents bitwise
// logical AND, and ~msi_addr_mask is the bitwise logical complement of the address mask.
typedef union {
    struct {
        uint64_t mask:52;
        uint64_t reserved:12;
    };
    uint64_t raw;
} msi_addr_mask_t;
typedef union {
    struct {
        uint64_t pattern:52;
        uint64_t reserved:12;
    };
    uint64_t raw;
} msi_addr_pattern_t;

// In base-format the `DC` is 32-bytes. In extended-format the `DC` is 64-bytes.
//
// Base-format device-context
//   bits:    63:0:'Translation-control (tc)'
//   bits:  127:64:'IO Hypervisor guest address translation and protection (iohgatp)'
//   bits: 191:128: 'Translation-attributes (ta)'
//   bits: 255:192: 'First-stage-context (fsc)'
// Extended-format device-context
//   bits:    63:0:'Translation-control (tc)'
//   bits:  127:64:'IO Hypervisor guest address translation and protection (iohgatp)'
//   bits: 191:128: 'Translation-attributes (ta)'
//   bits: 255:192: 'First-stage-context (fsc)'
//   bits: 319:256:'MSI-page-table pointer (msiptp)'
//   bits: 383:320:'MSI-address-mask (msi_addr_mask)'
//   bits: 447:384:'MSI-address-pattern (msi_addr_pattern)'
//   bits: 511:448:'reserved'
typedef struct {
    // Base Format
    tc_t      tc;
    iohgatp_t iohgatp;
    ta_t      ta;
    fsc_t     fsc;
    // Extended Format - additional fields
    msiptp_t  msiptp;
    msi_addr_mask_t  msi_addr_mask;
    msi_addr_pattern_t  msi_addr_pattern;
    uint64_t  reserved;
} device_context_t;
#define RVI_IOMMU_BASE_FORMAT_DC_SIZE 32
#define RVI_IOMMU_EXT_FORMAT_DC_SIZE  64

// A valid (`V==1`) non-leaf DDT entry provides PPN of the next level DDT.
typedef union {
    struct {
        uint64_t V:1;
        uint64_t reserved0:9;
        uint64_t PPN:44;
        uint64_t reserved1:10;
    };
    uint64_t raw;
} ddte_t;

// Process context translation attributes
typedef union {
    struct {
        //`PC` is valid if the `V` bit is 1; If it is 0, all other bits in `PC` are don't
        // care and may be freely used by software.
        uint64_t V:1;
        // When Enable-Supervisory-access (`ENS`) is 1, transactions requesting supervisor
        // privilege are allowed with this `process_id` else the transaction is treated as
        // an unsupported transaction.
        uint64_t ENS:1;
        // When `ENS` is 1, the `SUM` (permit Supervisor User Memory access) bit
        // modifies the privilege with which supervisor privilege transactions access
        // virtual memory. When `SUM` is 0, supervisor privilege transactions to pages
        // mapped with `U`-bit in PTE set to 1 will fault.
        // When `ENS` is 1, supervisor privilege transactions that read with execute
        // intent to pages mapped with `U` bit in PTE set to 1 will fault, regardless of
        // the state of `SUM`.
        uint64_t SUM:1;
        uint64_t reserved0:9;
        uint64_t PSCID:20;
        uint64_t reserved1:32;
    };
    uint64_t raw;
} pc_ta_t;
typedef union {
    // IO (Virtual)Supervisor addr. translation and prot. (`iovsatp`/`iosatp`) field
    //  {bits:  43:0: 'PPN'
    //  {bits: 59:44: 'reserved'
    //  {bits: 63:60: 'MODE'
    // The encoding of the `iosatp`/`iovsatp` `MODE` field are as the same as the
    // encoding for `MODE` field in the `satp` CSR.
    // When two-stage address translation is active (`iohgatp.MODE != Bare`), the `PPN`
    // field holds a guest PPN of the root of a VS-stage page table. Addresses of the
    // VS-stage page table entries are then converted by guest physical address
    // translation process, as controlled by the `iohgatp`, into a supervisor physical
    // address. A guest OS may thus directly edit the VS-stage page table to limit
    // access by the device to a subset of its memory and specify permissions for the
    // device accesses.
    iosatp_t iosatp;
    uint64_t raw;
} pc_fsc_t;

// A valid (`V==1`) non-leaf PDT entry holds the PPN of the
// next-level PDT.
typedef union {
    struct {
        uint64_t V:1;
        uint64_t reserved0:9;
        uint64_t PPN:44;
        uint64_t reserved1:10;
    };
    uint64_t raw;
} pdte_t;

// The leaf PDT page is indexed by `PDI[0]` and holds the 16-byte
// process-context (PC)
// The `PC` is interpreted as two 64-bit doublewords. The byte order of each of the
// doublewords in memory, little-endian or big-endian, is the endianness as
// determined by `fctrl.END` (<<FCTRL>>).
typedef struct {
    pc_ta_t  ta;
    pc_fsc_t fsc;
} process_context_t;

#endif // __IOMMU_DATA_STRUCTURES_H__
