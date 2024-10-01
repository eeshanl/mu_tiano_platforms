/** @file SbsaQemuSmmuDxe.c

    This file contains functions for the SMMU driver for the Qemu SBSA platform.

    Copyright (C) Microsoft Corporation. All rights reserved.
    SPDX-License-Identifier: BSD-2-Clause-Patent

    Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
    QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
**/

#include <IndustryStandard/IoRemappingTable.h>
#include <IndustryStandard/SbsaQemuAcpi.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HardwareInfoLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>

#include "SbsaQemuIort.h"
#include "smmuv3.h"
#include "smmuv3registers.h"

/*
 * A function that add the IORT ACPI table.
  IN EFI_ACPI_COMMON_HEADER    *CurrentTable
 */
EFI_STATUS
AddIortTable(IN EFI_ACPI_TABLE_PROTOCOL *AcpiTable) {
    EFI_STATUS Status;
    UINTN TableHandle;
    UINT32 TableSize;
    EFI_PHYSICAL_ADDRESS PageAddress;
    UINT8 *New;

    // Initialize IORT ACPI Header
    EFI_ACPI_6_0_IO_REMAPPING_TABLE Header = {
        SBSAQEMU_ACPI_HEADER(EFI_ACPI_6_0_IO_REMAPPING_TABLE_SIGNATURE,
                             SBSA_IO_REMAPPING_STRUCTURE,
                             EFI_ACPI_IO_REMAPPING_TABLE_REVISION_00),
        3,
        sizeof(EFI_ACPI_6_0_IO_REMAPPING_TABLE),  // NodeOffset
        0};

    // Initialize SMMU3 Structure
    SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE Smmu3 = {
        {
            {
                EFI_ACPI_IORT_TYPE_SMMUv3,
                sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE),
                2,  // Revision
                0,  // Reserved
                1,  // NumIdMapping
                OFFSET_OF(SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE,
                          SmmuIdMap)  // IdReference
            },
            PcdGet64(PcdSmmuBase),                     // Base address
            EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE,  // Flags
            0,                                         // Reserved
            0,                                         // VATOS address
            EFI_ACPI_IORT_SMMUv3_MODEL_GENERIC,        // SMMUv3 Model
            74,                                        // Event
            75,                                        // Pri
            77,                                        // Gerror
            76,                                        // Sync
            0,                                         // Proximity domain
            1                                          // DevIDMappingIndex
        },
        {
            0x0000,                                           // InputBase
            0xffff,                                           // NumIds
            0x0000,                                           // OutputBase
            OFFSET_OF(SBSA_IO_REMAPPING_STRUCTURE, ItsNode),  // OutputReference
            0                                                 // Flags
        }};

    // NOTE(hrw): update to IORT E.e?
    SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE Rc = {
        {
            {
                EFI_ACPI_IORT_TYPE_ROOT_COMPLEX,                 // Type
                sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE),  // Length
                0,                                               // Revision
                0,                                               // Reserved
                1,  // NumIdMappings
                OFFSET_OF(SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE,
                          RcIdMap)  // IdReference
            },
            1,                                           // CacheCoherent
            0,                                           // AllocationHints
            0,                                           // Reserved
            1,                                           // MemoryAccessFlags
            EFI_ACPI_IORT_ROOT_COMPLEX_ATS_UNSUPPORTED,  // AtsAttribute
            0x0,                                         // PciSegmentNumber
            // 0,       //MemoryAddressSizeLimit
        },
        {
            0x0000,  // InputBase
            0xffff,  // NumIds
            0x0000,  // OutputBase
            OFFSET_OF(SBSA_IO_REMAPPING_STRUCTURE,
                      SmmuNode),  // OutputReference
            0,                    // Flags
        }};

    SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE Its = {
        // EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE
        {
            // EFI_ACPI_6_0_IO_REMAPPING_NODE
            {
                EFI_ACPI_IORT_TYPE_ITS_GROUP,                     // Type
                sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE),  // Length
                0,                                                // Revision
                0,                                                // Identifier
                0,  // NumIdMappings
                0,  // IdReference
            },
            1,  // ITS count
        },
        0,  // GIC ITS Identifiers
    };

    // Calculate the new table size based on the number of cores
    TableSize = sizeof(EFI_ACPI_6_0_IO_REMAPPING_TABLE) +
                sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE) +
                sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE) +
                sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE);

    Status = gBS->AllocatePages(AllocateAnyPages, EfiACPIReclaimMemory,
                                EFI_SIZE_TO_PAGES(TableSize), &PageAddress);
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "Failed to allocate pages for IORT table\n"));
        return EFI_OUT_OF_RESOURCES;
    }

    New = (UINT8 *)(UINTN)PageAddress;
    ZeroMem(New, TableSize);

    // Add the  ACPI Description table header
    CopyMem(New, &Header, sizeof(EFI_ACPI_6_0_IO_REMAPPING_TABLE));
    ((EFI_ACPI_DESCRIPTION_HEADER *)New)->Length = TableSize;
    New += sizeof(EFI_ACPI_6_0_IO_REMAPPING_TABLE);

    // ITS Node
    CopyMem(New, &Its, sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE));
    New += sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE);

    // SMMUv3 Node
    CopyMem(New, &Smmu3, sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE));
    New += sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE);

    // RC Node
    CopyMem(New, &Rc, sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE));
    New += sizeof(SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE);

    AcpiPlatformChecksum((UINT8 *)PageAddress, TableSize);

    Status = AcpiTable->InstallAcpiTable(AcpiTable,
                                         (EFI_ACPI_COMMON_HEADER *)PageAddress,
                                         TableSize, &TableHandle);
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "Failed to install IORT table\n"));
    }

    return Status;
}


