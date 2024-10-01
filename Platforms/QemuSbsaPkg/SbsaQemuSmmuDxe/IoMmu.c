/** @file IoMmu.c

    This file contains util functions for the IoMmu protocol for the Qemu SBSA platform.

    Copyright (C) Microsoft Corporation. All rights reserved.
    SPDX-License-Identifier: BSD-2-Clause-Patent

    Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
    QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
**/

#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/IoMmu.h>
#include "IoMmu.h"
#include "SmmuV3Registers.h"


EDKII_IOMMU_PROTOCOL  QemuSmmuIoMmu = {
  EDKII_IOMMU_PROTOCOL_REVISION,
  IoMmuSetAttribute,
  IoMmuMap,
  IoMmuUnmap,
  IoMmuAllocateBuffer,
  IoMmuFreeBuffer,
};

typedef struct IOMMU_MAP_INFO {
  UINTN   NumberOfBytes;
  UINT64  VA;
  UINT64  PA;
} IOMMU_MAP_INFO;


STATIC
EFI_STATUS
EFIAPI
UpdateMapping (
  IN PAGE_TABLE* Root,
  IN UINT64 VA,
  IN UINT64 PA,
  IN UINT64 Flags,
  IN BOOLEAN Valid
  )
{
  if (Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PAGE_TABLE* Current = Root;
  UINT64 index;
  UINTN Pages = EFI_SIZE_TO_PAGES(sizeof(PAGE_TABLE));

  for (UINT8 i = 0; i < PAGE_TABLE_DEPTH - 1; i++) {
    index = (VA >> (12 + (9 * (PAGE_TABLE_DEPTH - 1 - i)))) & 0x1FF;
    DEBUG((DEBUG_INFO, "Level %d index = 0x%lx\n", i, index));

    if (Current->entries[index] == 0) {

      PAGE_TABLE * NewPage = (PAGE_TABLE*) ((UINT64) AllocateAlignedPages(Pages, EFI_PAGE_SIZE) & ~0xFFF);
      if (NewPage == 0) {
        return EFI_OUT_OF_RESOURCES;
      }
      ZeroMem((VOID *) NewPage, EFI_PAGES_TO_SIZE(Pages));

      Current->entries[index] = (PageTableEntry) NewPage;

    }

    if (Valid) {
      Current->entries[index] |= 0x1; // valid entry
    }

    Current->entries[index] |= Flags;

    Current = (PAGE_TABLE*) (Current->entries[index] & ~0xFFF);
  }

  // leaf level
  if (Current != 0) {
    index = (VA >> 12) & 0x1FF;
    DEBUG((DEBUG_INFO, "Level 3 index = 0x%lx\n", index));

    if (Valid && (Current->entries[index] & 0x1) != 0) {
      DEBUG((DEBUG_INFO, "Page already mapped\n"));
    }

    // Assign PA and Flags
    Current->entries[index] = (PA & ~0xFFF) | Flags;

    if (Valid) {
      Current->entries[index] |= 0x1; // valid entry
    } else {
      Current->entries[index] &= ~0x1; // only invalidate leaf entry
    }

  }

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
IoMmuMap (
  IN     EDKII_IOMMU_PROTOCOL   *This,
  IN     EDKII_IOMMU_OPERATION  Operation,
  IN     VOID                   *HostAddress,
  IN OUT UINTN                  *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS   *DeviceAddress,
  OUT    VOID                   **Mapping
  )
{
  EFI_STATUS            Status;
  INTN                  Bytes;
  EFI_PHYSICAL_ADDRESS  PhysicalAddress;
  EFI_PHYSICAL_ADDRESS  PhysicalAddressStart;
  IOMMU_MAP_INFO *      MapInfo;

  // Arm Architecture Reference Manual Armv8, for Armv8-A architecture profile: The VMSAv8-64 translation table format descriptors.
  UINT64                Flags = 0x402; // Bit #10 AF = 1, Table/Page Descriptors for levels 0-3 so set bit #1 to 0b'1 for each entry

  // Set R/W bits
  if (Operation == EdkiiIoMmuOperationBusMasterRead || Operation == EdkiiIoMmuOperationBusMasterRead64) {
    Flags |= 1 << 6;
  } else if (Operation == EdkiiIoMmuOperationBusMasterWrite || Operation == EdkiiIoMmuOperationBusMasterWrite64) {
    Flags |= 2 << 6;
  } else if (Operation == EdkiiIoMmuOperationBusMasterCommonBuffer || Operation == EdkiiIoMmuOperationBusMasterCommonBuffer64) {
    Flags |= 3 << 6;
  } else {
    return EFI_INVALID_PARAMETER;
  }

  PhysicalAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress;
  PhysicalAddressStart = PhysicalAddress;
  Bytes = *NumberOfBytes;

  while (Bytes > 0) {
    Status = UpdateMapping(Smmu->PageTableRoot, PhysicalAddress, PhysicalAddress, Flags, TRUE);
    if (EFI_ERROR(Status)) {
      return Status;
    }
    Bytes -= EFI_PAGE_SIZE;
    PhysicalAddress += EFI_PAGE_SIZE;
  }

  *DeviceAddress = PhysicalAddressStart; // Identity mapping

  DEBUG ((DEBUG_INFO, "%a - Operation = %d PageTableRoot = 0x%llx DeviceAddress = 0x%llx PhysicalAddress = 0x%llx NumberOfBytes = %d\n",
        __func__, Operation, Smmu->PageTableRoot, *DeviceAddress, PhysicalAddressStart, *NumberOfBytes));

  SmmuV3PrintErrors(Smmu);

  MapInfo = (IOMMU_MAP_INFO *) AllocateZeroPool(sizeof(IOMMU_MAP_INFO));
  MapInfo->NumberOfBytes = *NumberOfBytes;
  MapInfo->VA = *DeviceAddress;
  MapInfo->PA = PhysicalAddressStart;

  *Mapping = MapInfo;
  Status = EFI_SUCCESS;
  return Status;
}


EFI_STATUS
EFIAPI
IoMmuUnmap (
  IN  EDKII_IOMMU_PROTOCOL  *This,
  IN  VOID                  *Mapping
  )
{
  IOMMU_MAP_INFO *MapInfo = (IOMMU_MAP_INFO *) Mapping;
  if (Mapping == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  EFI_STATUS Status;
  UINT64     PhysicalAddress;
  UINT64     VA;
  INT64      NumberOfBytes;
  SMMUV3_CMD_GENERIC Command;

  PhysicalAddress = MapInfo->PA;
  VA = MapInfo->VA;
  NumberOfBytes = MapInfo->NumberOfBytes;
  DEBUG ((DEBUG_INFO, "%a Mapping->VA = 0x%llx MapInfo->NumberOfBytes = %d\n", __func__, MapInfo->VA, MapInfo->NumberOfBytes));

  while (NumberOfBytes > 0) {
    Status = UpdateMapping(Smmu->PageTableRoot, VA, PhysicalAddress, 0, FALSE);
    if (EFI_ERROR(Status)) {
      return Status;
    }
    NumberOfBytes -= EFI_PAGE_SIZE;
    PhysicalAddress += EFI_PAGE_SIZE;
    VA = PhysicalAddress;
  }

  // Invalidate TLB
  SMMUV3_BUILD_CMD_TLBI_NSNH_ALL(&Command);
  SmmuV3SendCommand(Smmu, &Command);
  SMMUV3_BUILD_CMD_TLBI_EL2_ALL(&Command);
  SmmuV3SendCommand(Smmu, &Command);
  // Issue a CMD_SYNC command to guarantee that any previously issued TLB
  // invalidations (CMD_TLBI_*) are completed (SMMUv3.2 spec section 4.6.3).
  SMMUV3_BUILD_CMD_SYNC_NO_INTERRUPT(&Command);
  SmmuV3SendCommand(Smmu, &Command);

  if (MapInfo != NULL) {
    FreePool(MapInfo);
  }

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
IoMmuFreeBuffer (
  IN  EDKII_IOMMU_PROTOCOL  *This,
  IN  UINTN                 Pages,
  IN  VOID                  *HostAddress
  )
{
  return gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress, Pages);
}


EFI_STATUS
EFIAPI
IoMmuAllocateBuffer (
  IN     EDKII_IOMMU_PROTOCOL  *This,
  IN     EFI_ALLOCATE_TYPE     Type,
  IN     EFI_MEMORY_TYPE       MemoryType,
  IN     UINTN                 Pages,
  IN OUT VOID                  **HostAddress,
  IN     UINT64                Attributes
  )
{
  EFI_STATUS                Status;
  EFI_PHYSICAL_ADDRESS      PhysicalAddress;

  Status = gBS->AllocatePages (
              Type,
              MemoryType,
              Pages,
              &PhysicalAddress
              );
  if (!EFI_ERROR (Status)) {
      *HostAddress = (VOID *)(UINTN)PhysicalAddress;
  }
  DEBUG ((DEBUG_INFO, "%a PhysicalAddress = 0x%llx\n", __func__, PhysicalAddress));
  return Status;
}


EFI_STATUS
EFIAPI
IoMmuSetAttribute (
  IN EDKII_IOMMU_PROTOCOL  *This,
  IN EFI_HANDLE            DeviceHandle,
  IN VOID                  *Mapping,
  IN UINT64                IoMmuAccess
  )
{
  DEBUG ((DEBUG_INFO, "%a - Access = 0x%llx\n", __func__, IoMmuAccess));
  // IOMMU_MAP_INFO *MapInfo = (IOMMU_MAP_INFO *) Mapping;
  // if (Mapping == NULL) {
  //   return EFI_SUCCESS;
  // }

  // if (IoMmuAccess == 0) {
  //   return EFI_SUCCESS;
  // }

  // EFI_STATUS Status;
  // UINT64     PhysicalAddress;
  // UINT64     VA;
  // INT64      NumberOfBytes;

  // PhysicalAddress = MapInfo->PA;
  // VA = MapInfo->VA;
  // NumberOfBytes = MapInfo->NumberOfBytes;
  // DEBUG ((DEBUG_INFO, "%a Mapping->VA = 0x%llx MapInfo->NumberOfBytes = %d\n", __func__, MapInfo->VA, MapInfo->NumberOfBytes));

  // while (NumberOfBytes > 0) {
  //   Status = UpdateMapping(Smmu->PageTableRoot, VA, PhysicalAddress, IoMmuAccess << 6, TRUE);
  //   if (EFI_ERROR(Status)) {
  //     return Status;
  //   }
  //   NumberOfBytes -= EFI_PAGE_SIZE;
  //   PhysicalAddress += EFI_PAGE_SIZE;
  //   VA = PhysicalAddress;
  // }

  return EFI_SUCCESS;
}


PAGE_TABLE*
EFIAPI
PageTableInit (IN UINT8 Level) {
  if (Level >= PAGE_TABLE_DEPTH) {
    return NULL;
  }

  UINTN Pages = EFI_SIZE_TO_PAGES(sizeof(PAGE_TABLE));

  PAGE_TABLE *PageTable = (PAGE_TABLE *) ((UINT64) AllocateAlignedPages(Pages, EFI_PAGE_SIZE) & ~0xFFF);

  if (PageTable == NULL) {
    return NULL;
  }

  ZeroMem(PageTable, EFI_PAGES_TO_SIZE(Pages));

  // PageTable->entries[0] = (PageTableEntry)PageTableInit(Level + 1) & ~0xFFF;

  DEBUG ((DEBUG_INFO, "%a - Created SmmuV3 Page Table. Pages = 0x%llx\n", __func__, Pages));

  ASSERT(PageTable != NULL);
  return PageTable;
}


VOID
EFIAPI
PageTableDeInit (
  IN UINT8 Level,
  IN PAGE_TABLE *PageTable
  )
{
  if (Level >= PAGE_TABLE_DEPTH || PageTable == NULL) {
    return;
  }

  // Iterate through the entries of the current page table
  for (UINTN i = 0; i < PAGE_TABLE_SIZE; i++) {
    PageTableEntry entry = PageTable->entries[i];

    // If the entry is a pointer to another page table, recursively deinit it
    if (entry != 0) {
      PageTableDeInit(Level + 1, (PAGE_TABLE *)(entry & ~0xFFF));
    }
  }

  // Free the current page table
  FreePages(PageTable, EFI_SIZE_TO_PAGES(sizeof(PAGE_TABLE)));
}


EFI_STATUS
EFIAPI
IoMmuInit () {
  EFI_STATUS Status;
  EFI_HANDLE  Handle;
  
  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEdkiiIoMmuProtocolGuid,
                  &QemuSmmuIoMmu,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
      DEBUG((DEBUG_ERROR, "InitializeSbsaQemuSmmuDxe: Failed to install gEdkiiIoMmuProtocolGuid\n"));
      return Status;
  }
  return Status;
}
