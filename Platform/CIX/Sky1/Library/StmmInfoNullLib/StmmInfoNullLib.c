/** @file  StmmInfoNullLib.c

  Copyright 2025 Cix Technology Group Co., Ltd. All Rights Reserved
  Copyright (c) 2022-2024, CIX, Ltd. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "StmmInfoNullLibInternal.h"

EFI_STATUS
EFIAPI
StmmInfoGetVersion (
  IN OUT CHAR16  **Version,
  IN OUT UINT32  *Size
  )
{
  return EFI_UNSUPPORTED;
}