/**
  Initialise the SMMUv3 to set it in ABORT mode and stop DMA.

  @param [in]  SmmuReg    Base address of the SMMUv3.

  @retval EFI_SUCCESS     Success.
  @retval EFI_TIMEOUT     Timeout.
**/
EFI_STATUS
EFIAPI
SmmuV3GlobalAbort (
  IN  UINT64 SmmuBase
  )
{
  EFI_STATUS  Status;
  UINT32      RegVal;

  // Attribute update has completed when SMMU_(S)_GBPA.Update bit is 0.
  Status = SmmuV3Poll (SmmuBase + SMMU_GBPA, SMMU_GBPA_UPDATE, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // SMMU_(S)_CR0 resets to zero with all streams bypassing the SMMU,
  // so just abort all incoming transactions.
  RegVal = MmioRead32 (SmmuBase + SMMU_GBPA);

  // Set the SMMU_GBPA.ABORT and SMMU_GBPA.UPDATE.
  RegVal |= (SMMU_GBPA_ABORT | SMMU_GBPA_UPDATE);

  MmioWrite32 (SmmuBase + SMMU_GBPA, RegVal);

  // Attribute update has completed when SMMU_(S)_GBPA.Update bit is 0.
  Status = SmmuV3Poll (SmmuBase + SMMU_GBPA, SMMU_GBPA_UPDATE, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Sanity check to see if abort is set
  Status = SmmuV3Poll (SmmuBase + SMMU_GBPA, SMMU_GBPA_ABORT,
  SMMU_GBPA_ABORT); if (EFI_ERROR (Status)) {
    return Status;
  }
  DEBUG ((DEBUG_INFO, "SmmuV3GlobalAbort: abort bit = 1\n"));

  return EFI_SUCCESS;
}

/**
  Initialise the SMMUv3 to set Non-secure streams to bypass the SMMU.

  @param [in]  SmmuReg    Base address of the SMMUv3.

  @retval EFI_SUCCESS     Success.
  @retval EFI_TIMEOUT     Timeout.
**/
EFI_STATUS
EFIAPI
SmmuV3SetGlobalBypass(IN UINT64 SmmuBase) {
    EFI_STATUS Status;
    UINT32 RegVal;

    // Attribute update has completed when SMMU_(S)_GBPA.Update bit is 0.
    Status = SmmuV3Poll(SmmuBase + SMMU_GBPA, SMMU_GBPA_UPDATE, 0);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // SMMU_(S)_CR0 resets to zero with all streams bypassing the SMMU
    RegVal = MmioRead32(SmmuBase + SMMU_GBPA);

    // TF-A configures the SMMUv3 to abort all incoming transactions.
    // Clear the SMMU_GBPA.ABORT to allow Non-secure streams to bypass
    // the SMMU.
    RegVal &= ~SMMU_GBPA_ABORT;
    RegVal |= SMMU_GBPA_UPDATE;

    MmioWrite32(SmmuBase + SMMU_GBPA, RegVal);

    Status = SmmuV3Poll(SmmuBase + SMMU_GBPA, SMMU_GBPA_UPDATE, 0);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    return EFI_SUCCESS;
}

VOID * 
EFIAPI
SmmuV3AllocateEventQueue (
    IN UINT64  SmmuBase,
    OUT UINT32* QueueLog2Size
    )
{
    UINT32 QueueSize;
    SMMUV3_IDR1 Idr1 = (SMMUV3_IDR1) SmmuV3ReadRegister32 (SmmuBase, SMMU_IDR1);

    *QueueLog2Size = MIN(Idr1.EventQs, SMMUV3_EVENT_QUEUE_OS_LOG2ENTRIES);
    QueueSize = SMMUV3_EVENT_QUEUE_SIZE_FROM_LOG2(*QueueLog2Size);
    return AllocateZeroPool (QueueSize);
}

VOID * 
EFIAPI
SmmuV3AllocateCommandQueue (
    IN UINT64   SmmuBase,
    OUT UINT32* QueueLog2Size
    )
{
    UINT32 QueueSize;
    SMMUV3_IDR1 Idr1 = (SMMUV3_IDR1) SmmuV3ReadRegister32 (SmmuBase, SMMU_IDR1);

    *QueueLog2Size = MIN(Idr1.CmdQs, SMMUV3_COMMAND_QUEUE_OS_LOG2ENTRIES);
    QueueSize = SMMUV3_COMMAND_QUEUE_SIZE_FROM_LOG2(*QueueLog2Size);
    return AllocateZeroPool (QueueSize);
}

VOID
EFIAPI
SmmuV3FreeQueue (
    IN VOID   *QueuePtr
    )
{
    FreePool(QueuePtr);
}

EFI_STATUS
EFIAPI
SmmuV3BuildStreamTable (
    IN UINT64                      SmmuBase,
    IN UINT32                      Log2Size,
    OUT PSMMUV3_STREAM_TABLE_ENTRY StreamEntry
    )
{
    /* WIP: Fix StreamEntry bits as needed */
    EFI_STATUS Status;
    UINT32 OutputAddressWidth;
    UINT32 InputSize;
    UINT8 IortCohac = 1;
    UINT8 Httu = 0;
    UINT8 CCA = 1;
    UINT8 CPM = 1;
    UINT8 DACS = 0;

    SMMUV3_IDR0 Idr0;
    SMMUV3_IDR1 Idr1;
    SMMUV3_IDR5 Idr5;
    if (StreamEntry == NULL || SmmuBase == 0) {
        return EFI_INVALID_PARAMETER;
    }
    ZeroMem(StreamEntry, sizeof(SMMUV3_STREAM_TABLE_ENTRY));

    Idr0.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IDR0);
    Idr1.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IDR1);

    Idr5.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IDR5);
    DEBUG((DEBUG_INFO, "0x%lx 0x%lx 0x%lx\n", Idr0.Cohacc, Idr1.AttrTypesOvr, Idr5.AsUINT32));
     // 0x6 = stage2 translate stage1 bypass
     // 0x4 == stage2 bypass stage1 bypass
    StreamEntry->Config = 0x4;
    StreamEntry->Eats = 0; // ATS not supported
    StreamEntry->S2Vmid = 10; //??? Domain->Vmid; Choose a none zero value
    StreamEntry->S2Tg = 0; // 4KB granule size
    StreamEntry->S2Aa64 = 1; // AArch64 S2 translation tables
    StreamEntry->S2Ttb = 0; //??? Domain->Settings.S2.PageTableRoot >> 4;
    if (Idr0.S1p == 1 && Idr0.S2p == 1) {
        StreamEntry->S2Ptw = 1;
    }
    StreamEntry->S2Sl0 = 0; //??? 2-level page table

    //
    // Set the maximum output address width. Per SMMUv3.2 spec (sections 5.2 and
    // 3.4.1), the maximum input address width with AArch64 format is given by
    // SMMU_IDR5.OAS field and capped at:
    // - 48 bits in SMMUv3.0,
    // - 52 bits in SMMUv3.1+. However, an address greater than 48 bits can
    //   only be output from stage 2 when a 64KB translation granule is in use
    //   for that translation table, which is not currently supported (only 4KB
    //   granules).
    //
    //  Thus the maximum input address width is restricted to 48-bits even if
    //  it is advertised to be larger.
    //
    OutputAddressWidth = SmmuV3DecodeAddressWidth(Idr5.Oas);
    if (OutputAddressWidth < 48) {
        StreamEntry->S2Ps = SmmuV3EncodeAddressWidth(OutputAddressWidth);
    } else {
        StreamEntry->S2Ps = SmmuV3EncodeAddressWidth(48);
    }
    InputSize = OutputAddressWidth;
    StreamEntry->S2T0Sz = 64 - InputSize;
    if (IortCohac != 0) {
        StreamEntry->S2Ir0 = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE;
        StreamEntry->S2Or0 = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE;
        StreamEntry->S2Sh0 = ARM64_SHATTR_INNER_SHAREABLE;
    } else {
        StreamEntry->S2Ir0 = ARM64_RGNCACHEATTR_NONCACHEABLE;
        StreamEntry->S2Or0 = ARM64_RGNCACHEATTR_NONCACHEABLE;
        StreamEntry->S2Sh0 = ARM64_SHATTR_OUTER_SHAREABLE;
    }
    StreamEntry->S2Had = Httu;
    StreamEntry->S2Rs = 0x2; // record faults

    // TODO: implementation registers Impl0, Impl1, Impl2 are ignored

    if (Idr1.AttrTypesOvr != 0) {
        StreamEntry->ShCfg = 0x1;
    }

    if (Idr1.AttrTypesOvr != 0 && (CCA == 1 && CPM == 1 && DACS == 0)) {
        StreamEntry->Mtcfg = 0x1;
        StreamEntry->MemAttr = 0xF; // Inner+Outer write-back cached
        StreamEntry->ShCfg = 0x3; // Inner shareable
    }
    // TODO: Ignore for now
    // StreamEntry->AllocCfg = 0;

    StreamEntry->Valid = 1;

    Status = EFI_SUCCESS;
    return Status;
}

