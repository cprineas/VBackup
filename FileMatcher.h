//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#pragma once


#define	FNM_MATCH	0
#define	FNM_NOMATCH	1

class CFileMatcher
{
public:
	CFileMatcher(LPTSTR pszMask);
	virtual ~CFileMatcher();
	static int FileNameMatch(LPCTSTR pattern, LPCTSTR string, BOOL fMatchedDot=FALSE);
	BOOL Match(LPTSTR pszName, BOOL fDirectory);


protected:
	void ParseMultiple(LPTSTR pszMultiple);
    TCHAR  m_szMultiple[MAX_PATH+2];
	LPTSTR m_pszDirBuffer;
	LPTSTR m_pszMask;
	LPTSTR m_pszMaskDir;
	LPTSTR m_pszMultipleDir;
	LPTSTR m_pszMultiple;
	BOOL m_fMatchAll;
	BOOL m_fMatchAllDir;
};


