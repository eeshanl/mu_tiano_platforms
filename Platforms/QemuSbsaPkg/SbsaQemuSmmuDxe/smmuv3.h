/** @file smmuv3.h

    This file is the smmuv3 header file for SMMU driver for the Qemu SBSA platform.

    Copyright (C) Microsoft Corporation. All rights reserved.
    SPDX-License-Identifier: BSD-2-Clause-Patent

    Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
    QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
**/

#ifndef SMMUV3_H
#define SMMUV3_H

#include "smmuv3registers.h"


#define ROUND_UP(_Value_, _Alignment_) \
    (((_Value_) + (_Alignment_) - 1) & ~((_Alignment_) - 1))

#define SMMU_MMIO_PAGE_SIZE (1UL << 12) // 4 KB

//
// Macros to align values up or down. Alignment is required to be power of 2.
//

#define ALIGN_DOWN_BY(length, alignment) \
    ((UINT32)(length) & ~((UINT32)(alignment) - 1))

#define ALIGN_UP_BY(length, alignment) \
    (ALIGN_DOWN_BY(((UINT32)(length) + (alignment) - 1), alignment))

#define ARM64_RGNCACHEATTR_NONCACHEABLE 0
#define ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE 1
#define ARM64_RGNCACHEATTR_WRITETHROUGH 2
#define ARM64_RGNCACHEATTR_WRITEBACK_NOWRITEALLOCATE 3

#define ARM64_SHATTR_NON_SHAREABLE 0
#define ARM64_SHATTR_OUTER_SHAREABLE 2
#define ARM64_SHATTR_INNER_SHAREABLE 3

//
// Define the OS limit on the command queue entries. The command queue is
// limited to one page (4KB) worth of entries.
//
#define SMMUV3_COMMAND_QUEUE_OS_LOG2ENTRIES (8)
#define SMMUV3_COMMAND_QUEUE_OS_ENTRIES \
    (1UL << SMMUV3_COMMAND_QUEUE_OS_LOG2ENTRIES)

//
// Define the size of each entry in the command queue.
//
#define SMMUV3_COMMAND_QUEUE_ENTRY_SIZE (sizeof(SMMUV3_CMD_GENERIC))

//
// Macros to compute command queue size given its Log2 size.
//
#define SMMUV3_COMMAND_QUEUE_SIZE_FROM_LOG2(QueueLog2Size) \
    ((UINT32)(1UL << (QueueLog2Size)) * \
        (UINT16)(SMMUV3_COMMAND_QUEUE_ENTRY_SIZE))

//
// Define the OS limit on the event queue size. The event queue is limited
// to one page (4KB) worth of entries.
//
#define SMMUV3_EVENT_QUEUE_OS_LOG2ENTRIES (7)
#define SMMUV3_EVENT_QUEUE_OS_ENTRIES (1UL << SMMUV3_EVENT_QUEUE_OS_LOG2ENTRIES)

//
// Define the size of each entry in the event queue.
//
#define SMMUV3_EVENT_QUEUE_ENTRY_SIZE (sizeof(SMMUV3_FAULT_RECORD))

//
// Macros to compute event queue size given its Log2 size.
//
#define SMMUV3_EVENT_QUEUE_SIZE_FROM_LOG2(QueueLog2Size) \
    ((UINT32)(1UL << (QueueLog2Size)) * (UINT16)(SMMUV3_EVENT_QUEUE_ENTRY_SIZE))

#define SMMUV3_COUNT_FROM_LOG2(Log2Size) (1UL << (Log2Size))


typedef enum _SMMU_ADDRESS_SIZE_TYPE {
    SmmuAddressSize32Bit = 0,
    SmmuAddressSize36Bit = 1,
    SmmuAddressSize40Bit = 2,
    SmmuAddressSize42Bit = 3,
    SmmuAddressSize44Bit = 4,
    SmmuAddressSize48Bit = 5,
    SmmuAddressSize52Bit = 6,
} SMMU_ADDRESS_SIZE_TYPE, *PSMMU_ADDRESS_SIZE_TYPE;


UINT32
EFIAPI
SmmuV3DecodeAddressWidth (
    IN UINT32 AddressSizeType
    );

UINT8
EFIAPI
SmmuV3EncodeAddressWidth (
    IN UINT32 AddressWidth
    );

UINT32
EFIAPI
SmmuV3ReadRegister32 (
    IN UINT64 SmmuBase,
    IN UINT64 Register
    );

UINT64
EFIAPI
SmmuV3ReadRegister64 (
    IN UINT64 SmmuBase,
    IN UINT64 Register
    );

UINT32
EFIAPI
SmmuV3WriteRegister32 (
    IN UINT64 SmmuBase,
    IN UINT64 Register,
    IN UINT32 Value
    );

UINT64
EFIAPI
SmmuV3WriteRegister64 (
    IN UINT64 SmmuBase,
    IN UINT64 Register,
    IN UINT64 Value
    );

EFI_STATUS
EFIAPI
SmmuV3DisableInterrupts (
    IN UINT64 SmmuBase,
    IN BOOLEAN ClearStaleErrors
    );

EFI_STATUS
EFIAPI
SmmuV3EnableInterrupts (
    IN UINT64 SmmuBase
    );

EFI_STATUS
EFIAPI
SmmuV3Disable (
    IN UINT64 SmmuBase
    );

EFI_STATUS
EFIAPI
SmmuV3Poll (
    IN UINT64 SmmuReg,
    IN UINT32 Mask,
    IN UINT32 Value
    );

#endif
