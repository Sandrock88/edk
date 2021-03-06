/*++

Copyright (c) 2004 - 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  idebus.c
    
Abstract: 
    

Revision History
  This module is modified from DXE\IDE module for Ide Contriller Init support

--*/

#include "idebus.h"

#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_SUB_CLASS_IDE       0x01

//
// IDE Bus Driver GUID
//
EFI_GUID gIDEBusDriverGuid = {
  0xe778c047, 0x2121, 0x4ad1, 0x82, 0x36, 0x9, 0x7, 0x40, 0x61, 0x9f, 0x5
};

//
// IDE Bus Driver Binding Protocol Instance
//
EFI_DRIVER_BINDING_PROTOCOL gIDEBusDriverBinding = {
  IDEBusDriverBindingSupported,
  IDEBusDriverBindingStart,
  IDEBusDriverBindingStop,
  0xa,
  NULL,
  NULL
};

EFI_DRIVER_ENTRY_POINT (IDEBusControllerDriverEntryPoint)

//
// ***********************************************************************************
// IDEBusControllerDriverEntryPoint
// ***********************************************************************************
//
EFI_STATUS
EFIAPI
IDEBusControllerDriverEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
/*++
  
  Routine Description:
  
  Arguments:
  
  Returns:
  
--*/
// TODO:    ImageHandle - add argument and description to function comment
// TODO:    SystemTable - add argument and description to function comment
{
  return INSTALL_ALL_DRIVER_PROTOCOLS_OR_PROTOCOLS2 (
          ImageHandle,
          SystemTable,
          &gIDEBusDriverBinding,
          ImageHandle,
          &gIDEBusComponentName,
          NULL,
          NULL
          );
}
//
// ***********************************************************************************
// IDEBusDriverBindingSupported
// ***********************************************************************************
//
EFI_STATUS
EFIAPI
IDEBusDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++
  
Routine Description:
  Register Driver Binding protocol for this driver.
  
Arguments:
  This   -- A pointer to the EFI_DRIVER_BINDING_PROTOCOL instance.
  ControllerHandle    -- The handle of the controller to test.
  RemainingDevicePath -- A pointer to the remaining portion of a device path.

Returns: 
  EFI_SUCCESS - Driver loaded.
  other       - Driver not loaded.
