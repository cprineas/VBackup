//*****************************************
// Copyright (c) 2010-2015 Charles Prineas
//
// Released under MIT License
// See LICENSE file for details
//*****************************************

#include "stdafx.h"
#include <windows.h>
#include <assert.h>
#include "FileMatcher.h"
#include "Macros.h"

/////////////////////////////////////////////////////////////////////////////
// CFileMatcher


CFileMatcher::CFileMatcher(LPTSTR pszMask)
{
	m_pszDirBuffer = NULL;

	if (pszMask == NULL)
	{
		ASSERT(FALSE);
		// I am not sure if this is possible - but I better check for it just in case!

		m_fMatchAll    = TRUE;
		m_fMatchAllDir = TRUE;

		// There is no point in initialising m_pszMask and m_pszMultiple - as they will not be used!

		return;
	}

	m_pszMask        = pszMask;
	m_pszMaskDir     = NULL;
	m_pszMultiple    = NULL;
	m_pszMultipleDir = NULL;

	// Parse the mask to see if it contains multiple/dual masks (ie, look for ",;!")

	TCHAR  ch;
	int    nDir   = 0;
	int    nChars = 0;
	LPTSTR pszDir = NULL;
	LPTSTR psz    = pszMask;

	while (ch = *psz++)
	{
		if ((ch == ',') || (ch == ';'))
		{
			if (pszDir)
			{
				if (m_pszMultipleDir == NULL)
					m_pszMultipleDir = pszDir;		// We need to re-point this to the mask buffer when we create it later on
			}
			else
			{
				if (m_pszMultiple == NULL)
				{
					// We will be modifying the multiple mask string - so copy it into m_szMultiple
					m_pszMultiple = m_szMultiple;
					STRNCPY(m_pszMultiple, pszMask, MAX_PATH);
				}
			}
		}
		else

		if ((ch == '|') && !pszDir)
		{
			pszDir = psz;
			nDir   = nChars;
		}

		nChars++;
	}

	BOOL fNoDirs = FALSE;

	if (pszDir)
	{
		// We have a second mask - we will need to assign m_pszDirBuffer so it can hold it
		// We also need to copy the original mask into its own buffer (we will use m_szMultiple) as we will need to replace the | with a nul!

		if (nDir)
		{
			if (m_pszMultiple == NULL)
				STRNCPY(m_szMultiple, pszMask, MAX_PATH);
				
			m_pszMask = m_szMultiple;

			ASSERT(m_szMultiple[nDir] == '|');
			m_szMultiple[nDir] = 0;
		}
		else
		{
			// We have an *empty* mask - we will display *no* files!
			m_pszMask = NULL;
			ASSERT(m_pszMultiple == NULL);
		}

		// If the directory mask contains anything - create a buffer for it - otherwise we will use the same buffers as the "file"
		int nLen = STRLEN(pszDir);

		if (nLen)
		{
			m_pszDirBuffer = new TCHAR[nLen+2];		// We may need to add 2 nuls to the end - this will be done when we parse the buffer later on
			STRCPY(m_pszDirBuffer, pszDir);
			m_pszMaskDir = m_pszDirBuffer;

			if (m_pszMultipleDir)
				m_pszMultipleDir = m_pszMaskDir;
		}
		else
		{
			// The dir mask is empty - display *no* dirs!
			fNoDirs = TRUE;
		}
	}

	if (m_pszMask)
		m_fMatchAll = (!STRCMP(m_pszMask, _T("*.*")) ||! STRCMP(m_pszMask, _T("*"))) ? TRUE : FALSE;
	else
		m_fMatchAll = FALSE;

	if (m_pszMaskDir)
		m_fMatchAllDir = (!STRCMP(m_pszMaskDir, _T("*.*")) ||! STRCMP(m_pszMaskDir, _T("*"))) ? TRUE : FALSE;
	else
		m_fMatchAllDir = !fNoDirs;
    
	if (m_pszMultiple)
		ParseMultiple(m_pszMultiple);
	
	if (m_pszMultipleDir && (m_pszMultipleDir != m_pszMultiple))
		ParseMultiple(m_pszMultipleDir);
}

