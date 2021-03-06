#/*++
#
#  Copyright (c) 2006 - 2008, Intel Corporation                                                         
#  All rights reserved. This program and the accompanying materials                          
#  are licensed and made available under the terms and conditions of the BSD License         
#  which accompanies this distribution.  The full text of the license may be found at        
#  http://opensource.org/licenses/bsd-license.php                                            
#                                                                                            
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             
#  
#  Module Name:
#  
#    Makefile
#  
#  Abstract:
#   
#    This makefile is used to build the platform tip.
#   
#--*/

#
# Everything depends on EDK_SOURCE & EDK_TOOLS_PATH. Make sure it's defined
#
!IFNDEF EDK_SOURCE
!ERROR EDK_SOURCE environmental variable not set 
!ENDIF

!IFNDEF EDK_TOOLS_PATH
!MESSAGE EDK_TOOLS_PATH environmental variable not set, default setting used
!ENDIF

#
# Set the processor type
#
PROCESSOR = IA32

#
# Define a toolchain so we can pick up the basic defines from
# the EFI tool setup file.
#
TOOLCHAIN = TOOLCHAIN_MSVC

#
# Include the platform-specific settings file
#
!INCLUDE PlatformTools.env

#
# Default target to create the makefiles and then build the drivers
#
all : tempstamp_file build_tools builds loader

#
# Create a file to store the date and time, and used by SetStamp.exe
#
tempstamp_file:
!IF DEFINED(DATE_STAMP) && DEFINED(TIME_STAMP)
  @echo $(DATE_STAMP) $(TIME_STAMP) > GenStamp.txt
!ELSEIF EXIST(GenStamp.txt)
  - @del GenStamp.txt
!ENDIF

#
# Make all the build tools by calling the makefile
#
build_tools :
  -if not exist $(EDK_TOOLS_OUTPUT) mkdir $(EDK_TOOLS_OUTPUT)
  -if exist $(EDK_TOOLS_BIN)\*.* copy $(EDK_TOOLS_BIN)\*.* $(EDK_TOOLS_OUTPUT)
  $(MAKE) -f $(EDK_TOOLS_SOURCE)\makefile PROCESSOR=$(PROCESSOR) BUILD_DIR=$(BUILD_DIR)
  $(MAKE) -f $(DUET_TOOLS_SOURCE)\makefile PROCESSOR=$(PROCESSOR) BUILD_DIR=$(BUILD_DIR)
  $(GUIDCHK) -b $(EDK_TOOLS_OUTPUT)\GuidDatabase.txt  

PROCESSOR=$(BUILD_TARGET_PROCESSOR)

COMMON_PROCESSDSC_OPTIONS =                        \
    -d EFI_SOURCE=$(EDK_SOURCE)                    \
    -d EDK_SOURCE=$(EDK_SOURCE)                    \
    -d PROCESSOR=$(PROCESSOR)                      \
    -d PROJECT_NAME=$(PROJECT_NAME)                \
    -d COMPRESS_METHOD=$(COMPRESS_METHOD)          \
    -d OEMTOOLPATH=$(EDK_TOOLS_OUTPUT)             \
    -d SHELL_INF=$(SHELL_INF)                      \
    -d UEFI_PREFIX=$(UEFI_PREFIX)                  \
    -d PI_PREFIX=$(PI_PREFIX)                      \
    -d CAPSULE_INF=$(CAPSULE_INF)                  \
    -d ATAPI_PASS_THRU_INF=$(ATAPI_PASS_THRU_INF)  \
    -d DEVPATH_INF=$(DEVPATH_INF)                  \
    -g $(EDK_TOOLS_OUTPUT)\GuidDatabase.txt        \
    -xref FV\Guid.xref

#
# Run ProcessDsc on our main description files to generate the
# makefiles and package files. Specify -v option for more verbose output.
# Specify "-xref filename" to generate a cross-reference file of 
# basename-to-fileguid that can be used by VolInfo.
#
makefiles : 
  $(PROCESSDSC) $(COMMON_PROCESSDSC_OPTIONS) $(PROJECT_NAME).dsc
    
