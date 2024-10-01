/** @file
*
*  Copyright (c) 2024, Linaro Ltd. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#ifndef HARDWARE_INFO_LIB
#define HARDWARE_INFO_LIB

typedef struct {
  UINT32    NodeId;
  UINT64    AddressBase;
  UINT64    AddressSize;
} MemoryInfo;

/**
  Sockets: the number of sockets on sbsa-ref platform.
  Clusters: the number of clusters in one socket.
  Cores: the number of cores in one cluster.
  Threads: the number of threads in one core.
**/
typedef struct {
  UINT32    Sockets;
  UINT32    Clusters;
  UINT32    Cores;
  UINT32    Threads;
} CpuTopology;

typedef struct {
  UINTN    DistributorBase;
  UINTN    RedistributorBase;
  UINTN    ItsBase;
} GicInfo;

typedef struct {
  UINT32    Major;
  UINT32    Minor;
} PlatformVersion;

/**
  Get CPU count from information passed by Qemu.

**/
UINT32
GetCpuCount (
  VOID
  );

/**
  Get MPIDR for a given cpu from device tree passed by Qemu.

  @param [in]   CpuId    Index of cpu to retrieve MPIDR value for.

  @retval                MPIDR value of CPU at index <CpuId>

**/
UINT64
GetMpidr (
  IN UINTN  CpuId
  );

#endif /* HARDWARE_INFO_LIB */
