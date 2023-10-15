/*++

Copyright (c) 2021-2022, Kilian Kegel. All rights reserved.<BR>

    SPDX-License-Identifier: GNU General Public License v3.0 only

Module Name:

    LibWin324UEFI.h

Abstract:

    Win32 API for UEFI common definitions

Author:

    Kilian Kegel

--*/

#ifndef _WIN324UEFI_H_
#define _WIN324UEFI_H_

#define WIN324UEFI_ID 0x4946455534323357ULL

typedef struct tagW4UFILE
{
    uint64_t    signature;
    uint32_t    dwDesiredAccess;
    uint32_t    dwCreationDisposition;
    void* pFile;

}W4UFILE;

//
// Windows equates
//
#define far
#define near

typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef float               FLOAT;
typedef FLOAT* PFLOAT;
typedef BOOL near* PBOOL;
typedef BOOL far* LPBOOL;
typedef BYTE near* PBYTE;
typedef BYTE far* LPBYTE;
typedef int near* PINT;
typedef int far* LPINT;
typedef WORD near* PWORD;
typedef WORD far* LPWORD;
typedef long far* LPLONG;
typedef DWORD near* PDWORD;
typedef DWORD far* LPDWORD;
typedef void far* LPVOID;
//typedef CONST void far* LPCVOID;
typedef void* PVOID;

typedef int                 INT;
typedef unsigned int        UINT;
typedef unsigned int* PUINT;

#define DECLSPEC_IMPORT __declspec(dllimport)
#define WINBASEAPI DECLSPEC_IMPORT
#define WINAPI      __stdcall

#endif//_WIN324UEFI_H_

