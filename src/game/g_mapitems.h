/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#ifndef GAME_MAPITEMS_H
#define GAME_MAPITEMS_H

// layer types
enum
{
	LAYERTYPE_INVALID=0,
	LAYERTYPE_GAME, // not used
	LAYERTYPE_TILES,
	LAYERTYPE_QUADS,
};


// game layer tiles
enum
{
	ENTITY_NULL=0,
	ENTITY_SPAWN,
	ENTITY_SPAWN_RED,
	ENTITY_SPAWN_BLUE,
	ENTITY_FLAGSTAND_RED,
	ENTITY_FLAGSTAND_BLUE,
	ENTITY_ARMOR_1,
	ENTITY_HEATH_1,
	ENTITY_WEAPON_SHOTGUN,
	ENTITY_WEAPON_ROCKET,
	ENTITY_POWERUP_NINJA,
	NUM_ENTITIES,
	
	TILE_AIR=0,
	TILE_SOLID,
	TILE_NOHOOK,
	
	TILEFLAG_VFLIP=1,
	TILEFLAG_HFLIP=2,
	
	ENTITY_OFFSET=255-16*4,
};

typedef struct
{
	int x, y; // 22.10 fixed point
} POINT;

typedef struct
{
	int r, g, b, a;
} COLOR;

typedef struct
{
	POINT points[5];
	COLOR colors[4];
	POINT texcoords[4];
	
	int pos_env;
	int pos_env_offset;
	
	int color_env;
	int color_env_offset;
} QUAD;

typedef struct
{
	unsigned char index;
	unsigned char flags;
	unsigned char reserved1;
	unsigned char reserved2;
} TILE;

typedef struct 
{
	int version;
	int width;
	int height;
	int external;
	int image_name;
	int image_data;
} MAPITEM_IMAGE;

typedef struct
{
	int version;
	int offset_x;
	int offset_y;
	int parallax_x;
	int parallax_y;

	int start_layer;
	int num_layers;
} MAPITEM_GROUP;

typedef struct
{
	int version;
	int type;
	int flags;
} MAPITEM_LAYER;

typedef struct
{
	MAPITEM_LAYER layer;
	int version;
	
	int width;
	int height;
	int flags;
	
	COLOR color;
	int color_env;
	int color_env_offset;
	
	int image;
	int data;
} MAPITEM_LAYER_TILEMAP;

typedef struct
{
	MAPITEM_LAYER layer;
	int version;
	
	int num_quads;
	int data;
	int image;
} MAPITEM_LAYER_QUADS;

typedef struct
{
	int version;
} MAPITEM_VERSION;

// float to fixed
inline int f2fx(float v) { return (int)(v*(float)(1<<10)); }
inline float fx2f(int v) { return v*(1.0f/(1<<10)); }


#endif
