/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/config.h>

#include "netban.h"
#include "network.h"
#include "protocol.h"


bool CNetServer::Open(NETADDR BindAddr, CNetBan *pNetBan, int MaxClients, int MaxClientsPerIP, int Flags)
{
	// zero out the whole structure
	mem_zero(this, sizeof(*this));

	// open socket
	m_Socket = net_udp_create(BindAddr);
	if(!m_Socket.type)
		return false;

	m_pNetBan = pNetBan;

	// clamp clients
	m_MaxClients = MaxClients;
	if(m_MaxClients > NET_MAX_CLIENTS)
		m_MaxClients = NET_MAX_CLIENTS;
	if(m_MaxClients < 1)
		m_MaxClients = 1;

	m_MaxClientsPerIP = MaxClientsPerIP;

	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		m_aSlots[i].m_Connection.Init(m_Socket, true);

	return true;
}

int CNetServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_UserPtr = pUser;
	return 0;
}

int CNetServer::Close()
{
	// TODO: implement me
	return 0;
}

int CNetServer::Drop(int ClientID, int Type, const char *pReason)
{
	// TODO: insert lots of checks here
	/*NETADDR Addr = ClientAddr(ClientID);

	dbg_msg("net_server", "client dropped. cid=%d ip=%d.%d.%d.%d reason=\"%s\"",
		ClientID,
		Addr.ip[0], Addr.ip[1], Addr.ip[2], Addr.ip[3],
		pReason
		);*/
	if(m_pfnDelClient)
		m_pfnDelClient(ClientID, Type, pReason, m_UserPtr);

	m_aSlots[ClientID].m_Connection.Disconnect(pReason);

	return 0;
}

