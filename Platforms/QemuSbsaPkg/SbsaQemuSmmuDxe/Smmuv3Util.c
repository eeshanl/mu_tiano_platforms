/** @file Smmuv3Util.c

    This file contains util functions for the SMMU driver for the Qemu SBSA platform.

    Copyright (C) Microsoft Corporation. All rights reserved.
    SPDX-License-Identifier: BSD-2-Clause-Patent

    Qemu smmu worked on this sha - a53b931645183bd0c15dd19ae0708fc3c81ecf1d
    QEMU emulator version 9.1.50 (v9.1.0-475-ga53b931645)
**/

#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include "smmuv3.h"

UINT32
EFIAPI
SmmuV3DecodeAddressWidth (
    IN UINT32 AddressSizeType
    )
{
    UINT32 Length;
    switch (AddressSizeType) {
        case SmmuAddressSize32Bit:
            Length = 32;
            break;
        case SmmuAddressSize36Bit:
            Length = 36;
            break;
        case SmmuAddressSize40Bit:
            Length = 40;
            break;
        case SmmuAddressSize42Bit:
            Length = 42;
            break;
        case SmmuAddressSize44Bit:
            Length = 44;
            break;
        case SmmuAddressSize48Bit:
            Length = 48;
            break;
        case SmmuAddressSize52Bit:
            Length = 52;
            break;
        default:
            ASSERT(FALSE);
            Length = 0;
            break;
    }
    return Length;
}


UINT8
EFIAPI
SmmuV3EncodeAddressWidth (
    IN UINT32 AddressWidth
    )
{
    UINT8 Encoding;
    switch (AddressWidth) {
        case 32:
            Encoding = SmmuAddressSize32Bit;
            break;

        case 36:
            Encoding = SmmuAddressSize36Bit;
            break;

        case 40:
            Encoding = SmmuAddressSize40Bit;
            break;

        case 42:
            Encoding = SmmuAddressSize42Bit;
            break;

        case 44:
            Encoding = SmmuAddressSize44Bit;
            break;

        case 48:
            Encoding = SmmuAddressSize48Bit;
            break;

        case 52:
            Encoding = SmmuAddressSize52Bit;
            break;

        default:
            ASSERT(FALSE);
            Encoding = 0;
    }
    return Encoding;
}

UINT32
EFIAPI
SmmuV3ReadRegister32 (
    IN UINT64 SmmuBase,
    IN UINT64 Register
    )
{
    return MmioRead32(SmmuBase + Register);
}

UINT64
EFIAPI
SmmuV3ReadRegister64 (
    IN UINT64 SmmuBase,
    IN UINT64 Register
    )
{
    return MmioRead64(SmmuBase + Register);
}

UINT32
EFIAPI
SmmuV3WriteRegister32 (
    IN UINT64 SmmuBase,
    IN UINT64 Register,
    IN UINT32 Value
    )
{
    return MmioWrite32(SmmuBase + Register, Value);
}

UINT64
EFIAPI
SmmuV3WriteRegister64 (
    IN UINT64 SmmuBase,
    IN UINT64 Register,
    IN UINT64 Value
    )
{
    return MmioWrite64(SmmuBase + Register, Value);
}