#
# Create the guid-to-basename cross reference file of protocols
# used by volinfo for decoding dependency expressions.
#
protocols.xref : 
  -@if not exist fv mkdir FV
  cd $(EDK_SOURCE)
  $(GUIDCHK) -g -u $(BUILD_TARGET_DIRECTORY) -e inf -x > $(BUILD_DIR)\Fv\protocols.xref
  cd $(BUILD_DIR)

#
# Multi-thread build using ProcessDsc utility.
#
builds :
  $(PROCESSDSC) $(COMMON_PROCESSDSC_OPTIONS) -n $(BUILD_THREAD_NUMBER) $(PROJECT_NAME).dsc

#
# Define a pseudo-target to start building with the libraries or with the 
# components.
#
libraries :
  $(MAKE) -f makefile.out libraries 

components :
  $(MAKE) -f makefile.out components 

#
# Make browse info file
#
bsc:
  - dir /s /b *.sbr > $(PROCESSOR)\temp.rm
  $(BSCMAKE) /n /o $(PROCESSOR)\$(PROJECT_NAME).bsc "@$(PROCESSOR)\temp.rm"

#
# Create a target to clean up all build directories
#
clean : cleantools cleanbuilds

#
# Clean up all build directories except for tools.
#
cleanbuilds :
  - @if exist *.bak del *.bak > NUL
  - @if exist ia32 rd /s/q ia32 > NUL
  - @if exist ipf rd /s /q ipf > NUL
  - @if exist ebc rd /s /q ebc > NUL
  - @if exist x64 rd /s /q x64 > NUL
  - @if exist fv rd /s /q fv > NUL
  - @if exist log rd /s /q log > NUL
  - @if exist makefile.out del makefile.out > NUL
  - @if exist module.* del module.* > NUL
  - @echo All modules cleaned

#
# Clean up only the tools
#
cleantools :
  - @if exist vc??.* del vc??.* > NUL
  - @if exist Tools rd /s /q Tools > NUL
  - @echo All tools cleaned

#
# Rebuild target, clean and build all
#
rebuild : clean all

#
# cleanall target to clean all build directories
# and binary files in $(EFI_PLATFORM_BIN).
#
cleanall : cleantools cleanbuilds
  - @if exist $(EFI_PLATFORM_BIN) rd /s /q $(EFI_PLATFORM_BIN) > NUL
  - @if exist $(EFI_BINARY) rd $(EFI_BINARY) > NUL 2>&1
  - @echo All targets cleaned

loader:
  $(MAKE) loader \
          -f $(BUILD_DIR)\..\Loader\EfiLdr\$(PROCESSOR)\Makefile \
          BUILD_DIR=$(BUILD_DIR) \
          PROCESSOR=$(PROCESSOR)

!IFNDEF EFI_BOOT_DISK
!MESSAGE EFI_BOOT_DISK environmental variable not set - e.g. set EFI_BOOT_DISK=e:, now assume a:
EFI_BOOT_DISK=a:
!ENDIF

!IF "$(EFI_BOOT_DISK)" == "a:" || "$(EFI_BOOT_DISK)" == "A:" || "$(EFI_BOOT_DISK)" == "b:" || "$(EFI_BOOT_DISK)" == "B:"
EFI_SHELL_BIN = Shell.efi
!ELSE
EFI_SHELL_BIN = Shell_Full.efi
!ENDIF

floppy:
  -copy Fv\Efildr $(EFI_BOOT_DISK)\Efildr /Y
  -copy Fv\EfiExtended.fv $(EFI_BOOT_DISK)\ /Y
  -@if not exist $(EFI_BOOT_DISK)\efi\boot ( mkdir $(EFI_BOOT_DISK)\efi\boot )
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\$(EFI_SHELL_BIN) $(EFI_BOOT_DISK)\efi\boot\boot$(PROCESSOR).efi /Y
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\Apps\LoadFv.efi $(EFI_BOOT_DISK)\ /Y

