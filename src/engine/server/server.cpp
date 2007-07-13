#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <baselib/system.h>

#include <engine/interface.h>

#include <engine/packet.h>
#include <engine/snapshot.h>

#include <engine/compression.h>
#include <engine/versions.h>

#include <engine/network.h>
#include <engine/config.h>


namespace baselib {}
using namespace baselib;

static snapshot_builder builder;

void *snap_new_item(int type, int id, int size)
{
	dbg_assert(type >= 0 && type <=0xffff, "incorrect type");
	dbg_assert(id >= 0 && id <=0xffff, "incorrect id");
	return builder.new_item(type, id, size);
}

//
class client
{
public:
	enum
	{
		STATE_EMPTY = 0,
		STATE_CONNECTING = 1,
		STATE_INGAME = 2,
	};

	// connection state info
	int state;
	
	int last_acked_snapshot;
	snapshot_storage snapshots;

	char name[MAX_NAME_LENGTH];
	char clan[MAX_CLANNAME_LENGTH];

	bool is_empty() const { return state == STATE_EMPTY; }
	bool is_ingame() const { return state == STATE_INGAME; }
};

static client clients[MAX_CLIENTS];
static int current_tick = 0;
static int send_heartbeats = 1;
static net_server net;

int server_tick()
{
	return current_tick;
}

int server_tickspeed()
{
	return SERVER_TICK_SPEED;
}

int server_init()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		clients[i].state = client::STATE_EMPTY;
		clients[i].name[0] = 0;
		clients[i].clan[0] = 0;
		//clients[i].lastactivity = 0;
	}

	current_tick = 0;

	return 0;
}

