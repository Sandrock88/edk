/*++

Copyright (c) 2006 - 2007, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  WinNtGop.h

Abstract:

  Private data for the Gop driver that is bound to the WinNt Thunk protocol

--*/

#ifndef _WIN_NT_GOP_H_
#define _WIN_NT_GOP_H_

#include "EfiWinNT.h"
#include "Tiano.h"
#include "EfiDriverLib.h"

//
// Driver Consumed Protocols
//
#include EFI_PROTOCOL_DEFINITION (DevicePath)
#include EFI_PROTOCOL_DEFINITION (WinNtIo)

//
// Driver Produced Protocols
//
#include EFI_PROTOCOL_DEFINITION (DriverBinding)
#include EFI_PROTOCOL_DEFINITION (ComponentName2)
#include EFI_PROTOCOL_DEFINITION (GraphicsOutput)
#include EFI_PROTOCOL_DEFINITION (SimpleTextIn)
#include EFI_PROTOCOL_DEFINITION (SimpleTextInputEx)
#include "LinkedList.h"

//
// WM_SYSKEYDOWN/WM_SYSKEYUP Notification
// lParam
// bit 24: Specifies whether the key is an extended key, 
// such as the right-hand ALT and CTRL keys that appear on 
// an enhanced 101- or 102-key keyboard. 
// The value is 1 if it is an extended key; otherwise, it is 0.
// bit 29:Specifies the context code. 
// The value is 1 if the ALT key is down while the key is pressed/released; 
// it is 0 if the WM_SYSKEYDOWN message is posted to the active window 
// because no window has the keyboard focus.
#define GOP_EXTENDED_KEY         (0x1 << 24)
#define GOP_ALT_KEY_PRESSED      (0x1 << 29)


#define MAX_Q 256

typedef struct {
  UINTN         Front;
  UINTN         Rear;
  UINTN         Count;
  EFI_INPUT_KEY Q[MAX_Q];
} GOP_QUEUE_FIXED;

#define WIN_NT_GOP_CLASS_NAME       L"WinNtGopWindow"

#define GOP_PRIVATE_DATA_SIGNATURE  EFI_SIGNATURE_32 ('S', 'g', 'o', 'N')

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#define SIMPLE_TEXTIN_EX_NOTIFY_GUID \
  { \
    0x856f2def, 0x4e93, 0x4d6b, 0x94, 0xce, 0x1c, 0xfe, 0x47, 0x1, 0x3e, 0xa5 \
  }

#define WIN_NT_GOP_SIMPLE_TEXTIN_EX_NOTIFY_SIGNATURE EFI_SIGNATURE_32 ('W', 'g', 'S', 'n')

typedef struct _WIN_NT_GOP_SIMPLE_TEXTIN_EX_NOTIFY {
  UINTN                                 Signature;
  EFI_HANDLE                            NotifyHandle;
  EFI_KEY_DATA                          KeyData;
  EFI_KEY_NOTIFY_FUNCTION               KeyNotificationFn;
  EFI_LIST_ENTRY                        NotifyEntry;
} WIN_NT_GOP_SIMPLE_TEXTIN_EX_NOTIFY;

#endif

#define GRAPHICS_OUTPUT_INVALIDE_MODE_NUMBER 0xffff

typedef struct {
  UINT32                     HorizontalResolution;
  UINT32                     VerticalResolution;
  UINT32                     ColorDepth;
  UINT32                     RefreshRate;
} GOP_MODE_DATA;

