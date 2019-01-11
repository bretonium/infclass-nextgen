/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_WHITE_HOLE_H
#define GAME_SERVER_ENTITIES_WHITE_HOLE_H

#include <game/server/entity.h>

class CWhiteHole : public CEntity
{
public:
	enum
	{
		NUM_PARTICLES = 330, // change this to create more or less particles for the visual white hole effect
		NUM_IDS = NUM_PARTICLES,
	};
	
private:
	void StartVisualEffect();
	void MoveParticles();
	void MovePlayers();

public:
	CWhiteHole(CGameWorld *pGameWorld, vec2 CenterPos, int OwnerClientID);
	virtual ~CWhiteHole();
	
	virtual void Snap(int SnappingClient);
	virtual void Reset();
	virtual void TickPaused();
	virtual void Tick();
	
	int GetOwner() const;
	int GetTick() { return m_LifeSpan; }
	
private:
	// physics
	const float m_PlayerPullStrength = 4.0f; // change this to define how strong the white hole pulls
	const float m_RadiusGrowthRate = 6.0f; // how fast the hole growths when it is created
	const float m_PlayerDrag = 0.9f;
	// visual - its recommended not to change this
	const float m_ParticleStartSpeed = 1.1f; 
	const float m_ParticleAcceleration = 1.01f;
	int m_ParticleStopTickTime; // when X time is left stop creating particles - close animation

	int m_IDs[NUM_IDS];
	vec2 m_ParticlePos[NUM_PARTICLES];
	vec2 m_ParticleVec[NUM_PARTICLES];

	bool isDieing;
	
public:
	int m_StartTick;
	int m_Owner;
	int m_LifeSpan;
	int m_Radius; // changes overtime - grows when created - shrinks when dieing
};

#endif