int server_getclientinfo(int client_id, client_info *info)
{
	dbg_assert(client_id >= 0 && client_id < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(info != 0, "info can not be null");

	if(clients[client_id].is_ingame())
	{
		info->name = clients[client_id].name;
		info->latency = 0;
		return 1;
	}
	return 0;
}


int server_send_msg(int client_id)
{
	const msg_info *info = msg_get_info();
	NETPACKET packet;
	packet.client_id = client_id;
	packet.data = info->data;
	packet.data_size = info->size;

	if(info->flags&MSGFLAG_VITAL)	
		packet.flags = PACKETFLAG_VITAL;
	
	net.send(&packet);
	return 0;
}
	
// TODO: remove this class
class server
{
public:
	//socket_udp4 game_socket;

	const char *map_name;
	const char *server_name;
	int64 lasttick;
	int64 lastheartbeat;
	netaddr4 master_server;

	int biggest_snapshot;

	bool run(const char *servername, const char *mapname)
	{
		biggest_snapshot = 0;

		net_init(); // For Windows compatibility.
		map_name = mapname;
		server_name = servername;

		// load map
		if(!map_load(mapname))
		{
			dbg_msg("server", "failed to load map. mapname='%s'");
			return false;
		}

		// start server
		if(!net.open(8303, 0, 0))
		{
			dbg_msg("network/server", "couldn't open socket");
			return false;
		}

		//for(int i = 0; i < MAX_CLIENTS; i++)
			//dbg_msg("network/server", "\t%d: %d", i, clients[i].state);

		if (net_host_lookup(MASTER_SERVER_ADDRESS, MASTER_SERVER_PORT, &master_server) != 0)
		{
			// TODO: fix me
			//master_server = netaddr4(0, 0, 0, 0, 0);
		}

		mods_init();

		int64 time_per_tick = time_freq()/SERVER_TICK_SPEED;
		int64 time_per_heartbeat = time_freq() * 30;
		int64 starttime = time_get();
		//int64 lasttick = starttime;
		lasttick = starttime;
		lastheartbeat = 0;

		int64 reporttime = time_get();
		int64 reportinterval = time_freq()*3;

		int64 simulationtime = 0;
		int64 snaptime = 0;
		int64 networktime = 0;

		while(1)
		{
			int64 t = time_get();
			if(t-lasttick > time_per_tick)
			{
				{
					int64 start = time_get();
					tick();
					simulationtime += time_get()-start;
				}

				{
					int64 start = time_get();
					snap();
					snaptime += time_get()-start;
				}

				lasttick += time_per_tick;
			}

			if(send_heartbeats)
			{
				if (t > lastheartbeat+time_per_heartbeat)
				{
					if (master_server.port != 0)
					{
						int players = 0;

						for (int i = 0; i < MAX_CLIENTS; i++)
							if (!clients[i].is_empty())
								players++;

						// TODO: fix me
						netaddr4 me(127, 0, 0, 0, 8303);
						//send_heartbeat(0, &me, players, MAX_CLIENTS, server_name, mapname);
					}

					lastheartbeat = t+time_per_heartbeat;
				}
			}

			{
				int64 start = time_get();
				pump_network();
				networktime += time_get()-start;
			}

			if(reporttime < time_get())
			{
				int64 totaltime = simulationtime+snaptime+networktime;
				dbg_msg("server/report", "sim=%.02fms snap=%.02fms net=%.02fms total=%.02fms load=%.02f%%",
					simulationtime/(float)reportinterval*1000,
					snaptime/(float)reportinterval*1000,
					networktime/(float)reportinterval*1000,
					totaltime/(float)reportinterval*1000,
					(simulationtime+snaptime+networktime)/(float)reportinterval*100.0f);

				unsigned sent_total=0, recv_total=0;
				/*
				for (int i = 0; i < MAX_CLIENTS; i++)
					if (!clients[i].is_empty())
					{
						unsigned s,r;
						clients[i].conn.counter_get(&s,&r);
						clients[i].conn.counter_reset();
						sent_total += s;
						recv_total += r;
					}
				*/

				dbg_msg("server/report", "biggestsnap=%d send=%d recv=%d",
					biggest_snapshot, sent_total/3, recv_total/3);

				simulationtime = 0;
				snaptime = 0;
				networktime = 0;

				reporttime += reportinterval;
			}

			thread_sleep(1);
		}

		mods_shutdown();
		map_unload();
	}

	void tick()
	{
		current_tick++;
		mods_tick();
	}

	void snap()
	{
		//if(current_tick&1)
		//	return;
		mods_presnap();

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(clients[i].is_ingame())
			{
				char data[MAX_SNAPSHOT_SIZE];
				char deltadata[MAX_SNAPSHOT_SIZE];
				char compdata[MAX_SNAPSHOT_SIZE];
				//char intdata[MAX_SNAPSHOT_SIZE];
				builder.start();
				mods_snap(i);

				// finish snapshot
				int snapshot_size = builder.finish(data);

				// remove old snapshos
				// keep 1 seconds worth of snapshots
				clients[i].snapshots.purge_until(current_tick-SERVER_TICK_SPEED);
				
				// save it the snapshot
				clients[i].snapshots.add(current_tick, snapshot_size, data);
				
				// find snapshot that we can preform delta against
				static snapshot emptysnap;
				emptysnap.data_size = 0;
				emptysnap.num_items = 0;
				
				snapshot *deltashot = &emptysnap;
				int deltashot_size;
				int delta_tick = -1;
				{
					void *delta_data;
					deltashot_size = clients[i].snapshots.get(clients[i].last_acked_snapshot, (void **)&delta_data);
					if(deltashot_size >= 0)
					{
						delta_tick = clients[i].last_acked_snapshot;
						deltashot = (snapshot *)delta_data;
					}
				}
				
				// create delta
				int deltasize = snapshot_create_delta(deltashot, (snapshot*)data, deltadata);
				
				if(deltasize)
				{
					// compress it
					//int intsize = -1;
					unsigned char intdata[MAX_SNAPSHOT_SIZE];
					int intsize = intpack_compress(deltadata, deltasize, intdata);
					
					int compsize = zerobit_compress(intdata, intsize, compdata);
					//dbg_msg("compress", "%5d --delta-> %5d --int-> %5d --zero-> %5d %5d",
						//snapshot_size, deltasize, intsize, compsize, intsize-compsize);
					snapshot_size = compsize;

					if(snapshot_size > biggest_snapshot)
						biggest_snapshot = snapshot_size;

					const int max_size = MAX_SNAPSHOT_PACKSIZE;
					int numpackets = (snapshot_size+max_size-1)/max_size;
					for(int n = 0, left = snapshot_size; left; n++)
					{
						int chunk = left < max_size ? left : max_size;
						left -= chunk;

						if(numpackets == 1)
							msg_pack_start(NETMSG_SNAPSMALL, 0);
						else
							msg_pack_start(NETMSG_SNAP, 0);
						msg_pack_int(current_tick);
						msg_pack_int(current_tick-delta_tick); // compressed with
						msg_pack_int(chunk);
						msg_pack_raw(&compdata[n*max_size], chunk);
						msg_pack_end();
						//const msg_info *info = msg_get_info();
						//dbg_msg("server", "size=%d", info->size);
						server_send_msg(i);
					}
				}
				else
				{
					msg_pack_start(NETMSG_SNAPEMPTY, 0);
					msg_pack_int(current_tick);
					msg_pack_int(current_tick-delta_tick); // compressed with
					msg_pack_end();
					server_send_msg(i);
				}
			}
		}

		mods_postsnap();
	}

	void send_map(int cid)
	{
		msg_pack_start(NETMSG_MAP, MSGFLAG_VITAL);
		msg_pack_string(map_name, 0);
		msg_pack_end();
		server_send_msg(cid);
	}

	void drop(int cid, const char *reason)
	{
		if(clients[cid].state == client::STATE_EMPTY)
			return;

		clients[cid].state = client::STATE_EMPTY;
		mods_client_drop(cid);
		dbg_msg("game", "player dropped. reason='%s' cid=%x name='%s'", reason, cid, clients[cid].name);
	}

	void process_client_packet(NETPACKET *packet)
	{
		int cid = packet->client_id;
		int msg = msg_unpack_start(packet->data, packet->data_size);
		if(msg == NETMSG_INFO)
		{
			strncpy(clients[cid].name, msg_unpack_string(), MAX_NAME_LENGTH);
			strncpy(clients[cid].clan, msg_unpack_string(), MAX_CLANNAME_LENGTH);
			const char *password = msg_unpack_string();
			const char *skin = msg_unpack_string();
			(void)password; // ignore these variables
			(void)skin;
			send_map(cid);
		}
		else if(msg == NETMSG_ENTERGAME)
		{
			dbg_msg("game", "player as entered the game. cid=%x", cid);
			clients[cid].state = client::STATE_INGAME;
			mods_client_enter(cid);
		}
		else if(msg == NETMSG_INPUT)
		{
			int input[MAX_INPUT_SIZE];
			int size = msg_unpack_int();
			for(int i = 0; i < size/4; i++)
				input[i] = msg_unpack_int();
			mods_client_input(cid, input);
		}
		else if(msg == NETMSG_SNAPACK)
		{
			clients[cid].last_acked_snapshot = msg_unpack_int();
		}
		else
		{
			dbg_msg("server", "strange message cid=%d msg=%d data_size=%d", cid, msg, packet->data_size);
		}
		
	}

	void process_packet(NETPACKET *packet)
	{
	}

	void client_timeout(int clientId)
	{
		drop(clientId, "client timedout");
	}

	void pump_network()
	{
		net.update();
		
		// process packets
		NETPACKET packet;
		while(net.recv(&packet))
		{
			
			if(packet.client_id == -1)
			{
				// stateless
			}
			else
				process_client_packet(&packet);
		}
		
		// check for removed clients
		while(1)
		{
			int cid = net.delclient();
			if(cid == -1)
				break;
			
			clients[cid].state = client::STATE_EMPTY;
			clients[cid].name[0] = 0;
			clients[cid].clan[0] = 0;
			clients[cid].snapshots.purge_all();
			
			mods_client_drop(cid);
			
			dbg_msg("server", "del client %d", cid);
		}
		
		// check for new clients
		while(1)
		{
			int cid = net.newclient();
			if(cid == -1)
				break;
			
			clients[cid].state = client::STATE_CONNECTING;
			clients[cid].name[0] = 0;
			clients[cid].clan[0] = 0;
			clients[cid].snapshots.purge_all();
			clients[cid].last_acked_snapshot = -1;
			
			dbg_msg("server", "new client %d", cid);
		}
	}
};

