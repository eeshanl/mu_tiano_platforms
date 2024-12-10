# Sbsa SMMU/IOMMU Driver

This document describes the System Memory Management Unit (SMMU) driver implementation for the SBSA platform, and how it integrates with the PCI I/O subsystem. The driver configures the SMMUv3 hardware and implements the IOMMU protocol to provide address translation and memory protection for DMA operations.

## Architecture Overview

The system consists of three main components working together:

1. **PCI I/O Protocol**: Provides interface for PCI device access and DMA operations
2. **IOMMU Protocol**: Implements DMA remapping and memory protection
3. **SMMU Hardware Driver**: Configures and manages the SMMU hardware

### Component Integration Flow

```
PCI Device Driver
      ↓
PCI I/O Protocol
      ↓
IOMMU Protocol
      ↓
SMMU Hardware  
```

## IOMMU Protocol Integration

1. **PCI Driver Initiates DMA**:
   - PCI device driver calls PciIo->Map(), PciIo->Unmap()
   - Provides host memory address and operation type

2. **IOMMU Protocol Setup**:
   - Implements the IOMMU protocol:
      ```c
      struct _EDKII_IOMMU_PROTOCOL {
        UINT64                         Revision;
        EDKII_IOMMU_SET_ATTRIBUTE      SetAttribute;
        EDKII_IOMMU_MAP                Map;
        EDKII_IOMMU_UNMAP              Unmap;
        EDKII_IOMMU_ALLOCATE_BUFFER    AllocateBuffer;
        EDKII_IOMMU_FREE_BUFFER        FreeBuffer;
      };
      ```
   - Configures IOMMU page tables with PageTableInit()
### DMA Mapping

1. **IoMmu Map**:
    ```c
    EFI_STATUS
    EFIAPI
    IoMmuMap (
      IN     EDKII_IOMMU_PROTOCOL   *This,
      IN     EDKII_IOMMU_OPERATION  Operation,
      IN     VOID                   *HostAddress,
      IN OUT UINTN                  *NumberOfBytes,
      OUT    EFI_PHYSICAL_ADDRESS   *DeviceAddress,
      OUT    VOID                   **Mapping
      );
    ```
    - Sets access permissions based on operation type:
      - BusMasterRead: READ access
      - BusMasterWrite: WRITE access
      - BusMasterCommonBuffer: READ/WRITE access
   - Maps HostAddress to DeviceAddress
   - Validates operation type
   - Called by PciIo protocol for mapping


### DMA Unmapping

1. **PCI Driver Completes DMA**:
   - Calls PciIo->Unmap()
   - Provides mapping handle

2. **IoMmu Unmap**:
    ```c
    EFI_STATUS
    EFIAPI
    IoMmuUnmap (
      IN  EDKII_IOMMU_PROTOCOL  *This,
      IN  VOID                  *Mapping
      );
    ```
   - Invalidates mapping in Page Table
   - Invalidates TLB entries


## SMMU Configuration

### 1. SMMUv3 Hardware Setup

The SMMU is configured in stage 2 translation mode with:
- Stream table for device ID mapping
- Command queue for SMMU operations, like TLB management
- Event queue for error handling
- 4KB translation granule

### 2. Page Table Structure

The IOMMU uses a 4-level page table structure for DMA address translation:
https://developer.arm.com/documentation/101811/0104/Translation-granule/The-starting-level-of-address-translation

```
Level 0 Table (L0)
    ↓
Level 1 Table (L1)
    ↓
Level 2 Table (L2)
    ↓
Level 3 Table (L3)
    ↓
Physical Page
```

### 3. Address Translation Process

1. **Device Issues DMA**:
   - Device uses IOVA (I/O Virtual Address)
   - SMMU intercepts access

2. **SMMU Translation**:
   - Looks up Stream Table Entry (STE)
   - Walks 4-level page tables
   - Converts IOVA to PA (Physical Address)

## Memory Protection

The IOMMU protocol provides several protection mechanisms:

1. **Access Control**:
   - Read/Write permissions per mapping
   - Device isolation through Stream IDs

2. **Address Range Protection**:
   - Validates DMA addresses
   - Prevents access outside mapped regions

3. **Error Handling**:
   - Translation faults logged to Event Queue

## Performance Features

The implementation includes optimizations for:

1. **Integration of SmmuV3 with IOMMU Protocol**

2. **TLB Management**:
   - TLB invalidation for unmapped entries

## Configuration Options

Key IOMMU settings controlled through the SMMU:

```
- Translation Stages: Stage 2 only
- Page Size: 4KB fixed
- Stream Table Size: Based on device IDs
- Queue Sizes: Configurable power-of-2
```

## Limitations

Current implementation constraints:

1. Fixed 4KB granule size
2. 48-bit address space limit
3. Stage 2 translation only
4. Identity mapped page tables


## Future Enhancements

Potential improvements:

1. Multiple translation granule support
2. Stage 1 translation
3. Different page table mapping schemes
4. Updated IoMmu Protocol to optimize redundencies

## Relevant Docs

- SMMUv3 specification https://developer.arm.com/documentation/ihi0070/latest/
- ARM a_a-profile_architecture_reference_manual https://developer.arm.com/documentation/102105/ka-07
- Intel IOMMU for DMA protection in UEFI https://www.intel.com/content/dam/develop/external/us/en/documents/intel-whitepaper-using-iommu-for-dma-protection-in-uefi.pdf
- IORT documentation https://developer.arm.com/documentation/den0049/latest/

## Notes

Qemu Version:

- Qemu Smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
- QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)

TFA Version:

- chery-pick [feat(qemu-sbsa): handle CPU information · ARM-software/arm-trusted-firmware@42925c1 (github.com)](https://github.com/ARM-software/arm-trusted-firmware/commit/42925c15bee09162c6dfc8c2204843ffac6201c1#diff-efe8a973d827b75aa34a8b6fd065bc9a7ffd33290d4a73697797e24e56460ae2R76) in Silicon/Arm/TFA - 42925c15bee09162c6dfc8c2204843ffac6201c1