PSMMUV3_STREAM_TABLE_ENTRY
EFIAPI
SmmuV3AllocateStreamTable (
    IN UINT64  SmmuBase,
    IN UINT32  OutputBase,
    IN UINT32  NumIds,
    OUT UINT32 *Log2Size,
    OUT UINT32 *Size
    )
{
    UINT32 MaxStreamId;
    UINT32 SidMsb;
    UINT32 Alignment;
    UINTN Pages;

    MaxStreamId = OutputBase + NumIds;
    SidMsb = HighBitSet32 (MaxStreamId);
    *Log2Size = SidMsb + 1;
    *Size = SMMUV3_LINEAR_STREAM_TABLE_SIZE_FROM_LOG2(*Log2Size);
    *Size = ROUND_UP(*Size, SMMU_MMIO_PAGE_SIZE);
    Alignment = ALIGN_UP_BY(*Size, SMMU_MMIO_PAGE_SIZE);
    Pages = EFI_SIZE_TO_PAGES(*Size);
    VOID * AllocatedAddress = AllocateAlignedPages (Pages, Alignment);
    DEBUG((DEBUG_INFO,
           "AllocateAlignedPages() Address=%llx Pages = %d Log2Size = %d Size = %d Alignment = %d Entries = %d\n",
           AllocatedAddress,
           Pages, *Log2Size, *Size, Alignment,
           *Size / sizeof(SMMUV3_STREAM_TABLE_ENTRY)));
    ASSERT(AllocatedAddress != NULL);
    ZeroMem (AllocatedAddress, *Size);
    return (PSMMUV3_STREAM_TABLE_ENTRY) AllocatedAddress;
}

