/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "e_system.h"
#include "e_config.h"
#include "e_linereader.h"

CONFIGURATION config;

void config_reset()
{
    #define MACRO_CONFIG_INT(name,def,min,max) config.name = def;
    #define MACRO_CONFIG_STR(name,len,def) str_copy(config.name, def, len);
 
    #include "e_config_variables.h" 
 
    #undef MACRO_CONFIG_INT 
    #undef MACRO_CONFIG_STR 
}

void strip_spaces(char **p)
{
	char *s = *p;
	char *end;
	
	while (*s == ' ')
		++s;

	end = s + strlen(s);
	while (end > s && *(end - 1) == ' ')
		*--end = 0;
}

void config_set(const char *line)
{
	const char *c = strchr(line, '=');
	if (c)
	{
		char var[256];
		char val[256];
		char *var_str = var;
		char *val_str = val;

		str_copy(val, c+1, sizeof(val));
		mem_copy(var, line, c - line);
		var[c - line] = 0;

		strip_spaces(&var_str);
		strip_spaces(&val_str);

		#define MACRO_CONFIG_INT(name,def,min,max) { if (strcmp(#name, var_str) == 0) config_set_ ## name (&config, atoi(val_str)); }
    	#define MACRO_CONFIG_STR(name,len,def) { if (strcmp(#name, var_str) == 0) { config_set_ ## name (&config, val_str); } }
 
    	#include "e_config_variables.h" 
 
    	#undef MACRO_CONFIG_INT 
    	#undef MACRO_CONFIG_STR 
	}
}

void config_load(const char *filename)
{
	IOHANDLE file;
	dbg_msg("config/load", "loading %s", filename);
	file = io_open(filename, IOFLAG_READ);
	
	if(file)
	{
		char *line;
		LINEREADER lr;
		linereader_init(&lr, file);

		while ((line = linereader_get(&lr)))
			config_set(line);

		io_close(file);
	}
}

void config_save(const char *filename)
{
	IOHANDLE file;
	dbg_msg("config/save", "saving config to %s", filename);

	file = io_open(filename, IOFLAG_WRITE);

	if(file)
	{
#if defined(CONF_FAMILY_WINDOWS)
		const char newline[] = "\r\n";
#else
		const char newline[] = "\n";
#endif
		const int newline_len = sizeof(newline)-1;
		
    	#define MACRO_CONFIG_INT(name,def,min,max) { char str[256]; str_format(str, sizeof(str), "%s=%i%s", #name, config.name, newline); io_write(file, str, strlen(str)); }
    	#define MACRO_CONFIG_STR(name,len,def) { io_write(file, #name, strlen(#name)); io_write(file, "=", 1); io_write(file, config.name, strlen(config.name)); io_write(file, newline, newline_len); }
 
    	#include "e_config_variables.h" 
 
    	#undef MACRO_CONFIG_INT 
    	#undef MACRO_CONFIG_STR 

		io_close(file);
	}
	else
		dbg_msg("config/save", "couldn't open %s for writing. :(", filename);
}

#define MACRO_CONFIG_INT(name,def,min,max) int config_get_ ## name (CONFIGURATION *c) { return c->name; }
#define MACRO_CONFIG_STR(name,len,def) const char *config_get_ ## name (CONFIGURATION *c) { return c->name; }
#include "e_config_variables.h"
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_STR

#define MACRO_CONFIG_INT(name,def,min,max) void config_set_ ## name (CONFIGURATION *c, int val) { if(min != max) { if (val < min) val = min; if (max != 0 && val > max) val = max; } c->name = val; }
#define MACRO_CONFIG_STR(name,len,def) void config_set_ ## name (CONFIGURATION *c, const char *str) { str_copy(c->name, str, len-1); c->name[sizeof(c->name)-1] = 0; }
#include "e_config_variables.h"
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_STR
