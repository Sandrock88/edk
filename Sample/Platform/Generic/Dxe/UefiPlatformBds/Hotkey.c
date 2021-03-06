/*++

Copyright (c) 2007 - 2008, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  Hotkey.h

Abstract:

  Provides a way for 3rd party applications to register themselves for launch by the
  Boot Manager based on hot key

Revision History

--*/

#include "Hotkey.h"

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)

EFI_LIST_ENTRY  mHotkeyList = INITIALIZE_LIST_HEAD_VARIABLE (mHotkeyList);
BOOLEAN         mHotkeyCallbackPending = FALSE;
EFI_EVENT       mHotkeyEvent;
VOID            *mHotkeyRegistration;


BOOLEAN
IsKeyOptionValid (
  IN EFI_KEY_OPTION     *KeyOption
)
/*++

Routine Description:

  Check if the Key Option is valid or not.

Arguments:

  KeyOption    - The Hot Key Option to be checked.

Returns:

  TRUE         - The Hot Key Option is valid.
  FALSE        - The Hot Key Option is invalid.

--*/
{
  UINT16   BootOptionName[10];
  UINT8    *BootOptionVar;
  UINTN    BootOptionSize;
  UINT32   Crc;

  //
  // Check whether corresponding Boot Option exist
  //
  SPrint (BootOptionName, sizeof (BootOptionName), L"Boot%04x", KeyOption->BootOption);
  BootOptionVar = BdsLibGetVariableAndSize (
                    BootOptionName,
                    &gEfiGlobalVariableGuid,
                    &BootOptionSize
                    );

  if (BootOptionVar == NULL || BootOptionSize == 0) {
    return FALSE;
  }

  //
  // Check CRC for Boot Option
  //
  gBS->CalculateCrc32 (BootOptionVar, BootOptionSize, &Crc);
  gBS->FreePool (BootOptionVar);

  return (KeyOption->BootOptionCrc == Crc) ? TRUE : FALSE;
}

