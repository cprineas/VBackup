//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

//************************************************************************************************
// Date			Version		Change
//
// 12 NOV 2010	0.6			Fixed problem where error message was displayed - even when using /RP:
//							GetBackupSet did not handle y/n keys! We now call GetYesNo.
// 22 DEC 2010  0.7			/RP: called SetFlag instead of SetRestore (which meant we had to also include /r to do a restore)
//************************************************************************************************

#include "stdafx.h"
#include <windows.h>
#include <assert.h>
#include <time.h>
#include <conio.h>
#include "Tools.h"
#include "VFilePath.h"
#include "FileMatcher.h"
#include "StringArray.h"
#include "Macros.h"

#define PROGRAM_NAME		_T("VBackup")
#define PROGRAM_URL			_T("www.fileviewer.com/vbackup")

#define UNDERLINE			_T("------------------------------------------------------------------\n")
#define VERSIONS_FOLDER		_T(".VBackups")
#define RECENT_FILENAME		_T("~.recent")

#define	FLAG_INCLUDEFILES		0x0001
#define FLAG_EXCLUDEFILES		0x0002
#define FLAG_INCLUDEFOLDERS		0x0004
#define FLAG_EXCLUDEFOLDERS		0x0008
#define FLAG_CONTINUEONERROR	0x0010
#define FLAG_APPENDLOG			0x0020
#define	FLAG_INCLUDEVERSION		0x0040
#define FLAG_EXCLUDEVERSION		0x0080
#define FLAG_LOGSKIPPED			0x0100
#define FLAG_RESTOREPROMPT		0x0200
#define FLAG_RESTOREALL			0x0400

#define SetFlag(x)	(m_dwFlags |= (x))
#define IsFlag(x)	((m_dwFlags & (x)) ? TRUE : FALSE)

CStringArray* m_pFilesArray     = NULL;
CStringArray  m_FilesInclude;
CStringArray  m_FilesExclude;
CStringArray  m_FoldersInclude;
CStringArray  m_FoldersExclude;
CStringArray  m_VersionInclude;
CStringArray  m_VersionExclude;
CVFilePath    m_vfSrc;
CVFilePath    m_vfDst;
CVFilePath    m_vfVersion;
time_t        m_tStart;
    

_TCHAR m_szDateTime[32];
LPTSTR m_pszBackupName = NULL;
LPTSTR m_pszBackupDate = NULL;
LPTSTR m_pszVersion    = NULL;

BOOL m_fVersion = FALSE;

// Command line settings
LPTSTR  m_pszLog        = NULL;
HANDLE  m_hLog          = NULL;
DWORD   m_dwFlags       = 0;
DWORD64 m_dw64Max       = 0;
DWORD64 m_dw64MaxV      = 0;
BOOL    m_fNoSubFolders = FALSE;
BOOL    m_fListOnly     = FALSE;
BOOL    m_fRestore      = FALSE;
BOOL    m_fQuiet        = FALSE;
BOOL    m_fUnicode      = FALSE;
BOOL    m_fMaxSize      = FALSE;
BOOL    m_fMaxSizeV     = FALSE;
BOOL    m_fNoBackupSet  = FALSE;
BOOL    m_fLogIdentical = FALSE;

int     m_nCopied       = 0;
int     m_nVersions     = 0;
int     m_nIdentical    = 0;
int     m_nFilesSkipped = 0;
int     m_nDirsSkipped  = 0;
int     m_nCopyErrors   = 0;
int     m_nErrors       = 0;
int     m_nWarnings     = 0;
int     m_nReparse      = 0;

LPTSTR ProgramVersion(LPTSTR pszFilePath=NULL)
{
	if (m_pszVersion && !pszFilePath)
		return m_pszVersion;

	if (m_pszVersion)
	{
		delete [] m_pszVersion;
		m_pszVersion = NULL;
	}

	TCHAR szPath[MAX_PATH];

    if (pszFilePath)
        STRCPY(szPath, pszFilePath);

    if (pszFilePath || GetModuleFileName(NULL, szPath, _countof(szPath)))
	{
		DWORD dwVerHnd;
		DWORD dwVerInfoSize = GetFileVersionInfoSize(szPath, &dwVerHnd);

		if (dwVerInfoSize)
		{
			HANDLE  hMem;
			LPVOID  lpvMem;

			// Allocate version storage and retrieve version info

			hMem = GlobalAlloc(GMEM_MOVEABLE, dwVerInfoSize);
			lpvMem = GlobalLock(hMem);
			GetFileVersionInfo(szPath, dwVerHnd, dwVerInfoSize, lpvMem);

			LPTSTR pszVersion=NULL;
            BOOL   fQuery;
			UINT   uLen;

		    fQuery = VerQueryValue(lpvMem, TEXT("\\StringFileInfo\\040904B0\\ProductVersion"), (LPVOID *)&pszVersion, &uLen);

			if (fQuery)
			{
				if (pszVersion && uLen)
				{
#ifdef _WIN64
					CVFilePath vfVersion(pszVersion);
	                vfVersion.Append(_T(" (x64)"));
					m_pszVersion = NewString(vfVersion.Path());
#else
					m_pszVersion = NewString(pszVersion);
#endif
				}
			}

			GlobalUnlock(hMem);
			GlobalFree(hMem);
		}
    }

	if (m_pszVersion == NULL)
		m_pszVersion = NewString(_T("Unknown Version"));

	return m_pszVersion;
}

LPTSTR ErrorString(int nError)
{
	static TCHAR szMessage[512];

	DWORD dwStat = FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM,
							NULL,
							nError,
							MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
							szMessage,
							512, NULL);

	if (dwStat == 0)
		SPRINTF_S(szMessage, 512, TEXT("Error %d"), nError);
	else
	{
		// Remove any terminating new line characters
		LPTSTR psz = &szMessage[dwStat-1];

		while ((psz > szMessage) && (*psz < 14))
			*psz-- = 0;
	}

	return szMessage;
}

int ErrorNL(int nError)
{
	PRINTF(_T("\n\n"));

	return nError;
}

int ContinueOnError(int nError, BOOL fAllowAll=TRUE)
{
	if (!nError)
		return 0;

	while (1)
	{
		int ch = _getch();
		ch = toupper(ch);

		if (ch == 'Y')
			return ErrorNL(0);

		if (ch == 'N')
			return ErrorNL(nError);

		if (fAllowAll && (ch == 'A'))
		{
			SetFlag(FLAG_CONTINUEONERROR);
			return ErrorNL(0);
		}

		MessageBeep(MB_OK);
	}

	ASSERT(FALSE);
	return 0;
}

int WriteLog(LPTSTR pszText)
{
	int   nError = 0;
	DWORD dw;

	if (m_hLog)
	{
		if (m_fUnicode)
		{
			if (!::WriteFile(m_hLog, pszText, (DWORD)STRLEN(pszText)*sizeof(TCHAR), &dw, NULL))
				nError = GetLastError();
		}
		else
		{
			LPSTR pszA = StringA(pszText);
			
			if (!::WriteFile(m_hLog, pszA, (DWORD)strlen(pszA), &dw, NULL))
				nError = GetLastError();

			delete [] pszA;
		}
	}

	if (nError)
	{
		PRINTF(_T("Failed to write to log file: %s\n\nDo you want to continue with logging disabled (y/n)? "), ErrorString(nError));

		// Logging failed - close the log file and exit

		::CloseHandle(m_hLog);
		m_hLog = NULL;

		if (ContinueOnError(nError, FALSE))
			exit(3);
	}

	return 0;
}

int LogNL()
{
	return WriteLog(_T("\n"));
}

int Log(TCHAR *fmt, ...)
{
	if (m_hLog == NULL)
		return 0;

    TCHAR szText[1024];

    va_list args;
    va_start(args, fmt);

    vswprintf_s(szText, 1024, fmt, args);
    va_end(args);

	return WriteLog(szText);
}

int Progress(LPTSTR pszLog, TCHAR *fmt, ...)
{
	if (m_fQuiet && !m_hLog)
		return 0;

    TCHAR szText[1024];

    va_list args;
    va_start(args, fmt);

    vswprintf_s(szText, 1024, fmt, args);
    va_end(args);

	if (!m_fQuiet)
		PUTS(szText);		// Note that PUTS includes a terminating new line character

	if (m_hLog)
	{
		if (pszLog)
			WriteLog(pszLog);

		WriteLog(szText);
		LogNL();
	}

	return 0;
}