int main(int argc, char **argv)
{
	dbg_msg("server", "starting...");

	dbg_msg("server", "%d %d", sizeof(snapshot), sizeof(snapshot::item));

	config_reset();
	config_load("server.cfg");

	const char *mapname = "data/demo.map";
	const char *servername = 0;
	
	// parse arguments
	for(int i = 1; i < argc; i++)
	{
		if(argv[i][0] == '-' && argv[i][1] == 'm' && argv[i][2] == 0 && argc - i > 1)
		{
			// -m map
			i++;
			mapname = argv[i];
		}
		else if(argv[i][0] == '-' && argv[i][1] == 'n' && argv[i][2] == 0 && argc - i > 1)
		{
			// -n server name
			i++;
			servername = argv[i];
		}
		else if(argv[i][0] == '-' && argv[i][1] == 'p' && argv[i][2] == 0)
		{
			// -p (private server)
			send_heartbeats = 0;
		}
		else if(argv[i][0] == '-' && argv[i][1] == 'o' && argv[i][2] == 0)
		{
			// -o port
			i++;
			config_set_sv_port(&config, atol(argv[i]));
		}
	}

	if(!mapname)
	{
		dbg_msg("server", "no map given (-m MAPNAME)");
		return 0;
	}

	if(!servername)
	{
		dbg_msg("server", "no server name given (-n \"server name\")");
		return 0;
	}

	server_init();
	server s;
	s.run(servername, mapname);
	return 0;
}