typedef struct {
  UINT64                        Signature;

  EFI_HANDLE                    Handle;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  GraphicsOutput;
  EFI_SIMPLE_TEXT_IN_PROTOCOL   SimpleTextIn;

  EFI_WIN_NT_THUNK_PROTOCOL     *WinNtThunk;

  EFI_UNICODE_STRING_TABLE      *ControllerNameTable;

  //
  // GOP Private Data for QueryMode ()
  //
  GOP_MODE_DATA                 *ModeData;

  //
  // GOP Private Data knowing when to start hardware
  //
  BOOLEAN                       HardwareNeedsStarting;

  CHAR16                        *WindowName;
  CHAR16                        Buffer[160];

  HANDLE                        ThreadInited; // Semaphore
  HANDLE                        ThreadHandle; // Thread
  DWORD                         ThreadId;

  HWND                          WindowHandle;
  WNDCLASSEX                    WindowsClass;

  //
  // This screen is used to redraw the scree when windows events happen. It's
  // updated in the main thread and displayed in the windows thread.
  //
  BITMAPV4HEADER                *VirtualScreenInfo;
  RGBQUAD                       *VirtualScreen;

  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *FillLine;

  //
  // Keyboard Queue used by Simple Text In. WinProc thread adds, and main
  // thread removes.
  //
  CRITICAL_SECTION              QCriticalSection;
  GOP_QUEUE_FIXED               Queue;

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL SimpleTextInEx;
  EFI_KEY_STATE                     KeyState;
  EFI_LIST_ENTRY                    NotifyList;
  BOOLEAN                           LeftShift;
  BOOLEAN                           RightShift;  
  BOOLEAN                           LeftAlt;
  BOOLEAN                           RightAlt;
  BOOLEAN                           LeftCtrl;
  BOOLEAN                           RightCtrl;
  BOOLEAN                           LeftLogo;
  BOOLEAN                           RightLogo;
  BOOLEAN                           Menu;
  BOOLEAN                           SysReq;  
  BOOLEAN                           NumLock;
  BOOLEAN                           ScrollLock;
  BOOLEAN                           CapsLock;  
#endif

} GOP_PRIVATE_DATA;

#define GOP_PRIVATE_DATA_FROM_THIS(a)  \
         CR(a, GOP_PRIVATE_DATA, GraphicsOutput, GOP_PRIVATE_DATA_SIGNATURE)

#define GOP_PRIVATE_DATA_FROM_TEXT_IN_THIS(a)  \
         CR(a, GOP_PRIVATE_DATA, SimpleTextIn, GOP_PRIVATE_DATA_SIGNATURE)

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#define GOP_PRIVATE_DATA_FROM_TEXT_IN_EX_THIS(a)  \
         CR(a, GOP_PRIVATE_DATA, SimpleTextInEx, GOP_PRIVATE_DATA_SIGNATURE)

extern EFI_GUID gSimpleTextInExNotifyGuid;

#endif

//
// Global Protocol Variables
//
extern EFI_DRIVER_BINDING_PROTOCOL  gWinNtGopDriverBinding;
extern EFI_COMPONENT_NAME2_PROTOCOL gWinNtGopComponentName;

//
// Gop Hardware abstraction internal worker functions
//
EFI_STATUS
WinNtGopSupported (
  IN  EFI_WIN_NT_IO_PROTOCOL  *WinNtIo
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  WinNtIo - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
WinNtGopConstructor (
  IN  GOP_PRIVATE_DATA    *Private
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Private - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
WinNtGopDestructor (
  IN  GOP_PRIVATE_DATA    *Private
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Private - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

//
// EFI 1.1 driver model prototypes for Win NT GOP
//

EFI_STATUS
EFIAPI
WinNtGopInitialize (
  IN EFI_HANDLE            ImageHandle,
  IN EFI_SYSTEM_TABLE      *SystemTable
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
WinNtGopDriverBindingSupported (
  IN  EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN  EFI_HANDLE                      Handle,
  IN  EFI_DEVICE_PATH_PROTOCOL        *RemainingDevicePath
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  This                - TODO: add argument description
  Handle              - TODO: add argument description
  RemainingDevicePath - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
EFIAPI
WinNtGopDriverBindingStart (
  IN  EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN  EFI_HANDLE                      Handle,
  IN  EFI_DEVICE_PATH_PROTOCOL        *RemainingDevicePath
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  This                - TODO: add argument description
  Handle              - TODO: add argument description
  RemainingDevicePath - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
EFIAPI
WinNtGopDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Handle,
  IN  UINTN                        NumberOfChildren,
  IN  EFI_HANDLE                   *ChildHandleBuffer
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  This              - TODO: add argument description
  Handle            - TODO: add argument description
  NumberOfChildren  - TODO: add argument description
  ChildHandleBuffer - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
GopPrivateAddQ (
  IN  GOP_PRIVATE_DATA    *Private,
  IN  EFI_INPUT_KEY       Key
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Private - TODO: add argument description
  Key     - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
WinNtGopInitializeSimpleTextInForWindow (
  IN  GOP_PRIVATE_DATA    *Private
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Private - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

EFI_STATUS
WinNtGopDestroySimpleTextInForWindow (
  IN  GOP_PRIVATE_DATA    *Private
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Private - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

UINTN
Atoi (
  IN  CHAR16  *String
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  String  - TODO: add argument description

Returns:

  TODO: add return values

--*/
;

#endif