usb:
  -copy Fv\Efildr16 $(EFI_BOOT_DISK)\Efildr16 /Y
  -copy Fv\EfiExtended.fv $(EFI_BOOT_DISK)\ /Y
  -@if not exist $(EFI_BOOT_DISK)\efi\boot ( mkdir $(EFI_BOOT_DISK)\efi\boot )
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\$(EFI_SHELL_BIN) $(EFI_BOOT_DISK)\efi\boot\boot$(PROCESSOR).efi /Y
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\Apps\LoadFv.efi $(EFI_BOOT_DISK)\ /Y

usb32:
  -copy Fv\Efildr20 $(EFI_BOOT_DISK)\Efildr20 /Y
  -copy Fv\EfiExtended.fv $(EFI_BOOT_DISK)\ /Y
  -@if not exist $(EFI_BOOT_DISK)\efi\boot ( mkdir $(EFI_BOOT_DISK)\efi\boot )
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\$(EFI_SHELL_BIN) $(EFI_BOOT_DISK)\efi\boot\boot$(PROCESSOR).efi /Y
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\Apps\LoadFv.efi $(EFI_BOOT_DISK)\ /Y

hd:
  -copy Fv\Efildr16 $(EFI_BOOT_DISK)\Efildr16 /Y
  -copy $(PROCESSOR)\Bs16.com $(EFI_BOOT_DISK)\ /Y
  -copy $(PROCESSOR)\Gpt.com $(EFI_BOOT_DISK)\ /Y
  -copy $(PROCESSOR)\DumpBs.efi $(EFI_BOOT_DISK)\ /Y
  @echo copy Efildr16 fs0: -q       >  $(EFI_BOOT_DISK)\createhd.nsh
  @echo copy Bs16.com fs0: -q       >> $(EFI_BOOT_DISK)\createhd.nsh
  @echo copy gpt.com fs0: -q        >> $(EFI_BOOT_DISK)\createhd.nsh
  -copy Fv\EfiExtended.fv $(EFI_BOOT_DISK)\ /Y
  @echo copy EfiExtended.fv fs0: -q >> $(EFI_BOOT_DISK)\createhd.nsh
  -@if not exist $(EFI_BOOT_DISK)\efi\boot ( mkdir $(EFI_BOOT_DISK)\efi\boot )
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\$(EFI_SHELL_BIN) $(EFI_BOOT_DISK)\efi\boot\boot$(PROCESSOR).efi /Y
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\Apps\LoadFv.efi $(EFI_BOOT_DISK)\ /Y
  @echo copy efi fs0: -r -q         >> $(EFI_BOOT_DISK)\createhd.nsh
  @echo copy LoadFv.efi fs0: -q     >> $(EFI_BOOT_DISK)\createhd.nsh
  @echo fs0:                        >> $(EFI_BOOT_DISK)\createhd.nsh
  @echo stall 1000000               >> $(EFI_BOOT_DISK)\createhd.nsh
  @echo dumpbs -a blk3 0 Efildr16 Bs16.com gpt.com  >> $(EFI_BOOT_DISK)\createhd.nsh