void CFileMatcher::ParseMultiple(LPTSTR pszMultiple)
{
	LPTSTR psz;
    LPTSTR pszNext = pszMultiple;
	int    nLen    = lstrlen(pszMultiple);

    // Replace all commas (or separators) with a null

    while (1)
    {
        psz = STRCHR(pszNext, ',');

        if (psz == NULL)
            psz = STRCHR(pszNext, ';');

        if (psz == NULL)
            break;

        if ((psz != pszMultiple) && (psz[-1] != '\\'))
            *psz = 0;

        pszNext = psz + 1;
    }

    // Now doubly null terminate pszMultiple

    pszMultiple[nLen+1] = 0;
}

CFileMatcher::~CFileMatcher()
{
	delete [] m_pszDirBuffer;
}


/////////////////////////////////////////////////////////////////////////////
// CFileMatcher message handlers

BOOL CFileMatcher::Match(LPTSTR pszName, BOOL fDirectory)
{
	if ((fDirectory) ? m_fMatchAllDir : m_fMatchAll)
		return TRUE;

	LPTSTR pszMultiple;
	LPTSTR pszMask;

	if (fDirectory)
	{
		pszMultiple = m_pszMultipleDir;
		pszMask     = m_pszMaskDir;
	}
	else
	{
		pszMultiple = m_pszMultiple;
		pszMask     = m_pszMask;
	}

	if (pszMask == NULL)
		return FALSE;

    if (!pszMultiple)
		return !FileNameMatch(pszMask, pszName);

	BOOL fMatched = TRUE;
	BOOL fAnd     = FALSE;

    while (*pszMultiple)
    {
		if (*pszMultiple == '!')
			fAnd = TRUE;

		fMatched = !FileNameMatch(pszMultiple, pszName);

		// If we are ANDing, we need to match ALL masks, not just one of them

		if (fAnd)
		{
			if (!fMatched)
				break;
		}
		else

        if (fMatched)
            break;

        pszMultiple += STRLEN(pszMultiple) + 1;
    }

	return fMatched;
}


//*************************** FILE NAME MATCHING ********************************

#define FOLD(c)	((iswupper (c)) ? towlower (c) : (c))

