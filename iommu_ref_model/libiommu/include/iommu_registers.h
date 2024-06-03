// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#ifndef _IOMMU_REGS_H_
#define _IOMMU_REGS_H_
// The `capabilities` register is a read-only register reporting features supported
// by the IOMMU. Each field if not clear indicates presence of that feature in
// the IOMMU. At reset, the register shall contain the IOMMU supported features.
// Hypervisor may provide an SW emulated IOMMU to allow the guest to manage
// the VS-stage page tables for fine grained control on memory accessed by guest
// controlled devices.
// A hypervisor that provides such an emulated IOMMU to the guest may retain
// control of the G-stage page tables and clear the `SvNx4` fields of the
// emulated `capabilities` register.
// A hypervisor that provides such an emulated IOMMU to the guest may retain
// control of the MSI page tables used to direct MSI to guest interrupt files in
// an IMSIC or to a memory-resident-interrupt-file and clear the `MSI_FLAT` and
// `MSI_MRIF` fields of the emulated `capabilities` register.
typedef union {
    struct {
        uint64_t version : 8;      // The `version` field holds the version of the
                                   // specification implemented by the IOMMU. The low
                                   // nibble is used to hold the minor version of the
                                   // specification and the upper nibble is used to
                                   // hold the major version of the specification.
                                   // For example, an implementation that supports
                                   // version 1.0 of the specification reports 0x10.
        uint64_t Sv32    : 1;      // Page-based 32-bit virtual addressing is supported
        uint64_t Sv39    : 1;      // Page-based 39-bit virtual addressing is supported
        uint64_t Sv48    : 1;      // Page-based 48-bit virtual addressing is supported
        uint64_t Sv57    : 1;      // Page-based 57-bit virtual addressing is supported
        uint64_t rsvd0   : 3;      // Reserved for standard use.
        uint64_t Svpbmt  : 1;      // Page-based memory types.
        uint64_t Sv32x4  : 1;      // Page-based 34-bit virtual addressing for G-stage
                                   // translation is supported
        uint64_t Sv39x4  : 1;      // Page-based 41-bit virtual addressing for G-stage
                                   // translation is supported
        uint64_t Sv48x4  : 1;      // Page-based 50-bit virtual addressing for G-stage
                                   // translation is supported
        uint64_t Sv57x4  : 1;      // Page-based 59-bit virtual addressing for G-stage
                                   // translation is supported
        uint64_t rsvd1   : 1;      // Reserved for standard use.
        uint64_t amo_mrif: 1;      // Atomic updates to MRIF is supported.
        uint64_t msi_flat: 1;      // MSI address translation using Write-through
                                   // mode MSI PTE is supported.
        uint64_t msi_mrif: 1;      // MSI address translation using MRIF mode MSI PTE
                                   // is supported.
        uint64_t amo_hwad: 1;      // Atomic updates to PTE accessed (A)
                                   // and dirty (D) bit is supported.
        uint64_t ats     : 1;      // PCIe Address Translation Services (ATS) and
                                   // page-request interface (PRI) is supported.
        uint64_t t2gpa   : 1;      // Returning guest-physical-address in ATS
                                   //  translation completions is supported.
        uint64_t end     : 1;      // When 0, IOMMU supports one endianness (either little
                                   // or big). When 1, IOMMU supports both endianness.
                                   // The endianness is defined in `fctl` register.
        uint64_t igs     : 2;      // IOMMU interrupt generation support.
                                   // !Value  !Name      ! Description
                                   // !0      ! `MSI`    ! IOMMU supports only MSI
                                   //                      generation.
                                   // !1      ! `WSI`    ! IOMMU supports only wire
                                   //                      interrupt generation.
                                   // !2      ! `BOTH`   ! IOMMU supports both MSI
                                   //                      and wire interrupt generation.
                                   //                      The interrupt generation method
                                   //                      must be defined in `fctl`
                                   //                      register.
                                   // !3      ! 0        ! Reserved for standard use
        uint64_t hpm     : 1;      // IOMMU implements a hardware performance monitor.
        uint64_t dbg     : 1;      // IOMMU supports the translation-request interface.
        uint64_t pas     : 6;      // Physical Address Size supported by the IOMMU
        uint64_t pd8     : 1;      // One level PDT with 8-bit process_id supported.
        uint64_t pd17    : 1;      // Two level PDT with 17-bit process_id supported.
        uint64_t pd20    : 1;      // Three level PDT with 20-bit process_id supported.
        uint64_t rsvd3   : 15;     // Reserved for standard use
        uint64_t custom  : 8;      // _Designated for custom use_
    };
    uint64_t raw;
} capabilities_t;
// This register must be readable in any implementation. An implementation may
// allow one or more fields in the register to be writable to support enabling
// or disabling the feature controlled by that field.
// If software enables or disables a feature when the IOMMU is not OFF
// (i.e. `ddtp.iommu_mode == Off`) then the IOMMU behavior is `UNSPECIFIED`.
typedef union {
    struct {
        uint32_t be      : 1;      // When 0, IOMMU accesses to memory resident data
                                   // structures, as specified in Table 7, and
                                   // accesses to in-memory queues are performed as
                                   // little-endian accesses and when 1 as
                                   // big-endian accesses.
        uint32_t wsi     : 1;      // When 1, IOMMU interrupts are signaled as
                                   // wired-interrupts.
        uint32_t gxl     : 1;      // Controls the address-translation schemes
                                   // that may be used for guest physical addresses
                                   // as defined in Table 2.
        uint32_t reserved: 13;     // reserved for standard use.
        uint32_t custom  : 16;     //  _Designated for custom use._
    };
    uint32_t raw;
} fctl_t;
// The device-context is 64-bytes in size if `capabilities.MSI_FLAT` is 1 else it is
// 32-bytes.
// When the `iommu_mode` is `Bare` or `Off`, the `PPN` field is don't-care. When
// in `Bare` mode only Untranslated requests are allowed. Translated requests,
// Translation request, and message transactions are unsupported.
// All IOMMU must support `Off` and `Bare` mode. An IOMMU is allowed to support a
// subset of directory-table levels and device-context widths. At a minimum one
// of the modes must be supported.
// When the `iommu_mode` field value is changed the IOMMU guarantees that
// in-flight transactions from devices connected to the IOMMU will be processed
// with the configurations applicable to the old value of the `iommu_mode` field
// and that all transactions and previous requests from devices that have already
// been processed by the IOMMU be committed to a global ordering point such that
// they can be observed by all RISC-V hart, devices, and IOMMUs in the platform.
typedef union {
    struct {
        uint64_t iommu_mode: 4;    // The IOMMU may be configured to be in following
                                   // modes:
                                   // !Value  !Name      ! Description
                                   // !0      ! `Off`    ! No inbound memory
                                   //                      transactions are allowed
                                   //                      by the IOMMU.
                                   // !1      ! `Bare`   ! No translation or
                                   //                      protection. All inbound
                                   //                      memory accesses are passed
                                   //                      through.
                                   // !2      ! `1LVL`   ! One-level
                                   //                      device-directory-table
                                   // !3      ! `2LVL`   ! Two-level
                                   //                      device-directory-table
                                   // !4      ! `3LVL`   ! Three-level
                                   //                      device-directory-table
                                   // !5-13   ! reserved ! Reserved for standard use.
                                   // !14-15  ! custom   ! Designated for custom use.
        uint64_t busy    : 1;      // A write to `ddtp` may require the IOMMU to
                                   // perform many operations that may not occur
                                   // synchronously to the write. When a write is
                                   // observed by the `ddtp`, the `busy` bit is set
                                   // to 1. When the `busy` bit is 1, behavior of
                                   // additional writes to the `ddtp` is
                                   // implementation defined. Some implementations
                                   // may ignore the second write and others may
                                   // perform the actions determined by the second
                                   // write. Software must verify that the `busy`
                                   // bit is 0 before writing to the `ddtp`.

                                   // If the `busy` bit reads 0 then the IOMMU has
                                   // completed the operations associated with the
                                   // previous write to `ddtp`.

                                   // An IOMMU that can complete these operations
                                   // synchronously may hard-wire this bit to 0.
        uint64_t reserved0: 5;     // reserved for standard use.
        uint64_t ppn     : 44;     // Holds the `PPN` of the root page of the
                                   // device-directory-table.
        uint64_t reserved1: 10;    // reserved for standard use.
    };
    uint64_t raw;
} ddtp_t;
// This 64-bits register (RW) holds the PPN of the root page of the command-queue
// and number of entries in the queue. Each command is 16 bytes.
typedef union {
    struct {
        uint64_t log2szm1: 5;      // The `LOG2SZ-1` field holds the number of
                                   // entries in command-queue as a log to base 2
                                   // minus 1.
                                   // A value of 0 indicates a queue of 2 entries.
                                   // Each IOMMU command is 16-bytes.
                                   // If the command-queue has 256 or fewer entries
                                   // then the base address of the queue is always
                                   // aligned to 4-KiB. If the command-queue has more
                                   // than 256 entries then the command-queue
                                   // base address must be naturally aligned to
                                   // `2^LOG2SZ^ x 16`.
        uint64_t reserved0: 5;     // Reserved for standard use
        uint64_t ppn     : 44;     // Holds the `PPN` of the root page of the
                                   // in-memory command-queue used by software to
                                   // queue commands to the IOMMU.
        uint64_t reserved1: 10;    // Reserved for standard use
    };
    uint64_t raw;
} cqb_t;
// This 32-bits register (RO) holds the index into the command-queue where
// the IOMMU will fetch the next command.
typedef union {
    struct {
        uint32_t index;            // Holds the `index` into the command-queue from where
                                   // the next command will be fetched next by the IOMMU.
    };
    uint32_t raw;
} cqh_t;
// This 32-bits register (RO) holds the index into the command-queue where
// the IOMMU will fetch the next command.
typedef union {
    struct {
        uint32_t index;            // Holds the `index` into the command-queue where
                                   // software queues the next command for IOMMU.  Only
                                   // `LOG2SZ:0` bits are writable when the queue is
                                   // in enabled state (i.e., `cqsr.cqon == 1`).
    };
    uint32_t raw;
} cqt_t;
// This 64-bits register (RW) holds the PPN of the root page of the fault-queue
// and number of entries in the queue. Each fault record is 32 bytes.
typedef union {
    struct {
        uint64_t log2szm1: 5;      // The `LOG2SZ-1` field holds the number of
                                   // entries in fault-queue as a log-to-base-2
                                   // minus 1. A value of 0 indicates a queue of 2
                                   // entries. Each fault record is 32-bytes.
                                   // If the fault-queue has 128 or fewer entries then
                                   // the base address of the queue is always aligned
                                   // to 4-KiB. If the fault-queue has more than 128
                                   // entries then the fault-queue base address must
                                   // be naturally aligned to `2^LOG2SZ^ x 32`.
        uint64_t reserved0: 5;     // Reserved for standard use
        uint64_t ppn     : 44;     // Holds the `PPN` of the root page of the
                                   // in-memory fault-queue used by IOMMU to queue
                                   // fault record.
        uint64_t reserved1: 10;    // Reserved for standard use
    };
    uint64_t raw;
} fqb_t;