hd32:
  -copy Fv\Efildr20 $(EFI_BOOT_DISK)\Efildr20 /Y
  -copy $(PROCESSOR)\Bs32.com $(EFI_BOOT_DISK)\ /Y
  -copy $(PROCESSOR)\Gpt.com $(EFI_BOOT_DISK)\ /Y
  -copy $(PROCESSOR)\DumpBs.efi $(EFI_BOOT_DISK)\ /Y
  @echo copy Efildr20 fs0: -q       >  $(EFI_BOOT_DISK)\createhd32.nsh
  @echo copy Bs32.com fs0: -q       >> $(EFI_BOOT_DISK)\createhd32.nsh
  @echo copy gpt.com fs0: -q        >> $(EFI_BOOT_DISK)\createhd32.nsh
  -copy Fv\EfiExtended.fv $(EFI_BOOT_DISK)\ /Y
  @echo copy EfiExtended.fv fs0: -q >> $(EFI_BOOT_DISK)\createhd32.nsh
  -@if not exist $(EFI_BOOT_DISK)\efi\boot ( mkdir $(EFI_BOOT_DISK)\efi\boot )
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\$(EFI_SHELL_BIN) $(EFI_BOOT_DISK)\efi\boot\boot$(PROCESSOR).efi /Y
  -copy $(EDK_SOURCE)\Other\Maintained\Application\UefiShell\bin\$(PROCESSOR)\Apps\LoadFv.efi $(EFI_BOOT_DISK)\ /Y
  @echo copy efi fs0: -r -q         >> $(EFI_BOOT_DISK)\createhd32.nsh
  @echo copy LoadFv.efi fs0: -q     >> $(EFI_BOOT_DISK)\createhd32.nsh
  @echo fs0:                        >> $(EFI_BOOT_DISK)\createhd32.nsh
  @echo stall 1000000               >> $(EFI_BOOT_DISK)\createhd32.nsh
  @echo dumpbs -a blk3 0 Efildr20 Bs32.com gpt.com  >> $(EFI_BOOT_DISK)\createhd32.nsh

#
# Set disk label for format use
#
DISK_LABEL=DUET

createfloppy:
  @echo Format $(EFI_BOOT_DISK) ...
  @echo.> FormatCommandInput.txt
  @echo.n>> FormatCommandInput.txt
  @format /v:$(DISK_LABEL) /q $(EFI_BOOT_DISK) < FormatCommandInput.txt > NUL
  @del FormatCommandInput.txt
  @echo Create boot sector ...
  -Tools\Genbootsector -if=$(EFI_BOOT_DISK) -of=$(PROCESSOR)\FDBs.com
  -Tools\Bootsectimage -g $(PROCESSOR)\FDBs.com $(PROCESSOR)\Bootsect.com -f
  @del $(PROCESSOR)\FDBs.com
  -Tools\Genbootsector -of=$(EFI_BOOT_DISK) -if=$(PROCESSOR)\Bootsect.com
  @echo Done.

createusb:
  @echo Format $(EFI_BOOT_DISK) ...
  @echo.> FormatCommandInput.txt
  @format /FS:FAT /v:$(DISK_LABEL) /q $(EFI_BOOT_DISK) < FormatCommandInput.txt > NUL
  @del FormatCommandInput.txt
  @echo Create boot sector ...
  -Tools\Genbootsector -if=$(EFI_BOOT_DISK) -of=$(PROCESSOR)\UsbBs16.com
  -Tools\Bootsectimage -g $(PROCESSOR)\UsbBs16.com $(PROCESSOR)\Bs16.com -f
  @del $(PROCESSOR)\UsbBs16.com
  -Tools\Genbootsector -of=$(EFI_BOOT_DISK) -if=$(PROCESSOR)\Bs16.com
  -Tools\Genbootsector -mbr -of=$(EFI_BOOT_DISK) -if=$(PROCESSOR)\Mbr.com
  @echo Done.
  @echo PLEASE UNPLUG USB, THEN PLUG IT AGAIN!

createusb32:
  @echo Format $(EFI_BOOT_DISK) ...
  @echo.> FormatCommandInput.txt
  @format /FS:FAT32 /v:$(DISK_LABEL) /q $(EFI_BOOT_DISK) < FormatCommandInput.txt > NUL
  @del FormatCommandInput.txt
  @echo Create boot sector ...
  -Tools\Genbootsector -if=$(EFI_BOOT_DISK) -of=$(PROCESSOR)\UsbBs32.com
  -Tools\Bootsectimage -g $(PROCESSOR)\UsbBs32.com $(PROCESSOR)\Bs32.com -f
  @del $(PROCESSOR)\UsbBs32.com
  -Tools\Genbootsector -of=$(EFI_BOOT_DISK) -if=$(PROCESSOR)\Bs32.com
  -Tools\Genbootsector -mbr -of=$(EFI_BOOT_DISK) -if=$(PROCESSOR)\Mbr.com
  @echo Done.
  @echo PLEASE UNPLUG USB, THEN PLUG IT AGAIN!

