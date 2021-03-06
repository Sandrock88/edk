/*++

Copyright (c) 2005 - 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  NetBuffer.c

Abstract:


--*/

#include "NetBuffer.h"

STATIC
NET_BUF *
NetbufAllocStruct (
  IN UINT32                 BlockNum,
  IN UINT32                 BlockOpNum
  )
/*++

Routine Description:

  Allocate and build up the sketch for a NET_BUF. The net buffer allocated
  has the BlockOpNum's NET_BLOCK_OP, and its associated NET_VECTOR has the 
  BlockNum's NET_BLOCK.

Arguments:

  BlockNum   - The number of NET_BLOCK in the Vector of net buffer
  BlockOpNum - The number of NET_BLOCK_OP in the net buffer
Returns:

  NET_BUF * - Pointer to the allocated NET_BUF. If NULL 
              the allocation failed due to resource limit.

--*/  
{
  NET_BUF                   *Nbuf;
  NET_VECTOR                *Vector;
   
  ASSERT (BlockOpNum >= 1);
  
  //
  // Allocate three memory blocks.
  //
  Nbuf = NetAllocateZeroPool (NET_BUF_SIZE (BlockOpNum));

  if (Nbuf == NULL) {
    return NULL;
  }

  Nbuf->Signature           = NET_BUF_SIGNATURE;
  Nbuf->RefCnt              = 1;
  Nbuf->BlockOpNum          = BlockOpNum;
  NetListInit (&Nbuf->List);
 
  if (BlockNum != 0) {
    Vector = NetAllocateZeroPool (NET_VECTOR_SIZE (BlockNum));

    if (Vector == NULL) {
      goto FreeNbuf;
    }

    Vector->Signature = NET_VECTOR_SIGNATURE;
    Vector->RefCnt    = 1;
    Vector->BlockNum  = BlockNum;
    Nbuf->Vector      = Vector;
  }
  
  return Nbuf;
  
FreeNbuf:
  
  NetFreePool (Nbuf);
  return NULL;
}

NET_BUF  *
NetbufAlloc (
  IN UINT32                 Len
  )
/*++

Routine Description:

  Allocate a single block NET_BUF. Upon allocation, all the
  free space is in the tail room.

Arguments:

  Len - The length of the block.

Returns:

  NET_BUF * - Pointer to the allocated NET_BUF. If NULL 
              the allocation failed due to resource limit.

--*/
{
  NET_BUF                   *Nbuf;
  NET_VECTOR                *Vector;
  UINT8                     *Bulk;

  ASSERT (Len > 0);

  Nbuf = NetbufAllocStruct (1, 1);

  if (Nbuf == NULL) {
    return NULL;
  }
  
  Bulk = NetAllocatePool (Len);
  
  if (Bulk == NULL) {
    goto FreeNBuf;
  }

  Vector = Nbuf->Vector;
  Vector->Len                 = Len;

  Vector->Block[0].Bulk       = Bulk;
  Vector->Block[0].Len        = Len;

  Nbuf->BlockOp[0].BlockHead  = Bulk;
  Nbuf->BlockOp[0].BlockTail  = Bulk + Len;

  Nbuf->BlockOp[0].Head       = Bulk;
  Nbuf->BlockOp[0].Tail       = Bulk;
  Nbuf->BlockOp[0].Size       = 0;

  return Nbuf;

FreeNBuf:
  NetFreePool (Nbuf);
  return NULL;
}

STATIC
VOID 
NetbufFreeVector (
  IN NET_VECTOR             *Vector
  )
/*++

Routine Description:

  Free the vector

Arguments:

  Vector  - Pointer to the NET_VECTOR to be freed.

Returns:

  None.

--*/

{
  UINT32                    Index;

  NET_CHECK_SIGNATURE (Vector, NET_VECTOR_SIGNATURE);
  ASSERT (Vector->RefCnt > 0);

  Vector->RefCnt--;

  if (Vector->RefCnt > 0) {
    return;
  }

  if (Vector->Free != NULL) {
    //
    // Call external free function to free the vector if it
    // isn't NULL. If NET_VECTOR_OWN_FIRST is set, release the 
    // first block since it is allocated by us
    //
    if (Vector->Flag & NET_VECTOR_OWN_FIRST) {
      NetFreePool (Vector->Block[0].Bulk);
    }

    Vector->Free (Vector->Arg);
    
  } else {
    //
    // Free each memory block associated with the Vector
    //
    for (Index = 0; Index < Vector->BlockNum; Index++) {
      NetFreePool (Vector->Block[Index].Bulk);
    }
  }

  NetFreePool (Vector);
}

VOID
NetbufFree (
  IN NET_BUF                *Nbuf
  )
/*++

Routine Description:

  Free the buffer and its associated NET_VECTOR.

Arguments:

  Nbuf  - Pointer to the NET_BUF to be freed.

Returns:

  None.

--*/
{
  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  ASSERT (Nbuf->RefCnt > 0);

  Nbuf->RefCnt--;

  if (Nbuf->RefCnt == 0) {
    //
    // Update Vector only when NBuf is to be released. That is,
    // all the sharing of Nbuf increse Vector's RefCnt by one
    //
    NetbufFreeVector (Nbuf->Vector);
    NetFreePool (Nbuf);
  }
}

NET_BUF  *
NetbufClone (
  IN NET_BUF                *Nbuf
  )
