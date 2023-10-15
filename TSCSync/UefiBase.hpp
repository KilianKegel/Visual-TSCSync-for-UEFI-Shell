/*++

	TSCSync
	https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell

	Copyright (c) 2017-2023, Kilian Kegel. All rights reserved.
	SPDX-License-Identifier: GNU General Public License v3.0

Module Name:

	UefiBase.hpp

Abstract:

	TSCSync - TimeStampCounter (TSC) synchronizer,  analysing System Timer characteristics
	UEFI base class members

Author:

	Kilian Kegel

--*/

#ifndef _UEFI_BASE_HPP_
#define _UEFI_BASE_HPP_

#include <uefi.h>
#include <stdint.h>
#include "DPRINTF.H"
#include "base_t.h"

class CUefiBase {
public:
	int32_t VideoModeCurrent;
	int32_t VideoModeMax;
	ABSDIM ScrDim;

	ABSDIM QueryMode(IN int32_t ModeNumber);

	EFI_STATUS ClrScr(void);
	EFI_STATUS ClrScr(int32_t attr/*console color*/);
	
	EFI_STATUS GotoXY(int32_t x, int32_t y);				// place cursor
	EFI_STATUS GotoXY(ABSPOS WinPos);					// place cursor

	EFI_GUID   SimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
	EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* pSimpleTextInputExProtocol;

	CUefiBase();
	EFI_KEY_DATA ReadKeyStrokeEx(void);
};

#define ELC(x) /*ELementCount*/(sizeof(x)/sizeof(x[0]))

#endif//_UEFI_BASE_HPP_