VOID
EFIAPI
SmmuV3FreeStreamTable (
    IN PSMMUV3_STREAM_TABLE_ENTRY StreamTablePtr,
    IN UINT32 Size
    )
{
    UINTN Pages;
    Pages = EFI_SIZE_TO_PAGES(Size);
    FreeAlignedPages ((VOID *) StreamTablePtr, Pages);
}

EFI_STATUS
EFIAPI
SmmuV3Configure (
    IN UINT64  SmmuBase,
    IN UINTN   IortCohac
    )
{
    EFI_STATUS Status;
    UINT32 STLog2Size;
    UINT32 STSize;
    UINT32 CommandQueueLog2Size;
    UINT32 EventQueueLog2Size;
    UINT8 ReadWriteAllocationHint;
    SMMUV3_STRTAB_BASE StrTabBase;
    SMMUV3_STRTAB_BASE_CFG StrTabBaseCfg;
    PSMMUV3_STREAM_TABLE_ENTRY StreamTablePtr;
    SMMUV3_CMDQ_BASE CommandQueueBase;
    SMMUV3_EVENTQ_BASE EventQueueBase;
    SMMUV3_STREAM_TABLE_ENTRY TemplateStreamEntry;
    SMMUV3_CR0 Cr0;
    SMMUV3_CR1 Cr1;
    SMMUV3_CR2 Cr2;
    SMMUV3_IDR0 Idr0;
    SMMUV3_GERROR GError;

    // // Configure SMMUv3 to Abort all transactions.
    // Status = SmmuV3GlobalAbort (SmmuBase);
    // if (EFI_ERROR (Status)) {
    //   DEBUG ((
    //     DEBUG_ERROR,
    //     "SbsaQemuSmmuDxe: Failed to set SMMUv3 in abort mode.\n"
    //   ));
    //   return Status;
    // }

    // Status = SmmuV3SetGlobalBypass(SmmuBase);
    // if (EFI_ERROR(Status)) {
    //     DEBUG((DEBUG_ERROR, "Error SmmuV3SetGlobalBypass: SmmuBase=0x%lx\n", SmmuBase));
    //     goto End;
    // }

    if (IortCohac != 0) {
        ReadWriteAllocationHint = 0x1;
    } else {
        ReadWriteAllocationHint = 0x0;
    }
    GError.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_GERROR);
    DEBUG((DEBUG_INFO, "GError: 0x%lx\n", GError.AsUINT32));
    ASSERT(GError.AsUINT32 == 0);
    
    // Disable SMMU before configuring
    Status = SmmuV3Disable(SmmuBase);
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "Error SmmuV3Disable: SmmuBase=0x%lx\n", SmmuBase));
        goto End;
    }

    Status = SmmuV3DisableInterrupts(SmmuBase, TRUE);
    if(EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "Error SmmuV3DisableInterrupts: SmmuBase=0x%lx\n", SmmuBase));
        goto End;
    }

    StreamTablePtr = SmmuV3AllocateStreamTable(SmmuBase, 0, 0xFFFF, &STLog2Size, &STSize);
    ASSERT(StreamTablePtr != NULL);
    // Build default STE template
    Status = SmmuV3BuildStreamTable(SmmuBase, STLog2Size, &TemplateStreamEntry);
    if (EFI_ERROR(Status)) {
        SmmuV3FreeStreamTable(StreamTablePtr, STSize);
        DEBUG((DEBUG_ERROR, "Error SmmuV3BuildStreamTable: SmmuBase=0x%lx\n", SmmuBase));
        goto End;
    }
    // Load default STE values
    for (UINT32 i = 0; i < SMMUV3_COUNT_FROM_LOG2(STLog2Size); i++) {
        CopyMem(&StreamTablePtr[i], &TemplateStreamEntry, sizeof(SMMUV3_STREAM_TABLE_ENTRY));
    }


    VOID* CommandQueue = SmmuV3AllocateCommandQueue(SmmuBase, &CommandQueueLog2Size);
    VOID* EventQueue = SmmuV3AllocateEventQueue(SmmuBase, &EventQueueLog2Size);
    ASSERT(CommandQueue != NULL);
    ASSERT(EventQueue != NULL);

    // Configure Stream Table Base
    StrTabBaseCfg.AsUINT32 = 0;
    StrTabBaseCfg.Fmt = 0; // Linear format
    StrTabBaseCfg.Log2Size = STLog2Size;

    SmmuV3WriteRegister32(SmmuBase, SMMU_STRTAB_BASE_CFG, StrTabBaseCfg.AsUINT32);

    StrTabBase.AsUINT64 = 0;
    StrTabBase.Ra = ReadWriteAllocationHint;
    StrTabBase.Addr = ((UINT64) StreamTablePtr) >> 6;
    SmmuV3WriteRegister64(SmmuBase, SMMU_STRTAB_BASE, StrTabBase.AsUINT64);

    // Configure Command Queue Base
    CommandQueueBase.AsUINT64 = 0;
    CommandQueueBase.Log2Size = CommandQueueLog2Size;
    CommandQueueBase.Addr = ((UINT64) CommandQueue) >> 5;
    CommandQueueBase.Ra = ReadWriteAllocationHint;
    SmmuV3WriteRegister64(SmmuBase, SMMU_CMDQ_BASE, CommandQueueBase.AsUINT64);
    SmmuV3WriteRegister32(SmmuBase, SMMU_CMDQ_PROD, 0);
    SmmuV3WriteRegister32(SmmuBase, SMMU_CMDQ_CONS, 0);


    // Configure Event Queue Base
    EventQueueBase.AsUINT64 = 0;
    EventQueueBase.Log2Size = EventQueueLog2Size;
    EventQueueBase.Addr = ((UINT64) EventQueue) >> 5;
    EventQueueBase.Wa = ReadWriteAllocationHint;
    SmmuV3WriteRegister64(SmmuBase, SMMU_EVENTQ_BASE, EventQueueBase.AsUINT64);
    SmmuV3WriteRegister32(SmmuBase, SMMU_EVENTQ_PROD, 0);
    SmmuV3WriteRegister32(SmmuBase, SMMU_EVENTQ_CONS, 0);

    // Enable GError and event interrupts
    Status = SmmuV3EnableInterrupts(SmmuBase);
    if(EFI_ERROR(Status)) {
        SmmuV3FreeStreamTable(StreamTablePtr, STSize);
        SmmuV3FreeQueue(CommandQueue);
        SmmuV3FreeQueue(EventQueue);
        DEBUG((DEBUG_ERROR, "Error SmmuV3DisableInterrupts: SmmuBase=0x%lx\n", SmmuBase));
        goto End;
    }

    // Configure CR1
    Cr1.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_CR1);
    Cr1.AsUINT32 &= ~SMMUV3_CR1_VALID_MASK;
    if (IortCohac != 0) {
        Cr1.QueueIc = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE; // WBC
        Cr1.QueueOc = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE; // WBC
        Cr1.QueueSh = ARM64_SHATTR_INNER_SHAREABLE; // Inner-shareable
    }
    SmmuV3WriteRegister32(SmmuBase, SMMU_CR1, Cr1.AsUINT32);

    // Configure CR2
    Cr2.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_CR2);
    Cr2.AsUINT32 &= ~SMMUV3_CR2_VALID_MASK;
    Cr2.E2h = 0;
    Cr2.RecInvSid = 1; // Record C_BAD_STREAMID for invalid input streams.

    //
    // If broadcast TLB maintenance (BTM) is not enabled, then configure
    // private TLB maintenance (PTM). Per spec (section 6.3.12), the PTM bit is
    // only valid when BTM is indicated as supported.
    //
    Idr0.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IDR0);
    if (Idr0.Btm == 1) {
        DEBUG((DEBUG_INFO, "BTM = 1\n"));
        Cr2.Ptm = 1; // Private TLB maintenance.
    }
    SmmuV3WriteRegister32(SmmuBase, SMMU_CR2, Cr2.AsUINT32);

    // Configure CR0 part1
    ArmDataSynchronizationBarrier(); // DSB

    Cr0.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_CR0);
    Cr0.EventQEn = 1;
    Cr0.CmdQEn = 1;

    SmmuV3WriteRegister32(SmmuBase, SMMU_CR0, Cr0.AsUINT32);
    Status = SmmuV3Poll(SmmuBase + SMMU_CR0ACK, 0xC, 0xC);
    if (EFI_ERROR(Status)) {
        SmmuV3FreeStreamTable(StreamTablePtr, STSize);
        SmmuV3FreeQueue(CommandQueue);
        SmmuV3FreeQueue(EventQueue);
        DEBUG((DEBUG_ERROR, "Error SmmuV3Poll: 0x%lx\n", SmmuBase + SMMU_CR0ACK));
        goto End;
    }


    //
    // TODO:
    // Invalidate all cached configuration and TLB entries
    //


    // Configure CR0 part2
    Cr0.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_CR0);
    ArmDataSynchronizationBarrier(); // DSB

    Cr0.AsUINT32 = Cr0.AsUINT32 & ~SMMUV3_CR0_VALID_MASK;
    Cr0.SmmuEn = 1;
    Cr0.EventQEn = 1;
    Cr0.CmdQEn = 1;
    Cr0.PriQEn = 0;
    Cr0.Vmw = 0; // Disable VMID wildcard matching.
    Idr0.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IDR0);
    if (Idr0.Ats != 0) {
        Cr0.AtsChk = 1; // disable bypass for ATS translated traffic.
    }
    SmmuV3WriteRegister32(SmmuBase, SMMU_CR0, Cr0.AsUINT32);
    Status = SmmuV3Poll(SmmuBase + SMMU_CR0ACK, SMMUV3_CR0_SMMU_EN_MASK, SMMUV3_CR0_SMMU_EN_MASK);
    if (EFI_ERROR(Status)) {
        SmmuV3FreeStreamTable(StreamTablePtr, STSize);
        SmmuV3FreeQueue(CommandQueue);
        SmmuV3FreeQueue(EventQueue);
        DEBUG((DEBUG_ERROR, "Error SmmuV3Poll: 0x%lx\n", SmmuBase + SMMU_CR0ACK));
        goto End;
    }

    ArmDataSynchronizationBarrier(); // DSB

    GError.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_GERROR);
    DEBUG((DEBUG_INFO, "GError: 0x%lx\n", GError.AsUINT32));

