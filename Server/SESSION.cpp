#define _CRT_SECURE_NO_WARNINGS

#include <iostream>

#include "SESSION.h"

concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;
thread_local EXP_OVER_POOL g_exp_overs;

std::priority_queue<event> timer_queue;
std::mutex timer_lock;

std::priority_queue<query> query_queue;
std::mutex query_lock;

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

void EXP_OVER::reset() {
	ZeroMemory(&m_over, sizeof(m_over));
	ZeroMemory(&m_buffer, sizeof(m_buffer));
	m_wsabuf[0].len = sizeof(m_buffer);
	m_wsabuf[0].buf = m_buffer;

	m_accept_socket = INVALID_SOCKET;
	m_io_type = IO_NONE;  

	m_target_id = INVALID_ID;

	m_error_code = EC_SUCCESS;
	m_avatars.clear();
}

EXP_OVER_POOL::EXP_OVER_POOL() {
	m_capacity = MAX_EXP_COUNT / std::thread::hardware_concurrency();
}

EXP_OVER_POOL::~EXP_OVER_POOL() {
	while (!m_pool.empty()) {
		delete m_pool.top();
		m_pool.pop();
	}
}

EXP_OVER* EXP_OVER_POOL::acquire() {
	if (m_pool.empty()) {
		return new EXP_OVER;
	} else {
		EXP_OVER* obj = m_pool.top();
		m_pool.pop();
		return obj;
	}
}

void EXP_OVER_POOL::release(EXP_OVER* eo) {
	if (nullptr == eo) { return; }

	if (m_pool.size() >= m_capacity) {
		delete eo;
	} else {
		eo->reset();
		m_pool.push(eo);
	}
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

SESSION::SESSION(int id, SOCKET c_socket) : m_id(id), m_c_socket(c_socket) {
	m_remained = 0;
	m_state = ST_ACCEPT;

	m_x = (rand() % W_WIDTH);
	m_y = (rand() % W_HEIGHT);
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
	//if (ret == SOCKET_ERROR) {
	//	int err = WSAGetLastError();
	//	if (err != WSA_IO_PENDING) {
	//		printf("[do_recv] WSARecv failed! Error: %d\n", err);
	//	}
	//}
}

void SESSION::do_send(void* buff) {
	EXP_OVER* o = g_exp_overs.acquire();
	o->m_io_type = IO_SEND;
	unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];
	memcpy(o->m_buffer, buff, packet_size);
	o->m_wsabuf[0].len = packet_size;

	DWORD send_bytes;
	auto ret = WSASend(m_c_socket, o->m_wsabuf, 1, &send_bytes, 0, &(o->m_over), NULL);
	//if (ret == SOCKET_ERROR) {
	//	int err = WSAGetLastError();
	//	if (err != WSA_IO_PENDING) {
	//		printf("[do_send] WSASend failed! Error: %d\n", err);
	//	}
	//}
}

void SESSION::send_login_ok(const std::vector<AVATAR>& avatars) {
	// Alloc Memory
	size_t avatar_count = avatars.size();
	size_t packet_size = sizeof(SC_LOGIN_OK_PACKET) + (avatar_count * sizeof(AVATAR));

	char* buffer = new char[packet_size];

	// Header
	SC_LOGIN_OK_PACKET* p = reinterpret_cast<SC_LOGIN_OK_PACKET*>(buffer);
	p->size = static_cast<unsigned char>(packet_size);
	p->type = SC_LOGIN_OK;

	// Copy Avatar Data
	AVATAR* ptr = reinterpret_cast<AVATAR*>(buffer + sizeof(SC_LOGIN_OK_PACKET));
	for (size_t i = 0; i < avatar_count; ++i) {
		ptr[i] = avatars[i];
	}

	do_send(p);

	delete[] buffer;
}

