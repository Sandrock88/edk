/*++

Copyright (c) 2007, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  UsbMassImpl.c
    
Abstract:

  The implementation of USB mass storage class device driver.
  The command set supported is "USB Mass Storage Specification 
  for Bootability". 

Revision History

--*/

#include "UsbMassImpl.h"

//
// The underlying transport protocol. CBI support isn't included
// in the current build. It is being obseleted by the standard
// body. If you want to enable it, remove the if directive here,
// then add the UsbMassCbi.c/.h to the driver's inf file.
//
STATIC 
USB_MASS_TRANSPORT *mUsbMassTransport[] = {
  &mUsbCbi0Transport,
  &mUsbCbi1Transport,
  &mUsbBotTransport,
  NULL
};

UINTN mUsbMscInfo  = EFI_D_INFO;
UINTN mUsbMscError = EFI_D_ERROR;

EFI_STATUS
UsbMassInitMedia (
  IN USB_MASS_DEVICE          *UsbMass
  )
/*++

Routine Description:

  Retrieve the media parameters such as disk gemotric for the 
  device's BLOCK IO protocol.

Arguments:

  UsbMass - The USB mass storage device

Returns:

  EFI_SUCCESS - The media parameters is updated successfully.
  Others      - Failed to get the media parameters.

--*/
{
  EFI_BLOCK_IO_MEDIA          *Media;
  EFI_STATUS                  Status;
  UINTN                       Index;
    
  Media = &UsbMass->BlockIoMedia;

  //
  // Initialize the MediaPrsent/ReadOnly and others to the default.
  // We are not forced to get it right at this time, check UEFI2.0
  // spec for more information:
  //
  // MediaPresent: This field shows the media present status as
  //               of the most recent ReadBlocks or WriteBlocks call.
  //
  // ReadOnly    : This field shows the read-only status as of the
  //               recent WriteBlocks call.
  //
  // but remember to update MediaId/MediaPresent/ReadOnly status
  // after ReadBlocks and WriteBlocks
  //
  Media->MediaPresent     = FALSE;
  Media->LogicalPartition = FALSE;
  Media->ReadOnly         = FALSE;
  Media->WriteCaching     = FALSE;
  Media->IoAlign          = 0;

  //
  // Some device may spend several seconds before it is ready.
  // Try several times before giving up. Wait 5s at most.
  //
  Status = EFI_SUCCESS;
    
  for (Index = 0; Index < USB_BOOT_WAIT_RETRY; Index++) {

    Status = UsbBootGetParams (UsbMass);
    if ((Status != EFI_MEDIA_CHANGED) 
        && (Status != EFI_NOT_READY) 
        && (Status != EFI_TIMEOUT)) {
      break;
    }
      
    Status = UsbBootIsUnitReady (UsbMass);
    if (EFI_ERROR (Status)) {
      gBS->Stall (USB_BOOT_UNIT_READY_STALL * (Index + 1)); 
    }
    
  }

  return Status;
}

EFI_STATUS
UsbMassReset (
  IN EFI_BLOCK_IO_PROTOCOL    *This,
  IN BOOLEAN                  ExtendedVerification
  )
/*++

Routine Description:

  Reset the block device. ExtendedVerification is ignored for this.

Arguments:

  This                  - The BLOCK IO protocol
  ExtendedVerification  - Whether to execute extended verfication.

Returns:

  EFI_SUCCESS - The device is successfully resetted.
  Others      - Failed to reset the device.

--*/
{
  USB_MASS_DEVICE *UsbMass;

  UsbMass = USB_MASS_DEVICE_FROM_BLOCKIO (This);
  return UsbMass->Transport->Reset (UsbMass->Context, ExtendedVerification);
}

EFI_STATUS
UsbMassReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL    *This,
  IN UINT32                   MediaId,
  IN EFI_LBA                  Lba,
  IN UINTN                    BufferSize,
  OUT VOID                    *Buffer
  )