EFI_STATUS
EFIAPI
SmmuV3DisableInterrupts (
    IN UINT64 SmmuBase,
    IN BOOLEAN ClearStaleErrors
    )
{
    EFI_STATUS Status;
    SMMUV3_IRQ_CTRL IrqControl;
    SMMUV3_GERROR GlobalErrors;

    IrqControl.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IRQ_CTRL);
    if ((IrqControl.AsUINT32 & SMMUV3_IRQ_CTRL_GLOBAL_PRIQ_EVTQ_EN_MASK) != 0) {
        IrqControl.AsUINT32 &= ~SMMUV3_IRQ_CTRL_GLOBAL_PRIQ_EVTQ_EN_MASK;
        SmmuV3WriteRegister32(SmmuBase, SMMU_IRQ_CTRL, IrqControl.AsUINT32);
        Status = SmmuV3Poll(SmmuBase + SMMU_IRQ_CTRLACK, SMMUV3_IRQ_CTRL_GLOBAL_PRIQ_EVTQ_EN_MASK, 0);
        if (Status != EFI_SUCCESS) {
            DEBUG((DEBUG_ERROR, "Error SmmuV3Poll: 0x%lx\n", SmmuBase + SMMU_IRQ_CTRLACK));
            return Status;
        }
    }

    if (ClearStaleErrors != FALSE) {
        GlobalErrors.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_GERROR);
        GlobalErrors.AsUINT32 = GlobalErrors.AsUINT32 & SMMUV3_GERROR_VALID_MASK;
        SmmuV3WriteRegister32(SmmuBase, SMMU_GERROR, GlobalErrors.AsUINT32);
    }
    return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SmmuV3EnableInterrupts (
    IN UINT64 SmmuBase
    )
{
    EFI_STATUS Status;
    SMMUV3_IRQ_CTRL IrqControl;
    IrqControl.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_IRQ_CTRL);
    IrqControl.AsUINT32 &= ~SMMUV3_IRQ_CTRL_GLOBAL_PRIQ_EVTQ_EN_MASK;
    IrqControl.GlobalErrorIrqEn = 1;
    IrqControl.EventqIrqEn = 1;
    SmmuV3WriteRegister32(SmmuBase, SMMU_IRQ_CTRL, IrqControl.AsUINT32);
    Status = SmmuV3Poll(SmmuBase + SMMU_IRQ_CTRLACK, 0x5, 0x5);
    if (Status != EFI_SUCCESS) {
        DEBUG((DEBUG_ERROR, "Error SmmuV3Poll: 0x%lx\n", SmmuBase + SMMU_IRQ_CTRLACK));
    }
    return Status;
}

EFI_STATUS
EFIAPI
SmmuV3Disable (
    IN UINT64 SmmuBase
    )
{
    SMMUV3_CR0 Cr0;
    EFI_STATUS Status;
    Cr0.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_CR0);
    if ((Cr0.AsUINT32 & SMMUV3_CR0_SMMU_CMDQ_EVTQ_PRIQ_EN_MASK) != 0) {
        Cr0.AsUINT32 = Cr0.AsUINT32 & ~SMMUV3_CR0_SMMU_CMDQ_EVTQ_PRIQ_EN_MASK;
        SmmuV3WriteRegister32(SmmuBase, SMMU_CR0, Cr0.AsUINT32);
        Status = SmmuV3Poll(SmmuBase + SMMU_CR0ACK, SMMUV3_CR0_SMMU_CMDQ_EVTQ_PRIQ_EN_MASK, 0);
        if (Status != EFI_SUCCESS) {
            DEBUG((DEBUG_ERROR, "Error SmmuV3Poll: 0x%lx\n", SmmuBase + SMMU_CR0ACK));
            return Status;
        }
    }
    return EFI_SUCCESS;
}

/**
  Poll the SMMU register and test the value based on the mask.

  @param [in]  SmmuReg    Base address of the SMMU register.
  @param [in]  Mask       Mask of register bits to monitor.
  @param [in]  Value      Expected value.

  @retval EFI_SUCCESS     Success.
  @retval EFI_TIMEOUT     Timeout.
**/
EFI_STATUS
EFIAPI
SmmuV3Poll(IN UINT64 SmmuReg, IN UINT32 Mask, IN UINT32 Value) {
    UINT32 RegVal;
    UINTN Count;

    // Set 1ms timeout value.
    Count = 10;
    do {
        RegVal = MmioRead32(SmmuReg);
        DEBUG((DEBUG_INFO, "SmmuV3Poll: Read SMMUv3 register 0x%llx = 0x%lx\n",
               SmmuReg, RegVal));
        if ((RegVal & Mask) == Value) {
            DEBUG((DEBUG_INFO,
                   "SmmuV3Poll: Register read matched expected value 0x%x\n",
                   ((Value == 0) ? (RegVal & ~Mask) : (RegVal | Mask))));
            return EFI_SUCCESS;
        }
        MicroSecondDelay(100);
    } while ((--Count) > 0);

    DEBUG((DEBUG_ERROR,
           "SmmuV3Poll: Timeout polling SMMUv3 register @%p Read value 0x%x "
           "expected 0x%x\n",
           SmmuReg, RegVal,
           ((Value == 0) ? (RegVal & ~Mask) : (RegVal | Mask))));

    return EFI_TIMEOUT;
}

