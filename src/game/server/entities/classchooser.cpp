/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include "classchooser.h"

CClassChooser::CClassChooser(CGameWorld *pGameWorld, vec2 Pos, int pId)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_CLASSCHOOSER)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	for(int i=0; i<END_HUMANCLASS - START_HUMANCLASS - 1; i++)
	{
		m_IDClass[i] = Server()->SnapNewID();
	}
	m_PlayerID = pId;
}

void CClassChooser::Destroy()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_PlayerID);
	if(OwnerChar && OwnerChar->m_pClassChooser == this)
	{
		OwnerChar->m_pClassChooser = 0;
	}
	
	for(int i=0; i<END_HUMANCLASS - START_HUMANCLASS - 1; i++)
	{
		Server()->SnapFreeID(m_IDClass[i]);
	}
	delete this;
}

void CClassChooser::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CClassChooser::SetCursor(vec2 CurPos)
{
	m_CurPos = CurPos;
}

int CClassChooser::SelectClass()
{
	if(length(m_CurPos) >= 200.0)
		return 0;

	if(length(m_CurPos) > 50.0)
	{
		int NbChoosableClass = 0;
		for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
		{
			if(GameServer()->m_pController->IsChoosableClass(i))
			{
				NbChoosableClass++;
			}
		}

		if(NbChoosableClass <= 0)
			return 0;

		float angle = atan2(-m_CurPos.y, -m_CurPos.x);
		int Selection = static_cast<int>(NbChoosableClass*(angle/pi));
		
		if(Selection < 0 || Selection >= NbChoosableClass)
			return 0;
		
		int ClassIter = 0;
		for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
		{
			if(GameServer()->m_pController->IsChoosableClass(i))
			{
				if(ClassIter == Selection) return i;
				else ClassIter++;
			}
		}
	}
	else
	{
		CPlayer* pPlayer = GameServer()->m_apPlayers[m_PlayerID];
		if(pPlayer)
		{
			return GameServer()->m_pController->ChooseHumanClass(pPlayer);
		}
	}
	
	return 0;
}

void CClassChooser::Snap(int SnappingClient)
{	
	if(NetworkClipped(SnappingClient) || SnappingClient != m_PlayerID)
		return;
	
	int NbChoosableClass = 0;
	for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
	{
		if(GameServer()->m_pController->IsChoosableClass(i))
		{
			NbChoosableClass++;
		}
	}	
	
	if(NbChoosableClass <= 0)
		return;
	
	float stepAngle = pi/static_cast<float>(NbChoosableClass);
	
	int ClassIterator = 0;
	for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
	{
		if(!GameServer()->m_pController->IsChoosableClass(i))
			continue;
		
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDClass[i - START_HUMANCLASS - 1], sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x - 130.0*cos((static_cast<float>(ClassIterator)+0.5)*stepAngle);
		pP->m_Y = (int)m_Pos.y - 130.0*sin((static_cast<float>(ClassIterator)+0.5)*stepAngle);
		pP->m_Type = POWERUP_WEAPON;
		
		switch(i)
		{
			case PLAYERCLASS_SOLDIER:
				pP->m_Subtype = WEAPON_GRENADE;
				break;
			case PLAYERCLASS_MEDIC:
				pP->m_Subtype = WEAPON_HAMMER;
				break;
			case PLAYERCLASS_SCIENTIST:
				pP->m_Subtype = WEAPON_SHOTGUN;
				break;
			case PLAYERCLASS_ENGINEER:
				pP->m_Subtype = WEAPON_RIFLE;
				break;
			case PLAYERCLASS_NINJA:
				pP->m_Subtype = WEAPON_NINJA;
				break;
		}
		
		ClassIterator++;
	}
	
	if(length(m_CurPos) < 200.0)
	{
		float angle = atan2(-m_CurPos.y, -m_CurPos.x);
		
		int i=0;
		
		if(length(m_CurPos) > 50.0)
		{
			for(i=NbChoosableClass-1; i>=0; i--)
			{
				if(angle >= static_cast<float>(i)*pi/static_cast<float>(NbChoosableClass)) break;
			}
			
			switch(START_HUMANCLASS+i+1)
			{
				case PLAYERCLASS_SOLDIER:
					GameServer()->SendBroadcast("Soldier", m_PlayerID);
					break;
				case PLAYERCLASS_MEDIC:
					GameServer()->SendBroadcast("Medic", m_PlayerID);
					break;
				case PLAYERCLASS_SCIENTIST:
					GameServer()->SendBroadcast("Scientist", m_PlayerID);
					break;
				case PLAYERCLASS_ENGINEER:
					GameServer()->SendBroadcast("Engineer", m_PlayerID);
					break;
				case PLAYERCLASS_NINJA:
					GameServer()->SendBroadcast("Ninja", m_PlayerID);
					break;
			}
			
		}
		else
		{
			GameServer()->SendBroadcast("Random selection", m_PlayerID);
			
			i = rand()%NbChoosableClass;
		}
		
		if(i>=0 && i<NbChoosableClass)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;
				
			
			pObj->m_X = (int)m_Pos.x - 130.0*cos((static_cast<float>(i)+0.5)*stepAngle);
			pObj->m_Y = (int)m_Pos.y - 130.0*sin((static_cast<float>(i)+0.5)*stepAngle);

			pObj->m_FromX = (int)m_Pos.x;
			pObj->m_FromY = (int)m_Pos.y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
	else
	{
		GameServer()->SendBroadcast("Choose your class by clicking on the weapon, or wait random selection", m_PlayerID);
	}
}