EFI_STATUS
RegisterHotkey (
  IN EFI_KEY_OPTION     *KeyOption,
  OUT UINT16            *KeyOptionNumber
)
/*++

Routine Description:

  Create Key#### for the given hotkey.

Arguments:

  KeyOption             - The Hot Key Option to be added.
  KeyOptionNumber       - The key option number for Key#### (optional).

Returns:

  EFI_SUCCESS           - Register hotkey successfully.
  EFI_INVALID_PARAMETER - The hotkey option is invalid.

--*/
{
  UINT16          KeyOptionName[10];
  UINT16          *KeyOrder;
  UINTN           KeyOrderSize;
  UINT16          *NewKeyOrder;
  UINTN           Index;
  UINT16          MaxOptionNumber;
  UINT16          RegisterOptionNumber;
  EFI_KEY_OPTION  *TempOption;
  UINTN           TempOptionSize;
  EFI_STATUS      Status;
  UINTN           KeyOptionSize;
  BOOLEAN         UpdateBootOption;

  //
  // Validate the given key option
  //
  if (!IsKeyOptionValid (KeyOption)) {
    return EFI_INVALID_PARAMETER;
  }

  KeyOptionSize = sizeof (EFI_KEY_OPTION) + KeyOption->KeyData.Options.InputKeyCount * sizeof (EFI_INPUT_KEY);
  UpdateBootOption = FALSE;

  //
  // Check whether HotKey conflict with keys used by Setup Browser
  //

  KeyOrder = BdsLibGetVariableAndSize (
               VarKeyOrder,
               &gEfiGlobalVariableGuid,
               &KeyOrderSize
               );
  if (KeyOrder == NULL) {
    KeyOrderSize = 0;
  }

  //
  // Find free key option number
  //
  MaxOptionNumber = 0;
  TempOption = NULL;
  for (Index = 0; Index < KeyOrderSize / sizeof (UINT16); Index++) {
    if (MaxOptionNumber < KeyOrder[Index]) {
      MaxOptionNumber = KeyOrder[Index];
    }

    SPrint (KeyOptionName, sizeof (KeyOptionName), L"Key%04x", KeyOrder[Index]);
    TempOption = BdsLibGetVariableAndSize (
                   KeyOptionName,
                   &gEfiGlobalVariableGuid,
                   &TempOptionSize
                   );

    if (EfiCompareMem (TempOption, KeyOption, TempOptionSize) == 0) {
      //
      // Got the option, so just return
      //
      gBS->FreePool (TempOption);
      gBS->FreePool (KeyOrder);
      return EFI_SUCCESS;
    }

    if (KeyOption->KeyData.PackedValue == TempOption->KeyData.PackedValue) {
      if (KeyOption->KeyData.Options.InputKeyCount == 0 ||
          EfiCompareMem (
            ((UINT8 *) TempOption) + sizeof (EFI_KEY_OPTION),
            ((UINT8 *) KeyOption) + sizeof (EFI_KEY_OPTION),
            KeyOptionSize - sizeof (EFI_KEY_OPTION)
            ) == 0) {
          //
          // Hotkey is the same but BootOption changed, need update
          //
          UpdateBootOption = TRUE;
          break;
      }
    }

    gBS->FreePool (TempOption);
  }

  if (UpdateBootOption) {
    RegisterOptionNumber = KeyOrder[Index];
    gBS->FreePool (TempOption);
  } else {
    RegisterOptionNumber = MaxOptionNumber + 1;
  }

  if (KeyOptionNumber != NULL) {
    *KeyOptionNumber = RegisterOptionNumber;
  }

  //
  // Create variable Key####
  //
  SPrint (KeyOptionName, sizeof (KeyOptionName), L"Key%04x", RegisterOptionNumber);
  Status = gRT->SetVariable (
                  KeyOptionName,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                  KeyOptionSize,
                  KeyOption
                  );
  if (EFI_ERROR (Status)) {
    EfiLibSafeFreePool (KeyOrder);
    return Status;
  }

  //
  // Update the key order variable - "KeyOrder"
  //
  if (!UpdateBootOption) {
    Index = KeyOrderSize / sizeof (UINT16);
    KeyOrderSize += sizeof (UINT16);
  }

  NewKeyOrder = EfiLibAllocatePool (KeyOrderSize);
  if (NewKeyOrder == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (KeyOrder != NULL) {
    EfiCopyMem (NewKeyOrder, KeyOrder, KeyOrderSize);
  }

  NewKeyOrder[Index] = RegisterOptionNumber;

  Status = gRT->SetVariable (
                  VarKeyOrder,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                  KeyOrderSize,
                  NewKeyOrder
                  );

  gBS->FreePool (KeyOrder);
  gBS->FreePool (NewKeyOrder);

  return Status;
}

EFI_STATUS
UnregisterHotkey (
  IN UINT16     KeyOptionNumber
)
/*++

Routine Description:

  Delete Key#### for the given Key Option number.

Arguments:

  KeyOptionNumber       - Key option number for Key####

Returns:

  EFI_SUCCESS           - Unregister hotkey successfully.
  EFI_NOT_FOUND         - No Key#### is found for the given Key Option number.

--*/
{
  UINT16      KeyOption[10];
  UINTN       Index;
  EFI_STATUS  Status;
  UINTN       Index2Del;
  UINT16      *KeyOrder;
  UINTN       KeyOrderSize;

  //
  // Delete variable Key####
  //
  SPrint (KeyOption, sizeof (KeyOption), L"Key%04x", KeyOptionNumber);
  gRT->SetVariable (
         KeyOption,
         &gEfiGlobalVariableGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
         0,
         NULL
         );

  //
  // Adjust key order array
  //
  KeyOrder = BdsLibGetVariableAndSize (
               VarKeyOrder,
               &gEfiGlobalVariableGuid,
               &KeyOrderSize
               );
  if (KeyOrder == NULL) {
    return EFI_SUCCESS;
  }

  Index2Del = 0;
  for (Index = 0; Index < KeyOrderSize / sizeof (UINT16); Index++) {
    if (KeyOrder[Index] == KeyOptionNumber) {
      Index2Del = Index;
      break;
    }
  }

  if (Index != KeyOrderSize / sizeof (UINT16)) {
    //
    // KeyOptionNumber found in "KeyOrder", delete it
    //
    for (Index = Index2Del; Index < KeyOrderSize / sizeof (UINT16) - 1; Index++) {
      KeyOrder[Index] = KeyOrder[Index + 1];
    }

    KeyOrderSize -= sizeof (UINT16);
  }

  Status = gRT->SetVariable (
                  VarKeyOrder,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                  KeyOrderSize,
                  KeyOrder
                  );

  gBS->FreePool (KeyOrder);

  return Status;
}

EFI_STATUS
HotkeyCallback (
  IN EFI_KEY_DATA     *KeyData
)
/*++

Routine Description:

  This is the common notification function for HotKeys, it will be registered
  with SimpleTextInEx protocol interface - RegisterKeyNotify() of ConIn handle.

Arguments:

  KeyData               - A pointer to a buffer that is filled in with the keystroke
                          information for the key that was pressed.

Returns:

  EFI_SUCCESS           - KeyData is successfully processed.

--*/
{
  BOOLEAN            HotkeyCatched;
  EFI_LIST_ENTRY     BootLists;
  EFI_LIST_NODE      *Link;
  BDS_HOTKEY_OPTION  *Hotkey;
  UINT16             Buffer[10];
  BDS_COMMON_OPTION  *BootOption;
  UINTN              ExitDataSize;
  CHAR16             *ExitData;
  EFI_TPL            OldTpl;
  EFI_STATUS         Status;
  EFI_KEY_DATA       *HotkeyData;

  if (mHotkeyCallbackPending) {
    //
    // When responsing to a Hotkey, ignore sequential hotkey stroke until
    // the current Boot#### load option returned
    //
    return EFI_SUCCESS;
  }

  Status = EFI_SUCCESS;
  Link = GetFirstNode (&mHotkeyList);

  while (!IsNull (&mHotkeyList, Link)) {
    HotkeyCatched = FALSE;
    Hotkey = BDS_HOTKEY_OPTION_FROM_LINK (Link);

    //
    // Is this Key Stroke we are waiting for?
    //
    HotkeyData = &Hotkey->KeyData[Hotkey->WaitingKey];
    if ((KeyData->Key.ScanCode == HotkeyData->Key.ScanCode) &&
       (KeyData->Key.UnicodeChar == HotkeyData->Key.UnicodeChar) &&
       ((HotkeyData->KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) ? (KeyData->KeyState.KeyShiftState == HotkeyData->KeyState.KeyShiftState) : TRUE)) {
      //
      // Receive an expecting key stroke
      //
      if (Hotkey->CodeCount > 1) {
        //
        // For hotkey of key combination, transit to next waiting state
        //
        Hotkey->WaitingKey++;

        if (Hotkey->WaitingKey == Hotkey->CodeCount) {
          //
          // Received the whole key stroke sequence
          //
          HotkeyCatched = TRUE;
        }
      } else {
        //
        // For hotkey of single key stroke
        //
        HotkeyCatched = TRUE;
      }
    } else {
      //
      // Receive an unexpected key stroke, reset to initial waiting state
      //
      Hotkey->WaitingKey = 0;
    }

    if (HotkeyCatched) {
      //
      // Reset to initial waiting state
      //
      Hotkey->WaitingKey = 0;

      //
      // Launch its BootOption
      //
      InitializeListHead (&BootLists);

      SPrint (Buffer, sizeof (Buffer), L"Boot%04x", Hotkey->BootOptionNumber);
      BootOption = BdsLibVariableToOption (&BootLists, Buffer);
      BootOption->BootCurrent = Hotkey->BootOptionNumber;
      BdsLibConnectDevicePath (BootOption->DevicePath);

      //
      // Clear the screen before launch this BootOption
      //
      gST->ConOut->Reset (gST->ConOut, FALSE);

      //
      // BdsLibBootViaBootOption() is expected to be invoked at TPL level EFI_TPL_DRIVER,
      // so raise the TPL to EFI_TPL_DRIVER first, then restore it
      //
      OldTpl = gBS->RaiseTPL (EFI_TPL_DRIVER);

      mHotkeyCallbackPending = TRUE;
      Status = BdsLibBootViaBootOption (BootOption, BootOption->DevicePath, &ExitDataSize, &ExitData);
      mHotkeyCallbackPending = FALSE;

      gBS->RestoreTPL (OldTpl);

      if (EFI_ERROR (Status)) {
        //
        // Call platform action to indicate the boot fail
        //
        PlatformBdsBootFail (BootOption, Status, ExitData, ExitDataSize);
      } else {
        //
        // Call platform action to indicate the boot success
        //
        PlatformBdsBootSuccess (BootOption);
      }
    }

    Link = GetNextNode (&mHotkeyList, Link);
  }

  return Status;
}

EFI_STATUS
HotkeyRegisterNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *SimpleTextInEx
)
/*++

Routine Description:

  Register the common HotKey notify function to given SimpleTextInEx protocol instance.

Arguments:

  SimpleTextInEx        - Simple Text Input Ex protocol instance

Returns:

  EFI_SUCCESS           - Register hotkey notification function successfully.
  EFI_OUT_OF_RESOURCES  - Unable to allocate necessary data structures.

--*/
{
  UINTN              Index;
  EFI_STATUS         Status;
  EFI_LIST_NODE      *Link;
  BDS_HOTKEY_OPTION  *Hotkey;

  //
  // Register notification function for each hotkey
  //
  Link = GetFirstNode (&mHotkeyList);

  while (!IsNull (&mHotkeyList, Link)) {
    Hotkey = BDS_HOTKEY_OPTION_FROM_LINK (Link);

    Index = 0;
    do {
      Status = SimpleTextInEx->RegisterKeyNotify (
                                 SimpleTextInEx,
                                 &Hotkey->KeyData[Index],
                                 HotkeyCallback,
                                 &Hotkey->NotifyHandle
                                 );
      if (EFI_ERROR (Status)) {
        //
        // some of the hotkey registry failed
        //
        return Status;
      }
      Index ++;
    } while (Index < Hotkey->CodeCount);

    Link = GetNextNode (&mHotkeyList, Link);
  }

  return EFI_SUCCESS;
}

