# Visual-TSCSync-for-UEFI-Shell
TSCSync - TimeStampCounter (TSC) synchronizer,  analyze System Timer characteristics

![TSCSyncStart](TSCSyncStart.png)

![TSCSyncAbout](TSCSyncAbout.png)

![TSCSyncMenu](TSCSyncMenu.png)

![XLSXSheet1](XLSXSheet1.png)

![XLSXSheet1](XLSXSheet2.png)

![XLSXSheet1](XLSXSheet3.png)

## Intention
Explore timer characteristics on current UEFI Personal Computer (PC) hardware.

Analyse usability and accuracy of 64Bit TSC (time stamp counter) as
a time base in UEFI BIOS POST and UEFI SHELL Applications.

## Goal
Provide program sequence, synchronisation method and basic 
knowledge on how to deal with the fastes, and most precise timer
on x86-Microprocessors.

## Approach
Create an UEFI Shell Tool **TSCSync** that makes it easy to select, modify
and scale data processing, logging, representation for this particular laboratory
application.

## Howto
### Menu driven configuration
Just watch the video: https://www.youtube.com/watch?v=I92emFEyTDI

### Binary
[TSCSync.EFI](x64/EFIApp/TSCSync.efi)

### AUTORUN configuration
**NOTE:** **/AUTORUN** mode usually runs a predefined and saved configuration.
This mode was made available to enable repeated .NSH/batch controlled
**TSCSync** invocation with modified settings, like
* calibration **/METHOD**
	* **TIANO**, original *tianocore* `InternalAcpiDelay()`
	* **ACPI**, native **TSCSYNC** ACPI counter
	* **PIT**, native **TSCSYNC** PIT i8254 counter
* output filename **/OUT**
* modified reference synchronisation time **/SYNCTIME** 1..1000
* modified reference synchronisation device **/SYNCREF**
	* **RTC**
	* **ACPI**

Just watch the video: https://www.youtube.com/watch?v=hjeykqZqekc&t=27s

## Revision history
### 20240103, v1.2.1 Build 4
* fix rounding issue for X.YZ9999999GHz values
* provide .EFI executable [TSCSync.EFI](x64/EFIApp/TSCSync.efi)
* internal: update submodules
### 20240101, v1.2.0 Build 2
* add TSC clock speed rounding (experimental), get identical result like **CPUID leaf 0x15**-enabled platforms
* simplify command line parameters -> remove selection of SYNCREF (synchronization reference device), SYNCTIME (synchronization time)
	* force SYNCREF ACPI timer, force SYNCTIME 5 seconds
* add version + build enumeration (experimental) 
### 20231210
* add retrieval of [**Time Stamp Counter and Core Crystal Clock Information Leaf**](https://www.intel.com/content/dam/develop/external/us/en/documents/architecture-instruction-set-extensions-programming-reference.pdf#page=34)
	* NOTE: On platforms with available **CPUID leaf 0x15** (Intel CPU 2017 and later) it demonstrates, that ACPI reference synchronisation is very accurate (about 0.1ppm)
* add retrieval of **MSR_PLATFORM_INFO** TSC Speed detection
* add reference [spreadsheet](https://github.com/KilianKegel/Visual-TSCSync-for-UEFI-Shell/blob/main/RTL.xlsx) taken on RAPTOR LAKE platform
NOTE: Improvements below apply only to recent INTEL(tm) platforms only. The particular CPUID leaf 0x15 and MSR **MSR_PLATFORM_INFO** are not available on AMD systems.
### 20231202
* add retrieval, examination and comparison of original **UEFI** **`EFI_TIMESTAMP_PROTOCOL`**<br>
  NOTE: **`EFI_TIMESTAMP_PROTOCOL`** provides inaccurate results on most systems.
### 20231119
* add error correction menu `CONF\Error Correction` and command line (`/ERRCODIS`) selectable
	* NOTE: Error correction N/A for original TIANOCORE (TIANO) measurement method<br>
	  Only available for **TSCSYNC** ACPI and PIT.
* add calibration method menu `CONF\Calibration Method TIANO/PIT/ACPI` and command line (`/METHOD`)  selectable
* add number of measurement command line (`/NUM:0/1/2/3`)  selectable
* update to TORO C Library 20231118
### 20231105
* improvement, *BETA RELEASE*
* add **Calibration Method** for *PIT(i8254)* and *original tianocore* to CONFig menu<br>
  NOTE: the third method "ACPI" (that is my own calibration based on ACPI timer) can be selected via command line only
### 20231028
* improvement, *BETA RELEASE*
* add **/METHOD:TIANO/ACPI/PIT** to select ACPI/PIT(i8254) or original *tianocore* calibration method
* CONFIG Menu shows true timing values, instead of ACPI clock numbers
### 20231015
* initial revision, *ALPHA RELEASE*
