/*++
Copyright (c) 2007 - 2010, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  IfrParse.c

Abstract:

  Parser for IFR binary encoding.

--*/

#include "Setup.h"
#include "Ui.h"
#include "EfiPrintLib.h"

UINT16           mStatementIndex;
UINT16           mExpressionOpCodeIndex;

BOOLEAN          mInScopeSubtitle;
BOOLEAN          mInScopeSuppress;
BOOLEAN          mInScopeGrayOut;
BOOLEAN          mInScopeDisable;
FORM_EXPRESSION  *mSuppressExpression;
FORM_EXPRESSION  *mGrayOutExpression;
FORM_EXPRESSION  *mDisableExpression;

EFI_GUID  gTianoHiiIfrGuid = EFI_IFR_TIANO_GUID;

FORM_BROWSER_STATEMENT *
CreateStatement (
  IN UINT8                        *OpCodeData,
  IN OUT FORM_BROWSER_FORMSET     *FormSet,
  IN OUT FORM_BROWSER_FORM        *Form
  )
/*++

Routine Description:
  Initialize Statement header members.

Arguments:
  OpCodeData - Pointer of the raw OpCode data.
  FormSet    - Pointer of the current FormSe.
  Form       - Pointer of the current Form.

Returns:
  The Statement.

--*/
{
  FORM_BROWSER_STATEMENT    *Statement;
  EFI_IFR_STATEMENT_HEADER  *StatementHdr;

  if (Form == NULL) {
    //
    // We are currently not in a Form Scope, so just skip this Statement
    //
    return NULL;
  }

  Statement = &FormSet->StatementBuffer[mStatementIndex];
  mStatementIndex++;

  InitializeListHead (&Statement->DefaultListHead);
  InitializeListHead (&Statement->OptionListHead);
  InitializeListHead (&Statement->InconsistentListHead);
  InitializeListHead (&Statement->NoSubmitListHead);

  Statement->Signature = FORM_BROWSER_STATEMENT_SIGNATURE;

  Statement->Operand = ((EFI_IFR_OP_HEADER *) OpCodeData)->OpCode;

  StatementHdr = (EFI_IFR_STATEMENT_HEADER *) (OpCodeData + sizeof (EFI_IFR_OP_HEADER));
  EfiCopyMem (&Statement->Prompt, &StatementHdr->Prompt, sizeof (EFI_STRING_ID));
  EfiCopyMem (&Statement->Help, &StatementHdr->Help, sizeof (EFI_STRING_ID));

  if (mInScopeSuppress) {
    Statement->SuppressExpression = mSuppressExpression;
  }

  if (mInScopeGrayOut) {
    Statement->GrayOutExpression = mGrayOutExpression;
  }

  if (mInScopeDisable) {
    Statement->DisableExpression = mDisableExpression;
  }

  Statement->InSubtitle = mInScopeSubtitle;

  //
  // Insert this Statement into current Form
  //
  InsertTailList (&Form->StatementListHead, &Statement->Link);

  return Statement;
}

FORM_BROWSER_STATEMENT *
CreateQuestion (
  IN UINT8                        *OpCodeData,
  IN OUT FORM_BROWSER_FORMSET     *FormSet,
  IN OUT FORM_BROWSER_FORM        *Form
  )
/*++

Routine Description:
  Initialize Question's members.

Arguments:
  OpCodeData - Pointer of the raw OpCode data.
  FormSet    - Pointer of the current FormSet.
  Form       - Pointer of the current Form.

Returns:
  The Question.

--*/
{
  FORM_BROWSER_STATEMENT   *Statement;
  EFI_IFR_QUESTION_HEADER  *QuestionHdr;
  EFI_LIST_ENTRY           *Link;
  FORMSET_STORAGE          *Storage;
  NAME_VALUE_NODE          *NameValueNode;

  Statement = CreateStatement (OpCodeData, FormSet, Form);
  if (Statement == NULL) {
    return NULL;
  }

  QuestionHdr = (EFI_IFR_QUESTION_HEADER *) (OpCodeData + sizeof (EFI_IFR_OP_HEADER));
  EfiCopyMem (&Statement->QuestionId, &QuestionHdr->QuestionId, sizeof (EFI_QUESTION_ID));
  EfiCopyMem (&Statement->VarStoreId, &QuestionHdr->VarStoreId, sizeof (EFI_VARSTORE_ID));
  EfiCopyMem (&Statement->VarStoreInfo.VarOffset, &QuestionHdr->VarStoreInfo.VarOffset, sizeof (UINT16));

  Statement->QuestionFlags = QuestionHdr->Flags;

  if (Statement->VarStoreId == 0) {
    //
    // VarStoreId of zero indicates no variable storage
    //
    return Statement;
  }

  //
  // Find Storage for this Question
  //
  Link = GetFirstNode (&FormSet->StorageListHead);
  while (!IsNull (&FormSet->StorageListHead, Link)) {
    Storage = FORMSET_STORAGE_FROM_LINK (Link);

    if (Storage->VarStoreId == Statement->VarStoreId) {
      Statement->Storage = Storage;
      break;
    }

    Link = GetNextNode (&FormSet->StorageListHead, Link);
  }
  ASSERT (Statement->Storage != NULL);

  //
  // Initialilze varname for Name/Value or EFI Variable
  //
  if ((Statement->Storage->Type == EFI_HII_VARSTORE_NAME_VALUE) ||
      (Statement->Storage->Type == EFI_HII_VARSTORE_EFI_VARIABLE)) {
    Statement->VariableName = GetToken (Statement->VarStoreInfo.VarName, FormSet->HiiHandle);
    ASSERT (Statement->VariableName != NULL);

    if (Statement->Storage->Type == EFI_HII_VARSTORE_NAME_VALUE) {
      //
      // Insert to Name/Value varstore list
      //
      NameValueNode = EfiLibAllocateZeroPool (sizeof (NAME_VALUE_NODE));
      ASSERT (NameValueNode != NULL);
      NameValueNode->Signature = NAME_VALUE_NODE_SIGNATURE;
      NameValueNode->Name = EfiLibAllocateCopyPool (EfiStrSize (Statement->VariableName), Statement->VariableName);
      ASSERT (NameValueNode->Name != NULL);
      NameValueNode->Value = EfiLibAllocateZeroPool (0x10);
      ASSERT (NameValueNode->Value != NULL);
      NameValueNode->EditValue = EfiLibAllocateZeroPool (0x10);
      ASSERT (NameValueNode->EditValue != NULL);

      InsertTailList (&Statement->Storage->NameValueListHead, &NameValueNode->Link);
    }
  }

  return Statement;
}

FORM_EXPRESSION *
CreateExpression (
  IN OUT FORM_BROWSER_FORM        *Form
  )
/*++

Routine Description:
  Allocate a FORM_EXPRESSION node.

Arguments:
  Form - The Form associated with this Expression

Returns:
  Pointer to a FORM_EXPRESSION data structure.

--*/
{
  FORM_EXPRESSION  *Expression;

  Expression = EfiLibAllocateZeroPool (sizeof (FORM_EXPRESSION));
  Expression->Signature = FORM_EXPRESSION_SIGNATURE;
  InitializeListHead (&Expression->OpCodeListHead);

  return Expression;
}

FORMSET_STORAGE *
CreateStorage (
  IN FORM_BROWSER_FORMSET  *FormSet
  )
/*++

Routine Description:
  Allocate a FORMSET_STORAGE data structure and insert to FormSet Storage List.

Arguments:
  FormSet - Pointer of the current FormSet

Returns:
  Pointer to a FORMSET_STORAGE data structure.

--*/
{
  FORMSET_STORAGE  *Storage;

  Storage = EfiLibAllocateZeroPool (sizeof (FORMSET_STORAGE));
  Storage->Signature = FORMSET_STORAGE_SIGNATURE;
  InitializeListHead (&Storage->NameValueListHead);
  InsertTailList (&FormSet->StorageListHead, &Storage->Link);

  return Storage;
}

EFI_STATUS
InitializeConfigHdr (
  IN FORM_BROWSER_FORMSET  *FormSet,
  IN OUT FORMSET_STORAGE   *Storage
  )
