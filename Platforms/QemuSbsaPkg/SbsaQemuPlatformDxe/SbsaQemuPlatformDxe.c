/** @file
*  FDT client protocol driver for qemu,mach-virt-ahci DT node
*
*  Copyright (c) 2019, Linaro Ltd. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HardwareInfoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

/* Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
Copyright (c) 2003-2024 Fabrice Bellard and the QEMU Project developers
*/

EFI_STATUS
EFIAPI
InitializeSbsaQemuPlatformDxe (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS       Status;
  UINTN            Size;
  VOID             *Base;
  GicInfo          GicInfo;
  PlatformVersion  PlatVer;

  DEBUG ((DEBUG_INFO, "%a: InitializeSbsaQemuPlatformDxe called\n", __FUNCTION__));

  Base = (VOID *)(UINTN)PcdGet64 (PcdPlatformAhciBase);
  ASSERT (Base != NULL);
  Size = (UINTN)PcdGet32 (PcdPlatformAhciSize);
  ASSERT (Size != 0);

  DEBUG ((
    DEBUG_INFO,
    "%a: Got platform AHCI %llx %u\n",
    __FUNCTION__,
    Base,
    Size
    ));

  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeAhci,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,
             NULL,
             1,
             Base,
             Size
             );

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: NonDiscoverable: Cannot install AHCI device @%p (Status == %r)\n",
      __FUNCTION__,
      Base,
      Status
      ));
    return Status;
  }

  GetPlatformVersion (&PlatVer);
  DEBUG ((DEBUG_INFO, "Platform version: %d.%d\n", PlatVer.Major, PlatVer.Minor));

  PcdSet32S (PcdPlatformVersionMajor, PlatVer.Major);
  PcdSet32S (PcdPlatformVersionMinor, PlatVer.Minor);

  GetGicInformation (&GicInfo);
  DEBUG ((DEBUG_INFO, "GicInfo : 0x%llx,  0x%llx, 0x%llx\n", GicInfo.DistributorBase, GicInfo.RedistributorBase, GicInfo.ItsBase));

  PcdSet64S (PcdGicDistributorBase, GicInfo.DistributorBase);
  PcdSet64S (PcdGicRedistributorsBase, GicInfo.RedistributorBase);
  PcdSet64S (PcdGicItsBase, GicInfo.ItsBase);
  DEBUG ((DEBUG_INFO, "PcdGet64(PcdGicItsBase)=0x%llx\n", PcdGet64(PcdGicItsBase)));
  DEBUG ((DEBUG_INFO, "PcdGet64(PcdGicDistributorBase)=0x%llx\n", PcdGet64(PcdGicDistributorBase)));
  DEBUG ((DEBUG_INFO, "PcdGet64(PcdGicRedistributorsBase)=0x%llx\n", PcdGet64(PcdGicRedistributorsBase)));

  return EFI_SUCCESS;
}
