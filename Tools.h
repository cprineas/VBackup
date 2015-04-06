//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#pragma once

LPTSTR NewString(LPTSTR pszBuffer, int nLen=-1);
CHAR* StringA(WCHAR* pszWide, int nLen=-1, UINT uCodePage=0);
HANDLE FindFirstFileV(LPTSTR pszPath, WIN32_FIND_DATA* pFindData);
int CreateDirectoryRecursive(LPTSTR pszDirectory, LPTSTR pszCopyAttributes=NULL);
HANDLE CreateFileV(LPTSTR pszFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
BOOL PathExists(LPTSTR pszPath);
DWORD GetFileAttributesV(LPTSTR pszFileName, DWORD dwSet=0xffffffff);
int GetFileDetails(LPTSTR pszPath, WIN32_FIND_DATA* pFindData);
BOOL MoveFileV(LPTSTR pszExisting, LPTSTR pszNew, BOOL fMoveEx=FALSE, DWORD dwFlags=0);
BOOL CopyFileV(LPTSTR pszExisting, LPTSTR pszNew, BOOL fFailIfExists);
BOOL DeleteFileV(LPTSTR pszFileName);
BOOL IsEmpty(LPTSTR pszPath);
LPTSTR ConcatString(LPTSTR psz1, LPTSTR psz2);