/*++

Routine Description:

  Create a copy of NET_BUF that share the associated NET_DATA.

Arguments:

  Nbuf  - Pointer to the net buffer to be cloned.

Returns:

  NET_BUF *  - Pointer to the cloned net buffer.

--*/
{
  NET_BUF                   *Clone;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  Clone = NetAllocatePool (NET_BUF_SIZE (Nbuf->BlockOpNum));

  if (Clone == NULL) {
    return NULL;
  }

  Clone->Signature  = NET_BUF_SIGNATURE;
  Clone->RefCnt     = 1;
  NetListInit (&Clone->List);

  Clone->Ip   = Nbuf->Ip;
  Clone->Tcp  = Nbuf->Tcp;
  
  NetCopyMem (Clone->ProtoData, Nbuf->ProtoData, NET_PROTO_DATA);

  NET_GET_REF (Nbuf->Vector);

  Clone->Vector     = Nbuf->Vector;
  Clone->BlockOpNum = Nbuf->BlockOpNum;
  Clone->TotalSize  = Nbuf->TotalSize;
  NetCopyMem (Clone->BlockOp, Nbuf->BlockOp, sizeof (NET_BLOCK_OP) * Nbuf->BlockOpNum);

  return Clone;
}

NET_BUF  *
NetbufDuplicate (
  IN NET_BUF                *Nbuf,
  IN NET_BUF                *Duplicate        OPTIONAL,
  IN UINT32                 HeadSpace
  )
/*++

Routine Description:

  Create a duplicated copy of Nbuf, data is copied. Also leave some 
  head space before the data.

Arguments:

  Nbuf      - Pointer to the net buffer to be cloned.
  Duplicate - Pointer to the net buffer to duplicate to, if NULL a new net 
              buffer is allocated.
  HeadSpace - Length of the head space to reserve
  
Returns:

  NET_BUF *  - Pointer to the duplicated net buffer.

--*/
{
  UINT8                     *Dst;
  
  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  if (Duplicate == NULL) {
    Duplicate = NetbufAlloc (Nbuf->TotalSize + HeadSpace);
  }

  if (Duplicate == NULL) {
    return NULL;
  }

  //
  // Don't set the IP and TCP head point, since it is most
  // like that they are pointing to the memory of Nbuf.
  //
  NetCopyMem (Duplicate->ProtoData, Nbuf->ProtoData, NET_PROTO_DATA);
  NetbufReserve (Duplicate, HeadSpace);
  
  Dst = NetbufAllocSpace (Duplicate, Nbuf->TotalSize, NET_BUF_TAIL);
  NetbufCopy (Nbuf, 0, Nbuf->TotalSize, Dst);

  return Duplicate;
}

VOID
NetbufFreeList (
  IN NET_LIST_ENTRY         *Head
  )
/*++

Routine Description:

  Free a list of net buffers.

Arguments:

  Head  - Pointer to the head of linked net buffers.

Returns:

  None.

--*/
{
  NET_LIST_ENTRY            *Entry;
  NET_LIST_ENTRY            *Next;
  NET_BUF                   *Nbuf;

  Entry = Head->ForwardLink;
  
  NET_LIST_FOR_EACH_SAFE (Entry, Next, Head) {
    Nbuf = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);
    NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
    
    NetListRemoveEntry (Entry);
    NetbufFree (Nbuf);
  }
  
  ASSERT (NetListIsEmpty (Head));
}

UINT8  *
NetbufGetByte (
  IN  NET_BUF               *Nbuf,
  IN  UINT32                Offset,
  OUT UINT32                *Index  OPTIONAL
  )
/*++

Routine Description:

  Get the position of some byte in the net buffer. This can be used
  to, for example, retrieve the IP header in the packet. It also 
  returns the fragment that contains the byte which is used mainly by
  the buffer implementation itself.

Arguments:

  Nbuf   - Pointer to the net buffer.
  Offset - The index or offset of the byte
  Index  - Index of the fragment that contains the block
  
Returns:

  UINT8 *  - Pointer to the nth byte of data in the net buffer.
             If NULL, there is no such data in the net buffer.

--*/
{
  NET_BLOCK_OP              *BlockOp;
  UINT32                    Loop;
  UINT32                    Len;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  if (Offset >= Nbuf->TotalSize) {
    return NULL;
  }
  
  BlockOp = Nbuf->BlockOp;
  Len     = 0;
  
  for (Loop = 0; Loop < Nbuf->BlockOpNum; Loop++) {

    if (Len + BlockOp[Loop].Size <= Offset) {
      Len += BlockOp[Loop].Size;
      continue;
    } 

    if (Index != NULL) {
      *Index = Loop;
    }
    
    return BlockOp[Loop].Head + (Offset - Len);
  }

  return NULL;
}


STATIC
VOID
NetbufSetBlock (
  IN NET_BUF                *Nbuf,
  IN UINT8                  *Bulk,
  IN UINT32                 Len,
  IN UINT32                 Index
  )
/*++

Routine Description:

  Set the NET_BLOCK and corresponding NET_BLOCK_OP in
  the buffer. All the pointers in NET_BLOCK and NET_BLOCK_OP
  are set to the bulk's head and tail respectively. So, this
  function alone can't be used by NetbufAlloc.

Arguments:

  Nbuf  - Pointer to the net buffer.
  Bulk  - Pointer to the data.
  Len   - Length of the bulk data.
  Index - The data block index in the net buffer the bulk data
          should belong to.

Returns:

  None.

--*/
{
  NET_BLOCK_OP              *BlockOp;
  NET_BLOCK                 *Block;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  NET_CHECK_SIGNATURE (Nbuf->Vector, NET_VECTOR_SIGNATURE);
  ASSERT (Index < Nbuf->BlockOpNum);

  Block               = &(Nbuf->Vector->Block[Index]);
  BlockOp             = &(Nbuf->BlockOp[Index]);
  Block->Len          = Len;
  Block->Bulk         = Bulk;
  BlockOp->BlockHead  = Bulk;
  BlockOp->BlockTail  = Bulk + Len;
  BlockOp->Head       = Bulk;
  BlockOp->Tail       = Bulk + Len;
  BlockOp->Size       = Len;
}


STATIC
VOID
NetbufSetBlockOp (
  IN NET_BUF                *Nbuf,
  IN UINT8                  *Bulk,
  IN UINT32                 Len,
  IN UINT32                 Index
  )
