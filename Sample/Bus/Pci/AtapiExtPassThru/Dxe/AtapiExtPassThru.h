/*++

Copyright (c) 2004 - 2007, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

    AtapiPassThru.h
    
Abstract: 
    

Revision History
--*/

#ifndef _APT_H
#define _APT_H

#include "Tiano.h"
#include "EfiDriverLib.h"
#include "Pci22.h"

//
// Driver Consumed Protocol Prototypes
//
#include EFI_PROTOCOL_DEFINITION (DevicePath)
#include EFI_PROTOCOL_DEFINITION (PciIo)

//
// Driver Produced Protocol Prototypes
//
#include EFI_PROTOCOL_DEFINITION (DriverBinding)
#include EFI_PROTOCOL_DEFINITION (ComponentName)
#include EFI_PROTOCOL_DEFINITION (ComponentName2)
#include EFI_PROTOCOL_DEFINITION (ScsiPassThru)
#include EFI_PROTOCOL_DEFINITION (ScsiPassThruExt)

//
// bit definition
//
#define bit(a)        (1 << (a))

#define MAX_TARGET_ID 4
//
// IDE Registers
//
typedef union {
  UINT16  Command;        /* when write */
  UINT16  Status;         /* when read */
} IDE_CMD_OR_STATUS;

typedef union {
  UINT16  Error;          /* when read */
  UINT16  Feature;        /* when write */
} IDE_ERROR_OR_FEATURE;

typedef union {
  UINT16  AltStatus;      /* when read */
  UINT16  DeviceControl;  /* when write */
} IDE_AltStatus_OR_DeviceControl;


typedef enum {
  IdePrimary    = 0,
  IdeSecondary  = 1,
  IdeMaxChannel = 2
} EFI_IDE_CHANNEL;

//
// bit definition
//
#define bit0  (1 << 0)
#define bit1  (1 << 1)
#define bit2  (1 << 2)
#define bit3  (1 << 3)


//
// Bit definitions in Programming Interface byte of the Class Code field
// in PCI IDE controller's Configuration Space
//
#define IDE_PRIMARY_OPERATING_MODE            bit0
#define IDE_PRIMARY_PROGRAMMABLE_INDICATOR    bit1
#define IDE_SECONDARY_OPERATING_MODE          bit2
#define IDE_SECONDARY_PROGRAMMABLE_INDICATOR  bit3


#define ATAPI_MAX_CHANNEL 2

//
// IDE registers set
//
typedef struct {
  UINT16                          Data;
  IDE_ERROR_OR_FEATURE            Reg1;
  UINT16                          SectorCount;
  UINT16                          SectorNumber;
  UINT16                          CylinderLsb;
  UINT16                          CylinderMsb;
  UINT16                          Head;
  IDE_CMD_OR_STATUS               Reg;
  IDE_AltStatus_OR_DeviceControl  Alt;
  UINT16                          DriveAddress;
} IDE_BASE_REGISTERS;

#define ATAPI_SCSI_PASS_THRU_DEV_SIGNATURE  EFI_SIGNATURE_32 ('a', 's', 'p', 't')

typedef struct {
  UINTN                            Signature;
  EFI_HANDLE                       Handle;
  EFI_EXT_SCSI_PASS_THRU_PROTOCOL  ScsiPassThru;
  EFI_EXT_SCSI_PASS_THRU_MODE      ScsiPassThruMode;
  EFI_PCI_IO_PROTOCOL              *PciIo;
  IDE_BASE_REGISTERS               *IoPort;
  IDE_BASE_REGISTERS               AtapiIoPortRegisters[2];
  CHAR16                           ControllerName[100];
  CHAR16                           ChannelName[100];
  UINT8                            LatestTargetId;
  UINT64                           LatestLun;
} ATAPI_EXT_SCSI_PASS_THRU_DEV;


//
// IDE registers' base addresses
//
typedef struct {
  UINT16  CommandBlockBaseAddr;
  UINT16  ControlBlockBaseAddr;
} IDE_REGISTERS_BASE_ADDR;



#define ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS(a) \
  CR (a, \
      ATAPI_EXT_SCSI_PASS_THRU_DEV, \
      ScsiPassThru, \
      ATAPI_SCSI_PASS_THRU_DEV_SIGNATURE \
      )
//
// Global Variables
//
extern EFI_DRIVER_BINDING_PROTOCOL  gAtapiScsiPassThruDriverBinding;
#if (EFI_SPECIFICATION_VERSION >= 0x00020000)
extern EFI_COMPONENT_NAME2_PROTOCOL gAtapiScsiPassThruComponentName;
#else
extern EFI_COMPONENT_NAME_PROTOCOL  gAtapiScsiPassThruComponentName;
#endif

