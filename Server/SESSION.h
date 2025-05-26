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
#include <unordered_set>
#include <concurrent_unordered_map.h>

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

class EXP_OVER;
class SESSION;

extern concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;

enum EVENT_TYPE { EV_MOVE, EV_DIE, EV_RESPAWN };
struct event {
	int obj_id;
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
enum IO_TYPE { IO_ACCEPT, IO_SEND, IO_RECV, IO_NPC_MOVE, IO_NPC_DIE, IO_NPC_RESPAWN };
class EXP_OVER {
public:
	WSAOVERLAPPED m_over;
	char m_buffer[1024];
	WSABUF m_wsabuf[1];

	SOCKET m_accept_socket;
	IO_TYPE m_io_type;

public:
	EXP_OVER();
	EXP_OVER(IO_TYPE io_type);
	~EXP_OVER();
};

//////////////////////////////////////////////////
// SESSION
enum STATE { ST_ACCEPT, ST_INGAME, ST_CLOSE, ST_DIE };
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
	std::atomic<int> m_level;

	std::atomic<bool> m_is_active;

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

	void wake_up();
	void sleep();

	void receive_damage(int damage);
	void respawn();
};