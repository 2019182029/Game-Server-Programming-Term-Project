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

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

class EXP_OVER;
class SESSION;

extern concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;

extern std::default_random_engine dre;
extern std::uniform_int_distribution<int> uid;

enum EVENT_TYPE { 
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

//////////////////////////////////////////////////
// EXP_OVER
enum IO_TYPE { 
	IO_ACCEPT, IO_SEND, IO_RECV, 
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

public:
	SESSION();
	SESSION(int id);
	SESSION(int id, SOCKET c_socket);
	~SESSION();

	void do_recv();
	void do_send(void* buff);

	void send_login_info();
	void send_add_object(int c_id);
	void send_move_object(int c_id);
	void send_remove_object(int c_id);
	void send_chat(int c_id, const char* mess);

	void send_attack(int c_id);

	bool earn_exp(int& exp);
	void send_earn_exp(int exp);
	void send_level_up(int c_id);
	void send_damaged(int hp);
	void send_death(int c_id);

	void try_wake_up(int target_id);
	void wake_up(int target_id);
	void sleep();
	void receive_damage(int damage, int target_id);
	bool is_alive();
	void respawn();
};