/*++

Copyright (c) 2005 - 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED. 

Module Name:

  Tcp4Option.c

Abstract:

  Routines to process TCP option.

--*/

#include "Tcp4Main.h"

STATIC
UINT16
TcpGetUint16 (
  IN UINT8 *Buf
  )
{
  UINT16  Value;
  NetCopyMem (&Value, Buf, sizeof (UINT16));
  return NTOHS (Value);
}

STATIC
VOID
TcpPutUint16 (
  IN UINT8  *Buf,
  IN UINT16 Data
  )
{
  Data = HTONS (Data);
  NetCopyMem (Buf, &Data, sizeof (UINT16));
}

STATIC
UINT32
TcpGetUint32 (
  IN UINT8 *Buf
  )
{
  UINT32  Value;
  NetCopyMem (&Value, Buf, sizeof (UINT32));
  return NTOHL (Value);
}

STATIC
VOID
TcpPutUint32 (
  IN UINT8  *Buf,
  IN UINT32 Data
  )
{
  Data = HTONL (Data);
  NetCopyMem (Buf, &Data, sizeof (UINT32));
}

UINT8
TcpComputeScale (
  IN TCP_CB *Tcb
  )
/*++

Routine Description:

  Compute the window scale value according to the given
  buffer size.

Arguments:

  Tcb - Pointer to the TCP_CB of this TCP instance.

Returns:

  UINT8  - The scale value.

--*/
{
  UINT8   Scale;
  UINT32  BufSize;

  ASSERT (Tcb && Tcb->Sk);

  BufSize = GET_RCV_BUFFSIZE (Tcb->Sk);

  Scale   = 0;
  while ((Scale < TCP_OPTION_MAX_WS) &&
         ((UINT32) (TCP_OPTION_MAX_WIN << Scale) < BufSize)) {

    Scale++;
  }

  return Scale;
}

UINT16
TcpSynBuildOption (
  IN TCP_CB  *Tcb,
  IN NET_BUF *Nbuf
  )
/*++

Routine Description:

  Build the TCP option in three-way handshake.

Arguments:

  Tcb   - Pointer to the TCP_CB of this TCP instance.
  Nbuf  - Pointer to the buffer to store the options.

Returns:

  The total length of the TCP option field.

--*/
{
  char    *Data;
  UINT16  Len;

  ASSERT (Tcb && Nbuf && !Nbuf->Tcp);

  Len = 0;

  //
  // Add timestamp option if not disabled by application
  // and it is the first SYN segment or the peer has sent
  // us its timestamp.
  //
  if (!TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_NO_TS) &&
      (!TCP_FLG_ON (TCPSEG_NETBUF (Nbuf)->Flag, TCP_FLG_ACK) ||
      TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_RCVD_TS))) {

    Data = NetbufAllocSpace (
            Nbuf,
            TCP_OPTION_TS_ALIGNED_LEN,
            NET_BUF_HEAD
            );

    ASSERT (Data);
    Len += TCP_OPTION_TS_ALIGNED_LEN;

    TcpPutUint32 (Data, TCP_OPTION_TS_FAST);
    TcpPutUint32 (Data + 4, mTcpTick);
    TcpPutUint32 (Data + 8, 0);
  }

  //
  // Build window scale option, only when are configured
  // to send WS option, and either we are doing active
  // open or we have received WS option from peer.
  //
  if (!TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_NO_WS) &&
      (!TCP_FLG_ON (TCPSEG_NETBUF (Nbuf)->Flag, TCP_FLG_ACK) ||
      TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_RCVD_WS))) {

    Data = NetbufAllocSpace (
            Nbuf,
            TCP_OPTION_WS_ALIGNED_LEN,
            NET_BUF_HEAD
            );

    ASSERT (Data);

    Len += TCP_OPTION_WS_ALIGNED_LEN;
    TcpPutUint32 (Data, TCP_OPTION_WS_FAST | TcpComputeScale (Tcb));
  }

  //
  // Build MSS option
  //
  Data = NetbufAllocSpace (Nbuf, TCP_OPTION_MSS_LEN, 1);
  ASSERT (Data);

  Len += TCP_OPTION_MSS_LEN;
  TcpPutUint32 (Data, TCP_OPTION_MSS_FAST | Tcb->RcvMss);

  return Len;
}

UINT16
TcpBuildOption (
  IN TCP_CB  *Tcb,
  IN NET_BUF *Nbuf
  )
