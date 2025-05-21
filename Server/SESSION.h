#pragma once

#include "WS2tcpip.h"
#include "atomic"
#include "unordered_set"
#include "mutex"
#include <concurrent_unordered_map.h>

class EXP_OVER;
class SESSION;

extern concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;

//////////////////////////////////////////////////
// EXP_OVER
enum IO_TYPE { IO_ACCEPT, IO_SEND, IO_RECV };
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
	int m_hp;
	int m_max_hp;
	int m_exp;
	int m_level;

public:
	SESSION(int id, SOCKET c_socket);
	~SESSION();

	void do_recv();
	void do_send(void* buff);
	void send_login_info();
	void send_add_object(int c_id);
	void send_move_object(int c_id);
	void send_remove_object(int c_id);
};