--*/
// TODO:    Controller - add argument and description to function comment
// TODO:    EFI_UNSUPPORTED - add return value to function comment
{
  EFI_STATUS                        Status;
  EFI_DEVICE_PATH_PROTOCOL          *ParentDevicePath;
  EFI_DEV_PATH                      *Node;
  EFI_IDE_CONTROLLER_INIT_PROTOCOL  *IdeInit;

  if (RemainingDevicePath != NULL) {
    Node = (EFI_DEV_PATH *) RemainingDevicePath;
    if (Node->DevPath.Type != MESSAGING_DEVICE_PATH ||
        Node->DevPath.SubType != MSG_ATAPI_DP ||
        DevicePathNodeLength(&Node->DevPath) != sizeof(ATAPI_DEVICE_PATH)) {
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **) &ParentDevicePath,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (Status == EFI_ALREADY_STARTED) {
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }
 
  //
  // Clsoe protocol, don't use device path protocol in the .Support() function
  //
  gBS->CloseProtocol (
        Controller,
        &gEfiDevicePathProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  //
  // Verify the Ide Controller Init Protocol, which installed by the
  // IdeController module.
  // Note 1: PciIo protocol has been opened BY_DRIVER by ide_init, so We can't
  //         open BY_DRIVER here) That's why we don't check pciio protocol
  // Note 2: ide_init driver check ide controller's pci config space, so we dont
  //         check here any more to save code size
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiIdeControllerInitProtocolGuid,
                  (VOID **) &IdeInit,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (Status == EFI_ALREADY_STARTED) {
    return EFI_SUCCESS;
  }

  //
  // If protocols were opened normally, closed it
  //
  gBS->CloseProtocol (
        Controller,
        &gEfiIdeControllerInitProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  return Status;
}

//
// ***********************************************************************************
// IDEBusDriverBindingStart
// ***********************************************************************************
//
EFI_STATUS
EFIAPI
IDEBusDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++

  Routine Description:
    Start this driver on Controller by detecting all disks and installing 
      BlockIo protocol on them.

  Arguments:
    This                - Protocol instance pointer.
    Controller          - Handle of device to bind driver to.
    RemainingDevicePath - Not used, always produce all possible children.

  Returns:
    EFI_SUCCESS         - This driver is added to ControllerHandle.
    EFI_ALREADY_STARTED - This driver is already running on ControllerHandle.
    other               - This driver does not support this device.

--*/
{
  EFI_STATUS                        Status;
  EFI_STATUS                        SavedStatus;
  EFI_PCI_IO_PROTOCOL               *PciIo;
  EFI_DEVICE_PATH_PROTOCOL          *ParentDevicePath;
  EFI_DEV_PATH                      *Node;
  UINT8                             IdeChannel;
  UINT8                             BeginningIdeChannel;
  UINT8                             EndIdeChannel;
  UINT8                             IdeDevice;
  UINT8                             BeginningIdeDevice;
  UINT8                             EndIdeDevice;
  IDE_BLK_IO_DEV                    *IdeBlkIoDevice[IdeMaxChannel][IdeMaxDevice];
  IDE_BLK_IO_DEV                    *IdeBlkIoDevicePtr;
  IDE_REGISTERS_BASE_ADDR           IdeRegsBaseAddr[IdeMaxChannel];
  ATA_TRANSFER_MODE                 TransferMode;
  ATA_DRIVE_PARMS                   DriveParameters;
  EFI_DEV_PATH                      NewNode;
  UINT8                             ConfigurationOptions;
  UINT16                            CommandBlockBaseAddr;
  UINT16                            ControlBlockBaseAddr;
  UINTN                             DataSize;
  UINT32                            Attributes;
  IDE_BUS_DRIVER_PRIVATE_DATA       *IdeBusDriverPrivateData;

  //
  // Local variables declaration for IdeControllerInit support
  //
  EFI_IDE_CONTROLLER_INIT_PROTOCOL  *IdeInit;
  BOOLEAN                           EnumAll;
  BOOLEAN                           ChannelEnabled;
  UINT8                             ChannelCount;
  UINT8                             MaxDevices;
  EFI_IDENTIFY_DATA                 IdentifyData;
  EFI_ATA_COLLECTIVE_MODE           *SupportedModes;

  UINT64                            Supports;

  IdeBusDriverPrivateData = NULL;
  SupportedModes          = NULL;

  //
  // Perform IdeBus initialization
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **) &ParentDevicePath,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if ((EFI_ERROR (Status)) && (Status != EFI_ALREADY_STARTED)) {
    return Status;
  }
  
  //
  // Now open the IDE_CONTROLLER_INIT protocol. Step7.1
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiIdeControllerInitProtocolGuid,
                  (VOID **) &IdeInit,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  //
  // The following OpenProtocol function with _GET_PROTOCOL attribute and
  // will not return EFI_ALREADY_STARTED, so save it for now
  //
  SavedStatus = Status;

  if ((EFI_ERROR (Status)) && (Status != EFI_ALREADY_STARTED)) {
    DEBUG ((EFI_D_ERROR, "Open Init, Status=%x", Status));
    //
    // open protocol is not SUCCESS or not ALREADY_STARTED, error exit
    //
    goto ErrorExit;
  }

  //
  // Save Enumall and ChannelCount. Step7.2
  //
  EnumAll       = IdeInit->EnumAll;
  ChannelCount  = IdeInit->ChannelCount;

  //
  // Consume PCI I/O protocol. Note that the OpenProtocol with _GET_PROTOCOL
  // attribute will not return EFI_ALREADY_STARTED
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Open PciIo, Status=%x", Status));
    goto ErrorExit;
  }

  //
  // We must check EFI_ALREADY_STARTED because many ATAPI devices are removable
  //
  if (SavedStatus != EFI_ALREADY_STARTED) {
    IdeBusDriverPrivateData = EfiLibAllocatePool (sizeof (IDE_BUS_DRIVER_PRIVATE_DATA));
    if (IdeBusDriverPrivateData == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto ErrorExit;
    }

    EfiZeroMem (IdeBusDriverPrivateData, sizeof (IDE_BUS_DRIVER_PRIVATE_DATA));
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Controller,
                    &gIDEBusDriverGuid,
                    IdeBusDriverPrivateData,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      goto ErrorExit;
    }

  } else {
    Status = gBS->OpenProtocol (
                    Controller,
                    &gIDEBusDriverGuid,
                    (VOID **) &IdeBusDriverPrivateData,
                    This->DriverBindingHandle,
                    Controller,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      IdeBusDriverPrivateData = NULL;
      goto ErrorExit;
    }
  }

  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationSupported,
                    0,
                    &Supports
                    );
  if (!EFI_ERROR (Status)) {
    Supports &= EFI_PCI_DEVICE_ENABLE;
    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationEnable,
                      Supports,
                      NULL
                      );
  }
  if (EFI_ERROR (Status)) {
    goto ErrorExit;
  }

  //
  // Read the environment variable that contains the IDEBus Driver's
  // Config options that were set by the Driver Configuration Protocol
  //
  DataSize = sizeof (ConfigurationOptions);
  Status = gRT->GetVariable (
                  L"Configuration",
                  &gIDEBusDriverGuid,
                  &Attributes,
                  &DataSize,
                  &ConfigurationOptions
                  );
  if (EFI_ERROR (Status)) {
    ConfigurationOptions = 0x0f;
  }

  if (EnumAll) {
    //
    // If IdeInit->EnumAll is TRUE, must enumerate all IDE device anyway
    //
    BeginningIdeChannel = IdePrimary;
    EndIdeChannel       = IdeSecondary;
    BeginningIdeDevice  = IdeMaster;
    EndIdeDevice        = IdeSlave;
  } else if (RemainingDevicePath == NULL) {
    //
    // RemainingDevicePath is NULL, scan IDE bus for each device;
    //
    BeginningIdeChannel = IdePrimary;
    EndIdeChannel       = IdeSecondary;
    BeginningIdeDevice  = IdeMaster;
    //
    // default, may be redefined by IdeInit
    //
    EndIdeDevice = IdeSlave;
  } else {
    //
    // RemainingDevicePath is not NULL, only scan the specified device.
    //
    Node                = (EFI_DEV_PATH *) RemainingDevicePath;
    BeginningIdeChannel = Node->Atapi.PrimarySecondary;
    EndIdeChannel       = BeginningIdeChannel;
    BeginningIdeDevice  = Node->Atapi.SlaveMaster;
    EndIdeDevice        = BeginningIdeDevice;
  }

  //
  // Obtain IDE IO port registers' base addresses
  //
  Status = GetIdeRegistersBaseAddr (PciIo, IdeRegsBaseAddr);
  if (EFI_ERROR (Status)) {
    goto ErrorExit;
  }

  //
  // Report status code: begin IdeBus initialization
  //
  ReportStatusCodeWithDevicePath (
    EFI_PROGRESS_CODE,
    (EFI_IO_BUS_ATA_ATAPI | EFI_IOB_PC_RESET),
    0,
    &gIDEBusDriverGuid,
    ParentDevicePath
    );

  //
  // Strictly follow the enumeration based on IDE_CONTROLLER_INIT protocol
  //
  for (IdeChannel = BeginningIdeChannel; IdeChannel <= EndIdeChannel; IdeChannel++) {

    IdeInit->NotifyPhase (IdeInit, EfiIdeBeforeChannelEnumeration, IdeChannel);

    //
    // now obtain channel information fron IdeControllerInit protocol. Step9
    //
    Status = IdeInit->GetChannelInfo (
                        IdeInit,
                        IdeChannel,
                        &ChannelEnabled,
                        &MaxDevices
                        );
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "[GetChannel, Status=%x]", Status));
      continue;
    }

    if (!ChannelEnabled) {
      continue;
    }

    EndIdeDevice = (UINT8) EFI_MIN ((MaxDevices - 1), EndIdeDevice);

    //
    // Now inform the IDE Controller Init Module. Sept10
    //
    IdeInit->NotifyPhase (IdeInit, EfiIdeBeforeChannelReset, IdeChannel);

    //
    // No reset channel function implemented. Sept11
    //
    IdeInit->NotifyPhase (IdeInit, EfiIdeAfterChannelReset, IdeChannel);

    //
    // Step13
    //
    IdeInit->NotifyPhase (
              IdeInit,
              EfiIdeBusBeforeDevicePresenceDetection,
              IdeChannel
              );

    //
    // Prepare to detect IDE device of this channel
    //
    InitializeIDEChannelData ();

    //
    // -- 1st inner loop --- Master/Slave ------------  Step14
    //
    for (IdeDevice = BeginningIdeDevice; IdeDevice <= EndIdeDevice; IdeDevice++) {
      //
      // Check whether the configuration options allow this device
      //
      if (!(ConfigurationOptions & (1 << (IdeChannel * 2 + IdeDevice)))) {
        continue;
      }
      
      //
      // The device has been scanned in another Start(), No need to scan it again
      // for perf optimization.
      //
      if (IdeBusDriverPrivateData->HaveScannedDevice[IdeChannel * 2 + IdeDevice]) {
        continue;
      }
      
      //
      // create child handle for the detected device.
      //
      IdeBlkIoDevice[IdeChannel][IdeDevice] = EfiLibAllocatePool (sizeof (IDE_BLK_IO_DEV));
      if (IdeBlkIoDevice[IdeChannel][IdeDevice] == NULL) {
        continue;
      }

      IdeBlkIoDevicePtr = IdeBlkIoDevice[IdeChannel][IdeDevice];

      EfiZeroMem (IdeBlkIoDevicePtr, sizeof (IDE_BLK_IO_DEV));

      IdeBlkIoDevicePtr->Signature  = IDE_BLK_IO_DEV_SIGNATURE;
      IdeBlkIoDevicePtr->Channel    = IdeChannel;
      IdeBlkIoDevicePtr->Device     = IdeDevice;

      //
      // initialize Block IO interface's Media pointer
      //
      IdeBlkIoDevicePtr->BlkIo.Media = &IdeBlkIoDevicePtr->BlkMedia;

      //
      // Initialize IDE IO port addresses, including Command Block registers
      // and Control Block registers
      //
      IdeBlkIoDevicePtr->IoPort = EfiLibAllocatePool (sizeof (IDE_BASE_REGISTERS));
      if (IdeBlkIoDevicePtr->IoPort == NULL) {
        continue;
      }

      EfiZeroMem (IdeBlkIoDevicePtr->IoPort, sizeof (IDE_BASE_REGISTERS));
      CommandBlockBaseAddr = IdeRegsBaseAddr[IdeChannel].CommandBlockBaseAddr;
      ControlBlockBaseAddr = IdeRegsBaseAddr[IdeChannel].ControlBlockBaseAddr;

      IdeBlkIoDevicePtr->IoPort->Data = CommandBlockBaseAddr;
      (*(UINT16 *) &IdeBlkIoDevicePtr->IoPort->Reg1) = (UINT16) (CommandBlockBaseAddr + 0x01);
      IdeBlkIoDevicePtr->IoPort->SectorCount = (UINT16) (CommandBlockBaseAddr + 0x02);
      IdeBlkIoDevicePtr->IoPort->SectorNumber = (UINT16) (CommandBlockBaseAddr + 0x03);
      IdeBlkIoDevicePtr->IoPort->CylinderLsb = (UINT16) (CommandBlockBaseAddr + 0x04);
      IdeBlkIoDevicePtr->IoPort->CylinderMsb = (UINT16) (CommandBlockBaseAddr + 0x05);
      IdeBlkIoDevicePtr->IoPort->Head = (UINT16) (CommandBlockBaseAddr + 0x06);
      (*(UINT16 *) &IdeBlkIoDevicePtr->IoPort->Reg) = (UINT16) (CommandBlockBaseAddr + 0x07);

      (*(UINT16 *) &IdeBlkIoDevicePtr->IoPort->Alt) = ControlBlockBaseAddr;
      IdeBlkIoDevicePtr->IoPort->DriveAddress = (UINT16) (ControlBlockBaseAddr + 0x01);

      IdeBlkIoDevicePtr->IoPort->MasterSlave = (UINT16) ((IdeDevice == IdeMaster) ? 1 : 0);

      IdeBlkIoDevicePtr->PciIo = PciIo;
      IdeBlkIoDevicePtr->IdeBusDriverPrivateData = IdeBusDriverPrivateData;
      IdeBlkIoDevicePtr->IoPort->BusMasterBaseAddr = IdeRegsBaseAddr[IdeChannel].BusMasterBaseAddr;

      //
      // Report Status code: is about to detect IDE drive
      //
      EfiLibReportStatusCode (
        EFI_PROGRESS_CODE,
        (EFI_IO_BUS_ATA_ATAPI | EFI_P_PC_PRESENCE_DETECT),
        0,
        &gIDEBusDriverGuid,
        NULL
        );

      //
      // Discover device, now!
      //
      PERF_START (0, L"DiscoverIdeDevice", L"IDE", 0);
      Status = DiscoverIdeDevice (IdeBlkIoDevicePtr);
      PERF_END (0, L"DiscoverIdeDevice", L"IDE", 0);

      IdeBusDriverPrivateData->HaveScannedDevice[IdeChannel * 2 + IdeDevice]  = TRUE;
      IdeBusDriverPrivateData->DeviceProcessed[IdeChannel * 2 + IdeDevice]    = FALSE;

      if (!EFI_ERROR (Status)) {
        //
        // Set Device Path
        //
        EfiZeroMem (&NewNode, sizeof (NewNode));
        NewNode.DevPath.Type    = MESSAGING_DEVICE_PATH;
        NewNode.DevPath.SubType = MSG_ATAPI_DP;
        SetDevicePathNodeLength (&NewNode.DevPath, sizeof (ATAPI_DEVICE_PATH));

        NewNode.Atapi.PrimarySecondary  = (UINT8) IdeBlkIoDevicePtr->Channel;
        NewNode.Atapi.SlaveMaster       = (UINT8) IdeBlkIoDevicePtr->Device;
        NewNode.Atapi.Lun               = IdeBlkIoDevicePtr->Lun;
        IdeBlkIoDevicePtr->DevicePath = EfiAppendDevicePathNode (
                                          ParentDevicePath,
                                          &NewNode.DevPath
                                          );
        if (IdeBlkIoDevicePtr->DevicePath == NULL) {
          ReleaseIdeResources (IdeBlkIoDevicePtr);
          continue;
        }

        //
        // Submit identify data to IDE controller init driver
        //
        IdentifyData  = *IdeBlkIoDevicePtr->pIdData;
        IdeBusDriverPrivateData->DeviceFound[IdeChannel * 2 + IdeDevice] = TRUE;
        IdeInit->SubmitData (IdeInit, IdeChannel, IdeDevice, &IdentifyData);
      } else {
        //
        // Device detection failed
        //
        IdeBusDriverPrivateData->DeviceFound[IdeChannel * 2 + IdeDevice] = FALSE;
        IdeInit->SubmitData (IdeInit, IdeChannel, IdeDevice, NULL);
        ReleaseIdeResources (IdeBlkIoDevicePtr);
        IdeBlkIoDevicePtr = NULL;
      }
      //
      // end of 1st inner loop ---
      //
    }
    //
    // end of 1st outer loop =========
    //
  }

  //
  // = 2nd outer loop == Primary/Secondary =================
  //
  for (IdeChannel = BeginningIdeChannel; IdeChannel <= EndIdeChannel; IdeChannel++) {

    //
    // -- 2nd inner loop --- Master/Slave --------
    //
    for (IdeDevice = BeginningIdeDevice; IdeDevice <= EndIdeDevice; IdeDevice++) {

      if (IdeBusDriverPrivateData->DeviceProcessed[IdeChannel * 2 + IdeDevice]) {
        continue;
      }

      if (!IdeBusDriverPrivateData->DeviceFound[IdeChannel * 2 + IdeDevice]) {
        continue;
      }

      Status = IdeInit->CalculateMode (
                          IdeInit,
                          IdeChannel,
                          IdeDevice,
                          &SupportedModes
                          );
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "[bStStp20S=%x]", Status));
        continue;
      }

      IdeBlkIoDevicePtr = IdeBlkIoDevice[IdeChannel][IdeDevice];

      //
      // Set best supported PIO mode on this IDE device
      //
      if (SupportedModes->PioMode.Mode <= ATA_PIO_MODE_2) {
        TransferMode.ModeCategory = ATA_MODE_CATEGORY_DEFAULT_PIO;
      } else {
        TransferMode.ModeCategory = ATA_MODE_CATEGORY_FLOW_PIO;
      }

      TransferMode.ModeNumber = (UINT8) (SupportedModes->PioMode.Mode);

      if (SupportedModes->ExtModeCount == 0){
        Status                  = SetDeviceTransferMode (IdeBlkIoDevicePtr, &TransferMode);

        if (EFI_ERROR (Status)) {
          IdeBusDriverPrivateData->DeviceFound[IdeChannel * 2 + IdeDevice] = FALSE;
          ReleaseIdeResources (IdeBlkIoDevicePtr);
          IdeBlkIoDevicePtr = NULL;
          continue;
        }
      }

      //
      // Set supported DMA mode on this IDE device. Note that UDMA & MDMA cann't
      // be set together. Only one DMA mode can be set to a device. If setting
      // DMA mode operation fails, we can continue moving on because we only use
      // PIO mode at boot time. DMA modes are used by certain kind of OS booting
      //
      if (SupportedModes->UdmaMode.Valid) {

        TransferMode.ModeCategory = ATA_MODE_CATEGORY_UDMA;
        TransferMode.ModeNumber   = (UINT8) (SupportedModes->UdmaMode.Mode);
        Status                    = SetDeviceTransferMode (IdeBlkIoDevicePtr, &TransferMode);

        if (EFI_ERROR (Status)) {
          IdeBusDriverPrivateData->DeviceFound[IdeChannel * 2 + IdeDevice] = FALSE;
          ReleaseIdeResources (IdeBlkIoDevicePtr);
          IdeBlkIoDevicePtr = NULL;
          continue;
        }
        //
        // Record Udma Mode
        //
        IdeBlkIoDevicePtr->UdmaMode.Valid = TRUE;
        IdeBlkIoDevicePtr->UdmaMode.Mode  = SupportedModes->UdmaMode.Mode;
        EnableInterrupt (IdeBlkIoDevicePtr);
      } else if (SupportedModes->MultiWordDmaMode.Valid) {

        TransferMode.ModeCategory = ATA_MODE_CATEGORY_MDMA;
        TransferMode.ModeNumber   = (UINT8) SupportedModes->MultiWordDmaMode.Mode;
        Status                    = SetDeviceTransferMode (IdeBlkIoDevicePtr, &TransferMode);

        if (EFI_ERROR (Status)) {
          IdeBusDriverPrivateData->DeviceFound[IdeChannel * 2 + IdeDevice] = FALSE;
          ReleaseIdeResources (IdeBlkIoDevicePtr);
          IdeBlkIoDevicePtr = NULL;
          continue;
        }

        EnableInterrupt (IdeBlkIoDevicePtr);
      }

      //
      // Init driver parameters
      //
      DriveParameters.Sector          = (UINT8) IdeBlkIoDevicePtr->pIdData->AtaData.sectors_per_track;
      DriveParameters.Heads           = (UINT8) (IdeBlkIoDevicePtr->pIdData->AtaData.heads - 1);
      DriveParameters.MultipleSector  = (UINT8) IdeBlkIoDevicePtr->pIdData->AtaData.multi_sector_cmd_max_sct_cnt;
      //
      // Set Parameters for the device:
      // 1) Init
      // 2) Establish the block count for READ/WRITE MULTIPLE (EXT) command
      //
      if ((IdeBlkIoDevicePtr->Type == IdeHardDisk) || (IdeBlkIoDevicePtr->Type == Ide48bitAddressingHardDisk)) {
        Status = SetDriveParameters (IdeBlkIoDevicePtr, &DriveParameters);
      }
      
      //
      // Record PIO mode used in private data
      //
      IdeBlkIoDevicePtr->PioMode = SupportedModes->PioMode.Mode;

      //
      // Set IDE controller Timing Blocks in the PCI Configuration Space
      //
      IdeInit->SetTiming (IdeInit, IdeChannel, IdeDevice, SupportedModes);

      //
      // Add Component Name for the IDE/ATAPI device that was discovered.
      //
      IdeBlkIoDevicePtr->ControllerNameTable = NULL;
      ADD_NAME (IdeBlkIoDevicePtr);

      Status = gBS->InstallMultipleProtocolInterfaces (
                      &IdeBlkIoDevicePtr->Handle,
                      &gEfiDevicePathProtocolGuid,
                      IdeBlkIoDevicePtr->DevicePath,
                      &gEfiBlockIoProtocolGuid,
                      &IdeBlkIoDevicePtr->BlkIo,
                      &gEfiDiskInfoProtocolGuid,
                      &IdeBlkIoDevicePtr->DiskInfo,
                      NULL
                      );

      if (EFI_ERROR (Status)) {
        ReleaseIdeResources (IdeBlkIoDevicePtr);
      }

      gBS->OpenProtocol (
            Controller,
            &gEfiPciIoProtocolGuid,
            (VOID **) &PciIo,
            This->DriverBindingHandle,
            IdeBlkIoDevicePtr->Handle,
            EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
            );

      IdeBusDriverPrivateData->DeviceProcessed[IdeChannel * 2 + IdeDevice] = TRUE;

      //
      // Report status code: device eanbled!
      //
      ReportStatusCodeWithDevicePath (
        EFI_PROGRESS_CODE,
        (EFI_IO_BUS_ATA_ATAPI | EFI_P_PC_ENABLE),
        0,
        &gIDEBusDriverGuid,
        IdeBlkIoDevicePtr->DevicePath
        );

      //
      // Create event to clear pending IDE interrupt
      //
      Status = gBS->CreateEvent (
                      EFI_EVENT_SIGNAL_EXIT_BOOT_SERVICES,
                      EFI_TPL_NOTIFY,
                      ClearInterrupt,
                      IdeBlkIoDevicePtr,
                      &IdeBlkIoDevicePtr->ExitBootServiceEvent
                      );

      //
      // end of 2nd inner loop ----
      //
    }
    //
    // end of 2nd outer loop ==========
    //
  }
  
  //
  // All configurations done! Notify IdeController to do post initialization
  // work such as saving IDE controller PCI settings for S3 resume
  //
  IdeInit->NotifyPhase (IdeInit, EfiIdeBusPhaseMaximum, 0);

  if (SupportedModes != NULL) {
    gBS->FreePool (SupportedModes);
  }

  PERF_START (0, L"Finish IDE detection", L"IDE", 1);
  PERF_END (0, L"Finish IDE detection", L"IDE", 0);

  return EFI_SUCCESS;

