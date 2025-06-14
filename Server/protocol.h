#pragma once

// Server
constexpr int PORT_NUM = 4000;
constexpr int BUF_SIZE = 200;
constexpr int ID_SIZE = 32;
constexpr int PW_SIZE = 64;
constexpr int NAME_SIZE = 32;
constexpr int CHAT_SIZE = 100;

// Character
constexpr int MAX_USER = 10000;
constexpr int MAX_NPC = 2000;
constexpr int INVALID_ID = -1;

constexpr int PAWN = 0;
constexpr int BISHOP = 1;
constexpr int ROOK = 2;
constexpr int KING = 3;

constexpr int KNIGHT = 4;
constexpr int QUEEN = 5;

constexpr int AGGRO_RANGE = 3;
constexpr int CHASE_RANGE = 7;
constexpr int KNIGHT_ATTACK_RANGE = 5;
constexpr int QUEEN_ATTACK_RANGE = 3;

// Screen
constexpr int S_WIDTH = 1000;
constexpr int S_HEIGHT = 1000;

constexpr int S_VISIBLE_TILES = 20;

constexpr int S_TILE_WIDTH = S_WIDTH / S_VISIBLE_TILES;
constexpr int S_TILE_HEIGHT = S_HEIGHT / S_VISIBLE_TILES;

// World
constexpr int W_WIDTH = 2000;
constexpr int W_HEIGHT = 2000;

// Sector
constexpr int VIEW_RANGE = 7;

constexpr int SECTOR_WIDTH = 10;
constexpr int SECTOR_HEIGHT = 10;

constexpr int SECTOR_ROWS = W_WIDTH / SECTOR_WIDTH;
constexpr int SECTOR_COLS = W_HEIGHT / SECTOR_WIDTH;

// Packet ID
constexpr char CS_LOGIN = 0;
constexpr char CS_USER_LOGIN = 1;
constexpr char CS_MOVE = 2;
constexpr char CS_CHAT = 3;
constexpr char CS_ATTACK = 4;			// 4 방향 공격
constexpr char CS_TELEPORT = 5;			// RANDOM한 위치로 Teleport, Stress Test할 때 Hot Spot현상을 피하기 위해 구현
constexpr char CS_LOGOUT = 6;			// 클라이언트에서 정상적으로 접속을 종료하는 패킷
constexpr char CS_SELECT_AVATAR = 7;
constexpr char CS_CREATE_AVATAR = 8;
constexpr char CS_USER_REGISTER = 9;

constexpr char SC_LOGIN_INFO = 2;
constexpr char SC_ADD_OBJECT = 3;
constexpr char SC_REMOVE_OBJECT = 4;
constexpr char SC_MOVE_OBJECT = 5;
constexpr char SC_CHAT = 6;
constexpr char SC_LOGIN_OK = 7;
constexpr char SC_LOGIN_FAIL = 8;
constexpr char SC_STAT_CHANGE = 9;
constexpr char SC_ATTACK = 10;
constexpr char SC_EARN_EXP = 11;
constexpr char SC_LEVEL_UP = 12;
constexpr char SC_DAMAGE = 13;
constexpr char SC_DEATH = 14;
constexpr char SC_HEAL = 15;

// Error Code
constexpr char NO_ID = -1;  
constexpr char WRONG_PW = -2;  
constexpr char DUPLICATED = -3;  
constexpr char EXEC_DIRECT = -4;
constexpr char ALREADY_EXIST = -5;

#pragma pack (push, 1)

struct AVATAR {
	int avatar_id;
	int slot;
	int level;
};

struct SC_LOGIN_OK_PACKET {
	unsigned char size;
	char	type;
};

struct SC_LOGIN_FAIL_PACKET {
	unsigned char size;
	char	type;
	char	error_code;
};

struct CS_LOGIN_PACKET {
	unsigned char size;
	char	type;
	char	id[ID_SIZE];
	char	pw[PW_SIZE];
};

struct CS_SELECT_AVATAR_PACKET {
	unsigned char size;
	char	type;
	int		avatar_id;
};

struct CS_CREATE_AVATAR_PACKET {
	unsigned char size;
	char	type;
	int		slot_id;
};

struct CS_USER_LOGIN_PACKET {
	unsigned char size;
	char	type;
	char	id[ID_SIZE];
	char	pw[PW_SIZE];
};

struct CS_USER_REGISTER_PACKET {
	unsigned char size;
	char	type;
	char	id[ID_SIZE];
	char	pw[PW_SIZE];
};

struct CS_MOVE_PACKET {
	unsigned char size;
	char	type;
	char	direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	unsigned int move_time;
};

struct CS_CHAT_PACKET {
	unsigned char size;
	char	type;
	char	mess[CHAT_SIZE];
};

struct CS_TELEPORT_PACKET {
	unsigned char size;
	char	type;
	unsigned int move_time;
};

struct CS_ATTACK_PACKET {
	unsigned char size;
	char	type;
};

struct CS_LOGOUT_PACKET {
	unsigned char size;
	char	type;
};

struct SC_LOGIN_INFO_PACKET {
	unsigned char size;
	char	type;
	int		id;
	int		hp;
	int		max_hp;
	int		exp;
	int		level;
	short	x, y;
};

struct SC_ADD_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	int		level;
	short	x, y;
	char	name[NAME_SIZE];
};

struct SC_REMOVE_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
};

struct SC_MOVE_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	short	x, y;
	unsigned int move_time;
};

struct SC_CHAT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	char	mess[CHAT_SIZE];
};

struct SC_STAT_CHANGE_PACKET {
	unsigned char size;
	char	type;
	int		hp;
	int		max_hp;
	int		exp;
	int		level;
};

struct SC_ATTACK_PACKET {
	unsigned char size;
	char	type;
	int		id;
};

struct SC_EARN_EXP_PACKET {
	unsigned char size;
	char	type;
	int		exp;
};

struct SC_LEVEL_UP_PACKET {
	unsigned char size;
	char	type;
	int		id;
	int		level;
};

struct SC_DAMAGE_PACKET {
	unsigned char size;
	char	type;
	int		hp;
};

struct SC_HEAL_PACKET {
	unsigned char size;
	char	type;
	int		hp;
};

struct SC_DEATH_PACKET {
	unsigned char size;
	char	type;
	int		id;
};

#pragma pack (pop)