// This 32-bits register (RW) holds the index into fault-queue where the
// software will fetch the next fault record.
typedef union {
    struct {
        uint32_t index;            // Holds the `index` into the fault-queue from which
                                   // software reads the next fault record.  Only
                                   // `LOG2SZ:0` bits are writable when the queue is
                                   // in enabled state (i.e., `fqsr.fqon == 1`).
    };
    uint32_t raw;
} fqh_t;
// This 32-bits register (RO) holds the index into the fault-queue where the
// IOMMU queues the next fault record.
typedef union {
    struct {
        uint32_t index;            // Holds the `index` into the fault-queue where IOMMU
                                   // writes the next fault record.
    };
    uint32_t raw;
} fqt_t;
// This 64-bits register (RW) holds the PPN of the root page of the
// page-request-queue and number of entries in the queue. Each page-request
// message is 16 bytes.
typedef union {
    struct {
        uint64_t log2szm1: 5;      // The `LOG2SZ-1` field holds the number of entries
                                   // in page-request-queue as a log-to-base-2 minus 1.
                                   // A value of 0 indicates a queue of 2 entries.
                                   // Each page-request is 16-bytes. If the
                                   // page-request-queue has 256 or fewer entries
                                   // then the base address of the queue is always
                                   // aligned to 4-KiB.
                                   // If the page-request-queue has more than 256
                                   // entries then the page-request-queue base address
                                   // must be naturally aligned to `2^LOG2SZ^ x 16`.
        uint64_t reserved0: 5;     // Reserved for standard use
        uint64_t ppn     : 44;     // Holds the `PPN` of the root page of the
                                   // in-memory page-request-queue used by IOMMU to
                                   // queue "Page Request" messages.
        uint64_t reserved1: 10;    // Reserved for standard use
    };
    uint64_t raw;
} pqb_t;
// This 32-bits register (RW) holds the index into the page-request-queue where
// software will fetch the next page-request.
typedef union {
    struct {
        uint32_t index;
    };
    uint32_t raw;
} pqh_t;
// This 32-bits register (RW) holds the index into the page-request-queue where
// software will fetch the next page-request.
typedef union {
    struct {
        uint32_t index;
    };
    uint32_t raw;
} pqt_t;
// This 32-bits register (RW) is used to control the operations and report the
// status of the command-queue.
typedef union {
    struct {
        uint32_t cqen    :1;       // The command-queue-enable bit enables the command-
                                   // queue when set to 1. Changing `cqen` from 0 to 1
                                   // sets the `cqh` and `cqt` to 0. The command-queue
                                   // may take some time to be active following setting
                                   // the `cqen` to 1. When the command queue is active,
                                   // the `cqon` bit reads 1.
                                   //
                                   // When `cqen` is changed from 1 to 0, the command
                                   // queue may stay active till the commands already
                                   // fetched from the command-queue are being processed
                                   // and/or there are outstanding implicit loads from
                                   // the command-queue.  When the command-queue turns
                                   // off, the `cqon` bit reads 0, `cqh` is set to 0,
                                   // `cqt` is set to 0 and the `cqcsr` bits `cmd_ill`,
                                   // `cmd_to`, `cqmf`, `fence_w_ip` are set to 0.
                                   //
                                   // When the `cqon` bit reads 0, the IOMMU guarantees
                                   // that no implicit memory accesses to the command
                                   // queue are in-flight and the command-queue will not
                                   // generate new implicit loads to the queue memory.
        uint32_t cie     :1;       // Command-queue-interrupt-enable bit enables
                                   // generation of interrupts from command-queue when
                                   // set to 1.
        uint32_t rsvd0   :6;       // Reserved for standard use
        uint32_t cqmf    :1;       // If command-queue access leads to a memory fault then
                                   // the command-queue-memory-fault bit is set to 1 and
                                   // the command-queue stalls until this bit is cleared.
                                   // When `cqmf` is set to 1, an interrupt is generated
                                   // if an interrupt is not already pending
                                   // (i.e., `ipsr.cip == 1`) and not masked
                                   // (i.e. `cqsr.cie == 0`). To re-enable command
                                   // processing, software should clear this bit by
                                   // writing 1.
        uint32_t cmd_to  :1;       // If the execution of a command leads to a
                                   // timeout (e.g. a command to invalidate device ATC
                                   // may timeout waiting for a completion), then the
                                   // command-queue sets the `cmd_to` bit and stops
                                   // processing from the command-queue. When `cmd_to` is
                                   // set to 1 an interrupt is generated if an interrupt
                                   // is not already pending (i.e., `ipsr.cip == 1`) and
                                   // not masked (i.e. `cqsr.cie == 0`). To re-enable
                                   // command processing software should clear this bit
                                   // by writing 1.
        uint32_t cmd_ill :1;       // If an illegal or unsupported command is fetched and
                                   // decoded by the command-queue then the command-queue
                                   // sets the `cmd_ill` bit and stops processing from the
                                   // command-queue. When `cmd_ill` is set to 1,
                                   // an interrupt is generated if not already pending
                                   // (i.e. `ipsr.cip == 1`) and not masked
                                   // (i.e.  `cqsr.cie == 0`). To re-enable command
                                   // processing software should clear this bit by
                                   // writing 1.
        uint32_t fence_w_ip :1;    // An IOMMU that supports only wired interrupts sets
                                   // `fence_w_ip` bit is set to indicate completion of a
                                   // `IOFENCE.C` command. An interrupt on setting
                                   // `fence_w_ip` if not already pending
                                   // (i.e. `ipsr.cip == 1`) and not masked
                                   // (i.e. `cqsr.cie == 0`) and `fence_w_ip` is 0.
                                   // To re-enable interrupts on `IOFENCE.C` completion
                                   // software should clear this bit by writing 1.
                                   // This bit is reserved if the IOMMU uses MSI.
        uint32_t rsvd1   :4;       // Reserved for standard use
        uint32_t cqon    :1;       // The command-queue is active if `cqon` is 1.
                                   // IOMMU behavior on changing cqb when busy is 1 or
                                   // `cqon` is 1 is implementation defined. The software
                                   // recommended sequence to change `cqb` is to first
                                   // disable the command-queue by clearing cqen and
                                   // waiting for both `busy` and `cqon` to be 0 before
                                   // changing the `cqb`.
        uint32_t busy    :1;       // A write to `cqcsr` may require the IOMMU to perform
                                   // many operations that may not occur synchronously
                                   // to the write. When a write is observed by the
                                   // `cqcsr`, the `busy` bit is set to 1.
                                   //
                                   // When the `busy` bit is 1, behavior of additional
                                   // writes to the `cqcsr` is implementation defined.
                                   // Some implementations may ignore the second write and
                                   // others may perform the actions determined by the
                                   // second write.
                                   //
                                   // Software must verify that the busy bit is 0 before
                                   // writing to the `cqcsr`. An IOMMU that can complete
                                   // controls synchronously may hard-wire this bit to 0.
                                   //
                                   // An IOMMU that can complete these operations
                                   // synchronously may hard-wire this bit to 0.
        uint32_t rsvd2   :10;      // Reserved for standard use
        uint32_t custom  :4;       // _Designated for custom use._
    };
    uint32_t raw;
} cqcsr_t;
typedef union {
    struct {
        uint32_t fqen    :1;
        uint32_t fie     :1;
        uint32_t rsvd0   :6;
        uint32_t fqmf    :1;
        uint32_t fqof    :1;
        uint32_t rsvd1   :6;
        uint32_t fqon    :1;
        uint32_t busy    :1;
        uint32_t rsvd2   :10;
        uint32_t custom  :4;
    };
    uint32_t raw;
} fqcsr_t;
typedef union {
    struct {
        uint32_t pqen    :1;
        uint32_t pie     :1;
        uint32_t rsvd0   :6;
        uint32_t pqmf    :1;
        uint32_t pqof    :1;
        uint32_t rsvd1   :6;
        uint32_t pqon    :1;
        uint32_t busy    :1;
        uint32_t rsvd2   :10;
        uint32_t custom  :4;
    };
    uint32_t raw;
} pqcsr_t;
typedef union {
    struct {
        uint32_t cip     :1;
        uint32_t fip     :1;
        uint32_t pmip    :1;
        uint32_t pip     :1;
        uint32_t rsvd0   :4;
        uint32_t custom  :8;
        uint32_t rsvd1   :16;
    };
    uint32_t raw;
} ipsr_t;
typedef union {
    struct {
        uint32_t cy      :1;
        uint32_t hpm     :31;
    };
    uint32_t raw;
} iocountovf_t;
typedef union {
    struct {
        uint32_t cy      :1;
        uint32_t hpm     :31;
    };
    uint32_t raw;
} iocountinh_t;
typedef union {
    struct {
        uint64_t counter :63;
        uint64_t of      :1;
    };
    uint64_t raw;
} iohpmcycles_t;
typedef struct {
    uint64_t counter :64;
} iohpmctr_t;
typedef union {
    struct {
        uint64_t eventID :15;
        uint64_t dmask   :1;
        uint64_t pid_pscid :20;
        uint64_t did_gscid :24;
        uint64_t pv_pscv :1;
        uint64_t dv_gscv :1;
        uint64_t idt     :1;
        uint64_t of      :1;
    };
    uint64_t raw;
} iohpmevt_t;

