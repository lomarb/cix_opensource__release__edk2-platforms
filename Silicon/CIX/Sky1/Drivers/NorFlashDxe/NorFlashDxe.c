/** @file  NorFlashDxe.c

  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved
  Copyright (c) 2022, CIX, Ltd. All rights reserved.
  Copyright (c) 2011 - 2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "NorFlashDxe.h"

STATIC EFI_EVENT  mNorFlashVirtualAddrChangeEvent;

//
// Global variable declarations
//
NOR_FLASH_INSTANCE  **mNorFlashInstances;
UINT32              mNorFlashDeviceCount;
UINTN               mFlashNvStorageVariableBase;
EFI_EVENT           mFvbVirtualAddrChangeEvent;

NOR_FLASH_INSTANCE  mNorFlashInstanceTemplate = {
  NOR_FLASH_SIGNATURE, // Signature
  NULL,                // Handle

  XSPI_FLASH_DIRECT_MMIO_ADDR, // DeviceBaseAddress
  SIZE_16MB,                   // Size
  NULL,                        // ShadowBuffer
  {
    0,           // MediaId
    FALSE,       // RemovableMedia
    TRUE,        // MediaPresent
    FALSE,       // LogicalPartition
    FALSE,       // ReadOnly
    FALSE,       // WriteCaching;
    SIZE_4KB,    // BlockSize
    4,           // IoAlign
    0xFFF,       // LastBlock  // 16MB flash size
    0,           // LowestAlignedLba
    1,           // LogicalBlocksPerPhysicalBlock
  }, // Media
  {
    EFI_BLOCK_IO_PROTOCOL_REVISION2, // Revision
    NULL,                            // Media ... NEED TO BE FILLED
    NorFlashBlockIoReset,            // Reset;
    NorFlashBlockIoReadBlocks,       // ReadBlocks
    NorFlashBlockIoWriteBlocks,      // WriteBlocks
    NorFlashBlockIoFlushBlocks       // FlushBlocks
  }, // BlockIoProtocol
  {
    EFI_DISK_IO_PROTOCOL_REVISION, // Revision
    NorFlashDiskIoReadDisk,        // ReadDisk
    NorFlashDiskIoWriteDisk        // WriteDisk
  }, // DiskIoProtocol
  {
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_VENDOR_DP,
        {
          (UINT8)(OFFSET_OF (NOR_FLASH_DEVICE_PATH, End)),
          (UINT8)(OFFSET_OF (NOR_FLASH_DEVICE_PATH, End) >> 8)
        }
      },
      {
        0  // GUID
      },
    },
    0, // Index
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
    }
  }, // DevicePath
  {
    XSPI_FLASH_DIRECT_MMIO_ADDR,
    XSPI_FLASH_DMA_MMIO_ADDR,
    SIZE_64MB,
    0
  }, // AccessInfo

  XSPI_FLASH_DIRECT_MMIO_ADDR, // RegionBaseAddress
  0,                           // Blocks
  0,                           // StartLba
  {
    FvbGetAttributes,      // GetAttributes
    FvbSetAttributes,      // SetAttributes
    FvbGetPhysicalAddress, // GetPhysicalAddress
    FvbGetBlockSize,       // GetBlockSize
    FvbRead,               // Read
    FvbWrite,              // Write
    FvbEraseBlocks,        // EraseBlocks
    NULL,                  // ParentHandle
  } // FvbProtoccol
};

STATIC NOR_FLASH_REGION_DESCRIPTION  mNorFlashDevices[] = {
  {
    XSPI_FLASH_DIRECT_MMIO_ADDR,  // Memory mapped IO base address for nor flash
    SIZE_16MB,
    FixedPcdGet32 (PcdNorFlashNvramAddr),
    FixedPcdGet32 (PcdNorFlashNvramSize),
    SIZE_4KB,
  },
};

VOID
EFIAPI
FlashMemoryInitEventNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                    Status;
  NOR_FLASH_REGION_DESCRIPTION  *NorFlashDevices;
  UINTN                         RuntimeMmioRegionBase;
  UINTN                         RuntimeMmioRegionSize;

  NorFlashDevices = (NOR_FLASH_REGION_DESCRIPTION *)Context;
  //
  // Declare the Non-Volatile storage as EFI_MEMORY_RUNTIME
  //

  // Note: all the NOR Flash region needs to be reserved into the UEFI Runtime memory;
  //       even if we only use the small block region of the NOR Flash.
  //       The reason is when the NOR Flash memory is set into program mode, the command
  //       is written as the base of the flash region (ie: Instance->DeviceBaseAddress)
  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  NorFlashDevices->DeviceBaseAddress,
                  NorFlashDevices->DeviceSize,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DebugPrint (
      DEBUG_ERROR,
      "%a: fail to add memory space base 0x%x, size 0x%x, status %r\n",
      __FUNCTION__,
      NorFlashDevices->DeviceBaseAddress,
      NorFlashDevices->DeviceSize,
      Status
      );
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG ((DEBUG_INFO, "%a: add flash space base 0x%x, length 0x%x to memory space\n", __FUNCTION__, NorFlashDevices->DeviceBaseAddress, NorFlashDevices->DeviceSize));

  Status = gDS->SetMemorySpaceAttributes (
                  NorFlashDevices->DeviceBaseAddress,
                  NorFlashDevices->DeviceSize,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DebugPrint (
      DEBUG_ERROR,
      "%a: fail to set memory attributes base 0x%x, size 0x%x, status %r\n",
      __FUNCTION__,
      NorFlashDevices->DeviceBaseAddress,
      NorFlashDevices->DeviceSize,
      Status
      );
    gDS->RemoveMemorySpace (
           NorFlashDevices->DeviceBaseAddress,
           NorFlashDevices->DeviceSize
           );
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG ((DEBUG_INFO, "%a: set flash memory space attribute 0x%llx\n", __FUNCTION__, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME));

  // to cover XSPI host MMIO space
  RuntimeMmioRegionBase = XSPI_BASE;
  RuntimeMmioRegionSize = XSPI_SIZE;

  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  RuntimeMmioRegionBase,
                  RuntimeMmioRegionSize,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "%a: fail to add memory space base 0x%x, size 0x%x, status %r\n", __FUNCTION__, RuntimeMmioRegionBase, RuntimeMmioRegionSize, Status);
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG ((DEBUG_INFO, "%a: add memory space base 0x%x, length 0x%x\n", __FUNCTION__, RuntimeMmioRegionBase, RuntimeMmioRegionSize));

  Status = gDS->SetMemorySpaceAttributes (
                  RuntimeMmioRegionBase,
                  RuntimeMmioRegionSize,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "%a: fail to set memory attributes base 0x%x, size 0x%x, status %r\n", __FUNCTION__, RuntimeMmioRegionBase, RuntimeMmioRegionSize, Status);
    gDS->RemoveMemorySpace (
           RuntimeMmioRegionBase,
           RuntimeMmioRegionSize
           );
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG ((DEBUG_INFO, "%a: set memory space attribute 0x%llx\n", __FUNCTION__, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME));
}

EFI_STATUS
EFIAPI
NorFlashFvbInitialize (
  IN NOR_FLASH_INSTANCE  *Instance
  )
{
  EFI_STATUS     Status;
  EFI_BOOT_MODE  BootMode;

  DEBUG ((DEBUG_INFO, "%a: enter\n", __FUNCTION__));
  ASSERT ((Instance != NULL));

  mFlashNvStorageVariableBase = (PcdGet64 (PcdFlashNvStorageVariableBase64) != 0) ?
                                PcdGet64 (PcdFlashNvStorageVariableBase64) : PcdGet32 (PcdFlashNvStorageVariableBase);

  // Set the region base address of the FVB
  Instance->RegionBaseAddress = mFlashNvStorageVariableBase;
  // Set the index of the first LBA for the FVB
  Instance->StartLba = (mFlashNvStorageVariableBase - Instance->DeviceBaseAddress) / Instance->Media.BlockSize;
  // Set the blocks number of the FVB
  Instance->Blocks = (PcdGet32 (PcdFlashNvStorageVariableSize) + PcdGet32 (PcdFlashNvStorageFtwWorkingSize) + PcdGet32 (PcdFlashNvStorageFtwSpareSize)) / \
                     Instance->Media.BlockSize;

  BootMode = GetBootModeHob ();
  if (BootMode == BOOT_WITH_DEFAULT_SETTINGS) {
    Status = EFI_INVALID_PARAMETER;
  } else {
    // Determine if there is a valid header at the beginning of the NorFlash
    Status = ValidateFvHeader (Instance);
  }

  // Install the Default FVB header if required
  if (EFI_ERROR (Status)) {
    // There is no valid header, so time to install one.
    DEBUG ((DEBUG_INFO, "%a: The FVB Header is not valid\n", __FUNCTION__));
    DEBUG ((DEBUG_INFO, "%a: Installing a correct one for this volume\n", __FUNCTION__));

    // Erase all the NorFlash that is reserved for variable storage
    Status = FvbEraseBlocks (&Instance->FvbProtocol, (EFI_LBA)0, Instance->Blocks, EFI_LBA_LIST_TERMINATOR);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Install all appropriate headers
    Status = InitializeFvAndVariableStoreHeaders (Instance);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  //
  // The driver implementing the variable read service can now be dispatched;
  // the varstore headers are in place.
  //
  Status = gBS->InstallProtocolInterface (
                  &gImageHandle,
                  &gEdkiiNvVarStoreFormattedGuid,
                  EFI_NATIVE_INTERFACE,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Register for the virtual address change event
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  FvbVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mFvbVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "%a: exit\n", __FUNCTION__));

  return Status;
}

EFI_STATUS
NorFlashCreateInstance (
  IN UINTN                NorFlashDeviceBase,
  IN UINTN                NorFlashRegionBase,
  IN UINTN                NorFlashSize,
  IN UINT32               Index,
  IN UINT32               BlockSize,
  IN BOOLEAN              SupportFvb,
  OUT NOR_FLASH_INSTANCE  **NorFlashInstance
  )
{
  EFI_STATUS          Status;
  NOR_FLASH_INSTANCE  *Instance;

  if (NorFlashInstance == NULL) {
    DebugPrint (DEBUG_ERROR, "%a: invalid NOR_FLASH_INSTANCE buffer, status %r\n", __FUNCTION__, EFI_INVALID_PARAMETER);
    return EFI_INVALID_PARAMETER;
  }

  Instance = AllocateRuntimeCopyPool (sizeof (NOR_FLASH_INSTANCE), &mNorFlashInstanceTemplate);
  if (Instance == NULL) {
    DebugPrint (DEBUG_ERROR, "%a: invalid NOR_FLASH_INSTANCE buffer, status %r\n", __FUNCTION__, EFI_OUT_OF_RESOURCES);
    return EFI_OUT_OF_RESOURCES;
  }

  Instance->DeviceBaseAddress = NorFlashDeviceBase;
  Instance->Size              = NorFlashSize;

  Instance->BlockIoProtocol.Media = &Instance->Media;
  Instance->Media.MediaId         = Index;
  Instance->Media.BlockSize       = BlockSize;
  Instance->Media.LastBlock       = (NorFlashSize / BlockSize) - 1;

  CopyGuid (&Instance->DevicePath.Vendor.Guid, &gCixNorFlashDevicePathGuid);
  Instance->DevicePath.Index = (UINT8)Index;

  Instance->AccessInfo.BaseAddress    = XSPI_FLASH_DIRECT_MMIO_ADDR;
  Instance->AccessInfo.DmaBaseAddress = XSPI_FLASH_DMA_MMIO_ADDR;
  Instance->AccessInfo.Size           = SIZE_64MB;
  Instance->AccessInfo.RemappedOffset = 0;

  Instance->ShadowBuffer = AllocateRuntimeZeroPool (BlockSize);
  if (Instance->ShadowBuffer == NULL) {
    DebugPrint (DEBUG_ERROR, "%a: invalid shadow buffer, status %r\n", __FUNCTION__, EFI_OUT_OF_RESOURCES);
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_INFO, "%a: block size 0x%x, region index %d\n", __FUNCTION__, BlockSize, Index));

  DEBUG ((DEBUG_INFO, "%a: fvb protocol %S\n", __FUNCTION__, (SupportFvb == TRUE) ? L"supported" : L"not supported"));
  if (SupportFvb) {
    NorFlashFvbInitialize (Instance);

    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Instance->Handle,
                    &gEfiDevicePathProtocolGuid,
                    &Instance->DevicePath,
                    &gCixFlashBlockIoProtocolGuid,
                    &Instance->BlockIoProtocol,
                    &gEfiDiskIoProtocolGuid,
                    &Instance->DiskIoProtocol,
                    &gEfiFirmwareVolumeBlockProtocolGuid,
                    &Instance->FvbProtocol,
                    &gCixXspiInfoProtocolGuid,
                    &Instance->AccessInfo,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      DebugPrint (DEBUG_ERROR, "%a: fail to install protocols for nor flash %d, status %r\n", __FUNCTION__, Index, Status);
      FreePool (Instance);
      return Status;
    }
  } else {
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Instance->Handle,
                    &gEfiDevicePathProtocolGuid,
                    &Instance->DevicePath,
                    &gCixFlashBlockIoProtocolGuid,
                    &Instance->BlockIoProtocol,
                    &gEfiDiskIoProtocolGuid,
                    &Instance->DiskIoProtocol,
                    &gCixXspiInfoProtocolGuid,
                    &Instance->AccessInfo,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      DebugPrint (DEBUG_ERROR, "%a: fail to install protocols for nor flash %d, status %r\n", __FUNCTION__, Index, Status);
      FreePool (Instance);
      return Status;
    }
  }

  *NorFlashInstance = Instance;

  return Status;
}

/**
  Fixup internal data so that EFI can be call in virtual mode.
  Call the passed in Child Notify event and convert any pointers in
  lib to virtual mode.

  @param[in]    Event   The Event that is being processed
  @param[in]    Context Event Context
**/
VOID
EFIAPI
NorFlashVirtualNotifyEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN  Index;
  UINTN  HostAddr, SdmaAddr;

  HostAddr = GetNorFlashHostBaseAddr ();
  SdmaAddr = GetNorFlashSdmaBaseAddr ();

  EfiConvertPointer (0x0, (VOID **)&HostAddr);
  EfiConvertPointer (0x0, (VOID **)&SdmaAddr);

  UpdateNorFlashHostBaseAddr (HostAddr);
  UpdateNorFlashSdmaBaseAddr (SdmaAddr);

  for (Index = 0; Index < mNorFlashDeviceCount; Index++) {
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->DeviceBaseAddress);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->RegionBaseAddress);
    // Convert BlockIo protocol
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->BlockIoProtocol.FlushBlocks);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->BlockIoProtocol.ReadBlocks);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->BlockIoProtocol.Reset);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->BlockIoProtocol.WriteBlocks);
    // Convert Fvb
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.EraseBlocks);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.GetAttributes);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.GetBlockSize);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.GetPhysicalAddress);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.Read);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.SetAttributes);
    EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->FvbProtocol.Write);
    if (mNorFlashInstances[Index]->ShadowBuffer != NULL) {
      EfiConvertPointer (0x0, (VOID **)&mNorFlashInstances[Index]->ShadowBuffer);
    }
  }

  return;
}