ErrorExit:

  //
  // Report error code: controller error
  //
  ReportStatusCodeWithDevicePath (
    EFI_ERROR_CODE | EFI_ERROR_MINOR,
    (EFI_IO_BUS_ATA_ATAPI | EFI_IOB_EC_CONTROLLER_ERROR),
    0,
    &gIDEBusDriverGuid,
    ParentDevicePath
    );

  gBS->CloseProtocol (
        Controller,
        &gEfiIdeControllerInitProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  gBS->UninstallMultipleProtocolInterfaces (
        Controller,
        &gIDEBusDriverGuid,
        IdeBusDriverPrivateData,
        NULL
        );

  if (IdeBusDriverPrivateData != NULL) {
    gBS->FreePool (IdeBusDriverPrivateData);
  }

  if (SupportedModes != NULL) {
    gBS->FreePool (SupportedModes);
  }

  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  gBS->CloseProtocol (
        Controller,
        &gEfiDevicePathProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  return Status;

}

//
// ***********************************************************************************
// IDEBusDriverBindingStop
// ***********************************************************************************
//
EFI_STATUS
EFIAPI
IDEBusDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN  EFI_HANDLE                      Controller,
  IN  UINTN                           NumberOfChildren,
  IN  EFI_HANDLE                      *ChildHandleBuffer
  )