typedef union {
    struct {
        uint64_t civ     :4;
        uint64_t fiv     :4;
        uint64_t pmiv    :4;
        uint64_t piv     :4;
        uint64_t reserved:16;
        uint64_t custom  :32;
    };
    uint64_t raw;
} icvec_t;

// The tr_req_iova is a 64-bit WARL register used to
// implement a translation-request interface for
// debug. This register is present when
// capabilities.DBG == 1.
typedef union {
    struct {
        uint64_t reserved:12;  // Reserved
        uint64_t vpn:52;       // The IOVA virtual page number
    };
    uint64_t raw;
} tr_req_iova_t;
// The tr_req_ctrl is a 64-bit WARL register used to
// implement a translation-request interface for
// debug. This register is present when
// capabilities.DBG == 1.
typedef union {
    struct {
        uint64_t go_busy:1;    // This bit is set to indicate a valid request
                               // has been setup in the tr_req_iova/tr_req_ctrl
                               // registers for the IOMMU to translate.
                               // The IOMMU indicates completion of the requested
                               // translation by clearing this bit to 0. On
                               // completion, the results of the translation are
                               // in tr_response register.

        uint64_t Priv:1;       // When set to 1 the requests needs Privileged Mode
                               // access for this translation.
        uint64_t Exe:1;        // When set to 1 the request needs execute access
                               // for this translation.
        uint64_t NW:1;         // When set to 1 the request only needs read-only
                               // access for this translation.
        uint64_t reserved0:8;  // Reserved for standard use
        uint64_t PID:20;       // When PV is 1 this field provides the process_id for
                               // this translation request.
        uint64_t PV:1;         // When set to 1 the PID field of the register is valid.
        uint64_t reserved:3;   // Reserved for standard use
        uint64_t custom:4;     // Designated for custom use
        uint64_t DID:24;       // This field provides the device_id for this
                               // translation request.
    };
    uint64_t raw;
} tr_req_ctrl_t;