STATIC
EFI_STATUS
XspiHwInit (
  )
{
  EFI_STATUS          Status;
  CIX_CLOCK_PROTOCOL  *Clock;
  CIX_RESET_PROTOCOL  *Reset;

  DEBUG ((DEBUG_INFO, "%a %d  \n", __FUNCTION__, __LINE__));
  Status = gBS->LocateProtocol (
                  &gCixClockProtocolGuid,
                  NULL,
                  (VOID **)&Clock
                  );
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "%a: locate gClockProtocolGuid failed, status %r\n", __FUNCTION__, Status);
  } else {
    Clock->ClockEnable (CLK_TREE_FCH_XSPI_FUNC);
    Clock->ClockEnable (CLK_TREE_FCH_XSPI_MACLK);
    Clock->ClockEnable (CLK_TREE_FCH_XSPI_APB);
  }

  Status = gBS->LocateProtocol (
                  &gCixResetProtocolGuid,
                  NULL,
                  (VOID **)&Reset
                  );
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "%a: locate gCixResetProtocolGuid failed, status %r\n", __FUNCTION__, Status);
  } else {
    Reset->ResetDeassert (FCH_XSPI_SYS_RST_N);
    Reset->ResetDeassert (FCH_XSPI_REG_RST_N);
  }

  return Status;
}

