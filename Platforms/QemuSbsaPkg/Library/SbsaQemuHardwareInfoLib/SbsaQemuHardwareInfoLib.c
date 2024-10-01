/** @file
*
*  Copyright (c) 2021, NUVIA Inc. All rights reserved.
*  Copyright (c) 2024, Linaro Ltd. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <Library/ArmMonitorLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/HardwareInfoLib.h>
#include <IndustryStandard/SbsaQemuSmc.h>
#include <Library/AcpiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

/**
  Get CPU count from information passed by TF-A.

**/
UINT32
GetCpuCount (
  VOID
  )
{
  ARM_MONITOR_ARGS  SmcArgs;

  SmcArgs.Arg0 = SIP_SVC_GET_CPU_COUNT;
  ArmMonitorCall (&SmcArgs);

  if (SmcArgs.Arg0 != SMC_SIP_CALL_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "%a: SIP_SVC_GET_CPU_COUNT call failed. We have no cpu information.\n", __func__));
    ResetShutdown ();
  }

  DEBUG ((DEBUG_INFO, "%a: We have %d cpus.\n", __func__, SmcArgs.Arg1));

  return SmcArgs.Arg1;
}

/**
  Get MPIDR for a given cpu from TF-A.

  @param [in]   CpuId    Index of cpu to retrieve MPIDR value for.

  @retval                MPIDR value of CPU at index <CpuId>
**/
UINT64
GetMpidr (
  IN UINTN  CpuId
  )
{
  ARM_MONITOR_ARGS  SmcArgs;

  SmcArgs.Arg0 = SIP_SVC_GET_CPU_NODE;
  SmcArgs.Arg1 = CpuId;
  ArmMonitorCall (&SmcArgs);

  if (SmcArgs.Arg0 != SMC_SIP_CALL_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "%a: SIP_SVC_GET_CPU_NODE call failed. We have no MPIDR for CPU%d.\n", __func__, CpuId));
    ResetShutdown ();
  }

  DEBUG ((DEBUG_INFO, "%a: MPIDR for CPU%d: = %d\n", __func__, CpuId, SmcArgs.Arg2));

  return SmcArgs.Arg2;
}