// The tr_response is a 64-bit RO register used to hold
// the results of a translation requested using the
// translation-request interface. This register is present
// when capabilities.DBG == 1
typedef union {
    struct {
        uint64_t fault:1;      // If the process to translate the IOVA detects
                               // a fault then the `fault` field is set to 1.
                               // The detected fault may be reported through the
                               // fault-queue.
        uint64_t reserved0:6;  // Reserved for standard use
        uint64_t PBMT:2;       // Memory type determined for the translation
                               // using the PBMT fields in the S/VS-stage and/or
                               // the G-stage page tables used for the
                               // translation. This value of field is
                               // `UNSPECIFIED` if the `fault` field is 1.
        uint64_t S:1;          // Translation range size field, when set to 1
                               // indicates that the translation applies to a
                               // range that is larger than 4 KiB and the size
                               // of the translation range is encoded in the
                               // `PPN` field. The value of this field is
                               // `UNSPECIFIED` if the `fault` field is 1.
        uint64_t  PPN:44;      // If the fault bit is 0, then this field provides the PPN determined
                               // as a result of translating the iova_vpn in tr_req_iova.
                               // If the fault bit is 1, then the value of this field is UNSPECIFIED.
                               // If the S bit is 0, then the size of the translation is 4 KiB - a page.
                               // If the S bit is 1, then the translation resulted in a super-page, and
                               // the size of the super-page is encoded in the PPN itself. If scanning
                               // from bit position 0 to bit position 43, the first bit with a value
                               // of 0 at position X, then the super-page size is 2X+1 * 4 KiB.
                               // If X is not 0, then all bits at position 0 through X-1 are each
                               // encoded with a value of 1.
                               // .Example of encoding of super page size in `PPN`
                               //  !           `PPN`          !`S`!   Size
                               //  !`yyyy....yyyy yyyy yyyy`  !`0`!  4 KiB
                               //  !`yyyy....yyyy yyyy 0111`  !`1`! 64 KiB
                               //  !`yyyy....yyy0 1111 1111`  !`1`!  2 MiB
                               //  !`yyyy....yy01 1111 1111`  !`1`!  4 MiB

        uint64_t reserved1:6;  // Reserved for standard use
        uint64_t custom:4;     // _Designated for custom use_
    };
    uint64_t raw;
} tr_response_t;


