#pragma once

// Server
constexpr int PORT_NUM = 4000;
constexpr int BUF_SIZE = 200;
constexpr int NAME_SIZE = 20;
constexpr int CHAT_SIZE = 100;

// Character
constexpr int MAX_USER = 10000;
constexpr int MAX_NPC = 20000;
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
constexpr char CS_MOVE = 1;
constexpr char CS_CHAT = 2;
constexpr char CS_ATTACK = 3;			// 4 방향 공격
constexpr char CS_TELEPORT = 4;			// RANDOM한 위치로 Teleport, Stress Test할 때 Hot Spot현상을 피하기 위해 구현
constexpr char CS_LOGOUT = 5;			// 클라이언트에서 정상적으로 접속을 종료하는 패킷

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
constexpr char SC_DAMAGED = 13;
constexpr char SC_DEATH = 14;

#pragma pack (push, 1)

struct CS_LOGIN_PACKET {
	unsigned char size;
	char	type;
	char	name[NAME_SIZE];
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

struct SC_LOGIN_OK_PACKET {
	unsigned char size;
	char	type;
};

struct SC_LOGIN_FAIL_PACKET {
	unsigned char size;
	char	type;
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

struct SC_DAMAGED_PACKET {
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