void SESSION::send_login_fail(int error_code) {
	SC_LOGIN_FAIL_PACKET p;
	p.size = sizeof(SC_LOGIN_FAIL_PACKET);
	p.type = SC_LOGIN_FAIL;
	p.error_code = error_code;
	do_send(&p);
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
	p.hp = client->m_hp;
	p.max_hp = client->m_max_hp;
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

bool SESSION::earn_exp(int earned_exp, int& prev_exp, int& curr_exp) {
	prev_exp = m_exp.fetch_add(earned_exp);
	curr_exp = prev_exp + earned_exp;

	if (0 == (curr_exp % 100)) {
		if (m_level < KING) {
			++m_level;
			return true;
		}
	}
	return false;
}

void SESSION::send_earn_exp(const char* name, int exp) {
	SC_EARN_EXP_PACKET p;
	p.size = sizeof(SC_EARN_EXP_PACKET);
	p.type = SC_EARN_EXP;
	p.exp = exp;
	strncpy(p.name, name, NAME_SIZE);
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

void SESSION::send_damage(int c_id, int hp) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_DAMAGE_PACKET p;
	p.size = sizeof(SC_DAMAGE_PACKET);
	p.type = SC_DAMAGE;
	p.id = c_id;
	p.hp = hp;
	do_send(&p);
}

void SESSION::send_heal(int c_id, int hp) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_HEAL_PACKET p;
	p.size = sizeof(SC_HEAL_PACKET);
	p.type = SC_HEAL;
	p.id = c_id;
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

void SESSION::send_respawn(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	SC_RESPAWN_PACKET p;
	p.size = sizeof(SC_RESPAWN_PACKET);
	p.type = SC_RESPAWN;
	p.id = client->m_id;
	p.x = client->m_x;
	p.y = client->m_y;
	do_send(&p);
}

void SESSION::send_stat_change() {
	SC_STAT_CHANGE_PACKET p;
	p.size = sizeof(SC_STAT_CHANGE_PACKET);
	p.type = SC_STAT_CHANGE;
	p.hp = m_hp;
	p.exp = m_exp;
	p.level = m_level;
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
			std::lock_guard<std::mutex> tl(timer_lock);
			timer_queue.emplace(event{ m_id, target_id, std::chrono::high_resolution_clock::now() + std::chrono::seconds(1), EV_NPC_MOVE });
		}
	}
}

void SESSION::sleep() {
	m_is_active = false;
}

void SESSION::receive_damage(int damage, int target_id, int& hp) {
	int prev_hp = m_hp.fetch_sub(damage);
	hp = prev_hp - damage;

	// Death Event
	if ((0 < prev_hp) && (0 >= hp)) {
		std::lock_guard<std::mutex> tl(timer_lock);
		timer_queue.emplace(event{ m_id, target_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), (m_id < MAX_USER) ? EV_PLAYER_DIE : EV_NPC_DIE });
		return;
	} 

	// Heal Event
	if (m_max_hp == prev_hp) {
		std::lock_guard<std::mutex> tl(timer_lock);
		timer_queue.emplace(event{ m_id, INVALID_ID, std::chrono::high_resolution_clock::now() + std::chrono::seconds(5), EV_HEAL });
	}
}

bool SESSION::heal(int& hp) {
	if (false == is_alive()) { return false; }

	while (true) {
		int expected = m_hp;

		if (m_max_hp <= expected) { break; }

		if (std::atomic_compare_exchange_strong(&m_hp, &expected, expected + 1)) {
			hp = expected + 1;

			if (m_max_hp > expected + 1) {
				std::lock_guard<std::mutex> tl(timer_lock);
				timer_queue.emplace(event{ m_id, INVALID_ID, std::chrono::high_resolution_clock::now() + std::chrono::seconds(5), EV_HEAL });
			}
				
			return true;
		}
	}

	return false;
}

bool SESSION::is_alive() {
	return (0 < m_hp);
}

void SESSION::respawn(short x, short y) {
	m_x = x;
	m_y = y;
	m_hp = m_max_hp;
	m_exp = 0;
	m_level = max(0, m_level - 1);
}
