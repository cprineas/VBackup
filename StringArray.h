//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#pragma once

typedef struct
{
	LPTSTR pszString;
	void*  pNext;
} STRING_LIST;


/////////////////////////////////////////////////////////////////////////////
// CStringArray


class CStringArray
{
public:
	CStringArray();
	~CStringArray();
    int Add(LPTSTR pszString);
    LPTSTR GetFirst();
    LPTSTR GetNext();
    LPTSTR GetCurrent();
	LPTSTR GetString(int nString);
    void DeleteTail();
	void Sort();

protected:
    STRING_LIST* m_pHead;
    STRING_LIST* m_pTail;
    STRING_LIST* m_pCurrent;
    int m_nCount;
    void DeleteString(STRING_LIST* pString);
};

/////////////////////////////////////////////////////////////////////////////

