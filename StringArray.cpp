//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

// Implements CStringArray - which is basically a linked list of "strings" (LPTSTR)

#include "stdafx.h"
#include <windows.h>
#include <assert.h>
#include "StringArray.h"
#include "Macros.h"

extern LPTSTR NewString(LPTSTR pszBuffer, int nLen=-1);

CStringArray::CStringArray()
{
    m_pHead    = NULL;
    m_pTail    = NULL;
    m_pCurrent = NULL;
    m_nCount   = 0;
}

CStringArray::~CStringArray()
{
    while (m_pHead)
    {
        STRING_LIST* pNext = (STRING_LIST *)m_pHead->pNext;

        DeleteString(m_pHead);

        m_pHead = pNext;
    }
}

void CStringArray::DeleteString(STRING_LIST* pString)
{
    delete [] pString->pszString;
    delete pString;

	m_nCount--;
}

int CStringArray::Add(LPTSTR pszString)
{
	STRING_LIST* pString = new STRING_LIST;

	pString->pszString = NewString(pszString);
	pString->pNext = NULL;

	if (m_pHead == NULL)
		m_pHead = pString;

	if (m_pTail)
		m_pTail->pNext = pString;

	m_pTail    = pString;
	m_pCurrent = pString;
	
	return ++m_nCount;
}

LPTSTR CStringArray::GetFirst()
{
    m_pCurrent = m_pHead;

    return GetCurrent();
}

LPTSTR CStringArray::GetNext()
{
    if (m_pCurrent)
        m_pCurrent = (STRING_LIST *)m_pCurrent->pNext;

    return GetCurrent();
}

LPTSTR CStringArray::GetCurrent()
{
    return (m_pCurrent) ? m_pCurrent->pszString : NULL;
}

LPTSTR CStringArray::GetString(int nString)
{
	ASSERT(nString < m_nCount);

	if (nString >= m_nCount)
		return NULL;

	int          nIndex = 0;
	STRING_LIST* pList  = m_pHead;

	ASSERT(pList);

	while (pList && (nIndex < nString))
	{
		pList = (STRING_LIST *)pList->pNext;
		nIndex++;
	}

	ASSERT(pList);

	return (pList) ? pList->pszString : NULL;
}

void CStringArray::Sort()
{
	// Sort in reverse order (ie, most recent date first) - we will do a simple linear sort!

	if (!m_nCount)
		return;

	ASSERT(m_pHead);

	STRING_LIST* pHead = m_pHead;

	while (pHead->pNext)
	{
		// Scan the list to see which entry goes first
		STRING_LIST* pFirst = pHead;
		STRING_LIST* pList  = (STRING_LIST *)pHead->pNext;

		while (pList)
		{
			if (STRCMP(pList->pszString, pFirst->pszString) > 0)
				pFirst = pList;

			pList  = (STRING_LIST *)pList->pNext;
		}

		if (pFirst != pHead)
		{
			LPTSTR psz = pHead->pszString;

			pHead->pszString  = pFirst->pszString;
			pFirst->pszString = psz;
		}

		pHead = (STRING_LIST *)pHead->pNext;
	}
}

void CStringArray::DeleteTail()
{
    // We only need to know m_pTail so we can delete it after we parse the arguments
	// We don't need to maintain m_pTail - but we will in case we need it in the future!

	STRING_LIST* pCurrent  = m_pHead;
	STRING_LIST* pPrevious = NULL;

	while (pCurrent)
	{
		if (pCurrent == m_pTail)
		{
			DeleteString(pCurrent);
			m_pTail = pPrevious;

			if (m_pTail)
				m_pTail->pNext = NULL;

			break;
		}

		pPrevious = pCurrent;
		pCurrent  = (STRING_LIST *)pCurrent->pNext;
	}
}