/*++

Routine Description:

  Set the NET_BLOCK_OP in the buffer. The corresponding NET_BLOCK 
  structure is left untouched. Some times, there is no 1:1 relationship
  between NET_BLOCK and NET_BLOCK_OP. For example, that in NetbufGetFragment.

Arguments:

  Nbuf  - Pointer to the net buffer.
  Bulk  - Pointer to the data.
  Len   - Length of the bulk data.
  Index - The data block index in the net buffer the bulk data
          should belong to.

Returns:

  None.

--*/
{
  NET_BLOCK_OP              *BlockOp;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  ASSERT (Index < Nbuf->BlockOpNum);

  BlockOp             = &(Nbuf->BlockOp[Index]);
  BlockOp->BlockHead  = Bulk;
  BlockOp->BlockTail  = Bulk + Len;
  BlockOp->Head       = Bulk;
  BlockOp->Tail       = Bulk + Len;
  BlockOp->Size       = Len;
}

STATIC 
VOID 
NetbufGetFragmentFree (
  IN VOID                   *Arg
  )
/*++

Routine Description:

  Helper function for NetbufClone. It is necessary because NetbufGetFragment
  may allocate the first block to accomodate the HeadSpace and HeadLen. So, it 
  need to create a new NET_VECTOR. But, we want to avoid data copy by sharing 
  the old NET_VECTOR.
  
Arguments:

  Arg       - Point to the old NET_VECTOR

Returns:

  NONE

--*/

{
  NET_VECTOR                *Vector;

  Vector = (NET_VECTOR *)Arg;
  NetbufFreeVector (Vector);
}


NET_BUF  *
NetbufGetFragment (
  IN NET_BUF                *Nbuf,
  IN UINT32                 Offset,
  IN UINT32                 Len,
  IN UINT32                 HeadSpace
  )
/*++

Routine Description:

  Create a NET_BUF structure which contains Len byte data of
  Nbuf starting from Offset. A new NET_BUF structure will be 
  created but the associated data in NET_VECTOR is shared. 
  This function exists to do IP packet fragmentation.

Arguments:

  Nbuf      - Pointer to the net buffer to be cloned.
  Offset    - Starting point of the data to be included in new buffer.
  Len       - How many data to include in new data
  HeadSpace - How many bytes of head space to reserve for protocol header

Returns:

  NET_BUF *  - Pointer to the cloned net buffer.

--*/
{
  NET_BUF                   *Child;
  NET_VECTOR                *Vector;
  NET_BLOCK_OP              *BlockOp;
  UINT32                    CurBlockOp;
  UINT32                    BlockOpNum;
  UINT8                     *FirstBulk;
  UINT32                    Index;
  UINT32                    First;
  UINT32                    Last;
  UINT32                    FirstSkip;
  UINT32                    FirstLen;
  UINT32                    LastLen;
  UINT32                    Cur;
  
  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  
  if ((Len == 0) || (Offset + Len > Nbuf->TotalSize)) {
    return NULL;
  }

  //
  // First find the first and last BlockOp that contains 
  // the valid data, and compute the offset of the first
  // BlockOp and length of the last BlockOp
  //
  BlockOp = Nbuf->BlockOp;
  Cur     = 0;

  for (Index = 0; Index < Nbuf->BlockOpNum; Index++) {
    if (Offset < Cur + BlockOp[Index].Size) {
      break;
    }

    Cur += BlockOp[Index].Size;
  }

  //
  // First is the index of the first BlockOp, FirstSkip is 
  // the offset of the first byte in the first BlockOp.
  //
  First     = Index;
  FirstSkip = Offset - Cur;
  FirstLen  = BlockOp[Index].Size - FirstSkip;

  //
  //redundant assignment to make compiler happy.
  //
  Last      = 0;
  LastLen   = 0;
  
  if (Len > FirstLen) {
    Cur += BlockOp[Index].Size;
    Index++;

    for (; Index < Nbuf->BlockOpNum; Index++) {
      if (Offset + Len <= Cur + BlockOp[Index].Size) {
        Last    = Index;
        LastLen = Offset + Len - Cur;
        break;
      }

      Cur += BlockOp[Index].Size;
    }
 
  } else {
    Last     = First;
    LastLen  = Len;
    FirstLen = Len;
  }

  BlockOpNum = Last - First + 1; 
  CurBlockOp = 0;

  if (HeadSpace != 0) {
    //
    // Allocate an extra block to accomdate the head space.
    //
    BlockOpNum++;
    
    Child = NetbufAllocStruct (1, BlockOpNum);
    
    if (Child == NULL) {
      return NULL;
    }

    FirstBulk = NetAllocatePool (HeadSpace);
    
    if (FirstBulk == NULL) {
      goto FreeChild;
    }

    Vector        = Child->Vector;
    Vector->Free  = NetbufGetFragmentFree;
    Vector->Arg   = Nbuf->Vector;
    Vector->Flag  = NET_VECTOR_OWN_FIRST;
    Vector->Len   = HeadSpace;

    //
    //Reserve the head space in the first block
    //
    NetbufSetBlock (Child, FirstBulk, HeadSpace, 0);
    Child->BlockOp[0].Head += HeadSpace;
    Child->BlockOp[0].Size =  0;
    CurBlockOp++;

  }else {
    Child = NetbufAllocStruct (0, BlockOpNum);

    if (Child == NULL) {
      return NULL;
    }

    Child->Vector = Nbuf->Vector;
  }

  NET_GET_REF (Nbuf->Vector);
  Child->TotalSize = Len;

  //
  // Set all the BlockOp up, the first and last one are special
  // and need special process.
  //
  NetbufSetBlockOp (
    Child,
    Nbuf->BlockOp[First].Head + FirstSkip,
    FirstLen,
    CurBlockOp++
    );

  for (Index = First + 1; Index < Last ; Index++) {
    NetbufSetBlockOp (
      Child, 
      BlockOp[Index].Head, 
      BlockOp[Index].Size, 
      CurBlockOp++
      );    
  }

  if (First != Last) {
    NetbufSetBlockOp (
      Child,
      BlockOp[Last].Head,
      LastLen,
      CurBlockOp
      );
  }

  NetCopyMem (Child->ProtoData, Nbuf->ProtoData, NET_PROTO_DATA);
  return Child;
  
FreeChild:
  
  NetFreePool (Child);
  return NULL;
}


