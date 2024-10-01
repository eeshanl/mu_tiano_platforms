/** @file smmuv3.c

    This file is the smmuv3 header file for SMMU driver for the Qemu SBSA platform.

    Copyright (C) Microsoft Corporation. All rights reserved.
    SPDX-License-Identifier: BSD-2-Clause-Patent

    Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
    QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
**/

#ifndef SMMUV3_H
#define SMMUV3_H

// SMMUv3 Global Bypass Attribute (GBPA) register offset.
#define SMMU_GBPA                         0x0044

// SMMU_GBPA register fields.
#define SMMU_GBPA_UPDATE                  BIT31
#define SMMU_GBPA_ABORT                   BIT20

#endif
