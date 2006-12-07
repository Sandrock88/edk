/*++

Copyright 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  genbootsector.c
  
Abstract:

Revision History

--*/

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRIVE 26

INT
GetDrvNumOffset (
  IN VOID *BootSector
  );

typedef enum {
  PatchTypeUnknown,
  PatchTypeFloppy,
  PatchTypeIde,
  PatchTypeUsb,
} PATCH_TYPE;

typedef enum {
  ErrorSuccess,
  ErrorFileCreate,
  ErrorFileReadWrite,
  ErrorNoMbr,
  ErrorFatType
} ERROR_STATUS;

typedef struct _DRIVE_TYPE_DESC {
  UINT  Type;
  CHAR  *Description;
} DRIVE_TYPE_DESC;

#define DRIVE_TYPE_ITEM(x) {x, #x}

DRIVE_TYPE_DESC DriveTypeDesc[] = {
  DRIVE_TYPE_ITEM (DRIVE_UNKNOWN),
  DRIVE_TYPE_ITEM (DRIVE_NO_ROOT_DIR),
  DRIVE_TYPE_ITEM (DRIVE_REMOVABLE),
  DRIVE_TYPE_ITEM (DRIVE_FIXED),
  DRIVE_TYPE_ITEM (DRIVE_REMOTE),
  DRIVE_TYPE_ITEM (DRIVE_CDROM),
  DRIVE_TYPE_ITEM (DRIVE_RAMDISK),
  (UINT) -1, NULL
};

CHAR *ErrorStatusDesc[] = {
  "Success",
  "Failed to create files",
  "Failed to read/write files",
  "No MBR exists",
  "Failed to detect Fat type"
};
typedef struct _DRIVE_INFO {
  CHAR              VolumeLetter;
  DRIVE_TYPE_DESC   *DriveType;
  UINT              DiskNumber;
} DRIVE_INFO;

#define BOOT_SECTOR_LBA_OFFSET 0x1FA

#define IsLetter(x) (((x) >= 'a' && (x) <= 'z') || ((x) >= 'A' && (x) <= 'Z'))

BOOL
GetDriveInfo (
  CHAR       VolumeLetter,
  DRIVE_INFO *DriveInfo
  )
{
  HANDLE                  VolumeHandle;
  STORAGE_DEVICE_NUMBER   StorageDeviceNumber;
  DWORD                   BytesReturned;
  BOOL                    Success;
  UINT                    DriveType;
  UINT                    Index;

  CHAR RootPath[]         = "X:\\";       // "X:\"  -> for GetDriveType
  CHAR VolumeAccessPath[] = "\\\\.\\X:";  // "\\.\X:"  -> to open the volume

  RootPath[0] = VolumeAccessPath[4] = VolumeLetter;
  DriveType = GetDriveType(RootPath);
  if (DriveType != DRIVE_REMOVABLE && DriveType != DRIVE_FIXED) {
    return FALSE;
  }

  DriveInfo->VolumeLetter = VolumeLetter;
  VolumeHandle = CreateFile (
                   VolumeAccessPath,
                   0,
                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                   NULL,
                   OPEN_EXISTING,
                   0,
                   NULL
                   );
  if (VolumeHandle == INVALID_HANDLE_VALUE) {
    fprintf (
      stderr, 
      "ERROR: CreateFile failed: Volume = %s, LastError = 0x%x\n", 
      VolumeAccessPath, 
      GetLastError ()
      );
    return FALSE;
  }

  //
  // Get Disk Number. It should fail when operating on floppy. That's ok because:
  // To direct write to disk:
  //   for USB and HD: use path = \\.\PHYSICALDRIVEx, where x is Disk Number
  //   for floppy:     use path = \\.\X:, where X can be A or B
  // So the Disk Number is only needed when operating on Hard or USB disk.
  //
  Success = DeviceIoControl(
              VolumeHandle, 
              IOCTL_STORAGE_GET_DEVICE_NUMBER,
              NULL, 
              0, 
              &StorageDeviceNumber, 
              sizeof(StorageDeviceNumber),
              &BytesReturned, 
              NULL
              );
  //
  // DeviceIoControl should fail if Volume is floppy or network drive.
  //
  if (!Success) {
    DriveInfo->DiskNumber = (UINT) -1;
  } else if (StorageDeviceNumber.DeviceType != FILE_DEVICE_DISK) {
    //
    // Only care about the disk.
    //
    return FALSE;
  } else{
    DriveInfo->DiskNumber = StorageDeviceNumber.DeviceNumber;
  }
  CloseHandle(VolumeHandle);
  
  DriveInfo->DriveType = NULL;
  for (Index = 0; DriveTypeDesc[Index].Description != NULL; Index ++) {
    if (DriveType == DriveTypeDesc[Index].Type) {
      DriveInfo->DriveType = &DriveTypeDesc[Index];
      break;
    }
  }

  if (DriveInfo->DriveType == NULL) {
    //
    // Should have a type.
    //
    fprintf (stderr, "ERROR: fetal error!!!\n");
    return FALSE;
  }
  return TRUE;
}

