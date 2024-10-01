/** @file SbsaQemuSmmuDxe.c

    This file contains functions for the SMMU driver for the Qemu SBSA platform.

    Copyright (C) Microsoft Corporation. All rights reserved.
    SPDX-License-Identifier: BSD-2-Clause-Patent

    Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
    QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
**/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <IndustryStandard/IoRemappingTable.h>
#include <IndustryStandard/SbsaQemuAcpi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HardwareInfoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>
#include "SbsaQemuIort.h"
#include "smmuv3.h"

/*
 * A function that add the IORT ACPI table.
  IN EFI_ACPI_COMMON_HEADER    *CurrentTable
 */
EFI_STATUS
AddIortTable (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable
  )
{
  EFI_STATUS            Status;
  UINTN                 TableHandle;
  UINT32                TableSize;
  EFI_PHYSICAL_ADDRESS  PageAddress;
  UINT8                 *New;

  // Initialize IORT ACPI Header
  EFI_ACPI_6_0_IO_REMAPPING_TABLE  Header = {
    SBSAQEMU_ACPI_HEADER (
      EFI_ACPI_6_0_IO_REMAPPING_TABLE_SIGNATURE,
      SBSA_IO_REMAPPING_STRUCTURE,
      EFI_ACPI_IO_REMAPPING_TABLE_REVISION_00
      ),
    3,
    sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE),        // NodeOffset
    0
  };

  // Initialize SMMU3 Structure
  SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE  Smmu3 = {
    {
      {
        EFI_ACPI_IORT_TYPE_SMMUv3,
        sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE),
        2,                                                               // Revision
        0,                                                               // Reserved
        1,                                                               // NumIdMapping
        OFFSET_OF (SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE, SmmuIdMap) // IdReference
      },
      PcdGet64 (PcdSmmuBase),                   // Base address
      EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE, // Flags
      0,                                        // Reserved
      0,                                        // VATOS address
      EFI_ACPI_IORT_SMMUv3_MODEL_GENERIC,       // SMMUv3 Model
      74,                                       // Event
      75,                                       // Pri
      77,                                       // Gerror
      76,                                       // Sync
      0,                                        // Proximity domain
      1                                         // DevIDMappingIndex
    },
    {
      0x0000,                                           // InputBase
      0xffff,                                           // NumIds
      0x0000,                                           // OutputBase
      OFFSET_OF (SBSA_IO_REMAPPING_STRUCTURE, ItsNode), // OutputReference
      0                                                 // Flags
    }
  };

  // NOTE(hrw): update to IORT E.e?
  SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE  Rc = {
    {
      {
        EFI_ACPI_IORT_TYPE_ROOT_COMPLEX,                            // Type
        sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE),            // Length
        0,                                                          // Revision
        0,                                                          // Reserved
        1,                                                          // NumIdMappings
        OFFSET_OF (SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE, RcIdMap) // IdReference
      },
      1,                                          // CacheCoherent
      0,                                          // AllocationHints
      0,                                          // Reserved
      1,                                          // MemoryAccessFlags
      EFI_ACPI_IORT_ROOT_COMPLEX_ATS_UNSUPPORTED, // AtsAttribute
      0x0,                                        // PciSegmentNumber
      // 0,       //MemoryAddressSizeLimit
    },
    {
      0x0000,                                            // InputBase
      0xffff,                                            // NumIds
      0x0000,                                            // OutputBase
      OFFSET_OF (SBSA_IO_REMAPPING_STRUCTURE, SmmuNode), // OutputReference
      0,                                                 // Flags
    }
  };

  SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE  Its = {
    // EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE
    {
      // EFI_ACPI_6_0_IO_REMAPPING_NODE
      {
        EFI_ACPI_IORT_TYPE_ITS_GROUP,                     // Type
        sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE), // Length
        0,                                                // Revision
        0,                                                // Identifier
        0,                                                // NumIdMappings
        0,                                                // IdReference
      },
      1,    // ITS count
    },
    0,      // GIC ITS Identifiers
  };

  // Calculate the new table size based on the number of cores
  TableSize = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE) +
              sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE) +
              sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE) +
              sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE);

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiACPIReclaimMemory,
                  EFI_SIZE_TO_PAGES (TableSize),
                  &PageAddress
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to allocate pages for IORT table\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  New = (UINT8 *)(UINTN)PageAddress;
  ZeroMem (New, TableSize);

  // Add the  ACPI Description table header
  CopyMem (New, &Header, sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE));
  ((EFI_ACPI_DESCRIPTION_HEADER *)New)->Length = TableSize;
  New                                         += sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE);

  // ITS Node
  CopyMem (New, &Its, sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE));
  New += sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_ITS_NODE);

  // SMMUv3 Node
  CopyMem (New, &Smmu3, sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE));
  New += sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_SMMU3_NODE);

  // RC Node
  CopyMem (New, &Rc, sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE));
  New += sizeof (SBSA_EFI_ACPI_6_0_IO_REMAPPING_RC_NODE);

  AcpiPlatformChecksum ((UINT8 *)PageAddress, TableSize);

  Status = AcpiTable->InstallAcpiTable (
                        AcpiTable,
                        (EFI_ACPI_COMMON_HEADER *)PageAddress,
                        TableSize,
                        &TableHandle
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to install IORT table\n"));
  }

  return Status;
}

