/*++

Copyright (c) 2004 - 2005, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  Metronome.h

Abstract:

  NT Emulation Metronome Architectural Protocol Driver as defined in DXE CIS

--*/

#ifndef _NT_THUNK_METRONOME_H_
#define _NT_THUNK_METRONOME_H_

#include "Efi2WinNT.h"
#include "EfiWinNtLib.h"
#include "EfiDriverLib.h"

//
// Produced Protocols
//
#include EFI_ARCH_PROTOCOL_PRODUCER (Metronome)

//
// Period of on tick in 100 nanosecond units
//
#define TICK_PERIOD 2000

//
// Function Prototypes
//

EFI_STATUS
EFIAPI
WinNtMetronomeDriverInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  ImageHandle - TODO: add argument description
  SystemTable - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
EFIAPI
WinNtMetronomeDriverWaitForTick (
  IN EFI_METRONOME_ARCH_PROTOCOL  *This,
  IN UINT32                       TickNumber
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  This        - TODO: add argument description
  TickNumber  - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

#endif
