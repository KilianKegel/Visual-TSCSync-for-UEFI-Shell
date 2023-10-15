/*++

	TSCSync
	https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell

	Copyright (c) 2017-2023, Kilian Kegel. All rights reserved.
	SPDX-License-Identifier: GNU General Public License v3.0

Module Name:

	TextWindow.hpp

Abstract:

	TSCSync - TimeStampCounter (TSC) synchronizer,  analysing System Timer characteristics
	Text window processing class members

Author:

	Kilian Kegel

--*/
#ifndef _TEXT_WINDOWS_H_
#define _TEXT_WINDOWS_H_

#include <stdio.h>
#include <uefi.h>
#include "UefiBase.hpp"
#include "TextWindow.hpp"
#include "DPRINTF.H"
#include "base_t.h"

enum TEXT_KEY {
	NO_KEY, KEY_ALT, KEY_ESC, KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_ENTER, KEY_SPACE,
	KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12
};

//
// NOTE:	currently it is only one (1) "TextBlock" per window supported
//			That could be improved/extended if needed in the future.
//			A "TextBlock" in a window can hold multiple lines (separated by newline '\n')  
//			and one (1) start coordinate and one (1) text attribute
//			'\n' sets the cursor to the next line in the block and not to next line on the screen.
//			All "TextBlock" related members contain "Block" in their names
class CTextWindow : public CUefiBase {
public:
	CTextWindow* pParent;
	ABSPOS WinPos;																// Window position	absolute on the screen
	ABSDIM WinDim;																// Window dimension	absolute on the screen
	RELPOS TxtPos;																// Text position	relative in the window
	//ABSDIM TxtDim;															// Text dimension	relative in the window
	RELPOS BlockPos;															// Block position	relative in the window
	WINATT WinAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK;						// Window attribute
	WINATT BgAtt = EFI_BACKGROUND_BLACK + EFI_WHITE;							// Background attribute
	WINATT BlockAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK;						// TextBlock attribute
	wchar_t* pwcsWinClrLine = nullptr;											// clear line wide string
	wchar_t* pwcsWinHorizBorder = nullptr;
	wchar_t* pwcsBlockDrawBuf = nullptr;										// text buffer size of X*Y
	wchar_t* pwcsBlockScrtchBuf = nullptr;										// clear buffer size of X*Y
	EFI_KEY_DATA KeyData;

	bool fFullScreen = false;
	int  nProgress = 0;									// counter for progress indicator

	CTextWindow(CTextWindow* pParent);
	CTextWindow(CTextWindow* pParent, int32_t ScreenAttrib);
	CTextWindow(CTextWindow* pParent, ABSPOS WinPos, ABSDIM WinDim, int32_t WindowAttrib);
	~CTextWindow();

	CTextWindow* TextWindowGetRoot(void);				// return instance without NULL == pParent
	void TextWindowUpdateProgress(void);				// update the progress indicator at Root Window

	int TextBorder(RELPOS TxtPos, ABSDIM WinDim, wchar_t upleft, wchar_t upright, wchar_t lowleft, wchar_t lowright, wchar_t horiz, wchar_t verti, wchar_t* pwcsTitle);

	int TextPrint(const char* strFmt, ...);											// narrow string
	int TextPrint(const wchar_t* wcsFmt, ...);										// wide string
	int TextPrint(RELPOS TxtPos, const char* strFmt, ...);							// narrow string
	int TextPrint(RELPOS TxtPos, const wchar_t* wcsFmt, ...);						// wide string
	int TextPrint(RELPOS TxtPos, WINATT TxtAtt, const char* strFmt, ...);			// narrow string
	int TextPrint(RELPOS TxtPos, WINATT TxtAtt, const wchar_t* wcsFmt, ...);		// wide string

	int TextVPrint(const char* strFmt, va_list ap);
	int TextVPrint(const wchar_t* wcsFmt, va_list ap);

	void TextBlockDraw(RELPOS BlockPos);											// place at XY a textblock, one or more '\n' separated lines
	void TextBlockDraw(RELPOS BlockPos, WINATT TxtAtt);								// place at XY a textblock, one or more '\n' separated lines
	void TextBlockDraw(RELPOS BlockPos, WINATT TxtAtt, const char* strFmt, ...);	// place at XY a textblock, one or more '\n' separated lines
	void TextBlockDraw(RELPOS BlockPos, WINATT TxtAtt, const wchar_t* strFmt, ...);	// place at XY a textblock, one or more '\n' separated lines
	void TextBlockDraw(RELPOS BlockPos, const char* strFmt, ...);
	void TextBlockDraw(RELPOS BlockPos, const wchar_t* wcsFmt, ...);
	void TextBlockRfrsh(void);														// refresh last textblock
	void TextBlockClear(void);														// clear   last textblock

	//void TextClearWindow(void);
	void TextClearWindow(int BgAtt);

	TEXT_KEY TextGetKey(void);
};

#endif//_TEXT_WINDOWS_H_