NET_BUF  *
NetbufFromExt (
  IN NET_FRAGMENT           *ExtFragment,
  IN UINT32                 ExtNum,
  IN UINT32                 HeadSpace,
  IN UINT32                 HeadLen,
  IN NET_VECTOR_EXT_FREE    ExtFree,
  IN VOID                   *Arg          OPTIONAL
  )
/*++

Routine Description:

  Build a NET_BUF from external blocks.

Arguments:

  ExtFragment  - Pointer to the data block.
  ExtNum       - The number of the data block.
  HeadSpace    - The head space to be reserved.
  HeadLen      - The length of the protocol header, This function
                 will pull that number of data into a linear block.
  ExtFree      - Pointer to the caller provided free function.
  Arg          - The argument passed to ExtFree when ExtFree
                 is called.

Returns:

  NET_BUF *  - Pointer to the net buffer built from the data blocks.

--*/
{
  NET_BUF                   *Nbuf;
  NET_VECTOR                *Vector;
  NET_FRAGMENT              SavedFragment;
  UINT32                    SavedIndex;
  UINT32                    TotalLen;
  UINT32                    BlockNum;
  UINT8                     *FirstBlock;
  UINT32                    FirstBlockLen;
  UINT8                     *Header;
  UINT32                    CurBlock;
  UINT32                    Index;
  UINT32                    Len;
  UINT32                    Copied;

  ASSERT ((ExtFragment != NULL) && (ExtNum > 0) && (ExtFree != NULL));

  SavedFragment.Bulk = NULL;
  SavedFragment.Len  = 0;

  FirstBlockLen  = 0;
  FirstBlock     = NULL;
  BlockNum       = ExtNum;
  Index          = 0;
  TotalLen       = 0;
  SavedIndex     = 0;
  Len            = 0;
  Copied         = 0;
  
  //
  // No need to consolidate the header if the first block is 
  // longer than the header length or there is only one block.
  //
  if ((ExtFragment[0].Len >= HeadLen) || (ExtNum == 1)) {
    HeadLen = 0;
  }
  
  //
  // Allocate an extra block if we need to:
  //  1. Allocate some header space
  //  2. aggreate the packet header
  //
  if ((HeadSpace != 0) || (HeadLen != 0)) {
    FirstBlockLen = HeadLen + HeadSpace;
    FirstBlock    = NetAllocatePool (FirstBlockLen);

    if (FirstBlock == NULL) {
      return NULL;
    }

    BlockNum++;
  }
  
  //
  // Copy the header to the first block, reduce the NET_BLOCK
  // to allocate by one for each block that is completely covered
  // by the first bulk.
  //
  if (HeadLen != 0) {
    Len    = HeadLen;
    Header = FirstBlock + HeadSpace;
    
    for (Index = 0; Index < ExtNum; Index++) {
      if (Len >= ExtFragment[Index].Len) {
        NetCopyMem (Header, ExtFragment[Index].Bulk, ExtFragment[Index].Len);

        Copied    += ExtFragment[Index].Len;
        Len       -= ExtFragment[Index].Len;
        Header    += ExtFragment[Index].Len;
        TotalLen  += ExtFragment[Index].Len;
        BlockNum--;
        
        if (Len == 0) {
          //
          // Increament the index number to point to the next 
          // non-empty fragment.
          //
          Index++;
          break;
        }
        
      } else {
        NetCopyMem (Header, ExtFragment[Index].Bulk, Len);
        
        Copied    += Len;
        TotalLen  += Len;
        
        //
        // Adjust the block structure to exclude the data copied,
        // So, the left-over block can be processed as other blocks.
        // But it must be recovered later. (SavedIndex > 0) always
        // holds since we don't aggreate the header if the first block
        // is bigger enough that the header is continuous
        //
        SavedIndex    = Index;
        SavedFragment = ExtFragment[Index];
        ExtFragment[Index].Bulk += Len;
        ExtFragment[Index].Len  -= Len;
        break;
      }
    }
  }

  Nbuf = NetbufAllocStruct (BlockNum, BlockNum);

  if (Nbuf == NULL) {
    goto FreeFirstBlock;
  }

  Vector       = Nbuf->Vector;
  Vector->Free = ExtFree;
  Vector->Arg  = Arg;
  Vector->Flag = (FirstBlockLen ? NET_VECTOR_OWN_FIRST : 0);
  
  //
  // Set the first block up which may contain 
  // some head space and aggregated header
  //
  CurBlock = 0;

  if (FirstBlockLen != 0) {
    NetbufSetBlock (Nbuf, FirstBlock, HeadSpace + Copied, 0);
    Nbuf->BlockOp[0].Head += HeadSpace;
    Nbuf->BlockOp[0].Size =  Copied;

    CurBlock++;
  }
  
  for (; Index < ExtNum; Index++) {
    NetbufSetBlock (Nbuf, ExtFragment[Index].Bulk, ExtFragment[Index].Len, CurBlock);
    TotalLen += ExtFragment[Index].Len;
    CurBlock++;
  }

  Vector->Len     = TotalLen + HeadSpace;  
  Nbuf->TotalSize = TotalLen;

  if (SavedIndex) {
    ExtFragment[SavedIndex] = SavedFragment;
  }
  
  return Nbuf;

FreeFirstBlock:
  NetFreePool (FirstBlock);
  return NULL;
}

EFI_STATUS
NetbufBuildExt (
  IN NET_BUF                *Nbuf,             
  IN NET_FRAGMENT           *ExtFragment,
  IN UINT32                 *ExtNum
  )
