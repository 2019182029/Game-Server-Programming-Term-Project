#define _CRT_SECURE_NO_WARNINGS

#include <iostream>

#include "SESSION.h"

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
	m_x = 0;
	m_y = 0;
	m_hp = 10;
	m_max_hp = 10;
	m_exp = 0;
	m_level = 0;

	m_is_active = false;
}

SESSION::SESSION(int id, SOCKET c_socket) : m_c_socket(c_socket), m_id(id) {
	m_remained = 0;
	m_state = ST_ACCEPT;

	m_x = 0;
	m_y = 0;
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
}

void SESSION::do_send(void* buff) {
	EXP_OVER* o = new EXP_OVER(IO_SEND);
	unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];
	memcpy(o->m_buffer, buff, packet_size);
	o->m_wsabuf[0].len = packet_size;

	DWORD send_bytes;
	auto ret = WSASend(m_c_socket, o->m_wsabuf, 1, &send_bytes, 0, &(o->m_over), NULL);
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
	strcpy(p.name, client->m_name);
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
	p.move_time = client->m_last_move_time;
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

void SESSION::send_chat(int c_id, const char* mess) {
	SC_CHAT_PACKET p;
	p.size = static_cast<unsigned char>(sizeof(p));
	p.type = SC_CHAT;
	p.id = c_id;
	strcpy(p.mess, mess);
	do_send(&p);
}

void SESSION::send_attack(int c_id) {
	SC_ATTACK_PACKET p;
	p.size = sizeof(SC_ATTACK_PACKET);
	p.type = SC_ATTACK;
	p.id = c_id;
	do_send(&p);
}

bool SESSION::earn_exp(int& exp) {
	int prev_exp = m_exp.fetch_add(50);
	exp = prev_exp + 50;

	if (0 == (exp % 100)) {
		if (m_level < KING) {
			++m_level;
			return true;
		}
	}
	return false;
}

void SESSION::send_earn_exp(int exp) {
	SC_EARN_EXP_PACKET p;
	p.size = sizeof(SC_EARN_EXP_PACKET);
	p.type = SC_EARN_EXP;
	p.exp = exp;
	do_send(&p);
}

void SESSION::send_level_up(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_LEVEL_UP_PACKET p;
	p.size = sizeof(SC_LEVEL_UP_PACKET);
	p.type = SC_LEVEL_UP;
	p.id = c_id;
	p.level = client->m_level;
	do_send(&p);
}

void SESSION::send_damaged(int hp) {
	SC_DAMAGED_PACKET p;
	p.size = sizeof(SC_DAMAGED_PACKET);
	p.type = SC_DAMAGED;
	p.hp = hp;
	do_send(&p);
}

void SESSION::send_death(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_DEATH_PACKET p;
	p.size = sizeof(SC_DEATH_PACKET);
	p.type = SC_DEATH;
	p.id = c_id;
	do_send(&p);
}

void SESSION::try_wake_up(int target_id) {
	switch (m_level) {
	case KNIGHT: {
		wake_up(target_id);
		break;
	}

	case QUEEN: {
		std::shared_ptr<SESSION> client = g_clients.at(target_id);
		if (nullptr == client) return;

		if ((abs(client->m_x - m_x) < AGGRO_RANGE) &&
			(abs(client->m_y - m_y) < AGGRO_RANGE)) { wake_up(target_id); }
		break;
	}
	}
}

void SESSION::wake_up(int target_id) {
	if (!m_is_active) {
		bool expected = false;

		if (std::atomic_compare_exchange_strong(&m_is_active, &expected, true)) {
			timer_lock.lock();
			timer_queue.emplace(event{ m_id, target_id, std::chrono::high_resolution_clock::now() + std::chrono::seconds(1), EV_NPC_MOVE });
			timer_lock.unlock();
		}
	}
}

void SESSION::sleep() {
	m_is_active = false;
}

void SESSION::receive_damage(int damage, int target_id) {
	int prev_hp = m_hp.fetch_sub(damage);
	int curr_hp = prev_hp - damage;

	if ((0 < prev_hp) && (0 >= curr_hp)) { 
		if (m_id < MAX_USER) {
			timer_lock.lock();
			timer_queue.emplace(event{ m_id, INVALID_ID, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), EV_PLAYER_DIE });
			timer_lock.unlock();
		} else {
			timer_lock.lock();
			timer_queue.emplace(event{ m_id, target_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), EV_NPC_DIE });
			timer_lock.unlock();
		}
	} else {
		if (m_id < MAX_USER) {
			send_damaged(curr_hp);
		}
	}
}

bool SESSION::is_alive() {
	return (0 < m_hp);
}

void SESSION::respawn(short x, short y) {
	m_x = x;
	m_y = y;
	m_hp = m_max_hp;
}