/*++

Routine Description:

  Read some blocks of data from the block device.

Arguments:

  This        - The Block IO protocol 
  MediaId     - The media's ID of the device for current request
  Lba         - The start block number
  BufferSize  - The size of buffer to read data in
  Buffer      - The buffer to read data to

Returns:

  EFI_SUCCESS           - The data is successfully read
  EFI_NO_MEDIA          - Media isn't present
  EFI_MEDIA_CHANGED     - The device media has been changed, that is, MediaId changed
  EFI_INVALID_PARAMETER - Some parameters are invalid, such as Buffer is NULL.
  EFI_BAD_BUFFER_SIZE   - The buffer size isn't a multiple of media's block size, 
                          or overflow the last block number.
--*/
{
  USB_MASS_DEVICE     *UsbMass;
  EFI_BLOCK_IO_MEDIA  *Media;
  EFI_STATUS          Status;
  UINTN               TotalBlock;
  
  UsbMass = USB_MASS_DEVICE_FROM_BLOCKIO (This);
  Media   = &UsbMass->BlockIoMedia;

  //
  // First, validate the parameters
  //
  if ((Buffer == NULL) || (BufferSize == 0)) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // If it is a remoable media, such as CD-Rom or Usb-Floppy,
  // if, need to detect the media before each rw, while Usb-Flash
  // needn't. However, it's hard to identify Usb-Floppy between 
  // Usb-Flash by now, so detect media every time.
  // 
  Status = UsbBootDetectMedia (UsbMass);
  if (EFI_ERROR (Status)) {
    DEBUG ((mUsbMscError, "UsbMassReadBlocks: UsbBootDetectMedia (%r)\n", Status));
    return Status;
  } 
  
  //
  // Make sure BlockSize and LBA is consistent with BufferSize
  //
  if ((BufferSize % Media->BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  TotalBlock = BufferSize / Media->BlockSize;

  if (Lba + TotalBlock - 1 > Media->LastBlock) {
    return EFI_BAD_BUFFER_SIZE;
  }
  
  Status = UsbBootReadBlocks (UsbMass, (UINT32) Lba, TotalBlock, Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((mUsbMscError, "UsbMassReadBlocks: UsbBootReadBlocks (%r) -> Reset\n", Status));
    UsbMassReset (This, TRUE);
  }
  
  return Status;
}

EFI_STATUS
UsbMassWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL    *This,
  IN UINT32                   MediaId,
  IN EFI_LBA                  Lba,
  IN UINTN                    BufferSize,
  IN VOID                     *Buffer
  )
/*++

Routine Description:

  Write some blocks of data to the block device.

Arguments:

  This        - The Block IO protocol 
  MediaId     - The media's ID of the device for current request
  Lba         - The start block number
  BufferSize  - The size of buffer to write data to
  Buffer      - The buffer to write data to

Returns:

  EFI_SUCCESS           - The data is successfully written
  EFI_NO_MEDIA          - Media isn't present
  EFI_MEDIA_CHANGED     - The device media has been changed, that is, MediaId changed
  EFI_INVALID_PARAMETER - Some parameters are invalid, such as Buffer is NULL.
  EFI_BAD_BUFFER_SIZE   - The buffer size isn't a multiple of media's block size, 

--*/
{
  USB_MASS_DEVICE     *UsbMass;
  EFI_BLOCK_IO_MEDIA  *Media;
  EFI_STATUS          Status;
  UINTN               TotalBlock;

  UsbMass = USB_MASS_DEVICE_FROM_BLOCKIO (This);
  Media   = &UsbMass->BlockIoMedia;

  //
  // First, validate the parameters
  //
  if ((Buffer == NULL) || (BufferSize == 0)) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // If it is a remoable media, such as CD-Rom or Usb-Floppy,
  // if, need to detect the media before each rw, while Usb-Flash
  // needn't. However, it's hard to identify Usb-Floppy between 
  // Usb-Flash by now, so detect media every time.
  // 
  Status = UsbBootDetectMedia (UsbMass);
  if (EFI_ERROR (Status)) {
    DEBUG ((mUsbMscError, "UsbMassWriteBlocks: UsbBootDetectMedia (%r)\n", Status));
    return Status;
  }
  
  //
  // Make sure BlockSize and LBA is consistent with BufferSize
  //
  if ((BufferSize % Media->BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  TotalBlock = BufferSize / Media->BlockSize;

  if (Lba + TotalBlock - 1 > Media->LastBlock) {
    return EFI_BAD_BUFFER_SIZE;
  }
  
  //
  // Try to write the data even the device is marked as ReadOnly,
  // and clear the status should the write succeed.
  //
  Status = UsbBootWriteBlocks (UsbMass, (UINT32) Lba, TotalBlock, Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((mUsbMscError, "UsbMassWriteBlocks: UsbBootWriteBlocks (%r) -> Reset\n", Status));
    UsbMassReset (This, TRUE);
  }
  
  return Status;
}

EFI_STATUS
UsbMassFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
/*++

Routine Description:

  Flush the cached writes to disks. USB mass storage device doesn't
  support write cache, so return EFI_SUCCESS directly.

Arguments:

  This  - The BLOCK IO protocol

Returns:

  EFI_SUCCESS - Always returns success

--*/
{
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
USBMassDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++

Routine Description:

  Check whether the controller is a supported USB mass storage.

Arguments:

  This                - The USB mass driver's driver binding.
  Controller          - The device to test against.
  RemainingDevicePath - The remaining device path

Returns:

  EFI_SUCCESS     - This device is a supported USB mass storage.
  EFI_UNSUPPORTED - The device isn't supported
  Others          - Some error happened.

--*/
{
  EFI_USB_IO_PROTOCOL           *UsbIo;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;
  USB_MASS_TRANSPORT            *Transport;
  EFI_STATUS                    Status;
  INTN                          Index;

  //
  // Check whether the controlelr support USB_IO
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiUsbIoProtocolGuid,
                  &UsbIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  
  //
  // Get the interface to check the USB class and find a transport
  // protocol handler.
  //
  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Interface);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Status = EFI_UNSUPPORTED;

  if (Interface.InterfaceClass != USB_MASS_STORE_CLASS) {
    goto ON_EXIT;
  }

  for (Index = 0; mUsbMassTransport[Index] != NULL; Index++) {
    Transport = mUsbMassTransport[Index];
    if (Interface.InterfaceProtocol == Transport->Protocol) {
      Status = Transport->Init (UsbIo, Controller, NULL);
      break;
    }
  }

  DEBUG ((mUsbMscInfo, "Found a USB mass store device %r\n", Status));

ON_EXIT:
  gBS->CloseProtocol (
        Controller,
        &gEfiUsbIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  return Status;
}

EFI_STATUS
EFIAPI
USBMassDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++

Routine Description:

  Start the USB mass storage device on the controller. It will 
  install a BLOCK_IO protocol on the device if everything is OK.

Arguments:

  This                - The USB mass storage driver binding.
  Controller          - The USB mass storage device to start on
  RemainingDevicePath - The remaining device path.

Returns:

  EFI_SUCCESS          - The driver has started on the device.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory
  Others               - Failed to start the driver on the device.

--*/
{
  EFI_USB_IO_PROTOCOL           *UsbIo;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;
  USB_MASS_DEVICE               *UsbMass;
  USB_MASS_TRANSPORT            *Transport;
  EFI_STATUS                    Status;
  UINTN                         Index;

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiUsbIoProtocolGuid,
                  &UsbIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  UsbMass = EfiLibAllocateZeroPool (sizeof (USB_MASS_DEVICE));
  if (UsbMass == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  
  //
  // Initialize the transport protocols
  //
  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Interface);
  if (EFI_ERROR (Status)) {
    DEBUG ((mUsbMscError, "USBMassDriverBindingStart: UsbIo->UsbGetInterfaceDescriptor (%r)\n", Status));
    goto ON_ERROR;
  }
  
  Status = EFI_UNSUPPORTED;

  for (Index = 0; mUsbMassTransport[Index] != NULL; Index++) {
    Transport = mUsbMassTransport[Index];

    if (Interface.InterfaceProtocol == Transport->Protocol) {
      UsbMass->Transport = Transport;
      Status             = Transport->Init (UsbIo, Controller, &UsbMass->Context);
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((mUsbMscError, "USBMassDriverBindingStart: Transport->Init (%r)\n", Status));
    goto ON_ERROR;
  }

  UsbMass->Signature            = USB_MASS_SIGNATURE;
  UsbMass->Controller           = Controller;
  UsbMass->UsbIo                = UsbIo;
  UsbMass->BlockIo.Media        = &UsbMass->BlockIoMedia;
  UsbMass->BlockIo.Reset        = UsbMassReset;
  UsbMass->BlockIo.ReadBlocks   = UsbMassReadBlocks;
  UsbMass->BlockIo.WriteBlocks  = UsbMassWriteBlocks;
  UsbMass->BlockIo.FlushBlocks  = UsbMassFlushBlocks;
  UsbMass->OpticalStorage       = FALSE;
    
  //
  // Get the storage's parameters, such as last block number.
  // then install the BLOCK_IO
  //
  Status = UsbMassInitMedia (UsbMass);
  if (!EFI_ERROR (Status)) {
    if ((UsbMass->Pdt != USB_PDT_DIRECT_ACCESS) && 
         (UsbMass->Pdt != USB_PDT_CDROM) &&
         (UsbMass->Pdt != USB_PDT_OPTICAL) && 
         (UsbMass->Pdt != USB_PDT_SIMPLE_DIRECT)) {
      DEBUG ((mUsbMscError, "USBMassDriverBindingStart: Found an unsupported peripheral type[%d]\n", UsbMass->Pdt));
      goto ON_ERROR;  
    }
  } else if (Status != EFI_NO_MEDIA){
    DEBUG ((mUsbMscError, "USBMassDriverBindingStart: UsbMassInitMedia (%r)\n", Status));
    goto ON_ERROR;
  }
    
  Status = gBS->InstallProtocolInterface (
                  &Controller,
                  &gEfiBlockIoProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &UsbMass->BlockIo
                  );
  if (EFI_ERROR (Status)) {
    goto ON_ERROR;
  }

  return EFI_SUCCESS;

ON_ERROR:
  gBS->FreePool (UsbMass);

  gBS->CloseProtocol (
        Controller,
        &gEfiUsbIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  return Status;
}

EFI_STATUS
EFIAPI
USBMassDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL *This,
  IN  EFI_HANDLE                  Controller,
  IN  UINTN                       NumberOfChildren,
  IN  EFI_HANDLE                  *ChildHandleBuffer
  )
/*++

Routine Description:

  Stop controlling the device.

Arguments:

  This              - The USB mass storage driver binding
  Controller        - The device controller controlled by the driver.
  NumberOfChildren  - The number of children of this device
  ChildHandleBuffer - The buffer of children handle.

Returns:

  EFI_SUCCESS - The driver stopped from controlling the device.
  Others      - Failed to stop the driver 
  
--*/
{
  EFI_STATUS            Status;
  USB_MASS_DEVICE       *UsbMass;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;

  //
  // First, get our context back from the BLOCK_IO
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiBlockIoProtocolGuid,
                  &BlockIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  UsbMass = USB_MASS_DEVICE_FROM_BLOCKIO (BlockIo);

  //
  // Uninstall Block I/O protocol from the device handle,
  // then call the transport protocol to stop itself.
  //
  Status = gBS->UninstallProtocolInterface (
                  Controller,
                  &gEfiBlockIoProtocolGuid,
                  &UsbMass->BlockIo
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
        Controller,
        &gEfiUsbIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  UsbMass->Transport->Fini (UsbMass->Context);
  gBS->FreePool (UsbMass);

  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL gUSBMassDriverBinding = {
  USBMassDriverBindingSupported,
  USBMassDriverBindingStart,
  USBMassDriverBindingStop,
  0x11,
  NULL,
  NULL
};

EFI_DRIVER_ENTRY_POINT (USBMassStorageEntryPoint)

EFI_STATUS
EFIAPI
USBMassStorageEntryPoint (
  IN EFI_HANDLE               ImageHandle,
  IN EFI_SYSTEM_TABLE         *SystemTable
  )
/*++

Routine Description:

  The entry point for the driver, which will install the driver binding and 
  component name protocol

Arguments:

  ImageHandle - The image handle of this driver
  SystemTable - The system table

Returns:

  EFI_SUCCESS - the protocols are installed OK
  Others      - Failed to install protocols.

--*/
{
  EFI_STATUS  Status;

  //
  // Install driver binding protocol
  //
  Status = EfiLibInstallAllDriverProtocols (
             ImageHandle,
             SystemTable,
             &gUSBMassDriverBinding,
             ImageHandle,
             &gUsbMassStorageComponentName,
             NULL,
             NULL
             );

  return Status;
}