/*++

Routine Description:

  Build a fragment table to contain the fragments in the 
  buffer. This is the opposite of the NetbufFromExt.

Arguments:
  Nbuf          - Point to the net buffer
  ExtFragment   - Pointer to the data block.
  ExtNum        - The number of the data block.

Returns:
  EFI_BUFFER_TOO_SMALL - The number of non-empty block is bigger than ExtNum
  EFI_SUCCESS          - Fragment table built.

--*/
{
  UINT32                    Index;
  UINT32                    Current;

  Current = 0;
  
  for (Index = 0; (Index < Nbuf->BlockOpNum); Index++) {
    if (Nbuf->BlockOp[Index].Size == 0) {
      continue;
    }
    
    if (Current < *ExtNum) {
      ExtFragment[Current].Bulk = Nbuf->BlockOp[Index].Head;
      ExtFragment[Current].Len  = Nbuf->BlockOp[Index].Size;
      Current++;
    } else {
      return EFI_BUFFER_TOO_SMALL;
    }
  }

  *ExtNum = Current;
  return EFI_SUCCESS;
}

NET_BUF  *
NetbufFromBufList (
  IN NET_LIST_ENTRY         *BufList,
  IN UINT32                 HeadSpace,
  IN UINT32                 HeaderLen,
  IN NET_VECTOR_EXT_FREE    ExtFree,
  IN VOID                   *Arg              OPTIONAL
  )
/*++

Routine Description:

  Build a NET_BUF from a list of NET_BUF.

Arguments:

  BufList     - A List of NET_BUF.
  HeadSpace   - The head space to be reserved.
  HeaderLen   - The length of the protocol header, This function
                will pull that number of data into a linear block.
  ExtFree     - Pointer to the caller provided free function.
  Arg         - The argument passed to ExtFree when ExtFree
                is called.

Returns:

  NET_BUF *  - Pointer to the net buffer built from the data blocks.

--*/

{
  NET_FRAGMENT              *Fragment;
  UINT32                    FragmentNum;
  NET_LIST_ENTRY            *Entry;
  NET_BUF                   *Nbuf;
  UINT32                    Index;
  UINT32                    Current;

  //
  //Compute how many blocks are there
  //
  FragmentNum = 0;

  NET_LIST_FOR_EACH (Entry, BufList) {
    Nbuf = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);
    NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
    FragmentNum += Nbuf->BlockOpNum;
  }

  //
  //Allocate and copy block points
  //
  Fragment = NetAllocatePool (sizeof (NET_FRAGMENT) * FragmentNum);

  if (Fragment == NULL) {
    return NULL;
  }

  Current = 0;
  
  NET_LIST_FOR_EACH (Entry, BufList) {
    Nbuf = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);
    NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
    
    for (Index = 0; Index < Nbuf->BlockOpNum; Index++) {
      if (Nbuf->BlockOp[Index].Size) {
        Fragment[Current].Bulk = Nbuf->BlockOp[Index].Head;
        Fragment[Current].Len  = Nbuf->BlockOp[Index].Size;
        Current++;
      }
    }
  }

  Nbuf = NetbufFromExt (Fragment, Current, HeadSpace, HeaderLen, ExtFree, Arg);
  NetFreePool (Fragment);

  return Nbuf;
}

VOID
NetbufReserve (
  IN NET_BUF                *Nbuf,
  IN UINT32                 Len
  )
/*++

Routine Description:

  Reserve some space in the header room of the buffer. 
  Upon allocation, all the space are in the tail room 
  of the buffer. Call this function to move some space
  to the header room. This function is quite limited in 
  that it can only reserver space from the first block
  of an empty NET_BUF not built from the external. But
  it should be enough for the network stack.

Arguments:

  Nbuf  - Pointer to the net buffer.
  Len   - The length of buffer to be reserverd.

Returns:

  None.

--*/
{
  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  NET_CHECK_SIGNATURE (Nbuf->Vector, NET_VECTOR_SIGNATURE);

  ASSERT ((Nbuf->BlockOpNum == 1) && (Nbuf->TotalSize == 0));
  ASSERT ((Nbuf->Vector->Free == NULL) && (Nbuf->Vector->Len >= Len));

  Nbuf->BlockOp[0].Head += Len;
  Nbuf->BlockOp[0].Tail += Len;

  ASSERT (Nbuf->BlockOp[0].Tail <= Nbuf->BlockOp[0].BlockTail);
}

UINT8  *
NetbufAllocSpace (
  IN NET_BUF                *Nbuf,
  IN UINT32                 Len,
  IN BOOLEAN                FromHead
  )
/*++

Routine Description:

  Allocate some space from the header or tail of the buffer.

Arguments:

  Nbuf      - Pointer to the net buffer.
  Len       - The length of the buffer to be allocated.
  FromHead  - The flag to indicate whether reserve the data
              from head or tail. TRUE for from head, and
              FALSE for from tail.

Returns:

  UINT8 *   - Pointer to the first byte of the allocated buffer.

--*/
{
  NET_BLOCK_OP              *BlockOp;
  UINT32                    Index;
  UINT8                     *SavedTail;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  NET_CHECK_SIGNATURE (Nbuf->Vector, NET_VECTOR_SIGNATURE);

  ASSERT (Len > 0);
  
  if (FromHead) {
    //
    // Allocate some space from head. If the buffer is empty,
    // allocate from the first block. If it isn't, allocate 
    // from the first non-empty block, or the block before that.
    //
    if (Nbuf->TotalSize == 0) {
      Index = 0;
    } else {
      NetbufGetByte (Nbuf, 0, &Index);
     
      if ((NET_HEADSPACE(&(Nbuf->BlockOp[Index])) < Len) && (Index > 0)) {
        Index--;
      }
    }
    
    BlockOp = &(Nbuf->BlockOp[Index]);
    
    if (NET_HEADSPACE (BlockOp) < Len) {
      return NULL;
    }

    BlockOp->Head   -= Len;
    BlockOp->Size   += Len;
    Nbuf->TotalSize += Len;

    return BlockOp->Head;
    
  } else {
    //
    // Allocate some space from the tail. If the buffer is empty,
    // allocate from the first block. If it isn't, allocate 
    // from the last non-empty block, or the block after that.
    //
    if (Nbuf->TotalSize == 0) {
      Index = 0;
    } else {
      NetbufGetByte (Nbuf, Nbuf->TotalSize - 1, &Index);
      
      if ((NET_TAILSPACE(&(Nbuf->BlockOp[Index])) < Len) && 
          (Index < Nbuf->BlockOpNum - 1)) {
          
        Index++;
      }
    }

    BlockOp = &(Nbuf->BlockOp[Index]);
    
    if (NET_TAILSPACE (BlockOp) < Len) {
      return NULL;
    }

    SavedTail       = BlockOp->Tail;

    BlockOp->Tail   += Len;
    BlockOp->Size   += Len;
    Nbuf->TotalSize += Len;

    return SavedTail;
  }
}

