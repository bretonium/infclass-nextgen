/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <iostream>
#include <engine/shared/config.h>
#include "player.h"


MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	m_pCharacter = 0;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();
	
/* INFECTION MODIFICATION START ***************************************/
	m_Score = Server()->GetClientScore(ClientID);
	m_ScoreRound = 0;
	m_ScoreMode = PLAYERSCOREMODE_NORMAL;
	m_WinAsHuman = 0;
	m_class = PLAYERCLASS_NONE;
	m_InfectionTick = -1;
	for(int i=0; i<NB_PLAYERCLASS; i++)
	{
		m_knownClass[i] = false;
	}
	
/* INFECTION MODIFICATION END *****************************************/
}

CPlayer::~CPlayer()
{
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	Server()->SetClientScore(m_ClientID, m_Score);

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(!GameServer()->m_World.m_Paused)
	{
		if(!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
			m_ViewPos -= vec2(clamp(m_ViewPos.x-m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y-m_LatestActivity.m_TargetY, -400.0f, 400.0f));

		if(!m_pCharacter && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
			m_Spawning = true;

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
			{
				m_ViewPos = m_pCharacter->m_Pos;
			}
			else
			{
				m_pCharacter->Destroy();
				delete m_pCharacter;
				m_pCharacter = 0;
			}
		}
		else if(m_Spawning && m_RespawnTick <= Server()->Tick())
			TryRespawn();
		
		if(!IsInfected()) m_HumanTime++;
	}
	else
	{
		++m_RespawnTick;
		++m_DieTick;
		++m_ScoreStartTick;
		++m_LastActionTick;
		++m_TeamChangeTick;
 	}
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if(m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::Snap(int SnappingClient)
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, m_ClientID, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	
/* INFECTION MODIFICATION STRAT ***************************************/
	if(GetTeam() == TEAM_SPECTATORS)
	{
		StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	}
	else
	{
		if(m_ScoreMode == PLAYERSCOREMODE_ROUNDSCORE)
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%s%i pt", (m_ScoreRound > 0 ? "+" : ""), m_ScoreRound);
			
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
		}
		else if(m_ScoreMode == PLAYERSCOREMODE_TIME)
		{
			float RoundDuration = static_cast<float>(m_HumanTime/((float)Server()->TickSpeed()))/60.0f;
			int Minutes = static_cast<int>(RoundDuration);
			int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes,((Seconds < 10) ? "0" : ""), Seconds);
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
		}
		else
		{
			switch(GetClass())
			{
				case PLAYERCLASS_ENGINEER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Engineer*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Engineer");
					break;
				case PLAYERCLASS_SOLDIER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Soldier*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Soldier");
					break;
				case PLAYERCLASS_SCIENTIST:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Scientist*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Scientist");
					break;
				case PLAYERCLASS_MEDIC:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Medic*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Medic");
					break;
				case PLAYERCLASS_NINJA:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Ninja*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Ninja");
					break;
				case PLAYERCLASS_SMOKER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Smoker*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Smoker");
					break;
				case PLAYERCLASS_BOOMER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Boomer*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Boomer");
					break;
				case PLAYERCLASS_HUNTER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Hunter*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Hunter");
					break;
				case PLAYERCLASS_UNDEAD:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Undead*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Undead");
					break;
				case PLAYERCLASS_WITCH:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "*Witch*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Witch");
					break;
				default:
					StrToInts(&pClientInfo->m_Clan0, 3, "");
			}
		}
	}
	
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

	if(
		GameServer()->m_apPlayers[SnappingClient] && !IsInfected() &&
		(
			(Server()->GetClientCustomSkin(SnappingClient) == 1 && SnappingClient == GetCID()) ||
			(Server()->GetClientCustomSkin(SnappingClient) == 2)
		)
	)
	{
		StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_CustomSkinName);
	}
	else StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
	
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;
/* INFECTION MODIFICATION END *****************************************/

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = m_ClientID;
/* INFECTION MODIFICATION START ***************************************/
	switch(m_ScoreMode)
	{
		case PLAYERSCOREMODE_NORMAL:
			pPlayerInfo->m_Score = m_Score;
			break;
		case PLAYERSCOREMODE_ROUNDSCORE:
			pPlayerInfo->m_Score = m_ScoreRound;
			break;
		case PLAYERSCOREMODE_TIME:
			pPlayerInfo->m_Score = m_HumanTime/Server()->TickSpeed();
			break;
	}
/* INFECTION MODIFICATION END *****************************************/
	pPlayerInfo->m_Team = m_Team;

	if(m_ClientID == SnappingClient)
		pPlayerInfo->m_Local = 1;

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::OnDisconnect(const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		char aBuf[512];
		if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(m_ClientID), pReason);
		else
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(m_ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", m_ClientID, Server()->ClientName(m_ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags&PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags&PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
 		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire&1))
		m_Spawning = true;

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire&1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::Respawn()
{
	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	// clamp the team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;

	char aBuf[512];
	if(DoChatMsg)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(m_ClientID), GameServer()->m_pController->GetTeamName(Team));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;

/* INFECTION MODIFICATION START ***************************************/
	if(!GameServer()->m_pController->PreSpawn(this, &SpawnPos))
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos);
}

/* INFECTION MODIFICATION START ***************************************/
int CPlayer::GetClass()
{
	return m_class;
}

void CPlayer::SetClassSkin(int newClass)
{
	switch(newClass)
	{
		case PLAYERCLASS_ENGINEER:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "limekitty", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_SOLDIER:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "brownbear", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_SCIENTIST:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "toptri", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_MEDIC:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "twinbop", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_NINJA:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "default", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 255;
			m_TeeInfos.m_ColorFeet = 0;
			break;
		case PLAYERCLASS_SMOKER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "cammostripes", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_BOOMER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "saddo", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_HUNTER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "warpaint", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_UNDEAD:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "redstripe", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3014400;
			m_TeeInfos.m_ColorFeet = 13168;
			break;
		case PLAYERCLASS_WITCH:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "redbopp", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 16776744;
			m_TeeInfos.m_ColorFeet = 13168;
			break;
		default:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "default", sizeof(m_TeeInfos.m_SkinName));
			Server()->SetClientClan(GetCID(), "");
	}
}

void CPlayer::SetClass(int newClass)
{	
	if(m_class == newClass)
		return;
	
	m_class = newClass;
	
	SetClassSkin(newClass);
	
	if(m_pCharacter)
	{
		m_pCharacter->SetClass(newClass);
	}
}

void CPlayer::StartInfection(bool force)
{
	if(!force && IsInfected())
		return;
	
	
	if(!IsInfected())
	{
		m_InfectionTick = Server()->Tick();
	}
	
	int c = GameServer()->m_pController->ChooseInfectedClass(this);
	
	SetClass(c);
}

bool CPlayer::IsInfected() const
{
	return (m_class > END_HUMANCLASS);
}

bool CPlayer::IsKownClass(int c)
{
	return m_knownClass[c];
}

void CPlayer::IncreaseScore(int Points)
{
	m_Score += Points;
	m_ScoreRound += Points;
}

void CPlayer::SetScoreMode(int Mode)
{
	m_ScoreMode = Mode;
}
/* INFECTION MODIFICATION END *****************************************/