int PrintError(int nError, TCHAR *fmt, ...)
{
    TCHAR szText[1024];

    va_list args;
    va_start(args, fmt);

    vswprintf_s(szText, 1024, fmt, args);
    va_end(args);

	if (nError)
		m_nErrors++;
	else
		m_nWarnings++;

	if (nError)
		PRINTF(_T("Error: %s (%s)\n"), szText, ErrorString(nError));
	else
		PRINTF(_T("Warning: %s\n"), szText);

	if (m_hLog)
	{
		if (nError)
			Log(_T("    **Error: %s (%s)\n"), szText, ErrorString(nError));
		else
			Log(_T("  **Warning: %s\n"), szText);
	}

	if (!nError || IsFlag(FLAG_CONTINUEONERROR))
		return 0;

	PRINTF(_T("Continue with backup (y/n/a)? "));

	return ContinueOnError(nError);
}

int WriteBOM(HANDLE hFile, BOOL fUTF8)
{
	DWORD dw;
	BYTE  szBOM[3];
	int   nBOM = 2;

	if (fUTF8)
	{
		szBOM[0] = 0xef;
		szBOM[1] = 0xbb;
		szBOM[2] = 0xbf;
		nBOM++;
	}
	else
	{
		szBOM[0] = 0xff;
		szBOM[1] = 0xfe;
	}
	
	if (!::WriteFile(hFile, szBOM, nBOM, &dw, NULL))
		return GetLastError();

	if (fUTF8 && !::WriteFile(hFile, ": DO NOT EDIT THIS FILE\n", 24, &dw, NULL))
		return GetLastError();

	return 0;
}

