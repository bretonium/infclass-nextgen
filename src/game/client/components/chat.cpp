#include <string.h> // strcmp

#include <engine/e_client_interface.h>
#include <game/generated/g_protocol.hpp>
#include <game/generated/gc_data.hpp>

#include <game/client/gameclient.hpp>

#include <game/client/components/sounds.hpp>

#include "chat.hpp"

void CHAT::on_reset()
{
	mode = MODE_NONE;
	for(int i = 0; i < MAX_LINES; i++)
		lines[i].tick = -1000000;
	current_line = 0;
}

void CHAT::con_say(void *result, void *user_data)
{
	((CHAT*)user_data)->say(0, console_arg_string(result, 0));
}

void CHAT::con_sayteam(void *result, void *user_data)
{
	((CHAT*)user_data)->say(1, console_arg_string(result, 0));
}

void CHAT::con_chat(void *result, void *user_data)
{
	const char *mode = console_arg_string(result, 0);
	if(strcmp(mode, "all") == 0)
		((CHAT*)user_data)->enable_mode(0);
	else if(strcmp(mode, "team") == 0)
		((CHAT*)user_data)->enable_mode(1);
	else
		dbg_msg("console", "expected all or team as mode");
}

void CHAT::on_init()
{
	MACRO_REGISTER_COMMAND("say", "r", con_say, this);
	MACRO_REGISTER_COMMAND("say_team", "r", con_sayteam, this);
	MACRO_REGISTER_COMMAND("chat", "s", con_chat, this);
}

bool CHAT::on_input(INPUT_EVENT e)
{
	if(mode == MODE_NONE)
		return false;

	if(e.flags&INPFLAG_PRESS && e.key == KEY_ESC)
		mode = MODE_NONE;
	else if(e.flags&INPFLAG_PRESS && (e.key == KEY_ENTER || e.key == KEY_KP_ENTER))
	{
		if(input.get_string()[0])
			gameclient.chat->say(mode == MODE_ALL ? 0 : 1, input.get_string());
		mode = MODE_NONE;
	}
	else
		input.process_input(e);
	
	return true;
}


void CHAT::enable_mode(int team)
{
	if(mode == MODE_NONE)
	{
		if(team)
			mode = MODE_TEAM;
		else
			mode = MODE_ALL;
		
		input.clear();
		inp_clear_events();
	}
}

void CHAT::on_message(int msgtype, void *rawmsg)
{
	if(msgtype == NETMSGTYPE_SV_CHAT)
	{
		NETMSG_SV_CHAT *msg = (NETMSG_SV_CHAT *)rawmsg;
		add_line(msg->cid, msg->team, msg->message);

		if(msg->cid >= 0)
			gameclient.sounds->play(SOUNDS::CHN_GUI, SOUND_CHAT_CLIENT, 0, vec2(0,0));
		else
			gameclient.sounds->play(SOUNDS::CHN_GUI, SOUND_CHAT_SERVER, 0, vec2(0,0));
	}
}

void CHAT::add_line(int client_id, int team, const char *line)
{
	current_line = (current_line+1)%MAX_LINES;
	lines[current_line].tick = client_tick();
	lines[current_line].client_id = client_id;
	lines[current_line].team = team;
	lines[current_line].name_color = -2;

	if(client_id == -1) // server message
	{
		str_copy(lines[current_line].name, "*** ", sizeof(lines[current_line].name));
		str_format(lines[current_line].text, sizeof(lines[current_line].text), "%s", line);
	}
	else
	{
		if(gameclient.clients[client_id].team == -1)
			lines[current_line].name_color = -1;

		if(gameclient.snap.gameobj && gameclient.snap.gameobj->flags&GAMEFLAG_TEAMS)
		{
			if(gameclient.clients[client_id].team == 0)
				lines[current_line].name_color = 0;
			else if(gameclient.clients[client_id].team == 1)
				lines[current_line].name_color = 1;
		}
		
		str_copy(lines[current_line].name, gameclient.clients[client_id].name, sizeof(lines[current_line].name));
		str_format(lines[current_line].text, sizeof(lines[current_line].text), ": %s", line);
	}
	
	dbg_msg("chat", "%s%s", lines[current_line].name, lines[current_line].text);
}

void CHAT::on_render()
{
	gfx_mapscreen(0,0,300*gfx_screenaspect(),300);
	float x = 10.0f;
	float y = 300.0f-30.0f;
	if(mode != MODE_NONE)
	{
		// render chat input
		TEXT_CURSOR cursor;
		gfx_text_set_cursor(&cursor, x, y, 8.0f, TEXTFLAG_RENDER);
		cursor.line_width = 300.0f;
		
		if(mode == MODE_ALL)
			gfx_text_ex(&cursor, "All: ", -1);
		else if(mode == MODE_TEAM)
			gfx_text_ex(&cursor, "Team: ", -1);
		else
			gfx_text_ex(&cursor, "Chat: ", -1);
			
		gfx_text_ex(&cursor, input.get_string(), input.cursor_offset());
		TEXT_CURSOR marker = cursor;
		gfx_text_ex(&marker, "|", -1);
		gfx_text_ex(&cursor, input.get_string()+input.cursor_offset(), -1);
	}

	y -= 8;

	int i;
	for(i = 0; i < MAX_LINES; i++)
	{
		int r = ((current_line-i)+MAX_LINES)%MAX_LINES;
		if(client_tick() > lines[r].tick+50*15)
			break;

		float begin = x;
		float fontsize = 8.0f;
		
		// get the y offset
		TEXT_CURSOR cursor;
		gfx_text_set_cursor(&cursor, begin, 0, fontsize, 0);
		cursor.line_width = 300.0f;
		gfx_text_ex(&cursor, lines[r].name, -1);
		gfx_text_ex(&cursor, lines[r].text, -1);
		y -= cursor.y + cursor.font_size;

		// reset the cursor
		gfx_text_set_cursor(&cursor, begin, y, fontsize, TEXTFLAG_RENDER);
		cursor.line_width = 300.0f;

		// render name
		gfx_text_color(0.8f,0.8f,0.8f,1);
		if(lines[r].client_id == -1)
			gfx_text_color(1,1,0.5f,1); // system
		else if(lines[r].team)
			gfx_text_color(0.45f,0.9f,0.45f,1); // team message
		else if(lines[r].name_color == 0)
			gfx_text_color(1.0f,0.5f,0.5f,1); // red
		else if(lines[r].name_color == 1)
			gfx_text_color(0.7f,0.7f,1.0f,1); // blue
		else if(lines[r].name_color == -1)
			gfx_text_color(0.75f,0.5f,0.75f, 1); // spectator
			
		// render name
		gfx_text_ex(&cursor, lines[r].name, -1);

		// render line
		gfx_text_color(1,1,1,1);
		if(lines[r].client_id == -1)
			gfx_text_color(1,1,0.5f,1); // system
		else if(lines[r].team)
			gfx_text_color(0.65f,1,0.65f,1); // team message

		gfx_text_ex(&cursor, lines[r].text, -1);
	}

	gfx_text_color(1,1,1,1);
}

void CHAT::say(int team, const char *line)
{
	// send chat message
	NETMSG_CL_SAY msg;
	msg.team = team;
	msg.message = line;
	msg.pack(MSGFLAG_VITAL);
	client_send_msg();
}