STATIC
VOID
NetblockTrim (
  IN NET_BLOCK_OP           *BlockOp,
  IN UINT32                 Len,
  IN BOOLEAN                FromHead
  )
/*++

Routine Description:

  Trim a single NET_BLOCK.

Arguments:

  BlockOp   - Pointer to the NET_BLOCK.
  Len       - The length of the data to be trimmed.
  FromHead  - The flag to indicate whether trim data from
              head or tail. TRUE for from head, and FALSE
              for from tail.

Returns:

  None.

--*/
{
  ASSERT (BlockOp && (BlockOp->Size >= Len));

  BlockOp->Size -= Len;

  if (FromHead) {
    BlockOp->Head += Len;
  } else {
    BlockOp->Tail -= Len;
  }
}

UINT32
NetbufTrim (
  IN NET_BUF                *Nbuf,
  IN UINT32                 Len,
  IN BOOLEAN                FromHead
  )
/*++

Routine Description:

  Trim some data from the header or tail of the buffer.

Arguments:

  Nbuf      - Pointer to the net buffer.
  Len       - The length of the data to be trimmed.
  FromHead  - The flag to indicate whether trim data from
              head or tail. TRUE for from head, and FALSE
              for from tail.

Returns:

  UINTN     - Length of the actually trimmed data.

--*/
{
  NET_BLOCK_OP              *BlockOp;
  UINT32                    Index;
  UINT32                    Trimmed;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  if (Len > Nbuf->TotalSize) {
    Len = Nbuf->TotalSize;
  }

  //
  // If FromTail is true, iterate backward. That
  // is, init Index to NBuf->BlockNum - 1, and
  // decrease it by 1 during each loop. Otherwise,
  // iterate forward. That is, init Index to 0, and
  // increase it by 1 during each loop.
  //
  Trimmed          = 0;
  Nbuf->TotalSize -= Len;

  Index   = (FromHead ? 0 : Nbuf->BlockOpNum - 1);
  BlockOp = Nbuf->BlockOp;

  for (;;) {
    if (BlockOp[Index].Size == 0) {
      Index += (FromHead ? 1 : -1);
      continue;
    }

    if (Len > BlockOp[Index].Size) {
      Len     -= BlockOp[Index].Size;
      Trimmed += BlockOp[Index].Size;
      NetblockTrim (&BlockOp[Index], BlockOp[Index].Size, FromHead);
    } else {
      Trimmed += Len;
      NetblockTrim (&BlockOp[Index], Len, FromHead);
      break;
    }

    Index += (FromHead ? 1 : -1);
  }

  return Trimmed;
}

UINT32
NetbufCopy (
  IN NET_BUF                *Nbuf,
  IN UINT32                 Offset,
  IN UINT32                 Len,
  IN UINT8                  *Dest
  )
/*++

Routine Description:

  Copy the data from the specific offset to the destination.

Arguments:

  Nbuf    - Pointer to the net buffer.
  Offset  - The sequence number of the first byte to copy.
  Len     - Length of the data to copy.
  Dest    - The destination of the data to copy to.

Returns:

  UINTN   - The length of the copied data.

--*/
{
  NET_BLOCK_OP              *BlockOp;
  UINT32                    Skip;
  UINT32                    Left;
  UINT32                    Copied;
  UINT32                    Index;
  UINT32                    Cur;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);
  ASSERT (Dest);

  if ((Len == 0) || (Nbuf->TotalSize <= Offset)) {
    return 0;
  }

  if (Nbuf->TotalSize - Offset < Len) {
    Len = Nbuf->TotalSize - Offset;
  }

  BlockOp = Nbuf->BlockOp;

  //
  // Skip to the offset. Don't make "Offset-By-One" error here.
  // Cur + BLOCK.SIZE is the first sequence number of next block.
  // So, (Offset < Cur + BLOCK.SIZE) means that the  first byte
  // is in the current block. if (Offset == Cur + BLOCK.SIZE), the
  // first byte is the next block's first byte.
  //
  Cur = 0;
  
  for (Index = 0; Index < Nbuf->BlockOpNum; Index++) {
    if (BlockOp[Index].Size == 0) {
      continue;
    }

    if (Offset < Cur + BlockOp[Index].Size) {
      break;
    }

    Cur += BlockOp[Index].Size;
  }

  //
  // Cur is the sequence number of the first byte in the block
  // Offset - Cur is the number of bytes before first byte to
  // to copy in the current block.
  //
  Skip  = Offset - Cur;
  Left  = BlockOp[Index].Size - Skip;

  if (Len <= Left) {
    NetCopyMem (Dest, BlockOp[Index].Head + Skip, Len);
    return Len;
  }

  NetCopyMem (Dest, BlockOp[Index].Head + Skip, Left);

  Dest  += Left;
  Len   -= Left;
  Copied = Left;

  Index++;

  for (; Index < Nbuf->BlockOpNum; Index++) {
    if (Len > BlockOp[Index].Size) {
      Len    -= BlockOp[Index].Size;
      Copied += BlockOp[Index].Size;

      NetCopyMem (Dest, BlockOp[Index].Head, BlockOp[Index].Size);
      Dest   += BlockOp[Index].Size;
    } else {
      Copied += Len;
      NetCopyMem (Dest, BlockOp[Index].Head, Len);
      break;
    }
  }

  return Copied;
}