void FormatFileTime(LPTSTR pszDateTime, FILETIME& ft)
{
	// Do not use FileTimeToLocalFileTime to convert ft!
	// Doing this will mean that the file "name" will agree with the time stamp of the file, however, should Daylight Savings change, they will no longer agree!

	SYSTEMTIME st;

	if (FileTimeToSystemTime(&ft, &st))
		SPRINTF_S(pszDateTime, 32, _T("~%d%02d%02d-%02d%02d%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	else
		STRCPY(pszDateTime, m_szDateTime);
}

BOOL Match(LPTSTR pszName, CStringArray& FileSpecArray)
{
	LPTSTR pszMask = FileSpecArray.GetFirst();

	while (pszMask)
	{
		if (CFileMatcher::FileNameMatch(pszMask, pszName) == FNM_MATCH)
			return TRUE;

		pszMask = FileSpecArray.GetNext();
	}

	return FALSE;
}

BOOL IncludeFile(WIN32_FIND_DATA* pfd, DWORD64 dw64MaxSize)
{
	// Returns if the file size if greater than dw64MaxSize

	DWORD64 dw64Size = 0;
	
	if (pfd->nFileSizeHigh)
	{
		dw64Size   = (DWORD64)pfd->nFileSizeHigh;
		dw64Size <<= 32;
	}

	dw64Size += pfd->nFileSizeLow;

	return (dw64Size > dw64MaxSize) ? FALSE : TRUE;
}

BOOL IncludeFolder(LPTSTR pszName)
{
	// Returns whether we should include pszName in the copy
	// The "include" specification takes precedence over "exclude"

	BOOL fCopy = TRUE;

	if (IsFlag(FLAG_INCLUDEFOLDERS))
		fCopy = Match(pszName, m_FoldersInclude);
	else
		
	if (IsFlag(FLAG_EXCLUDEFOLDERS))
		fCopy = !Match(pszName, m_FoldersExclude);

	return fCopy;
}

BOOL IncludeFile(LPTSTR pszName)
{
	// Returns whether we should include pszName in the copy
	// The "include" specification takes precedence over "exclude"

	BOOL fCopy = TRUE;

	if (IsFlag(FLAG_INCLUDEFILES))
		fCopy = Match(pszName, m_FilesInclude);
	else
		
	if (IsFlag(FLAG_EXCLUDEFILES))
		fCopy = !Match(pszName, m_FilesExclude);

	return fCopy;
}

BOOL IncludeVersion(LPTSTR pszName)
{
	// Returns whether we should make a version backup of pszName
	// The "include" specification takes precedence over "exclude"

	BOOL fCopy = TRUE;

	if (IsFlag(FLAG_INCLUDEVERSION))
		fCopy = Match(pszName, m_VersionInclude);
	else
		
	if (IsFlag(FLAG_EXCLUDEVERSION))
		fCopy = !Match(pszName, m_VersionExclude);

	return fCopy;
}

BOOL IsSubFolder(LPTSTR pszRoot, LPTSTR pszFolder)
{
	int nLen = STRLEN(pszRoot);

	if (STRNICMP(pszRoot, pszFolder, nLen))
		return FALSE;

	if (pszRoot[nLen-1] == '\\')
		return (pszFolder[nLen-1] == '\\') ? TRUE : FALSE;

	return (pszFolder[nLen] == '\\') ? TRUE : FALSE;
}

BOOL FilesAreDifferent(LPTSTR pszSrc, LPTSTR pszDst, WIN32_FIND_DATA* pfdSrc, WIN32_FIND_DATA* pfdDst, BOOL& fDstExists)
{
	HANDLE h = FindFirstFileV(pszDst, pfdDst);

	if (h == INVALID_HANDLE_VALUE)
	{
		// Assume that the destination doesn't exist
		fDstExists = FALSE;
		return TRUE;
	}
		
	fDstExists = TRUE;

	if (!memcmp(&pfdDst->ftLastWriteTime, &pfdSrc->ftLastWriteTime, sizeof(FILETIME)) && (pfdDst->nFileSizeLow == pfdSrc->nFileSizeLow))
	{
		// Time stamp and file size (< 4GB) are the same - so we'll assume the file hasn't changed since the last backup
		return FALSE;
	}

	return TRUE;
}

int GetVersionFolder(LPTSTR pszPath, CVFilePath* pvfPath)
{
	int nError = 0;

	if (!m_fVersion)
	{
		m_vfVersion.SetPath((pszPath) ? pszPath : pvfPath->Path());
		m_vfVersion.SetFileName(VERSIONS_FOLDER, (pszPath) ? FALSE : TRUE);

		if (!m_fListOnly && !m_fRestore && !PathExists(m_vfVersion.Path()))
		{
			if (!(nError = CreateDirectoryRecursive(m_vfVersion.Path())))
			{
				// Let's hide the versions folder
				DWORD dw = GetFileAttributesV(m_vfVersion.Path());

				if (dw == 0xffffffff)
					nError = GetLastError();
				else
				{
					// Set the HIDDEN attribute - but don't abort if we get an error!
					GetFileAttributesV(m_vfVersion.Path(), dw |= FILE_ATTRIBUTE_HIDDEN);
				}
			}
		}

		m_vfVersion.AppendSlash();
		m_fVersion = TRUE;
	}

	return nError;
}

int DeleteRetry(LPTSTR pszPath)
{
	// Clear read-only attrib before trying to delete

	if (DeleteFileV(pszPath))
		return 0;

	int nError = GetLastError();

	if (GetFileAttributesV(pszPath, 0) == 0xffffffff)
		return nError;

	return (DeleteFileV(pszPath)) ? 0 : GetLastError();
}

int CreateRecent(CVFilePath& vfBackup)
{
	// vfBackup contains the file path of the backup set just created - also create a ~recent file containing the backup set name
		
	LPTSTR pszBackupSet = NewString(vfBackup.FileName());

	vfBackup.SetFileName(RECENT_FILENAME);

	int    nError = 0;
	HANDLE h      = CreateFileV(vfBackup.Path(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (h == INVALID_HANDLE_VALUE)
	{
		nError = DeleteRetry(vfBackup.Path());
	
		if (!nError)
		{
			h = CreateFileV(vfBackup.Path(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			
			if (h == INVALID_HANDLE_VALUE)
				nError = GetLastError();
		}
	}

	if (h != INVALID_HANDLE_VALUE)
	{
		DWORD dw;
		WriteFile(h, pszBackupSet, STRLEN(pszBackupSet)*sizeof(TCHAR), &dw, NULL);	// Don't bother checking for error

		CloseHandle(h);
	}

	delete [] pszBackupSet;

	return nError;		// Don't print error for this
}

int CreateBackupSet(HANDLE& hBackup, LPTSTR pszDst, BOOL& fError)
{
	int nError = GetVersionFolder(pszDst, NULL);

	if (nError)
	{
		fError = TRUE;

		return PrintError(nError, _T("Could not create backup directory %s"), m_vfVersion.Path());
	}

	// Create the backup set
	CVFilePath vfBackup(m_vfVersion.Path());

	vfBackup.AppendFileName(m_szDateTime);

	if (m_pszBackupName)
	{
		vfBackup.Append(_T("-"));
		vfBackup.Append(m_pszBackupName);
	}

	hBackup = CreateFileV(vfBackup.Path(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hBackup == INVALID_HANDLE_VALUE)
		nError = GetLastError();
	else
		nError = WriteBOM(hBackup, TRUE);

	if (nError)
	{
		if (hBackup != INVALID_HANDLE_VALUE)
			::CloseHandle(hBackup);

		fError         = TRUE;
		hBackup        = NULL;
		m_fNoBackupSet = TRUE;
		
		return PrintError(nError, _T("Could not create backup set %s"), vfBackup.Path());
	}

	CreateRecent(vfBackup);

	return 0;
}

int LogBackup(HANDLE hBackup, WIN32_FIND_DATA* pfd)
{
	int nError = 0;

	// Write out the Attributes + Filesize + Date/Time followed by the file name

	CHAR sz[64];

	if (pfd->nFileSizeHigh)
	{
		DWORD dw64Size = (DWORD64)pfd->nFileSizeHigh;
		
		dw64Size <<= 32;
		dw64Size  += pfd->nFileSizeLow;

		sprintf(sz, "%X %X %X %I64X ", pfd->dwFileAttributes, pfd->ftLastWriteTime.dwHighDateTime, pfd->ftLastWriteTime.dwLowDateTime, dw64Size);
	}
	else
		sprintf(sz, "%X %X %X %X ", pfd->dwFileAttributes, pfd->ftLastWriteTime.dwHighDateTime, pfd->ftLastWriteTime.dwLowDateTime, pfd->nFileSizeLow);

	DWORD dw;

	if (!::WriteFile(hBackup, sz, (DWORD)strlen(sz), &dw, NULL))
		nError = GetLastError();
	else
	{
		LPSTR pszA = StringA(pfd->cFileName, -1, CP_UTF8);

		ASSERT(pszA);

		if (pszA && !::WriteFile(hBackup, pszA, (DWORD)strlen(pszA), &dw, NULL))
			nError = GetLastError();

		delete [] pszA;
	}

	if (!nError && !::WriteFile(hBackup, "\n", 1, &dw, NULL))
		nError = GetLastError();

	if (nError)
	{
		::CloseHandle(hBackup);

		hBackup        = NULL;
		m_fNoBackupSet = TRUE;

		return PrintError(nError, _T("Could not write to backup set"));
	}

	return 0;
}

int MoveVersion(CVFilePath& vfPath, BOOL& fError, BOOL& fExists, WIN32_FIND_DATA* pfdSrc, WIN32_FIND_DATA* pfdDst)
{
	// Make a version backup of vfPath - as long as we are not excluding it

	ASSERT(fExists);
	ASSERT(pfdSrc);
	ASSERT(pfdDst);

	if (IsFlag(FLAG_INCLUDEVERSION|FLAG_EXCLUDEVERSION) && !IncludeVersion(vfPath.FileName()))
		return 0;

	if (m_fMaxSizeV && pfdSrc && !IncludeFile(pfdSrc, m_dw64MaxV))
		return 0;

	int nStat = GetVersionFolder(NULL, &vfPath);

	if (nStat)
	{
		fError = TRUE;
		return PrintError(nStat, _T("Could not create version directory %s"), m_vfVersion.Path());
	}

	// Create a *folder* under .versions that has the same name as the file name - and create the folder if it doesn't exist
	CVFilePath vfNewVersion(m_vfVersion.Path());

	vfNewVersion.AppendFileName(vfPath.FileName());
		
	if (!m_fListOnly && !PathExists(vfNewVersion.Path()))
	{
		nStat = CreateDirectoryRecursive(vfNewVersion.Path());
	
		if (nStat)
		{
			fError = TRUE;
			return PrintError(nStat, _T("Could not create version directory %s"), vfNewVersion.Path());
		}
	}

	TCHAR szDateTime[32];

	FormatFileTime(szDateTime, pfdDst->ftLastWriteTime);

	vfNewVersion.AppendFileName(szDateTime);

	if (!m_fListOnly && !MoveFileV(vfPath.Path(), vfNewVersion.Path()))
	{
		nStat = GetLastError();

		if (nStat == ERROR_ALREADY_EXISTS)
		{
			// The backup already exists - Let's print a warning!
			ASSERT(FALSE);
			PrintError(0, _T("Backup of %s not made as %s already exists"), vfPath.Path(), vfNewVersion.Path());
			return 0;
		}
	}
	
	if (nStat)
	{
		fError = TRUE;
		return PrintError(nStat, _T("Could not make backup version %s -> %s"), vfPath.Path(), vfNewVersion.Path());
	}

	fExists = FALSE;	// We have moved the file - so it no longer exists
	m_nVersions++;
	Progress(NULL, _T("Made backup: %s -> %s"), vfPath.Path(), vfNewVersion.Path());

	return 0;
}

int VCopyFile(CVFilePath& vfSrc, CVFilePath& vfDst, WIN32_FIND_DATA* pfdSrc)
{
	// Copies pszSrc to pszDst - as long as they are different

	BOOL            fDstExists;
	WIN32_FIND_DATA fdDst;

	if (!FilesAreDifferent(vfSrc.Path(), vfDst.Path(), pfdSrc, &fdDst, fDstExists))
	{
		if (m_fLogIdentical)
			Log(_T("  Unchanged: %s\n"), vfSrc.Path());

		m_nIdentical++;
		return 0;
	}

	int  nError = 0;

	if (fDstExists)
	{
		BOOL fError = FALSE;

		nError = MoveVersion(vfDst, fError, fDstExists, pfdSrc, &fdDst);

		// Note that MoveVersion will print its own error message
		if (fError)
			return nError;
	}

	if (m_fListOnly)
	{
		m_nCopied++;
		return Progress(_T("     Copied: "), _T("%s -> %s"), vfSrc.Path(), vfDst.Path());
	}

	if (fDstExists && (fdDst.dwFileAttributes & (FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)))
	{
		// If we haven't moved the file, change the attributes if the File Copy will fail because of them

		fdDst.dwFileAttributes &= ~(FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM);
		GetFileAttributesV(vfDst.Path(), fdDst.dwFileAttributes);
	}

	if (CopyFileV(vfSrc.Path(), vfDst.Path(), FALSE))
	{
		Progress(_T("     Copied: "), _T("%s -> %s"), vfSrc.Path(), vfDst.Path());
		m_nCopied++;
		return 0;
	}
	
	m_nCopyErrors++;

	return PrintError(GetLastError(), _T("Copy failed %s -> %s"), vfSrc.Path(), vfDst.Path());
}

int VCopyDir(LPTSTR pszSrc, LPTSTR pszDst)
{
	CVFilePath vfSrc(pszSrc);
	CVFilePath vfDst(pszDst);

	WIN32_FIND_DATA fd;

	HANDLE hDir = FindFirstFileV(vfSrc.AppendFileName(_T("*")), &fd);
	
	if (hDir == INVALID_HANDLE_VALUE)
		return PrintError(GetLastError(), _T("Could not open directory %s"), pszSrc);

	int nError = 0;

	if (!m_fListOnly && !PathExists(pszDst))
	{
		nError = CreateDirectoryRecursive(pszDst, pszSrc);

		if (nError)
			return PrintError(nError, _T("Could not create destination directory %s"), pszDst);

		Progress(NULL, _T("Created Dir: %s"), pszDst);
	}

	m_fVersion = FALSE;

	HANDLE hBackup = NULL;
	BOOL   fError  = FALSE;

	if (!m_fListOnly && !m_fNoBackupSet)
		nError = CreateBackupSet(hBackup, pszDst, fError);

	if (fError)
		return nError;

	do
	{
		BOOL fCopy = FALSE;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!m_fNoSubFolders)
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
				{
					CVFilePath vfReparse(pszSrc, fd.cFileName);
					
					LPTSTR pszReparse = vfReparse.ReparseToPath();

					if (IsSubFolder(m_vfSrc.Path(), pszReparse))
					{
						PrintError(0, _T("Ignoring mount/reparse point: %s\\%s. The path it points to (%s) is in the backup tree and will be backed up"), pszSrc, fd.cFileName, pszReparse);
					}
					else
					{
						m_nReparse++;
						PrintError(0, _T("%s\\%s is a mount/reparse point and will not be backed up"), pszSrc, fd.cFileName);
					}
				}
				else

				if (!STRCMP(VERSIONS_FOLDER, fd.cFileName))
				{
					if (!m_fRestore)
						PrintError(0, _T("Ignoring versions folder in source tree: %s\\%s"), pszSrc, fd.cFileName);
				}
				else

				if (STRCMP(_T(".."), fd.cFileName) && STRCMP(_T("."), fd.cFileName))
				{
					if (IsFlag(FLAG_INCLUDEFOLDERS|FLAG_EXCLUDEFOLDERS))
						fCopy = IncludeFolder(fd.cFileName);
					else
						fCopy = TRUE;

					if (fCopy)
					{
						vfSrc.SetFileName(fd.cFileName);
						vfDst.SetFileName(fd.cFileName);

						if (hBackup && (nError=LogBackup(hBackup, &fd)))
							break;

						nError = VCopyDir(vfSrc.Path(), vfDst.Path());

						m_fVersion = FALSE;
					}
					else
					{
						m_nDirsSkipped++;
					
						if (IsFlag(FLAG_LOGSKIPPED))
							Log(_T("    Skipped: %s\\%s\n"), pszSrc, fd.cFileName);
					}
				}
			}
		}
		else
		{
			if (IsFlag(FLAG_INCLUDEFILES|FLAG_EXCLUDEFILES))
				fCopy = IncludeFile(fd.cFileName);
			else
				fCopy = TRUE;

			if (fCopy && m_fMaxSize)
				fCopy = IncludeFile(&fd, m_dw64Max);

			if (fCopy)
			{
				vfSrc.SetFileName(fd.cFileName);
				vfDst.SetFileName(fd.cFileName);

				if (hBackup && (nError=LogBackup(hBackup, &fd)))
					break;

				nError = VCopyFile(vfSrc, vfDst, &fd);
			}
			else
			{
				m_nFilesSkipped++;

				if (IsFlag(FLAG_LOGSKIPPED))
					Log(_T("    Skipped: %s\\%s\n"), pszSrc, fd.cFileName);
			}
		}
	}
	while (!nError && ::FindNextFile(hDir, &fd));

    FindClose(hDir);

	if (hBackup)
		::CloseHandle(hBackup);

	if (nError || m_fListOnly)
		return nError;	// Don't print an error here as it should already have been printed in CVopyDir/VCopyFile

	return 0;
}

int ReadBOM(HANDLE hBackup)
{
	// The file should begin with a UTF8 BOM (3 chars) - don't bother checking it.

	BYTE  sz[3];
	DWORD dw;

	if (ReadFile(hBackup, &sz, 3, &dw, NULL))
		return 0;

	return GetLastError();
}

int IgnoreLine(HANDLE hBackup)
{
	// Read until an EOL
	DWORD dwRead;
	BYTE  b;
	
	while (1)
	{
		if (!ReadFile(hBackup, &b, 1, &dwRead, NULL))
			return GetLastError();

		if (dwRead == 0)
			return ERROR_HANDLE_EOF;

		if (b == 10)
			break;
	}

	return ERROR_EMPTY;
}

int GetDWORD(HANDLE hBackup, DWORD& dwLo, DWORD* pdwHi=NULL)
{
	DWORD64 dw64 = 0;

	dwLo = 0;

	// Read the file one byte at a time until we get a non digit (should be a space)

	DWORD dwRead;
	BYTE  b;
	
	while (1)
	{
		if (!ReadFile(hBackup, &b, 1, &dwRead, NULL))
			return GetLastError();

		if (dwRead == 0)
			return ERROR_HANDLE_EOF;

		if ((b >= '0') && (b <= '9'))
			dw64 = (dw64 * 16) + b - '0';
		else

		if ((b >= 'A') && (b <= 'F'))
			dw64 = (dw64 * 16) + b - 'A' + 10;
		else

		if ((b >= 'a') && (b <= 'f'))
			dw64 = (dw64 * 16) + b - 'a' + 10;
		else
		
		if (b == ':')
			return IgnoreLine(hBackup);		// We should really check that the ":" is the first char in the line!
		else
		{
			ASSERT(b == ' ');
			break;
		}
	}

	if (pdwHi)
		*pdwHi = (DWORD)(dw64 >> 32);

	dwLo = (DWORD)dw64;

	return 0;
}

int ReadFileName(HANDLE hBackup, LPTSTR pszFileName, BOOL& fEOF)
{
	// Now read the file name into pszFileName (which has size MAX_PATH)
	// We will read it into a buffer of MAX_PATH*3 as it is UTF8 encoded - and then convert to unicode

	CHAR  szFileNameA[MAX_PATH*3];
	DWORD dwRead;
	CHAR  b;
	int   nLen = 0;
	CHAR* psz  = szFileNameA;

	while (1)
	{
		if (!ReadFile(hBackup, &b, 1, &dwRead, NULL))
			return GetLastError();

		if (dwRead == 0)
		{
			fEOF = TRUE;
			break;
		}

		if (b == 10)
		{
			*psz = 0;
			break;
		}

		if (nLen >= (MAX_PATH*3))
		{
			ASSERT(FALSE);
			return ERROR_BAD_LENGTH;
		}

		*psz++ = b;
		nLen++;
	}

	if (!nLen && fEOF)
		return ERROR_HANDLE_EOF;

	// Now convert the UTF8 buffer to Unicode
	int nSize = MultiByteToWideChar(CP_UTF8, 0, szFileNameA, nLen+1, pszFileName, MAX_PATH);		// Include the nul terminator in length to be converted

	if (nSize <= 0)
		return ERROR_INVALID_DATA;

	return 0;
}

BOOL ReadRecentName(HANDLE hRecent, CVFilePath& vfRecent)
{
	TCHAR  szFileName[MAX_PATH+1];
	DWORD  dwRead;
	TCHAR  ch;
	int    nLen = 0;
	TCHAR* psz  = szFileName;

	while (1)
	{
		if (!ReadFile(hRecent, &ch, sizeof(TCHAR), &dwRead, NULL))
			break;

		if (dwRead != sizeof(TCHAR))
			break;

		*psz++ = ch;

		if (nLen++ >= MAX_PATH)
			break;
	}

	szFileName[nLen] = 0;

	// Let's see if the backup set pointed to by .0recent exists

	vfRecent.SetFileName(szFileName);

	if (!PathExists(vfRecent.Path()))
		return FALSE;

	ASSERT(m_pszBackupName == NULL);

	m_pszBackupName = NewString(szFileName);

	return TRUE;
}

int GetNextBackupFile(HANDLE hBackup, WIN32_FIND_DATA* pfd, BOOL& fEOF)
{
	// Lines should be in following format (all numbers in hex):
	//
	// Attributes HighDate LowDate Size FileName

	int nError = GetDWORD(hBackup, pfd->dwFileAttributes);

	if (!nError)
		nError = GetDWORD(hBackup, pfd->ftLastWriteTime.dwHighDateTime);

	if (!nError)
		nError = GetDWORD(hBackup, pfd->ftLastWriteTime.dwLowDateTime);

	if (!nError)
		nError = GetDWORD(hBackup, pfd->nFileSizeLow, &pfd->nFileSizeHigh);

	if (!nError)
		nError = ReadFileName(hBackup, pfd->cFileName, fEOF);

	return nError;
}

BOOL SameVersion(WIN32_FIND_DATA* pfd1, WIN32_FIND_DATA* pfd2)
{
	return (memcmp(&pfd1->ftLastWriteTime, &pfd2->ftLastWriteTime, sizeof(FILETIME)) || (pfd1->nFileSizeHigh != pfd2->nFileSizeHigh) || (pfd1->nFileSizeLow != pfd2->nFileSizeLow)) ? FALSE : TRUE;
}

LPTSTR FindVersion(LPTSTR pszSrc, WIN32_FIND_DATA* pfd, CVFilePath& vfNewVersion, int& nError)
{
	vfNewVersion.MakePath(pszSrc, VERSIONS_FOLDER);

	vfNewVersion.AppendFileName(pfd->cFileName);

	WIN32_FIND_DATA fdVersion;
	TCHAR           szVersion[32];

	// The file name should contain the time stamp - so there shouldn't be the need to search for it
	FormatFileTime(szVersion, pfd->ftLastWriteTime);

	vfNewVersion.AppendFileName(szVersion);

	nError = GetFileDetails(vfNewVersion.Path(), &fdVersion);

	if (!nError && SameVersion(pfd, &fdVersion))
		return vfNewVersion.Path();
		
	ASSERT(FALSE);	// We shouldn't come here!

	// We haven't found the file - let's look through the entire version directory to see if we can find one
	PrintError(0, _T("File not in version history - trying to locate it: %s"), vfNewVersion.Path());

	nError = ERROR_FILE_NOT_FOUND;

	HANDLE hDir = FindFirstFileV(vfNewVersion.SetFileName(_T("*")), &fdVersion);

	if (hDir == INVALID_HANDLE_VALUE)
		nError = GetLastError();
	else
	{
		do
		{
			if (!(fdVersion.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				if (SameVersion(pfd, &fdVersion))
				{
					// Found it (I hope!)
					vfNewVersion.SetFileName(fdVersion.cFileName);
					nError = 0;
					break;
				}
			}
		}
		while (::FindNextFile(hDir, &fdVersion));

		FindClose(hDir);
	}

	return (nError) ? NULL : vfNewVersion.Path();
}

int RestoreFile(LPTSTR pszSrc, LPTSTR pszDst, WIN32_FIND_DATA* pfd)
{
	// We need to restore pfd->cFileName from pszSrc to pszDst
	// Firstly, we need to determine if the file the latest file in pszSrc or if we need to copy it from a backup version
	//
	// Note that ony the attributes, time, size and filename of pfd are valid (whice were read from the backup set)

	WIN32_FIND_DATA fdSrc;
	CVFilePath      vfSrc(pszSrc, pfd->cFileName);
	CVFilePath      vfNewVersion;
	LPTSTR          pszNewVersion = vfSrc.Path();

	int nError = GetFileDetails(pszNewVersion, &fdSrc);

	// If we fail, we will assume that the file does not exist - and we need to search the version history
	
	if (!nError)
	{
		// The file exists - but is it the one we want?

		if (!SameVersion(&fdSrc, pfd))
			nError = ERROR_FILE_NOT_FOUND;	// It's not the one we want
	}

	if (nError)
	{
		// Look for the correct file in the version history
		pszNewVersion = FindVersion(pszSrc, pfd, vfNewVersion, nError);
	}

	if (nError)
		return PrintError(nError, _T("The file cannot be found in the backup directory: %s\\%s"), pszSrc, pfd->cFileName);

	// Finally, let's copy the file. Since we always restore to an empty directory we shouldn't have problems with overwriting existing files

	CVFilePath vfDst(pszDst, pfd->cFileName);

	if (m_fListOnly)
	{
		m_nCopied++;
		return Progress(_T("   Restored: "), _T("%s -> %s"), pszNewVersion, vfDst.Path());
	}

	if (CopyFileV(pszNewVersion, vfDst.Path(), FALSE))
	{
		Progress(_T("   Restored: "), _T("%s -> %s"), pszNewVersion, vfDst.Path());
		m_nCopied++;
		return 0;
	}

	m_nCopyErrors++;

	return PrintError(GetLastError(), _T("Restore failed %s -> %s"), vfSrc.Path(), vfDst.Path());
}

int RestoreBackupSet(LPTSTR pszSrc, LPTSTR pszDst)
{
	int nError = GetVersionFolder(pszSrc, NULL);

	m_fVersion = FALSE;

	if (nError)
		return PrintError(nError, _T("Backup directory not found in %s"),pszSrc);

	// Create the destination directory if it doesn't exist
	if (!m_fListOnly && !PathExists(pszDst))
	{
		nError = CreateDirectoryRecursive(pszDst, pszSrc);

		if (nError)
			return PrintError(nError, _T("Could not create destination directory %s"), pszDst);

		Progress(NULL, _T("Created Dir: %s"), pszDst);
	}

	// Open the backup set and read the files to restore

	CVFilePath vfBackup(m_vfVersion.Path(), m_pszBackupName);

	HANDLE hBackup = CreateFileV(vfBackup.Path(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hBackup == INVALID_HANDLE_VALUE)
		return PrintError(GetLastError(), _T("Could not open Backup Set %s"), vfBackup.Path());

	// Now let's read the file contents and restore the files
	
	nError = ReadBOM(hBackup);

	BOOL fEOF = FALSE;

	while (!nError && !fEOF)
	{
		BOOL            fRestore = FALSE;
		WIN32_FIND_DATA fdRestore;

		nError = GetNextBackupFile(hBackup, &fdRestore, fEOF);

		if (nError == ERROR_HANDLE_EOF)
		{
			nError = 0;
			break;
		}

		if (nError == ERROR_EMPTY)
		{
			nError = 0;
			continue;
		}

		if (nError)
		{
			nError = PrintError(nError, _T("Error in reading backup set %s (all files can not be restored)"), vfBackup.Path());
			break;	// Do not proceed with this backup set
		}

		if (fdRestore.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!m_fNoSubFolders)
			{
				// We got a directory - should we restore it?
				if (IsFlag(FLAG_INCLUDEFOLDERS|FLAG_EXCLUDEFOLDERS))
					fRestore = IncludeFolder(fdRestore.cFileName);
				else
					fRestore = TRUE;

				if (fRestore)
				{
					CVFilePath vfSrc(pszSrc, fdRestore.cFileName);
					CVFilePath vfDst(pszDst, fdRestore.cFileName);

					nError = RestoreBackupSet(vfSrc.Path(), vfDst.Path());
					
					m_fVersion = FALSE;		// I am not sure if we need to do this here - but it doesn't hurt!
				}
				else
				{
					m_nDirsSkipped++;

					if (IsFlag(FLAG_LOGSKIPPED))
						Log(_T("    Skipped: %s\\%s\n"), pszSrc, fdRestore.cFileName);
				}
			}
		}
		else
		{
			// Should we restore the file?
			if (IsFlag(FLAG_INCLUDEFILES|FLAG_EXCLUDEFILES))
				fRestore = IncludeFile(fdRestore.cFileName);
			else
				fRestore = TRUE;

			if (fRestore && m_fMaxSize)
				fRestore = IncludeFile(&fdRestore, m_dw64Max);

			if (fRestore)
			{
				nError = RestoreFile(pszSrc, pszDst, &fdRestore);
			}
			else
			{
				m_nFilesSkipped++;

				if (IsFlag(FLAG_LOGSKIPPED))
					Log(_T("    Skipped: %s\\%s\n"), pszSrc, fdRestore.cFileName);
			}
		}
	}

	CloseHandle(hBackup);

	return nError;
}

int Init()
{
	int nError = 0;

    _tzset();
	time(&m_tStart);

	struct tm* today = localtime(&m_tStart);
	size_t s = (today) ? STRFTIME(m_szDateTime, 32, _T("~%Y%m%d-%H%M%S"), today) : 0;

	if (!s)
	{
		PRINTF(_T("Unexpected error: cannot get current time!\n"));
		return 3;
	}

	if (m_pszLog)
	{
		BOOL fNew = (!IsFlag(FLAG_APPENDLOG) || !PathExists(m_pszLog));

		m_hLog = CreateFileV(m_pszLog, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, (IsFlag(FLAG_APPENDLOG)) ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (m_hLog == INVALID_HANDLE_VALUE)
		{
			nError = GetLastError();
			m_hLog = NULL;

			PRINTF(_T("Could not create log file: %s (%s)\n"), m_pszLog, ErrorString(nError));
		}
		else
		{
			if (fNew)
			{
				if (m_fUnicode)
					WriteBOM(m_hLog, FALSE);
			}
			else

			if (IsFlag(FLAG_APPENDLOG))
			{
				SetFilePointer(m_hLog, 0, NULL, FILE_END);
				LogNL();
			}
		}
	}

	if (m_hLog)
	{
		WriteLog(UNDERLINE);

		TCHAR  sz[256];
		struct tm* today = localtime(&m_tStart);

		size_t s = (today) ? STRFTIME(sz, 256, _T("VBackup Started : %a %b %d %Y %H:%M:%S"), today) : 0;

		if (s)
		{
			Log(sz);
			Log(_T("   [%s]\n"), ProgramVersion());
		}

		Log(_T("   Command Line : "));
		WriteLog(GetCommandLine());
		LogNL();

		if (m_fListOnly)
			Log(_T("                  LIST ONLY MODE\n"));

		WriteLog(UNDERLINE);
		LogNL();
	}

	return nError;
}

void Exit()
{
	if (m_hLog)
	{
		TCHAR  sz[256];
		time_t tNow;

		time(&tNow);
		struct tm* today = localtime(&tNow);

		DWORD dwS = (DWORD)difftime(tNow, m_tStart);

		LogNL();
		WriteLog(UNDERLINE);

		size_t s = (today) ? STRFTIME(sz, 256, _T("  VBackup Ended : %a %b %d %Y %H:%M:%S"), today) : 0;

		if (s)
		{
			Log(sz);
			LogNL();

			DWORD dwH;
			DWORD dwM;

			dwH  = dwS / 3600;;
			dwS -= (dwH * 3600);
			dwM  = dwS / 60;
			dwS -= (dwM * 60);

			if (dwH)
				Log(_T("  Time Taken    : %d:%02d:%02d\n"), dwH, dwM, dwS);
			else
			{
				Log(_T("  Time Taken    : "));

				if (dwM)
					Log(_T("%dm "), dwM);

				Log(_T("%ds\n"), dwS);
			}

			LogNL();
		}
		
		if (m_fRestore)
		{
			Log(_T("  Files Restored:              : %7d\n"), m_nCopied);
		}
		else
		{
			Log(_T("  Files Copied                 : %7d\n"), m_nCopied);
			Log(_T("  Files Not Copied (unchanged) : %7d\n"), m_nIdentical);
		}

		if (m_nFilesSkipped)
			Log(_T("  Files Skipped                : %7d\n"), m_nFilesSkipped);

		if (m_nDirsSkipped)
			Log(_T("  Directories Skipped          : %7d\n"), m_nDirsSkipped);

		if (!m_fRestore)
			Log(_T("  Number of Version Backups    : %7d\n"), m_nVersions);

		if (m_nReparse)
			Log(_T("  Ignored Mount/Reparse Points : %7d\n"), m_nReparse);

		if (m_nWarnings)
			Log(_T("  Number of Warnings           : %7d\n"), m_nWarnings);

		if (m_nErrors)
		{
			if (m_fRestore)
				Log(_T("  Number of Restore Errors     : %7d\n"), m_nCopyErrors);
			else
				Log(_T("  Number of Copy Errors        : %7d\n"), m_nCopyErrors);
		}

		Log(_T("  Total Number of Errors       : %7d\n"), m_nErrors);

		WriteLog(UNDERLINE);

		CloseHandle(m_hLog);
	}

	if (m_nErrors)
		printf("\nTotal Number of Errors: %d\n", m_nErrors);

	if (m_pszBackupName)
		delete [] m_pszBackupName;
	
	if (m_pszBackupDate)
		delete [] m_pszBackupDate;
	
	if (m_pszVersion)
		delete [] m_pszVersion;
}

BOOL HasVersions(LPTSTR pszPath)
{
	CVFilePath vfPath(pszPath, VERSIONS_FOLDER);

	return PathExists(vfPath.Path());
}

int VCopy()
{
	// Added check for FLAG_RESTOREPROMPT in 0.6 after I couldn't restore an old version of V using 0.5! (I got this error message)
	if (!IsFlag(FLAG_RESTOREPROMPT) && HasVersions(m_vfSrc.Path()))
	{
		printf("You cannot use VBackup to back up a backup directory\nIf you want to restore from the backup directory, use the /R or /RP option\n\n");
		printf("Type VBackup -? for command help\n\n");
		return 2;
	}

	int nError = Init();

	if (!nError)
		nError = VCopyDir(m_vfSrc.Path(), m_vfDst.Path());

	Exit();

	return (nError) ? 2 : 0;
}

int GetYesNo()
{
	while (1)
	{
		int ch = _getch();
		ch = toupper(ch);

		if (ch == 'Y')
			return ErrorNL(1);

		if ((ch == 'N') || (ch == 27))
			break;

		MessageBeep(MB_OK);
	}

	return ErrorNL(0);
}

int GetBackupSet(int nMax)
{
	int nDigits = 0;
	int nBackup = 0;

	while (1)
	{
		int ch = _getch();

		if (ch == 8)
		{
			if (nDigits)
			{
				_putch(8);
				_putch(' ');
				_putch(8);
				nBackup /= 10;
				nDigits--;
			}
		}
		else

		if ((ch >= '0') && (ch <= '9'))
		{
			int n = (nBackup * 10) + ch - '0';

			if (n > nMax)
				MessageBeep(MB_OK);
			else
			{
				nBackup = n;
				_putch(ch);
				nDigits++;
			}
		}
		else

		if (ch == 27)
			return 0;
		else

		if (ch == 13)
			break;
		else

		if (ch == 0)		// Function key pressed - next char is the key
			_getch();
		else
			MessageBeep(MB_OK);
	}
	
	PUTS(_T("\n\n"));
	return nBackup;
}

void OutputBackupSet(int nFile, LPTSTR psz)
{
	// Format YYYY/MM/DD HH:MM:SS

	if (STRLEN(psz) < 16)
		PRINTF(_T("%3d) %s\n"), nFile, psz);
	else
	{
		PRINTF(_T("%3d) "), nFile);

		LPTSTR pszName = &psz[16];

		if (*pszName == '-')
			pszName++;

		PRINTF(_T("%c%c%c%c/%c%c/%c%c %c%c:%c%c:%c%c"), psz[1], psz[2], psz[3], psz[4], psz[5], psz[6], psz[7], psz[8], psz[10], psz[11], psz[12], psz[13], psz[14], psz[15]);
		
		if (*pszName)
			PRINTF(_T(": %s\n"), pszName);
		else
			PRINTF(_T("\n"));
	}
}

BOOL ValidBackupSet(WIN32_FIND_DATA* pfd)
{
	// Make sure that pfd is a valid backup set
	// ~\?\?\?\?\?\?\?\?-\?\?\?\?\?\?
	//
	// For the moment, we will assume the name is OK (pfd->cFileName) and only check the file size

	return (!pfd->nFileSizeHigh && ((pfd->nFileSizeLow/sizeof(TCHAR)) <= MAX_PATH)) ? TRUE : FALSE;
}

BOOL SelectRecentBackup(LPTSTR pszPath)
{
	ASSERT(m_pszBackupName == NULL);

	// Return the backup set of the most recent backup
	int nError = GetVersionFolder(pszPath, NULL);

	m_fVersion = FALSE;

	if (nError)
	{
		PRINTF(_T("Could not find any backup sets in %s - consider using /RA"), m_vfVersion.Path());
		return FALSE;
	}

	CVFilePath vfVersion(m_vfVersion.Path(), RECENT_FILENAME);
	
	HANDLE hRecent = CreateFileV(vfVersion.Path(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hRecent != INVALID_HANDLE_VALUE)
	{
		ReadRecentName(hRecent, vfVersion);
		CloseHandle(hRecent);
	}

	if (m_pszBackupName)
		return TRUE;

	// We couldn't read the ~.recent file, so we will go through *all* of the backup sets and return the most recent!

	vfVersion.SetFileName(_T("~\?\?\?\?\?\?\?\?-\?\?\?\?\?\?*"));

	TCHAR           szRecent[MAX_PATH+1];
	FILETIME        ftRecent;
	BOOL            fRecent = FALSE;
	WIN32_FIND_DATA fd;
	HANDLE          hDir   = FindFirstFileV(vfVersion.Path(), &fd);
	
	if (hDir != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ValidBackupSet(&fd))
			{
				if (!fRecent)
				{
					STRNCPY(szRecent, fd.cFileName, MAX_PATH);
					ftRecent = fd.ftLastWriteTime;
					fRecent  = TRUE;
				}
				else

				if (CompareFileTime(&fd.ftLastWriteTime, &ftRecent) > 0)
				{
					STRNCPY(szRecent, fd.cFileName, MAX_PATH);
					ftRecent = fd.ftLastWriteTime;
				}
			}
		}
		while (::FindNextFile(hDir, &fd));
	}

    FindClose(hDir);

	if (!fRecent)
	{
		PRINTF(_T("Could not find any backup sets in %s - consider using /RA"), pszPath);
		return FALSE;
	}

	m_pszBackupName = NewString(szRecent);

	return TRUE;
}

BOOL SelectBackupSet(LPTSTR pszPath)
{
	int nError = GetVersionFolder(pszPath, NULL);

	m_fVersion = FALSE;

	if (nError)
	{
		PRINTF(_T("Could not find any backup sets in %s - consider using /RA"), m_vfVersion.Path());
		return FALSE;
	}

	CVFilePath vfVersion;
	
	if (m_pszBackupDate)
	{
		vfVersion.MakePath(m_vfVersion.Path(), m_pszBackupDate);
		vfVersion.Append(_T("-\?\?\?\?\?\?"));
	}
	else
		vfVersion.MakePath(m_vfVersion.Path(), _T("~\?\?\?\?\?\?\?\?-\?\?\?\?\?\?"));	// Yikes - we seem to need the sloshes otherwise the correct string is not passed!

	if (m_pszBackupName)
	{
		vfVersion.Append(_T("-"));
		vfVersion.Append(m_pszBackupName);
	}
	else
		vfVersion.Append(_T("*"));

	// Let's try and find some backup sets - and store them in a CStringArray
	CStringArray    strFiles;
	WIN32_FIND_DATA fd;

	int    nFiles = 0;
	HANDLE hDir   = FindFirstFileV(vfVersion.Path(), &fd);
	
	if (hDir != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				strFiles.Add(fd.cFileName);
				nFiles++;
			}
		}
		while (::FindNextFile(hDir, &fd));
	}

    FindClose(hDir);

	if (nFiles == 0)
	{
		PRINTF(_T("Could not find any matching backup sets in %s"), m_vfVersion.Path());
		return FALSE;
	}

	if (nFiles > 50)
	{
		PRINTF(_T("Too many backup sets found - please specify a backup date or a backup set name"));
		return FALSE;
	}

	strFiles.Sort();

	int    nFile = 1;
	LPTSTR psz   = strFiles.GetFirst();

	PRINTF(_T("\n"));

	while (psz)
	{
		OutputBackupSet(nFile, psz);
		psz = strFiles.GetNext();
		nFile++;
	}

	if (nFiles == 1)
		PRINTF(_T("\nRestore this backup set (y/n)? "));
	else
		PRINTF(_T("\nEnter which backup set to restore (1-%d, ESC to cancel)? "), nFiles);

	nFile = (nFiles == 1) ? GetYesNo() : GetBackupSet(nFiles);

	if (nFile == 0)
		return FALSE;

	// Replace m_pszBackupName (if it exists) with the actual name of the backup set

	if (m_pszBackupName)
		delete [] m_pszBackupName;

	m_pszBackupName = NewString(strFiles.GetString(nFile-1));

	ASSERT(m_pszBackupName);

	return (m_pszBackupName) ? TRUE : FALSE;
}

int VRestore()
{
	if (!IsEmpty(m_vfDst.Path()))
	{
		PRINTF(_T("VBackup will only restore to a non-existent or empty directory\n"));
		return 3;
	}

	BOOL fBackupSet = FALSE;

	// If we are not doing a "restore all" determine which backup set to restore

	if (!IsFlag(FLAG_RESTOREALL))
	{
		if (IsFlag(FLAG_RESTOREPROMPT) || m_pszBackupDate || m_pszBackupName)
			fBackupSet = SelectBackupSet(m_vfSrc.Path());
		else
			fBackupSet = SelectRecentBackup(m_vfSrc.Path());

		if (!fBackupSet)
			return FALSE;
	}

	m_fNoBackupSet = TRUE;		// Don't create a backup set for a restore

	int nError = Init();

	if (!nError)
	{
		if (fBackupSet)
		{
			Progress(_T("  Restoring: "), _T("Backup Set %s"), m_pszBackupName);

			nError = RestoreBackupSet(m_vfSrc.Path(), m_vfDst.Path());
		}
		else
			nError = VCopyDir(m_vfSrc.Path(), m_vfDst.Path());
	}

	Exit();

	return (nError) ? 2 : 0;
}

int Help()
//********
{
	PRINTF(_T("%s %s (Freeware)\n"), PROGRAM_NAME, ProgramVersion());
    printf("Copyright (c) 2010-2015 Charles Prineas. All Rights Reserved.\n\n");
	PRINTF(_T("Usage:   %s Source Destination [options]\n\n"), PROGRAM_NAME);
	printf("           Source   The directory to back up\n");
	printf("      Destination   The directory where Source will be backed up to\n");
	printf("::\n");
	printf(":: Options:\n");
	printf("::\n");
	printf("               /?   Display this help screen\n");
	printf("               /C   Continue with backup if an error occurs\n");
	printf("               /R   Restore the latest backup\n");
	printf("              /RP   Restore previous backup (you will be prompted with available backups)\n");
	printf("              /RA   Restore all files in the backup\n");
	printf("     /RP:YYYYMMDD   Restore backup made on YYYY/MM/DD\n");
	printf("               /L   List Only - do not backup any files\n");
	printf("               /Q   Quiet mode - suppress output\n");
	printf("              /NS   Do not backup subdirectories\n");
	printf("             /LSK   Log skipped files\n");
	printf("             /LUN   Log unchanged files\n");
	printf("             /NBS   Do not create a backup set\n");
	printf("\n");
	printf("  /IF file [file]   Only backup files matching name/mask\n");
	printf("  /XF file [file]   Exclude files matching name/mask\n");
	printf("    /ID dir [dir]   Only backup directories matching name/mask\n");
	printf("    /XD dir [dir]   Exclude directories matching name/mask\n");
	printf("\n");
	printf(" /IFV file [file]   Only make version backup of files matching name/mask\n");
	printf(" /XFV file [file]   Do not make version backup of files matching name/mask\n");
	printf("\n");
	printf("        /MAX:size   Do not back up files greater than size\n");
	printf("       /MAXV:size   Do not make version backups of files greater than size\n");
	printf("\n");
	printf("       /NAME:name   Give the backup a meaningful name (which can be used when restoring)\n");
	printf("        /LOG:file   Output to log file (overwrite existing log)\n");
	printf("       /LOG+:file   Output to log file (append to existing log)\n");
	printf("     /UNILOG:file   Output to Unicode log file (overwrite existing log)\n");
	printf("    /UNILOG+:file   Output to Unicode log file (append to existing log)\n");
	printf("::\n");
	printf(":: Examples:\n");
	printf("::\n");
	printf("       VBackup C:\\Source D:\\Backups\\Source /XF *.tmp *.bak /XD Release Debug /NAME:\"Version 11\"\n");
	printf("       VBackup /R D:\\Backups\\Source C:\\Source.Restore\n");
	printf("       VBackup /R /NAME:\"Version 11\" D:\\Backups\\Source \"C:\\Source.Version 11\"\n");
	printf("::\n");
	printf(":: Notes:\n");
	printf("::\n");
	printf("       Use quotes around names if they contain spaces\n");
	printf("       Only ONE source directory can be specified\n");
	printf("       File names (or masks) must be passed using /IF\n");
	printf("       You can only restore to an empty (or non-existent) directory\n");
	printf("::\n");
	printf(":: Contact:\n");
	printf("::\n");
	printf("       http://www.fileviewer.com/vbackup\n");
	printf("\n");

	return 1;
}

/* Possible future comand line options:

	/A		Only backup if Archive bit set (keep archive)
	/M		Only backup if archive bit set (clear archive)
	/R		Restore (should only be able to restore in EMPTY or non-existent dir)
	/NR		Don't overwrite Read-Only
	/NS		No subdirectories
	/F		Full mode (display full paths in log)?
	/O		Copy ownership + ACL?
	/U		Update (only copy files that exist)
	/IA:xx	Only backup specified attributes
	/XA:xx	Exclude specified attributes
*/

BOOL ParseSize(LPTSTR pszSize, DWORD64& dw64Size, BOOL& fSize)
{
	TCHAR ch;

	dw64Size = 0;

	while (ch = *pszSize++)
	{
		if ((ch >= '0') && (ch <= '9'))
		{
			dw64Size *= 10;
			dw64Size += ch - '0';
		}
		else
		{
			switch (ch)
			{
				case 'k':
				case 'K':
					dw64Size *= 1024;
					break;

				case 'm':
				case 'M':
					dw64Size *= 1024*1024;
					break;

				case 'g':
				case 'G':
					dw64Size *= 1024*1024*1024;
					break;

				default:
					PRINTF(_T("Illegal character in size: can only use 0-9 or KMG\n"));
					return CMD_ERR;
			}
		}
	}

	return (fSize = TRUE);
}

void SetRestore(DWORD dwFlag)
{
	m_fRestore = TRUE;

	if (dwFlag)
	{
		SetFlag(dwFlag);
	}
}

int Parse(int argc, _TCHAR* argv[])
//*********************************
{
	LPTSTR* ppszString = NULL;
    int     nFiles     = 0;

    for (int i=1; i<argc; i++)
    {
        if ((argv [i] [0] == '/') ||
            (argv [i] [0] == '-'))
        {
			ppszString = NULL;

            if (!STRNICMP (&argv [i] [1], _T("?"), 1))
                return CMD_HELP;

            if (!STRICMP (&argv [i] [1], _T("IF")))
			{
				SetFlag(FLAG_INCLUDEFILES);
				m_pFilesArray = &m_FilesInclude;
			}
			else

            if (!STRICMP (&argv [i] [1], _T("XF")))
			{
				SetFlag(FLAG_EXCLUDEFILES);
				m_pFilesArray = &m_FilesExclude;
			}
			else
            
            if (!STRICMP (&argv [i] [1], _T("ID")))
			{
				SetFlag(FLAG_INCLUDEFOLDERS);
				m_pFilesArray = &m_FoldersInclude;
			}
			else

            if (!STRICMP (&argv [i] [1], _T("XD")))
			{
				SetFlag(FLAG_EXCLUDEFOLDERS);
				m_pFilesArray = &m_FoldersExclude;
			}
			else

            if (!STRICMP (&argv [i] [1], _T("IFV")))
			{
				SetFlag(FLAG_INCLUDEVERSION);
				m_pFilesArray = &m_VersionInclude;
			}
			else

            if (!STRICMP (&argv [i] [1], _T("XFV")))
			{
				SetFlag(FLAG_EXCLUDEVERSION);
				m_pFilesArray = &m_VersionExclude;
			}
			else

			if (!STRICMP (&argv [i] [1], _T("L")))
				m_fListOnly = TRUE;
			else

			if (!STRICMP (&argv [i] [1], _T("R")))
				SetRestore(0);
			else

			if (!STRNICMP (&argv [i] [1], _T("RP:"), 3))
			{
				SetRestore(FLAG_RESTOREPROMPT);		// Changed SetFlag to SetRestore in version 0.7

				if (m_pszBackupDate)
					delete [] m_pszBackupDate;

				m_pszBackupDate = ConcatString(_T("~"), &argv[i][4]);
			}
			else

			if (!STRICMP (&argv [i] [1], _T("RP")))
				SetRestore(FLAG_RESTOREPROMPT);
			else

			if (!STRICMP (&argv [i] [1], _T("RA")))
				SetRestore(FLAG_RESTOREALL);
			else

			if (!STRICMP (&argv [i] [1], _T("C")))
				SetFlag(FLAG_CONTINUEONERROR);
			else

			if (!STRICMP (&argv [i] [1], _T("Q")))
				m_fQuiet = TRUE;
			else

			if (!STRICMP (&argv [i] [1], _T("LSK")))
				SetFlag(FLAG_LOGSKIPPED);
			else

			if (!STRICMP (&argv [i] [1], _T("LUN")))
				m_fLogIdentical = TRUE;
			else

			if (!STRICMP (&argv [i] [1], _T("NBS")))
				m_fNoBackupSet = TRUE;
			else

			if (!STRICMP (&argv [i] [1], _T("NS")))
				m_fNoSubFolders = TRUE;
			else

			if (!STRNICMP (&argv [i] [1], _T("LOG:"), 4))
			{
				m_pszLog = &argv[i][5];
			}
			else
			
			if (!STRICMP (&argv [i] [1], _T("LOG")))
				ppszString = &m_pszLog;
			else

			if (!STRNICMP (&argv [i] [1], _T("LOG+:"), 5))
			{
				m_pszLog = &argv[i][6];
				SetFlag(FLAG_APPENDLOG);
			}
			else

			if (!STRICMP (&argv [i] [1], _T("LOG+")))
			{
				SetFlag(FLAG_APPENDLOG);
				ppszString = &m_pszLog;
			}
			else

			if (!STRNICMP (&argv [i] [1], _T("UNILOG:"), 7))
			{
				m_pszLog   = &argv[i][8];
				m_fUnicode = TRUE;
			}
			else

			if (!STRICMP (&argv [i] [1], _T("UNILOG")))
			{
				m_fUnicode = TRUE;
				ppszString = &m_pszLog;
			}
			else

			if (!STRNICMP (&argv [i] [1], _T("UNILOG+:"), 8))
			{
				m_pszLog   = &argv[i][9];
				m_fUnicode = TRUE;
				SetFlag(FLAG_APPENDLOG);
			}
			else

			if (!STRICMP (&argv [i] [1], _T("UNILOG+")))
			{
				SetFlag(FLAG_APPENDLOG);
				m_fUnicode = TRUE;
				ppszString = &m_pszLog;
			}
			else

			if (!STRNICMP (&argv [i] [1], _T("MAX:"), 4))
			{
				if (!ParseSize(&argv[i][5], m_dw64Max, m_fMaxSize))
					return CMD_ERR;
			}
			else

			if (!STRNICMP (&argv [i] [1], _T("MAXV:"), 5))
			{
				if (!ParseSize(&argv[i][6], m_dw64Max, m_fMaxSizeV))
					return CMD_ERR;
			}
			else

			if (!STRNICMP (&argv [i] [1], _T("NAME:"), 5))
			{
				if (m_pszBackupName)
				{
					PRINTF(_T("Backup Name already set\n"));
					return CMD_ERR;
				}

				m_pszBackupName = NewString(&argv[i][6]);
			}
			else

			if (!STRICMP (&argv [i] [1], _T("NAME")))
			{
				if (m_pszBackupName)
				{
					PRINTF(_T("Backup Name already set\n"));
					return CMD_ERR;
				}

				ppszString = &m_pszBackupName;
			}
			else
			{
				PRINTF(_T("Unknown Command: %s\n"), argv[i]);
				return CMD_ERR;
			}
        }
        else
        {
			if (ppszString)
			{
				*ppszString = NewString(argv[i]);
				ppszString  = NULL;
			}
			else

			if (m_pFilesArray)
				m_pFilesArray->Add(argv[i]);
			else
			{
				switch (nFiles)
				{
					case 0:
						m_vfSrc.SetPath(argv[i]);
						break;

					case 1:
						m_vfDst.SetPath(argv[i]);
						break;

					default:
						PRINTF(_T("Error: Too many arguments - can only specify ONE source and ONE destination directory\n"));
						return CMD_ERR;
				}

				nFiles++;
			}
        }
    }
	
	if (nFiles != 2)
	{
		PRINTF(_T("Error: Not enough arguments - must specify ONE source and ONE destination directory\n"));
		return CMD_ERR;
	}

	if (m_fListOnly && !m_pszLog)	// Make sure m_fQuiet isn't set if we have specified /L
		m_fQuiet = FALSE;

	return CMD_NORMAL;
}

int _tmain(int argc, _TCHAR* argv[])
{
	INT nCmd = Parse(argc, argv);

	if (nCmd == CMD_HELP)
		return Help();

	if (nCmd == CMD_ERR)
	{
		PRINTF(_T("\nType VBackup -? for command help\n\n"));
		return 2;
	}

	return (m_fRestore) ? VRestore() : VCopy();
}