/*++
  
  Routine Description:
    Stop this driver on Controller Handle. 

  Arguments:
    This              - Protocol instance pointer.
    DeviceHandle      - Handle of device to stop driver on 
    NumberOfChildren  - Not used
    ChildHandleBuffer - Not used

  Returns:
    EFI_SUCCESS       - This driver is removed DeviceHandle
    other             - This driver was not removed from this device
  
--*/
// TODO:    Controller - add argument and description to function comment
// TODO:    EFI_DEVICE_ERROR - add return value to function comment
{
  EFI_STATUS                  Status;
  EFI_PCI_IO_PROTOCOL         *PciIo;
  BOOLEAN                     AllChildrenStopped;
  UINTN                       Index;
  IDE_BUS_DRIVER_PRIVATE_DATA *IdeBusDriverPrivateData;
  UINT64                      Supports;

  IdeBusDriverPrivateData = NULL;

  if (NumberOfChildren == 0) {

    Status = gBS->OpenProtocol (
                    Controller,
                    &gEfiPciIoProtocolGuid,
                    (VOID **) &PciIo,
                    This->DriverBindingHandle,
                    Controller,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (!EFI_ERROR (Status)) {
      Status = PciIo->Attributes (
                        PciIo,
                        EfiPciIoAttributeOperationSupported,
                        0,
                        &Supports
                        );
      if (!EFI_ERROR (Status)) {
        Supports &= (EFI_PCI_DEVICE_ENABLE               |
                     EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO |
                     EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO);
        Status = PciIo->Attributes (
                          PciIo,
                          EfiPciIoAttributeOperationDisable,
                          Supports,
                          NULL
                          );
      }
    }

    gBS->OpenProtocol (
          Controller,
          &gIDEBusDriverGuid,
          (VOID **) &IdeBusDriverPrivateData,
          This->DriverBindingHandle,
          Controller,
          EFI_OPEN_PROTOCOL_GET_PROTOCOL
          );

    gBS->UninstallMultipleProtocolInterfaces (
          Controller,
          &gIDEBusDriverGuid,
          IdeBusDriverPrivateData,
          NULL
          );

    if (IdeBusDriverPrivateData != NULL) {
      gBS->FreePool (IdeBusDriverPrivateData);
    }
    //
    // Close the bus driver
    //
    gBS->CloseProtocol (
          Controller,
          &gEfiIdeControllerInitProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );
    gBS->CloseProtocol (
          Controller,
          &gEfiPciIoProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );
    gBS->CloseProtocol (
          Controller,
          &gEfiDevicePathProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );

    return EFI_SUCCESS;
  }

  AllChildrenStopped = TRUE;

  for (Index = 0; Index < NumberOfChildren; Index++) {

    Status = DeRegisterIdeDevice (This, Controller, ChildHandleBuffer[Index]);

    if (EFI_ERROR (Status)) {
      AllChildrenStopped = FALSE;
    }
  }

  if (!AllChildrenStopped) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

//
// ***********************************************************************************
// DeRegisterIdeDevice
// ***********************************************************************************
//
EFI_STATUS
DeRegisterIdeDevice (
  IN  EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN  EFI_HANDLE                     Controller,
  IN  EFI_HANDLE                     Handle
  )
/*++

Routine Description:

  Deregister an IDE device and free resources
  
Arguments:

  This            - Protocol instance pointer.
  Controller      - Ide device handle
  Handle          - Handle of device to deregister driver on 
  
Returns:

  EFI_STATUS

--*/
// TODO:    EFI_SUCCESS - add return value to function comment
{
  EFI_STATUS            Status;
  EFI_BLOCK_IO_PROTOCOL *BlkIo;
  IDE_BLK_IO_DEV        *IdeBlkIoDevice;
  EFI_PCI_IO_PROTOCOL   *PciIo;
  UINTN                 Index;

  Status = gBS->OpenProtocol (
                  Handle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **) &BlkIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  IdeBlkIoDevice = IDE_BLOCK_IO_DEV_FROM_THIS (BlkIo);

  //
  // Report Status code: Device disabled
  //
  ReportStatusCodeWithDevicePath (
    EFI_PROGRESS_CODE,
    (EFI_IO_BUS_ATA_ATAPI | EFI_P_PC_DISABLE),
    0,
    &gIDEBusDriverGuid,
    IdeBlkIoDevice->DevicePath
    );

  //
  // Close the child handle
  //
  Status = gBS->CloseProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  This->DriverBindingHandle,
                  Handle
                  );

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  Handle,
                  &gEfiDevicePathProtocolGuid,
                  IdeBlkIoDevice->DevicePath,
                  &gEfiBlockIoProtocolGuid,
                  &IdeBlkIoDevice->BlkIo,
                  &gEfiDiskInfoProtocolGuid,
                  &IdeBlkIoDevice->DiskInfo,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    gBS->OpenProtocol (
          Controller,
          &gEfiPciIoProtocolGuid,
          (VOID **) &PciIo,
          This->DriverBindingHandle,
          Handle,
          EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
          );
    return Status;
  }
  
  //
  // Release allocated resources
  //
  Index = IdeBlkIoDevice->Channel * 2 + IdeBlkIoDevice->Device;
  IdeBlkIoDevice->IdeBusDriverPrivateData->HaveScannedDevice[Index] = FALSE;

  ReleaseIdeResources (IdeBlkIoDevice);

  return EFI_SUCCESS;
}

