/*++

	TSCSync
	https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell

	Copyright (c) 2017-2023, Kilian Kegel. All rights reserved.
	SPDX-License-Identifier: GNU General Public License v3.0

Module Name:

	base_t.h

Abstract:

	TSCSync - TimeStampCounter (TSC) synchronizer,  analysing System Timer characteristics
	base definitions

Author:

	Kilian Kegel

--*/
#ifndef _PRJTYPES_H_
#define _PRJTYPES_H_

typedef struct _WINDIM {
	//int32_t Row;
	//int32_t Col/*umns*/;
	int32_t X;
	int32_t Y;
}ABSDIM/* absolute */;

//Window Position
typedef struct _WINPOS {
	//int32_t Row;
	//int32_t Col/*umn*/;
	int32_t X;
	int32_t Y;
}ABSPOS/* absolute */, RELPOS/* relative */;

typedef int32_t WINATT;

#define DFLT_SCREEN_ATTRIBS (EFI_BACKGROUND_LIGHTGRAY+EFI_BLACK)

#endif//_PRJTYPES_H_