VOID
ListDrive (
  VOID
  )
{
  UINT       Index;
  DRIVE_INFO DriveInfo;
  
  UINT Mask =  GetLogicalDrives();

  for (Index = 0; Index < MAX_DRIVE; Index++) {
    if (((Mask >> Index) & 0x1) == 1) {
      if (GetDriveInfo ('A' + (CHAR) Index, &DriveInfo)) {
        if (Index < 2) {
          // Floppy will occupy 'A' and 'B'
          fprintf (
            stdout,
            "%c: - Type: %s\n",
            DriveInfo.VolumeLetter,
            DriveInfo.DriveType->Description
            );
        }
        else {
          fprintf (
            stdout,
            "%c: - DiskNum: %d, Type: %s\n", 
            DriveInfo.VolumeLetter,
            DriveInfo.DiskNumber, 
            DriveInfo.DriveType->Description
            );
        }
      }
    }
  }

}

DWORD
GetBootSectorOffset (
  HANDLE     DiskHandle,
  BOOL       WriteToDisk,
  PATCH_TYPE PatchType
  )
/*++
Description:
  Get the offset of boot sector.
  For non-MBR disk, offset is just 0
  for disk with MBR, offset needs to be caculated by parsing MBR

  Additional thing is done here:
    Manually select first partition as active one if no one is active: needs to patch MBR
--*/
{
  BYTE    DiskPartition[0x200];
  DWORD   BytesReturn;
  DWORD   DbrOffset;
  DWORD   Index;
  BOOL    HasMbr;

  DbrOffset = 0;
  HasMbr    = FALSE;
  
  //
  // Check whether having MBR
  //
  SetFilePointer(DiskHandle, 0, NULL, FILE_BEGIN);
  if (ReadFile (DiskHandle, DiskPartition, 0x200, &BytesReturn, NULL)) {
    // Check 55AA
    if ((DiskPartition[0x1FE] == 0x55) && (DiskPartition[0x1FF] == 0xAA)) {
      // Check Jmp
      if (((DiskPartition[0] != 0xEB) || (DiskPartition[2] != 0x90)) &&
          (DiskPartition[0] != 0xE9)) {
        // Check Boot Indicator
        if (((DiskPartition[0x1BE] == 0x0) || (DiskPartition[0x1BE] == 0x80)) &&
            ((DiskPartition[0x1CE] == 0x0) || (DiskPartition[0x1CE] == 0x80)) &&
            ((DiskPartition[0x1DE] == 0x0) || (DiskPartition[0x1DE] == 0x80)) &&
            ((DiskPartition[0x1EE] == 0x0) || (DiskPartition[0x1EE] == 0x80))) {
          //
          // Check Signature, Jmp, and Boot Indicator.
          // if all pass, we assume MBR found.
          //
          HasMbr = TRUE;
        }
      }
    }
  }

  if (HasMbr) {
    //
    // Skip MBR
    //
    fprintf (stdout, "Skip MBR!\n");
    for (Index = 0; Index < 4; Index++) {
      //
      // Found Boot Indicator.
      //
      if (DiskPartition[0x1BE + (Index * 0x10)] == 0x80) {
        DbrOffset = *(DWORD *)&DiskPartition[0x1BE + (Index * 0x10) + 8];
        break;
      }
    }
    //
    // If no boot indicator, we manually select 1st partition, and patch MBR accordingly.
    //
    if (Index == 4) {
      DbrOffset = *(DWORD *)&DiskPartition[0x1BE + 8];
      if (WriteToDisk && (PatchType == PatchTypeUsb)) {
        SetFilePointer(DiskHandle, 0, NULL, FILE_BEGIN);
        DiskPartition[0x1BE] = 0x80;
        WriteFile (DiskHandle, DiskPartition, 0x200, &BytesReturn, NULL);
      }
    }
  }

  return DbrOffset;
}