EFI_STATUS
EFIAPI
NorFlashInitialise (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                    Status;
  UINT32                        Index;
  NOR_FLASH_REGION_DESCRIPTION  *NorFlashDevices;
  BOOLEAN                       ContainVariableStorage;
  EFI_EVENT                     MemoryInitEvent;

  DEBUG ((DEBUG_INFO, "%a: enter\n", __FUNCTION__));

  POST_CODE (XspiInitDxeStart);

  Status = XspiHwInit ();
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "%a: fail to do XspiHwInit, status %r\n", __FUNCTION__, Status);
    return Status;
  }

  Status = NorFlashPlatformInitialization ();
  if (EFI_ERROR (Status)) {
    DebugPrint (DEBUG_ERROR, "%a: fail to initialize nor flash devices, status %r\n", __FUNCTION__, Status);
    return Status;
  }

  NorFlashDevices      = mNorFlashDevices;
  mNorFlashDeviceCount = ARRAY_SIZE (mNorFlashDevices);

  mNorFlashInstances = AllocateRuntimePool (sizeof (NOR_FLASH_INSTANCE *) * mNorFlashDeviceCount);

  for (Index = 0; Index < mNorFlashDeviceCount; Index++) {
    // Check if this nor flash device contain the variable storage region
    if (PcdGet64 (PcdFlashNvStorageVariableBase64) != 0) {
      ContainVariableStorage =
        (NorFlashDevices[Index].RegionBaseAddress <= PcdGet64 (PcdFlashNvStorageVariableBase64)) &&
        (PcdGet64 (PcdFlashNvStorageVariableBase64) + PcdGet32 (PcdFlashNvStorageVariableSize) <=
         NorFlashDevices[Index].RegionBaseAddress + NorFlashDevices[Index].RegionSize);
    } else {
      ContainVariableStorage =
        (NorFlashDevices[Index].RegionBaseAddress <= PcdGet32 (PcdFlashNvStorageVariableBase)) &&
        (PcdGet32 (PcdFlashNvStorageVariableBase) + PcdGet32 (PcdFlashNvStorageVariableSize) <=
         NorFlashDevices[Index].RegionBaseAddress + NorFlashDevices[Index].RegionSize);
    }

    DEBUG ((
      DEBUG_INFO,
      "%a: device base 0x%x, region %x(base 0x%x, length 0x%x), %Scontains variables\n",
      __FUNCTION__,
      NorFlashDevices[Index].DeviceBaseAddress,
      Index,
      NorFlashDevices[Index].RegionBaseAddress,
      NorFlashDevices[Index].RegionSize,
      ContainVariableStorage ? L" " : L" not "
      ));

    if (ContainVariableStorage) {
      EfiCreateProtocolNotifyEvent (
        &gEfiCpuArchProtocolGuid,
        TPL_CALLBACK,
        FlashMemoryInitEventNotify,
        &NorFlashDevices[Index],
        &MemoryInitEvent
        );
    }

    Status = NorFlashCreateInstance (
               NorFlashDevices[Index].DeviceBaseAddress,
               NorFlashDevices[Index].RegionBaseAddress,
               NorFlashDevices[Index].DeviceSize,
               Index,
               NorFlashDevices[Index].BlockSize,
               ContainVariableStorage,
               &mNorFlashInstances[Index]
               );
    if (EFI_ERROR (Status)) {
      DebugPrint (DEBUG_ERROR, "%a: fail to create instance for nor flash index %d, status %r\n", __FUNCTION__, Index, Status);
    }
  }

  //
  // Register for the virtual address change event
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  NorFlashVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mNorFlashVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  POST_CODE (XspiInitDxeEnd);

  DEBUG ((DEBUG_INFO, "%a exit\n", __FUNCTION__));

  return Status;
}