/**
  Poll the SMMU register and test the value based on the mask.

  @param [in]  SmmuReg    Base address of the SMMU register.
  @param [in]  Mask       Mask of register bits to monitor.
  @param [in]  Value      Expected value.

  @retval EFI_SUCCESS     Success.
  @retval EFI_TIMEOUT     Timeout.
**/
STATIC
EFI_STATUS
EFIAPI
SmmuV3Poll (
  IN  UINT64 SmmuReg,
  IN  UINT32 Mask,
  IN  UINT32 Value
  )
{
  UINT32 RegVal;
  UINTN  Count;

  // Set 1ms timeout value.
  Count = 10;
  do {
    RegVal = MmioRead32 (SmmuReg);
    DEBUG ((
      DEBUG_INFO,
      "SmmuV3Poll: Read SMMUv3 register 0x%llx = 0x%lx\n",
      SmmuReg,
      RegVal
    ));
    if ((RegVal & Mask) == Value) {
      DEBUG ((
        DEBUG_INFO,
        "SmmuV3Poll: Register read matched expected value 0x%x\n",
        ((Value == 0) ? (RegVal & ~Mask) : (RegVal | Mask))
      ));
      return EFI_SUCCESS;
    }
    MicroSecondDelay (100);
  } while ((--Count) > 0);

  DEBUG ((
    DEBUG_ERROR,
    "SmmuV3Poll: Timeout polling SMMUv3 register @%p Read value 0x%x expected 0x%x\n",
    SmmuReg,
    RegVal,
    ((Value == 0) ? (RegVal & ~Mask) : (RegVal | Mask))
  ));

  return EFI_TIMEOUT;
}

/**
  Initialise the SMMUv3 to set Non-secure streams to bypass the SMMU.

  @param [in]  SmmuReg    Base address of the SMMUv3.

  @retval EFI_SUCCESS     Success.
  @retval EFI_TIMEOUT     Timeout.
**/
STATIC
EFI_STATUS
EFIAPI
SmmuV3Init (
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

  // TF-A configures the SMMUv3 to abort all incoming transactions.
  // Clear the SMMU_GBPA.ABORT to allow Non-secure streams to bypass
  // the SMMU.
  RegVal &= ~SMMU_GBPA_ABORT;
  RegVal |= SMMU_GBPA_UPDATE;

  MmioWrite32 (SmmuBase + SMMU_GBPA, RegVal);

  Status = SmmuV3Poll (SmmuBase + SMMU_GBPA, SMMU_GBPA_UPDATE, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

/**
 * Entrypoint for SbsaQemuSmmuDxe driver
 * See UEFI specification for the details of the parameters
 */
EFI_STATUS
EFIAPI
InitializeSbsaQemuSmmuDxe (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS                   Status;
  EFI_ACPI_TABLE_PROTOCOL      *AcpiTable;
  UINT64                       SmmuBase;

  DEBUG ((DEBUG_INFO, "SbsaQemuSmmuDxe: called\n"));

  // Check if ACPI Table Protocol has been installed
  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SbsaQemuSmmuDxe: Failed to locate ACPI Table Protocol\n"));
    return Status;
  }

  // Add IORT Table
  if (PcdGet64 (PcdGicItsBase) > 0) {
    Status = AddIortTable (AcpiTable);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "SbsaQemuSmmuDxe: Failed to add IORT table\n"));
      return Status;
    }
    DEBUG ((DEBUG_INFO, "SbsaQemuSmmuDxe: Successfully added IORT table\n"));
  }

  // Get SMMUv3 base address from PCD
  SmmuBase = PcdGet64 (PcdSmmuBase);
  DEBUG ((DEBUG_INFO, "SbsaQemuSmmuDxe: PcdGet64(PcdSmmuBase)=0x%llx\n", SmmuBase));

  // Configure SMMUv3 to set transactions in bypass mode.
  Status = SmmuV3Init (SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "SbsaQemuSmmuDxe: Failed to initialise SMMUv3 in bypass mode.\n"
    ));
    return Status;
  }
  DEBUG ((
    DEBUG_INFO,
    "SbsaQemuSmmuDxe: Successfully initialised SMMUv3 in bypass mode.\n"
  ));

  return Status;
}