//
// ATAPI Command op code
//
#define OP_INQUIRY                      0x12
#define OP_LOAD_UNLOAD_CD               0xa6
#define OP_MECHANISM_STATUS             0xbd
#define OP_MODE_SELECT_10               0x55
#define OP_MODE_SENSE_10                0x5a
#define OP_PAUSE_RESUME                 0x4b
#define OP_PLAY_AUDIO_10                0x45
#define OP_PLAY_AUDIO_MSF               0x47
#define OP_PLAY_CD                      0xbc
#define OP_PLAY_CD_MSF                  0xb4
#define OP_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1e
#define OP_READ_10                      0x28
#define OP_READ_12                      0xa8
#define OP_READ_CAPACITY                0x25
#define OP_READ_CD                      0xbe
#define OP_READ_CD_MSF                  0xb9
#define OP_READ_HEADER                  0x44
#define OP_READ_SUB_CHANNEL             0x42
#define OP_READ_TOC                     0x43
#define OP_REQUEST_SENSE                0x03
#define OP_SCAN                         0xba
#define OP_SEEK_10                      0x2b
#define OP_SET_CD_SPEED                 0xbb
#define OP_STOPPLAY_SCAN                0x4e
#define OP_START_STOP_UNIT              0x1b
#define OP_TEST_UNIT_READY              0x00
#define OP_FORMAT_UNIT                  0x04
#define OP_READ_FORMAT_CAPACITIES       0x23
#define OP_VERIFY                       0x2f
#define OP_WRITE_10                     0x2a
#define OP_WRITE_12                     0xaa
#define OP_WRITE_AND_VERIFY             0x2e

//
// ATA Command
//
#define ATAPI_SOFT_RESET_CMD  0x08

typedef enum {
  DataIn  = 0,
  DataOut = 1,
  DataBi  = 2,
  NoData  = 3,
  End     = 0xff
} DATA_DIRECTION;

typedef struct {
  UINT8           OpCode;
  DATA_DIRECTION  Direction;
} SCSI_COMMAND_SET;

#define MAX_CHANNEL         2

#define ValidCdbLength(Len) ((Len) == 6 || (Len) == 10 || (Len) == 12) ? 1 : 0

//
// IDE registers bit definitions
//
// ATA Err Reg bitmap
//
#define BBK_ERR   bit (7) /* Bad block detected */
#define UNC_ERR   bit (6) /* Uncorrectable Data */
#define MC_ERR    bit (5) /* Media Change */
#define IDNF_ERR  bit (4) /* ID Not Found */
#define MCR_ERR   bit (3) /* Media Change Requested */
#define ABRT_ERR  bit (2) /* Aborted Command */
#define TK0NF_ERR bit (1) /* Track 0 Not Found */
#define AMNF_ERR  bit (0) /* Address Mark Not Found */

//
// ATAPI Err Reg bitmap
//
#define SENSE_KEY_ERR (bit (7) | bit (6) | bit (5) | bit (4))
#define EOM_ERR bit (1) /* End of Media Detected */
#define ILI_ERR bit (0) /* Illegal Length Indication */

//
// Device/Head Reg
//
#define LBA_MODE  bit (6)
#define DEV       bit (4)
#define HS3       bit (3)
#define HS2       bit (2)
#define HS1       bit (1)
#define HS0       bit (0)
#define CHS_MODE  (0)
#define DRV0      (0)
#define DRV1      (1)
#define MST_DRV   DRV0
#define SLV_DRV   DRV1

//
// Status Reg
//
#define BSY   bit (7) /* Controller Busy */
#define DRDY  bit (6) /* Drive Ready */
#define DWF   bit (5) /* Drive Write Fault */
#define DSC   bit (4) /* Disk Seek Complete */
#define DRQ   bit (3) /* Data Request */
#define CORR  bit (2) /* Corrected Data */
#define IDX   bit (1) /* Index */
#define ERR   bit (0) /* Error */
#define CHECK bit (0) /* Check bit for ATAPI Status Reg */

//
// Device Control Reg
//
#define SRST  bit (2) /* Software Reset */
#define IEN_L bit (1) /* Interrupt Enable #*/

//
// ATAPI Feature Register
//
#define OVERLAP bit (1)
#define DMA     bit (0)

