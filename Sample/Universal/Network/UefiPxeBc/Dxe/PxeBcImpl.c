/*++

Copyright (c) 2007 - 2009, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED. 

Module Name:

  PxeBcImpl.c
 
Abstract:

  Interface routines for PxeBc

--*/

#include "PxeBcImpl.h"

UINT32  mPxeDhcpTimeout[4] = { 4, 8, 16, 32 };

EFI_STATUS
EFIAPI
EfiPxeBcStart (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN BOOLEAN                          UseIpv6
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This    - GC_NOTO: add argument description
  UseIpv6 - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_ALREADY_STARTED - GC_NOTO: Add description for return value
  EFI_UNSUPPORTED - GC_NOTO: Add description for return value
  EFI_SUCCESS - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;
  EFI_STATUS              Status;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;

  if (Mode->Started) {
    return EFI_ALREADY_STARTED;
  }

  if (UseIpv6) {
    //
    // IPv6 is not supported now.
    //
    return EFI_UNSUPPORTED;
  }

  //
  // Configure the udp4 instance to let it receive data
  //
  Status = Private->Udp4Read->Configure (Private->Udp4Read, &Private->Udp4CfgData);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Private->AddressIsOk = FALSE;

  EfiZeroMem (Mode, sizeof (EFI_PXE_BASE_CODE_MODE));

  Mode->Started = TRUE;
  Mode->TTL     = DEFAULT_TTL;
  Mode->ToS     = DEFAULT_ToS;
  Mode->AutoArp = TRUE;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EfiPxeBcStop (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This  - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_NOT_STARTED - GC_NOTO: Add description for return value
  EFI_SUCCESS - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  Mode->Started = FALSE;

  Private->CurrentUdpSrcPort = 0;
  Private->Udp4Write->Configure (Private->Udp4Write, NULL);
  Private->Udp4Read->Groups (Private->Udp4Read, FALSE, NULL);
  Private->Udp4Read->Configure (Private->Udp4Read, NULL);

  Private->Dhcp4->Stop (Private->Dhcp4);
  Private->Dhcp4->Configure (Private->Dhcp4, NULL);

  Private->FileSize = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EfiPxeBcDhcp (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN BOOLEAN                          SortOffers
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This        - GC_NOTO: add argument description
  SortOffers  - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_NOT_STARTED - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;
  EFI_DHCP4_PROTOCOL      *Dhcp4;
  EFI_DHCP4_CONFIG_DATA   Dhcp4CfgData;
  EFI_DHCP4_MODE_DATA     Dhcp4Mode;
  EFI_DHCP4_PACKET_OPTION *OptList[PXEBC_DHCP4_MAX_OPTION_NUM];
  UINT32                  OptCount;
  EFI_STATUS              Status;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status              = EFI_SUCCESS;
  Private             = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode                = Private->PxeBc.Mode;
  Dhcp4               = Private->Dhcp4;
  Private->Function   = EFI_PXE_BASE_CODE_FUNCTION_DHCP;
  Private->SortOffers = SortOffers;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }
  //
  // Initialize the DHCP options and build the option list
  //
  OptCount = PxeBcBuildDhcpOptions (Private, OptList, TRUE);

  //
  // Set the DHCP4 config data.
  // The four discovery timeouts are 4, 8, 16, 32 seconds respectively.
  //
  NetZeroMem (&Dhcp4CfgData, sizeof (EFI_DHCP4_CONFIG_DATA));
  Dhcp4CfgData.OptionCount      = OptCount;
  Dhcp4CfgData.OptionList       = OptList;
  Dhcp4CfgData.Dhcp4Callback    = PxeBcDhcpCallBack;
  Dhcp4CfgData.CallbackContext  = Private;
  Dhcp4CfgData.DiscoverTryCount = 4;
  Dhcp4CfgData.DiscoverTimeout  = mPxeDhcpTimeout;
  
  Status = Dhcp4->Configure (Dhcp4, &Dhcp4CfgData);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }
  
  //
  // Zero those arrays to record the varies numbers of DHCP OFFERS.
  //
  Private->GotProxyOffer = FALSE;
  Private->NumOffers     = 0;
  Private->BootpIndex    = 0;
  NetZeroMem (Private->ServerCount, sizeof (Private->ServerCount));
  NetZeroMem (Private->ProxyIndex, sizeof (Private->ProxyIndex));
  
  Status = Dhcp4->Start (Dhcp4, NULL);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }
  
  Status = Dhcp4->GetModeData (Dhcp4, &Dhcp4Mode);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }
  
  ASSERT (Dhcp4Mode.State == Dhcp4Bound);
  
  NetCopyMem (&Private->StationIp, &Dhcp4Mode.ClientAddress, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Private->SubnetMask, &Dhcp4Mode.SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Private->GatewayIp, &Dhcp4Mode.RouterAddress, sizeof (EFI_IPv4_ADDRESS));
  
  NetCopyMem (&Mode->StationIp, &Private->StationIp, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Mode->SubnetMask, &Private->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  
  //
  // Check the selected offer to see whether BINL is required, if no or BINL is
  // finished, set the various Mode members.
  //
  Status = PxeBcCheckSelectedOffer (Private);
  if (!EFI_ERROR (Status)) {
    goto ON_EXIT;
  }
  
ON_EXIT:
  if (EFI_ERROR (Status)) {
    Dhcp4->Stop (Dhcp4);
    Dhcp4->Configure (Dhcp4, NULL);
  } else {
    //
    // Remove the previously configured option list and callback function
    //
    NetZeroMem (&Dhcp4CfgData, sizeof (EFI_DHCP4_CONFIG_DATA));
    Dhcp4->Configure (Dhcp4, &Dhcp4CfgData);

    Private->AddressIsOk = TRUE;
  }

  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeBcDiscover (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN UINT16                           Type,
  IN UINT16                           *Layer,
  IN BOOLEAN                          UseBis,
  IN EFI_PXE_BASE_CODE_DISCOVER_INFO  *Info   OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This        - GC_NOTO: add argument description
  Type        - GC_NOTO: add argument description
  Layer       - GC_NOTO: add argument description
  UseBis      - GC_NOTO: add argument description
  Info        - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_NOT_STARTED - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA              *Private;
  EFI_PXE_BASE_CODE_MODE          *Mode;
  EFI_DHCP4_PROTOCOL              *Dhcp4;
  EFI_PXE_BASE_CODE_DISCOVER_INFO DefaultInfo;
  EFI_PXE_BASE_CODE_SRVLIST       *SrvList;
  EFI_PXE_BASE_CODE_SRVLIST       DefaultSrvList;
  PXEBC_CACHED_DHCP4_PACKET       *Packet;
  PXEBC_VENDOR_OPTION            *VendorOpt;
  UINT16                          Index;
  EFI_STATUS                      Status;
  PXEBC_BOOT_SVR_ENTRY            *BootSvrEntry;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private           = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode              = Private->PxeBc.Mode;
  Dhcp4             = Private->Dhcp4;
  BootSvrEntry      = NULL;
  SrvList           = NULL;
  Status            = EFI_DEVICE_ERROR;
  Private->Function = EFI_PXE_BASE_CODE_FUNCTION_DISCOVER;

  if (!Private->AddressIsOk) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  //
  // If layer isn't EFI_PXE_BASE_CODE_BOOT_LAYER_INITIAL,
  //   use the previous setting;
  // If info isn't offered,
  //   use the cached DhcpAck and ProxyOffer packets.
  //
  if (*Layer != EFI_PXE_BASE_CODE_BOOT_LAYER_INITIAL) {

    if (!Mode->PxeDiscoverValid || !Mode->PxeReplyReceived || (!Mode->PxeBisReplyReceived && UseBis)) {

      return EFI_INVALID_PARAMETER;
    }

    DefaultInfo.IpCnt                 = 1;
    DefaultInfo.UseUCast              = TRUE;

    DefaultSrvList.Type               = Type;
    DefaultSrvList.AcceptAnyResponse  = FALSE;
    DefaultSrvList.IpAddr.Addr[0]     = Private->ServerIp.Addr[0];

    SrvList = &DefaultSrvList;
    Info = &DefaultInfo;
  } else if (Info == NULL) {
    //
    // Create info by the cached packet before
    //
    Packet    = (Mode->ProxyOfferReceived) ? &Private->ProxyOffer : &Private->Dhcp4Ack;
    VendorOpt = &Packet->PxeVendorOption;

    if (!Mode->DhcpAckReceived || !IS_VALID_DISCOVER_VENDOR_OPTION (VendorOpt->BitMap)) {
      //
      // Address is not acquired or no discovery options.
      //
      return EFI_INVALID_PARAMETER;
    }

    DefaultInfo.UseMCast    = (BOOLEAN)!IS_DISABLE_MCAST_DISCOVER (VendorOpt->DiscoverCtrl);
    DefaultInfo.UseBCast    = (BOOLEAN)!IS_DISABLE_BCAST_DISCOVER (VendorOpt->DiscoverCtrl);
    DefaultInfo.MustUseList = (BOOLEAN) IS_ENABLE_USE_SERVER_LIST (VendorOpt->DiscoverCtrl);
    DefaultInfo.UseUCast    = DefaultInfo.MustUseList;

    if (DefaultInfo.UseMCast) {
      //
      // Get the multicast discover ip address from vendor option.
      //
      NetCopyMem (&DefaultInfo.ServerMCastIp.Addr, &VendorOpt->DiscoverMcastIp, sizeof (EFI_IPv4_ADDRESS));
    }

    DefaultInfo.IpCnt = 0;

    if (DefaultInfo.MustUseList) {
      BootSvrEntry  = VendorOpt->BootSvr;
      Status        = EFI_INVALID_PARAMETER;

      while (((UINT8) (BootSvrEntry - VendorOpt->BootSvr)) < VendorOpt->BootSvrLen) {

        if (BootSvrEntry->Type == HTONS (Type)) {
          Status = EFI_SUCCESS;
          break;
        }

        BootSvrEntry = GET_NEXT_BOOT_SVR_ENTRY (BootSvrEntry);
      }

      if (EFI_ERROR (Status)) {
        return Status;
      }

      DefaultInfo.IpCnt = BootSvrEntry->IpCnt;
    }

    Info = &DefaultInfo;
  } else {

    SrvList = Info->SrvList;

    if (!SrvList[0].AcceptAnyResponse) {

      for (Index = 1; Index < Info->IpCnt; Index++) {
        if (SrvList[Index].AcceptAnyResponse) {
          break;
        }
      }

      if (Index != Info->IpCnt) {
        return EFI_INVALID_PARAMETER;
      }
    }
  }

  if ((!Info->UseUCast && !Info->UseBCast && !Info->UseMCast) || (Info->MustUseList && Info->IpCnt == 0)) {

    return EFI_INVALID_PARAMETER;
  }
  //
  // Execute discover by UniCast/BroadCast/MultiCast
  //
  if (Info->UseUCast) {

    for (Index = 0; Index < Info->IpCnt; Index++) {

      if (BootSvrEntry == NULL) {
        Private->ServerIp.Addr[0] = SrvList[Index].IpAddr.Addr[0];
      } else {
        NetCopyMem (&Private->ServerIp, &BootSvrEntry->IpAddr[Index], sizeof (EFI_IPv4_ADDRESS));
      }

      Status = PxeBcDiscvBootService (
                Private,
                Type,
                Layer,
                UseBis,
                &SrvList[Index].IpAddr,
                0,
                NULL,
                TRUE,
                &Private->PxeReply.Packet.Ack
                );
    }

  } else if (Info->UseMCast) {

    Status = PxeBcDiscvBootService (
              Private,
              Type,
              Layer,
              UseBis,
              &Info->ServerMCastIp,
              0,
              NULL,
              TRUE,
              &Private->PxeReply.Packet.Ack
              );

  } else if (Info->UseBCast) {

    Status = PxeBcDiscvBootService (
              Private,
              Type,
              Layer,
              UseBis,
              NULL,
              Info->IpCnt,
              SrvList,
              TRUE,
              &Private->PxeReply.Packet.Ack
              );
  }

  if (EFI_ERROR (Status) || !Mode->PxeReplyReceived || (!Mode->PxeBisReplyReceived && UseBis)) {

    Status = EFI_DEVICE_ERROR;
  } else {
    PxeBcParseCachedDhcpPacket (&Private->PxeReply);
  }

  if (Mode->PxeBisReplyReceived) {
    NetCopyMem (&Private->ServerIp, &Mode->PxeReply.Dhcpv4.BootpSiAddr, sizeof (EFI_IPv4_ADDRESS));
  }

  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeBcMtftp (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN EFI_PXE_BASE_CODE_TFTP_OPCODE    Operation,
  IN OUT VOID                         *BufferPtr,
  IN BOOLEAN                          Overwrite,
  IN OUT UINT64                       *BufferSize,
  IN UINTN                            *BlockSize    OPTIONAL,
  IN EFI_IP_ADDRESS                   *ServerIp,
  IN UINT8                            *Filename,
  IN EFI_PXE_BASE_CODE_MTFTP_INFO     *Info         OPTIONAL,
  IN BOOLEAN                          DontUseBuffer
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This          - GC_NOTO: add argument description
  Operation     - GC_NOTO: add argument description
  BufferPtr     - GC_NOTO: add argument description
  Overwrite     - GC_NOTO: add argument description
  BufferSize    - GC_NOTO: add argument description
  BlockSize     - GC_NOTO: add argument description
  ServerIp      - GC_NOTO: add argument description
  Filename      - GC_NOTO: add argument description
  Info          - GC_NOTO: add argument description
  DontUseBuffer - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;
  EFI_MTFTP4_PROTOCOL     *Mtftp4;
  EFI_MTFTP4_CONFIG_DATA  Mtftp4Config;
  EFI_STATUS              Status;

  if ((This == NULL) ||
      (Filename == NULL) ||
      (BufferSize == NULL) ||
      ((ServerIp == NULL) || !Ip4IsUnicast (NTOHL (ServerIp->Addr[0]), 0)) ||
      ((BufferPtr == NULL) && DontUseBuffer) ||
      ((BlockSize != NULL) && (*BlockSize < 512))) {

    return EFI_INVALID_PARAMETER;
  }

  Status  = EFI_DEVICE_ERROR;
  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;
  Mtftp4  = Private->Mtftp4;

  Mtftp4Config.UseDefaultSetting = FALSE;
  Mtftp4Config.TimeoutValue      = PXEBC_MTFTP_TIMEOUT;
  Mtftp4Config.TryCount          = PXEBC_MTFTP_RETRIES;

  NetCopyMem (&Mtftp4Config.StationIp, &Private->StationIp, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Mtftp4Config.SubnetMask, &Private->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Mtftp4Config.GatewayIp, &Private->GatewayIp, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Mtftp4Config.ServerIp, ServerIp, sizeof (EFI_IPv4_ADDRESS));

  switch (Operation) {

  case EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE:

    Status = PxeBcTftpGetFileSize (
              Private,
              &Mtftp4Config,
              Filename,
              BlockSize,
              BufferSize
              );

    break;

  case EFI_PXE_BASE_CODE_TFTP_READ_FILE:

    Status = PxeBcTftpReadFile (
              Private,
              &Mtftp4Config,
              Filename,
              BlockSize,
              BufferPtr,
              BufferSize,
              DontUseBuffer
              );

    break;

  case EFI_PXE_BASE_CODE_TFTP_WRITE_FILE:

    Status = PxeBcTftpWriteFile (
              Private,
              &Mtftp4Config,
              Filename,
              Overwrite,
              BlockSize,
              BufferPtr,
              BufferSize
              );

    break;

  case EFI_PXE_BASE_CODE_TFTP_READ_DIRECTORY:

    Status = PxeBcTftpReadDirectory (
              Private,
              &Mtftp4Config,
              Filename,
              BlockSize,
              BufferPtr,
              BufferSize,
              DontUseBuffer
              );

    break;

  case EFI_PXE_BASE_CODE_MTFTP_GET_FILE_SIZE:
  case EFI_PXE_BASE_CODE_MTFTP_READ_FILE:
  case EFI_PXE_BASE_CODE_MTFTP_READ_DIRECTORY:
    Status = EFI_UNSUPPORTED;
    break;

  default:

    Status = EFI_INVALID_PARAMETER;
    break;
  }

  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeBcUdpWrite (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN UINT16                           OpFlags,
  IN EFI_IP_ADDRESS                   *DestIp,
  IN EFI_PXE_BASE_CODE_UDP_PORT       *DestPort,
  IN EFI_IP_ADDRESS                   *GatewayIp  OPTIONAL,
  IN EFI_IP_ADDRESS                   *SrcIp      OPTIONAL,
  IN OUT EFI_PXE_BASE_CODE_UDP_PORT   *SrcPort    OPTIONAL,
  IN UINTN                            *HeaderSize OPTIONAL,
  IN VOID                             *HeaderPtr  OPTIONAL,
  IN UINTN                            *BufferSize,
  IN VOID                             *BufferPtr
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This        - GC_NOTO: add argument description
  OpFlags     - GC_NOTO: add argument description
  DestIp      - GC_NOTO: add argument description
  DestPort    - GC_NOTO: add argument description
  GatewayIp   - GC_NOTO: add argument description
  SrcIp       - GC_NOTO: add argument description
  SrcPort     - GC_NOTO: add argument description
  HeaderSize  - GC_NOTO: add argument description
  HeaderPtr   - GC_NOTO: add argument description
  BufferSize  - GC_NOTO: add argument description
  BufferPtr   - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_OUT_OF_RESOURCES - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA        *Private;
  EFI_PXE_BASE_CODE_MODE    *Mode;
  EFI_UDP4_PROTOCOL         *Udp4;
  EFI_UDP4_COMPLETION_TOKEN Token;
  EFI_UDP4_TRANSMIT_DATA    *Udp4TxData;
  UINT32                    FragCount;
  UINT32                    DataLength;
  EFI_UDP4_SESSION_DATA     Udp4Session;
  EFI_STATUS                Status;
  BOOLEAN                   IsDone;

  IsDone = FALSE;

  if ((This == NULL) || (DestIp == NULL) || (DestPort == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((GatewayIp != NULL) && !Ip4IsUnicast (NTOHL (GatewayIp->Addr[0]), 0)) {
    //
    // Gateway is provided but it's not a unicast IP address.
    //
    return EFI_INVALID_PARAMETER;
  }

  if ((HeaderSize != NULL) && ((*HeaderSize == 0) || (HeaderPtr == NULL))) {
    //
    // The HeaderSize ptr isn't NULL and: 1. the value is zero; or 2. the HeaderPtr
    // is NULL.
    //
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize == NULL) || ((*BufferSize != 0) && (BufferPtr == NULL))) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;
  Udp4    = Private->Udp4Write;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  if (!Private->AddressIsOk && (SrcIp == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Private->CurrentUdpSrcPort == 0) ||
    ((SrcPort != NULL) && (*SrcPort != Private->CurrentUdpSrcPort))) {
    //
    // Port is changed, (re)configure the Udp4Write instance
    //
    if (SrcPort != NULL) {
      Private->CurrentUdpSrcPort = *SrcPort;
    }

    Status = PxeBcConfigureUdpWriteInstance (
               Udp4,
               &Private->StationIp.v4,
               &Private->SubnetMask.v4,
               &Private->GatewayIp.v4,
               &Private->CurrentUdpSrcPort
               );
    if (EFI_ERROR (Status)) {
      Private->CurrentUdpSrcPort = 0;
      return EFI_INVALID_PARAMETER;
    }
  }

  EfiZeroMem (&Token, sizeof (EFI_UDP4_COMPLETION_TOKEN));
  EfiZeroMem (&Udp4Session, sizeof (EFI_UDP4_SESSION_DATA));

  NetCopyMem (&Udp4Session.DestinationAddress, DestIp, sizeof (EFI_IPv4_ADDRESS));
  Udp4Session.DestinationPort = *DestPort;

  if (SrcIp != NULL) {
    NetCopyMem (&Udp4Session.SourceAddress, SrcIp, sizeof (EFI_IPv4_ADDRESS));
  }

  if (SrcPort != NULL) {
    Udp4Session.SourcePort = *SrcPort;
  }

  FragCount = (HeaderSize != NULL) ? 2 : 1;
  Udp4TxData = (EFI_UDP4_TRANSMIT_DATA *) NetAllocateZeroPool (sizeof (EFI_UDP4_TRANSMIT_DATA) + (FragCount - 1) * sizeof (EFI_UDP4_FRAGMENT_DATA));
  if (Udp4TxData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Udp4TxData->FragmentCount = FragCount;
  Udp4TxData->FragmentTable[FragCount - 1].FragmentLength = (UINT32) *BufferSize;
  Udp4TxData->FragmentTable[FragCount - 1].FragmentBuffer = BufferPtr;
  DataLength = (UINT32) *BufferSize;

  if (FragCount == 2) {

    Udp4TxData->FragmentTable[0].FragmentLength = (UINT32) *HeaderSize;
    Udp4TxData->FragmentTable[0].FragmentBuffer = HeaderPtr;
    DataLength += (UINT32) *HeaderSize;
  }

  if (GatewayIp != NULL) {
    Udp4TxData->GatewayAddress  = (EFI_IPv4_ADDRESS *) GatewayIp;
  }

  Udp4TxData->UdpSessionData  = &Udp4Session;
  Udp4TxData->DataLength      = DataLength;
  Token.Packet.TxData         = Udp4TxData;

  Status = gBS->CreateEvent (
                  EFI_EVENT_NOTIFY_SIGNAL,
                  NET_TPL_EVENT,
                  PxeBcCommonNotify,
                  &IsDone,
                  &Token.Event
                  );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Status = Udp4->Transmit (Udp4, &Token);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  while (!IsDone) {

    Udp4->Poll (Udp4);
  }

  Status = Token.Status;

ON_EXIT:

  if (Token.Event != NULL) {
    gBS->CloseEvent (Token.Event);
  }

  NetFreePool (Udp4TxData);

  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeBcUdpRead (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN UINT16                           OpFlags,
  IN OUT EFI_IP_ADDRESS               *DestIp, OPTIONAL
  IN OUT EFI_PXE_BASE_CODE_UDP_PORT   *DestPort, OPTIONAL
  IN OUT EFI_IP_ADDRESS               *SrcIp, OPTIONAL
  IN OUT EFI_PXE_BASE_CODE_UDP_PORT   *SrcPort, OPTIONAL
  IN UINTN                            *HeaderSize, OPTIONAL
  IN VOID                             *HeaderPtr, OPTIONAL
  IN OUT UINTN                        *BufferSize,
  IN VOID                             *BufferPtr
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This        - GC_NOTO: add argument description
  OpFlags     - GC_NOTO: add argument description
  DestIp      - GC_NOTO: add argument description
  DestPort    - GC_NOTO: add argument description
  SrcIp       - GC_NOTO: add argument description
  SrcPort     - GC_NOTO: add argument description
  HeaderSize  - GC_NOTO: add argument description
  HeaderPtr   - GC_NOTO: add argument description
  BufferSize  - GC_NOTO: add argument description
  BufferPtr   - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_NOT_STARTED - GC_NOTO: Add description for return value
  EFI_OUT_OF_RESOURCES - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA        *Private;
  EFI_PXE_BASE_CODE_MODE    *Mode;
  EFI_UDP4_PROTOCOL         *Udp4;
  EFI_UDP4_COMPLETION_TOKEN Token;
  EFI_UDP4_RECEIVE_DATA     *RxData;
  EFI_UDP4_SESSION_DATA     *Session;
  EFI_STATUS                Status;
  BOOLEAN                   IsDone;
  BOOLEAN                   Matched;
  UINTN                     CopyLen;

  if (This == NULL || DestIp == NULL || DestPort == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_USE_FILTER) {
    return EFI_UNSUPPORTED;
  }

  if ((!(OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_DEST_PORT) && (DestPort == NULL)) ||
      (!(OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_DEST_PORT) && (SrcIp == NULL)) ||
      (!(OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_SRC_PORT) && (SrcPort == NULL))) {
    return EFI_INVALID_PARAMETER;
  }

  if (((HeaderSize != NULL) && (*HeaderSize == 0)) || ((HeaderSize != NULL) && (HeaderPtr == NULL))) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize == NULL) || ((BufferPtr == NULL) && (*BufferSize != 0))) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;
  Udp4    = Private->Udp4Read;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  Mode->IcmpErrorReceived = FALSE;

  Status = gBS->CreateEvent (
                  EFI_EVENT_NOTIFY_SIGNAL,
                  NET_TPL_EVENT,
                  PxeBcCommonNotify,
                  &IsDone,
                  &Token.Event
                  );
  if (EFI_ERROR (Status)) {
    return EFI_OUT_OF_RESOURCES;
  }

TRY_AGAIN:

  IsDone = FALSE;
  Status = Udp4->Receive (Udp4, &Token);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_ICMP_ERROR) {
      Mode->IcmpErrorReceived = TRUE;
    }
    goto ON_EXIT;
  }

  Udp4->Poll (Udp4);

  if (!IsDone) {
    Status = EFI_TIMEOUT;
  } else {

    //
    // check whether this packet matches the filters
    //
    if (EFI_ERROR (Token.Status)){
      goto ON_EXIT;
    }

    RxData  = Token.Packet.RxData;
    Session = &RxData->UdpSession;

    Matched = TRUE;

    if (OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_USE_FILTER) {
      Matched = FALSE;
      //
      // Check UDP package by IP filter settings
      //
      if (PxeBcCheckIpByFilter (Mode, Session)) {
        Matched = TRUE;
      }
    }
	
    if (Matched) {
      Matched = FALSE;
	  
      //
      // Match the destination ip of the received udp dgram
      //
      if (OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_DEST_IP) {
        Matched = TRUE;
  
        if (DestIp != NULL) {
          NetCopyMem (DestIp, &Session->DestinationAddress, sizeof (EFI_IPv4_ADDRESS));
        }
      } else {
        if (DestIp != NULL) {
          if (EFI_IP4_EQUAL (*DestIp, Session->DestinationAddress)) {
            Matched = TRUE;
          }
        } else {
          if (EFI_IP4_EQUAL (Private->StationIp, Session->DestinationAddress)) {
            Matched = TRUE;
          }
        }
      }
    }
	
    if (Matched) {
      //
      // Match the destination port of the received udp dgram
      //
      if (OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_DEST_PORT) {

        if (DestPort != NULL) {
          *DestPort = Session->DestinationPort;
        }
      } else {

        if (*DestPort != Session->DestinationPort) {
          Matched = FALSE;
        }        
      }
    }

    if (Matched) {
      //
      // Match the source ip of the received udp dgram
      //
      if (OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_SRC_IP) {

        if (SrcIp != NULL) {
          NetCopyMem (SrcIp, &Session->SourceAddress, sizeof (EFI_IPv4_ADDRESS));
        }
      } else {

        if (!EFI_IP4_EQUAL (*SrcIp, Session->SourceAddress)) {
          Matched = FALSE;
        }
      }
    }

    if (Matched) {
      //
      // Match the source port of the received udp dgram
      //
      if (OpFlags & EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_SRC_PORT) {

        if (SrcPort != NULL) {
          *SrcPort = Session->SourcePort;
        }
      } else {

        if (*SrcPort != Session->SourcePort) {
          Matched = FALSE;
        }
      }
    }

    if (Matched) {

      CopyLen = 0;

      if (HeaderSize != NULL) {
        CopyLen = NET_MIN (*HeaderSize, RxData->DataLength);
        NetCopyMem (HeaderPtr, RxData->FragmentTable[0].FragmentBuffer, CopyLen);
        *HeaderSize = CopyLen;
      }

      if (RxData->DataLength - CopyLen > *BufferSize) {

        Status = EFI_BUFFER_TOO_SMALL;
      } else {

        *BufferSize = RxData->DataLength - CopyLen;
        NetCopyMem (BufferPtr, (UINT8 *) RxData->FragmentTable[0].FragmentBuffer + CopyLen, *BufferSize);
      }
    } else {

      Status = EFI_TIMEOUT;
    }

    //
    // Recycle the RxData
    //
    gBS->SignalEvent (RxData->RecycleSignal);

    if (!Matched) {
      goto TRY_AGAIN;
    }
  }

ON_EXIT:

  Udp4->Cancel (Udp4, &Token);

  gBS->CloseEvent (Token.Event);
 
  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeBcSetIpFilter (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN EFI_PXE_BASE_CODE_IP_FILTER      *NewFilter
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This      - GC_NOTO: add argument description
  NewFilter - GC_NOTO: add argument description

Returns:

  EFI_UNSUPPORTED - GC_NOTO: Add description for return value

--*/
{
  EFI_STATUS                Status;
  PXEBC_PRIVATE_DATA        *Private;
  EFI_PXE_BASE_CODE_MODE    *Mode;
  UINTN                     Index;
  BOOLEAN                   PromiscuousNeed;

  if (This == NULL || NewFilter == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode = Private->PxeBc.Mode;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  PromiscuousNeed = FALSE;
  for (Index = 0; Index < NewFilter->IpCnt; ++Index) {
    if (IP4_IS_LOCAL_BROADCAST (EFI_IP4 (NewFilter->IpList[Index].v4))) {
      //
      // The IP is a broadcast address.
      //
      DEBUG ((EFI_D_ERROR, "There is broadcast address in NewFilter.\n"));
      return EFI_INVALID_PARAMETER;
    }
    if (Ip4IsUnicast (EFI_IP4 (NewFilter->IpList[Index].v4), 0) &&
        (NewFilter->Filters & EFI_PXE_BASE_CODE_IP_FILTER_STATION_IP)
       ) {
      //
      // If EFI_PXE_BASE_CODE_IP_FILTER_STATION_IP is set and IP4 address is in IpList,
      // promiscuous mode is needed.
      //
      PromiscuousNeed = TRUE;
    }
  }

  //
  // Clear the UDP instance configuration, all joined groups will be left
  // during the operation.
  //
  Private->Udp4Read->Configure (Private->Udp4Read, NULL);
  Private->Udp4CfgData.AcceptPromiscuous  = FALSE;
  Private->Udp4CfgData.AcceptBroadcast    = FALSE;

  if (PromiscuousNeed ||
      NewFilter->Filters & EFI_PXE_BASE_CODE_IP_FILTER_PROMISCUOUS ||
      NewFilter->Filters & EFI_PXE_BASE_CODE_IP_FILTER_PROMISCUOUS_MULTICAST
     ) {
    //
    // Configure the udp4 filter to receive all packages
    //
    Private->Udp4CfgData.AcceptPromiscuous  = TRUE;

    //
    // Configure the UDP instance with the new configuration.
    //
    Status = Private->Udp4Read->Configure (Private->Udp4Read, &Private->Udp4CfgData);
    if (EFI_ERROR (Status)) {
      return Status;
    }

  } else {

    if (NewFilter->Filters & EFI_PXE_BASE_CODE_IP_FILTER_BROADCAST) {
      //
      // Configure the udp4 filter to receive all broadcast packages
      //
      Private->Udp4CfgData.AcceptBroadcast    = TRUE;
    }

    //
    // Configure the UDP instance with the new configuration.
    //
    Status = Private->Udp4Read->Configure (Private->Udp4Read, &Private->Udp4CfgData);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (NewFilter->Filters & EFI_PXE_BASE_CODE_IP_FILTER_STATION_IP) {

      for (Index = 0; Index < NewFilter->IpCnt; ++Index) {
        if (IP4_IS_MULTICAST (EFI_NTOHL (NewFilter->IpList[Index].v4))) {
          //
          // Join the mutilcast group
          //
          Status = Private->Udp4Read->Groups (Private->Udp4Read, TRUE, &NewFilter->IpList[Index].v4);
          if (EFI_ERROR (Status)) {
            return Status;
          }
        }
      }
    }
  }


  //
  // Save the new filter.
  //
  EfiCopyMem (&Mode->IpFilter, NewFilter, sizeof (Mode->IpFilter));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EfiPxeBcArp (
  IN EFI_PXE_BASE_CODE_PROTOCOL       * This,
  IN EFI_IP_ADDRESS                   * IpAddr,
  IN EFI_MAC_ADDRESS                  * MacAddr OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This    - GC_NOTO: add argument description
  IpAddr  - GC_NOTO: add argument description
  MacAddr - GC_NOTO: add argument description

Returns:

  EFI_UNSUPPORTED - GC_NOTO: Add description for return value

--*/
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
EfiPxeBcSetParameters (
  IN EFI_PXE_BASE_CODE_PROTOCOL       *This,
  IN BOOLEAN                          *NewAutoArp, OPTIONAL
  IN BOOLEAN                          *NewSendGUID, OPTIONAL
  IN UINT8                            *NewTTL, OPTIONAL
  IN UINT8                            *NewToS, OPTIONAL
  IN BOOLEAN                          *NewMakeCallback  // OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This        - GC_NOTO: add argument description
  NewAutoArp  - GC_NOTO: add argument description
  NewSendGUID - GC_NOTO: add argument description
  NewTTL      - GC_NOTO: add argument description
  NewToS      - GC_NOTO: add argument description
  NewMakeCallback   - GC_NOTO: add argument description

Returns:

  GC_NOTO: add return values

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;
  EFI_STATUS              Status;

  Status = EFI_SUCCESS;

  if (This == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto ON_EXIT;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;

  if (NewSendGUID != NULL && *NewSendGUID == TRUE) {
    //
    // FixMe, cann't locate SendGuid
    //
  }

  if (NewMakeCallback != NULL && *NewMakeCallback == TRUE) {

    Status = gBS->HandleProtocol (
                    Private->Controller,
                    &gEfiPxeBaseCodeCallbackProtocolGuid,
                    (VOID **) &Private->PxeBcCallback
                    );
    if (EFI_ERROR (Status) || (Private->PxeBcCallback->Callback == NULL)) {

      Status = EFI_INVALID_PARAMETER;
      goto ON_EXIT;
    }
  }

  if (!Mode->Started) {
    Status = EFI_NOT_STARTED;
    goto ON_EXIT;
  }

  if (NewMakeCallback != NULL) {

    if (*NewMakeCallback) {
      //
      // Update the Callback protocol.
      //
      Status = gBS->HandleProtocol (
                      Private->Controller,
                      &gEfiPxeBaseCodeCallbackProtocolGuid,
                      (VOID **) &Private->PxeBcCallback
                      );

      if (EFI_ERROR (Status) || (Private->PxeBcCallback->Callback == NULL)) {
        Status = EFI_INVALID_PARAMETER;
        goto ON_EXIT;
      }
    } else {
      Private->PxeBcCallback = NULL;
    }

    Mode->MakeCallbacks = *NewMakeCallback;
  }

  if (NewAutoArp != NULL) {
    Mode->AutoArp = *NewAutoArp;
  }

  if (NewSendGUID != NULL) {
    Mode->SendGUID = *NewSendGUID;
  }

  if (NewTTL != NULL) {
    Mode->TTL = *NewTTL;
  }

  if (NewToS != NULL) {
    Mode->ToS = *NewToS;
  }

ON_EXIT:
  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeBcSetStationIP (
  IN EFI_PXE_BASE_CODE_PROTOCOL       * This,
  IN EFI_IP_ADDRESS                   * NewStationIp, OPTIONAL
  IN EFI_IP_ADDRESS                   * NewSubnetMask OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This          - GC_NOTO: add argument description
  NewStationIp  - GC_NOTO: add argument description
  NewSubnetMask - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_NOT_STARTED - GC_NOTO: Add description for return value
  EFI_SUCCESS - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (NewStationIp != NULL && !Ip4IsUnicast (NTOHL (NewStationIp->Addr[0]), 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (NewSubnetMask != NULL && !IP4_IS_VALID_NETMASK (NTOHL (NewSubnetMask->Addr[0]))) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  if (NewStationIp != NULL) {
    Mode->StationIp = *NewStationIp;
  }

  if (NewSubnetMask != NULL) {
    Mode->SubnetMask = *NewSubnetMask;
  }

  Private->AddressIsOk = TRUE;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EfiPxeBcSetPackets (
  IN EFI_PXE_BASE_CODE_PROTOCOL       * This,
  IN BOOLEAN                          * NewDhcpDiscoverValid, OPTIONAL
  IN BOOLEAN                          * NewDhcpAckReceived, OPTIONAL
  IN BOOLEAN                          * NewProxyOfferReceived, OPTIONAL
  IN BOOLEAN                          * NewPxeDiscoverValid, OPTIONAL
  IN BOOLEAN                          * NewPxeReplyReceived, OPTIONAL
  IN BOOLEAN                          * NewPxeBisReplyReceived, OPTIONAL
  IN EFI_PXE_BASE_CODE_PACKET         * NewDhcpDiscover, OPTIONAL
  IN EFI_PXE_BASE_CODE_PACKET         * NewDhcpAck, OPTIONAL
  IN EFI_PXE_BASE_CODE_PACKET         * NewProxyOffer, OPTIONAL
  IN EFI_PXE_BASE_CODE_PACKET         * NewPxeDiscover, OPTIONAL
  IN EFI_PXE_BASE_CODE_PACKET         * NewPxeReply, OPTIONAL
  IN EFI_PXE_BASE_CODE_PACKET         * NewPxeBisReply OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This                    - GC_NOTO: add argument description
  NewDhcpDiscoverValid    - GC_NOTO: add argument description
  NewDhcpAckReceived      - GC_NOTO: add argument description
  NewProxyOfferReceived   - GC_NOTO: add argument description
  NewPxeDiscoverValid     - GC_NOTO: add argument description
  NewPxeReplyReceived     - GC_NOTO: add argument description
  NewPxeBisReplyReceived  - GC_NOTO: add argument description
  NewDhcpDiscover         - GC_NOTO: add argument description
  NewDhcpAck              - GC_NOTO: add argument description
  NewProxyOffer           - GC_NOTO: add argument description
  NewPxeDiscover          - GC_NOTO: add argument description
  NewPxeReply             - GC_NOTO: add argument description
  NewPxeBisReply          - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_NOT_STARTED - GC_NOTO: Add description for return value
  EFI_SUCCESS - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA      *Private;
  EFI_PXE_BASE_CODE_MODE  *Mode;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PXEBC_PRIVATE_DATA_FROM_PXEBC (This);
  Mode    = Private->PxeBc.Mode;

  if (!Mode->Started) {
    return EFI_NOT_STARTED;
  }

  Private->FileSize = 0;

  if (NewDhcpDiscoverValid != NULL) {
    Mode->DhcpDiscoverValid = *NewDhcpDiscoverValid;
  }

  if (NewDhcpAckReceived != NULL) {
    Mode->DhcpAckReceived = *NewDhcpAckReceived;
  }

  if (NewProxyOfferReceived != NULL) {
    Mode->ProxyOfferReceived = *NewProxyOfferReceived;
  }

  if (NewPxeDiscoverValid != NULL) {
    Mode->PxeDiscoverValid = *NewPxeDiscoverValid;
  }

  if (NewPxeReplyReceived != NULL) {
    Mode->PxeReplyReceived = *NewPxeReplyReceived;
  }

  if (NewPxeBisReplyReceived != NULL) {
    Mode->PxeBisReplyReceived = *NewPxeBisReplyReceived;
  }

  if (NewDhcpDiscover != NULL) {
    NetCopyMem (&Mode->DhcpDiscover, NewDhcpDiscover, sizeof (EFI_PXE_BASE_CODE_PACKET));
  }

  if (NewDhcpAck != NULL) {
    NetCopyMem (&Mode->DhcpAck, NewDhcpAck, sizeof (EFI_PXE_BASE_CODE_PACKET));
  }

  if (NewProxyOffer != NULL) {
    NetCopyMem (&Mode->ProxyOffer, NewProxyOffer, sizeof (EFI_PXE_BASE_CODE_PACKET));
  }

  if (NewPxeDiscover != NULL) {
    NetCopyMem (&Mode->PxeDiscover, NewPxeDiscover, sizeof (EFI_PXE_BASE_CODE_PACKET));
  }

  if (NewPxeReply != NULL) {
    NetCopyMem (&Mode->PxeReply, NewPxeReply, sizeof (EFI_PXE_BASE_CODE_PACKET));
  }

  if (NewPxeBisReply != NULL) {
    NetCopyMem (&Mode->PxeBisReply, NewPxeBisReply, sizeof (EFI_PXE_BASE_CODE_PACKET));
  }

  return EFI_SUCCESS;
}

EFI_PXE_BASE_CODE_PROTOCOL  mPxeBcProtocolTemplate = {
  EFI_PXE_BASE_CODE_INTERFACE_REVISION,
  EfiPxeBcStart,
  EfiPxeBcStop,
  EfiPxeBcDhcp,
  EfiPxeBcDiscover,
  EfiPxeBcMtftp,
  EfiPxeBcUdpWrite,
  EfiPxeBcUdpRead,
  EfiPxeBcSetIpFilter,
  EfiPxeBcArp,
  EfiPxeBcSetParameters,
  EfiPxeBcSetStationIP,
  EfiPxeBcSetPackets,
  NULL
};

EFI_PXE_BASE_CODE_CALLBACK_STATUS
EFIAPI
EfiPxeLoadFileCallback (
  IN EFI_PXE_BASE_CODE_CALLBACK_PROTOCOL  * This,
  IN EFI_PXE_BASE_CODE_FUNCTION           Function,
  IN BOOLEAN                              Received,
  IN UINT32                               PacketLength,
  IN EFI_PXE_BASE_CODE_PACKET             * PacketPtr OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This          - GC_NOTO: add argument description
  Function      - GC_NOTO: add argument description
  Received      - GC_NOTO: add argument description
  PacketLength  - GC_NOTO: add argument description
  PacketPtr     - GC_NOTO: add argument description

Returns:

  EFI_PXE_BASE_CODE_CALLBACK_STATUS_ABORT - GC_NOTO: Add description for return value
  EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE - GC_NOTO: Add description for return value
  EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE - GC_NOTO: Add description for return value
  EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE - GC_NOTO: Add description for return value
  EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE - GC_NOTO: Add description for return value

--*/
{
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;

  //
  // Catch Ctrl-C or ESC to abort.
  //
  Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

  if (!EFI_ERROR (Status)) {

    if (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == (0x1F & 'c')) {

      return EFI_PXE_BASE_CODE_CALLBACK_STATUS_ABORT;
    }
  }
  //
  // No print if receive packet
  //
  if (Received) {
    return EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE;
  }
  //
  // Print only for three functions
  //
  switch (Function) {

  case EFI_PXE_BASE_CODE_FUNCTION_MTFTP:
    //
    // Print only for open MTFTP packets, not every MTFTP packets
    //
    if (PacketLength != 0 && PacketPtr != NULL) {
      if (PacketPtr->Raw[0x1C] != 0x00 || PacketPtr->Raw[0x1D] != 0x01) {
        return EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE;
      }
    }
    break;

  case EFI_PXE_BASE_CODE_FUNCTION_DHCP:
  case EFI_PXE_BASE_CODE_FUNCTION_DISCOVER:
    break;

  default:
    return EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE;
  }

  if (PacketLength != 0 && PacketPtr != NULL) {
    //
    // Print '.' when transmit a packet
    //
    Aprint (".");

  }

  return EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE;
}

EFI_PXE_BASE_CODE_CALLBACK_PROTOCOL mPxeBcCallBackTemplate = {
  EFI_PXE_BASE_CODE_CALLBACK_INTERFACE_REVISION,
  EfiPxeLoadFileCallback
};

EFI_STATUS
DiscoverBootFile (
  IN     PXEBC_PRIVATE_DATA  *Private,
  IN OUT UINT64              *BufferSize,
  IN     VOID                *Buffer
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  Private     - GC_NOTO: add argument description
  BufferSize  - GC_NOTO: add argument description
  Buffer      - GC_NOTO: add argument description

Returns:

  GC_NOTO: add return values

--*/
{
  EFI_PXE_BASE_CODE_PROTOCOL  *PxeBc;
  EFI_PXE_BASE_CODE_MODE      *Mode;
  EFI_STATUS                  Status;
  UINT16                      Type;
  UINT16                      Layer;
  BOOLEAN                     UseBis;
  UINTN                       BlockSize;
  PXEBC_CACHED_DHCP4_PACKET   *Packet;
  UINT16                      Value;

  PxeBc = &Private->PxeBc;
  Mode  = PxeBc->Mode;
  Type  = EFI_PXE_BASE_CODE_BOOT_TYPE_BOOTSTRAP;
  Layer = EFI_PXE_BASE_CODE_BOOT_LAYER_INITIAL;

  //
  // do DHCP.
  //
  Status = PxeBc->Dhcp (PxeBc, TRUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  
  //
  // Select a boot server
  //
  Status = PxeBcSelectBootPrompt (Private);

  if (Status == EFI_SUCCESS) {
    Status = PxeBcSelectBootMenu (Private, &Type, TRUE);
  } else if (Status == EFI_TIMEOUT) {
    Status = PxeBcSelectBootMenu (Private, &Type, FALSE);
  }

  if (!EFI_ERROR (Status)) {

    if (Type == EFI_PXE_BASE_CODE_BOOT_TYPE_BOOTSTRAP) {
      // 
      // Local boot(PXE bootstrap server) need abort
      //
      return EFI_ABORTED;
    }

    UseBis  = Mode->BisSupported && Mode->BisDetected;
    Status  = PxeBc->Discover (PxeBc, Type, &Layer, UseBis, NULL);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  *BufferSize = 0;
  BlockSize   = 0x8000;

  //
  // Get bootfile name and (m)tftp server ip addresss
  //
  if (Mode->PxeReplyReceived) {
    Packet = &Private->PxeReply;
  } else if (Mode->ProxyOfferReceived) {
    Packet = &Private->ProxyOffer;
  } else {
    Packet = &Private->Dhcp4Ack;
  }

  //
  // Use siaddr in header, or use option 54 if siaddr is zero.
  //
  NetCopyMem (
    &Private->ServerIp, 
    &Packet->Packet.Offer.Dhcp4.Header.ServerAddr, 
    sizeof (EFI_IPv4_ADDRESS)
    );
  if (Private->ServerIp.Addr[0] == 0) {
    NetCopyMem (
      &Private->ServerIp,
      Packet->Dhcp4Option[PXEBC_DHCP4_TAG_INDEX_SERVER_ID]->Data,
      sizeof (EFI_IPv4_ADDRESS)
      );
  }

  ASSERT (Packet->Dhcp4Option[PXEBC_DHCP4_TAG_INDEX_BOOTFILE] != NULL);

  //
  // bootlfile name
  //
  Private->BootFileName = Packet->Dhcp4Option[PXEBC_DHCP4_TAG_INDEX_BOOTFILE]->Data;

  if (Packet->Dhcp4Option[PXEBC_DHCP4_TAG_INDEX_BOOTFILE_LEN] != NULL) {
    //
    // Already have the bootfile length option, compute the file size
    //
    NetCopyMem (&Value, Packet->Dhcp4Option[PXEBC_DHCP4_TAG_INDEX_BOOTFILE_LEN]->Data, sizeof (Value));
    Value       = NTOHS (Value);
    *BufferSize = 512 * Value;
    Status      = EFI_BUFFER_TOO_SMALL;
  } else {
    //
    // Get the bootfile size from tftp
    //
    Status = PxeBc->Mtftp (
                      PxeBc,
                      EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE,
                      Buffer,
                      FALSE,
                      BufferSize,
                      &BlockSize,
                      &Private->ServerIp,
                      Private->BootFileName,
                      NULL,
                      FALSE
                      );
  }

  Private->FileSize = (UINTN) *BufferSize;

  return Status;
}

EFI_STATUS
EFIAPI
EfiPxeLoadFile (
  IN EFI_LOAD_FILE_PROTOCOL           * This,
  IN EFI_DEVICE_PATH_PROTOCOL         * FilePath,
  IN BOOLEAN                          BootPolicy,
  IN OUT UINTN                        *BufferSize,
  IN VOID                             *Buffer OPTIONAL
  )
/*++

Routine Description:

  GC_NOTO: Add function description

Arguments:

  This        - GC_NOTO: add argument description
  FilePath    - GC_NOTO: add argument description
  BootPolicy  - GC_NOTO: add argument description
  BufferSize  - GC_NOTO: add argument description
  Buffer      - GC_NOTO: add argument description

Returns:

  EFI_INVALID_PARAMETER - GC_NOTO: Add description for return value
  EFI_UNSUPPORTED - GC_NOTO: Add description for return value

--*/
{
  PXEBC_PRIVATE_DATA          *Private;
  EFI_PXE_BASE_CODE_PROTOCOL  *PxeBc;
  EFI_PXE_BASE_CODE_MODE      *PxeBcMode;
  BOOLEAN                     NewMakeCallback;
  UINTN                       BlockSize;
  EFI_STATUS                  Status;
  UINT64                      TmpBufSize;

  Private         = PXEBC_PRIVATE_DATA_FROM_LOADFILE (This);
  PxeBc           = &Private->PxeBc;
  PxeBcMode       = PxeBc->Mode;
  NewMakeCallback = FALSE;
  BlockSize       = 0x8000;
  Status          = EFI_DEVICE_ERROR;

  if (This == NULL || BufferSize == NULL) {

    return EFI_INVALID_PARAMETER;
  }

  //
  // Only support BootPolicy
  //
  if (!BootPolicy) {
    return EFI_UNSUPPORTED;
  }

  Status = PxeBc->Start (PxeBc, FALSE);
  if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Private->Controller,
                  &gEfiPxeBaseCodeCallbackProtocolGuid,
                  (VOID **) &Private->PxeBcCallback
                  );
  if (Status == EFI_UNSUPPORTED) {

    Private->LoadFileCallback = mPxeBcCallBackTemplate;

    Status = gBS->InstallProtocolInterface (
                    &Private->Controller,
                    &gEfiPxeBaseCodeCallbackProtocolGuid,
                    EFI_NATIVE_INTERFACE,
                    &Private->LoadFileCallback
                    );

    NewMakeCallback = (BOOLEAN) (Status == EFI_SUCCESS);

    Status          = PxeBc->SetParameters (PxeBc, NULL, NULL, NULL, NULL, &NewMakeCallback);
    if (EFI_ERROR (Status)) {
      PxeBc->Stop (PxeBc);
      return Status;
    }
  }

  if (Private->FileSize == 0) {
    TmpBufSize  = 0;
    Status      = DiscoverBootFile (Private, &TmpBufSize, Buffer);

    if (sizeof (UINTN) < sizeof (UINT64) && (TmpBufSize > 0xFFFFFFFF)) {
      Status = EFI_DEVICE_ERROR;
    } else if (TmpBufSize > 0 && *BufferSize >= (UINTN) TmpBufSize && Buffer != NULL) {
      *BufferSize = (UINTN) TmpBufSize;
      Status = PxeBc->Mtftp (
                        PxeBc,
                        EFI_PXE_BASE_CODE_TFTP_READ_FILE,
                        Buffer,
                        FALSE,
                        &TmpBufSize,
                        &BlockSize,
                        &Private->ServerIp,
                        Private->BootFileName,
                        NULL,
                        FALSE
                        );
	} else if (TmpBufSize > 0) {
      *BufferSize = (UINTN) TmpBufSize;
      Status      = EFI_BUFFER_TOO_SMALL;
    }
  } else if (Buffer == NULL || Private->FileSize > *BufferSize) {
    *BufferSize = Private->FileSize;
    Status      = EFI_BUFFER_TOO_SMALL;
  } else {
    //
    // Download the file.
    //
    TmpBufSize = (UINT64) (*BufferSize);
    Status = PxeBc->Mtftp (
                      PxeBc,
                      EFI_PXE_BASE_CODE_TFTP_READ_FILE,
                      Buffer,
                      FALSE,
                      &TmpBufSize,
                      &BlockSize,
                      &Private->ServerIp,
                      Private->BootFileName,
                      NULL,
                      FALSE
                      );
  }
  //
  // If we added a callback protocol, now is the time to remove it.
  //
  if (NewMakeCallback) {

    NewMakeCallback = FALSE;

    PxeBc->SetParameters (PxeBc, NULL, NULL, NULL, NULL, &NewMakeCallback);

    gBS->UninstallProtocolInterface (
          Private->Controller,
          &gEfiPxeBaseCodeCallbackProtocolGuid,
          &Private->LoadFileCallback
          );
  }
  //
  // Check download status
  //
  switch (Status) {

  case EFI_SUCCESS:
    return EFI_SUCCESS;
    break;

  case EFI_BUFFER_TOO_SMALL:
    if (Buffer != NULL) {
      Aprint ("PXE-E05: Download buffer is smaller than requested file.\n");
    } else {
      return Status;
    }
    break;

  case EFI_DEVICE_ERROR:
    Aprint ("PXE-E07: Network device error.\n");
    break;

  case EFI_OUT_OF_RESOURCES:
    Aprint ("PXE-E09: Could not allocate I/O buffers.\n");
    break;

  case EFI_NO_MEDIA:
    Aprint ("PXE-E12: Could not detect network connection.\n");
    break;

  case EFI_NO_RESPONSE:
    Aprint ("PXE-E16: No offer received.\n");
    break;

  case EFI_TIMEOUT:
    Aprint ("PXE-E18: Server response timeout.\n");
    break;

  case EFI_ABORTED:
    Aprint ("PXE-E21: Remote boot cancelled.\n");
    break;

  case EFI_ICMP_ERROR:
    Aprint ("PXE-E22: Client received ICMP error from server.\n");
    break;

  case EFI_TFTP_ERROR:
    Aprint ("PXE-E23: Client received TFTP error from server.\n");
    break;

  default:
    Aprint ("PXE-E99: Unexpected network error.\n");
    break;
  }

  PxeBc->Stop (PxeBc);

  return Status;
}

EFI_LOAD_FILE_PROTOCOL  mLoadFileProtocolTemplate = { EfiPxeLoadFile };