ERROR_STATUS
ProcessBootSector (
  CHAR        *DiskName,
  CHAR        *FileName,
  BOOL        WriteToDisk,
  PATCH_TYPE  PatchType,
  BOOL        ProcessMbr
  )
{
  BYTE    DiskPartition[0x200];
  BYTE    DiskPartitionBackup[0x200];
  HANDLE  DiskHandle;
  HANDLE  FileHandle;
  DWORD   BytesReturn;
  DWORD   DbrOffset;
  INT     DrvNumOffset;

  DiskHandle = CreateFile (
                 DiskName, 
                 GENERIC_READ | GENERIC_WRITE, 
                 FILE_SHARE_READ, 
                 NULL, 
                 OPEN_EXISTING, 
                 FILE_ATTRIBUTE_NORMAL, 
                 NULL
                 );
  if (DiskHandle == INVALID_HANDLE_VALUE) {
    return ErrorFileCreate;
  }

  FileHandle = CreateFile (
                 FileName,
                 GENERIC_READ | GENERIC_WRITE,
                 0,
                 NULL,
                 OPEN_ALWAYS,
                 FILE_ATTRIBUTE_NORMAL,
                 NULL
                 );
  if (FileHandle == INVALID_HANDLE_VALUE) {
    return ErrorFileCreate;
  }

  DbrOffset = 0;
  //
  // Skip potential MBR for Ide & USB disk
  //
  if ((PatchType == PatchTypeIde) || (PatchType == PatchTypeUsb)) {
    DbrOffset = GetBootSectorOffset (DiskHandle, WriteToDisk, PatchType);

    if (!ProcessMbr) {
      SetFilePointer (DiskHandle, DbrOffset * 0x200, NULL, FILE_BEGIN);
    }
    else if(DbrOffset == 0) {
      //
      // If user want to process Mbr, but no Mbr exists, simply return FALSE
      //
      return ErrorNoMbr;
    }
    else {
      SetFilePointer (DiskHandle, 0, NULL, FILE_BEGIN);
    }
  }

  //
  // [File Pointer is pointed to beginning of Mbr or Dbr]
  //
  if (WriteToDisk) {
    //
    // Write
    //
    if (!ReadFile (FileHandle, DiskPartition, 0x200, &BytesReturn, NULL)) {
      return ErrorFileReadWrite;
    }
    if (ProcessMbr) {
      //
      // Use original partition table
      //
      if (!ReadFile (DiskHandle, DiskPartitionBackup, 0x200, &BytesReturn, NULL)) {
        return ErrorFileReadWrite;
      }
      memcpy (DiskPartition + 0x1BE, DiskPartitionBackup + 0x1BE, 0x40);
      SetFilePointer (DiskHandle, 0, NULL, FILE_BEGIN);
    }

    if (!WriteFile (DiskHandle, DiskPartition, 0x200, &BytesReturn, NULL)) {
      return ErrorFileReadWrite;
    }

  } else {
    //
    // Read
    //
    if (!ReadFile (DiskHandle, DiskPartition, 0x200, &BytesReturn, NULL)) {
      return ErrorFileReadWrite;
    }

    if (PatchType == PatchTypeUsb) {
      // Manually set BS_DrvNum to 0x80 as window's format.exe has a bug which will clear this field discarding whether USB disk's MBR. 
      // offset of BS_DrvNum is 0x24 for FAT12/16
      //                        0x40 for FAT32
      //
      DrvNumOffset = GetDrvNumOffset (DiskPartition);
      if (DrvNumOffset == -1) {
        return ErrorFatType;
      }
      //
      // Some legacy BIOS require 0x80 discarding MBR.
      // Question left here: is it needed to check Mbr before set 0x80?
      //
      DiskPartition[DrvNumOffset] = ((DbrOffset > 0) ? 0x80 : 0);
  }


    if (PatchType == PatchTypeIde) {
      //
      // Patch LBAOffsetForBootSector
      //
      *(DWORD *)&DiskPartition [BOOT_SECTOR_LBA_OFFSET] = DbrOffset;
    }
    if (!WriteFile (FileHandle, DiskPartition, 0x200, &BytesReturn, NULL)) {
      return ErrorFileReadWrite;
    }
  }
  CloseHandle (FileHandle);
  CloseHandle (DiskHandle);
  return ErrorSuccess;
}

VOID
PrintUsage (
  CHAR* AppName
  )
{
  fprintf (
    stdout,
    "Usage: %s [OPTIONS]...\n"
    "Copy file content from/to bootsector.\n"
    "\n"
    "  -l        list disks\n"
    "  -if=FILE  specified an input, can be files or disks\n"
    "  -of=FILE  specified an output, can be files or disks\n"
    "  -mbr      process MBR also\n"
    "  -h        print this message\n"
    "\n"
    "FILE providing a volume plus a colon (X:), indicates a disk\n"
    "FILE providing other format, indicates a file\n",
    AppName
    );
}
 