//
// ***********************************************************************************
// IDEBlkIoReset
// ***********************************************************************************
//
EFI_STATUS
EFIAPI
IDEBlkIoReset (
  IN  EFI_BLOCK_IO_PROTOCOL   *This,
  IN  BOOLEAN                 ExtendedVerification
  )
/*++

Routine Description:

Arguments:

Returns:

  None

--*/
// TODO:    This - add argument and description to function comment
// TODO:    ExtendedVerification - add argument and description to function comment
// TODO:    EFI_DEVICE_ERROR - add return value to function comment
{
  IDE_BLK_IO_DEV  *IdeBlkIoDevice;
  EFI_STATUS      Status;

  IdeBlkIoDevice = IDE_BLOCK_IO_DEV_FROM_THIS (This);
  //
  // Requery IDE IO resources in case of the switch of native and legacy modes
  //
  ReassignIdeResources (IdeBlkIoDevice);

  //
  // for ATA device, using ATA reset method
  //
  if (IdeBlkIoDevice->Type == IdeHardDisk ||
      IdeBlkIoDevice->Type == Ide48bitAddressingHardDisk) {
    return AtaSoftReset (IdeBlkIoDevice);
  }

  if (IdeBlkIoDevice->Type == IdeUnknown) {
    return EFI_DEVICE_ERROR;
  }
  
  //
  // for ATAPI device, using ATAPI reset method
  //
  Status = AtapiSoftReset (IdeBlkIoDevice);
  if (ExtendedVerification) {
    Status = AtaSoftReset (IdeBlkIoDevice);
  }

  return Status;
}

