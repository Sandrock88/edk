/*++ 

Copyright 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:
  EfiCompressMain.c
  
Abstract:

--*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "TianoCommon.h"
#include "Compress.h"

typedef enum {
  EFI_COMPRESS   = 1,
  TIANO_COMPRESS = 2
} COMPRESS_TYPE;

typedef struct _COMPRESS_ACTION_LIST {
  struct _COMPRESS_ACTION_LIST   *NextAction;
  INT32                          CompressType;
  CHAR8                          *InFileName;
  CHAR8                          *OutFileName;
} COMPRESS_ACTION_LIST;


STATIC
BOOLEAN
ParseCommandLine (
  INT32                 argc,
  CHAR8                 *argv[],
  COMPRESS_ACTION_LIST  **ActionListHead
  )
/*++

Routine Description:

  Parse command line options

Arguments:
  
  argc    - number of arguments passed into the command line.
  argv[]  - files to compress and files to output compressed data to.
  Options - Point to COMMAND_LINE_OPTIONS, receiving command line options.

Returns:
  
  BOOLEAN: TRUE for a successful parse.
--*/
;

STATIC
VOID
Usage (
  CHAR8 *ExeName
  )
/*++

Routine Description:

  Print usage.

Arguments:
  
  ExeName  - Application's full path

--*/
;


STATIC
BOOLEAN
ProcessFile (
  CHAR8         *InFileName,
  CHAR8         *OutFileName,
  COMPRESS_TYPE CompressType
  )
/*++

Routine Description:
  
  Compress InFileName to OutFileName using algorithm specified by CompressType.

Arguments:
  
  InFileName    - Input file to compress
  OutFileName   - Output file compress to
  CompressType  - Compress algorithm, can be EFI_COMPRESS or TIANO_COMPRESS

Returns:
  
  BOOLEAN: TRUE for compress file successfully

--*/
;

int
main (
  INT32 argc,
  CHAR8 *argv[]
  )
/*++

Routine Description:

  Compresses the input files

Arguments:

  argc   - number of arguments passed into the command line.
  argv[] - files to compress and files to output compressed data to.

Returns:

  int: 0 for successful execution of the function.

--*/
{
  COMPRESS_ACTION_LIST *ActionList;
  COMPRESS_ACTION_LIST *NextAction;
  UINT32               ActionCount;
  UINT32               SuccessCount;

  ActionList            = NULL;
  ActionCount          = SuccessCount = 0;

  if (!ParseCommandLine (argc, argv, &ActionList)) {
    Usage (*argv);
    return 1;
  }

  while (ActionList != NULL) {
    ++ActionCount;
    if (ProcessFile (
          ActionList->InFileName, 
          ActionList->OutFileName, 
          ActionList->CompressType)
        ) {
      ++SuccessCount;
    }
    NextAction = ActionList;
    ActionList = ActionList->NextAction;
    free (NextAction);
  }

  fprintf (stdout, "\nCompressed %d files, %d succeed!\n", ActionCount, SuccessCount);
  if (SuccessCount < ActionCount) {
    return 1;
  }

  return 0;
}

STATIC
BOOLEAN
ParseCommandLine (
  INT32                 argc,
  CHAR8                 *argv[],
  COMPRESS_ACTION_LIST  **ActionListHead
  )
{
  COMPRESS_TYPE         CurrentType;

  COMPRESS_ACTION_LIST  **Action;
  
  Action           =    ActionListHead;
  CurrentType      =    TIANO_COMPRESS;     // default compress algorithm is tiano compress

  // Skip Exe Name
  --argc;
  ++argv;

  while (argc > 0) {
    if (strcmp (*argv, "-h") == 0 || strcmp (*argv, "-?") == 0) {
      //
      // 1. Directly return, help message will be printed.
      //
      return FALSE;
    
    } else if (strncmp (*argv, "-t", 2) == 0) {
      //
      // 2. Specifying CompressType
      //
      if (stricmp ((*argv)+2, "EFI") == 0) {
        CurrentType = EFI_COMPRESS;
      } else if (stricmp ((*argv)+2, "Tiano") == 0) {
        CurrentType = TIANO_COMPRESS;
      } else {
        fprintf (stdout, "  ERROR: CompressType %s not supported!\n", (*argv)+2);
        return FALSE;
      }
    } else {
      //
      // 3. Current parameter is *FileName
      //
      if (*Action == NULL) { 
        //
        // need to create a new action item
        //
        *Action = (COMPRESS_ACTION_LIST*) malloc (sizeof **Action);
        if (*Action == NULL) {
          fprintf (stdout, "  ERROR: malloc failed!\n");
          return FALSE;
        }
        memset (*Action, 0, sizeof **Action);
        (*Action)->CompressType = CurrentType;
      }

      //
      // Assignment to InFileName and OutFileName in order
      // 
      if ((*Action)->InFileName == NULL) {
        (*Action)->InFileName  = *argv;
      } else {
        (*Action)->OutFileName = *argv;
        Action                 = &(*Action)->NextAction;
      }
    }

    --argc;
    ++argv;

  }
  
  if (*Action != NULL) {
    assert ((*Action)->InFileName != NULL);
    fprintf (stdout, "  ERROR: Compress OutFileName not specified with InFileName: %s!\n", (*Action)->InFileName);
    return FALSE;
  }

  if (*ActionListHead == NULL) {
    return FALSE;
  }
  return TRUE;
}