VOID
NetbufQueInit (
  IN NET_BUF_QUEUE          *NbufQue
  )
/*++

Routine Description:

  Initiate the net buffer queue.

Arguments:

  NbufQue - Pointer to the net buffer queue to be initiated.

Returns:

  None.

--*/
{
  NbufQue->Signature  = NET_QUE_SIGNATURE;
  NbufQue->RefCnt     = 1;
  NetListInit (&NbufQue->List);

  NetListInit (&NbufQue->BufList);
  NbufQue->BufSize  = 0;
  NbufQue->BufNum   = 0;
}

NET_BUF_QUEUE  *
NetbufQueAlloc (
  VOID
  )
/*++

Routine Description:

  Allocate an initialized net buffer queue.

Arguments:

  None.

Returns:

  NET_BUF_QUEUE * - Pointer to the allocated net buffer queue.

--*/
{
  NET_BUF_QUEUE             *NbufQue;

  NbufQue = NetAllocatePool (sizeof (NET_BUF_QUEUE));
  if (NbufQue == NULL) {
    return NULL;
  }

  NetbufQueInit (NbufQue);

  return NbufQue;
}

VOID
NetbufQueFree (
  IN NET_BUF_QUEUE          *NbufQue
  )
/*++

Routine Description:

  Free a net buffer queue.

Arguments:

  NbufQue - Poitner to the net buffer queue to be freed.

Returns:

  None.

--*/
{
  NET_CHECK_SIGNATURE (NbufQue, NET_QUE_SIGNATURE);

  NbufQue->RefCnt--;

  if (NbufQue->RefCnt == 0) {
    NetbufQueFlush (NbufQue);
    NetFreePool (NbufQue);
  }
}

VOID
NetbufQueAppend (
  IN NET_BUF_QUEUE          *NbufQue,
  IN NET_BUF                *Nbuf
  )
/*++

Routine Description:

  Append a buffer to the end of the queue.

Arguments:

  NbufQue - Pointer to the net buffer queue.
  Nbuf    - Pointer to the net buffer to be appended.

Returns:

  None.

--*/
{
  NET_CHECK_SIGNATURE (NbufQue, NET_QUE_SIGNATURE);
  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  NetListInsertTail (&NbufQue->BufList, &Nbuf->List);

  NbufQue->BufSize += Nbuf->TotalSize;
  NbufQue->BufNum++;
}

NET_BUF  *
NetbufQueRemove (
  IN NET_BUF_QUEUE          *NbufQue
  )
/*++

Routine Description:

  Remove a net buffer from head in the specific queue.

Arguments:

  NbufQue - Pointer to the net buffer queue.

Returns:

  NET_BUF *  - Pointer to the net buffer removed from
               the specific queue.

--*/
{
  NET_BUF                   *First;

  NET_CHECK_SIGNATURE (NbufQue, NET_QUE_SIGNATURE);

  if (NbufQue->BufNum == 0) {
    return NULL;
  }

  First = NET_LIST_USER_STRUCT (NbufQue->BufList.ForwardLink, NET_BUF, List);

  NetListRemoveHead (&NbufQue->BufList);

  NbufQue->BufSize -= First->TotalSize;
  NbufQue->BufNum--;
  return First;
}

UINT32
NetbufQueCopy (
  IN NET_BUF_QUEUE          *NbufQue,
  IN UINT32                 Offset,
  IN UINT32                 Len,
  IN UINT8                  *Dest
  )
/*++

Routine Description:

  Copy some data from the buffer queue to the destination.

Arguments:

  NbufQue - Pointer to the net buffer queue.
  Offset  - The sequence number of the first byte to copy.
  Len     - Length of the data to copy.
  Dest    - The destination of the data to copy to.

Returns:

  UINTN   - The length of the copied data.

--*/
{
  NET_LIST_ENTRY            *Entry;
  NET_BUF                   *Nbuf;
  UINT32                    Skip;
  UINT32                    Left;
  UINT32                    Cur;
  UINT32                    Copied;

  NET_CHECK_SIGNATURE (NbufQue, NET_QUE_SIGNATURE);
  ASSERT (Dest != NULL);

  if ((Len == 0) || (NbufQue->BufSize <= Offset)) {
    return 0;
  }

  if (NbufQue->BufSize - Offset < Len) {
    Len = NbufQue->BufSize - Offset;
  }
  
  //
  // skip to the Offset
  //
  Cur   = 0;
  Nbuf  = NULL;

  NET_LIST_FOR_EACH (Entry, &NbufQue->BufList) {
    Nbuf = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);

    if (Offset < Cur + Nbuf->TotalSize) {
      break;
    }

    Cur += Nbuf->TotalSize;
  }
  
  //
  // Copy the data in the first buffer.
  //
  Skip  = Offset - Cur;
  Left  = Nbuf->TotalSize - Skip;

  if (Len < Left) {
    return NetbufCopy (Nbuf, Skip, Len, Dest);
  }

  NetbufCopy (Nbuf, Skip, Left, Dest);
  Dest  += Left;
  Len   -= Left;
  Copied = Left;

  //
  // Iterate over the others
  //
  Entry = Entry->ForwardLink;

  while ((Len > 0) && (Entry != &NbufQue->BufList)) {
    Nbuf = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);

    if (Len > Nbuf->TotalSize) {
      Len -= Nbuf->TotalSize;
      Copied += Nbuf->TotalSize;

      NetbufCopy (Nbuf, 0, Nbuf->TotalSize, Dest);
      Dest += Nbuf->TotalSize;

    } else {
      NetbufCopy (Nbuf, 0, Len, Dest);
      Copied += Len;
      break;
    }

    Entry = Entry->ForwardLink;
  }

  return Copied;
}