typedef union {
    struct {
        uint64_t zero:2;
        uint64_t addr:54;
        uint64_t reserved:8;
    };
    uint64_t raw;
} msi_addr_t;
typedef union {
    struct {
        uint32_t m:1;
        uint32_t reserved:31;
    };
    uint32_t raw;
} msi_vec_ctrl_t;
typedef struct {
    msi_addr_t msi_addr;
    uint32_t msi_data;
    msi_vec_ctrl_t msi_vec_ctrl;
} msi_cfg_tbl_t;
// The IOMMU provides a memory-mapped programming interface. The memory-mapped
// registers of each IOMMU are located within a naturally aligned 4-KiB region
// (a page) of physical address space.
// The IOMMU behavior for register accesses where the address is not aligned to
// the size of the access or if the access spans multiple registers is undefined.
// IOMMU Memory-mapped register layout
typedef union {                        // |Ofst|Name            |Size|Description
    struct __attribute__((__packed__)) {
        capabilities_t capabilities;   // |0   |`capabilities`  |8   |Capabilities supported by the IOMMU
        fctl_t         fctl;           // |8   |`fctl`          |4   |Features control>>
        uint32_t       custom0;        // |12  |_custom_        |4   |For custom use_
        ddtp_t         ddtp;           // |16  |`ddtp`          |8   |Device directory table pointer
        cqb_t          cqb;            // |24  |`cqb`           |8   |Command-queue base
        cqh_t          cqh;            // |32  |`cqh`           |4   |Command-queue head
        cqt_t          cqt;            // |36  |`cqt`           |4   |Command-queue tail
        fqb_t          fqb;            // |40  |`fqb`           |8   |Fault-queue base
        fqh_t          fqh;            // |48  |`fqh`           |4   |Fault-queue head
        fqt_t          fqt;            // |52  |`fqt`           |4   |Fault-queue tail
        pqb_t          pqb;            // |56  |`pqb`           |8   |Page-request-queue base
        pqh_t          pqh;            // |64  |`pqh`           |4   |Page-request-queue head
        pqt_t          pqt;            // |68  |`pqt`           |4   |Page-request-queue tail
        cqcsr_t        cqcsr;          // |72  |`cqcsr`         |4   |Command-queue control and status register
        fqcsr_t        fqcsr;          // |76  |`fqcsr`         |4   |Fault-queue control and status register
        pqcsr_t        pqcsr;          // |80  |`pqcsr`         |4   |Page-req-queue control and status register
        ipsr_t         ipsr;           // |84  |`ipsr`          |4   |Interrupt pending status register
        iocountovf_t   iocountovf;     // |88  |`iocntovf`      |4   |Perf-monitoring counter overflow status
        iocountinh_t   iocountinh;     // |92  |`iocntinh`      |4   |Performance-monitoring counter inhibits
        iohpmcycles_t  iohpmcycles;    // |96  |`iohpmcycles`   |8   |Performance-monitoring cycles counter
        iohpmctr_t     iohpmctr[31];   // |104 |`iohpmctr1 - 31`|248 |Performance-monitoring event counters
        iohpmevt_t     iohpmevt[31];   // |352 |`iohpmevt1 - 31`|248 |Performance-monitoring event selector
        tr_req_iova_t  tr_req_iova;    // |600 |`tr_req_iova`   |8   |Translation-request IOVA
        tr_req_ctrl_t  tr_req_ctrl;    // |608 |`tr_req_ctrl`   |8   |Translation-request control
        tr_response_t  tr_response;    // |616 |`tr_response`   |8   |Translation-request response
        uint8_t        reserved0[58];  // |624 |Reserved        |82  |Reserved for future use (`WPRI`)
        uint8_t        custom1[78];    // |682 |_custom_        |78  |Designated for custom use (`WARL`)_
        icvec_t        icvec;          // |760 |`icvec`         |4   |Interrupt cause to vector register
        msi_cfg_tbl_t  msi_cfg_tbl[16];// |768 |`msi_cfg_tbl`   |256 |MSI Configuration Table
        uint8_t        reserved1[3072];// |1024|Reserved        |3072|Reserved for future use (`WPRI`)
    };
    uint8_t         regs1[4096];
    uint16_t        regs2[2048];
    uint32_t        regs4[1024];
    uint64_t        regs8[512];
} iommu_regs_t;

