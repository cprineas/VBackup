//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#pragma once

#define CMD_NORMAL      0
#define CMD_ALL         1
#define CMD_HELP        2
#define CMD_ERR       255

#define ASSERT		assert


#define STRSTR		_tcsstr
#define STRCHR		_tcschr
#define STRCMP		_tcscmp
#define STRICMP		_tcsicmp
#define STRNCMP		_tcsnccmp
#define STRNICMP	_tcsncicmp
#define STRLEN		(int)_tcsclen
#define STRRCHR		_tcsrchr
#define STRLWR		_tcslwr
#define STRUPR		_tcsupr
#define STRCPY		_tcscpy
#define STRNCPY		_tcsnccpy
#define STRCAT		_tcscat
#define STRNCAT		_tcsncat
#define ATOI		_tstoi
#define PRINTF		_tprintf
#define PUTS		_putts
#define SPRINTF		_stprintf
#define SPRINTF_S	_stprintf_s
#define STRPBRK     _tcspbrk
#define CTIME_S		_tctime_s
#define STRFTIME	_tcsftime