UINT32
NetbufQueTrim (
  IN NET_BUF_QUEUE          *NbufQue,
  IN UINT32                 Len
  )
/*++

Routine Description:

  Trim some data from the queue header, release the buffer if
  whole buffer is trimmed.

Arguments:

  NbufQue - Pointer to the net buffer queue.
  Len     - Length of the data to trim.

Returns:

  UINTN   - The length of the data trimmed.

--*/
{
  NET_LIST_ENTRY            *Entry;
  NET_LIST_ENTRY            *Next;
  NET_BUF                   *Nbuf;
  UINT32                    Trimmed;

  NET_CHECK_SIGNATURE (NbufQue, NET_QUE_SIGNATURE);

  if (Len == 0) {
    return 0;
  }

  if (Len > NbufQue->BufSize) {
    Len = NbufQue->BufSize;
  }

  NbufQue->BufSize -= Len;
  Trimmed = 0;

  NET_LIST_FOR_EACH_SAFE (Entry, Next, &NbufQue->BufList) {
    Nbuf = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);

    if (Len >= Nbuf->TotalSize) {
      Trimmed += Nbuf->TotalSize;
      Len -= Nbuf->TotalSize;

      NetListRemoveEntry (Entry);
      NetbufFree (Nbuf);
      
      NbufQue->BufNum--;

      if (Len == 0) {
        break;
      }
      
    } else {
      Trimmed += NetbufTrim (Nbuf, Len, NET_BUF_HEAD);
      break;
    }
  }

  return Trimmed;
}

VOID
NetbufQueFlush (
  IN NET_BUF_QUEUE          *NbufQue
  )
/*++

Routine Description:

  Flush the net buffer queue.

Arguments:

  NbufQue - Pointer to the queue to be flushed.

Returns:

  None.

--*/
{
  NET_CHECK_SIGNATURE (NbufQue, NET_QUE_SIGNATURE);

  NetbufFreeList (&NbufQue->BufList);

  NbufQue->BufNum   = 0;
  NbufQue->BufSize  = 0;
}

UINT16
NetblockChecksum (
  IN UINT8                  *Bulk,
  IN UINT32                 Len
  )
/*++

Routine Description:

  Compute checksum for a bulk of data.

Arguments:

  Bulk  - Pointer to the data.
  Len   - Length of the data, in bytes.

Returns:

  UINT16 - The computed checksum.

--*/
{
  register UINT32           Sum;

  Sum = 0;

  while (Len > 1) {
    Sum += *(UINT16 *) Bulk;
    Bulk += 2;
    Len -= 2;
  }

  //
  // Add left-over byte, if any
  //
  if (Len > 0) {
    Sum += *(UINT8 *) Bulk;
  }

  //
  // Fold 32-bit sum to 16 bits
  //
  while (Sum >> 16) {
    Sum = (Sum & 0xffff) + (Sum >> 16);

  }

  return (UINT16) Sum;
}

UINT16
NetAddChecksum (
  IN UINT16                 Checksum1,
  IN UINT16                 Checksum2
  )
/*++

Routine Description:

  Add two checksums.

Arguments:

  Checksum1 - The first checksum to be added.
  Checksum2 - The second checksum to be added.

Returns:

  UINT16    - The new checksum.

--*/
{
  UINT32                    Sum;

  Sum = Checksum1 + Checksum2;

  //
  // two UINT16 can only add up to a carry of 1.
  //
  if (Sum >> 16) {
    Sum = (Sum & 0xffff) + 1;

  }

  return (UINT16) Sum;
}

UINT16
NetbufChecksum (
  IN NET_BUF                *Nbuf
  )
/*++

Routine Description:

  Compute the checksum for a NET_BUF.

Arguments:

  Nbuf  - Pointer to the net buffer.

Returns:

  UINT16 - The computed checksum.

--*/
{
  NET_BLOCK_OP              *BlockOp;
  UINT32                    Offset;
  UINT16                    TotalSum;
  UINT16                    BlockSum;
  UINT32                    Index;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  TotalSum  = 0;
  Offset    = 0;
  BlockOp   = Nbuf->BlockOp;

  for (Index = 0; Index < Nbuf->BlockOpNum; Index++) {
    if (BlockOp[Index].Size == 0) {
      continue;
    }

    BlockSum = NetblockChecksum (BlockOp[Index].Head, BlockOp[Index].Size);

    if (Offset & 0x01) {
      //
      // The checksum starts with an odd byte, swap
      // the checksum before added to total checksum
      //
      BlockSum = NET_SWAP_SHORT (BlockSum);
    }

    TotalSum = NetAddChecksum (BlockSum, TotalSum);
    Offset  += BlockOp[Index].Size;
  }

  return TotalSum;
}

UINT16
NetPseudoHeadChecksum (
  IN IP4_ADDR               Src,
  IN IP4_ADDR               Dst,
  IN UINT8                  Proto,
  IN UINT16                 Len
  )
/*++

Routine Description:

  Compute the checksum for TCP/UDP pseudo header.
  Src, Dst are in network byte order. and Len is
  in host byte order.

Arguments:

  Src   - The source address of the packet.
  Dst   - The destination address of the packet.
  Proto - The protocol type of the packet.
  Len   - The length of the packet.

Returns:

  UINT16 - The computed checksum.

--*/
{
  NET_PSEUDO_HDR            Hdr;

  //
  // Zero the memory to relieve align problems
  //
  NetZeroMem (&Hdr, sizeof (Hdr));

  Hdr.SrcIp     = Src;
  Hdr.DstIp     = Dst;
  Hdr.Protocol  = Proto;
  Hdr.Len       = HTONS (Len);

  return NetblockChecksum ((UINT8 *) &Hdr, sizeof (Hdr));
}