INT
main (
  INT  argc,
  CHAR *argv[]
  )
{
  CHAR          *AppName;
  INT           Index;
  BOOL          ProcessMbr;
  CHAR          VolumeLetter;
  CHAR          *FilePath;
  BOOL          WriteToDisk;
  DRIVE_INFO    DriveInfo;
  PATCH_TYPE    PatchType;
  ERROR_STATUS  Status;

  CHAR        FloppyPathTemplate[] = "\\\\.\\%c:";
  CHAR        DiskPathTemplate[]   = "\\\\.\\PHYSICALDRIVE%u";
  CHAR        DiskPath[MAX_PATH];

  AppName = *argv;
  argv ++;
  argc --;
  
  ProcessMbr    = FALSE;
  WriteToDisk   = TRUE;
  FilePath      = NULL;
  VolumeLetter  = 0;

  //
  // Parse command line
  //
  for (Index = 0; Index < argc; Index ++) {
    if (stricmp (argv[Index], "-l") == 0) {
      ListDrive ();
      return 0;
    }
    else if (stricmp (argv[Index], "-mbr") == 0) {
      ProcessMbr = TRUE;
    }
    else if ((strnicmp (argv[Index], "-if=", 4) == 0) ||
             (strnicmp (argv[Index], "-of=", 4) == 0)
             ) {
      if (argv[Index][6] == '\0' && argv[Index][5] == ':' && IsLetter (argv[Index][4])) {
        VolumeLetter = argv[Index][4];
        if (strnicmp (argv[Index], "-if=", 4) == 0) {
          WriteToDisk = FALSE;
        }
      }
      else {
        FilePath = &argv[Index][4];
      }
    }
    else {
      PrintUsage (AppName);
      return 1;
    }
  }

  //
  // Check parameter
  //
  if (VolumeLetter == 0) {
    fprintf (stderr, "ERROR: Volume isn't provided!\n");
    PrintUsage (AppName);
    return 1;
  }
  
  if (FilePath == NULL) {
    fprintf (stderr, "ERROR: File isn't pvovided!\n");
    PrintUsage (AppName);
    return 1;
  }
    
  PatchType = PatchTypeUnknown;

  if ((VolumeLetter == 'A') || (VolumeLetter == 'a') || 
      (VolumeLetter == 'B') || (VolumeLetter == 'b') 
      ) {
    //
    // Floppy
    //
    sprintf (DiskPath, FloppyPathTemplate, VolumeLetter);
    PatchType = PatchTypeFloppy;
  }
  else {
    //
    // Hard/USB disk
    //
    if (!GetDriveInfo (VolumeLetter, &DriveInfo)) {
      fprintf (stderr, "ERROR: GetDriveInfo - 0x%x\n", GetLastError ());
      return 1;
    }

    //
    // Shouldn't patch my own hard disk, but can read it.
    // very safe then:)
    //
    if (DriveInfo.DriveType->Type == DRIVE_FIXED && WriteToDisk) {
      fprintf (stderr, "ERROR: Write to local harddisk - permission denied!\n");
      return 1;
    }
    
    sprintf (DiskPath, DiskPathTemplate, DriveInfo.DiskNumber);
    if (DriveInfo.DriveType->Type == DRIVE_REMOVABLE) {
      PatchType = PatchTypeUsb;
    }
    else if (DriveInfo.DriveType->Type == DRIVE_FIXED) {
      PatchType = PatchTypeIde;
    }
  }

  if (PatchType == PatchTypeUnknown) {
    fprintf (stderr, "ERROR: PatchType unknown!\n");
    return 1;
  }

  //
  // Process DBR (Patch or Read)
  //
  Status = ProcessBootSector (DiskPath, FilePath, WriteToDisk, PatchType, ProcessMbr);
  if (Status == ErrorSuccess) {
    fprintf (
      stdout, 
      "%s %s successfully!\n", 
      WriteToDisk ? "Write" : "Read", 
      ProcessMbr ? "MBR" : "DBR"
      );
    return 0;
  }
  else {
    fprintf (
      stderr, 
      "ERROR: %s %s failed - %s (LastError: 0x%x)!\n", 
      WriteToDisk ? "Write" : "Read", 
      ProcessMbr ? "MBR" : "DBR", 
      ErrorStatusDesc[Status],
      GetLastError ()
      );
    return 1;
  }
}