End:
    return Status;
}

/**
 * Entrypoint for SbsaQemuSmmuDxe drivers
 * See UEFI specification for the details of the parameters
 */
EFI_STATUS
EFIAPI
InitializeSbsaQemuSmmuDxe(IN EFI_HANDLE ImageHandle,
                          IN EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    EFI_ACPI_TABLE_PROTOCOL *AcpiTable;
    UINT64 SmmuBase;

    DEBUG((DEBUG_INFO, "SbsaQemuSmmuDxe: called\n"));

    // Check if ACPI Table Protocol has been installed
    Status = gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid, NULL,
                                 (VOID **)&AcpiTable);
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR,
               "SbsaQemuSmmuDxe: Failed to locate ACPI Table Protocol\n"));
        return Status;
    }

    // Add IORT Table
    if (PcdGet64(PcdGicItsBase) > 0) {
        Status = AddIortTable(AcpiTable);
        if (EFI_ERROR(Status)) {
            DEBUG((DEBUG_ERROR, "SbsaQemuSmmuDxe: Failed to add IORT table\n"));
            return Status;
        }
        DEBUG((DEBUG_INFO, "SbsaQemuSmmuDxe: Successfully added IORT table\n"));
    }

    // Get SMMUv3 base address from PCD
    SmmuBase = PcdGet64(PcdSmmuBase);
    DEBUG((DEBUG_INFO, "SbsaQemuSmmuDxe: PcdGet64(PcdSmmuBase)=0x%llx\n",
           SmmuBase));

    Status = SmmuV3Configure(SmmuBase, 1);
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "SmmuV3Configure: Failed to configure\n"));
        return Status;
    }
    DEBUG((DEBUG_INFO, "SbsaQemuSmmuDxe: Done Status=%x\n", Status));
    return Status;
}
