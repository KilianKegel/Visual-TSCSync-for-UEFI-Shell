/*++

	TSCSync
	https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell

	Copyright (c) 2017-2023, Kilian Kegel. All rights reserved.
	SPDX-License-Identifier: GNU General Public License v3.0

Module Name:

	main.cpp

Abstract:

	TSCSync - TimeStampCounter (TSC) synchronizer,  analyzing System Timer characteristics
	main program.

Author:

	Kilian Kegel

--*/
//
// prefixes used:
//	wcs - wide character string
//	str - narrow character string
//	fn	- function
//  g	- global
//  rg  - range, array
//  idx	- index
//	Mnu	- menu
//  Itm	- item
//  Mng	- managed
//	Cfg	- config

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <intrin.h>
#undef NULL
#include <uefi.h>
#include "UefiBase.hpp"
#include "TextWindow.hpp"
#include "DPRINTF.H"
#include "base_t.h"
#include "LibWin324UEFI.h"

#include <Protocol\AcpiTable.h>
#include <Protocol\Timestamp.h>
#include <Guid\Acpi.h>
#include <IndustryStandard/Acpi62.h>
#include <IndustryStandard/MemoryMappedConfigurationSpaceAccessTable.h>

#include <Protocol\AcpiSystemDescriptionTable.h>

#include "xlsxwriter.h"

extern "C" EFI_SYSTEM_TABLE * _cdegST;
extern "C" EFI_SYSTEM_TABLE * gSystemTable;
extern "C" EFI_HANDLE gImageHandle;

extern "C" int _outp(unsigned short port, int data_byte);
extern "C" unsigned short _outpw(unsigned short port, unsigned short data_word);
extern "C" unsigned long _outpd(unsigned short port, unsigned long data_word);
extern "C" int _inp(unsigned short port);
extern "C" unsigned short _inpw(unsigned short port);
extern "C" unsigned long _inpd(unsigned short port);
extern "C" void _disable(void);

extern bool gfKbdDbg;
extern "C" uint16_t gPmTmrBlkAddr;
extern "C" int64_t PITClkWait(uint32_t delay);
extern "C" int64_t AcpiClkWait(uint32_t delay);
extern "C" int64_t InternalAcpiDelay(uint32_t delay);
extern "C" int64_t PITClkWait(uint32_t delay);
extern "C" unsigned long long _osifIbmAtGetTscPer62799(uint32_t delay);

extern "C" WINBASEAPI UINT WINAPI EnumSystemFirmwareTables(
	/*_In_*/ DWORD FirmwareTableProviderSignature,
	/*_Out_writes_bytes_to_opt_(BufferSize, return)*/ PVOID pFirmwareTableEnumBuffer,
	/*_In_*/ DWORD BufferSize
);

extern "C" WINBASEAPI UINT WINAPI GetSystemFirmwareTable(
	/*_In_*/ DWORD FirmwareTableProviderSignature,
	/*_In_*/ DWORD FirmwareTableID,
	/*_Out_writes_bytes_to_opt_(BufferSize, return)*/ PVOID pFirmwareTableBuffer,
	/*_In_*/ DWORD BufferSize
);
extern "C" WINBASEAPI void Sleep(uint32_t dwMilliseconds);

extern "C" WINBASEAPI uint64_t WINAPI GetTickCount64(VOID);

extern void (*rgcbAtUpdate[32])(void* pThis, void* pBox, void* pContext, void* pParm);
extern void* rgcbAtUpdateParms[32][3];	// array of parameters: void* pThis, void* pBox, void* pContext
extern "C" char* _strefierror(EFI_STATUS);

#define TYPE char
#define TYPESIZE sizeof(TYPE)
#define TYPEMASK ((1ULL << TYPESIZE * 8)-1)

#define FORMATW_ADDR L"%02X: %02X%s"
#define FORMATWOADDR L"%s%02X%s"

#define RTCRD(idx) (_outp(0xED,0x55),_outp(0xED,0x55),_outp(0x70,idx),_outp(0xED,0x55),_outp(0xED,0x55),_inp(0x71))

#define IODELAY _outp(0xED, 0x55)

int64_t gTSCPerSecondReference;	// initally taken from RTC UP flag 0x10 Reg 0xC

static const char buildtimestamp[] =
{
    // YY year
    __DATE__[9], __DATE__[10],

    // First month letter, Oct Nov Dec = '1' otherwise '0'
    (__DATE__[0] == 'O' || __DATE__[0] == 'N' || __DATE__[0] == 'D') ? '1' : '0',

    // Second month letter
    (__DATE__[0] == 'J') ? ((__DATE__[1] == 'a') ? '1' :       // Jan, Jun or Jul
                             ((__DATE__[2] == 'n') ? '6' : '7')) :
    (__DATE__[0] == 'F') ? '2' :                                // Feb 
    (__DATE__[0] == 'M') ? (__DATE__[2] == 'r') ? '3' : '5' :   // Mar or May
    (__DATE__[0] == 'A') ? (__DATE__[1] == 'p') ? '4' : '8' :   // Apr or Aug
    (__DATE__[0] == 'S') ? '9' :                                // Sep
    (__DATE__[0] == 'O') ? '0' :                                // Oct
    (__DATE__[0] == 'N') ? '1' :                                // Nov
    (__DATE__[0] == 'D') ? '2' :                                // Dec
    0,

    // First day letter, replace space with digit
    __DATE__[4] == ' ' ? '0' : __DATE__[4],

    // Second day letter
    __DATE__[5],
    __TIME__[0],
    __TIME__[1],
    __TIME__[0],
    __TIME__[3],
    __TIME__[4],

   '\0'
};
char gstrBuildNum[16];

/////////////////////////////////////////////////////////////////////////////
// CONFIG menu functions and strings
/////////////////////////////////////////////////////////////////////////////
//
// gfCfgMngXyz - global flag configuration managed XYZ
//
bool gfCfgMngMnuItm_Config_PicApicSelect = true;	// "TIMER: PIT" vs. "TIMER: ACPI" APIT selected by default
bool gfCfgMngMnuItm_Config_PITDelaySelect1 = true;	// L"  PIT :    1 * 1193181 clocks = 173*19*11*11*3 * 1 " vs. L"* PIT :    1 * 1193181 clocks = 173*19*11*11*3 * 1 "	selected by default
bool gfCfgMngMnuItm_Config_PITDelaySelect2 = false;	// L"  PIT :   19 *   62799 clocks = 173*   11*11*3 * 1 " vs. L"* PIT :   19 *   62799 clocks = 173*   11*11*3 * 1 "
bool gfCfgMngMnuItm_Config_PITDelaySelect3 = false;	// L"  PIT :  363 *    3287 clocks = 173*19         * 1 " vs. L"* PIT :  363 *    3287 clocks = 173*19         * 1 "
bool gfCfgMngMnuItm_Config_PITDelaySelect4 = false;	// L"  PIT : 6897 *     173 clocks = 173            * 1 " vs. L"* PIT : 6897 *     173 clocks = 173            * 1 "
bool gfCfgMngMnuItm_Config_PITDelaySelect5 = false;	// L"  PIT : 9861 *     121 clocks =        11*11   * 1 " vs. L"* PIT : 9861 *     121 clocks =        11*11   * 1 "

bool gfCfgMngMnuItm_Config_ACPIDelaySelect1 = true;	// L"  ACPI:    1 * 3579543 clocks = 173*19*11*11*3 * 3 " vs. L"* ACPI:    1 * 3579543 clocks = 173*19*11*11*3 * 3 "	selected by default
bool gfCfgMngMnuItm_Config_ACPIDelaySelect2 = false;// L"  ACPI:   19 *  188397 clocks = 173*   11*11*3 * 3 " vs. L"* ACPI:   19 *  188397 clocks = 173*   11*11*3 * 3 "
bool gfCfgMngMnuItm_Config_ACPIDelaySelect3 = false;// L"  ACPI:  363 *    9861 clocks = 173*19         * 3 " vs. L"* ACPI:  363 *    9861 clocks = 173*19         * 3 "
bool gfCfgMngMnuItm_Config_ACPIDelaySelect4 = false;// L"  ACPI: 6897 *     519 clocks = 173            * 3 " vs. L"* ACPI: 6897 *     519 clocks = 173            * 3 "
bool gfCfgMngMnuItm_Config_ACPIDelaySelect5 = false;// L"  ACPI: 9861 *     363 clocks =        11*11   * 3 " vs. L"* ACPI: 9861 *     363 clocks =        11*11   * 3 "

bool gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = true;
bool gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = false;
bool gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = false;

extern "C" int  gfErrorCorrection;

/////////////////////////////////////////////////////////////////////////////
// global shared data
/////////////////////////////////////////////////////////////////////////////
char gstrACPISignature[128];
char gstrACPIOemId[128];
char gstrACPIOemTableId[128];
char gstrACPIPmTmrBlkAddr[128];
char gACPIPmTmrBlkSize[128];
char gACPIPCIEBase[128];

char gstrCPUID0[128];
char gstrCPUIDSig[128];
char gstrCPUIDFam[128];
char gstrCPUIDMod[128];
char gstrCPUIDStp[128];
char gstrCPUSpeed[128];
int64_t gTIMESTAMP_PROTOCOLPerSec;
int64_t gTIMESTAMP_PROTOCOLSecDriftPerDay;
char gstrTIMESTAMP_PROTOCOL[128];
int64_t gTIMESTAMP_PROTOCOLDriftPerDay;
extern "C" uint32_t gCOUNTER_WIDTH;

int64_t(*pfnDelay)(uint32_t  Delay) = &InternalAcpiDelay;

#define MAXNUM 1250
static uint16_t PITB2BStat[MAXNUM];
static int32_t ACPIB2BStat[MAXNUM];

static int64_t statbuf4DiffTSC[MAXNUM * 10];			// one buffer for all
static double statbuf4DriftSecPerDay[MAXNUM * 10];	// one buffer for all
static int cntSamples = 0;
static struct {
	char szCalibrTime[64];
	char szTicks[64];
	char szMultiplier[64];
	uint32_t delay;
	int64_t qwMultiplierToOneSecond;
	//int64_t(*pfnDelay)(uint32_t  Delay);
	bool* pEna;
	int64_t* rgDiffTSC;	    // equivalence of arrays and pointers
	double* rgDriftSecPerDay;	// equivalence of arrays and pointers

}parms[] = {
	// ACPI
	{
		"1s","3579543","1",
		3 * 1193181,1, &gfCfgMngMnuItm_Config_ACPIDelaySelect1, &statbuf4DiffTSC[0 * MAXNUM], &statbuf4DriftSecPerDay[0 * MAXNUM]},
	{	
		"52.632ms","188397","19",
		3 * 62799,19 , &gfCfgMngMnuItm_Config_ACPIDelaySelect2, &statbuf4DiffTSC[1 * MAXNUM], &statbuf4DriftSecPerDay[1 * MAXNUM]},
	{
		" 2.755ms","29583","363",
		3 * 3287,363 , &gfCfgMngMnuItm_Config_ACPIDelaySelect3, &statbuf4DiffTSC[2 * MAXNUM], &statbuf4DriftSecPerDay[2 * MAXNUM]},
	{
		" 1.000ms","3579","1000",
		3579,1000 , &gfCfgMngMnuItm_Config_ACPIDelaySelect4, &statbuf4DiffTSC[3 * MAXNUM], &statbuf4DriftSecPerDay[3 * MAXNUM]},
	{	"101.41us","363","9861",
		3 * 121,9861 , &gfCfgMngMnuItm_Config_ACPIDelaySelect5, &statbuf4DiffTSC[4 * MAXNUM], &statbuf4DriftSecPerDay[4 * MAXNUM]},
};

char gStatusStringColor = EFI_GREEN;
#define STATUSSTRING "STATUS: 1234567890abcdefABCDEF"
wchar_t wcsStatusBar[sizeof(STATUSSTRING)] = { L"" };
bool fStatusBarRightAdjusted = false;
uint32_t blink;														// counter is DECERMENTED until it reaches 0, and then stops decrementing, and the STATUS gets cleared

typedef struct {
	RELPOS RelPos;
	const wchar_t* wcsMenuName;
	CTextWindow* pTextWindow;
	ABSDIM MnuDim;
	const wchar_t* rgwcsMenuItem[32];								// max. number of menu items is currently 32
	int (*rgfnMnuItm[32])(CTextWindow*, void* pContext, void* pParm);	// max. number of menu items is currently 32
}menu_t;

//void invalid_parameter_handler(
//	const wchar_t* expression,
//	const wchar_t* function,
//	const wchar_t* file,
//	unsigned int line,
//	uintptr_t pReserved
//) {
//
//	if (NULL != file)
//		fprintf(stderr, "invalid parameter: %S(%d) %S() %S \n", file, line, function, expression);
//	else
//	{
//		//CDETRACE((TRCERR(1) "abnormal program termination due to invalid parameter\n"));
//		//exit(3);
//		fprintf(stderr, "error");
//		__debugbreak();
//	}
//	return;
//}

int rtcrd(int idx)
{
	int nRet = 0;
	int UIP = 0;

	do {

		//
		// read UIP- update in progress first
		//
		_outp(0x70, 0xA);
		IODELAY;

		UIP = 0x80 == (0x80 & _inp(0x71));
		IODELAY;

		_outp(0x70, idx);
		IODELAY;

		nRet = _inp(0x71); IODELAY;

	} while (1 == UIP);

	return nRet;
}
//
// globally shared data
//
bool gfExit = false;
bool gfSaveExit = false;
bool gfHexView = false;
bool gfRunConfig = false;
bool gfAutoRun = false;

bool gfStatusLineVisible;
#pragma pack(1)
typedef struct {
	EFI_ACPI_DESCRIPTION_HEADER                       Header;
	UINT64                                            Reserved0;
	UINT64  BaseAddress;
	UINT16  PciSegmentGroupNumber;
	UINT8   StartBusNumber;
	UINT8   EndBusNumber;
	UINT32  Reserved;
}CDE_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE;
CDE_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE* pMCFG = (CDE_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE*)malloc(32768 * 8);
#pragma pack()
//
// gfCfgMngXyz - global flag configuration managed XYZ
//
bool gfCfgMngMnuItm_View_Clock = true;
bool gfCfgMngMnuItm_View_Calendar = true;
char gCfgStr_File_SaveAs[256] = "default.xlsx";
char gCfgStr_CalibrMethod[64] = "original TIANOCORE";
int gfCfgSyncRef012 = 0;		//0 -> ACPI, 1 -> RTC, 2 -> PIT
int  gnCfgRefSyncTime = 3;		//sync time/delay