STATIC
BOOLEAN
ProcessFile (
  CHAR8         *InFileName,
  CHAR8         *OutFileName,
  COMPRESS_TYPE CompressType
  )
{
  EFI_STATUS          Status;
  FILE                *InFileP;
  FILE                *OutFileP;
  UINT32              SrcSize;
  UINT32              DstSize;
  UINT8               *SrcBuffer;
  UINT8               *DstBuffer;
  COMPRESS_FUNCTION   CompressFunc;

  SrcBuffer     =     DstBuffer = NULL;
  InFileP       =     OutFileP  = NULL;

  fprintf (stdout, "%s --> %s\n", InFileName, OutFileName);

  if ((OutFileP = fopen (OutFileName, "wb")) == NULL) {
    fprintf (stdout, "  ERROR: Can't open output file %s for write!\n", OutFileName);
    goto ErrorHandle;
  }

  if ((InFileP = fopen (InFileName, "rb")) == NULL) {
    fprintf (stdout, "  ERROR: Can't open input file %s for read!\n", InFileName);
    goto ErrorHandle;
  }
  
  //
  // Get the size of source file
  //
  fseek (InFileP, 0, SEEK_END);
  SrcSize = ftell (InFileP);
  rewind (InFileP);
  //
  // Read in the source data
  //
  if ((SrcBuffer = malloc (SrcSize)) == NULL) {
    fprintf (stdout, "  ERROR: Can't allocate memory!\n");
    goto ErrorHandle;
  }

  if (fread (SrcBuffer, 1, SrcSize, InFileP) != SrcSize) {
    fprintf (stdout, "  ERROR: Can't read from source!\n");
    goto ErrorHandle;
  }

  //
  // Choose the right compress algorithm
  //
  CompressFunc = (CompressType == EFI_COMPRESS) ? EfiCompress : TianoCompress;

  //
  // Get destination data size and do the compression
  //
  DstSize = 0;
  Status = CompressFunc (SrcBuffer, SrcSize, DstBuffer, &DstSize);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    fprintf (stdout, "  Error: Compress failed: %x!\n", Status);
    goto ErrorHandle;
  }
  if ((DstBuffer = malloc (DstSize)) == NULL) {
    fprintf (stdout, "  ERROR: Can't allocate memory!\n");
    goto ErrorHandle;
  }

  Status = CompressFunc (SrcBuffer, SrcSize, DstBuffer, &DstSize);
  if (EFI_ERROR (Status)) {
    fprintf (stdout, "  ERROR: Compress Error!\n");
    goto ErrorHandle;
  }

  fprintf (stdout, "  Orig Size = %ld\tComp Size = %ld\n", SrcSize, DstSize);

  if (DstBuffer == NULL) {
    fprintf (stdout, "  ERROR: No destination to write to!\n");
    goto ErrorHandle;
  }

  //
  // Write out the result
  //
  if (fwrite (DstBuffer, 1, DstSize, OutFileP) != DstSize) {
    fprintf (stdout, "  ERROR: Can't write to destination file!\n");
    goto ErrorHandle;
  }

  return TRUE;

ErrorHandle:
  if (SrcBuffer) {
    free (SrcBuffer);
  }

  if (DstBuffer) {
    free (DstBuffer);
  }

  if (InFileP) {
    fclose (InFileP);
  }

  if (OutFileP) {
    fclose (OutFileP);
  }
  return FALSE;
}

VOID
Usage (
  CHAR8 *ExeName
  )
{
  fprintf (
      stdout, 
      "\n"
      "Usage: %s {-tCompressType} [InFileName] [OutFileName]\n"
      "       %*c {-tCompressType} [InFileName] [OutFileName] ...\n"
      "\n"
      "where:\n"
      "  CompressType - optional compress algorithm (EFI | Tiano), case insensitive.\n"
      "                 If ommitted, compress type specified ahead is used, \n"
      "                 default is Tiano\n"
      "                 e.g.: EfiCompress a.in a.out -tEFI b.in b.out \\ \n"
      "                                     c.in c.out -tTiano d.in d.out\n"
      "                 a.in and d.in are compressed using tiano compress algorithm\n"
      "                 b.in and c.in are compressed using efi compress algorithm\n"
      "  InFileName   - input file path\n"
      "  OutFileName  - output file path\n",
      ExeName, strlen(ExeName), ' '
      );
}