EFI_STATUS
EFIAPI
IDEBlkIoReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL   *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 LBA,
  IN  UINTN                   BufferSize,
  OUT VOID                    *Buffer
  )
/*++

Routine Description:

  Read data from block io device
  
Arguments:

  This            - Protocol instance pointer.
  MediaId         - The media ID of the device
  LBA             - Starting LBA address to read data
  BufferSize      - The size of data to be read
  Buffer          - Caller supplied buffer to save data
  
Returns:

  read data status

--*/
// TODO:    EFI_DEVICE_ERROR - add return value to function comment
{
  IDE_BLK_IO_DEV  *IdeBlkIoDevice;

  IdeBlkIoDevice = IDE_BLOCK_IO_DEV_FROM_THIS (This);

  //
  // Requery IDE IO resources in case of the switch of native and legacy modes
  //
  ReassignIdeResources (IdeBlkIoDevice);

  //
  // For ATA compatible device, use ATA read block's mechanism
  //
  if (IdeBlkIoDevice->Type == IdeHardDisk ||
      IdeBlkIoDevice->Type == Ide48bitAddressingHardDisk) {
    return AtaBlkIoReadBlocks (
            IdeBlkIoDevice,
            MediaId,
            LBA,
            BufferSize,
            Buffer
            );
  }

  if (IdeBlkIoDevice->Type == IdeUnknown) {
    return EFI_DEVICE_ERROR;
  }
  
  //
  // for ATAPI device, using ATAPI read block's mechanism
  //
  return AtapiBlkIoReadBlocks (
          IdeBlkIoDevice,
          MediaId,
          LBA,
          BufferSize,
          Buffer
          );

}