/////////////////////////////////////////////////////////////////////////////
// FILE menu functions and strings
/////////////////////////////////////////////////////////////////////////////
int fnMnuItm_File_Exit(CTextWindow* pThis, void* pContext, void* pParm)
{
	int nRet = 0;

	if (0 == strcmp("ENTER", (char*)pParm))				// Enter?
	{
		pThis->TextPrint({ 20,20 }, "fnMnuItm_File_Exit...");
		gfExit = true;
	}
	return 0;
}

int fnMnuItm_File_SaveExit(CTextWindow* pThis, void* pContext, void* pParm)
{
	if (0 == strcmp("ENTER", (char*)pParm))				// Enter?
	{
		gfExit = true;
		gfSaveExit = true;
	}
	return 0;
}

int fnMnuItm_File_SaveAs(CTextWindow* pThis, void* pContext, void* pParm)
{
	FILE* fp;
	bool fFileExists;	// flag file exists
	bool fCreateOvrd= (0 == strcmp("AUTORUN", (char*)pParm));	// flag CreateOverride, always overwrite in AUTORUN mode
	CTextWindow* pSaveAsBox;
	CTextWindow* pRoot = pThis->TextWindowGetRoot();			// get FullScreen window

	if (0 == strcmp("ENTER", (char*)pParm))						// ENTER?
	{
		pThis->TextClearWindow(pRoot->WinAtt);					// clear the pull down menu 
		pRoot->TextBlockRfrsh();								// refresh the main window

		pSaveAsBox = new CTextWindow(pThis, { pRoot->WinDim.X / 2 - 60 / 2,pRoot->WinDim.Y / 2 - 5 / 2 }, { 60,5 }, EFI_BACKGROUND_CYAN | EFI_YELLOW);

		pSaveAsBox->TextBorder(
		{ 0,0 },
		{ 60,5 },
		BOXDRAW_DOWN_RIGHT,
		BOXDRAW_DOWN_LEFT,
		BOXDRAW_UP_RIGHT,
		BOXDRAW_UP_LEFT,
		BOXDRAW_HORIZONTAL,
		BOXDRAW_VERTICAL,
		nullptr
	);

	pSaveAsBox->TextPrint({ 2,0 }, " File Save As ... ");
	pSaveAsBox->TextPrint({ 2,2 }, EFI_BACKGROUND_BLUE | EFI_WHITE, "                                                        ");

		//
		// simulate the "SAVE-AS dialog box"
		//
		if (1)
		{
			TEXT_KEY key;

			if (1) 
			{
				int i = strlen(gCfgStr_File_SaveAs);												// initial cursor position
				for (key = pThis->TextGetKey();
					KEY_ESC != key && KEY_ENTER != key;
					key = pThis->TextGetKey(), pThis->TextWindowUpdateProgress())
				{
					pSaveAsBox->TextPrint({ 2    ,2 }, EFI_BACKGROUND_BLUE | EFI_WHITE, gCfgStr_File_SaveAs);
					pSaveAsBox->TextPrint({ 2 + i,2 }, 4 & blink++ ? EFI_BACKGROUND_RED | EFI_WHITE : EFI_BACKGROUND_BLUE | EFI_WHITE, " ");//blink the cursor

					if (0xFFFF != pThis->KeyData.Key.UnicodeChar
						&& 0x0000 != pThis->KeyData.Key.UnicodeChar)
					{
						if ('\b'/* back space */ == (char)pThis->KeyData.Key.UnicodeChar)
							pSaveAsBox->TextPrint({ 2 + i,2 }, EFI_BACKGROUND_BLUE | EFI_WHITE, " "),	// clear old cursor
							i--;																		// adjust new curor position
						else
							gCfgStr_File_SaveAs[i++] = (char)pThis->KeyData.Key.UnicodeChar;

						gCfgStr_File_SaveAs[i] = '\0';
					}
				}
			}
			if (KEY_ESC == key)
				wcscpy(wcsStatusBar, L"STATUS: cancled"),
				gStatusStringColor = EFI_GREEN,
				blink = 0x3C;

			if (KEY_ENTER == key) do
			{
				errno = 0;
				fp = fopen(gCfgStr_File_SaveAs, "r");
				fFileExists = (nullptr != fp);	// flag file exists
				fCreateOvrd = !fFileExists;	// flag CreateOverride

				strcpy(gCfgStr_File_SaveAs, gCfgStr_File_SaveAs);
				//
				// FILE OVERRIDE Input BOX - start
				//
				if (true == fFileExists)
				{
					bool fYSel = false;												// YES/NO selected := NO

					pSaveAsBox->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK;		// delete SAVE AS BOX
					delete pSaveAsBox;												// delete SAVE AS BOX
					pSaveAsBox = nullptr;											// prevent from second deletion below
#define DBHIGHT 5												// InputBox height
					char strtmp[256];

					pRoot->TextBlockRfrsh();										// refresh the background

					snprintf(strtmp, sizeof(strtmp), "\"%s\" overwrite?", gCfgStr_File_SaveAs);
					int DBWIDTH/* InputBox width */ = 16 < strlen(strtmp) ? 4 + (int)strlen(strtmp) : 20;

					CTextWindow* pDialogBox = new CTextWindow(						// create Y/N Dialog BOX
						pThis,
						{ pRoot->WinDim.X / 2 - DBWIDTH / 2,pRoot->WinDim.Y / 2 - DBHIGHT / 2 },
						{ DBWIDTH,DBHIGHT },
						EFI_BACKGROUND_CYAN | EFI_YELLOW
					);
					// 00000000001111111111
					// 01234567890123456789
					// +------------------+
					// |    YES     NO    |
					// +------------------+

					pDialogBox->TextBorder(
						{ 0,0 },
						{ DBWIDTH,DBHIGHT },
						BOXDRAW_DOWN_RIGHT,
						BOXDRAW_DOWN_LEFT,
						BOXDRAW_UP_RIGHT,
						BOXDRAW_UP_LEFT,
						BOXDRAW_HORIZONTAL,
						BOXDRAW_VERTICAL,
						nullptr
					);

					pDialogBox->TextPrint({ DBWIDTH / 2 - (int)strlen(strtmp) / 2,1 }, strtmp);

					for (key = pThis->TextGetKey();
						KEY_ENTER != key && KEY_ESC != key;
						key = pThis->TextGetKey(), pThis->TextWindowUpdateProgress())
					{
						if (KEY_LEFT == key || KEY_RIGHT == key)
							fYSel ^= true;											// YES/NO selection toggle

						pDialogBox->TextPrint({ DBWIDTH / 2 - 7,DBHIGHT - 2 }, (fYSel ? EFI_BACKGROUND_GREEN : EFI_BACKGROUND_CYAN) | EFI_WHITE, "  YES  ");
						pDialogBox->TextPrint({ DBWIDTH / 2 + 1,DBHIGHT - 2 }, (fYSel ? EFI_BACKGROUND_CYAN : EFI_BACKGROUND_GREEN) | EFI_WHITE, "  NO  ");
					}
					pDialogBox->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK;		// delete Y/N CHECK BOX
					delete pDialogBox;												// delete Y/N CHECK BOX

					//
					// FILE OVERRIDE dialog BOX - end
					//
					if (fYSel)
						fCreateOvrd = true;
				else {
					wcscpy(wcsStatusBar, L"STATUS: ");
					swprintf(
						&wcsStatusBar[wcslen(wcsStatusBar)],
						sizeof(STATUSSTRING) - 1,
						L"%hs",
							strerror(EACCES)
						);
						gStatusStringColor = EFI_RED;
						blink = UINT_MAX & ~3;
					}
				}
			} while (0);//if (KEY_ENTER == key) do
		}
	}

	if (fCreateOvrd)
	{
		errno = 0;
		FILE* fp = fopen(gCfgStr_File_SaveAs, "w+");
		fFileExists = (nullptr != fp);
		swprintf(wcsStatusBar, sizeof(STATUSSTRING) - 1, L"%hs",
			fFileExists ? "STATUS: Success" : strerror(errno));
		gStatusStringColor = fFileExists ? EFI_YELLOW : EFI_RED;
		blink = fFileExists ? 0x3C : UINT_MAX & ~3;
		blink = UINT_MAX & ~3;//kgtest	dont stop STATUS 

		// delete strFileName
		if (fFileExists)
			fclose(fp),
			remove(gCfgStr_File_SaveAs);
        //
        // ATTENTION: Progress indicator stopped during I/O
        //
        if (1)
        {
            char* pLineKill = new char[pRoot->ScrDim.X];

            memset(pLineKill, '\x20', pRoot->ScrDim.X),
                pLineKill[pRoot->ScrDim.X - 1] = '\0';

            pRoot->TextPrint({ 0, pRoot->ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, pLineKill);
            pRoot->TextPrint({ 1, pRoot->ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, "ATTENTION: Progress indicator stopped during data processing");

        }

        //
		// create the .XLSX file
		//
		if (1)
		{
#define COL_TBL_START (/* start column --> */'D' - 'A')
			lxw_workbook* workbook = workbook_new(gCfgStr_File_SaveAs);
			lxw_worksheet* worksheet = workbook_add_worksheet(workbook, nullptr);
			lxw_format* bold = workbook_add_format(workbook);
			lxw_chart* chart, *chart2;
			lxw_chart_series* series, *series2;
			lxw_chartsheet* chartsheet1;

			format_set_bold(bold);
			format_set_text_wrap(bold);
			worksheet_set_row(worksheet, 0, 80, bold);
			format_set_text_wrap(bold);
			worksheet_set_column(worksheet, COLS("B:B"), 100, nullptr);

			//
			// write column title
			//
			for (int i = 0, col = 3/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values'  */; i < ELC(parms); i++)
			{
				char szTmp[128];
				if (false == *parms[i].pEna)
					continue;
				sprintf(szTmp, "Calibration Time: %s", parms[i].szCalibrTime);
				worksheet_write_string(worksheet, 0, (lxw_col_t)(COL_TBL_START + col), szTmp, nullptr);
				col++;
			}
            worksheet_write_string(worksheet, 0, (lxw_col_t)(COL_TBL_START + 1), "ACPI count read back to back diff", nullptr);
            worksheet_write_string(worksheet, 0, (lxw_col_t)(COL_TBL_START + 2), "PIT  count read back to back diff", nullptr);

			//
			// write system information 
			//
			if (1)
			{
				char strtmp[4* 64];
				sprintf(strtmp, "TSCSync generated Excel Table\n\nAnalyzing platform timer characteristics\n\n"), worksheet_write_string(worksheet, CELL("B1"), strtmp, bold);
				sprintf(strtmp, "ACPI OemId: %s", gstrACPIOemId), worksheet_write_string(worksheet, CELL("B2"), strtmp, bold);
				sprintf(strtmp, "ACPI OemTableId: %s", gstrACPIOemTableId), worksheet_write_string(worksheet, CELL("B3"), strtmp, bold);
				sprintf(strtmp, "ACPI Timer Address: %s", gstrACPIPmTmrBlkAddr), worksheet_write_string(worksheet, CELL("B4"), strtmp, bold);
				sprintf(strtmp, "ACPI Timer Size: %s", gACPIPmTmrBlkSize), worksheet_write_string(worksheet, CELL("B5"), strtmp, bold);
				sprintf(strtmp, "ACPI PCIEBase: %s", gACPIPCIEBase), worksheet_write_string(worksheet, CELL("B6"), strtmp, bold);
				sprintf(strtmp, "Vendor CPUID: %s", gstrCPUID0), worksheet_write_string(worksheet, CELL("B7"), strtmp, bold);
				sprintf(strtmp, "HostBridge VID:DID: %02X:%02X",((uint16_t*)pMCFG->BaseAddress)[0],((uint16_t*)pMCFG->BaseAddress)[1]), worksheet_write_string(worksheet, CELL("B8"), strtmp, bold);
				sprintf(strtmp, "CPUID Signature: %s", gstrCPUIDSig), worksheet_write_string(worksheet, CELL("B9"), strtmp, bold);
				sprintf(strtmp, "CPU Speed(measured): %s", gstrCPUSpeed), worksheet_write_string(worksheet, CELL("B10"), strtmp, bold);
				sprintf(strtmp, "CPU Speed(EFI_TIMESTAMP_PROTOCOL): %s", gstrTIMESTAMP_PROTOCOL), worksheet_write_string(worksheet, CELL("B11"), strtmp, bold);
				if (0 != strncmp(gstrTIMESTAMP_PROTOCOL, "N/A", strlen("N/A")))
					sprintf(strtmp, "    EFI_TIMESTAMP_PROTOCOL drift : %llds per day", gTIMESTAMP_PROTOCOLSecDriftPerDay), worksheet_write_string(worksheet, CELL("B12"), strtmp, bold);
				sprintf(strtmp, "target .XLSX: %s", gCfgStr_File_SaveAs), worksheet_write_string(worksheet, CELL("B13"), strtmp, bold);
                sprintf(strtmp, "RefTimerDev: %s", 2 == gfCfgSyncRef012 ? "PIT" : (1 == gfCfgSyncRef012 ? "RTC" : "ACPI")), worksheet_write_string(worksheet, CELL("B14"), strtmp, bold);//0 -> ACPI, 1 -> RTC, 2 -> PIT
				sprintf(strtmp, "RefSyncTime: %ds", gnCfgRefSyncTime), worksheet_write_string(worksheet, CELL("B15"), strtmp, bold);
				sprintf(strtmp, "Calibration Method: %s", gCfgStr_CalibrMethod), worksheet_write_string(worksheet, CELL("B16"), strtmp, bold);
                sprintf(strtmp, "Error correction: %s", 0 == gfErrorCorrection ? "disabled" : (pfnDelay == &InternalAcpiDelay ? "N/A on TIANOCORE" : "enabled")), worksheet_write_string(worksheet, CELL("B17"), strtmp, bold);

			}

			//	
			// write line numbers, NOTE: for scatter charts x-y coordinates needed , scatter charts must have 'categories' and 'values'  
			// 
			for (int j = 0; j < cntSamples; j++)
			{
				//if(false == gfAutoRun)
				//	pThis->TextWindowUpdateProgress();
				// write numbers		
				worksheet_write_number(worksheet, j + 1, COL_TBL_START, j, nullptr);
			}
            //
            // write ACPI back2back reads to coloumns 1 
            //
            for (int i = 0,j = MAXNUM - cntSamples + 1/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values'  */; i < (cntSamples-1); i++, j++)
            {
                //NOTE: ACPI timer counts up
                //NOTE: PIT  timer counts down

				int32_t PITDiff = PITB2BStat[j - 1] - PITB2BStat[j];		// get PIT counter value diff
				int32_t ACPIDiff = ACPIB2BStat[j] - ACPIB2BStat[j - 1];		// get ACPI counter value diff

				if (0 > ACPIDiff)//check ACPI counter overflow
					ACPIDiff = (1 << 24) - ACPIB2BStat[j - 1] + ACPIB2BStat[j];
				worksheet_write_number(worksheet, i + 1, (lxw_col_t)(COL_TBL_START + 1/*0 line numbers, 1 ACPI, 2 PIT*/), ACPIDiff, nullptr);

				if (0 > PITDiff)//check PIT counter overflow
					PITDiff = (1 << 16) - PITB2BStat[j] + PITB2BStat[j - 1];
				worksheet_write_number(worksheet, i + 1, (lxw_col_t)(COL_TBL_START + 2/*0 line numbers, 1 ACPI, 2 PIT*/), PITDiff ,nullptr);
			}

			for (int i = 0, col = 3/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values'  */; i < ELC(parms); i++)
			{
				if (false == *parms[i].pEna)
					continue;

				for (int j = 0; j < cntSamples; j++)
				{
					//if (false == gfAutoRun)
					//	pThis->TextWindowUpdateProgress();
					/* Write some data for the chart. */
					worksheet_write_number(worksheet, j + 1, (lxw_col_t)(COL_TBL_START + col), parms[i].rgDriftSecPerDay[j], nullptr);
				}
				col++;
			}

			//
			// preview diagram on first page
			//
			if (1)
			{

				/* Create a chart object. */
				chart = workbook_add_chart(workbook, LXW_CHART_SCATTER);

				//
				// Configure the chart for all measurements
				//
				for (int i = 0, col = 3/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values' */;
					i < ELC(parms);
					i++)
				{
					char strCategory[64], strValue[64], strLabel[64];

					if (false == *parms[i].pEna)
						continue;

					sprintf(strCategory, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START, 'A' + COL_TBL_START, 1 + cntSamples);
					sprintf(strValue, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START + col, 'A' + COL_TBL_START + col, 1 + cntSamples);

					series = chart_add_series(chart, strCategory, strValue);
					sprintf(strLabel, "%s", parms[i].szCalibrTime);
					chart_series_set_name(series, strLabel);

					col++;

				}
				chart_title_set_name(chart, "Overall preview, drift in seconds per day. Parameter:\nCalibration time");
				worksheet_insert_chart(worksheet, CELL("B20"), chart);
			}

            //
            // ACPI + PIT counter back to back diff preview diagram on first page
            //
            if (1)
            {

                /* Create a chart object. */
                chart = workbook_add_chart(workbook, LXW_CHART_SCATTER);
                chart2= workbook_add_chart(workbook, LXW_CHART_SCATTER);

                //
                // Configure the chart for all measurements
                //
                for (int i = 0, col = 1/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values' */;
                    i < 2;
                    i++)
                {
                    char strCategory[64], strValue[64], strLabel[64];

                    sprintf(strCategory, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START,       'A' + COL_TBL_START,        1 + cntSamples);
                    sprintf(strValue,    "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START + col, 'A' + COL_TBL_START + col,  1 + cntSamples);

                    series = chart_add_series(chart, strCategory, strValue);
                    series2= chart_add_series(chart2, strCategory, strValue);

                    sprintf(strLabel, "%s counter back to back diff", i == 0 ? "ACPI" : "PIT i8254");
                    chart_series_set_name(series, strLabel);
                    chart_series_set_name(series2, strLabel);

                    col++;

                }
                chartsheet1 = workbook_add_chartsheet(workbook, nullptr/*sheetnametmp*/);

                // Insert the chart into the chartsheet.
                lxw_error lxwerr = chartsheet_set_chart(chartsheet1, chart2);
                chart_title_set_name(chart, "ACPI and PIT i8254 counter back to back diff characteristics");
                chart_title_set_name(chart2, "ACPI and PIT i8254 counter back to back diff characteristics");

                chartsheet_set_landscape(chartsheet1);

                worksheet_insert_chart(worksheet, CELL("B36"), chart);
            }

			//
			// full screen view diagram on second page
			//
			if (1)
			{

				/* Create a chart object. */
				lxw_chart* chart2 = workbook_add_chart(workbook, LXW_CHART_SCATTER);

				//
				// Configure the chart for all measurements
				//
				for (int i = 0, col = 3/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values' */;
					i < ELC(parms);
					i++)
				{
					char strCategory[64], strValue[64], strLabel[64];

					if (false == *parms[i].pEna)
						continue;

					sprintf(strCategory, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START, 'A' + COL_TBL_START, 1 + cntSamples);
					sprintf(strValue, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START + col, 'A' + COL_TBL_START + col, 1 + cntSamples);

					series = chart_add_series(chart2, strCategory, strValue);
					sprintf(strLabel, "Calibration Time\n%s", parms[i].szCalibrTime);
					chart_series_set_name(series, strLabel);

					col++;
					//if (false == gfAutoRun)
					//	pThis->TextWindowUpdateProgress();
				}
				chartsheet1 = workbook_add_chartsheet(workbook, nullptr/*sheetnametmp*/);

				// Insert the chart into the chartsheet.
				lxw_error lxwerr = chartsheet_set_chart(chartsheet1, chart2);
				chart_title_set_name(chart2, "Overall full screen view\nDrift in seconds per day.\nParameter: Calibration Time");
				chartsheet_set_landscape(chartsheet1);
			}

			//
			// create one additional chartsheet for each single measurement
			//
			for (int i = 0, col = 3/* COL 0 is reserved for line numbers, scatter charts must have 'categories' and 'values' */;
				i < ELC(parms);
				i++)
			{
				char strLabel[64];
				char strCategory[64], strValue[64];
				lxw_chart* chart[ELC(parms)];
				lxw_chart_series* series[ELC(parms)];
				lxw_chartsheet* chartsheet[ELC(parms)];
				char sheetnametmp[64], strTitle[64];

				if (false == *parms[i].pEna)
					continue;

				sprintf(sheetnametmp, "Chart%d", col);
				sprintf(strCategory, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START, 'A' + COL_TBL_START, 1 + cntSamples);
				sprintf(strValue, "=Sheet1!$%c$2:$%c$%d", 'A' + COL_TBL_START + col, 'A' + COL_TBL_START + col, 1 + cntSamples);

				// Create the chartsheet.
				chartsheet[i] = workbook_add_chartsheet(workbook, nullptr/*sheetnametmp*/);

				// Create a chart object.

				chart[i] = workbook_add_chart(workbook, LXW_CHART_SCATTER);

				// Add a data series to the chart.
		//		chart_add_series(chart, NULL, "=Sheet1!$A$1:$A$6");
				series[i] = chart_add_series(chart[i], strCategory, strValue);
				sprintf(strLabel, "Calibration time\n%s", parms[i].szCalibrTime);
				chart_series_set_name(series[i], strLabel);

				// Insert the chart into the chartsheet.
				lxw_error lxwerr = chartsheet_set_chart(chartsheet[i], chart[i]);
				//printf("%2d: %d\n", i, lxwerr);

				lxwerr = chartsheet_set_header(chartsheet[i], sheetnametmp);
				sprintf(strLabel, "Drift in seconds per day.\nCalibration time of %s \nscaled to 1s by factor %s", parms[i].szCalibrTime, parms[i].szMultiplier);
				chart_title_set_name(chart[i], strLabel);

				col++;
				//if (false == gfAutoRun)
				//	pThis->TextWindowUpdateProgress();
			}

			lxw_error lxwerr = workbook_close(workbook);
		}
	}//if (fCreateOvrd)

	if (0 == strcmp("ENTER", (char*)pParm))					// ENTER?
		if (nullptr != pSaveAsBox)
			pSaveAsBox->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,
			delete pSaveAsBox;

	return 0;
}

int fnMnuItm_View_SysInfo(CTextWindow* pThis, void* pContext, void* pParm)
{
	if (0 == strcmp("ENTER", (char*)pParm))				// Enter?
	{
		CTextWindow* pRoot = pThis->TextWindowGetRoot();

		pRoot->TextBlockClear();
		//gfHexView ^= true;// toggle debug state
		pThis->TextClearWindow(pRoot->WinAtt);
		//
		// write system configuration
		// 
		pRoot->TextPrint({ 2, 4 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI OemId                       : %s", gstrACPIOemId);
		pRoot->TextPrint({ 2, 5 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI OemTableId                  : %s", gstrACPIOemTableId);
		pRoot->TextPrint({ 2, 6 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI Timer Address               : %s", gstrACPIPmTmrBlkAddr);
		pRoot->TextPrint({ 2, 7 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI Timer Size                  : %s", gACPIPmTmrBlkSize);
		pRoot->TextPrint({ 2, 8 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI PCIEBase                    : %s", gACPIPCIEBase);

		pRoot->TextPrint({ 2, 10 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Vendor CPUID                     : %s", gstrCPUID0);
		pRoot->TextPrint({ 2, 11 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "HostBridge VID:DID               : %02X:%02X",
			((uint16_t*)pMCFG->BaseAddress)[0],
			((uint16_t*)pMCFG->BaseAddress)[1]
		);
		pRoot->TextPrint({ 2, 12 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "CPUID Signature                  : %s", gstrCPUIDSig);
		pRoot->TextPrint({ 2, 13 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "CPU Speed(measured)              : %s", gstrCPUSpeed);
		pRoot->TextPrint({ 2, 14 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "CPU Speed(EFI_TIMESTAMP_PROTOCOL): %s", gstrTIMESTAMP_PROTOCOL);
		if (0 != strncmp(gstrTIMESTAMP_PROTOCOL, "N/A", strlen("N/A")))
			pRoot->TextPrint({ 2, 15 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "    EFI_TIMESTAMP_PROTOCOL drift : %llds per day", gTIMESTAMP_PROTOCOLSecDriftPerDay);

		pRoot->TextPrint({ 2, 17 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "target .XLSX                     : %s", gCfgStr_File_SaveAs);
        pRoot->TextPrint({ 2, 18 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "RefTimerDev                      : %s", 2 == gfCfgSyncRef012 ? "PIT" : (1 == gfCfgSyncRef012 ? "RTC" : "ACPI"));//0 -> ACPI, 1 -> RTC, 2 -> PIT
		pRoot->TextPrint({ 2, 19 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "RefSyncTime                      : %ds", gnCfgRefSyncTime);
		pRoot->TextPrint({ 2, 20 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Calibration Method               : %s", gCfgStr_CalibrMethod);
        pRoot->TextPrint({ 2, 21 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Error correction                 : %s", 0 == gfErrorCorrection ? "disabled" : (pfnDelay == &InternalAcpiDelay ? "N/A on TIANOCORE" : "enabled"));
	}
	return 0;
}

int fnMnuItm_View_Clock(CTextWindow* pThis, void* pContext, void* pParm)
{
	if (0 == strcmp("ENTER", (char*)pParm))				// Enter?
	{
		CTextWindow* pRoot = pThis->TextWindowGetRoot();

		pRoot->TextBlockClear();
		gfCfgMngMnuItm_View_Clock ^= true;// toggle debug state
		pThis->TextClearWindow(pRoot->WinAtt);
	}
	return 0;
}

int fnMnuItm_View_Calendar(CTextWindow* pThis, void* pContext, void* pParm)
{
	if (0 == strcmp("ENTER", (char*)pParm))				// Enter?
	{
		CTextWindow* pRoot = pThis->TextWindowGetRoot();

		pRoot->TextBlockClear();
		gfCfgMngMnuItm_View_Calendar ^= true;// toggle debug state
		pThis->TextClearWindow(pRoot->WinAtt);
	}
	return 0;
}


const wchar_t* wcsTimerSelectionPicStrings[2][1] =
{
	{ L"  TIMER: PIT    " },
	{ L"\x25ba TIMER: PIT    " }
};

const wchar_t* wcsTimerSelectionAcpiStrings[2][1] =
{
	{ L"  TIMER: ACPI   "},
	{ L"\x25ba TIMER: ACPI   "}
};

const wchar_t* wcsTimerDelayPicStrings[2][5] =
{
	{
		L"- PIT :    1 * 1193181 ticks = 173*19*11*11*3 * 1 ",
		L"- PIT :   19 *   62799 ticks = 173*   11*11*3 * 1 ",
		L"- PIT :  363 *    3287 ticks = 173*19         * 1 ",
		L"- PIT : 6897 *     173 ticks = 173            * 1 ",
		L"- PIT : 9861 *     121 ticks =        11*11   * 1 "
	},
	{
		L"+ PIT :    1 * 1193181 ticks = 173*19*11*11*3 * 1 ",
		L"+ PIT :   19 *   62799 ticks = 173*   11*11*3 * 1 ",
		L"+ PIT :  363 *    3287 ticks = 173*19         * 1 ",
		L"+ PIT : 6897 *     173 ticks = 173            * 1 ",
		L"+ PIT : 9861 *     121 ticks =        11*11   * 1 "
	},
};

const wchar_t* wcsTimerDelayAcpiStrings[2][5] =
{
	{
		L"- Calibration Time: 1.0000 s      ",
		L"- Calibration Time: 52.632 ms     ",
		L"- Calibration Time:  2.755 ms     ",
		L"- Calibration Time:  1.000 ms     ",
		L"- Calibration Time: 101.41 us     "
	},
	{
		L"+ Calibration Time: 1.0000 s      ",
		L"+ Calibration Time: 52.632 ms     ",
		L"+ Calibration Time:  2.755 ms     ",
		L"+ Calibration Time:  1.000 ms     ",
		L"+ Calibration Time: 101.41 us     "
	},
};

const wchar_t* wcsCalibMethod[2][3] =
{
	{
		L"- Calibration Method:  TIANO  ACPI",
		L"- Calibration Method: TSCSYNC PIT ",
		L"- Calibration Method: TSCSYNC ACPI",
	},
	{
		L"+ Calibration Method:  TIANO  ACPI",
		L"+ Calibration Method: TSCSYNC PIT ",
		L"+ Calibration Method: TSCSYNC ACPI",
	},
};

const wchar_t* wcsErrorCorrection[3][1] =
{
	{
		L"- Error Correction: disabled      ",
	},
	{
		L"+ Error Correction: enabled       ",
	},
	{
		L"  Error Correction: N/A for TIANO ",
	},
};

int fnMnuItm_Config_ACPIDelaySelect1(CTextWindow* pThis, void* pContext, void* pParm) { CTextWindow* pRoot = pThis->TextWindowGetRoot(); char* pParmStr = (char*)pParm; menu_t* pMenu = (menu_t*)pContext; int nRet = 0; if (0 == strcmp("ENTER", pParmStr))pThis->TextClearWindow(pRoot->WinAtt); else { gfCfgMngMnuItm_Config_ACPIDelaySelect1 ^= 1;		pMenu->rgwcsMenuItem[0/* menu item 0 */] = (wchar_t*)(wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect1][0]); nRet = 1; }return nRet; }
int fnMnuItm_Config_ACPIDelaySelect2(CTextWindow* pThis, void* pContext, void* pParm) { CTextWindow* pRoot = pThis->TextWindowGetRoot(); char* pParmStr = (char*)pParm; menu_t* pMenu = (menu_t*)pContext; int nRet = 0; if (0 == strcmp("ENTER", pParmStr))pThis->TextClearWindow(pRoot->WinAtt); else { gfCfgMngMnuItm_Config_ACPIDelaySelect2 ^= 1;		pMenu->rgwcsMenuItem[1/* menu item 1 */] = (wchar_t*)(wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect2][1]); nRet = 1; }return nRet; }
int fnMnuItm_Config_ACPIDelaySelect3(CTextWindow* pThis, void* pContext, void* pParm) { CTextWindow* pRoot = pThis->TextWindowGetRoot(); char* pParmStr = (char*)pParm; menu_t* pMenu = (menu_t*)pContext; int nRet = 0; if (0 == strcmp("ENTER", pParmStr))pThis->TextClearWindow(pRoot->WinAtt); else { gfCfgMngMnuItm_Config_ACPIDelaySelect3 ^= 1;		pMenu->rgwcsMenuItem[2/* menu item 2 */] = (wchar_t*)(wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect3][2]); nRet = 1; }return nRet; }
int fnMnuItm_Config_ACPIDelaySelect4(CTextWindow* pThis, void* pContext, void* pParm) { CTextWindow* pRoot = pThis->TextWindowGetRoot(); char* pParmStr = (char*)pParm; menu_t* pMenu = (menu_t*)pContext; int nRet = 0; if (0 == strcmp("ENTER", pParmStr))pThis->TextClearWindow(pRoot->WinAtt); else { gfCfgMngMnuItm_Config_ACPIDelaySelect4 ^= 1;		pMenu->rgwcsMenuItem[3/* menu item 3 */] = (wchar_t*)(wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect4][3]); nRet = 1; }return nRet; }
int fnMnuItm_Config_ACPIDelaySelect5(CTextWindow* pThis, void* pContext, void* pParm) { CTextWindow* pRoot = pThis->TextWindowGetRoot(); char* pParmStr = (char*)pParm; menu_t* pMenu = (menu_t*)pContext; int nRet = 0; if (0 == strcmp("ENTER", pParmStr))pThis->TextClearWindow(pRoot->WinAtt); else { gfCfgMngMnuItm_Config_ACPIDelaySelect5 ^= 1;		pMenu->rgwcsMenuItem[4/* menu item 4 */] = (wchar_t*)(wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect5][4]); nRet = 1; }return nRet; }

int fnMnuItm_Config_ErrorCorrection(CTextWindow* pThis, void* pContext, void* pParm)
{
	CTextWindow* pRoot = pThis->TextWindowGetRoot();
	char* pParmStr = (char*)pParm;
	menu_t* pMenu = (menu_t*)pContext;
	int nRet = 0;

	if (0 == strcmp("ENTER", pParmStr))
		pThis->TextClearWindow(pRoot->WinAtt);
	else {
		gfErrorCorrection ^= true;

		pMenu->rgwcsMenuItem[12/* menu item12 */] = (wchar_t*)(wcsErrorCorrection[pfnDelay == &InternalAcpiDelay ? 2/*"  Error Correction: N/A for TIANO "*/ : gfErrorCorrection][0]);
		nRet = 1;
	}
	return nRet;
}

int fnMnuItm_Config_CalibMethodSelectTIANOACPI(CTextWindow* pThis, void* pContext, void* pParm)
{ 
	CTextWindow* pRoot = pThis->TextWindowGetRoot(); 
	char* pParmStr = (char*)pParm; menu_t* pMenu = (menu_t*)pContext; 
	int nRet = 0; 
	
	if (0 == strcmp("ENTER", pParmStr))
		pThis->TextClearWindow(pRoot->WinAtt); 
	else { 
		//
		// toggle selection of two (2)
		// 	
		gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = true,
			gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = false,
			gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = false,

		strcpy(gCfgStr_CalibrMethod, "original TIANOCORE");
		pfnDelay = &InternalAcpiDelay;

		pMenu->rgwcsMenuItem[8 /* menu item 8 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI][0]); 
		pMenu->rgwcsMenuItem[9 /* menu item 9 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT][1]);
		pMenu->rgwcsMenuItem[10/* menu item10 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI][2]);

		pMenu->rgwcsMenuItem[12/* menu item12 */] = (wchar_t*)(wcsErrorCorrection[pfnDelay == &InternalAcpiDelay ? 2/*"  Error Correction: N/A for TIANO "*/ : gfErrorCorrection][0]);

		pMenu->rgfnMnuItm[12] = nullptr;

		nRet = 1;
		nRet = 1;
	}
	return nRet; 
}

int fnMnuItm_Config_CalibMethodSelectTSCSYNCPIT(CTextWindow* pThis, void* pContext, void* pParm) 
{ 
	CTextWindow* pRoot = pThis->TextWindowGetRoot(); 
	char* pParmStr = (char*)pParm; 
	menu_t* pMenu = (menu_t*)pContext; 
	int nRet = 0; 
	
	if (0 == strcmp("ENTER", pParmStr))
		pThis->TextClearWindow(pRoot->WinAtt); 
	else { 

		gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = true,
			gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = false,
			gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = false;
		
		strcpy(gCfgStr_CalibrMethod, "native TSCSync i8254 PIT");
		pfnDelay = &PITClkWait;

		pMenu->rgwcsMenuItem[8 /* menu item 8 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI][0]);
		pMenu->rgwcsMenuItem[9 /* menu item 9 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT][1]);
		pMenu->rgwcsMenuItem[10/* menu item10 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI][2]);

		pMenu->rgwcsMenuItem[12/* menu item12 */] = (wchar_t*)(wcsErrorCorrection[pfnDelay == &InternalAcpiDelay ? 2/*"  Error Correction: N/A for TIANO "*/ : gfErrorCorrection][0]);
		
		pMenu->rgfnMnuItm[12] = &fnMnuItm_Config_ErrorCorrection;

		nRet = 1;
	}
	return nRet; 
}

int fnMnuItm_Config_CalibMethodSelectTSCSYNCACPI(CTextWindow* pThis, void* pContext, void* pParm)
{
	CTextWindow* pRoot = pThis->TextWindowGetRoot();
	char* pParmStr = (char*)pParm;
	menu_t* pMenu = (menu_t*)pContext;
	int nRet = 0;

	if (0 == strcmp("ENTER", pParmStr))
		pThis->TextClearWindow(pRoot->WinAtt);
	else {

		gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = true,
			gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = false,
			gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = false;

		strcpy(gCfgStr_CalibrMethod, "native TSCSync ACPI");
		pfnDelay = &AcpiClkWait;

		pMenu->rgwcsMenuItem[8 /* menu item 8 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI][0]);
		pMenu->rgwcsMenuItem[9 /* menu item 9 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT][1]);
		pMenu->rgwcsMenuItem[10/* menu item10 */] = (wchar_t*)(wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI][2]);

		pMenu->rgwcsMenuItem[12/* menu item12 */] = (wchar_t*)(wcsErrorCorrection[pfnDelay == &InternalAcpiDelay ? 2/*"  Error Correction: N/A for TIANO "*/ : gfErrorCorrection][0]);

		pMenu->rgfnMnuItm[12] = &fnMnuItm_Config_ErrorCorrection;

		nRet = 1;

	}
	return nRet;
}

int gidxCfgMngMnuItm_Config_NumSamples = 0;		// index of selected NumSamples 0/1/2/3, saved at program exit

const wchar_t* wcsNumSamples[2][5] =
{
	{L"-   10",L"-   50",L"-  250",L"- 1250"/*,L"-62500"*/},/* non-selected strings */
    {L"+   10",L"+   50",L"+  250",L"+ 1250"/*,L"+62500"*/},/*     selected strings */
};

int fnMnuItm_NumSamples(CTextWindow* pThis, void* pContext, void* pParm)
{ 
	CTextWindow* pRoot = pThis->TextWindowGetRoot();
	CTextWindow* pSubMnuTextWindow = new CTextWindow(
		pThis,
		{ pThis->WinPos.X + pThis->WinDim.X,pThis->WinPos.Y + pThis->WinDim.Y - 8 },
		{ 10,6 },
		EFI_BACKGROUND_CYAN | EFI_YELLOW);
	menu_t* pMenu = (menu_t*)pContext;
	int nRet = 0;
	int idxMnuItm = 0, idxMnuItmChecked = gidxCfgMngMnuItm_Config_NumSamples/*checked with space bar*/;
	int idxMnuItmNUM = 4;		/* number of lines within the pulldown menu */;
	TEXT_KEY key = NO_KEY;


	pSubMnuTextWindow->TextBorder({ 0, 0 }, pSubMnuTextWindow->WinDim,
		BOXDRAW_DOWN_RIGHT,
		BOXDRAW_DOWN_LEFT,
		BOXDRAW_UP_RIGHT,
		BOXDRAW_UP_LEFT,
		BOXDRAW_HORIZONTAL,
		BOXDRAW_VERTICAL,
		nullptr);

	//
	// fill menu with menuitem strings at once
	//
	pSubMnuTextWindow->TextBlockDraw({ 2,1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW, L"%s\n%s\n%s\n%s",
		wcsNumSamples[0 == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][0],
		wcsNumSamples[1 == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][1],
		wcsNumSamples[2 == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][2],
		wcsNumSamples[3 == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][3]
		);
	// highlight the first string initially
	pSubMnuTextWindow->TextPrint({ 2,1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, wcsNumSamples[0 == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][idxMnuItm]);

	//
	// "Message"-Loop, receive keyboard messages...
	//
	for (	key = NO_KEY;
			KEY_ESC != key && KEY_ENTER != key; 
			key = pThis->TextGetKey(), 
				pThis->TextWindowUpdateProgress()
		)
	{
		if (KEY_DOWN == key) {

			pSubMnuTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW, wcsNumSamples[idxMnuItm == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][idxMnuItm]);	// de-highlight previous menu item
			idxMnuItm = (++idxMnuItm == idxMnuItmNUM ? 0 : idxMnuItm);
			pSubMnuTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, wcsNumSamples[idxMnuItm == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][idxMnuItm]);	// de-highlight previous menu item
		}
		else if (KEY_UP == key) {

			pSubMnuTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW, wcsNumSamples[idxMnuItm == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][idxMnuItm]);	// de-highlight previous menu item
			idxMnuItm = (--idxMnuItm < 0 ? idxMnuItmNUM - 1 : idxMnuItm);
			pSubMnuTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, wcsNumSamples[idxMnuItm == gidxCfgMngMnuItm_Config_NumSamples/* selected/non-selected */][idxMnuItm]);	// de-highlight previous menu item
		}

		if (KEY_SPACE == key) {

			gidxCfgMngMnuItm_Config_NumSamples = idxMnuItm;
			pSubMnuTextWindow->TextPrint({ 2,idxMnuItmChecked + 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW, wcsNumSamples[0][idxMnuItmChecked]);	// de-highlight previous menu item
			pSubMnuTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, wcsNumSamples[1][idxMnuItm]);			// highlight current menu item
			idxMnuItmChecked = idxMnuItm;
		}
	}
	//// save, if selected with ENTER, skip if ESC
	//if (KEY_ENTER == key)
	//	gidxCfgMngMnuItm_Config_NumSamples = idxMnuItm;

	pSubMnuTextWindow->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK;
	if (KEY_ENTER == key)
	{
		delete pSubMnuTextWindow->pParent;									// destroy the CONFIG menu window
		delete pSubMnuTextWindow;											// destroy the SUBMENU window
		nRet = 0;															// do not refresh menu window, it is destroyed
	}
	else {
		delete pSubMnuTextWindow;											// destroy the SUBMENU window
		nRet = 1;															// refresh menu window
	}
	return nRet;
}


/////////////////////////////////////////////////////////////////////////////
// About - BOX
/////////////////////////////////////////////////////////////////////////////
int fnMnuItm_About_0(CTextWindow* pThis, void* pContext, void* pParm)
{
	CTextWindow* pAboutBox;
	CTextWindow* pRoot = pThis->TextWindowGetRoot();

	pThis->TextClearWindow(pRoot->WinAtt);	// clear the pull down menu 
	pRoot->TextBlockRfrsh();	// refresh the main window

	if (1)// center coordinate calculation
	{

	}
	pAboutBox = new CTextWindow(pThis, { pRoot->WinDim.X / 2 - 78 / 2,pRoot->WinDim.Y / 2 - 21 / 2 }, { 78,21 }, EFI_BACKGROUND_CYAN | EFI_YELLOW);

	pAboutBox->TextBorder(
		{ 0,0 },
		{ 78,21 },
		BOXDRAW_DOWN_RIGHT,
		BOXDRAW_DOWN_LEFT,
		BOXDRAW_UP_RIGHT,
		BOXDRAW_UP_LEFT,
		BOXDRAW_HORIZONTAL,
		BOXDRAW_VERTICAL,
		nullptr
	);
    
    if (1)
    {
        long long tsbld = 0;
        sscanf(buildtimestamp, "%lld", &tsbld);

        pAboutBox->TextPrint({ 1, 1 }, " TSCSYNC -- TimeStampCounter (TSC) synchronizer, Build %llX",tsbld);
        pAboutBox->TextPrint({ 1, 2 }, "  Analyzing System Timer characteristics, Copyright(c) 2023, Kilian Kegel");
        pAboutBox->TextPrint({ 1, 4 }, "  Sample program to demonstrate for UEFI platforms:");
        pAboutBox->TextPrint({ 1, 5 }, "    - data processing, logging, representation for laboratory applications");
        pAboutBox->TextPrint({ 1, 6 }, "    - implementation of a simple menu driven user interface");
        pAboutBox->TextPrint({ 1, 7 }, "    - integration of open source 3rd party libraries (ZLIB, LIBXLSXWRITER)");
        pAboutBox->TextPrint({ 1, 9 }, "  Command line options:");
        pAboutBox->TextPrint({ 1,10 }, "   /AUTORUN          - run, save and terminate previously configured session");
        pAboutBox->TextPrint({ 1,11 }, "   /SYNCREF:<parm>   - choose RTC/ACPI timer reference, (default ACPI)");
        pAboutBox->TextPrint({ 1,12 }, "   /SYNCTIME:<sec>   - choose 3s sync time, (default 1 second)");
        pAboutBox->TextPrint({ 1,13 }, "   /OUT:<fname.xlsx> - assign filname of EXCEL logfile in .XLSX fileformat");
        pAboutBox->TextPrint({ 1,14 }, "   /METHOD:<type>    - calibration method TIANO (InternalAcpiDelay()),");
        pAboutBox->TextPrint({ 1,15 }, "                       ACPI (TSCSYNC-ACPI) or PIT (TSCSYNC-PIT-i8254)");
        pAboutBox->TextPrint({ 1,16 }, "   /NUM:0/1/2/3      - number of samples 0: 10, 1: 50, 2: 250, 3: 1250");
        pAboutBox->TextPrint({ 1,17 }, "   /ERRCODIS         - disable error correction of additionally gone through");
	    pAboutBox->TextPrint({ 1,18 }, "                       counter ticks. N/A for TIANOCORE measurement method");

    }
	//RealTimeClock Analyser

	for (TEXT_KEY key = NO_KEY; KEY_ESC != key && KEY_ENTER != key; key = pThis->TextGetKey(), pThis->TextWindowUpdateProgress())
		;

	pAboutBox->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK;
	delete pAboutBox;
	return 0;
}

int fnMnuItm_About_1(CTextWindow* pThis, void* pContext, void* pParm)
{
	CTextWindow* pRoot = pThis->TextWindowGetRoot();
	gfKbdDbg ^= true;// toggle debug state

	if (false == gfKbdDbg)
	{
		char* pLineKill = new char[pThis->ScrDim.X - 4];

		memset(pLineKill, '\x20', pThis->ScrDim.X - 4);
		pLineKill[pThis->ScrDim.X - 4] = '\0';

		pThis->GotoXY({ 2,pThis->ScrDim.Y - 3 });
		printf("%s", pLineKill);
	}

	pThis->TextClearWindow(pRoot->WinAtt);
	return 0;
}

void resetconsole(void)
{
	gSystemTable->ConOut->EnableCursor(gSystemTable->ConOut, 1);
	gSystemTable->ConOut->SetAttribute(gSystemTable->ConOut, EFI_BACKGROUND_BLACK + EFI_WHITE);
}

int fnMnuItm_RunConfig_0(CTextWindow* pThis, void* pContext, void* pParm)
{
	CTextWindow* pRoot = pThis->TextWindowGetRoot();

	gfRunConfig = true;

	pThis->TextClearWindow(pRoot->WinAtt);
	return 0;
}

int main(int argc, char** argv)
{
	int nRet = 1;
	gSystemTable = (EFI_SYSTEM_TABLE*)(argv[-1]);		//SystemTable is passed in argv[-1]
	gImageHandle = (void*)(argv[-2]);					//ImageHandle is passed in argv[-2]
	TEXT_KEY key = NO_KEY;

    if (0)
	{
        __debugbreak();

        srand((unsigned)__rdtsc());
        
		pfnDelay = &PITClkWait;// InternalAcpiDelay;// PITClkWait;

		for (int j = 123456; j <= 123456; j++)
			for (int64_t i = 0, n; i < 10; i++)
				n = (*pfnDelay)(j),
				printf("%s-> %lld, %d\n",n != j ? "ERROR " : "OKAY  " ,n,j);


		exit(0);
	}
	atexit(resetconsole);
	EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE* pFACP = (EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE*)malloc(32768 * 8);
	uint32_t DataSize2;
	//	_set_invalid_parameter_handler(invalid_parameter_handler);

	DataSize2 = GetSystemFirmwareTable('ACPI', 'PCAF', pFACP, 32768 * 8);

	sprintf(gstrACPISignature, "%.4s", (char*)&pFACP->Header.Signature);
	sprintf(gstrACPIOemId, "%.6s", (char*)&pFACP->Header.OemId);
	sprintf(gstrACPIOemTableId, "%.8s", (char*)&pFACP->Header.OemTableId);

	sprintf(gstrACPIPmTmrBlkAddr, "%04X", pFACP->PmTmrBlk);
	sprintf(gACPIPmTmrBlkSize, "%sBit", (0 != (pFACP->Flags & (1 << 8))) ? "32" : "24");

	gPmTmrBlkAddr = static_cast<uint16_t> (pFACP->PmTmrBlk);				// save ACPI timer base adress


    //
    // Initialize PIT timer channel 2
    //
    _outp(0x61, 0);                          // stop counter
    _outp(0x43, (2/*TIMER*/ << 6) + 0x34);   // program timer 2 for MODE 2
    _outp(0x42, 0x0);                        // write counter value low 65535
    _outp(0x42, 0x0);                        // write counter value high 65535
    _outp(0x61, 1);                          // start counter

	//
	// EFI_TIMESTAMP_PROTOCOL
	//
	if (1)
	{
		EFI_STATUS Status;
		EFI_GUID efi_timestamp_protocol_guid = EFI_TIMESTAMP_PROTOCOL_GUID;
		EFI_TIMESTAMP_PROTOCOL* pEFI_TIMESTAMP_PROTOCOL = (EFI_TIMESTAMP_PROTOCOL*)-1;
		EFI_TIMESTAMP_PROPERTIES efi_timestamp_properties;
		int i, synctime = 3, token;

		Status = gSystemTable->BootServices->LocateProtocol(&efi_timestamp_protocol_guid, NULL, (void**) &pEFI_TIMESTAMP_PROTOCOL);
		
		gTIMESTAMP_PROTOCOLPerSec = 0;

		if (EFI_SUCCESS != Status)
		{
			sprintf(gstrTIMESTAMP_PROTOCOL,"N/A, \"%s\"", _strefierror(Status));
		
		}
		else {
			Status = pEFI_TIMESTAMP_PROTOCOL->GetProperties(&efi_timestamp_properties);
			if (EFI_SUCCESS != Status)
				sprintf(gstrTIMESTAMP_PROTOCOL, "N/A, \"%s\n", _strefierror(Status));
			else
				sprintf(gstrTIMESTAMP_PROTOCOL, "%lldHz", efi_timestamp_properties.Frequency),
				gTIMESTAMP_PROTOCOLPerSec = (int64_t)efi_timestamp_properties.Frequency;

		}


	}

	DataSize2 = GetSystemFirmwareTable('ACPI', 'GFCM', pMCFG, 32768 * 8);
	sprintf(gACPIPCIEBase, "%p", pMCFG->BaseAddress);

	//
	// CPU ID
	//
	if (1)
	{
		int cpuInfo[4] = { 0,0,0,0 };
		char* pStr = (char*) &cpuInfo[1];
		unsigned char* puc;
		__cpuid(cpuInfo, 0);
		sprintf(gstrCPUID0, "%c%c%c%c%c%c%c%c%c%c%c%c",
			pStr[0],
			pStr[1],
			pStr[2],
			pStr[3],
			pStr[8],
			pStr[9],
			pStr[10],
			pStr[11],
			pStr[4],
			pStr[5],
			pStr[6],
			pStr[7]);

        __cpuid(cpuInfo, 1);
		sprintf(gstrCPUIDSig, "%X", cpuInfo[0]);
		sprintf(gstrCPUIDFam, "%X", 0x0F != (0x0F & cpuInfo[0] >> 8) ? (0x0F & cpuInfo[0] >> 8) : (0x0F & cpuInfo[0] >> 8) + (0xFF & cpuInfo[0] >> 20));
		sprintf(gstrCPUIDMod, "%X", ((6 == (0x0F & cpuInfo[0] >> 8)) || (0x0F == (0x0F & cpuInfo[0] >> 8))) ? ((0x1F & cpuInfo[0] >> 15) << 4) + (0x0F & cpuInfo[0] >> 4) : 0x0F & cpuInfo[0] >> 4);
		sprintf(gstrCPUIDStp, "%X", 0x0F & cpuInfo[0]);

	}

	//
	// getting config data from 
	//
	if (1)
	{
		FILE* fp = fopen("tscsync.cfg", "r");

		if (nullptr != fp)
		{
			//printf("reading cfg file\n");
			int tok = fscanf(fp, 
				"gfCfgMngMnuItm_View_Clock = %hhu\n\
				gfCfgMngMnuItm_View_Calendar = %hhu\n\
				gfCfgMngMnuItm_Config_PicApicSelect = %hhu\n\
				gfCfgMngMnuItm_Config_PITDelaySelect1   = %hhu\n\
				gfCfgMngMnuItm_Config_PITDelaySelect2   = %hhu\n\
				gfCfgMngMnuItm_Config_PITDelaySelect3   = %hhu\n\
				gfCfgMngMnuItm_Config_PITDelaySelect4   = %hhu\n\
				gfCfgMngMnuItm_Config_PITDelaySelect5   = %hhu\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect1  = %hhu\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect2  = %hhu\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect3  = %hhu\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect4  = %hhu\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect5  = %hhu\n\
				gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI  = %hhu\n\
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT  = %hhu\n\
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI  = %hhu\n\
				gidxCfgMngMnuItm_Config_NumSamples = %d\n\
				gCfgStr_File_SaveAs = %s\n\
				gfErrorCorrection = %hhu\n",

				(char*)&gfCfgMngMnuItm_View_Clock,
				(char*)&gfCfgMngMnuItm_View_Calendar,
				(char*)&gfCfgMngMnuItm_Config_PicApicSelect,

				(char*)&gfCfgMngMnuItm_Config_PITDelaySelect1 ,
				(char*)&gfCfgMngMnuItm_Config_PITDelaySelect2 ,
				(char*)&gfCfgMngMnuItm_Config_PITDelaySelect3 ,
				(char*)&gfCfgMngMnuItm_Config_PITDelaySelect4 ,
				(char*)&gfCfgMngMnuItm_Config_PITDelaySelect5 ,
				(char*)&gfCfgMngMnuItm_Config_ACPIDelaySelect1,
				(char*)&gfCfgMngMnuItm_Config_ACPIDelaySelect2,
				(char*)&gfCfgMngMnuItm_Config_ACPIDelaySelect3,
				(char*)&gfCfgMngMnuItm_Config_ACPIDelaySelect4,
				(char*)&gfCfgMngMnuItm_Config_ACPIDelaySelect5,
				(char*)&gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI,
				(char*)&gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT,
                (char*)&gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI,

				(int*)&gidxCfgMngMnuItm_Config_NumSamples,
				&gCfgStr_File_SaveAs[0],
				&gfErrorCorrection
			);

		}
	}

	//
	// process command line
	// 
	for (int arg = 1; arg < argc; arg++)
	{
		if (
			0 == _strnicmp(argv[arg], "/?", strlen("/?"))
			|| 0 == _strnicmp(argv[arg], "/h", strlen("/h"))
			|| 0 == _strnicmp(argv[arg], "-?", strlen("-?"))
			|| 0 == _strnicmp(argv[arg], "-h", strlen("-h"))
			)
		{   //      |------------------------------------------------------------------------------|
            long long tsbld = 0;
            sscanf(buildtimestamp, "%lld", &tsbld);
            printf("\nTSCSYNC -- TimeStampCounter (TSC) synchronizer, Build %llX\n  Analysing System Timer characteristics, Copyright (c) 2023, Kilian Kegel\n\n", tsbld);
            printf("  Sample program to demonstrate for UEFI platforms:\n");
            printf("    - data processing, logging, representation for laboratory applications\n");
            printf("    - implementation of a simple menu driven user interface\n");
            printf("    - integration of open source 3rd party libraries (ZLIB, LIBXLSXWRITER)\n\n");
            printf("  Command line options:\n");
            printf("   /AUTORUN          - run, save and terminate previously configured session\n");
            printf("   /SYNCREF:<parm>   - choose RTC/ACPI timer reference, (default ACPI)\n");
            printf("   /SYNCTIME:<sec>   - choose 3s sync time, (default 1 second)\n");
            printf("   /OUT:<fname.xlsx> - assign filname of EXCEL logfile in .XLSX fileformat\n");
            printf("   /METHOD:<type>    - calibration method TIANO (InternalAcpiDelay()),\n");
            printf("                       ACPI (TSCSYNC-ACPI) or PIT (TSCSYNC-PIT-i8254)\n");
            printf("   /NUM:0/1/2/3      - number of samples 0: 10, 1: 50, 2: 250, 3: 1250\n");
            printf("   /ERRCODIS         - disable error correction of additionally gone through\n");
            printf("                       counter ticks. N/A for TIANOCORE measurement method\n");
			exit(0);
		}

        if (0 == _strnicmp(argv[arg], "/ERRCODIS", strlen("/ERRCODIS")))
        {
            gfErrorCorrection = false;
        }


        if (0 == _strnicmp(argv[arg], "/NUM", strlen("/NUM")))
        {
            char strtmp[8];
            int t,num;
            
            t = sscanf(argv[arg], "%4s:%d", &strtmp, &num);

            if (!(num >= 0 && num <= 3))
                t = -1;

            if (t != 2)
            {
                fprintf(stderr, "Parameter failure \"%s\", consider format: \"/NUM:<0/1/2/3>\"", argv[arg]);
                exit(1);
            }
            
            gidxCfgMngMnuItm_Config_NumSamples = num;
        }

		if (0 == _strnicmp(argv[arg], "/OUT", strlen("/OUT")))
		{
			char strtmp[8];
			int t;

			t = sscanf(argv[arg], "%4s:%s", &strtmp, &gCfgStr_File_SaveAs);

			if (t != 2)
			{
				fprintf(stderr, "Parameter failure \"%s\", consider format: \"/OUT:filename.xlsx\"", argv[arg]);
				exit(1);
			}
		}

		if (0 == _stricmp(argv[arg], "/AUTORUN"))
			gfAutoRun = true,
			gfRunConfig = true;

		if (0 == _strnicmp(argv[arg], "/SYNCREF", strlen("/SYNCREF")))
		{
			char strtmp[8], strtmp2[16];
			int t;
			bool fErr = false;

			t = sscanf(argv[arg], "%8s:%s", &strtmp, &strtmp2);

			if (t != 2)
				fErr = true;

			if (0 == _stricmp(strtmp2, "RTC"))
				gfCfgSyncRef012 = 1;
			else if (0 == _stricmp(strtmp2, "ACPI"))
				gfCfgSyncRef012 = 0;
			else if (0 == _stricmp(strtmp2, "PIT"))
				gfCfgSyncRef012 = 2;
			else
				fErr = true;

			if (true == fErr)
			{
				fprintf(stderr, "Parameter failure \"%s\", consider format: \"/SYNCREF:ACPI\" or \"/SYNCREF:RTC\"", argv[arg]);
				exit(1);
			}
		}

		if (0 == _strnicmp(argv[arg], "/SYNCTIME", strlen("/SYNCTIME")))
		{
			char strtmp[16];
			int t;

			t = sscanf(argv[arg], "%9s:%d", &strtmp, &gnCfgRefSyncTime);

			if (t != 2 || 1 > gnCfgRefSyncTime)
			{
				fprintf(stderr, "Parameter failure \"%s\", consider format: \"/SYNCTIME:1\"", argv[arg]);
				exit(1);
			}
		}

        if (0 == _strnicmp(argv[arg], "/METHOD", strlen("/METHOD")))
        {
            char strtmp[8] = "", strtmp2[16] = "";
            int t;
            bool fErr = false;

            t = sscanf(argv[arg], "%7s:%s", &strtmp, &strtmp2);

            if (t != 2)
                fErr = true;

            if (0 == _stricmp(strtmp2, "TIANO"))
            {
				gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = true;
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = false;
                gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = false;

				strcpy(gCfgStr_CalibrMethod, "original TIANOCORE");
				pfnDelay = &InternalAcpiDelay;
            }
            else if (0 == _stricmp(strtmp2, "ACPI")) 
            {
                gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = false;
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = false;
                gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = true;

				strcpy(gCfgStr_CalibrMethod, "native TSCSync ACPI");
				pfnDelay = &AcpiClkWait;
            }
			else if (0 == _stricmp(strtmp2, "PIT"))
			{
				gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = false;
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = true;
                gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = false;

				strcpy(gCfgStr_CalibrMethod, "native TSCSync i8254 PIT");
				pfnDelay = &PITClkWait;
			}
			else
                fErr = true;

            if (true == fErr)
            {
                fprintf(stderr, "Parameter failure \"%s\", consider format: \"/METHOD:TIANO\" or \"/METHOD:ACPI\" or \"/METHOD:PIT\", Tokens %d, \"%s:%s\"\n", argv[arg],t,strtmp,strtmp2);
                exit(1);
            }

            if (0 == _strnicmp(argv[arg], "/TIMERWIDTH", strlen("/TIMERWIDTH")))
            {
                char strtmp[16];
                int t;

                t = sscanf(argv[arg], "%11s:%d", &strtmp, &gCOUNTER_WIDTH);

                if (t != 2 || 1 > gCOUNTER_WIDTH)
                {
                    fprintf(stderr, "Parameter failure \"%s\", consider format: \"/TIMERWIDTH:24\"", argv[arg]);
                    exit(1);
                }
            }
        }

    }

    //
    // set initial calibration method
    //
    if (1)
    {
        if (true == gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI && false == gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT)
            strcpy(gCfgStr_CalibrMethod, "original TIANOCORE"),
            pfnDelay = &InternalAcpiDelay;

        if (false == gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI && false == gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT)
            strcpy(gCfgStr_CalibrMethod, "native TSCSync ACPI"),
            pfnDelay = &AcpiClkWait;

        if (false == gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI && true == gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT)
            strcpy(gCfgStr_CalibrMethod, "native TSCSync i8254 PIT"),
            pfnDelay = &PITClkWait;

        printf("Initial calibration Method: %s\n", gCfgStr_CalibrMethod);
        
    }

    //
    // record ACPI timer charcteristics
    //
    if (1)
    {
        uint64_t min = (uint64_t)~0, max = 0, sum = 0;
        int32_t diff;
        _disable();

        for (int i = 0; i < MAXNUM; i++)
            ACPIB2BStat[i] = _inpd(gPmTmrBlkAddr);

        for (int i = 1; i < MAXNUM; i++)
        {
            diff = ACPIB2BStat[i] - ACPIB2BStat[i - 1];

            if (diff >= 0)
            {
                min = diff < min ? diff : min;
                max = diff > max ? diff : max;
                sum += diff;
                //if(diff > 1)
                //	printf("--> %4d: diff %5d\n", i,diff);
            }
        }
        printf("ACPI Timer characteristic %8lld consecutive reads: min %3lld, max %3lld, av %3lld\n", MAXNUM, min, max, sum / MAXNUM);
    }

    //
    // record PIT i8254 timer charcteristics
    //
    if (1)
    {
        //unsigned char counterLoHi[2];
        unsigned char* pbCount = (unsigned char*)&PITB2BStat[0];
        int64_t min = (uint64_t)~0, max = 0, sum = 0;
        int16_t diff;
#define TIMER 2

        _disable();

        for (int i = 0; i < MAXNUM; i++)
        {
            _outp(0x43, (TIMER << 6) + 0x0);                         // counter latch timer 2
            pbCount[0] = (unsigned char)_inp(0x40 + TIMER);          // get low byte
            pbCount[1] = (unsigned char)_inp(0x40 + TIMER);          // get high byte
            pbCount = &pbCount[2];
        }

        for (int i = 1; i < MAXNUM; i++)
        {
            diff = PITB2BStat[i - 1] - PITB2BStat[i];
            if ((int16_t)diff >= 0)
            {
                min = diff < min ? diff : min;
                max = diff > max ? diff : max;
                sum += diff;
                //printf("--> sum %5d\n", sum);
            }

            //printf("%d\n", buffer[i - 1] - buffer[i]);
        }
        printf("PIT  i8254 characteristic %8d consecutive reads: min %3lld, max %3lld, av %3lld\n", MAXNUM, min, max, sum / MAXNUM);

    }

    //
	// wait reference sync
	//
	if (1)
	{
		int SECONDS = gnCfgRefSyncTime;
		int64_t qwTSCEnd = 0 , qwTSCStart = 0;
		int sec = 1 + SECONDS;

		//
		// wait UP (update ended) interrupt flag to start on time https://www.nxp.com/docs/en/data-sheet/MC146818.pdf#page=16
		// 
		printf("%d seconds for ultra precise TSC calibration on %s... \n", SECONDS, 2 == gfCfgSyncRef012 ? "PIT" : (1 == gfCfgSyncRef012 ? "RTC" : "ACPI"));
		qwTSCStart = 0;

		_disable();

		_outp(0x70, 0x0A);										// RTC Register A

		while (0 == (0x80 & _inp(0x71)))
			;
		while (0 != (0x80 & _inp(0x71)))
			;
		qwTSCStart = __rdtsc();									// get start TSC at falling edge


		for (int i = 0; i < 1; i++)
		{
			if (1 == gfCfgSyncRef012) {
				sec = 1 + SECONDS;

				for (int i = 0; i < SECONDS; i++)
				{
					while (0 == (0x80 & _inp(0x71)))
						;
					while (0 != (0x80 & _inp(0x71)))					// wait for second falling edge
						;
				}
				qwTSCEnd = __rdtsc();									// get end TSC
			}
			else if (0 == gfCfgSyncRef012) {
				qwTSCEnd += AcpiClkWait(SECONDS * 3579543);				// this function returns the diff
				qwTSCStart = 0;											// clear start
			} else if (2 == gfCfgSyncRef012) {
				qwTSCEnd += PITClkWait(SECONDS * 3579543);				// this function returns the diff
				qwTSCStart = 0;											// clear start
			}//if (gfCfgRefSyncRTC) {
		}

		gTSCPerSecondReference = (int64_t)((qwTSCEnd - qwTSCStart) / SECONDS);
		
		if (1)
		{
			int64_t gTSCPerDayReference = (int64_t)(86400 * (qwTSCEnd - qwTSCStart) / SECONDS);
			/*
			                                        gTIMESTAMP_PROTOCOLPerSec * 86400 
				gTIMESTAMP_PROTOCOLSecDriftPerDay = --------------------------------- - 86400
				                                             gTSCPerDayReference
			*/
			gTIMESTAMP_PROTOCOLSecDriftPerDay = ((gTIMESTAMP_PROTOCOLPerSec * 86400) / gTSCPerSecondReference) - 86400;
		}
		

		printf("%s sync base: diff %lld\n", 2 == gfCfgSyncRef012 ? "PIT" : (1 == gfCfgSyncRef012 ? "RTC" : "ACPI"), (qwTSCEnd - qwTSCStart) / SECONDS);
	}

	//
	//	determine processor speed within 250ms second
	//
	if (1)
	{
		char buffer[128] = "Determining processor speed ...";
		//printf("%s", buffer);

		clock_t endCLK = CLOCKS_PER_SEC / 4 + clock();
		uint64_t endTSC, startTSC = __rdtsc();

		while (endCLK > clock())
			;
		endTSC = __rdtsc();

		sprintf(buffer, "%lld", 4 * (endTSC - startTSC));

		sprintf(gstrCPUSpeed, "%lldHz, %c.%c%cGHz", gTSCPerSecondReference, buffer[0], buffer[1], buffer[2]);
	}

	do
	{
		char* pc = new char[256];
		int* pi = new int(12345678);
		static wchar_t wcsSeparator17[] = { BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL,BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, BOXDRAW_HORIZONTAL, '\0' };

		menu_t menu[] =
		{
			{{ 1,0},	L" FILE ",		nullptr,{43,6/* # menuitems + 2 */},	/*{false},*/ {	L"Save series of measurements as .XLSX...",
																								wcsSeparator17,
																								L"Exit...                                ",
																								L"Save and Exit...                       "},{&fnMnuItm_File_SaveAs, nullptr, &fnMnuItm_File_Exit,&fnMnuItm_File_SaveExit}},
			{{ 8,0},	L" CONF ",	nullptr,{38,15	/* # menuitems + 2 */},	/*{false, false, true, false},*/
				{
					/*index 3 */ wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect1][0],	/* selected by default menu strings */
					/*index 4 */ wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect2][1],
					/*index 5 */ wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect3][2],
					/*index 6 */ wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect4][3],
					/*index 7 */ wcsTimerDelayAcpiStrings[gfCfgMngMnuItm_Config_ACPIDelaySelect5][4],
					/*index 8 */ wcsSeparator17,
					/*index 9 */ L"  NumSamples #                \x25BA ",
					/*index10 */ wcsSeparator17,
					/*index11 */ wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI][0],
					/*index12 */ wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT][1],
					/*index13 */ wcsCalibMethod[gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI][2],
					/*index10 */ wcsSeparator17,
					/*index10 */ wcsErrorCorrection[pfnDelay == &InternalAcpiDelay ? 2 : gfErrorCorrection][0],
				},
				{
					/*index 3 */ &fnMnuItm_Config_ACPIDelaySelect1,
					/*index 4 */ &fnMnuItm_Config_ACPIDelaySelect2,
					/*index 5 */ &fnMnuItm_Config_ACPIDelaySelect3,
					/*index 6 */ &fnMnuItm_Config_ACPIDelaySelect4,
					/*index 7 */ &fnMnuItm_Config_ACPIDelaySelect5,
					/*index 8 */ nullptr/* nullptr identifies SEPARATOR */,
					/*index 9 */ &fnMnuItm_NumSamples,
					/*index10 */ nullptr/* nullptr identifies SEPARATOR */,
					/*index11 */ &fnMnuItm_Config_CalibMethodSelectTIANOACPI,
					/*index12 */ &fnMnuItm_Config_CalibMethodSelectTSCSYNCPIT,
					/*index13 */ &fnMnuItm_Config_CalibMethodSelectTSCSYNCACPI,
					/*index10 */ nullptr/* nullptr identifies SEPARATOR */,
					/*index10 */ gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI ? nullptr : fnMnuItm_Config_ErrorCorrection/* nullptr identifies SEPARATOR */,
					}
				},
			{{15,0},	L" RUN  ",		nullptr,{20,3/* # menuitems + 2 */},	/*{false, false},*/ {L"Run CONFIG      "},{&fnMnuItm_RunConfig_0}},
			{{22,0},	L" VIEW ",		nullptr,{23,5/* # menuitems + 2 */},	/*{false},*/ {L"System Information ",L"Clock              ",L"Calendar           " },{&fnMnuItm_View_SysInfo,&fnMnuItm_View_Clock,&fnMnuItm_View_Calendar}},
			{{29,0},	L" HELP ",		nullptr,{20,4/* # menuitems + 2 */},	/*{false, false},*/ {L"About           ",L"KEYBOARD DEBUG  "},{&fnMnuItm_About_0, &fnMnuItm_About_1 }},
		};

		CTextWindow FullScreen(nullptr/* no parent pointer - this is the main window !!! */, DFLT_SCREEN_ATTRIBS);

		//
		// draw the border
		//
		if (1)
		{
			wchar_t wcsTitle[] = { BOXDRAW_VERTICAL_LEFT_DOUBLE,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,
				0x20,L'T',L'S',L'C',L'S',L'y',L'n',L'c',0x20,
				BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE ,BLOCKELEMENT_LIGHT_SHADE,BOXDRAW_VERTICAL_RIGHT_DOUBLE,'\0' };


			FullScreen.TextBorder(
				{ 0,1 },
				{ FullScreen.WinDim.X, FullScreen.WinDim.Y - 2/*FullScreen.WinDim.Y*/ },
				//{ FullScreen.WinPos.X-10,FullScreen.WinPos.Y - 11 },
				BOXDRAW_DOUBLE_DOWN_RIGHT,
				BOXDRAW_DOUBLE_DOWN_LEFT,
				BOXDRAW_DOUBLE_UP_RIGHT,
				BOXDRAW_DOUBLE_UP_LEFT,
				BOXDRAW_DOUBLE_HORIZONTAL,
				BOXDRAW_DOUBLE_VERTICAL,
				&wcsTitle[0]
			);
		}
		//
		// draw the menu strings
		//
		for (int i = 0; i < ELC(menu); i++)		// initialize the menu strings
			FullScreen.TextPrint(menu[i].RelPos, menu[i].wcsMenuName);

		//
		// draw status/help line
		//
		if (1)
		{
			wchar_t wcsARROW_LEFT[2] = { ARROW_LEFT ,'\0' },
				wcsARROW_UP[2] = { ARROW_UP ,'\0' },
				wcsARROW_RIGHT[2] = { ARROW_RIGHT ,'\0' },
				wcsARROW_DOWN[2] = { ARROW_DOWN ,'\0' };
			char* pLineKill = new char[FullScreen.ScrDim.X];

			memset(pLineKill, '\x20', FullScreen.ScrDim.X),
				pLineKill[FullScreen.ScrDim.X - 1] = '\0';

			if (false == gfStatusLineVisible)
			{
				gfStatusLineVisible = true;
				FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, pLineKill);
				FullScreen.TextPrint({ 1, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, L"F10:Menu \x25C4\x2518:Select SPACE:Check ESC:Return %s%s%s%s:Navigate", wcsARROW_LEFT, wcsARROW_RIGHT, wcsARROW_UP, wcsARROW_DOWN);
				
				if(gfAutoRun)
					FullScreen.TextPrint({ FullScreen.ScrDim.X / 2 - (int32_t)sizeof(" A U T O   R U N   M O D E ") / 2, FullScreen.ScrDim.Y - 2 }, EFI_BACKGROUND_RED | EFI_WHITE, " A U T O   R U N   M O D E ");

			}
		}

		//
		// write system configuration
		// 
		FullScreen.TextPrint({ 2, 4 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI OemId                       : %s", gstrACPIOemId);
		FullScreen.TextPrint({ 2, 5 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI OemTableId                  : %s", gstrACPIOemTableId);
		FullScreen.TextPrint({ 2, 6 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI Timer Address               : %s", gstrACPIPmTmrBlkAddr);
		FullScreen.TextPrint({ 2, 7 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI Timer Size                  : %s", gACPIPmTmrBlkSize);
		FullScreen.TextPrint({ 2, 8 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK,  "ACPI PCIEBase                    : %s", gACPIPCIEBase);

		FullScreen.TextPrint({ 2, 10 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Vendor CPUID                     : %s", gstrCPUID0);
		FullScreen.TextPrint({ 2, 11 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "HostBridge VID:DID               : %02X:%02X",
			((uint16_t*)pMCFG->BaseAddress)[0],
			((uint16_t*)pMCFG->BaseAddress)[1]
		);
		FullScreen.TextPrint({ 2, 12 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "CPUID Signature                  : %s", gstrCPUIDSig);
		FullScreen.TextPrint({ 2, 13 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "CPU Speed(measured)              : %s", gstrCPUSpeed);
		FullScreen.TextPrint({ 2, 14 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "CPU Speed(EFI_TIMESTAMP_PROTOCOL): %s", gstrTIMESTAMP_PROTOCOL);
		if (0 != strncmp(gstrTIMESTAMP_PROTOCOL, "N/A", strlen("N/A")))
			FullScreen.TextPrint({ 2, 15 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "    EFI_TIMESTAMP_PROTOCOL drift : %llds per day", gTIMESTAMP_PROTOCOLSecDriftPerDay);

		FullScreen.TextPrint({ 2, 17 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "target .XLSX                     : %s", gCfgStr_File_SaveAs);
		FullScreen.TextPrint({ 2, 18 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "RefTimerDev                      : %s", 2 == gfCfgSyncRef012 ? "PIT" : (1 == gfCfgSyncRef012 ? "RTC" : "ACPI"));
		FullScreen.TextPrint({ 2, 19 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "RefSyncTime                      : %ds", gnCfgRefSyncTime);
		FullScreen.TextPrint({ 2, 20 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Calibration Method               : %s", gCfgStr_CalibrMethod);
        FullScreen.TextPrint({ 2, 21 }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Error correction                 : %s", 0 == gfErrorCorrection ? "disabled" : (pfnDelay == &InternalAcpiDelay ? "N/A on TIANOCORE" : "enabled"));
		
		
		if (true == gfAutoRun)
			Sleep(3000);
		//
		// the master loop
		//
		if (1)
		{
			enum STATE {
				MENU_DFLT,
				MENU_ENTER_ACTIVATION,
				MENU_IS_ACTIVE,
				MENU_IS_OPEN,
				/*MENU_123_IS_ACTIVE,*/
			}state = MENU_DFLT;
			int idxMenu = 0, idxMnuItm = 0;
			int pgress = 0;

			for (; false == gfExit;)
			{

				FullScreen.TextWindowUpdateProgress();

				//
				// write STATUS BAR, if a status message exist in wcsStatusBar
				//
				if (0 != blink) {
					
					char* pLineKill = new char[FullScreen.ScrDim.X];

					memset(pLineKill, '\x20', FullScreen.ScrDim.X),
						pLineKill[FullScreen.ScrDim.X - 1] = '\0';

					if (true == gfStatusLineVisible) 
					{
						gfStatusLineVisible = false;
						FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, pLineKill);
						FullScreen.TextPrint({ 1, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, L"F10:Menu");
					}

					if (false == fStatusBarRightAdjusted)	// right adjust whatever is in the wcsStatusBar
					{
						wchar_t wcstmp[sizeof(wcsStatusBar)];
						wchar_t wcsFmt[32];

						wcscpy(wcstmp, wcsStatusBar);
						swprintf(wcsFmt, L"%%%ds", (int)ELC(wcsStatusBar) - 1);
						swprintf(wcsStatusBar, wcsFmt, wcstmp);						// do the right-justification!
						fStatusBarRightAdjusted = true;
					}

					if (0 == (blink-- & 3))
					{
						if (0 != wcscmp(wcsStatusBar, L""))
						{
							FullScreen.TextPrint({ FullScreen.ScrDim.X - (int32_t)ELC(wcsStatusBar), FullScreen.ScrDim.Y - 1 },
								EFI_BACKGROUND_BLUE | (blink & 4 ? gStatusStringColor : EFI_BLUE), wcsStatusBar);
						}
					}
				}
				else if (0 != wcscmp(wcsStatusBar, L""))
				{
					fStatusBarRightAdjusted = false;
					wmemset(wcsStatusBar, L'\x20', ELC(wcsStatusBar));																		// clear status bar message
					wcsStatusBar[-1 + ELC(wcsStatusBar)] = L'\0';																			// terminate string
					FullScreen.TextPrint({ FullScreen.ScrDim.X - (int32_t)sizeof(STATUSSTRING), FullScreen.ScrDim.Y - 1 },
						EFI_BACKGROUND_BLUE | EFI_WHITE, wcsStatusBar);																		// write cleared status bar message
					wcscpy(wcsStatusBar, L"");																								// cut status bar message to 0

					//
					// draw status/help line, initially
					//
					if (1)
					{
						wchar_t wcsARROW_LEFT[2] = { ARROW_LEFT ,'\0' },
							wcsARROW_UP[2] = { ARROW_UP ,'\0' },
							wcsARROW_RIGHT[2] = { ARROW_RIGHT ,'\0' },
							wcsARROW_DOWN[2] = { ARROW_DOWN ,'\0' };
						char* pLineKill = new char[FullScreen.ScrDim.X];

						memset(pLineKill, '\x20', FullScreen.ScrDim.X),
							pLineKill[FullScreen.ScrDim.X - 1] = '\0';

						if (false == gfStatusLineVisible)
						{
							gfStatusLineVisible = true;
							FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, pLineKill);
							FullScreen.TextPrint({ 1, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, L"F10:Menu \x25C4\x2518:Select SPACE:Check ESC:Return %s%s%s%s:Navigate", wcsARROW_LEFT, wcsARROW_RIGHT, wcsARROW_UP, wcsARROW_DOWN);
						}
					}
				}

				key = FullScreen.TextGetKey();

				switch (state) {
				case MENU_ENTER_ACTIVATION:
					//
					// clear main window since refresh for text block is not yet fully supported (for multiple text blocks, only for one...)
					//
					if (1) {
						wchar_t wcstmp[16];
						swprintf(wcstmp, INT_MAX, L"%%.%ds", FullScreen.WinDim.X - 2);

						for (int i = 2; i < FullScreen.WinDim.Y - 2; i++)
							FullScreen.TextPrint({ 1, i + FullScreen.WinPos.Y }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, wcstmp, FullScreen.pwcsWinClrLine);
					}

					//
					//  highlight the menu name
					//
					FullScreen.TextPrint(menu[idxMenu].RelPos, EFI_BACKGROUND_CYAN | EFI_YELLOW, menu[idxMenu].wcsMenuName);
					//
					// clear STATUS BAR by setting blink:=0
					//
					blink = 0;

					state = MENU_IS_ACTIVE;
					break;
				case MENU_IS_ACTIVE:
					if (KEY_ESC == key) {
						key = NO_KEY;
						state = MENU_DFLT;
						for (int i = 0; i < ELC(menu); i++)		// normalize the menu strings
							FullScreen.TextPrint(menu[i].RelPos, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, menu[i].wcsMenuName);
					}
					else if (KEY_LEFT == key) {
						FullScreen.TextPrint(menu[idxMenu].RelPos, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, menu[idxMenu].wcsMenuName);
						idxMenu = ((idxMenu - 1) < 0) ? ELC(menu) - 1 : idxMenu - 1;
						FullScreen.TextPrint(menu[idxMenu].RelPos, EFI_BACKGROUND_CYAN | EFI_YELLOW, menu[idxMenu].wcsMenuName);
						key = NO_KEY;
					}
					else if (KEY_RIGHT == key) {
						FullScreen.TextPrint(menu[idxMenu].RelPos, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, menu[idxMenu].wcsMenuName);
						idxMenu = ((idxMenu + 1) < ELC(menu)) ? idxMenu + 1 : 0;
						FullScreen.TextPrint(menu[idxMenu].RelPos, EFI_BACKGROUND_CYAN | EFI_YELLOW, menu[idxMenu].wcsMenuName);
						key = NO_KEY;
					}
					else if (KEY_ENTER == key) {
						menu[idxMenu].pTextWindow = new CTextWindow(&FullScreen, { menu[idxMenu].RelPos.X, 2 }, menu[idxMenu].MnuDim, EFI_BACKGROUND_CYAN | EFI_YELLOW);
						menu[idxMenu].pTextWindow->TextBorder({ 0, 0 }, menu[idxMenu].MnuDim,
							BOXDRAW_DOWN_RIGHT,
							BOXDRAW_DOWN_LEFT,
							BOXDRAW_UP_RIGHT,
							BOXDRAW_UP_LEFT,
							BOXDRAW_HORIZONTAL,
							BOXDRAW_VERTICAL,
							nullptr);

						//
						// fill menu with menuitem strings
						//
						menu[idxMenu].pTextWindow->pwcsBlockDrawBuf[0] = '\0';		// initially terminate the string list

						for (int i = 0; /* NOTE: check for NULL string to terminate the list */menu[idxMenu].rgwcsMenuItem[i]; i++)
						{
							wchar_t* wcsList = menu[idxMenu].pTextWindow->pwcsBlockDrawBuf;
							size_t x = wcslen(wcsList);								// always get end of strings

							swprintf(&wcsList[x], UINT_MAX, L"%s\n", menu[idxMenu].rgwcsMenuItem[i]);
						}
						menu[idxMenu].pTextWindow->TextBlockDraw({ 2, 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW);
						idxMnuItm = 0;
						menu[idxMenu].pTextWindow->TextPrint({ 2,1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[0]);
						//__debugbreak();

						state = MENU_IS_OPEN;
						key = NO_KEY;
					}
					break;
				case MENU_IS_OPEN:
					if (KEY_ESC == key) {
						menu[idxMenu].pTextWindow->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK; // set background attribute
						delete menu[idxMenu].pTextWindow;
						FullScreen.TextBlockRfrsh();
						state = MENU_IS_ACTIVE;
					}
					else if (KEY_ENTER == key)
					{
						int fRefrehMenu = 0;
						menu[idxMenu].pTextWindow->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK; // set background attribute
						fRefrehMenu = (*menu[idxMenu].rgfnMnuItm[idxMnuItm])(menu[idxMenu].pTextWindow, &menu[idxMenu]/*nullptr*/, (void*)"ENTER");
						
						if (fRefrehMenu)
						{
							//
							// redraw entire menu with refreshed string
							//
							menu[idxMenu].pTextWindow = new CTextWindow(&FullScreen, { menu[idxMenu].RelPos.X, 2 }, menu[idxMenu].MnuDim, EFI_BACKGROUND_CYAN | EFI_YELLOW);
							menu[idxMenu].pTextWindow->TextBorder({ 0, 0 }, menu[idxMenu].MnuDim,
								BOXDRAW_DOWN_RIGHT,
								BOXDRAW_DOWN_LEFT,
								BOXDRAW_UP_RIGHT,
								BOXDRAW_UP_LEFT,
								BOXDRAW_HORIZONTAL,
								BOXDRAW_VERTICAL,
								nullptr);

							//
							// fill menu with menuitem strings
							//
							menu[idxMenu].pTextWindow->pwcsBlockDrawBuf[0] = '\0';		// initially terminate the string list

							for (int i = 0; /* NOTE: check for NULL string to terminate the list */menu[idxMenu].rgwcsMenuItem[i]; i++)
							{
								wchar_t* wcsList = menu[idxMenu].pTextWindow->pwcsBlockDrawBuf;
								size_t x = wcslen(wcsList);								// always get end of strings

								swprintf(&wcsList[x], UINT_MAX, L"%s\n", menu[idxMenu].rgwcsMenuItem[i]);
							}
							menu[idxMenu].pTextWindow->TextBlockDraw({ 2, 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW);
							menu[idxMenu].pTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[idxMnuItm]);	//    highlight current  menu item
						}
						else {

							for (int i = 0; i < ELC(menu); i++)		// normalize the menu strings
								FullScreen.TextPrint(menu[i].RelPos, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, menu[i].wcsMenuName);
							state = MENU_DFLT;
						}
					}
					else if (KEY_SPACE == key)
					{
						int fRefrehMenu = 0;
						menu[idxMenu].pTextWindow->BgAtt = EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK; // set background attribute
						fRefrehMenu = (*menu[idxMenu].rgfnMnuItm[idxMnuItm])(menu[idxMenu].pTextWindow, &menu[idxMenu]/*nullptr*/, (void*)"SPACE");
						
						if (fRefrehMenu)
						{
							//
							// redraw entire menu with refreshed string
							//
							menu[idxMenu].pTextWindow = new CTextWindow(&FullScreen, { menu[idxMenu].RelPos.X, 2 }, menu[idxMenu].MnuDim, EFI_BACKGROUND_CYAN | EFI_YELLOW);
							menu[idxMenu].pTextWindow->TextBorder({ 0, 0 }, menu[idxMenu].MnuDim,
								BOXDRAW_DOWN_RIGHT,
								BOXDRAW_DOWN_LEFT,
								BOXDRAW_UP_RIGHT,
								BOXDRAW_UP_LEFT,
								BOXDRAW_HORIZONTAL,
								BOXDRAW_VERTICAL,
								nullptr);

							//
							// fill menu with menuitem strings
							//
							menu[idxMenu].pTextWindow->pwcsBlockDrawBuf[0] = '\0';		// initially terminate the string list

							for (int i = 0; /* NOTE: check for NULL string to terminate the list */menu[idxMenu].rgwcsMenuItem[i]; i++)
							{
								wchar_t* wcsList = menu[idxMenu].pTextWindow->pwcsBlockDrawBuf;
								size_t x = wcslen(wcsList);								// always get end of strings

								swprintf(&wcsList[x], UINT_MAX, L"%s\n", menu[idxMenu].rgwcsMenuItem[i]);
							}
							menu[idxMenu].pTextWindow->TextBlockDraw({ 2, 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW);
							menu[idxMenu].pTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[idxMnuItm]);	//    highlight current  menu item
						}


					}
					else if (KEY_DOWN == key) {
						int idxMnuItmNUM = menu[idxMenu].MnuDim.Y - 2/* number of lines within the pulldown menu */;

						menu[idxMenu].pTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[idxMnuItm]);	// de-highlight previous menu item
						do {
							idxMnuItm = (++idxMnuItm == idxMnuItmNUM ? 0 : idxMnuItm);
						} while (nullptr == menu[idxMenu].rgfnMnuItm[idxMnuItm]);	// skip separators
						menu[idxMenu].pTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[idxMnuItm]);	//    highlight current  menu item
					}
					else if (KEY_UP == key) {
						int idxMnuItmNUM = menu[idxMenu].MnuDim.Y - 2/* number of lines within the pulldown menu */;

						menu[idxMenu].pTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_CYAN | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[idxMnuItm]);	// de-highlight previous menu item
						do {
							idxMnuItm = (--idxMnuItm < 0 ? idxMnuItmNUM - 1 : idxMnuItm);
						} while (nullptr == menu[idxMenu].rgfnMnuItm[idxMnuItm]);	// skip separators
						menu[idxMenu].pTextWindow->TextPrint({ 2,idxMnuItm + 1 }, EFI_BACKGROUND_MAGENTA | EFI_YELLOW, menu[idxMenu].rgwcsMenuItem[idxMnuItm]);	//    highlight current  menu item
					}

					key = NO_KEY;
					break;
				case MENU_DFLT:
					if (KEY_F10 == key)
						state = MENU_ENTER_ACTIVATION,
						key = NO_KEY;
					while (0) {
						//AcpiClkWait(5 * 3579545);
						PITClkWait(3579545/3);
						gfHexView ^= true;
						if (true == gfHexView)
						{
							FullScreen.TextBlockDraw({ 5,13 }, EFI_BACKGROUND_BLUE | EFI_WHITE, "...");
						}
						else {
							FullScreen.TextBlockDraw({ 5,13 }, EFI_BACKGROUND_RED | EFI_WHITE, L"...");
						}
					}//while(1)
					
					if (gfRunConfig)
					{
						uint64_t seconds = 0;
						switch (gidxCfgMngMnuItm_Config_NumSamples)
						{
						case 0: cntSamples = 10; break;
						case 1: cntSamples = 50; break;
						case 2: cntSamples = 250; break;
						case 3: cntSamples = 1250; break;
						case 4: cntSamples = 62500; break;
						}

						//
						// clear main window if /AUTORUN, clear main window since refresh for text block is not yet fully supported (for multiple text blocks, only for one...)
						//
						if (1) {
							wchar_t wcstmp[16];
							swprintf(wcstmp, INT_MAX, L"%%.%ds", FullScreen.WinDim.X - 2);

							for (int i = 2; i < FullScreen.WinDim.Y - 2; i++)
								FullScreen.TextPrint({ 1, i + FullScreen.WinPos.Y }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, wcstmp, FullScreen.pwcsWinClrLine);
						}

						char* pLineKill = new char[FullScreen.ScrDim.X];

						memset(pLineKill, '\x20', FullScreen.ScrDim.X),
							pLineKill[FullScreen.ScrDim.X - 1] = '\0';

						//
						// approximate over all measurement time
						//
						if (1)
						{
							for (int i = 0, l = 0; i < ELC(parms); i++)
							{
								if (false == *parms[i].pEna)
									continue;
								seconds += ((uint64_t)parms[i].delay * (uint64_t)cntSamples) / 3579543;
							}
                            FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, pLineKill);
                            
                            //DEBUG FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, "%d seconds",seconds),
                            //DEBUG    __debugbreak();
                        }

						if (1)
						{
							clock_t endsec = seconds + clock() / CLOCKS_PER_SEC;
							bool fStop = false;

							for (int i = 0, l = 0; i < ELC(parms); i++)
							{
								char strbuftmp[128];
								if (false == *parms[i].pEna)
									continue;
								sprintf(strbuftmp, "running %d x %s calibration ... ", cntSamples, parms[i].szCalibrTime);
								FullScreen.TextBlockDraw({ 5,5 + 3 * l }, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "%s", strbuftmp);

								//
								// do the measurement
								//
								if (1)
								{
									uint64_t secondsold = 0;
									for (int j = 0; j < cntSamples; j++)
									{
                                        //
                                        // kgtest
                                        //
                                        //if (1) {
                                        //    memset(curprevdiff, 0, sizeof(curprevdiff));//kgtest
                                        //    iCPD = 0;//kgtest
                                        //}

										FullScreen.TextBlockDraw({ 2,2}, EFI_BACKGROUND_LIGHTGRAY | EFI_BLACK, "Additional ticks gone through: ");
										parms[i].rgDiffTSC[j] = pfnDelay(parms[i].delay);

                                        //
                                        // kgtest
                                        //
                                        //if (1) {
                                        //    char fname[16];

										seconds = clock() / CLOCKS_PER_SEC;

										if (secondsold == seconds)
											continue;
										secondsold = seconds;

										FullScreen.TextWindowUpdateProgress();

										if (false == fStop)
										{
											uint64_t h, m, s, tmp;
											
											h = (endsec - seconds) / 3600;
											tmp = (endsec - seconds) % 3600;
											m = tmp / 60;
											s = tmp % 60;
											
											FullScreen.TextPrint({ 1, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, "ATTENTION: Measurement finished in %02lld:%02lld min", m, s);
											if (0 == m && 0 == s)
												fStop = true;

										}
									}
								}

								//
								// scale each sample to entire day (86400 seconds)
								//
								if (1) {
									for (int j = 0; j < cntSamples; j++)
									{
										int64_t dblTSC = parms[i].rgDiffTSC[j];
										double dblScaled2EntireDay = ((double)((dblTSC * parms[i].qwMultiplierToOneSecond - gTSCPerSecondReference) * 86400)) / (double)gTSCPerSecondReference;

										parms[i].rgDriftSecPerDay[j] = (double)dblScaled2EntireDay;
									}
								}

								FullScreen.TextBlockDraw({ 5 + (int)strlen(strbuftmp),5 + 3 * l }, EFI_BACKGROUND_LIGHTGRAY | EFI_WHITE, "FINISHED");
								l++;
							}//for (int i = 0, l = 0; i < ELC(parms); i++)
						}

						if (1)
						{
							wchar_t wcsARROW_LEFT[2] = { ARROW_LEFT ,'\0' },
								wcsARROW_UP[2] = { ARROW_UP ,'\0' },
								wcsARROW_RIGHT[2] = { ARROW_RIGHT ,'\0' },
								wcsARROW_DOWN[2] = { ARROW_DOWN ,'\0' };
							char* pLineKill = new char[FullScreen.ScrDim.X];

							memset(pLineKill, '\x20', FullScreen.ScrDim.X),
								pLineKill[FullScreen.ScrDim.X - 1] = '\0';

							//if (false == gfStatusLineVisible)
							//{
							//	gfStatusLineVisible = true;
								FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, pLineKill);
								FullScreen.TextPrint({ 1, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_BLUE | EFI_WHITE, L"F10:Menu \x25C4\x2518:Select SPACE:Check ESC:Return %s%s%s%s:Navigate", wcsARROW_LEFT, wcsARROW_RIGHT, wcsARROW_UP, wcsARROW_DOWN);
							//}
						}

						gfRunConfig = false;
						
						if (true == gfAutoRun) 
						{
							char* pLineKill = new char[FullScreen.ScrDim.X];
							menu[idxMenu].pTextWindow = new CTextWindow(&FullScreen, {0,0}, {1,0}, EFI_BACKGROUND_CYAN | EFI_YELLOW);		//zero position, zero dimension (0,0 doesn't work!)

							memset(pLineKill, '\x20', FullScreen.ScrDim.X),
								pLineKill[FullScreen.ScrDim.X - 1] = '\0';

							FullScreen.TextPrint({ 0, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, pLineKill);
							FullScreen.TextPrint({ 1, FullScreen.ScrDim.Y - 1 }, EFI_BACKGROUND_RED | EFI_WHITE, "ATTENTION: Progress indicator stopped during I/O");

							(*menu[0/*idxMenu*/].rgfnMnuItm[0/*idxMnuItm*/])(menu[0/*idxMenu*/].pTextWindow, &menu[0/*idxMenu*/]/*nullptr*/, (void*)"AUTORUN");

							gfExit = true;
							break;
						}
					}

					break;
				default:break;
				}

				key = NO_KEY;
			}
		}

		delete pc;
		delete pi;
		DPRINTF(("...exit0"));

	} while (false == gfExit);

	//
	// save modified config, always modified or not
	//
	if (gfSaveExit)
	{
		FILE* fp = fopen("tscsync.cfg", "w");

		if (nullptr != fp)
		{
			fprintf(fp, "gfCfgMngMnuItm_View_Clock = %hhd\n\
				gfCfgMngMnuItm_View_Calendar = %hhd\n\
				gfCfgMngMnuItm_Config_PicApicSelect = %hhd\n\
				gfCfgMngMnuItm_Config_PITDelaySelect1  = %hhd\n\
				gfCfgMngMnuItm_Config_PITDelaySelect2  = %hhd\n\
				gfCfgMngMnuItm_Config_PITDelaySelect3  = %hhd\n\
				gfCfgMngMnuItm_Config_PITDelaySelect4  = %hhd\n\
				gfCfgMngMnuItm_Config_PITDelaySelect5  = %hhd\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect1 = %hhd\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect2 = %hhd\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect3 = %hhd\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect4 = %hhd\n\
				gfCfgMngMnuItm_Config_ACPIDelaySelect5 = %hhd\n\
				gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI = %hhd\n\
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT = %hhd\n\
				gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI = %hhd\n\
				gidxCfgMngMnuItm_Config_NumSamples = %d\n\
				gCfgStr_File_SaveAs = %s\n\
				gfErrorCorrection = %hhd\n",
				
				gfCfgMngMnuItm_View_Clock,
				gfCfgMngMnuItm_View_Calendar,
				gfCfgMngMnuItm_Config_PicApicSelect,
				gfCfgMngMnuItm_Config_PITDelaySelect1 ,
				gfCfgMngMnuItm_Config_PITDelaySelect2 ,
				gfCfgMngMnuItm_Config_PITDelaySelect3 ,
				gfCfgMngMnuItm_Config_PITDelaySelect4 ,
				gfCfgMngMnuItm_Config_PITDelaySelect5 ,
				gfCfgMngMnuItm_Config_ACPIDelaySelect1,
				gfCfgMngMnuItm_Config_ACPIDelaySelect2,
				gfCfgMngMnuItm_Config_ACPIDelaySelect3,
				gfCfgMngMnuItm_Config_ACPIDelaySelect4,
				gfCfgMngMnuItm_Config_ACPIDelaySelect5,
                gfCfgMngMnuItm_Config_CalibMethodSelectTIANOACPI,
                gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCPIT,
                gfCfgMngMnuItm_Config_CalibMethodSelectTSCSYNCACPI,

				gidxCfgMngMnuItm_Config_NumSamples,
				gCfgStr_File_SaveAs,
				gfErrorCorrection

			);
			fclose(fp);
			//system("attrib -a tscsync.cfg");
		}
	}

	DPRINTF(("...exit\n"));
	return nRet;
}