// Offset to field
#define CAPABILITIES_OFFSET  0
#define FCTRL_OFFSET         8
#define DDTP_OFFSET          16
#define CQB_OFFSET           24
#define CQH_OFFSET           32
#define CQT_OFFSET           36
#define FQB_OFFSET           40
#define FQH_OFFSET           48
#define FQT_OFFSET           52
#define PQB_OFFSET           56
#define PQH_OFFSET           64
#define PQT_OFFSET           68
#define CQCSR_OFFSET         72
#define FQCSR_OFFSET         76
#define PQCSR_OFFSET         80
#define IPSR_OFFSET          84
#define IOCNTOVF_OFFSET      88
#define IOCNTINH_OFFSET      92
#define IOHPMCYCLES_OFFSET   96

#define IOHPMCTR1_OFFSET     104
#define IOHPMCTR2_OFFSET     112
#define IOHPMCTR3_OFFSET     120
#define IOHPMCTR4_OFFSET     128
#define IOHPMCTR5_OFFSET     136
#define IOHPMCTR6_OFFSET     144
#define IOHPMCTR7_OFFSET     152
#define IOHPMCTR8_OFFSET     160
#define IOHPMCTR9_OFFSET     168
#define IOHPMCTR10_OFFSET    176
#define IOHPMCTR11_OFFSET    184
#define IOHPMCTR12_OFFSET    192
#define IOHPMCTR13_OFFSET    200
#define IOHPMCTR14_OFFSET    208
#define IOHPMCTR15_OFFSET    216
#define IOHPMCTR16_OFFSET    224
#define IOHPMCTR17_OFFSET    232
#define IOHPMCTR18_OFFSET    240
#define IOHPMCTR19_OFFSET    248
#define IOHPMCTR20_OFFSET    256
#define IOHPMCTR21_OFFSET    264
#define IOHPMCTR22_OFFSET    272
#define IOHPMCTR23_OFFSET    280
#define IOHPMCTR24_OFFSET    288
#define IOHPMCTR25_OFFSET    296
#define IOHPMCTR26_OFFSET    304
#define IOHPMCTR27_OFFSET    312
#define IOHPMCTR28_OFFSET    320
#define IOHPMCTR29_OFFSET    328
#define IOHPMCTR30_OFFSET    336
#define IOHPMCTR31_OFFSET    344