EFI_STATUS
EFIAPI
IDEBlkIoWriteBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL   *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 LBA,
  IN  UINTN                   BufferSize,
  IN  VOID                    *Buffer
  )
/*++

Routine Description:

  Write data to block io device
  
Arguments:

  This            - Protocol instance pointer.
  MediaId         - The media ID of the device
  LBA             - Starting LBA address to write data
  BufferSize      - The size of data to be written
  Buffer          - Caller supplied buffer to save data
  
Returns:

  write data status

--*/
// TODO:    EFI_DEVICE_ERROR - add return value to function comment
{
  IDE_BLK_IO_DEV  *IdeBlkIoDevice;

  IdeBlkIoDevice = IDE_BLOCK_IO_DEV_FROM_THIS (This);
  //
  // Requery IDE IO resources in case of the switch of native and legacy modes
  //
  ReassignIdeResources (IdeBlkIoDevice);

  //
  // for ATA device, using ATA write block's mechanism
  //
  if (IdeBlkIoDevice->Type == IdeHardDisk ||
      IdeBlkIoDevice->Type == Ide48bitAddressingHardDisk) {        

    return AtaBlkIoWriteBlocks (
            IdeBlkIoDevice,
            MediaId,
            LBA,
            BufferSize,
            Buffer
            );
  }

  if (IdeBlkIoDevice->Type == IdeUnknown) {
    return EFI_DEVICE_ERROR;
  }
  
  //
  // for ATAPI device, using ATAPI write block's mechanism
  //
  return AtapiBlkIoWriteBlocks (
          IdeBlkIoDevice,
          MediaId,
          LBA,
          BufferSize,
          Buffer
          );
}