EFI_STATUS
EFIAPI
SmmuV3ConsumeEventQueueForErrors (
    IN UINT64 SmmuBase,
    IN VOID* EventQ,
    IN UINTN Log2Size,
    OUT PSMMUV3_FAULT_RECORD FaultRecord
    )
{
    SMMUV3_EVENTQ_CONS Consumer;
    UINT32 ConsumerIndex;
    UINT32 ConsumerWrap;
    PSMMUV3_FAULT_RECORD NextFault;
    SMMUV3_EVENTQ_PROD Producer;
    UINT32 ProducerIndex;
    UINT32 ProducerWrap;
    BOOLEAN QueueEmpty;
    UINT32 QueueMask;
    UINT32 TotalQueueEntries;
    UINT32 WrapMask;

    TotalQueueEntries = SMMUV3_COUNT_FROM_LOG2(Log2Size);
    WrapMask = TotalQueueEntries;
    QueueMask = TotalQueueEntries - 1;

    Producer.AsUINT32 = SmmuV3ReadRegister32 (SmmuBase + 0x10000, SMMU_EVENTQ_PROD);
    Consumer.AsUINT32 = SmmuV3ReadRegister32 (SmmuBase + 0x10000, SMMU_EVENTQ_CONS);

    ProducerIndex = Producer.WriteIndex & QueueMask;
    ProducerWrap = Producer.WriteIndex & WrapMask;
    ConsumerIndex = Consumer.ReadIndex & QueueMask;
    ConsumerWrap = Consumer.ReadIndex & WrapMask;
    QueueEmpty = SMMUV3_IS_QUEUE_EMPTY(ProducerIndex,
                                       ProducerWrap,
                                       ConsumerIndex,
                                       ConsumerWrap);
    
    if (QueueEmpty != FALSE) {
        DEBUG((DEBUG_ERROR, "EventQ Empty\n"));
        goto End;
    }

    NextFault = EventQ + ConsumerIndex;
    CopyMem(FaultRecord, NextFault, SMMUV3_EVENT_QUEUE_ENTRY_SIZE);

    ConsumerIndex += 1;
    if (ConsumerIndex == TotalQueueEntries) {
        ConsumerIndex = 0;
        ConsumerWrap = ConsumerWrap ^ WrapMask;
    }

    Consumer.ReadIndex = ConsumerIndex | ConsumerWrap;

    ArmDataSynchronizationBarrier();

    SmmuV3WriteRegister32(SmmuBase + 0x10000, SMMU_EVENTQ_CONS, Consumer.AsUINT32);

End:
    return EFI_SUCCESS;
}


VOID
EFIAPI
SmmuV3PrintErrors (
    IN UINT64 SmmuBase
    )
{
    SMMUV3_GERROR GError;
    SMMUV3_FAULT_RECORD FaultRecord = {0};
    SmmuV3ConsumeEventQueueForErrors(SmmuBase, Smmu->EventQueue, Smmu->EventQueueLog2Size, &FaultRecord);
    DEBUG((DEBUG_INFO, "FaultRecord:\n"));
    for (UINTN i = 0; i < sizeof(FaultRecord.Fault) / sizeof(FaultRecord.Fault[0]); i++) {
        DEBUG((DEBUG_INFO, "0x%llx\n", FaultRecord.Fault[i]));
    }

    GError.AsUINT32 = SmmuV3ReadRegister32(SmmuBase, SMMU_GERROR);
    DEBUG((DEBUG_INFO, "GError: 0x%lx\n", GError.AsUINT32));
}

STATIC
VOID
EFIAPI
SmmuV3WriteCommands (
    IN SMMU_INFO* Smmu,
    IN UINT64 StartingIndex,
    IN UINT32 CommandCount,
    IN PSMMUV3_CMD_GENERIC Commands
    )
{
    UINT32 Index;
    UINT32 ProducerIndex;
    UINT32 QueueMask;
    UINT32 WrapMask;
    PSMMUV3_CMD_GENERIC CommandQueue;

    WrapMask = (1UL << Smmu->CommandQueueLog2Size);
    QueueMask = WrapMask - 1;
    CommandQueue = (PSMMUV3_CMD_GENERIC) Smmu->CommandQueue;
    for (Index = 0; Index < CommandCount; Index += 1) {
        ProducerIndex = (UINT32)((StartingIndex + Index) & QueueMask);
        CommandQueue[ProducerIndex] = Commands[Index];
    }

}