int CNetServer::Update()
{
	int64 Now = time_get();
	for(int i = 0; i < MaxClients(); i++)
	{
		m_aSlots[i].m_Connection.Update();
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR)
		{
			if(Now - m_aSlots[i].m_Connection.ConnectTime() < time_freq() && NetBan())
				NetBan()->BanAddr(ClientAddr(i), 60, "Stressing network");
			else
				Drop(i, CLIENTDROPTYPE_STRESSING, m_aSlots[i].m_Connection.ErrorString());
		}
	}

	return 0;
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk)
{
	while(1)
	{
		NETADDR Addr;

		// check for a chunk
		if(m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		int Bytes = net_udp_recv(m_Socket, &Addr, m_RecvUnpacker.m_aBuffer, NET_MAX_PACKETSIZE);

		// no more packets for now
		if(Bytes <= 0)
			break;
							
		if(CNetBase::UnpackPacket(m_RecvUnpacker.m_aBuffer, Bytes, &m_RecvUnpacker.m_Data) == 0)
		{
			// check if we just should drop the packet
			char aBuf[128];
			if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf)))
			{
				// banned, reply with a message
				CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf)+1);
				continue;
			}

			if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONNLESS)
			{
				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				pChunk->m_ClientID = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
				pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
				return 1;
			}
			else
			{
				//If the message is NET_PACKETFLAG_CONTROL, send fake
				//control message to force the client to send a NETMSG_INFO	
				if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONTROL && m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_CONNECT)
				{
					CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CONNECTACCEPT, 0, 0);
				}
				else
				{
					int ClientID = -1;
					
					for(int i = 0; i < MaxClients(); i++)
					{
						if(m_aSlots[i].m_Connection.State() != NET_CONNSTATE_OFFLINE &&
							net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), &Addr) == 0)
						{
							ClientID = i;
							break;
						}
					}
					
					//The client is unknown, check for NETMSG_INFO
					if(ClientID < 0)
					{
						//If the message is NETMSG_INFO, connect the client.
						static const char m_NetMsgInfoHeader1[] = { 0x00, 0x00, 0x01 };
						static const char m_NetMsgInfoHeader2[] = { 0x01, 0x03 };
						if(Bytes > 10
							&& (mem_comp(m_RecvUnpacker.m_aBuffer, m_NetMsgInfoHeader1, sizeof(m_NetMsgInfoHeader1)/sizeof(unsigned char)) == 0)
							&& (mem_comp(m_RecvUnpacker.m_aBuffer+5, m_NetMsgInfoHeader2, sizeof(m_NetMsgInfoHeader2)/sizeof(unsigned char)) == 0)
							&& m_RecvUnpacker.m_aBuffer[Bytes-1] == 0
						)
						{
							//Check password if captcha are enabled
							if(g_Config.m_InfCaptcha)
							{
								const unsigned char* pPassword = 0;
								int Iter = 7; //Skin the header and the ID
								while(m_RecvUnpacker.m_aBuffer[Iter] != 0 && Iter < Bytes)
									++Iter;
								if(Iter+1 < Bytes)
									pPassword = m_RecvUnpacker.m_aBuffer+Iter+1;
								
								if(!pPassword || (str_comp(GetCaptcha(&Addr, true), (const char*) pPassword) != 0))
								{
									const char WrongPasswordMsg[] = "Wrong password";
									CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, WrongPasswordMsg, sizeof(WrongPasswordMsg));
									return 0;
								}
							}
							
							// only allow a specific number of players with the same ip
							NETADDR ThisAddr = Addr, OtherAddr;
							int FoundAddr = 1;
							ThisAddr.port = 0;
							for(int i = 0; i < MaxClients(); ++i)
							{
								if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
									continue;

								OtherAddr = *m_aSlots[i].m_Connection.PeerAddress();
								OtherAddr.port = 0;
								if(!net_addr_comp(&ThisAddr, &OtherAddr))
								{
									if(FoundAddr++ >= m_MaxClientsPerIP)
									{
										char aBuf[128];
										str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIP);
										CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, sizeof(aBuf));
										return 0;
									}
								}
							}

							bool Found = false;
							for(int i = 0; i < MaxClients(); i++)
							{
								if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
								{
									Found = true;
									m_aSlots[i].m_Connection.SimulateConnexionWithInfo(&Addr);
									if(m_pfnNewClient)
										m_pfnNewClient(i, m_UserPtr);
									ClientID = i;
									break;
								}
							}

							if(!Found)
							{
								const char FullMsg[] = "This server is full";
								CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, FullMsg, sizeof(FullMsg));
							}
						}
						//Otherwise, just drop the message
					}
					
					if(ClientID >= 0)
					{
						if(m_aSlots[ClientID].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr))
						{
							if(m_RecvUnpacker.m_Data.m_DataSize)
								m_RecvUnpacker.Start(&Addr, &m_aSlots[ClientID].m_Connection, ClientID);
						}
					}
				}
			}
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk)
{
	if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
	{
		dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
		return -1;
	}

	if(pChunk->m_Flags&NETSENDFLAG_CONNLESS)
	{
		// send connectionless packet
		CNetBase::SendPacketConnless(m_Socket, &pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize);
	}
	else
	{
		int Flags = 0;
		dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
		dbg_assert(pChunk->m_ClientID < MaxClients(), "errornous client id");

		if(pChunk->m_Flags&NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientID].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags&NETSENDFLAG_FLUSH)
				m_aSlots[pChunk->m_ClientID].m_Connection.Flush();
		}
		else
		{
			Drop(pChunk->m_ClientID, CLIENTDROPTYPE_ERROR, "Error sending data");
		}
	}
	return 0;
}

void CNetServer::SetMaxClientsPerIP(int Max)
{
	// clamp
	if(Max < 1)
		Max = 1;
	else if(Max > NET_MAX_CLIENTS)
		Max = NET_MAX_CLIENTS;

	m_MaxClientsPerIP = Max;
}

const char* CNetServer::GetCaptcha(const NETADDR* pAddr, bool Debug)
{
	if(!m_lCaptcha.size())
		return "???";
	
	int IpHash = 0;
	for(int i=0; i<4; i++)
	{
		IpHash |= (pAddr->ip[i]<<(i*8));
	}
	
	int CaptchaId = IpHash%m_lCaptcha.size();
	
	if(Debug)
	{
		char aBuf[64];
		net_addr_str(pAddr, aBuf, 64, 0);
		dbg_msg("InfClass", "GetCaptcha: %s -> %d -> %d", aBuf, IpHash, CaptchaId);
	}
	
	return m_lCaptcha[CaptchaId].m_aText;
}

void CNetServer::AddCaptcha(const char* pText)
{
	CCaptcha Captcha;
	str_copy(Captcha.m_aText, pText, sizeof(Captcha));
	m_lCaptcha.add(Captcha);
}