#define IOHPMEVT1_OFFSET     352
#define IOHPMEVT2_OFFSET     360
#define IOHPMEVT3_OFFSET     368
#define IOHPMEVT4_OFFSET     376
#define IOHPMEVT5_OFFSET     384
#define IOHPMEVT6_OFFSET     392
#define IOHPMEVT7_OFFSET     400
#define IOHPMEVT8_OFFSET     408
#define IOHPMEVT9_OFFSET     416
#define IOHPMEVT10_OFFSET    424
#define IOHPMEVT11_OFFSET    432
#define IOHPMEVT12_OFFSET    440
#define IOHPMEVT13_OFFSET    448
#define IOHPMEVT14_OFFSET    456
#define IOHPMEVT15_OFFSET    464
#define IOHPMEVT16_OFFSET    472
#define IOHPMEVT17_OFFSET    480
#define IOHPMEVT18_OFFSET    488
#define IOHPMEVT19_OFFSET    496
#define IOHPMEVT20_OFFSET    504
#define IOHPMEVT21_OFFSET    512
#define IOHPMEVT22_OFFSET    520
#define IOHPMEVT23_OFFSET    528
#define IOHPMEVT24_OFFSET    536
#define IOHPMEVT25_OFFSET    544
#define IOHPMEVT26_OFFSET    552
#define IOHPMEVT27_OFFSET    560
#define IOHPMEVT28_OFFSET    568
#define IOHPMEVT29_OFFSET    576
#define IOHPMEVT30_OFFSET    584
#define IOHPMEVT31_OFFSET    592
#define TR_REQ_IOVA_OFFSET   600
#define TR_REQ_CTRL_OFFSET   608
#define TR_RESPONSE_OFFSET   616
#define RESERVED_OFFSET      624
#define CUSTOM_OFFSET        682
#define ICVEC_OFFSET         760