//
// ATAPI Interrupt Reason Reson Reg (ATA Sector Count Register)
//
#define RELEASE     bit (2)
#define IO          bit (1)
#define CoD         bit (0)

#define PACKET_CMD  0xA0

#define DEFAULT_CMD (0xa0)
//
// default content of device control register, disable INT
//
#define DEFAULT_CTL           (0x0a)
#define MAX_ATAPI_BYTE_COUNT  (0xfffe)

//
// function prototype
//
EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
 /*++

Routine Description:

  Entry point for EFI drivers.

Arguments:

  ImageHandle - EFI_HANDLE
  SystemTable - EFI_SYSTEM_TABLE

Returns:

  EFI_SUCCESS
  Others 

--*/
;

EFI_STATUS
RegisterAtapiExtScsiPassThru (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Controller,
  IN  EFI_PCI_IO_PROTOCOL          *PciIo
  )
/*++

Routine Description:
  Attaches SCSI Pass Thru Protocol for specified IDE channel.
    
Arguments:
  This              - Protocol instance pointer.
  Controller        - Parent device handle to the IDE channel.    
  PciIo             - PCI I/O protocol attached on the "Controller".                        
  
Returns:

  Always return EFI_SUCCESS unless installing SCSI Pass Thru Protocol failed.

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruFunction (
  IN EFI_EXT_SCSI_PASS_THRU_PROTOCOL                    *This,
  IN UINT8                                              *Target,
  IN UINT64                                             Lun,
  IN OUT EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET     *Packet,
  IN EFI_EVENT                                          Event OPTIONAL
  )
/*++

Routine Description:

  Implements EFI_EXT_SCSI_PASS_THRU_PROTOCOL.PassThru() function.

Arguments:

  This:     The EFI_EXT_SCSI_PASS_THRU_PROTOCOL instance.
  Target:   The Target ID of the ATAPI device to send the SCSI 
            Request Packet. To ATAPI devices attached on an IDE
            Channel, Target ID 0 indicates Master device;Target
            ID 1 indicates Slave device.
  Lun:      The LUN of the ATAPI device to send the SCSI Request
            Packet. To the ATAPI device, Lun is always 0.
  Packet:   The SCSI Request Packet to send to the ATAPI device 
            specified by Target and Lun.
  Event:    If non-blocking I/O is not supported then Event is ignored, 
            and blocking I/O is performed.
            If Event is NULL, then blocking I/O is performed.
            If Event is not NULL and non blocking I/O is supported, 
            then non-blocking I/O is performed, and Event will be signaled 
            when the SCSI Request Packet completes.      

Returns:  

  EFI_STATUS

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruGetNextTargetLun (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN OUT UINT8                           **Target,
  IN OUT UINT64                          *Lun
  )
/*++

Routine Description:

  Used to retrieve the list of legal Target IDs for SCSI devices 
  on a SCSI channel.

Arguments:

  This                  - Protocol instance pointer.
  Target                - On input, a pointer to the Target ID of a SCSI 
                          device present on the SCSI channel.  On output, 
                          a pointer to the Target ID of the next SCSI device
                          present on a SCSI channel.  An input value of 
                          0xFFFFFFFF retrieves the Target ID of the first 
                          SCSI device present on a SCSI channel.
  Lun                   - On input, a pointer to the LUN of a SCSI device
                          present on the SCSI channel. On output, a pointer
                          to the LUN of the next SCSI device present on 
                          a SCSI channel.
Returns:

  EFI_SUCCESS           - The Target ID and Lun of the next SCSI device 
                          on the SCSI channel was returned in Target and Lun.
  EFI_NOT_FOUND         - There are no more SCSI devices on this SCSI channel.
  EFI_INVALID_PARAMETER - Target is not 0xFFFFFFFF,and Target and Lun were not
                           returned on a previous call to GetNextDevice().

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruBuildDevicePath (
  IN     EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN     UINT8                              *Target,
  IN     UINT64                             Lun,
  IN OUT EFI_DEVICE_PATH_PROTOCOL           **DevicePath
  )
/*++

Routine Description:

  Used to allocate and build a device path node for a SCSI device 
  on a SCSI channel. Would not build device path for a SCSI Host Controller.

Arguments:

  This                  - Protocol instance pointer.
  Target                - The Target ID of the SCSI device for which
                          a device path node is to be allocated and built.
  Lun                   - The LUN of the SCSI device for which a device 
                          path node is to be allocated and built.
  DevicePath            - A pointer to a single device path node that 
                          describes the SCSI device specified by 
                          Target and Lun. This function is responsible 
                          for allocating the buffer DevicePath with the boot
                          service AllocatePool().  It is the caller's 
                          responsibility to free DevicePath when the caller
                          is finished with DevicePath.    
  Returns:
  EFI_SUCCESS           - The device path node that describes the SCSI device
                          specified by Target and Lun was allocated and 
                          returned in DevicePath.
  EFI_NOT_FOUND         - The SCSI devices specified by Target and Lun does
                          not exist on the SCSI channel.
  EFI_INVALID_PARAMETER - DevicePath is NULL.
  EFI_OUT_OF_RESOURCES  - There are not enough resources to allocate 
                          DevicePath.

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruGetTargetLun (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN  EFI_DEVICE_PATH_PROTOCOL       *DevicePath,
  OUT UINT8                          **Target,
  OUT UINT64                         *Lun
  )
/*++

Routine Description:

  Used to translate a device path node to a Target ID and LUN.

Arguments:

  This                  - Protocol instance pointer.
  DevicePath            - A pointer to the device path node that 
                          describes a SCSI device on the SCSI channel.
  Target                - A pointer to the Target ID of a SCSI device 
                          on the SCSI channel. 
  Lun                   - A pointer to the LUN of a SCSI device on 
                          the SCSI channel.    
Returns:

  EFI_SUCCESS           - DevicePath was successfully translated to a 
                          Target ID and LUN, and they were returned 
                          in Target and Lun.
  EFI_INVALID_PARAMETER - DevicePath/Target/Lun is NULL.
  EFI_UNSUPPORTED       - This driver does not support the device path 
                          node type in DevicePath.
  EFI_NOT_FOUND         - A valid translation from DevicePath to a 
                          Target ID and LUN does not exist.

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruResetChannel (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL   *This
  )
/*++

Routine Description:

  Resets a SCSI channel.This operation resets all the 
  SCSI devices connected to the SCSI channel.

Arguments:

  This                  - Protocol instance pointer.

Returns:

  EFI_SUCCESS           - The SCSI channel was reset.
  EFI_UNSUPPORTED       - The SCSI channel does not support 
                          a channel reset operation.
  EFI_DEVICE_ERROR      - A device error occurred while 
                          attempting to reset the SCSI channel.
  EFI_TIMEOUT           - A timeout occurred while attempting 
                          to reset the SCSI channel.

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruResetTarget (
  IN EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN UINT8                              *Target,
  IN UINT64                             Lun
  )
/*++

Routine Description:

  Resets a SCSI device that is connected to a SCSI channel.

Arguments:

  This                  - Protocol instance pointer.
  Target                - The Target ID of the SCSI device to reset. 
  Lun                   - The LUN of the SCSI device to reset.
    
Returns:

  EFI_SUCCESS           - The SCSI device specified by Target and 
                          Lun was reset.
  EFI_UNSUPPORTED       - The SCSI channel does not support a target
                          reset operation.
  EFI_INVALID_PARAMETER - Target or Lun are invalid.
  EFI_DEVICE_ERROR      - A device error occurred while attempting 
                          to reset the SCSI device specified by Target 
                          and Lun.
  EFI_TIMEOUT           - A timeout occurred while attempting to reset 
                          the SCSI device specified by Target and Lun.

--*/
;

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruGetNextTarget (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN OUT UINT8                           **Target
  )
/*++

Routine Description:
  Used to retrieve the list of legal Target IDs for SCSI devices 
  on a SCSI channel.

Arguments:
  This                  - Protocol instance pointer.
  Target                - On input, a pointer to the Target ID of a SCSI 
                          device present on the SCSI channel.  On output, 
                          a pointer to the Target ID of the next SCSI device
                           present on a SCSI channel.  An input value of 
                           0xFFFFFFFF retrieves the Target ID of the first 
                           SCSI device present on a SCSI channel.
  Lun                   - On input, a pointer to the LUN of a SCSI device
                          present on the SCSI channel. On output, a pointer
                          to the LUN of the next SCSI device present on 
                          a SCSI channel.
    
Returns:
  EFI_SUCCESS           - The Target ID and Lun of the next SCSI device 
                          on the SCSI channel was returned in Target and Lun.
  EFI_NOT_FOUND         - There are no more SCSI devices on this SCSI channel.
  EFI_INVALID_PARAMETER - Target is not 0xFFFFFFFF,and Target and Lun were not
                          returned on a previous call to GetNextDevice().

--*/
;

#endif
