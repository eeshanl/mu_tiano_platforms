/** @file

  Copyright (c) 2011-2014, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>

#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Guid/SmmuConfig.h>


EFI_STATUS
EFIAPI
PlatformPeim (
  VOID
  )
{
  SMMU_CONFIG SmmuConfig;
  SmmuConfig.x = 0x7;

  BuildFvHob (PcdGet64 (PcdFvBaseAddress), PcdGet32 (PcdFvSize));

  BuildGuidDataHob (&gEfiSmmuConfigGuid, &SmmuConfig, sizeof (SMMU_CONFIG));

  DEBUG ((DEBUG_INFO, "MY PEI LIB\n"));

  return EFI_SUCCESS;
}