/*++

Routine Description:
  Create ConfigHdr string for a storage.

Arguments:
  FormSet    - Pointer of the current FormSet
  Storage    - Pointer of the storage

Returns:
  EFI_SUCCESS - Initialize ConfigHdr success

--*/
{
  EFI_STATUS  Status;
  UINTN       StrBufferLen;
  CHAR16      *Name;

  if (Storage->Type == EFI_HII_VARSTORE_BUFFER) {
    Name = Storage->Name;
  } else {
    Name = NULL;
  }

  StrBufferLen = 0;
  Status = ConstructConfigHdr (
             Storage->ConfigHdr,
             &StrBufferLen,
             &Storage->Guid,
             Name,
             FormSet->DriverHandle
             );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Storage->ConfigHdr = EfiLibAllocateZeroPool (StrBufferLen);
    Status = ConstructConfigHdr (
               Storage->ConfigHdr,
               &StrBufferLen,
               &Storage->Guid,
               Name,
               FormSet->DriverHandle
               );
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Storage->ConfigRequest = EfiLibAllocateCopyPool (StrBufferLen, Storage->ConfigHdr);
  Storage->SpareStrLen = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
InitializeRequestElement (
  IN OUT FORM_BROWSER_FORMSET     *FormSet,
  IN OUT FORM_BROWSER_STATEMENT   *Question
  )
/*++

Routine Description:
  Initialize Request Element of a Question. <RequestElement> ::= '&'<BlockName> | '&'<Label>

Arguments:
  FormSet  - Pointer of the current FormSet.
  Question - The Question to be initialized.

Returns:
  EFI_SUCCESS           - Function success.
  EFI_INVALID_PARAMETER - No storage associated with the Question.

--*/
{
  FORMSET_STORAGE  *Storage;
  UINTN            StrLen;
  UINTN            StrSize;
  CHAR16           *NewStr;
  CHAR16           RequestElement[30];

  Storage = Question->Storage;
  if (Storage == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Storage->Type == EFI_HII_VARSTORE_EFI_VARIABLE) {
    //
    // <ConfigRequest> is unnecessary for EFI variable storage,
    // GetVariable()/SetVariable() will be used to retrieve/save values
    //
    return EFI_SUCCESS;
  }

  //
  // Prepare <RequestElement>
  //
  if (Storage->Type == EFI_HII_VARSTORE_BUFFER) {
    StrLen = SPrint (
               RequestElement,
               30 * sizeof (CHAR16),
               L"&OFFSET=%x&WIDTH=%x",
               (UINTN) Question->VarStoreInfo.VarOffset,
               (UINTN) Question->StorageWidth
               );
    Question->BlockName = EfiLibAllocateCopyPool ((StrLen + 1) * sizeof (CHAR16), RequestElement);
  } else {
    StrLen = SPrint (RequestElement, 30 * sizeof (CHAR16), L"&%s", Question->VariableName);
  }

  if ((Question->Operand == EFI_IFR_PASSWORD_OP) && (Question->QuestionFlags & EFI_IFR_FLAG_CALLBACK)) {
    //
    // Password with CALLBACK flag is stored in encoded format,
    // so don't need to append it to <ConfigRequest>
    //
    return EFI_SUCCESS;
  }

  //
  // Append <RequestElement> to <ConfigRequest>
  //
  if (StrLen > Storage->SpareStrLen) {
    //
    // Old String buffer is not sufficient for RequestElement, allocate a new one
    //
    StrSize = (Storage->ConfigRequest != NULL) ? EfiStrSize (Storage->ConfigRequest) : sizeof (CHAR16);
    NewStr = EfiLibAllocateZeroPool (StrSize + (CONFIG_REQUEST_STRING_INCREMENTAL * sizeof (CHAR16)));
    if (Storage->ConfigRequest != NULL) {
      EfiCopyMem (NewStr, Storage->ConfigRequest, StrSize);
      gBS->FreePool (Storage->ConfigRequest);
    }
    Storage->ConfigRequest = NewStr;
    Storage->SpareStrLen = CONFIG_REQUEST_STRING_INCREMENTAL;
  }

  EfiStrCat (Storage->ConfigRequest, RequestElement);
  Storage->ElementCount++;
  Storage->SpareStrLen -= StrLen;

  return EFI_SUCCESS;
}

VOID
DestroyExpression (
  IN FORM_EXPRESSION   *Expression
  )
/*++

Routine Description:
  Free resources of a Expression

Arguments:
  FormSet - Pointer of the Expression

Returns:
  None.

--*/
{
  EFI_LIST_ENTRY     *Link;
  EXPRESSION_OPCODE  *OpCode;

  while (!IsListEmpty (&Expression->OpCodeListHead)) {
    Link = GetFirstNode (&Expression->OpCodeListHead);
    OpCode = EXPRESSION_OPCODE_FROM_LINK (Link);
    RemoveEntryList (&OpCode->Link);

    EfiLibSafeFreePool (OpCode->ValueList);
  }

  //
  // Free this Expression
  //
  gBS->FreePool (Expression);
}

VOID
DestroyStorage (
  IN FORMSET_STORAGE   *Storage
  )
/*++

Routine Description:
  Free resources of a storage

Arguments:
  Storage - Pointer of the storage

Returns:
  None.

--*/
{
  EFI_LIST_ENTRY     *Link;
  NAME_VALUE_NODE    *NameValueNode;

  if (Storage == NULL) {
    return;
  }

  EfiLibSafeFreePool (Storage->Name);
  EfiLibSafeFreePool (Storage->Buffer);
  EfiLibSafeFreePool (Storage->EditBuffer);

  while (!IsListEmpty (&Storage->NameValueListHead)) {
    Link = GetFirstNode (&Storage->NameValueListHead);
    NameValueNode = NAME_VALUE_NODE_FROM_LINK (Link);
    RemoveEntryList (&NameValueNode->Link);

    EfiLibSafeFreePool (NameValueNode->Name);
    EfiLibSafeFreePool (NameValueNode->Value);
    EfiLibSafeFreePool (NameValueNode->EditValue);
    EfiLibSafeFreePool (NameValueNode);
  }

  EfiLibSafeFreePool (Storage->ConfigHdr);
  EfiLibSafeFreePool (Storage->ConfigRequest);

  gBS->FreePool (Storage);
}

VOID
DestroyStatement (
  IN OUT FORM_BROWSER_STATEMENT  *Statement
  )
/*++

Routine Description:
  Free resources of a Statement

Arguments:
  Statement - Pointer of the Statement

Returns:
  None.

--*/
{
  EFI_LIST_ENTRY    *Link;
  QUESTION_DEFAULT  *Default;
  QUESTION_OPTION   *Option;
  FORM_EXPRESSION   *Expression;

  //
  // Free Default value List
  //
  while (!IsListEmpty (&Statement->DefaultListHead)) {
    Link = GetFirstNode (&Statement->DefaultListHead);
    Default = QUESTION_DEFAULT_FROM_LINK (Link);
    RemoveEntryList (&Default->Link);

    gBS->FreePool (Default);
  }

  //
  // Free Options List
  //
  while (!IsListEmpty (&Statement->OptionListHead)) {
    Link = GetFirstNode (&Statement->OptionListHead);
    Option = QUESTION_OPTION_FROM_LINK (Link);
    RemoveEntryList (&Option->Link);

    gBS->FreePool (Option);
  }

  //
  // Free Inconsistent List
  //
  while (!IsListEmpty (&Statement->InconsistentListHead)) {
    Link = GetFirstNode (&Statement->InconsistentListHead);
    Expression = FORM_EXPRESSION_FROM_LINK (Link);
    RemoveEntryList (&Expression->Link);

    DestroyExpression (Expression);
  }

  //
  // Free NoSubmit List
  //
  while (!IsListEmpty (&Statement->NoSubmitListHead)) {
    Link = GetFirstNode (&Statement->NoSubmitListHead);
    Expression = FORM_EXPRESSION_FROM_LINK (Link);
    RemoveEntryList (&Expression->Link);

    DestroyExpression (Expression);
  }

  EfiLibSafeFreePool (Statement->VariableName);
  EfiLibSafeFreePool (Statement->BlockName);
  EfiLibSafeFreePool (Statement->BufferValue);
}

VOID
DestroyForm (
  IN OUT FORM_BROWSER_FORM  *Form
  )
/*++

Routine Description:
  Free resources of a Form

Arguments:
  Form - Pointer of the Form

Returns:
  None.

--*/
{
  EFI_LIST_ENTRY          *Link;
  FORM_EXPRESSION         *Expression;
  FORM_BROWSER_STATEMENT  *Statement;

  //
  // Free Form Expressions
  //
  while (!IsListEmpty (&Form->ExpressionListHead)) {
    Link = GetFirstNode (&Form->ExpressionListHead);
    Expression = FORM_EXPRESSION_FROM_LINK (Link);
    RemoveEntryList (&Expression->Link);

    DestroyExpression (Expression);
  }

  //
  // Free Statements/Questions
  //
  while (!IsListEmpty (&Form->StatementListHead)) {
    Link = GetFirstNode (&Form->StatementListHead);
    Statement = FORM_BROWSER_STATEMENT_FROM_LINK (Link);
    RemoveEntryList (&Statement->Link);

    DestroyStatement (Statement);
  }

  //
  // Free this Form
  //
  gBS->FreePool (Form);
}

VOID
DestroyFormSet (
  IN OUT FORM_BROWSER_FORMSET  *FormSet
  )
/*++

Routine Description:
  Free resources allocated for a FormSet

Arguments:
  FormSet - Pointer of the FormSet

Returns:
  None.

--*/
{
  EFI_LIST_ENTRY        *Link;
  FORMSET_STORAGE       *Storage;
  FORMSET_DEFAULTSTORE  *DefaultStore;
  FORM_BROWSER_FORM     *Form;

  //
  // Free IFR binary buffer
  //
  EfiLibSafeFreePool (FormSet->IfrBinaryData);

  //
  // Free FormSet Storage
  //
  if (FormSet->StorageListHead.ForwardLink != NULL) {
    while (!IsListEmpty (&FormSet->StorageListHead)) {
      Link = GetFirstNode (&FormSet->StorageListHead);
      Storage = FORMSET_STORAGE_FROM_LINK (Link);
      RemoveEntryList (&Storage->Link);

      DestroyStorage (Storage);
    }
  }

  //
  // Free FormSet Default Store
  //
  if (FormSet->DefaultStoreListHead.ForwardLink != NULL) {
    while (!IsListEmpty (&FormSet->DefaultStoreListHead)) {
      Link = GetFirstNode (&FormSet->DefaultStoreListHead);
      DefaultStore = FORMSET_DEFAULTSTORE_FROM_LINK (Link);
      RemoveEntryList (&DefaultStore->Link);

      gBS->FreePool (DefaultStore);
    }
  }

  //
  // Free Forms
  //
  if (FormSet->FormListHead.ForwardLink != NULL) {
    while (!IsListEmpty (&FormSet->FormListHead)) {
      Link = GetFirstNode (&FormSet->FormListHead);
      Form = FORM_BROWSER_FORM_FROM_LINK (Link);
      RemoveEntryList (&Form->Link);

      DestroyForm (Form);
    }
  }

  EfiLibSafeFreePool (FormSet->StatementBuffer);
  EfiLibSafeFreePool (FormSet->ExpressionBuffer);

  EfiLibSafeFreePool (FormSet);
}

BOOLEAN
IsExpressionOpCode (
  IN UINT8              Operand
  )
/*++

Routine Description:
  Tell whether this Operand is an Expression OpCode or not

Arguments:
  Operand   - Operand of an IFR OpCode.

Returns:
  TRUE      -  This is an Expression OpCode.
  FALSE     -  Not an Expression OpCode.

--*/
{
  if (((Operand >= EFI_IFR_EQ_ID_VAL_OP) && (Operand <= EFI_IFR_NOT_OP)) ||
      ((Operand >= EFI_IFR_MATCH_OP) && (Operand <= EFI_IFR_SPAN_OP)) ||
      (Operand == EFI_IFR_TO_LOWER_OP) ||
      (Operand == EFI_IFR_TO_UPPER_OP) ||
      (Operand == EFI_IFR_VERSION_OP)  ||
      (Operand == EFI_IFR_CATENATE_OP)
     ) {
    return TRUE;
  } else {
    return FALSE;
  }
}

BOOLEAN
IsClassGuidMatch (
  IN  FORM_BROWSER_FORMSET  *FormSet,
  IN  EFI_GUID              *ClassGuid
  )
/*++

Routine Description:
  Check whether a FormSet match with a given ClassGuid.

Arguments:
  FormSet      - The FormSet to be counted.
  ClassGuid    - The class GUID.

Returns:
  TRUE         - FormSet match with given ClassGuid.
  FALSE        - FormSet doesn't match with given ClassGuid.

--*/
{
  UINT8 Index;

  for (Index = 0; Index < FormSet->NumberOfClassGuid; Index++) {
    if (EfiCompareGuid (ClassGuid, &FormSet->ClassGuid[Index])) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
CountOpCodes (
  IN  FORM_BROWSER_FORMSET  *FormSet,
  IN OUT  UINT16            *NumberOfStatement,
  IN OUT  UINT16            *NumberOfExpression
  )
/*++

Routine Description:
  Calculate number of Statemens(Questions) and Expression OpCodes.

Arguments:
  FormSet            - The FormSet to be counted.
  NumberOfStatement  - Number of Statemens(Questions)
  NumberOfExpression - Number of Expression OpCodes

Returns:
  None.

--*/
{
  UINT16  StatementCount;
  UINT16  ExpressionCount;
  UINT8   *OpCodeData;
  UINTN   Offset;
  UINTN   OpCodeLen;

  Offset = 0;
  StatementCount = 0;
  ExpressionCount = 0;

  while (Offset < FormSet->IfrBinaryLength) {
    OpCodeData = FormSet->IfrBinaryData + Offset;
    OpCodeLen = ((EFI_IFR_OP_HEADER *) OpCodeData)->Length;
    Offset += OpCodeLen;

    if (IsExpressionOpCode (((EFI_IFR_OP_HEADER *) OpCodeData)->OpCode)) {
      ExpressionCount++;
    } else {
      StatementCount++;
    }
  }

  *NumberOfStatement = StatementCount;
  *NumberOfExpression = ExpressionCount;
}

EFI_STATUS
ParseOpCodes (
  IN FORM_BROWSER_FORMSET              *FormSet
  )
/*++

Routine Description:
  Parse opcodes in the formset IFR binary.

Arguments:

 FormSet - Pointer of the FormSet data structure.

Returns:

  EFI_SUCCESS - Opcode parse success.
  Other       - Opcode parse fail.

--*/
{
  EFI_STATUS              Status;
  UINT16                  Index;
  FORM_BROWSER_FORM       *CurrentForm;
  FORM_BROWSER_STATEMENT  *CurrentStatement;
  EXPRESSION_OPCODE       *ExpressionOpCode;
  FORM_EXPRESSION         *CurrentExpression;
  UINT8                   Operand;
  UINT8                   Scope;
  UINTN                   OpCodeOffset;
  UINTN                   OpCodeLength;
  UINT8                   *OpCodeData;
  UINT8                   ScopeOpCode;
  FORMSET_STORAGE         *Storage;
  FORMSET_DEFAULTSTORE    *DefaultStore;
  QUESTION_DEFAULT        *CurrentDefault;
  QUESTION_OPTION         *CurrentOption;
  UINT8                   Width;
  CHAR8                   *AsciiString;
  UINT16                  NumberOfStatement;
  UINT16                  NumberOfExpression;
  EFI_IMAGE_ID            *ImageId;
  BOOLEAN                 SuppressForOption;
  BOOLEAN                 InScopeOptionSuppress;
  FORM_EXPRESSION         *OptionSuppressExpression;
  UINT16                  DepthOfDisable;
  BOOLEAN                 OpCodeDisabled;
  BOOLEAN                 SingleOpCodeExpression;
  BOOLEAN                 InScopeDefault;
  EFI_HII_VALUE           *Value;

  mInScopeSubtitle         = FALSE;
  SuppressForOption        = FALSE;
  mInScopeSuppress         = FALSE;
  InScopeOptionSuppress    = FALSE;
  mInScopeGrayOut          = FALSE;
  mInScopeDisable           = FALSE;
  DepthOfDisable           = 0;
  OpCodeDisabled           = FALSE;
  SingleOpCodeExpression   = FALSE;
  InScopeDefault           = FALSE;
  CurrentExpression        = NULL;
  CurrentDefault           = NULL;
  CurrentOption            = NULL;
  OptionSuppressExpression = NULL;

  //
  // Get the number of Statements and Expressions
  //
  CountOpCodes (FormSet, &NumberOfStatement, &NumberOfExpression);

  mStatementIndex = 0;
  FormSet->StatementBuffer = EfiLibAllocateZeroPool (NumberOfStatement * sizeof (FORM_BROWSER_STATEMENT));
  if (FormSet->StatementBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mExpressionOpCodeIndex = 0;
  FormSet->ExpressionBuffer = EfiLibAllocateZeroPool (NumberOfExpression * sizeof (EXPRESSION_OPCODE));
  if (FormSet->ExpressionBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  InitializeListHead (&FormSet->StorageListHead);
  InitializeListHead (&FormSet->DefaultStoreListHead);
  InitializeListHead (&FormSet->FormListHead);

  CurrentForm = NULL;
  CurrentStatement = NULL;

  ResetScopeStack ();

  OpCodeOffset = 0;
  while (OpCodeOffset < FormSet->IfrBinaryLength) {
    OpCodeData = FormSet->IfrBinaryData + OpCodeOffset;

    OpCodeLength = ((EFI_IFR_OP_HEADER *) OpCodeData)->Length;
    OpCodeOffset += OpCodeLength;
    Operand = ((EFI_IFR_OP_HEADER *) OpCodeData)->OpCode;
    Scope = ((EFI_IFR_OP_HEADER *) OpCodeData)->Scope;

    //
    // If scope bit set, push onto scope stack
    //
    if (Scope) {
      PushScope (Operand);
    }

    if (OpCodeDisabled) {
      //
      // DisableIf Expression is evaluated to be TRUE, try to find its end.
      // Here only cares the EFI_IFR_DISABLE_IF and EFI_IFR_END
      //
      if (Operand == EFI_IFR_DISABLE_IF_OP) {
        DepthOfDisable++;
      } else if (Operand == EFI_IFR_END_OP) {
        Status = PopScope (&ScopeOpCode);
        if (EFI_ERROR (Status)) {
          return Status;
        }

        if (ScopeOpCode == EFI_IFR_DISABLE_IF_OP) {
          if (DepthOfDisable == 0) {
            mInScopeDisable = FALSE;
            OpCodeDisabled = FALSE;
          } else {
            DepthOfDisable--;
          }
        }
      }
      continue;
    }

    if (IsExpressionOpCode (Operand)) {
      ExpressionOpCode = &FormSet->ExpressionBuffer[mExpressionOpCodeIndex];
      mExpressionOpCodeIndex++;

      ExpressionOpCode->Signature = EXPRESSION_OPCODE_SIGNATURE;
      ExpressionOpCode->Operand = Operand;
      Value = &ExpressionOpCode->Value;

      switch (Operand) {
      case EFI_IFR_EQ_ID_VAL_OP:
        EfiCopyMem (&ExpressionOpCode->QuestionId, &((EFI_IFR_EQ_ID_VAL *) OpCodeData)->QuestionId, sizeof (EFI_QUESTION_ID));

        Value->Type = EFI_IFR_TYPE_NUM_SIZE_16;
        EfiCopyMem (&Value->Value.u16, &((EFI_IFR_EQ_ID_VAL *) OpCodeData)->Value, sizeof (UINT16));
        break;

      case EFI_IFR_EQ_ID_ID_OP:
        EfiCopyMem (&ExpressionOpCode->QuestionId, &((EFI_IFR_EQ_ID_ID *) OpCodeData)->QuestionId1, sizeof (EFI_QUESTION_ID));
        EfiCopyMem (&ExpressionOpCode->QuestionId2, &((EFI_IFR_EQ_ID_ID *) OpCodeData)->QuestionId2, sizeof (EFI_QUESTION_ID));
        break;

      case EFI_IFR_EQ_ID_LIST_OP:
        EfiCopyMem (&ExpressionOpCode->QuestionId, &((EFI_IFR_EQ_ID_LIST *) OpCodeData)->QuestionId, sizeof (EFI_QUESTION_ID));
        EfiCopyMem (&ExpressionOpCode->ListLength, &((EFI_IFR_EQ_ID_LIST *) OpCodeData)->ListLength, sizeof (UINT16));
        ExpressionOpCode->ValueList = EfiLibAllocateCopyPool (ExpressionOpCode->ListLength * sizeof (UINT16), &((EFI_IFR_EQ_ID_LIST *) OpCodeData)->ValueList);
        break;

      case EFI_IFR_TO_STRING_OP:
      case EFI_IFR_FIND_OP:
        ExpressionOpCode->Format = (( EFI_IFR_TO_STRING *) OpCodeData)->Format;
        break;

      case EFI_IFR_STRING_REF1_OP:
        Value->Type = EFI_IFR_TYPE_STRING;
        EfiCopyMem (&Value->Value.string, &(( EFI_IFR_STRING_REF1 *) OpCodeData)->StringId, sizeof (EFI_STRING_ID));
        break;

      case EFI_IFR_RULE_REF_OP:
        ExpressionOpCode->RuleId = (( EFI_IFR_RULE_REF *) OpCodeData)->RuleId;
        break;

      case EFI_IFR_SPAN_OP:
        ExpressionOpCode->Flags = (( EFI_IFR_SPAN *) OpCodeData)->Flags;
        break;

      case EFI_IFR_THIS_OP:
        ExpressionOpCode->QuestionId = CurrentStatement->QuestionId;
        break;

      case EFI_IFR_QUESTION_REF1_OP:
        EfiCopyMem (&ExpressionOpCode->QuestionId, &((EFI_IFR_EQ_ID_LIST *) OpCodeData)->QuestionId, sizeof (EFI_QUESTION_ID));
        break;

      case EFI_IFR_QUESTION_REF3_OP:
        if (OpCodeLength >= sizeof (EFI_IFR_QUESTION_REF3_2)) {
          EfiCopyMem (&ExpressionOpCode->DevicePath, &(( EFI_IFR_QUESTION_REF3_2 *) OpCodeData)->DevicePath, sizeof (EFI_STRING_ID));

          if (OpCodeLength >= sizeof (EFI_IFR_QUESTION_REF3_3)) {
            EfiCopyMem (&ExpressionOpCode->Guid, &(( EFI_IFR_QUESTION_REF3_3 *) OpCodeData)->Guid, sizeof (EFI_GUID));
          }
        }
        break;

      //
      // constant
      //
      case EFI_IFR_TRUE_OP:
        Value->Type = EFI_IFR_TYPE_BOOLEAN;
        Value->Value.b = TRUE;
        break;

      case EFI_IFR_FALSE_OP:
        Value->Type = EFI_IFR_TYPE_BOOLEAN;
        Value->Value.b = FALSE;
        break;

      case EFI_IFR_ONE_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_8;
        Value->Value.u8 = 1;
        break;

      case EFI_IFR_ZERO_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_8;
        Value->Value.u8 = 0;
        break;

      case EFI_IFR_ONES_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_64;
        Value->Value.u64 = 0xffffffffffffffff;
        break;

      case EFI_IFR_UINT8_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_8;
        Value->Value.u8 = (( EFI_IFR_UINT8 *) OpCodeData)->Value;
        break;

      case EFI_IFR_UINT16_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_16;
        EfiCopyMem (&Value->Value.u16, &(( EFI_IFR_UINT16 *) OpCodeData)->Value, sizeof (UINT16));
        break;

      case EFI_IFR_UINT32_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_32;
        EfiCopyMem (&Value->Value.u32, &(( EFI_IFR_UINT32 *) OpCodeData)->Value, sizeof (UINT32));
        break;

      case EFI_IFR_UINT64_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_64;
        EfiCopyMem (&Value->Value.u64, &(( EFI_IFR_UINT64 *) OpCodeData)->Value, sizeof (UINT64));
        break;

      case EFI_IFR_UNDEFINED_OP:
        Value->Type = EFI_IFR_TYPE_OTHER;
        break;

      case EFI_IFR_VERSION_OP:
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_16;
        Value->Value.u16 = EFI_IFR_SPECIFICATION_VERSION;
        break;

      default:
        break;
      }

      ASSERT (CurrentExpression != NULL);
      InsertTailList (&CurrentExpression->OpCodeListHead, &ExpressionOpCode->Link);

      if (SingleOpCodeExpression) {
        //
        // There are two cases to indicate the end of an Expression:
        // for single OpCode expression: one Expression OpCode
        // for expression consists of more than one OpCode: EFI_IFR_END
        //
        SingleOpCodeExpression = FALSE;

        if (mInScopeDisable && CurrentForm == NULL) {
          //
          // This is DisableIf expression for Form, it should be a constant expression
          //
          Status = EvaluateExpression (FormSet, CurrentForm, CurrentExpression);
          if (EFI_ERROR (Status)) {
            return Status;
          }
          if (CurrentExpression->Result.Type != EFI_IFR_TYPE_BOOLEAN) {
            return EFI_INVALID_PARAMETER;
          }

          OpCodeDisabled = CurrentExpression->Result.Value.b;
        }

        CurrentExpression = NULL;
      }

      continue;
    }

    //
    // Parse the Opcode
    //
    switch (Operand) {

    case EFI_IFR_FORM_SET_OP:
      //
      // Check the formset GUID
      //
      if (EfiCompareMem (&FormSet->Guid, &((EFI_IFR_FORM_SET *) OpCodeData)->Guid, sizeof (EFI_GUID)) != 0) {
        return EFI_INVALID_PARAMETER;
      }

      EfiCopyMem (&FormSet->FormSetTitle, &((EFI_IFR_FORM_SET *) OpCodeData)->FormSetTitle, sizeof (EFI_STRING_ID));
      EfiCopyMem (&FormSet->Help,         &((EFI_IFR_FORM_SET *) OpCodeData)->Help,         sizeof (EFI_STRING_ID));
      if (OpCodeLength > ((UINTN) &((EFI_IFR_FORM_SET *) 0)->Flags)) {
        //
        // This is the new version of formset OpCode
        //
        FormSet->NumberOfClassGuid = ((EFI_IFR_FORM_SET *) OpCodeData)->Flags & 0x3;
        EfiCopyMem (FormSet->ClassGuid, ((EFI_IFR_FORM_SET *) OpCodeData)->ClassGuid, FormSet->NumberOfClassGuid * sizeof (EFI_GUID));
      }
      break;

    case EFI_IFR_FORM_OP:
      //
      // Create a new Form for this FormSet
      //
      CurrentForm = EfiLibAllocateZeroPool (sizeof (FORM_BROWSER_FORM));
      CurrentForm->Signature = FORM_BROWSER_FORM_SIGNATURE;
      InitializeListHead (&CurrentForm->ExpressionListHead);
      InitializeListHead (&CurrentForm->StatementListHead);

      EfiCopyMem (&CurrentForm->FormId,    &((EFI_IFR_FORM *) OpCodeData)->FormId,    sizeof (UINT16));
      EfiCopyMem (&CurrentForm->FormTitle, &((EFI_IFR_FORM *) OpCodeData)->FormTitle, sizeof (EFI_STRING_ID));

      //
      // Insert into Form list of this FormSet
      //
      InsertTailList (&FormSet->FormListHead, &CurrentForm->Link);
      break;

    //
    // Storage
    //
    case EFI_IFR_VARSTORE_OP:
      //
      // Create a buffer Storage for this FormSet
      //
      Storage = CreateStorage (FormSet);
      Storage->Type = EFI_HII_VARSTORE_BUFFER;

      EfiCopyMem (&Storage->VarStoreId, &((EFI_IFR_VARSTORE *) OpCodeData)->VarStoreId, sizeof (EFI_VARSTORE_ID));
      EfiCopyMem (&Storage->Guid,       &((EFI_IFR_VARSTORE *) OpCodeData)->Guid,       sizeof (EFI_GUID));
      EfiCopyMem (&Storage->Size,       &((EFI_IFR_VARSTORE *) OpCodeData)->Size,       sizeof (UINT16));

      Storage->Buffer = EfiLibAllocateZeroPool (Storage->Size);
      Storage->EditBuffer = EfiLibAllocateZeroPool (Storage->Size);

      AsciiString = (CHAR8 *) ((EFI_IFR_VARSTORE *) OpCodeData)->Name;
      Storage->Name = EfiLibAllocateZeroPool (EfiAsciiStrSize (AsciiString) * 2);
      ASSERT (Storage->Name != NULL);
      for (Index = 0; AsciiString[Index] != 0; Index++) {
        Storage->Name[Index] = (CHAR16) AsciiString[Index];
      }

      //
      // Initialize <ConfigHdr>
      //
      InitializeConfigHdr (FormSet, Storage);
      break;

    case EFI_IFR_VARSTORE_NAME_VALUE_OP:
      //
      // Create a name/value Storage for this FormSet
      //
      Storage = CreateStorage (FormSet);
      Storage->Type = EFI_HII_VARSTORE_NAME_VALUE;

      EfiCopyMem (&Storage->VarStoreId, &((EFI_IFR_VARSTORE_NAME_VALUE *) OpCodeData)->VarStoreId, sizeof (EFI_VARSTORE_ID));
      EfiCopyMem (&Storage->Guid,       &((EFI_IFR_VARSTORE_NAME_VALUE *) OpCodeData)->Guid,       sizeof (EFI_GUID));

      //
      // Initialize <ConfigHdr>
      //
      InitializeConfigHdr (FormSet, Storage);
      break;

    case EFI_IFR_VARSTORE_EFI_OP:
      //
      // Create a EFI variable Storage for this FormSet
      //
      Storage = CreateStorage (FormSet);
      Storage->Type = EFI_HII_VARSTORE_EFI_VARIABLE;

      EfiCopyMem (&Storage->VarStoreId, &((EFI_IFR_VARSTORE_EFI *) OpCodeData)->VarStoreId, sizeof (EFI_VARSTORE_ID));
      EfiCopyMem (&Storage->Guid,       &((EFI_IFR_VARSTORE_EFI *) OpCodeData)->Guid,       sizeof (EFI_GUID));
      EfiCopyMem (&Storage->Attributes, &((EFI_IFR_VARSTORE_EFI *) OpCodeData)->Attributes, sizeof (UINT32));
      break;

    //
    // DefaultStore
    //
    case EFI_IFR_DEFAULTSTORE_OP:
      DefaultStore = EfiLibAllocateZeroPool (sizeof (FORMSET_DEFAULTSTORE));
      DefaultStore->Signature = FORMSET_DEFAULTSTORE_SIGNATURE;

      EfiCopyMem (&DefaultStore->DefaultId,   &((EFI_IFR_DEFAULTSTORE *) OpCodeData)->DefaultId,   sizeof (UINT16));
      EfiCopyMem (&DefaultStore->DefaultName, &((EFI_IFR_DEFAULTSTORE *) OpCodeData)->DefaultName, sizeof (EFI_STRING_ID));

      //
      // Insert to DefaultStore list of this Formset
      //
      InsertTailList (&FormSet->DefaultStoreListHead, &DefaultStore->Link);
      break;

    //
    // Statements
    //
    case EFI_IFR_SUBTITLE_OP:
      CurrentStatement = CreateStatement (OpCodeData, FormSet, CurrentForm);
      CurrentStatement->Flags = ((EFI_IFR_SUBTITLE *) OpCodeData)->Flags;

      if (Scope) {
        mInScopeSubtitle = TRUE;
      }
      break;

    case EFI_IFR_TEXT_OP:
      CurrentStatement = CreateStatement (OpCodeData, FormSet, CurrentForm);

      EfiCopyMem (&CurrentStatement->TextTwo, &((EFI_IFR_TEXT *) OpCodeData)->TextTwo, sizeof (EFI_STRING_ID));
      break;

    case EFI_IFR_RESET_BUTTON_OP:
      CurrentStatement = CreateStatement (OpCodeData, FormSet, CurrentForm);

      EfiCopyMem (&CurrentStatement->DefaultId, &((EFI_IFR_RESET_BUTTON *) OpCodeData)->DefaultId, sizeof (EFI_DEFAULT_ID));
      break;

    //
    // Questions
    //
    case EFI_IFR_ACTION_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      if (OpCodeLength == sizeof (EFI_IFR_ACTION_1)) {
        //
        // No QuestionConfig present, so no configuration string will be processed
        //
        CurrentStatement->QuestionConfig = 0;
      } else {
        EfiCopyMem (&CurrentStatement->QuestionConfig, &((EFI_IFR_ACTION *) OpCodeData)->QuestionConfig, sizeof (EFI_STRING_ID));
      }
      break;

    case EFI_IFR_REF_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      EfiCopyMem (&CurrentStatement->RefFormId, &((EFI_IFR_REF *) OpCodeData)->FormId, sizeof (EFI_FORM_ID));
      if (OpCodeLength >= sizeof (EFI_IFR_REF2)) {
        EfiCopyMem (&CurrentStatement->RefQuestionId, &((EFI_IFR_REF2 *) OpCodeData)->QuestionId, sizeof (EFI_QUESTION_ID));

        if (OpCodeLength >= sizeof (EFI_IFR_REF3)) {
          EfiCopyMem (&CurrentStatement->RefFormSetId, &((EFI_IFR_REF3 *) OpCodeData)->FormSetId, sizeof (EFI_GUID));

          if (OpCodeLength >= sizeof (EFI_IFR_REF4)) {
            EfiCopyMem (&CurrentStatement->RefDevicePath, &((EFI_IFR_REF4 *) OpCodeData)->DevicePath, sizeof (EFI_STRING_ID));
          }
        }
      }
      break;

    case EFI_IFR_ONE_OF_OP:
    case EFI_IFR_NUMERIC_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      CurrentStatement->Flags = ((EFI_IFR_ONE_OF *) OpCodeData)->Flags;
      Value = &CurrentStatement->HiiValue;

      switch (CurrentStatement->Flags & EFI_IFR_NUMERIC_SIZE) {
      case EFI_IFR_NUMERIC_SIZE_1:
        CurrentStatement->Minimum = ((EFI_IFR_NUMERIC *) OpCodeData)->data.u8.MinValue;
        CurrentStatement->Maximum = ((EFI_IFR_NUMERIC *) OpCodeData)->data.u8.MaxValue;
        CurrentStatement->Step    = ((EFI_IFR_NUMERIC *) OpCodeData)->data.u8.Step;
        CurrentStatement->StorageWidth = sizeof (UINT8);
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_8;
        break;

      case EFI_IFR_NUMERIC_SIZE_2:
        EfiCopyMem (&CurrentStatement->Minimum, &((EFI_IFR_NUMERIC *) OpCodeData)->data.u16.MinValue, sizeof (UINT16));
        EfiCopyMem (&CurrentStatement->Maximum, &((EFI_IFR_NUMERIC *) OpCodeData)->data.u16.MaxValue, sizeof (UINT16));
        EfiCopyMem (&CurrentStatement->Step,    &((EFI_IFR_NUMERIC *) OpCodeData)->data.u16.Step,     sizeof (UINT16));
        CurrentStatement->StorageWidth = sizeof (UINT16);
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_16;
        break;

      case EFI_IFR_NUMERIC_SIZE_4:
        EfiCopyMem (&CurrentStatement->Minimum, &((EFI_IFR_NUMERIC *) OpCodeData)->data.u32.MinValue, sizeof (UINT32));
        EfiCopyMem (&CurrentStatement->Maximum, &((EFI_IFR_NUMERIC *) OpCodeData)->data.u32.MaxValue, sizeof (UINT32));
        EfiCopyMem (&CurrentStatement->Step,    &((EFI_IFR_NUMERIC *) OpCodeData)->data.u32.Step,     sizeof (UINT32));
        CurrentStatement->StorageWidth = sizeof (UINT32);
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_32;
        break;

      case EFI_IFR_NUMERIC_SIZE_8:
        EfiCopyMem (&CurrentStatement->Minimum, &((EFI_IFR_NUMERIC *) OpCodeData)->data.u64.MinValue, sizeof (UINT64));
        EfiCopyMem (&CurrentStatement->Maximum, &((EFI_IFR_NUMERIC *) OpCodeData)->data.u64.MaxValue, sizeof (UINT64));
        EfiCopyMem (&CurrentStatement->Step,    &((EFI_IFR_NUMERIC *) OpCodeData)->data.u64.Step,     sizeof (UINT64));
        CurrentStatement->StorageWidth = sizeof (UINT64);
        Value->Type = EFI_IFR_TYPE_NUM_SIZE_64;
        break;

      default:
        break;
      }

      InitializeRequestElement (FormSet, CurrentStatement);

      if ((Operand == EFI_IFR_ONE_OF_OP) && Scope) {
        SuppressForOption = TRUE;
      }
      break;

    case EFI_IFR_ORDERED_LIST_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      CurrentStatement->Flags = ((EFI_IFR_ORDERED_LIST *) OpCodeData)->Flags;
      CurrentStatement->MaxContainers = ((EFI_IFR_ORDERED_LIST *) OpCodeData)->MaxContainers;

      //
      // No buffer type is defined in EFI_IFR_TYPE_VALUE, so a Configuration Driver
      // has to use FormBrowser2.Callback() to retrieve the uncommited data for
      // an interactive orderedlist (i.e. with EFI_IFR_FLAG_CALLBACK flag set).
      //
      CurrentStatement->HiiValue.Type = EFI_IFR_TYPE_OTHER;
      CurrentStatement->BufferValue = NULL;

      if (Scope) {
        SuppressForOption = TRUE;
      }
      break;

    case EFI_IFR_CHECKBOX_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      CurrentStatement->Flags = ((EFI_IFR_CHECKBOX *) OpCodeData)->Flags;
      CurrentStatement->StorageWidth = sizeof (BOOLEAN);
      CurrentStatement->HiiValue.Type = EFI_IFR_TYPE_BOOLEAN;

      InitializeRequestElement (FormSet, CurrentStatement);
      break;

    case EFI_IFR_STRING_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      //
      // MinSize is the minimum number of characters that can be accepted for this opcode,
      // MaxSize is the maximum number of characters that can be accepted for this opcode.
      // The characters are stored as Unicode, so the storage width should multiply 2.
      //
      CurrentStatement->Minimum = ((EFI_IFR_STRING *) OpCodeData)->MinSize;
      CurrentStatement->Maximum = ((EFI_IFR_STRING *) OpCodeData)->MaxSize;
      CurrentStatement->StorageWidth = ((UINT16) CurrentStatement->Maximum) * sizeof (CHAR16);
      CurrentStatement->Flags = ((EFI_IFR_STRING *) OpCodeData)->Flags;

      CurrentStatement->HiiValue.Type = EFI_IFR_TYPE_STRING;
      CurrentStatement->BufferValue = EfiLibAllocateZeroPool (CurrentStatement->StorageWidth + sizeof (CHAR16));

      InitializeRequestElement (FormSet, CurrentStatement);
      break;

    case EFI_IFR_PASSWORD_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      //
      // MinSize is the minimum number of characters that can be accepted for this opcode,
      // MaxSize is the maximum number of characters that can be accepted for this opcode.
      // The characters are stored as Unicode, so the storage width should multiply 2.
      //
      EfiCopyMem (&CurrentStatement->Minimum, &((EFI_IFR_PASSWORD *) OpCodeData)->MinSize, sizeof (UINT16));
      EfiCopyMem (&CurrentStatement->Maximum, &((EFI_IFR_PASSWORD *) OpCodeData)->MaxSize, sizeof (UINT16));
      CurrentStatement->StorageWidth = ((UINT16) CurrentStatement->Maximum) * sizeof (CHAR16);

      CurrentStatement->HiiValue.Type = EFI_IFR_TYPE_STRING;
      CurrentStatement->BufferValue = EfiLibAllocateZeroPool (CurrentStatement->StorageWidth + sizeof (CHAR16));

      InitializeRequestElement (FormSet, CurrentStatement);
      break;

    case EFI_IFR_DATE_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      CurrentStatement->Flags = ((EFI_IFR_DATE *) OpCodeData)->Flags;
      CurrentStatement->HiiValue.Type = EFI_IFR_TYPE_DATE;

      if ((CurrentStatement->Flags & EFI_QF_DATE_STORAGE) == QF_DATE_STORAGE_NORMAL) {
        CurrentStatement->StorageWidth = sizeof (EFI_HII_DATE);

        InitializeRequestElement (FormSet, CurrentStatement);
      } else {
        //
        // Don't assign storage for RTC type of date/time
        //
        CurrentStatement->Storage = NULL;
        CurrentStatement->StorageWidth = 0;
      }
      break;

    case EFI_IFR_TIME_OP:
      CurrentStatement = CreateQuestion (OpCodeData, FormSet, CurrentForm);

      CurrentStatement->Flags = ((EFI_IFR_TIME *) OpCodeData)->Flags;
      CurrentStatement->HiiValue.Type = EFI_IFR_TYPE_TIME;

      if ((CurrentStatement->Flags & QF_TIME_STORAGE) == QF_TIME_STORAGE_NORMAL) {
        CurrentStatement->StorageWidth = sizeof (EFI_IFR_TIME);

        InitializeRequestElement (FormSet, CurrentStatement);
      } else {
        //
        // Don't assign storage for RTC type of date/time
        //
        CurrentStatement->Storage = NULL;
        CurrentStatement->StorageWidth = 0;
      }
      break;

    //
    // Default
    //
    case EFI_IFR_DEFAULT_OP:
      //
      // EFI_IFR_DEFAULT appear in scope of a Question,
      // It creates a default value for the current question.
      // A Question may have more than one Default value which have different default types.
      //
      CurrentDefault = EfiLibAllocateZeroPool (sizeof (QUESTION_DEFAULT));
      CurrentDefault->Signature = QUESTION_DEFAULT_SIGNATURE;

      CurrentDefault->Value.Type = ((EFI_IFR_DEFAULT *) OpCodeData)->Type;
      EfiCopyMem (&CurrentDefault->DefaultId, &((EFI_IFR_DEFAULT *) OpCodeData)->DefaultId, sizeof (UINT16));
      EfiCopyMem (&CurrentDefault->Value.Value, &((EFI_IFR_DEFAULT *) OpCodeData)->Value, sizeof (EFI_IFR_TYPE_VALUE));
      ExtendValueToU64 (&CurrentDefault->Value);

      //
      // Insert to Default Value list of current Question
      //
      InsertTailList (&CurrentStatement->DefaultListHead, &CurrentDefault->Link);

      if (Scope) {
        InScopeDefault = TRUE;
      }
      break;

    //
    // Option
    //
    case EFI_IFR_ONE_OF_OPTION_OP:
      //
      // EFI_IFR_ONE_OF_OPTION appear in scope of a Question.
      // It create a selection for use in current Question.
      //
      CurrentOption = EfiLibAllocateZeroPool (sizeof (QUESTION_OPTION));
      CurrentOption->Signature = QUESTION_OPTION_SIGNATURE;

      CurrentOption->Flags = ((EFI_IFR_ONE_OF_OPTION *) OpCodeData)->Flags;
      CurrentOption->Value.Type = ((EFI_IFR_ONE_OF_OPTION *) OpCodeData)->Type;
      EfiCopyMem (&CurrentOption->Text, &((EFI_IFR_ONE_OF_OPTION *) OpCodeData)->Option, sizeof (EFI_STRING_ID));
      EfiCopyMem (&CurrentOption->Value.Value, &((EFI_IFR_ONE_OF_OPTION *) OpCodeData)->Value, sizeof (EFI_IFR_TYPE_VALUE));
      ExtendValueToU64 (&CurrentOption->Value);

      if (InScopeOptionSuppress) {
        CurrentOption->SuppressExpression = OptionSuppressExpression;
      }

      //
      // Insert to Option list of current Question
      //
      InsertTailList (&CurrentStatement->OptionListHead, &CurrentOption->Link);

      //
      // Now we know the Storage width of nested Ordered List
      //
      if ((CurrentStatement->Operand == EFI_IFR_ORDERED_LIST_OP) && (CurrentStatement->BufferValue == NULL)) {
        Width = 1;
        switch (CurrentOption->Value.Type) {
        case EFI_IFR_TYPE_NUM_SIZE_8:
          Width = 1;
          break;

        case EFI_IFR_TYPE_NUM_SIZE_16:
          Width = 2;
          break;

        case EFI_IFR_TYPE_NUM_SIZE_32:
          Width = 4;
          break;

        case EFI_IFR_TYPE_NUM_SIZE_64:
          Width = 8;
          break;

        default:
          //
          // Invalid type for Ordered List
          //
          break;
        }

        CurrentStatement->StorageWidth = CurrentStatement->MaxContainers * Width;
        CurrentStatement->BufferValue = EfiLibAllocateZeroPool (CurrentStatement->StorageWidth);
        CurrentStatement->ValueType = CurrentOption->Value.Type;

        InitializeRequestElement (FormSet, CurrentStatement);
      }
      break;

    //
    // Conditional
    //
    case EFI_IFR_NO_SUBMIT_IF_OP:
    case EFI_IFR_INCONSISTENT_IF_OP:
      //
      // Create an Expression node
      //
      CurrentExpression = CreateExpression (CurrentForm);
      EfiCopyMem (&CurrentExpression->Error, &((EFI_IFR_INCONSISTENT_IF *) OpCodeData)->Error, sizeof (EFI_STRING_ID));

      if (Operand == EFI_IFR_NO_SUBMIT_IF_OP) {
        CurrentExpression->Type = EFI_HII_EXPRESSION_NO_SUBMIT_IF;
        InsertTailList (&CurrentStatement->NoSubmitListHead, &CurrentExpression->Link);
      } else {
        CurrentExpression->Type = EFI_HII_EXPRESSION_INCONSISTENT_IF;
        InsertTailList (&CurrentStatement->InconsistentListHead, &CurrentExpression->Link);
      }
      break;

    case EFI_IFR_SUPPRESS_IF_OP:
      //
      // Question and Option will appear in scope of this OpCode
      //
      CurrentExpression = CreateExpression (CurrentForm);
      CurrentExpression->Type = EFI_HII_EXPRESSION_SUPPRESS_IF;
      InsertTailList (&CurrentForm->ExpressionListHead, &CurrentExpression->Link);

      if (SuppressForOption) {
        InScopeOptionSuppress = TRUE;
        OptionSuppressExpression = CurrentExpression;
      } else {
        mInScopeSuppress = TRUE;
        mSuppressExpression = CurrentExpression;
      }
      break;

    case EFI_IFR_GRAY_OUT_IF_OP:
      //
      // Questions will appear in scope of this OpCode
      //
      CurrentExpression = CreateExpression (CurrentForm);
      CurrentExpression->Type = EFI_HII_EXPRESSION_GRAY_OUT_IF;
      InsertTailList (&CurrentForm->ExpressionListHead, &CurrentExpression->Link);

      mInScopeGrayOut = TRUE;
      mGrayOutExpression = CurrentExpression;
      break;

    case EFI_IFR_DISABLE_IF_OP:
      //
      // The DisableIf expression should only rely on constant, so it could be
      // evaluated at initialization and it will not be queued
      //
      CurrentExpression = EfiLibAllocateZeroPool (sizeof (FORM_EXPRESSION));
      CurrentExpression->Signature = FORM_EXPRESSION_SIGNATURE;
      CurrentExpression->Type = EFI_HII_EXPRESSION_DISABLE_IF;
      InitializeListHead (&CurrentExpression->OpCodeListHead);

      if (CurrentForm != NULL) {
        //
        // This is DisableIf for Question, enqueue it to Form expression list
        //
        InsertTailList (&CurrentForm->ExpressionListHead, &CurrentExpression->Link);
      }

      mInScopeDisable = TRUE;
      OpCodeDisabled = FALSE;
      mDisableExpression = CurrentExpression;

      //
      // Take a look at next OpCode to see whether current expression consists
      // of single OpCode
      //
      if (((EFI_IFR_OP_HEADER *) (OpCodeData + OpCodeLength))->Scope == 0) {
        SingleOpCodeExpression = TRUE;
      }
      break;

    //
    // Expression
    //
    case EFI_IFR_VALUE_OP:
      CurrentExpression = CreateExpression (CurrentForm);
      CurrentExpression->Type = EFI_HII_EXPRESSION_VALUE;
      InsertTailList (&CurrentForm->ExpressionListHead, &CurrentExpression->Link);

      if (InScopeDefault) {
        //
        // Used for default (EFI_IFR_DEFAULT)
        //
        CurrentDefault->ValueExpression = CurrentExpression;
      } else {
        //
        // If used for a question, then the question will be read-only
        //
        CurrentStatement->ValueExpression = CurrentExpression;
      }
      break;

    case EFI_IFR_RULE_OP:
      CurrentExpression = CreateExpression (CurrentForm);
      CurrentExpression->Type = EFI_HII_EXPRESSION_RULE;

      CurrentExpression->RuleId = ((EFI_IFR_RULE *) OpCodeData)->RuleId;
      InsertTailList (&CurrentForm->ExpressionListHead, &CurrentExpression->Link);
      break;

    //
    // Image
    //
    case EFI_IFR_IMAGE_OP:
      //
      // Get ScopeOpcode from top of stack
      //
      PopScope (&ScopeOpCode);
      PushScope (ScopeOpCode);

      switch (ScopeOpCode) {
      case EFI_IFR_FORM_SET_OP:
        ImageId = &FormSet->ImageId;
        break;

      case EFI_IFR_FORM_OP:
        ImageId = &CurrentForm->ImageId;
        break;

      case EFI_IFR_ONE_OF_OPTION_OP:
        ImageId = &CurrentOption->ImageId;
        break;

      default:
        ImageId = &CurrentStatement->ImageId;
        break;
      }

      EfiCopyMem (ImageId, &((EFI_IFR_IMAGE *) OpCodeData)->Id, sizeof (EFI_IMAGE_ID));
      break;

    //
    // Refresh
    //
    case EFI_IFR_REFRESH_OP:
      CurrentStatement->RefreshInterval = ((EFI_IFR_REFRESH *) OpCodeData)->RefreshInterval;
      break;

    //
    // Vendor specific
    //
    case EFI_IFR_GUID_OP:
      if (EfiCompareGuid (&((EFI_IFR_GUID_BANNER *) OpCodeData)->Guid, &gTianoHiiIfrGuid)) {
        //
        // Tiano specific GUIDed opcodes
        //
        switch (((EFI_IFR_GUID_LABEL *) OpCodeData)->ExtendOpCode) {
        case EFI_IFR_EXTEND_OP_LABEL:
          //
          // just ignore label
          //
          break;

        case EFI_IFR_EXTEND_OP_BANNER:
          if (IsClassGuidMatch (FormSet, &gFrontPageClassGuid)) {
            EfiCopyMem (
              &gBannerData->Banner[((EFI_IFR_GUID_BANNER *) OpCodeData)->LineNumber][
              ((EFI_IFR_GUID_BANNER *) OpCodeData)->Alignment],
              &((EFI_IFR_GUID_BANNER *) OpCodeData)->Title,
              sizeof (EFI_STRING_ID)
              );
            break;
          }

#ifdef TIANO_EXTENDED_CLASS_SUBCLASS_SUPPORT
          if (FormSet->SubClass == EFI_FRONT_PAGE_SUBCLASS) {
            EfiCopyMem (
              &gBannerData->Banner[((EFI_IFR_GUID_BANNER *) OpCodeData)->LineNumber][
              ((EFI_IFR_GUID_BANNER *) OpCodeData)->Alignment],
              &((EFI_IFR_GUID_BANNER *) OpCodeData)->Title,
              sizeof (EFI_STRING_ID)
              );
          }
#endif
          break;

#ifdef TIANO_EXTENDED_CLASS_SUBCLASS_SUPPORT
        case EFI_IFR_EXTEND_OP_CLASS:
          EfiCopyMem (&FormSet->Class, &((EFI_IFR_GUID_CLASS *) OpCodeData)->Class, sizeof (UINT16));
          break;

        case EFI_IFR_EXTEND_OP_SUBCLASS:
          EfiCopyMem (&FormSet->SubClass, &((EFI_IFR_GUID_SUBCLASS *) OpCodeData)->SubClass, sizeof (UINT16));
          break;
#endif

        default:
          break;
        }
      }
      break;

    //
    // Scope End
    //
    case EFI_IFR_END_OP:
      Status = PopScope (&ScopeOpCode);
      if (EFI_ERROR (Status)) {
        ResetScopeStack ();
        return Status;
      }

      switch (ScopeOpCode) {
      case EFI_IFR_FORM_SET_OP:
        //
        // End of FormSet, update FormSet IFR binary length
        // to stop parsing substantial OpCodes
        //
        FormSet->IfrBinaryLength = OpCodeOffset;
        break;

      case EFI_IFR_FORM_OP:
        //
        // End of Form
        //
        CurrentForm = NULL;
        break;

      case EFI_IFR_ONE_OF_OPTION_OP:
        //
        // End of Option
        //
        CurrentOption = NULL;
        break;

      case EFI_IFR_SUBTITLE_OP:
        mInScopeSubtitle = FALSE;
        break;

      case EFI_IFR_NO_SUBMIT_IF_OP:
      case EFI_IFR_INCONSISTENT_IF_OP:
        //
        // Ignore end of EFI_IFR_NO_SUBMIT_IF and EFI_IFR_INCONSISTENT_IF
        //
        break;

      case EFI_IFR_SUPPRESS_IF_OP:
        if (SuppressForOption) {
          InScopeOptionSuppress = FALSE;
        } else {
          mInScopeSuppress = FALSE;
        }
        break;

      case EFI_IFR_GRAY_OUT_IF_OP:
        mInScopeGrayOut = FALSE;
        break;

      case EFI_IFR_DISABLE_IF_OP:
        mInScopeDisable = FALSE;
        OpCodeDisabled = FALSE;
        break;

      case EFI_IFR_ONE_OF_OP:
      case EFI_IFR_ORDERED_LIST_OP:
        SuppressForOption = FALSE;
        break;

      case EFI_IFR_DEFAULT_OP:
        InScopeDefault = FALSE;
        break;

      default:
        if (IsExpressionOpCode (ScopeOpCode)) {
          if (mInScopeDisable && CurrentForm == NULL) {
            //
            // This is DisableIf expression for Form, it should be a constant expression
            //
            Status = EvaluateExpression (FormSet, CurrentForm, CurrentExpression);
            if (EFI_ERROR (Status)) {
              return Status;
            }
            if (CurrentExpression->Result.Type != EFI_IFR_TYPE_BOOLEAN) {
              return EFI_INVALID_PARAMETER;
            }

            OpCodeDisabled = CurrentExpression->Result.Value.b;
            //
            // DisableIf Expression is only used once and not quequed, free it
            //
            DestroyExpression (CurrentExpression);
          }

          //
          // End of current Expression
          //
          CurrentExpression = NULL;
        }
        break;
      }
      break;

    default:
      break;
    }
  }

  return EFI_SUCCESS;
}
