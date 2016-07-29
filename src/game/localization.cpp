/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "localization.h"
#include <base/tl/algorithm.h>

#include <engine/shared/linereader.h>
#include <engine/console.h>
#include <engine/storage.h>

const char *Localize(const char *pStr)
{
	const char *pNewStr = g_Localization.FindString(str_quickhash(pStr));
	return pNewStr ? pNewStr : pStr;
}

CLocConstString::CLocConstString(const char *pStr)
{
	m_pDefaultStr = pStr;
	m_Hash = str_quickhash(m_pDefaultStr);
	m_Version = -1;
}

void CLocConstString::Reload()
{
	m_Version = g_Localization.Version();
	const char *pNewStr = g_Localization.FindString(m_Hash);
	m_pCurrentStr = pNewStr;
	if(!m_pCurrentStr)
		m_pCurrentStr = m_pDefaultStr;
}

CLocalizationDatabase::CLocalizationDatabase()
{
	m_VersionCounter = 0;
	m_CurrentVersion = 0;
}

void CLocalizationDatabase::AddString(const char *pOrgStr, const char *pNewStr)
{
	CString s;
	s.m_Hash = str_quickhash(pOrgStr);
	s.m_Replacement = *pNewStr ? pNewStr : pOrgStr;
	m_Strings.add(s);
}

bool CLocalizationDatabase::Load(const char *pFilename, IStorage *pStorage, IConsole *pConsole, const char* pLanguageCode)
{
	// empty string means unload
	if(pFilename[0] == 0)
	{
		m_Strings.clear();
		m_CurrentVersion = 0;
		return true;
	}

	IOHANDLE IoHandle = pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!IoHandle)
		return false;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "loaded '%s'", pFilename);
	pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "localization", aBuf);
	m_Strings.clear();

	int LanguageCodeSize = 0;
	if(pLanguageCode)
		LanguageCodeSize = str_length(pLanguageCode);

	char aOrigin[512];
	CLineReader LineReader;
	LineReader.Init(IoHandle);
	char *pLine;
	while((pLine = LineReader.Get()))
	{
		if(!str_length(pLine))
			continue;

		if(pLine[0] == '#') // skip comments
			continue;

		str_copy(aOrigin, pLine, sizeof(aOrigin));
		char *pReplacement = LineReader.Get();
		if(!pReplacement)
		{
			pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "localization", "unexpected end of file");
			break;
		}

		if(pLanguageCode)
		{
			int LanguageCodeSize = str_length(pLanguageCode);
			if(pReplacement[0] != '=' || pReplacement[1] != '=')
			{
				str_format(aBuf, sizeof(aBuf), "malform replacement line for '%s'", aOrigin);
				pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "localization", aBuf);
				continue;
			}
			
			//Normal line
			if(pReplacement[2] == ' ')
			{
				pReplacement += 3;
			}
			else
			{
				//Skip this line if the language is no the requested one
				if(str_comp_nocase_num(pReplacement+2, pLanguageCode, LanguageCodeSize) != 0)
					continue;
				
				if(pReplacement[LanguageCodeSize+2] != '=' || pReplacement[LanguageCodeSize+3] != '=' || pReplacement[LanguageCodeSize+4] != '=')
				{
					str_format(aBuf, sizeof(aBuf), "malform replacement line for '%s'", aOrigin);
					pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "localization", aBuf);
					continue;
				}
				
			}

			pReplacement += LanguageCodeSize+5;
		}
		else
		{
			if(pReplacement[0] != '=' || pReplacement[1] != '=' || pReplacement[2] != ' ')
			{
				str_format(aBuf, sizeof(aBuf), "malform replacement line for '%s'", aOrigin);
				pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "localization", aBuf);
				continue;
			}

			pReplacement += 3;
		}
		
		AddString(aOrigin, pReplacement);
	}
	io_close(IoHandle);

	m_CurrentVersion = ++m_VersionCounter;
	return true;
}

const char *CLocalizationDatabase::FindString(unsigned Hash)
{
	CString String;
	String.m_Hash = Hash;
	sorted_array<CString>::range r = ::find_binary(m_Strings.all(), String);
	if(r.empty())
		return 0;
	return r.front().m_Replacement;
}

CLocalizationDatabase g_Localization;