VOID
EFIAPI
HotkeyEvent (
  IN EFI_EVENT    Event,
  IN VOID         *Context
  )
/*++

Routine Description:
  Callback function for SimpleTextInEx protocol install events

Arguments:

  Standard event notification function arguments:
  Event         - the event that is signaled.
  Context       - not used here.

Returns:

--*/
{
  EFI_STATUS                         Status;
  UINTN                              BufferSize;
  EFI_HANDLE                         Handle;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *SimpleTextInEx;

  while (TRUE) {
    BufferSize = sizeof (EFI_HANDLE);
    Status = gBS->LocateHandle (
                    ByRegisterNotify,
                    NULL,
                    mHotkeyRegistration,
                    &BufferSize,
                    &Handle
                    );
    if (EFI_ERROR (Status)) {
      //
      // If no more notification events exist
      //
      return ;
    }

    Status = gBS->HandleProtocol (
                    Handle,
                    &gEfiSimpleTextInputExProtocolGuid,
                    &SimpleTextInEx
                    );
    ASSERT_EFI_ERROR (Status);

    HotkeyRegisterNotify (SimpleTextInEx);
  }
}

EFI_STATUS
HotkeyInsertList (
  IN EFI_KEY_OPTION     *KeyOption
)
/*++

Routine Description:

  Insert Key Option to hotkey list.

Arguments:

  KeyOption   - The Hot Key Option to be added to hotkey list.

Returns:

  EFI_SUCCESS - Add to hotkey list success.

--*/
{
  BDS_HOTKEY_OPTION  *HotkeyLeft;
  BDS_HOTKEY_OPTION  *HotkeyRight;
  UINTN              Index;
  EFI_BOOT_KEY_DATA  KeyOptions;
  UINT32             KeyShiftStateLeft;
  UINT32             KeyShiftStateRight;
  EFI_INPUT_KEY      *InputKey;
  EFI_KEY_DATA       *KeyData;

  HotkeyLeft = EfiLibAllocateZeroPool (sizeof (BDS_HOTKEY_OPTION));
  if (HotkeyLeft == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  HotkeyLeft->Signature = BDS_HOTKEY_OPTION_SIGNATURE;
  HotkeyLeft->BootOptionNumber = KeyOption->BootOption;

  KeyOptions = KeyOption->KeyData;

  HotkeyLeft->CodeCount = (UINT8) KeyOptions.Options.InputKeyCount;

  //
  // Map key shift state from KeyOptions to EFI_KEY_DATA.KeyState
  //
  KeyShiftStateRight = EFI_SHIFT_STATE_VALID;
  if (KeyOptions.Options.ShiftPressed) {
    KeyShiftStateRight |= EFI_RIGHT_SHIFT_PRESSED;
  }
  if (KeyOptions.Options.ControlPressed) {
    KeyShiftStateRight |= EFI_RIGHT_CONTROL_PRESSED;
  }
  if (KeyOptions.Options.AltPressed) {
    KeyShiftStateRight |= EFI_RIGHT_ALT_PRESSED;
  }
  if (KeyOptions.Options.LogoPressed) {
    KeyShiftStateRight |= EFI_RIGHT_LOGO_PRESSED;
  }
  if (KeyOptions.Options.MenuPressed) {
    KeyShiftStateRight |= EFI_MENU_KEY_PRESSED;
  }
  if (KeyOptions.Options.SysReqPressed) {
    KeyShiftStateRight |= EFI_SYS_REQ_PRESSED;
  }

  KeyShiftStateLeft = (KeyShiftStateRight & 0xffffff00) | ((KeyShiftStateRight & 0xff) << 1);

  InputKey = (EFI_INPUT_KEY *) (((UINT8 *) KeyOption) + sizeof (EFI_KEY_OPTION));

  Index = 0;
  KeyData = &HotkeyLeft->KeyData[0];
  do {
    //
    // If Key CodeCount is 0, then only KeyData[0] is used;
    // if Key CodeCount is n, then KeyData[0]~KeyData[n-1] are used
    //
    KeyData->Key.ScanCode = InputKey[Index].ScanCode;
    KeyData->Key.UnicodeChar = InputKey[Index].UnicodeChar;
    KeyData->KeyState.KeyShiftState = KeyShiftStateLeft;

    Index++;
    KeyData++;
  } while (Index < HotkeyLeft->CodeCount);
  InsertTailList (&mHotkeyList, &HotkeyLeft->Link);

  if (KeyShiftStateLeft != KeyShiftStateRight) {
    //
    // Need an extra hotkey for shift key on right
    //
    HotkeyRight = EfiLibAllocateCopyPool (sizeof (BDS_HOTKEY_OPTION), HotkeyLeft);
    if (HotkeyRight == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Index = 0;
    KeyData = &HotkeyRight->KeyData[0];
    do {
      //
      // Key.ScanCode and Key.UnicodeChar have already been initialized,
      // only need to update KeyState.KeyShiftState
      //
      KeyData->KeyState.KeyShiftState = KeyShiftStateRight;

      Index++;
      KeyData++;
    } while (Index < HotkeyRight->CodeCount);
    InsertTailList (&mHotkeyList, &HotkeyRight->Link);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
InitializeHotkeyService (
  VOID
  )
/*++

Routine Description:

  Process all the "Key####" variables, associate Hotkeys with corresponding Boot Options.

Arguments:

  None

Returns:

  EFI_SUCCESS   - Hotkey services successfully initialized.

--*/
{
  EFI_STATUS      Status;
  UINT32          BootOptionSupport;
  UINT16          *KeyOrder;
  UINTN           KeyOrderSize;
  UINTN           Index;
  UINT16          KeyOptionName[8];
  UINTN           KeyOptionSize;
  EFI_KEY_OPTION  *KeyOption;

  //
  // Export our capability - EFI_BOOT_OPTION_SUPPORT_KEY and EFI_BOOT_OPTION_SUPPORT_APP
  // with maximum number of key presses of 3
  //
  BootOptionSupport = EFI_BOOT_OPTION_SUPPORT_KEY | EFI_BOOT_OPTION_SUPPORT_APP;
  SET_BOOT_OPTION_SUPPORT_KEY_COUNT (BootOptionSupport, 3);
  Status = gRT->SetVariable (
                  L"BootOptionSupport",
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (UINT32),
                  &BootOptionSupport
                  );

  //
  // Get valid Key Option List from private EFI variable "KeyOrder"
  //
  KeyOrder = BdsLibGetVariableAndSize (
               VarKeyOrder,
               &gEfiGlobalVariableGuid,
               &KeyOrderSize
               );

  if (KeyOrder == NULL) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < KeyOrderSize / sizeof (UINT16); Index ++) {
    SPrint (KeyOptionName, sizeof (KeyOptionName), L"Key%04x", KeyOrder[Index]);
    KeyOption = BdsLibGetVariableAndSize (
                  KeyOptionName,
                  &gEfiGlobalVariableGuid,
                  &KeyOptionSize
                  );

    if (KeyOption == NULL || !IsKeyOptionValid (KeyOption)) {
      UnregisterHotkey (KeyOrder[Index]);
    } else {
      HotkeyInsertList (KeyOption);
    }
  }

  //
  // Register Protocol notify for Hotkey service
  //
  Status = gBS->CreateEvent (
                  EFI_EVENT_NOTIFY_SIGNAL,
                  EFI_TPL_CALLBACK,
                  HotkeyEvent,
                  NULL,
                  &mHotkeyEvent
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Register for protocol notifications on this event
  //
  Status = gBS->RegisterProtocolNotify (
                  &gEfiSimpleTextInputExProtocolGuid,
                  mHotkeyEvent,
                  &mHotkeyRegistration
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

#endif
