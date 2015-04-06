//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#include "stdafx.h"
#include <windows.h>
#include <assert.h>
#include "VFilePath.h"
#include "Macros.h"

// Use MAX_DIRECTORY_PATH as the directory API functions (like CreateDirectory) start having problems at 248 characters instead of MAX_PATH (260)
#define MAX_DIRECTORY_PATH			248

#define VFILE_MOVE		1
#define VFILE_MOVEEX	2
#define VFILE_COPY		3
#define VFILE_DELETE	4
#define VFILE_DELETEALL	5

LPTSTR NewString(LPTSTR pszBuffer, int nLen=-1)
{
	if (pszBuffer == NULL)
		return NULL;

	LPTSTR pszNew = new TCHAR[((nLen < 0) ? lstrlen(pszBuffer) : nLen) + 1];

	if (nLen < 0)
		STRCPY(pszNew, pszBuffer);
	else
	{
		memcpy(pszNew, pszBuffer, nLen*sizeof(TCHAR));
		pszNew[nLen] = 0;
	}

	return pszNew;
}

LPTSTR ConcatString(LPTSTR psz1, LPTSTR psz2)
{
	if (!psz1 && !psz2)
		return NULL;

	int nLen1 = (psz1) ? STRLEN(psz1) : 0;
	int nLen2 = (psz2) ? STRLEN(psz2) : 0;

	LPTSTR pszNew = new TCHAR[nLen1 + nLen2 + 1];
	LPTSTR psz    = pszNew;

	if (nLen1)
	{
		memcpy(psz, psz1, nLen1*sizeof(TCHAR));
		psz += nLen1;
	}

	if (nLen2)
	{
		memcpy(psz, psz2, nLen2*sizeof(TCHAR));
		psz += nLen2;
	}

	*psz = 0;

	return pszNew;
}

WCHAR* StringW(LPTSTR pszWide)
{
	return NewString(pszWide);
}

CHAR* StringA(WCHAR* pszWide, int nLen=-1, UINT uCodePage=0)
{
	if (pszWide == NULL)
		return NULL;

	int nSize = WideCharToMultiByte((uCodePage) ? uCodePage : CP_UTF8, 0, pszWide, nLen, NULL, 0, NULL, NULL);

	if (nSize <= 0)
	{
		ASSERT(FALSE);
		return NULL;
	}

	// Note that if we pass the length, the converted string will not be nul terminated

	CHAR* pszANSI = new CHAR[(nLen < 0) ? nSize : (nSize+1)];

    // Convert the string

	if (WideCharToMultiByte((uCodePage) ? uCodePage : CP_ACP, 0, pszWide, nLen, pszANSI, nSize, NULL, NULL) > 0)
	{
		if (nLen > 0)
			pszANSI[nSize] = 0;
		else
		{
			ASSERT(pszANSI[nSize-1] == 0);
		}

		return pszANSI;
	}

    delete [] pszANSI;

	return NULL;
}

WCHAR* FileNameW(LPTSTR pszNarrow, BOOL fFile)
{
	// The Unicode functions that use file names require that the file name start with "\\?\"
	// If pszNarrow does not already have this prefix - we need to add it.
	//
	// Note that this only works for a single NULL terminated file name.
	// It will not work for a doubly NULL terminated file list.

	if (pszNarrow == NULL)
		return NULL;

	LPTSTR pszLong = NULL;

	int nLen = lstrlen(pszNarrow);

	if (nLen >= ((fFile) ? MAX_PATH : MAX_DIRECTORY_PATH))
	{
		// We need to do 2 things when converting "narrow" names into "wide" names:
		//
		// Firstly, we need to prepend the file name with "\\?\"
		// Secondly, we need to see if the path contains a "\..\", and if it does, normalise the path
		// (ie, remove reference to its parent) because the UnicodeW functions do not seem to handle "\..\"!

		BOOL   fPrepend = FALSE;
		int    nDelete  = 0;
		int    nWideLen = 0;
		LPTSTR psz1     = NULL;
		LPTSTR psz2     = NULL;

		// Check to see if we need to prepend "\\?\"

		if (STRNCMP(pszNarrow, _T("\\\\?\\"), 4))
		{
			fPrepend = TRUE;
			nWideLen = nLen + 4;
		}

		psz2 = STRSTR(pszNarrow, _T("\\..\\"));

		if (psz2 && (psz2 != pszNarrow))
		{
			nDelete = 4;	// Delete the "\..\" - we still want to keep the file name after this

			psz1 = psz2 - 1;

			while (psz1 > pszNarrow)
			{
				if (*psz1 == '\\')
					break;

				psz1--;
				nDelete++;
			}

			if (nWideLen == 0)
				nWideLen = nLen;

			if (psz1 != pszNarrow)
				nWideLen -= nDelete;
			else
				psz1 = NULL;		// Something went wrong - don't strip "\..\"
		}

		if (nWideLen > 0)
		{
			pszLong = new TCHAR[nWideLen+1];

			LPTSTR psz = pszLong;

			if (fPrepend)
			{
				lstrcpy(psz, _T("\\\\?\\"));
				psz += 4;
				nWideLen -= 4;
			}

			if (psz1)
			{
				psz2 = pszNarrow;

				while (psz2 <= psz1)
				{
					*psz++ = *psz2++;
				}

				psz2 += nDelete;
				STRCPY(psz, psz2);
			}
			else
				STRCPY(psz, pszNarrow);
		}
	}

	WCHAR* pszW = StringW((pszLong) ? pszLong : pszNarrow);

	delete [] pszLong;

	return pszW;
}