EFI_STATUS
EFIAPI
SmmuV3SendCommand (
    IN SMMU_INFO* Smmu,
    IN PSMMUV3_CMD_GENERIC Command
    )
{
    UINT32 QueueMask;
    UINT32 WrapMask;
    UINT32 TotalQueueEntries;
    UINT32 NewProducerIndex;
    UINT32 ProducerIndex;
    UINT32 ConsumerIndex;
    UINT32 ProducerWrap;
    UINT32 ConsumerWrap;
    SMMUV3_CMDQ_PROD Producer;
    SMMUV3_CMDQ_CONS Consumer;
    UINT8 Count;

    // Set 1ms timeout value.
    Count = 10;

    Producer.AsUINT32 = SmmuV3ReadRegister32(Smmu->SmmuBase, SMMU_CMDQ_PROD);
    Consumer.AsUINT32 = SmmuV3ReadRegister32(Smmu->SmmuBase, SMMU_CMDQ_CONS);


    TotalQueueEntries = SMMUV3_COUNT_FROM_LOG2(Smmu->CommandQueueLog2Size);
    WrapMask = TotalQueueEntries;
    QueueMask = WrapMask - 1;
    ProducerWrap = Producer.WriteIndex & WrapMask;
    ConsumerWrap = Consumer.ReadIndex & WrapMask;

    ProducerIndex = Producer.WriteIndex & QueueMask;
    ConsumerIndex = Consumer.ReadIndex & QueueMask;

    while (Count > 0 && SMMUV3_IS_QUEUE_FULL(ProducerIndex,
                                             ProducerWrap,
                                             ConsumerIndex,
                                             ConsumerWrap) != FALSE)
    {
        DEBUG((DEBUG_ERROR, "Command Queue Full\n"));
        MicroSecondDelay(100);

        Producer.AsUINT32 = SmmuV3ReadRegister32(Smmu->SmmuBase, SMMU_CMDQ_PROD);
        Consumer.AsUINT32 = SmmuV3ReadRegister32(Smmu->SmmuBase, SMMU_CMDQ_CONS);

        ProducerWrap = Producer.WriteIndex & WrapMask;
        ConsumerWrap = Consumer.ReadIndex & WrapMask;

        ProducerIndex = Producer.WriteIndex & QueueMask;
        ConsumerIndex = Consumer.ReadIndex & QueueMask;

        Count--;
    }

    if (Count == 0) {
        DEBUG((DEBUG_ERROR, "Command Queue Full, Timeout\n"));
        return EFI_TIMEOUT;
    }
    DEBUG((DEBUG_INFO, "Current ProducerIndex = %d TotalQueueEntries = %d\n", ProducerIndex, TotalQueueEntries));
    SmmuV3WriteCommands(Smmu, ProducerIndex, 1, Command);

    ArmDataSynchronizationBarrier();

    NewProducerIndex = ProducerIndex + 1;

    Producer.AsUINT32 = 0;
    Producer.WriteIndex = NewProducerIndex & (QueueMask | WrapMask);

    SmmuV3WriteRegister32(Smmu->SmmuBase, SMMU_CMDQ_PROD, Producer.AsUINT32);

    Consumer.AsUINT32 = SmmuV3ReadRegister32(Smmu->SmmuBase, SMMU_CMDQ_CONS);
    Count = 10;

    // Wait for the command to be consumed
    while (Count > 0 && Consumer.ReadIndex < Producer.WriteIndex) {
        MicroSecondDelay(100);
        Consumer.AsUINT32 = SmmuV3ReadRegister32(Smmu->SmmuBase, SMMU_CMDQ_CONS);  
        Count--;
    }

    if (Count == 0) {
        DEBUG((DEBUG_ERROR, "Timeout waiting for command queue to be consumed\n"));
        return EFI_TIMEOUT;
    }

    return EFI_SUCCESS;
}