//
// ***********************************************************************************
// IDEBlkIoFlushBlocks
// ***********************************************************************************
//
EFI_STATUS
EFIAPI
IDEBlkIoFlushBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL   *This
  )
/*++

Routine Description:

Arguments:

Returns:

  None

--*/
// TODO:    This - add argument and description to function comment
// TODO:    EFI_SUCCESS - add return value to function comment
{
  //
  // return directly
  //
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
IDEDiskInfoInquiry (
  IN     EFI_DISK_INFO_PROTOCOL   *This,
  IN OUT VOID                     *InquiryData,
  IN OUT UINT32                   *InquiryDataSize
  )
/*++

  Routine Description:
    Return the results of the Inquiry command to a drive in InquiryData.
    Data format of Inquiry data is defined by the Interface GUID.

  Arguments:
    This        - Protocol instance pointer.
    InquiryData - Results of Inquiry command to device
    InquiryDataSize - Size of InquiryData in bytes.

  Returns:
    EFI_SUCCESS       - InquiryData valid
    EFI_NOT_FOUND     - Device does not support this data class
    EFI_DEVICE_ERROR  - Error reading InquiryData from device 
    EFI_BUFFER_TOO_SMALL - IntquiryDataSize not big enough

--*/
{
  IDE_BLK_IO_DEV  *IdeBlkIoDevice;

  IdeBlkIoDevice = IDE_BLOCK_IO_DEV_FROM_DISK_INFO_THIS (This);

  if (*InquiryDataSize < sizeof (INQUIRY_DATA)) {
    *InquiryDataSize = sizeof (INQUIRY_DATA);
    return EFI_BUFFER_TOO_SMALL;
  }

  if (IdeBlkIoDevice->pInquiryData == NULL) {
    return EFI_NOT_FOUND;
  }

  gBS->CopyMem (InquiryData, IdeBlkIoDevice->pInquiryData, sizeof (INQUIRY_DATA));
  *InquiryDataSize = sizeof (INQUIRY_DATA);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
IDEDiskInfoIdentify (
  IN     EFI_DISK_INFO_PROTOCOL   *This,
  IN OUT VOID                     *IdentifyData,
  IN OUT UINT32                   *IdentifyDataSize
  )
/*++

  Routine Description:
    Return the results of the Identify command to a drive in IdentifyData.
    Data format of Identify data is defined by the Interface GUID.

  Arguments:
    This        - Protocol instance pointer.
    IdentifyData - Results of Identify command to device
    IdentifyDataSize - Size of IdentifyData in bytes.

  Returns:
    EFI_SUCCESS       - IdentifyData valid
    EFI_NOT_FOUND     - Device does not support this data class
    EFI_DEVICE_ERROR  - Error reading IdentifyData from device 
    EFI_BUFFER_TOO_SMALL - IdentifyDataSize not big enough

--*/
{
  IDE_BLK_IO_DEV  *IdeBlkIoDevice;

  IdeBlkIoDevice = IDE_BLOCK_IO_DEV_FROM_DISK_INFO_THIS (This);

  if (*IdentifyDataSize < sizeof (EFI_IDENTIFY_DATA)) {
    *IdentifyDataSize = sizeof (EFI_IDENTIFY_DATA);
    return EFI_BUFFER_TOO_SMALL;
  }

  if (IdeBlkIoDevice->pIdData == NULL) {
    return EFI_NOT_FOUND;
  }

  gBS->CopyMem (IdentifyData, IdeBlkIoDevice->pIdData, sizeof (EFI_IDENTIFY_DATA));
  *IdentifyDataSize = sizeof (EFI_IDENTIFY_DATA);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
IDEDiskInfoSenseData (
  IN     EFI_DISK_INFO_PROTOCOL   *This,
  IN OUT VOID                     *SenseData,
  IN OUT UINT32                   *SenseDataSize,
  OUT    UINT8                    *SenseDataNumber
  )
/*++

  Routine Description:
    Return the results of the Request Sense command to a drive in SenseData.
    Data format of Sense data is defined by the Interface GUID.

  Arguments:
    This            - Protocol instance pointer.
    SenseData       - Results of Request Sense command to device
    SenseDataSize   - Size of SenseData in bytes.
    SenseDataNumber - Type of SenseData

  Returns:
    EFI_SUCCESS       - InquiryData valid
    EFI_NOT_FOUND     - Device does not support this data class
    EFI_DEVICE_ERROR  - Error reading InquiryData from device 
    EFI_BUFFER_TOO_SMALL - SenseDataSize not big enough

--*/
{
  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
IDEDiskInfoWhichIde (
  IN  EFI_DISK_INFO_PROTOCOL   *This,
  OUT UINT32                   *IdeChannel,
  OUT UINT32                   *IdeDevice
  )
/*++

  Routine Description:
    Return the results of the Request Sense command to a drive in SenseData.
    Data format of Sense data is defined by the Interface GUID.

  Arguments:
    This        - Protocol instance pointer.
    IdeChannel  - Primary or Secondary
    IdeDevice   - Master or Slave

  Returns:
    EFI_SUCCESS       - IdeChannel and IdeDevice are valid
    EFI_UNSUPPORTED   - This is not an IDE device

--*/
{
  IDE_BLK_IO_DEV  *IdeBlkIoDevice;

  IdeBlkIoDevice  = IDE_BLOCK_IO_DEV_FROM_DISK_INFO_THIS (This);
  *IdeChannel     = IdeBlkIoDevice->Channel;
  *IdeDevice      = IdeBlkIoDevice->Device;

  return EFI_SUCCESS;
}
