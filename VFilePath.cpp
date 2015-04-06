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

#define SLASH_CHAR		'\\'
#define SLASH_STRING	_T("\\")

extern LPTSTR NewString(LPTSTR pszBuffer, int nLen=-1);
extern HANDLE CreateFileV(LPTSTR pszFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

CVFilePath::CVFilePath()
{
	Init();
}

CVFilePath::CVFilePath(LPTSTR pszPath)
{
	Init();

    SetPath(pszPath);
}

CVFilePath::CVFilePath(LPTSTR pszDirectory, LPTSTR pszFileName)
{
	Init();

    MakePath(pszDirectory, pszFileName);
}

CVFilePath::~CVFilePath()
{
    DeletePath();
}

void CVFilePath::Init()
{
	m_nSize        = 0;
    m_nBufLen      = 0;
    m_dwPidl       = 0;
	m_nPathLen     = 0;
	m_nFileNameLen = 0;
	m_pszFileName  = NULL;
    m_pszWidePath  = NULL;
	m_fVirtual     = FALSE;
}

void CVFilePath::DeletePath()
{
    if (m_pszWidePath)
        delete [] m_pszWidePath;

	Init();
}

void CVFilePath::Empty()
{
	// We need Empty so that an initial NULL is placed in the buffer - so that subsequent
	// Appends will work.

    DeletePath();

    m_szPath[0] = 0;
}

int CVFilePath::Length()
{
    return lstrlen(Path());
}

int CVFilePath::Size()
{
    return (m_pszWidePath) ? m_nSize : MAX_VPATH;
}

LPTSTR CVFilePath::MakePath(LPTSTR pszDirectory, LPTSTR pszFileName)
{
    // Append the filename to the directory to make a path
    // If the resultant string is >= MAX_VPATH, we will store the path in m_pszWidePath.

    // Delete any path that we may already have

    DeletePath();

    int nDirLen  = (pszDirectory) ? lstrlen(pszDirectory) : 0;
    int nFileLen = (pszFileName)  ? lstrlen(pszFileName)  : 0;

    int nAppendSlash = 0;

	if (nDirLen)
		nAppendSlash = (pszDirectory[nDirLen-1] == SLASH_CHAR) ? 0 : 1;

    LPTSTR pszPathBuf;
    
	m_nPathLen = nDirLen + nFileLen + nAppendSlash;

    if (m_nPathLen < MAX_VPATH)
    {
        // Use pre-allocated buffer

		if (nDirLen)
			lstrcpy(m_szPath, pszDirectory);

        pszPathBuf = m_szPath;
    }
    else
    {
		m_nSize       = m_nPathLen+1;
        m_pszWidePath = new TCHAR[m_nSize];
        pszPathBuf    = m_pszWidePath;

		if (nDirLen)
			lstrcpy(pszPathBuf, pszDirectory);
    }

    if (nAppendSlash)
        pszPathBuf[nDirLen++] = SLASH_CHAR;

	lstrcpy(pszPathBuf+nDirLen, pszFileName);

    ASSERT(m_nPathLen == lstrlen(pszPathBuf));

	if (nFileLen)
	{
		m_nFileNameLen = nFileLen;
		m_pszFileName  = pszPathBuf+nDirLen;
	}
    
    return pszPathBuf;
}

LPTSTR CVFilePath::SetPath(LPTSTR pszPath)
{
    DeletePath();

	if (pszPath == NULL)
	{
		m_nPathLen = 0;
		m_szPath[0] = 0;
		return m_szPath;
	}

    m_nPathLen = lstrlen(pszPath);

    if (m_nPathLen < MAX_VPATH)
    {
        // Use pre-allocated buffer

        lstrcpy(m_szPath, pszPath);

        return m_szPath;
    }

    // Note that we do not prepend "\\?\" if the file name is long.
	// It will be added by the eventual call to FileNameW()

	m_nSize = m_nPathLen+1;

    m_pszWidePath = new TCHAR[m_nSize];

	lstrcpy(m_pszWidePath, pszPath);

	return m_pszWidePath;
}

LPTSTR CVFilePath::Append(LPTSTR pszBuf)
{
	ASSERT(pszBuf);

	int nBufLen  = lstrlen(pszBuf);
    int nPathLen = m_nPathLen + nBufLen;

    if ((nPathLen < MAX_VPATH) && (m_pszWidePath == NULL))
    {
		lstrcpy(&m_szPath[m_nPathLen], pszBuf);
		
		m_nPathLen = nPathLen;

		if (m_pszFileName)
			m_nFileNameLen += nBufLen;

        return m_szPath;
    }

	int nFileNameOffset = (m_pszFileName) ? (m_nPathLen - m_nFileNameLen) : 0;

	m_nSize = nPathLen+1;

    LPTSTR pszWide = new TCHAR[m_nSize];

    if (m_pszWidePath)
    {
        lstrcpy(pszWide, m_pszWidePath);

        delete [] m_pszWidePath;
    }
    else
    {
        lstrcpy(pszWide, m_szPath);
    }

    m_pszWidePath = pszWide;

	lstrcat(m_pszWidePath, pszBuf);

	m_nPathLen = nPathLen;

	if (nFileNameOffset)
		m_pszFileName = &m_pszWidePath[nFileNameOffset];

	if (m_pszFileName)
		m_nFileNameLen += nBufLen;

	return m_pszWidePath;
}

LPTSTR CVFilePath::AppendSlash()
{
    LPTSTR pszPath = Path();
    int    nLen    = lstrlen(pszPath);

	if (nLen && (pszPath[nLen-1] != SLASH_CHAR))
        Append(SLASH_STRING);

	m_pszFileName  = EOS();
	m_nFileNameLen = 0;

	return Path();
}

LPTSTR CVFilePath::AppendFileName(LPTSTR pszFileName)
{
    AppendSlash();

    Append(pszFileName);
	
	return Path();
}

LPTSTR CVFilePath::SetFileName(LPTSTR pszFileName, BOOL fRecalc)
{
	if (fRecalc)
		Split();

	if (m_pszFileName == NULL)
		return AppendFileName(pszFileName);

	int nFileNameLen = lstrlen(pszFileName);
	int nOverLen     = m_nFileNameLen - nFileNameLen;
	int nNewPathLen  = m_nPathLen - nOverLen;

	if ((nOverLen >= 0) || ((nNewPathLen < MAX_VPATH) && !m_pszWidePath))
	{
		// The new file name fits in the current buffer (or fits in m_szPath) - so copy it and adjust m_nPathLen
		lstrcpy(m_pszFileName, pszFileName);
		
		m_nPathLen     = nNewPathLen;
		m_nFileNameLen = nFileNameLen;

		return Path();
	}

	// We need to store the filename in m_pszWidePath

	m_nSize = nNewPathLen+1;

    LPTSTR pszWide = new TCHAR[m_nSize];

    if (m_pszWidePath)
    {
        lstrcpy(pszWide, m_pszWidePath);

        delete [] m_pszWidePath;
    }
    else
    {
        lstrcpy(pszWide, m_szPath);
    }

    m_pszWidePath = pszWide;
	m_pszFileName = &m_pszWidePath[m_nPathLen - m_nFileNameLen];

	lstrcpy(m_pszFileName, pszFileName);

	m_nPathLen     = nNewPathLen;
	m_nFileNameLen = nFileNameLen;

	return Path();
}

LPTSTR CVFilePath::Path()
{
	if (m_pszWidePath == NULL)
		return m_szPath;

	return m_pszWidePath;
}


void CVFilePath::ClearFileName()
{
	m_pszFileName  = NULL;
	m_nFileNameLen = 0;
}

LPTSTR CVFilePath::FileName()
{
	ASSERT(m_pszFileName);

	if (m_pszFileName == NULL)
		m_pszFileName = EOS();

	return m_pszFileName;
}

LPTSTR CVFilePath::EOS()
{
	return (m_pszWidePath) ? (&m_pszWidePath[m_nPathLen]) : (&m_szPath[m_nPathLen]);
}

void CVFilePath::Split()
{
	LPTSTR psz = STRRCHR(Path(), SLASH_CHAR);

	ASSERT(psz);

	if (psz == NULL)
		return ClearFileName();

	m_nFileNameLen = lstrlen(++psz);
	m_pszFileName  = psz;
}

BOOL CVFilePath::ReparseMicrosoft(REPARSE_DATA_BUFFER* pBuf, LPTSTR* ppszVirtual)
{
	// The reparse point is returned in the form of "\??\X:\..."
	// I really don't want to parse the question marks - so I will use the PrintName - which *seems* to be what we want!
	// If this doesn't work, we will have to use the SubstituteName and parse it.

	LPTSTR pszVirtual = NULL;

	if (pBuf->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
	{
		if (pBuf->MountPointReparseBuffer.PrintNameLength)
			pszVirtual = NewString(&pBuf->MountPointReparseBuffer.PathBuffer[pBuf->MountPointReparseBuffer.PrintNameOffset/sizeof(WCHAR)], pBuf->MountPointReparseBuffer.PrintNameLength/sizeof(WCHAR));
		else

		if (pBuf->MountPointReparseBuffer.SubstituteNameLength)
			pszVirtual = NewString(&pBuf->MountPointReparseBuffer.PathBuffer[pBuf->MountPointReparseBuffer.SubstituteNameOffset/sizeof(WCHAR)], pBuf->MountPointReparseBuffer.SubstituteNameLength/sizeof(WCHAR));
	}
	else

	if (pBuf->ReparseTag == IO_REPARSE_TAG_SYMLINK)
	{
		if (pBuf->SymbolicLinkReparseBuffer.PrintNameLength)
			pszVirtual = NewString(&pBuf->SymbolicLinkReparseBuffer.PathBuffer[pBuf->SymbolicLinkReparseBuffer.PrintNameOffset/sizeof(WCHAR)], pBuf->SymbolicLinkReparseBuffer.PrintNameLength/sizeof(WCHAR));
		else

		if (pBuf->SymbolicLinkReparseBuffer.SubstituteNameLength)
			pszVirtual = NewString(&pBuf->SymbolicLinkReparseBuffer.PathBuffer[pBuf->SymbolicLinkReparseBuffer.SubstituteNameOffset/sizeof(WCHAR)], pBuf->SymbolicLinkReparseBuffer.SubstituteNameLength/sizeof(WCHAR));
	}
	else
	{
		ASSERT(FALSE);
		return FALSE;
	}

	ASSERT(pszVirtual);
	if (pszVirtual == NULL)
		return FALSE;

	if (*ppszVirtual)
		delete [] *ppszVirtual;

	*ppszVirtual = pszVirtual;

	return TRUE;
}

BOOL CVFilePath::ReparseOther(REPARSE_GUID_DATA_BUFFER* pBuf, LPTSTR* ppszVirtual)
{
	ASSERT(FALSE);	// Can we come here? ie, when do we encounter a non Microsoft Reparse Point?

	return FALSE;
}

BYTE* CVFilePath::GetReparseData(HANDLE h, BYTE* pszBuf, DWORD dwBufSize, int& nError)
{
	DWORD dwSize = 0;

	// Use default buffer to get the reparse data

	if (!::DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, pszBuf, dwBufSize, &dwSize, NULL))
	{
		// This is where it was failing when Nicholas J Parker reported that he could not expand his DFS shares (in V11 Beta 4)
		nError = GetLastError();
	}
	else
	{
		nError = 0;
		return pszBuf;
	}

	BYTE* pBuf = NULL;

	while (nError == ERROR_INSUFFICIENT_BUFFER)
	{
		// Let's allocate a larger buffer

		dwBufSize += SUGGESTED_REPARSE_DATA_SIZE;

		if (dwBufSize > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
			dwBufSize = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;

		if (pBuf)
			delete [] pBuf;

		pBuf = new BYTE[dwBufSize];

		dwSize = 0;

		if (!::DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, pBuf, dwBufSize, &dwSize, NULL))
			nError = GetLastError();
		else
			nError = 0;

		if (dwBufSize == MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
			break;
	}

	if (nError)
	{
		ASSERT(FALSE);
		delete [] pBuf;
		return NULL;
	}

	return pBuf;
}

BOOL CVFilePath::IsVirtualFolder(LPTSTR pszFolder, LPTSTR* ppszVirtual)
{
	int nError = 0;

	HANDLE h = CreateFileV(pszFolder,	FILE_READ_EA,		//FILE_READ_ATTRIBUTES | SYNCHRONIZE,
										FILE_SHARE_READ | FILE_SHARE_WRITE,
										NULL, OPEN_EXISTING,
										FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
										NULL);

	if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    BYTE szBuf[SUGGESTED_REPARSE_DATA_SIZE];	// This should be enough

	BYTE* pBuf = GetReparseData(h, szBuf, sizeof(szBuf), nError);

	::CloseHandle(h);

    if (pBuf == NULL)
	{
		ASSERT(nError);
		return FALSE;
	}

	DWORD dwReparseTag = *((DWORD *)pBuf);

	BOOL fOK = (dwReparseTag & 0x80000000) ? ReparseMicrosoft((REPARSE_DATA_BUFFER *)pBuf, ppszVirtual) : ReparseOther((REPARSE_GUID_DATA_BUFFER *)pBuf, ppszVirtual);

	if (pBuf != szBuf)
		delete [] pBuf;

	return fOK;
}

LPTSTR CVFilePath::ReparseToPath()
{
	// Added in V10SR1 to convert the current buffer from a reparse point to the corresponding folder
	
	ASSERT(!m_fVirtual);

	if (!m_fVirtual)
	{
		LPTSTR pszVirtual = NULL;

		if (IsVirtualFolder(Path(), &pszVirtual))
		{
			SetPath(pszVirtual);
			
			//ASSERT(!m_fReparsePoint);	// Note that calling SetPath will clear m_fReparsePoint

			m_fVirtual = TRUE;
			delete [] pszVirtual;
		}
	}
	
	return Path();
}
