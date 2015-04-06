//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#pragma once

#define MAX_VPATH	MAX_PATH

typedef struct _REPARSE_DATA_BUFFER
{
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;

    union
    {
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } SymbolicLinkReparseBuffer;

        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;

        struct
        {
            UCHAR  DataBuffer[1];
        } GenericReparseBuffer;
    };

} REPARSE_DATA_BUFFER;

#define REPARSE_DATA_BUFFER_HEADER_SIZE  FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

#define SUGGESTED_REPARSE_DATA_SIZE		(REPARSE_GUID_DATA_BUFFER_HEADER_SIZE+MAX_PATH+MAX_PATH+MAX_PATH+MAX_PATH)


/////////////////////////////////////////////////////////////////////////////
// CVFilePath


class CVFilePath
{
public:
	CVFilePath();
	CVFilePath(LPTSTR pszPath);
	CVFilePath(LPTSTR pszDirectory, LPTSTR pszFileName);
	~CVFilePath();
	LPTSTR ReparseToPath();
    LPTSTR MakePath(LPTSTR pszDirectory, LPTSTR pszFileName);
    LPTSTR SetPath(LPTSTR pszPath);
	LPTSTR FileName();
    LPTSTR Path();
    LPTSTR Append(LPTSTR pszBuf);
    LPTSTR AppendSlash();
    LPTSTR AppendFileName(LPTSTR pszFileName);
    LPTSTR SetFileName(LPTSTR pszFileName, BOOL fRecalc=FALSE);
	int Length();
	int Size();
    void Empty();

protected:
	static BYTE* GetReparseData(HANDLE h, BYTE* pszBuf, DWORD dwBufSize, int& nError);
	static BOOL ReparseMicrosoft(REPARSE_DATA_BUFFER* pBuf, LPTSTR* ppszVirtual);
	static BOOL ReparseOther(REPARSE_GUID_DATA_BUFFER* pBuf, LPTSTR* ppszVirtual);
	BOOL IsVirtualFolder(LPTSTR pszFolder, LPTSTR* ppszVirtual);
	void Init();
	void Split();
	LPTSTR EOS();
	int m_nPathLen;
	int m_nSize;
	int m_nBufLen;
	BOOL m_fVirtual;
    DWORD m_dwPidl;
    TCHAR m_szPath[MAX_VPATH];
    LPTSTR m_pszWidePath;
	LPTSTR m_pszFileName;
	int m_nFileNameLen;
	void ClearFileName();
    void DeletePath();
};

/////////////////////////////////////////////////////////////////////////////

