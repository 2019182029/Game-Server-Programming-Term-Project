#include <iostream>

#include "SESSION.h"
#include "..\protocol.h"

concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;

std::priority_queue<event> timer_queue;
std::mutex timer_lock;

//////////////////////////////////////////////////
// EXP_OVER
EXP_OVER::EXP_OVER() {
	ZeroMemory(&m_over, sizeof(m_over));
	ZeroMemory(&m_buffer, sizeof(m_buffer));
	m_wsabuf[0].len = sizeof(m_buffer);
	m_wsabuf[0].buf = m_buffer;
}

EXP_OVER::EXP_OVER(IO_TYPE io_type) : EXP_OVER() {
	m_io_type = io_type;
}

EXP_OVER::~EXP_OVER() {

}

//////////////////////////////////////////////////
// SESSION
SESSION::SESSION() {

}

SESSION::SESSION(int id) : m_id(id) {
	m_x = 0; m_y = 0;
	m_hp = 10;
	m_max_hp = 10;
	m_exp = 0;
	m_level = 0;

	m_is_active = false;
}

SESSION::SESSION(int id, SOCKET c_socket) : m_c_socket(c_socket), m_id(id) {
	m_remained = 0;
	m_state = ST_ACCEPT;

	m_x = 0; m_y = 0;
	m_hp = 10;
	m_max_hp = 10;
	m_exp = 0;
	m_level = 0;

	m_is_active = false;
}

SESSION::~SESSION() {
	closesocket(m_c_socket);
}

void SESSION::do_recv() {
	m_recv_over.m_wsabuf[0].buf = m_recv_over.m_buffer + m_remained;
	m_recv_over.m_wsabuf[0].len = sizeof(m_recv_over.m_buffer) - m_remained;

	DWORD recv_flag = 0;
	auto ret = WSARecv(m_c_socket, m_recv_over.m_wsabuf, 1, NULL, &recv_flag, reinterpret_cast<WSAOVERLAPPED*>(&m_recv_over), NULL);

	if (ret == SOCKET_ERROR) {
		DWORD err = WSAGetLastError();
		if (err != WSA_IO_PENDING) {
			std::cout << "WSARecv failed : " << err << std::endl;
		}
	}
}

void SESSION::do_send(void* buff) {
	EXP_OVER* o = new EXP_OVER(IO_SEND);
	unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];
	memcpy(o->m_buffer, buff, packet_size);
	o->m_wsabuf[0].len = packet_size;

	DWORD send_bytes;
	auto ret = WSASend(m_c_socket, o->m_wsabuf, 1, &send_bytes, 0, &(o->m_over), NULL);

	if (ret == SOCKET_ERROR) {
		DWORD err = WSAGetLastError();
		if (err != WSA_IO_PENDING) {
			std::cout << "WSASend failed : " << err << std::endl;
			delete o;
		}
	}
}

void SESSION::send_login_info() {
	SC_LOGIN_INFO_PACKET p;
	p.size = sizeof(SC_LOGIN_INFO_PACKET);
	p.type = SC_LOGIN_INFO;
	p.x = m_x;
	p.y = m_y;
	p.id = m_id;
	p.hp = m_hp;
	p.max_hp = m_max_hp;
	p.exp = m_exp;
	p.level = m_level;
	do_send(&p);
}

void SESSION::send_add_object(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_ADD_OBJECT_PACKET p;
	p.size = sizeof(SC_ADD_OBJECT_PACKET);
	p.type = SC_ADD_OBJECT;
	p.id = c_id;
	p.x = client->m_x;
	p.y = client->m_y;
	p.level = client->m_level;
	m_vl.lock();
	m_view_list.insert(c_id);
	m_vl.unlock();
	do_send(&p);
}

void SESSION::send_move_object(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_MOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = client->m_x;
	p.y = client->m_y;
	do_send(&p);
}

void SESSION::send_remove_object(int c_id) {
	m_vl.lock();
	if (m_view_list.count(c_id)) {
		m_view_list.erase(c_id);
	} else {
		m_vl.unlock();
		return;
	}
	m_vl.unlock();
	
	SC_REMOVE_OBJECT_PACKET p;
	p.size = sizeof(p);
	p.type = SC_REMOVE_OBJECT;
	p.id = c_id;
	do_send(&p);
}

void SESSION::wake_up() {
	bool expected = false;
	if (std::atomic_compare_exchange_strong(&m_is_active, &expected, true)) {
		timer_lock.lock();
		timer_queue.emplace(event{ m_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), EV_MOVE });
		timer_lock.unlock();
	}
}

void SESSION::sleep() {
	m_is_active = false;
}

void SESSION::receive_damage(int damage) {
	while (true) {
		int hp = m_hp;

		if (0 >= hp) { break; }

		if (std::atomic_compare_exchange_strong(&m_hp, &hp, hp - damage)) {
			if (0 >= (hp - damage)) {
				m_state = ST_DIE;

				sleep();

				timer_lock.lock();
				timer_queue.emplace(event{ m_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), EV_DIE });
				timer_lock.unlock();
			}
			break;
		}
	}
}

void SESSION::respawn() {
	m_state = ST_INGAME;

	m_x = 0;
	m_y = 0;
	m_hp = m_max_hp;
}
