/*
  Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CIX_SMBIOS_DXE__
#define __CIX_SMBIOS_DXE__

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Protocol/Smbios.h>
#include <IndustryStandard/SmBios.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SocInfoProtocol.h>

// SMBIOS tables often reference each other using
// fixed constants, define a list of these constants
// for our hardcoded tables
enum SMBIOS_REFRENCE_HANDLES {
  SMBIOS_HANDLE_MEMORY = 0x800,
  SMBIOS_HANDLE_DRAM
};

EFI_STATUS
AddSmbiosType16 (
  IN EFI_SMBIOS_PROTOCOL           *Smbios,
  IN const MEM_INIT_OUTPUT_BUFFER  *MemoryInfo
  );

EFI_STATUS
AddSmbiosType17 (
  IN EFI_SMBIOS_PROTOCOL           *Smbios,
  IN const MEM_INIT_OUTPUT_BUFFER  *MemoryInfo
  );

EFI_STATUS
AddSmbiosType19 (
  IN EFI_SMBIOS_PROTOCOL           *Smbios,
  IN const MEM_INIT_OUTPUT_BUFFER  *MemoryInfo
  );

// Memory unit in KB
#define DRAM_LOW_BASE   0x200000
#define DRAM_LOW_TOP    0x2000000
#define DRAM_HIGH_BASE  0x20000000

#define COUNT_MEMORY_DEVICE_NUMBER(n) \
    (((n) & 0x8) >> 3) + (((n) & 0x4) >> 2) + (((n) & 0x2) >> 1) + ((n) & 0x1)

#endif