int CFileMatcher::FileNameMatch(LPCTSTR pattern, LPCTSTR string, BOOL fMatchedDot)
{
	// In V7, changed all chars to unsigned chars
	//
	// In V8 we added support for "null" extensions by specifying a mask that ends with a "." (added fMatchedDot parameter)
	// That is, "*." specifies all files with no extension

    register const unsigned short *p = (unsigned short *)pattern;
	register const unsigned short *n = (unsigned short *)string;
    register unsigned short c;

    BOOL fNot = (*p == '!') ? TRUE : FALSE;

    if (fNot)
        p++;

    while ((c = *p++) != '\0')
    {
        c = FOLD(c);

        switch (c)
	    {
	        case '?':
	            if (*n == '\0')
				{
					// Unlike the original version, '?' can match 0 occurences.

	                return (fNot) ? FNM_NOMATCH : FNM_MATCH;
				}
	            break;

	        case '\\':

	            //if (!(flags & FNM_NOESCAPE))
	            {
	                c = *p++;
	                c = FOLD(c);
	            }

	            if (FOLD(*n) != c)
	                return (fNot) ? FNM_MATCH : FNM_NOMATCH;

	            break;

	        case '*':
                // Added in V2002 - Check for *.*
                // If we are at a terminating *.*, ignore the final ".*" by advancing the
                // mask pointer two positions.
                // This has the effect of matching "abc" if we search for "a*.*", whereas before, it didn't.

                if ((p[0] == '.') && (p[1] == '*') && (p[2] == 0))
                    p += 2;

				// At this point, p points to the character in the mask *after* the "*"
	            for (c = *p++; ((c == '?') || (c == '*')); c = *p++, ++n)
                {
					// This seems to advance the pattern pointer past any following * or ? (eg, handles patterns like ** or *?)!
					//
	                if ((c == '?') && (*n == '\0'))
						return (fNot) ? FNM_MATCH : FNM_NOMATCH;
                }

	            if (c == '\0')
	                return (fNot) ? FNM_NOMATCH : FNM_MATCH;

				// c is now the character after the '*', p points to the character after that
				// n points to the remainder of the string that is to be matched (in the case of "*.", n will point to the *entire* file name)
				
	            {
	                unsigned short c1  = (c == '\\') ? *p : c;
					BOOL fNoExtension = ((c == '.') && (*p == 0)) ? TRUE : FALSE;
	                
					c1 = FOLD(c1);
	                for (--p; *n != '\0'; ++n)
                    {
						// If the character after the "*" is a "[" then we need to recursively call
						// FileNameMatch on the remainder of the pattern/string.
						// Similarly if the character after the "*" matches the current character in the string,
						// we recursively try to match the rest of the string
						//
						// Otherwise, we will keep going down the string until we find a match 

						unsigned short s = FOLD(*n);

						if (fNoExtension)
						{
							// Our pattern ends in "*." - we are only interested to see if the string has an extension or not
							// We just keep going along the string until we either find a '.' or it terminates

							ASSERT(c1 == c);
							ASSERT(c1 == '.');

							if (s == c1)
							{
								// We have found a dot - therefore, the file has an extension
								return (fNot) ? FNM_MATCH : FNM_NOMATCH;
							}
						}
						else

	                    if (((c == '[') || (s == c1)) && FileNameMatch((LPCTSTR)p, (LPCTSTR)n, fMatchedDot) == FNM_MATCH)
		                    return (fNot) ? FNM_NOMATCH : FNM_MATCH;

						// Set a flag to indicate that * has matched a '.' - we use this later when matching a trailing "." in a pattern
						if (s == '.')
							fMatchedDot = TRUE;
                    }

					// We come here if what comes after the "*" does not match anything in the string (eg, searching for "*a" in "xyz")
					if (fNoExtension)
					{
						// If we are searching for "*." we come here if we didn't find an extension in the string - therefore we have a match!
						return (fNot) ? FNM_NOMATCH : FNM_MATCH;
					}

	                return (fNot) ? FNM_MATCH : FNM_NOMATCH;
	            }

	        case '[':
	        {
	            // Nonzero if the sense of the character class is inverted.
	            register int not;

	            if (*n == '\0')
	                return (fNot) ? FNM_MATCH : FNM_NOMATCH;

	            not = (*p == '!' || *p == '^');
	            if (not)
	                ++p;

	            c = *p++;
	            for (;;)
	            {
		            register unsigned short cstart = c, cend = c;

		            if (c == '\\')
		                cstart = cend = *p++;

		            cstart = cend = FOLD(cstart);

		            if (c == '\0')
                    {
		                // Unterminated
		                return (fNot) ? FNM_MATCH : FNM_NOMATCH;
                    }

		            c = *p++;
		            c = FOLD(c);

		            if (c == '-' && *p != ']')
		            {
		                cend = *p++;
		                if (cend == '\\')
		                    cend = *p++;

		                if (cend == '\0')
		                    return (fNot) ? FNM_MATCH : FNM_NOMATCH;

		                cend = FOLD(cend);
		                c = *p++;
		            }

		            if (FOLD(*n) >= cstart && FOLD(*n) <= cend)
		                goto matched;

		            if (c == ']')
		                break;
	            }

	            if (!not)
	                return (fNot) ? FNM_MATCH : FNM_NOMATCH;

	            break;

	            matched:;
	            // Skip the rest of the [...] that already matched.

	            while (c != ']')
	            {
		            if (c == '\0')
                    {
		                // Unterminated
		                return (fNot) ? FNM_MATCH : FNM_NOMATCH;
                    }

		            c = *p++;
		            if (c == '\\')
		                ++p;	// XXX 1003.2d11 is unclear if this is right.
	            }

	            if (not)
	                return (fNot) ? FNM_MATCH : FNM_NOMATCH;
	        }
	        break;

			case '.':
			{
				// '.' case added in V8 to handle a "null' extension
				if (*p == 0)
				{
					// Our pattern ends with a '.' - we will treat this as specifying a file with "no" extension
					// Note that in Windows the names "ABC" and "ABC." are the same.
					//
					// We check fMatchedDot to see if we have previously matched a dot when using "*"
					// For example, if we filter on *a., we will get a match for "b.a" because the "*" will match "b."
					// However, we do not want this to match "b.a"!

					if ((*n == '\0') && !fMatchedDot)
						return (fNot) ? FNM_NOMATCH : FNM_MATCH;

					return (fNot) ? FNM_MATCH : FNM_NOMATCH;
				}

				// If the pattern doesn't *end* in a '.' we need to fall through to the default processing - ie, don't put a "break" here!
			}

	        default:
	            if (c != FOLD(*n))
	                return (fNot) ? FNM_MATCH : FNM_NOMATCH;
	    }

        ++n;
    }

    if (*n == '\0')
        return (fNot) ? FNM_NOMATCH : FNM_MATCH;

    return (fNot) ? FNM_MATCH : FNM_NOMATCH;
}