HANDLE CreateFileV(LPTSTR pszFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	HANDLE hFile = NULL;

	if (lstrlen(pszFileName) >= MAX_PATH)
	{
		WCHAR* pszFileNameW = FileNameW(pszFileName, TRUE);

		if (pszFileNameW)
		{
			hFile = ::CreateFileW(pszFileNameW, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

			// hFile is NULL if not implemented (on Win 9x)
			// I added the check for GetLastError() even though it doesn't appear to be necessary (for now!)

			if ((hFile == INVALID_HANDLE_VALUE) && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
				hFile = NULL;
		}

		delete [] pszFileNameW;
	}

	if (hFile == NULL)
	{
		hFile = ::CreateFile(pszFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
	}

	return hFile;
}

DWORD GetFileAttributesV(LPTSTR pszFileName, DWORD dwSet=0xffffffff)
{
	BOOL  fSet  = (dwSet != 0xffffffff);

	BOOL  fDone = FALSE;
	DWORD dw;

	if (lstrlen(pszFileName) >= MAX_PATH)
	{
        // Note that we need to check the file name length - even for the Unicode build!
		WCHAR* pszFileNameW = FileNameW(pszFileName, TRUE);

		if (pszFileNameW)
		{
			if (fSet)
			{
				dw = (DWORD)::SetFileAttributesW(pszFileNameW, dwSet);

				if (dw || (GetLastError() != ERROR_CALL_NOT_IMPLEMENTED))
					fDone = TRUE;
			}
			else
			{
				dw = ::GetFileAttributesW(pszFileNameW);

				if ((dw != 0xffffffff) || (GetLastError() != ERROR_CALL_NOT_IMPLEMENTED))
					fDone = TRUE;
			}
		}

		delete [] pszFileNameW;
	}

	if (fDone)
		return dw;

	return (fSet) ? ::SetFileAttributes(pszFileName, dwSet) : ::GetFileAttributes(pszFileName);
}

HANDLE FindFirstFileV(LPTSTR pszPath, WIN32_FIND_DATA* pFindData)
{
	BOOL   fDone = FALSE;
	HANDLE h;

	if (lstrlen(pszPath) >= MAX_PATH)
	{
		WCHAR* pszPathW = FileNameW(pszPath, TRUE);

		if (pszPathW)
		{
			h = ::FindFirstFile(pszPathW, pFindData);

			if (h != INVALID_HANDLE_VALUE)
				fDone = TRUE;
			else

			if (GetLastError() != ERROR_CALL_NOT_IMPLEMENTED)
				fDone = TRUE;
		}

		delete [] pszPathW;
	}

	if (fDone)
		return h;

	h = ::FindFirstFile(pszPath, pFindData);

	return h;
}

int GetFileDetails(LPTSTR pszPath, WIN32_FIND_DATA* pFindData)
{
	HANDLE h = FindFirstFileV(pszPath, pFindData);

	if (h == INVALID_HANDLE_VALUE)
	{
		int nError = GetLastError();
		
		ASSERT(nError);

		if (!nError)
			nError = ERROR_OPEN_FAILED;

		return nError;
	}

	FindClose(h);

	return 0;
}

BOOL IsDirectory(LPTSTR pszPath)
{
	// Returns TRUE if pszPath is a valid directory.

	DWORD dw = GetFileAttributesV(pszPath);

	if (dw == 0xffffffff)
		return FALSE;

	return (dw & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
}

BOOL CreateDirectoryV(LPTSTR pszDirectory)
{
	// We use MAX_DIRECTORY_PATH instaed of MAX_PATH as CreateDirectory doesn't handle normal paths >= 248 chars! (ie, we need to prepend \\?\)

	WCHAR* pszDirectoryW = NULL;

	if (lstrlen(pszDirectory) >= MAX_DIRECTORY_PATH)
	{
		// If we have a huge directory, we need to prepend it with the proper prefix!

		pszDirectoryW = FileNameW(pszDirectory, FALSE);
	}

	BOOL fOK = FALSE;

	fOK = ::CreateDirectory((pszDirectoryW) ? pszDirectoryW : pszDirectory, NULL);

	if (pszDirectoryW)
		delete [] pszDirectoryW;

	return fOK;
}

int CreateDirectoryRecursive(LPTSTR pszDirectory, LPTSTR pszCopyAttributes=NULL)
{
	// Create pszPath by creating each directory in the "tree"

	CVFilePath vfPath(pszDirectory);

	LPTSTR pszPath = vfPath.Path();
	LPTSTR pszNext = pszPath;
	LPTSTR psz;

	// Find out where to start - make sure we handle UNCs and the root

	if (STRSTR(pszPath, _T("\\\\")) == pszPath)
	{
		// UNC - skip "\\"
		pszNext += 2;
	}
	else

	if (STRCHR(pszPath, '\\') == pszPath)
	{
		pszNext++;
	}
	else

	if ((lstrlen(pszPath) > 2) && (pszPath[2] == '\\'))
	{
		// Jump over "X:\"
		pszNext += 3;
	}

	while (1)
	{
		psz = STRCHR(pszNext, '\\');
		if (psz == NULL)
			break;

		// Terminate at first directory and try to create it
		*psz = 0;

		if (!IsDirectory(pszPath))
		{
			if (!CreateDirectoryV(pszPath))
				return GetLastError();
		}

		// Replace '\\' and search again
		*psz    = '\\';
		pszNext = psz + 1;
	}

	// Now try to create the last part of the path

	if (CreateDirectoryV(pszPath))
	{
		// Check to see if we should copy the attributes of the original directory - don't return an error if it fails
		if (pszCopyAttributes)
		{
			DWORD dw = GetFileAttributesV(pszCopyAttributes);

			if (dw != 0xffffffff)
				GetFileAttributesV(pszPath, dw);
		}

		return 0;
	}

	return GetLastError();
}

BOOL VFileOperation(LPTSTR pszFile1, LPTSTR pszFile2, DWORD lParam, DWORD dwType)
{
	// This is used to perform CopyFile/MoveFile/DeleteFile with support for file names > MAX_PATH

	BOOL   fOK     = FALSE;
	WCHAR* psz1W   = NULL;
	WCHAR* psz2W   = NULL;

	if (lstrlen(pszFile1) >= MAX_PATH)
		psz1W = FileNameW(pszFile1, TRUE);

	if (pszFile2 && (lstrlen(pszFile2) >= MAX_PATH))
		psz2W = FileNameW(pszFile2, TRUE);

	switch (dwType)
	{
		case VFILE_DELETE:
			fOK = ::DeleteFile((psz1W) ? psz1W : pszFile1);
			break;

		case VFILE_COPY:
			fOK = ::CopyFile((psz1W) ? psz1W : pszFile1, (psz2W) ? psz2W : pszFile2, (lParam) ? COPY_FILE_FAIL_IF_EXISTS : 0);
			break;

		case VFILE_MOVE:
			fOK = ::MoveFile((psz1W) ? psz1W : pszFile1, (psz2W) ? psz2W : pszFile2);
			break;

		case VFILE_MOVEEX:
			fOK = ::MoveFileEx((psz1W) ? psz1W : pszFile1, (psz2W) ? psz2W : pszFile2, lParam);
			break;

		default:
			assert(0);
	}

	if (psz1W)
		delete [] psz1W;

	if (psz2W)
		delete [] psz2W;

	return fOK;
}

BOOL CopyFileV(LPTSTR pszExisting, LPTSTR pszNew, BOOL fFailIfExists)
{
	return VFileOperation(pszExisting, pszNew, (DWORD)fFailIfExists, VFILE_COPY);
}

BOOL MoveFileV(LPTSTR pszExisting, LPTSTR pszNew, BOOL fMoveEx=FALSE, DWORD dwFlags=0)
{
	return VFileOperation(pszExisting, pszNew, dwFlags, (fMoveEx) ? VFILE_MOVEEX : VFILE_MOVE);
}

BOOL MoveFileExV(LPTSTR pszExisting, LPTSTR pszNew, DWORD dwFlags)
{
	return MoveFileV(pszExisting, pszNew, TRUE, dwFlags);
}

BOOL DeleteFileV(LPTSTR pszFileName)
{
	return VFileOperation(pszFileName, NULL, NULL, VFILE_DELETE);
}

BOOL PathExists(LPTSTR pszPath)
{
	return (GetFileAttributesV(pszPath) == 0xffffffff) ? FALSE : TRUE;
}

BOOL IsEmpty(LPTSTR pszPath)
{
	if (!PathExists(pszPath))
		return TRUE;
	
	CVFilePath vfPath(pszPath, _T("*"));

	WIN32_FIND_DATA fd;

	HANDLE hDir = FindFirstFileV(vfPath.Path(), &fd);
	
	if (hDir == INVALID_HANDLE_VALUE)
		return TRUE;

	BOOL fEmpty = TRUE;

	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (STRCMP(_T(".."), fd.cFileName) && STRCMP(_T("."), fd.cFileName))
			{
				fEmpty = FALSE;
				break;
			}
		}
		else
		{
			fEmpty = FALSE;
			break;
		}
	}
	while (::FindNextFile(hDir, &fd));

    FindClose(hDir);

	return fEmpty;
}
