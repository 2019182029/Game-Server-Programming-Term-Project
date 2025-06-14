#pragma once

#include <WS2tcpip.h>
#include <MSWSock.h>

#include <mutex>
#include <thread>
#include <iostream>

#include <set>
#include <array>
#include <queue>
#include <vector>
#include <atomic>
#include <random>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <concurrent_unordered_map.h>

#include "protocol.h"
#include "include/lua.hpp"

#include <sqlext.h>
#include <locale.h>

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")
#pragma comment (lib, "lua54.lib")

class EXP_OVER;
class SESSION;

extern concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;

extern std::default_random_engine dre;
extern std::uniform_int_distribution<int> uid;

enum EVENT_TYPE { 
	EV_DAMAGE, EV_HEAL,
	EV_PLAYER_DIE, EV_PLAYER_RESPAWN,
	EV_NPC_MOVE, EV_NPC_ATTACK, EV_NPC_DIE, EV_NPC_RESPAWN 
};

struct event {
	int obj_id;
	int target_id;
	std::chrono::high_resolution_clock::time_point wakeup_time;
	EVENT_TYPE event_id;

	constexpr bool operator < (const event& _Left) const {
		return (wakeup_time > _Left.wakeup_time);
	}
};

extern std::priority_queue<event> timer_queue;
extern std::mutex timer_lock;

enum QUERY_TYPE { 
	QU_AUTO_SAVE, 
	QU_LOGIN, QU_USER_LOGIN, QU_USER_REGISTER, QU_LOGOUT, 
	QU_SELECT_AVATAR, QU_CREATE_AVATAR 
};

struct query {
	int obj_id;
	std::chrono::high_resolution_clock::time_point wakeup_time;
	QUERY_TYPE query_id;

	char client_id[ID_SIZE];
	char client_pw[PW_SIZE];

	int avatar_id;
	int slot;

	query(int o_id, std::chrono::high_resolution_clock::time_point w_time, QUERY_TYPE q_id) {
		obj_id = o_id;
		wakeup_time = w_time;
		query_id = q_id;
	}

	constexpr bool operator < (const query& _Left) const {
		return (wakeup_time > _Left.wakeup_time);
	}

	void set_avatar_id(int a_id) { avatar_id = a_id; }
	void set_slot(int s) { slot = s; }
};

extern std::priority_queue<query> query_queue;
extern std::mutex query_lock;

//////////////////////////////////////////////////
// EXP_OVER
enum IO_TYPE { 
	IO_ACCEPT, IO_SEND, IO_RECV, 
	IO_LOGIN, IO_LOGIN_OK, IO_LOGIN_FAIL,
	IO_DAMAGE, IO_HEAL,
	IO_PLAYER_DIE, IO_PLAYER_RESPAWN,
	IO_NPC_MOVE, IO_NPC_ATTACK, IO_NPC_DIE, IO_NPC_RESPAWN 
};

class EXP_OVER {
public:
	WSAOVERLAPPED m_over;
	char m_buffer[1024];
	WSABUF m_wsabuf[1];

	SOCKET m_accept_socket;
	IO_TYPE m_io_type;

	int m_target_id;

	int m_error_code;
	std::vector<AVATAR> m_avatars;

public:
	EXP_OVER();
	EXP_OVER(IO_TYPE io_type);
	~EXP_OVER();
};

//////////////////////////////////////////////////
// SESSION
enum STATE { ST_ACCEPT, ST_INGAME, ST_CLOSE };
class SESSION {
public:
	EXP_OVER m_recv_over{ IO_RECV };

	SOCKET m_c_socket;
	int m_remained;

	std::atomic<STATE> m_state;
	std::unordered_set<int> m_view_list;
	std::mutex m_vl;

	short m_x, m_y;
	int m_id;
	std::atomic<int> m_hp;
	int m_max_hp;
	std::atomic<int> m_exp;
	int m_level;
	char m_name[NAME_SIZE];

	std::atomic<bool> m_is_active;
	unsigned int m_last_move_time;

	lua_State* m_lua;
	std::mutex m_lua_lock;

	int m_avatar_id;
	int m_account_id;

public:
	SESSION();
	SESSION(int id);
	SESSION(int id, SOCKET c_socket);
	~SESSION();

	void do_recv();
	void do_send(void* buff);

	void send_login_ok(const std::vector<AVATAR>& avatars);
	void send_login_fail(int error_code);
	void send_login_info();
	void send_add_object(int c_id);
	void send_move_object(int c_id);
	void send_remove_object(int c_id);
	void send_chat(int c_id, const char* mess);

	void send_attack(int c_id);

	bool earn_exp(int& prev_exp, int& curr_exp);
	void send_earn_exp(const char* name, int exp);
	void send_level_up(int c_id);
	void send_damage(int c_id, int hp);
	void send_heal(int c_id, int hp);
	void send_death(int c_id);
	void send_stat_change();

	void try_wake_up(int target_id);
	void wake_up(int target_id);
	void sleep();
	void receive_damage(int damage, int target_id, int& hp);
	bool heal(int& hp);
	bool is_alive();
	void respawn(short x, short y);
};