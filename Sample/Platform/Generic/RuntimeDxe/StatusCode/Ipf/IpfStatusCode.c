/*++

Copyright (c) 2004 - 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  IpfStatusCode.c

Abstract:

  Contains the IPF installation function and an ESAL entry.

--*/

#include "StatusCode.h"

//
// Define the driver entry point
//
EFI_DRIVER_ENTRY_POINT (InstallStatusCode)

EFI_HANDLE               gStatusCodeHandle = NULL;
EFI_STATUS_CODE_PROTOCOL gStatusCodeInstance;  

SAL_RETURN_REGS
ReportStatusCodeEsalServicesClassCommonEntry (
  IN  UINT64                    FunctionId,
  IN  UINT64                    Arg2,
  IN  UINT64                    Arg3,
  IN  UINT64                    Arg4,
  IN  UINT64                    Arg5,
  IN  UINT64                    Arg6,
  IN  UINT64                    Arg7,
  IN  UINT64                    Arg8,
  IN  SAL_EXTENDED_SAL_PROC     ExtendedSalProc,
  IN   BOOLEAN                  VirtualMode,
  IN  VOID                      *Global
  )
/*++

Routine Description:

  Main entry for Extended SAL ReportStatusCode Services

Arguments:

  FunctionId        Function Id which needed to be called
  Arg2              Efi status code type
  Arg3              Efi status code value
  Arg4              Instance number 
  Arg5              Caller Id
  Arg6              Efi status code data
  Arg7              Not used       
  Arg8              Not used       
  ExtendedSalProc   Esal Proc pointer    
  VirtualMode       If this function is called in virtual mode
  Global            This module's global variable pointer
  
Returns:

  SAL_RETURN_REGS

--*/
{
  SAL_RETURN_REGS ReturnVal;

  switch (FunctionId) {

  case ReportStatusCode:
    ReturnVal.Status = StatusCodeReportStatusCode (
                        (EFI_STATUS_CODE_TYPE) Arg2,
                        (EFI_STATUS_CODE_VALUE) Arg3,
                        (UINT32) Arg4,
                        (EFI_GUID *) Arg5,
                        (EFI_STATUS_CODE_DATA *) Arg6
                        );
    break;

  default:
    ReturnVal.Status = EFI_SAL_INVALID_ARGUMENT;
    break;
  }

  return ReturnVal;
}

EFI_STATUS
EFIAPI
InstallStatusCode (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
/*++

Routine Description:

  Install the ReportStatusCode runtime service.

Arguments:

  ImageHandle     Image handle of the loaded driver
  SystemTable     Pointer to the System Table

Returns:

  EFI_SUCCESS     The function always returns success.

--*/
{
  EFI_STATUS               Status;

  //
  // Initialize RT status code
  //
  InitializeStatusCode (ImageHandle, SystemTable);

  //
  // Initialize ESAL capabilities
  //
  RegisterEsalClass (
    &gEfiExtendedSalStatusCodeServicesProtocolGuid,
    NULL,
    ReportStatusCodeEsalServicesClassCommonEntry,
    StatusCode,
    NULL
    );

  //
  // Update runtime service table.
  //
  gStatusCodeInstance.ReportStatusCode = EfiReportStatusCode;
#if (EFI_SPECIFICATION_VERSION < 0x00020000)
  SystemTable->RuntimeServices->ReportStatusCode = gStatusCodeInstance.ReportStatusCode;
#endif
 
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gStatusCodeHandle,
                  &gEfiStatusCodeRuntimeProtocolGuid,
                  &gStatusCodeInstance,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
