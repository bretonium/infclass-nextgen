/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/system.h>

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/network.h>
#include <engine/console.h>


static int HostLookupThread(void *pUser)
{
	CHostLookup *pLookup = (CHostLookup *)pUser;
	net_host_lookup(pLookup->m_aHostname, &pLookup->m_Addr, NETTYPE_IPV4);
	return 0;
}

class CEngine : public IEngine
{
public:
	/*
	static void con_dbg_dumpmem(IConsole::IResult *result, void *user_data)
	{
		mem_debug_dump();
	}

	static void con_dbg_lognetwork(IConsole::IResult *result, void *user_data)
	{
		CNetBase::OpenLog("network_sent.dat", "network_recv.dat");
	}*/

	CEngine(const char *pAppname)
	{
		dbg_logger_stdout();
		dbg_logger_debugger();
	
		//
		dbg_msg("engine", "running on %s-%s-%s", CONF_FAMILY_STRING, CONF_PLATFORM_STRING, CONF_ARCH_STRING);
	#ifdef CONF_ARCH_ENDIAN_LITTLE
		dbg_msg("engine", "arch is little endian");
	#elif defined(CONF_ARCH_ENDIAN_BIG)
		dbg_msg("engine", "arch is big endian");
	#else
		dbg_msg("engine", "unknown endian");
	#endif

		// init the network
		net_init();
		CNetBase::Init();
	
		m_JobPool.Init(1);

		//MACRO_REGISTER_COMMAND("dbg_dumpmem", "", CFGFLAG_SERVER|CFGFLAG_CLIENT, con_dbg_dumpmem, 0x0, "Dump the memory");
		//MACRO_REGISTER_COMMAND("dbg_lognetwork", "", CFGFLAG_SERVER|CFGFLAG_CLIENT, con_dbg_lognetwork, 0x0, "Log the network");
	}

	void InitLogfile()
	{
		// open logfile if needed
		if(g_Config.m_Logfile[0])
			dbg_logger_file(g_Config.m_Logfile);
	}

	void HostLookup(CHostLookup *pLookup, const char *pHostname)
	{
		str_copy(pLookup->m_aHostname, pHostname, sizeof(pLookup->m_aHostname));
		AddJob(&pLookup->m_Job, HostLookupThread, pLookup);
	}

	void AddJob(CJob *pJob, JOBFUNC pfnFunc, void *pData)
	{
		if(g_Config.m_Debug)
			dbg_msg("engine", "job added");
		m_JobPool.Add(pJob, pfnFunc, pData);
	}
};

IEngine *CreateEngine(const char *pAppname) { return new CEngine(pAppname); }