#define MSI_ADDR_0_OFFSET      768 + 0 * 16 + 0
#define MSI_DATA_0_OFFSET      768 + 0 * 16 + 8
#define MSI_VEC_CTRL_0_OFFSET  768 + 0 * 16 + 12
#define MSI_ADDR_1_OFFSET      768 + 1 * 16 + 0
#define MSI_DATA_1_OFFSET      768 + 1 * 16 + 8
#define MSI_VEC_CTRL_1_OFFSET  768 + 1 * 16 + 12
#define MSI_ADDR_2_OFFSET      768 + 2 * 16 + 0
#define MSI_DATA_2_OFFSET      768 + 2 * 16 + 8
#define MSI_VEC_CTRL_2_OFFSET  768 + 2 * 16 + 12
#define MSI_ADDR_3_OFFSET      768 + 3 * 16 + 0
#define MSI_DATA_3_OFFSET      768 + 3 * 16 + 8
#define MSI_VEC_CTRL_3_OFFSET  768 + 3 * 16 + 12
#define MSI_ADDR_4_OFFSET      768 + 4 * 16 + 0
#define MSI_DATA_4_OFFSET      768 + 4 * 16 + 8
#define MSI_VEC_CTRL_4_OFFSET  768 + 4 * 16 + 12
#define MSI_ADDR_5_OFFSET      768 + 5 * 16 + 0
#define MSI_DATA_5_OFFSET      768 + 5 * 16 + 8
#define MSI_VEC_CTRL_5_OFFSET  768 + 5 * 16 + 12
#define MSI_ADDR_6_OFFSET      768 + 6 * 16 + 0
#define MSI_DATA_6_OFFSET      768 + 6 * 16 + 8
#define MSI_VEC_CTRL_6_OFFSET  768 + 6 * 16 + 12
#define MSI_ADDR_7_OFFSET      768 + 7 * 16 + 0
#define MSI_DATA_7_OFFSET      768 + 7 * 16 + 8
#define MSI_VEC_CTRL_7_OFFSET  768 + 7 * 16 + 12
#define MSI_ADDR_8_OFFSET      768 + 8 * 16 + 0
#define MSI_DATA_8_OFFSET      768 + 8 * 16 + 8
#define MSI_VEC_CTRL_8_OFFSET  768 + 8 * 16 + 12
#define MSI_ADDR_9_OFFSET      768 + 9 * 16 + 0
#define MSI_DATA_9_OFFSET      768 + 9 * 16 + 8
#define MSI_VEC_CTRL_9_OFFSET  768 + 9 * 16 + 12
#define MSI_ADDR_10_OFFSET     768 + 10 * 16 + 0
#define MSI_DATA_10_OFFSET     768 + 10 * 16 + 8
#define MSI_VEC_CTRL_10_OFFSET 768 + 10 * 16 + 12
#define MSI_ADDR_11_OFFSET     768 + 11 * 16 + 0
#define MSI_DATA_11_OFFSET     768 + 11 * 16 + 8
#define MSI_VEC_CTRL_11_OFFSET 768 + 11 * 16 + 12
#define MSI_ADDR_12_OFFSET     768 + 12 * 16 + 0
#define MSI_DATA_12_OFFSET     768 + 12 * 16 + 8
#define MSI_VEC_CTRL_12_OFFSET 768 + 12 * 16 + 12
#define MSI_ADDR_13_OFFSET     768 + 13 * 16 + 0
#define MSI_DATA_13_OFFSET     768 + 13 * 16 + 8
#define MSI_VEC_CTRL_13_OFFSET 768 + 13 * 16 + 12
#define MSI_ADDR_14_OFFSET     768 + 14 * 16 + 0
#define MSI_DATA_14_OFFSET     768 + 14 * 16 + 8
#define MSI_VEC_CTRL_14_OFFSET 768 + 14 * 16 + 12
#define MSI_ADDR_15_OFFSET     768 + 15 * 16 + 0
#define MSI_DATA_15_OFFSET     768 + 15 * 16 + 8
#define MSI_VEC_CTRL_15_OFFSET 768 + 15 * 16 + 12

// capabilities fields
#define MSI      0
#define WSI      1
#define IGS_BOTH 2
#define ONE_END   0
#define BOTH_END  1

// ddtp defines
#define Off      0
#define DDT_Bare 1
#define DDT_1LVL 2
#define DDT_2LVL 3
#define DDT_3LVL 4

extern iommu_regs_t g_reg_file;
extern uint8_t g_num_hpm;
extern uint8_t g_hpmctr_bits;
extern uint8_t g_eventID_mask;
extern uint8_t g_num_vec_bits;
extern uint8_t g_gxl_writeable;
extern uint8_t g_fctl_be_writeable;
extern uint8_t offset_to_size[4096];
extern uint8_t g_max_iommu_mode;
extern uint8_t g_fill_ats_trans_in_ioatc;
extern uint32_t g_max_devid_mask;

extern void process_commands(void);
#endif //_IOMMU_REGS_H_