/*++

Routine Description:

  Build the TCP option in synchronized states.

Arguments:

  Tcb   - Pointer to the TCP_CB of this TCP instance.
  Nbuf  - Pointer to the buffer to store the options.

Returns:

  The total length of the TCP option field.

--*/
{
  char    *Data;
  UINT16  Len;

  ASSERT (Tcb && Nbuf && !Nbuf->Tcp);
  Len = 0;

  //
  // Build Timestamp option
  //
  if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_SND_TS) &&
      !TCP_FLG_ON (TCPSEG_NETBUF (Nbuf)->Flag, TCP_FLG_RST)) {

    Data = NetbufAllocSpace (
            Nbuf,
            TCP_OPTION_TS_ALIGNED_LEN,
            NET_BUF_HEAD
            );

    ASSERT (Data);
    Len += TCP_OPTION_TS_ALIGNED_LEN;

    TcpPutUint32 (Data, TCP_OPTION_TS_FAST);
    TcpPutUint32 (Data + 4, mTcpTick);
    TcpPutUint32 (Data + 8, Tcb->TsRecent);
  }

  return Len;
}

INTN
TcpParseOption (
  IN TCP_HEAD   *Tcp,
  IN TCP_OPTION *Option
  )
/*++

Routine Description:

  Parse the supported options.

Arguments:

  Tcp     - Pointer to the TCP_CB of this TCP instance.
  Option  - Pointer to the TCP_OPTION used to store the
            successfully pasrsed options.

Returns:

  0       - The options are successfully pasrsed.
  -1      - Ilegal option was found.

--*/
{
  UINT8 *Head;
  UINT8 TotalLen;
  UINT8 Cur;
  UINT8 Type;
  UINT8 Len;

  ASSERT (Tcp && Option);

  Option->Flag  = 0;

  TotalLen      = (Tcp->HeadLen << 2) - sizeof (TCP_HEAD);
  if (TotalLen <= 0) {
    return 0;
  }

  Head = (UINT8 *) (Tcp + 1);

  //
  // Fast process of timestamp option
  //
  if ((TotalLen == TCP_OPTION_TS_ALIGNED_LEN) &&
      (TcpGetUint32 (Head) == TCP_OPTION_TS_FAST)) {

    Option->TSVal = TcpGetUint32 (Head + 4);
    Option->TSEcr = TcpGetUint32 (Head + 8);
    Option->Flag  = TCP_OPTION_RCVD_TS;

    return 0;
  }

  //
  // Slow path to process the options.
  //
  Cur = 0;

  while (Cur < TotalLen) {
    Type = Head[Cur];

    switch (Type) {
    case TCP_OPTION_MSS:
      Len = Head[Cur + 1];

      if ((Len != TCP_OPTION_MSS_LEN) ||
          (TotalLen - Cur < TCP_OPTION_MSS_LEN)) {

        return -1;
      }

      Option->Mss = TcpGetUint16 (&Head[Cur + 2]);
      TCP_SET_FLG (Option->Flag, TCP_OPTION_RCVD_MSS);

      Cur += TCP_OPTION_MSS_LEN;
      break;

    case TCP_OPTION_WS:
      Len = Head[Cur + 1];

      if ((Len != TCP_OPTION_WS_LEN) ||
          (TotalLen - Cur < TCP_OPTION_WS_LEN)) {

        return -1;
      }

      Option->WndScale = NET_MIN (14, Head[Cur + 2]);
      TCP_SET_FLG (Option->Flag, TCP_OPTION_RCVD_WS);

      Cur += TCP_OPTION_WS_LEN;
      break;

    case TCP_OPTION_TS:
      Len = Head[Cur + 1];

      if ((Len != TCP_OPTION_TS_LEN) ||
          (TotalLen - Cur < TCP_OPTION_TS_LEN)) {

        return -1;
      }

      Option->TSVal = TcpGetUint32 (&Head[Cur + 2]);
      Option->TSEcr = TcpGetUint32 (&Head[Cur + 6]);
      TCP_SET_FLG (Option->Flag, TCP_OPTION_RCVD_TS);

      Cur += TCP_OPTION_TS_LEN;
      break;

    case TCP_OPTION_NOP:
      Cur++;
      break;

    case TCP_OPTION_EOP:
      Cur = TotalLen;
      break;

    default:
      Len = Head[Cur + 1];

      if (TotalLen - Cur < Len || Len < 2) {
        return -1;
      }

      Cur = Cur + Len;
      break;
    }

  }

  return 0;
}

UINT32
TcpPawsOK (
  IN TCP_CB *Tcb,
  IN UINT32 TSVal
  )
/*++

Routine Description:

  Check the segment against PAWS.

Arguments:

  Tcb   - Pointer to the TCP_CB of this TCP instance.
  TSVal - The timestamp value.

Returns:

  1     - The segment passed the PAWS check.
  0     - The segment failed to pass the PAWS check.

--*/
{
  //
  // PAWS as defined in RFC1323, buggy...
  //
  if (TCP_TIME_LT (TSVal, Tcb->TsRecent) &&
      TCP_TIME_LT (Tcb->TsRecentAge + TCP_PAWS_24DAY, mTcpTick)) {

    return 0;

  }

  return 1;
}
