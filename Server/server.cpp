#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <array>
#include <vector>
#include <atomic>
#include <iostream>

#include "..\protocol.h"
#include "SESSION.h"

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

constexpr short SERVER_PORT = 3000;

HANDLE g_hIOCP;
SOCKET g_s_socket, g_c_socket;
EXP_OVER g_accept_over{ IO_ACCEPT };

int g_new_id = 0;

std::array<std::array<std::unordered_set<int>, SECTOR_COLS>, SECTOR_ROWS> g_sector;
std::mutex g_mutex[SECTOR_ROWS][SECTOR_COLS];

//////////////////////////////////////////////////
// IOCP
void worker();
void disconnect(int c_id);
void process_packet(int c_id, char* packet);

//////////////////////////////////////////////////
// VIEW
bool can_see(int from, int to);
void update_sector(int c_id, short old_x, short old_y, short new_x, short new_y);


//////////////////////////////////////////////////
// NPC
void init_npc();
bool is_player(int c_id);
bool is_npc(int c_id);

//////////////////////////////////////////////////
// MAIN
int main() {
	WSADATA WSAData;
	auto ret = WSAStartup(MAKEWORD(2, 0), &WSAData);
	if (0 != ret) { std::cout << "WSAStartup Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSAStartup Succeed" << std::endl; }

	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);;
	if (INVALID_SOCKET == g_s_socket) { std::cout << "Client Socket Create Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSASocket Succeed" << std::endl; }

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind(g_s_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	if (SOCKET_ERROR == ret) { std::cout << "bind Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "bind Succeed" << std::endl; }

	ret = listen(g_s_socket, SOMAXCONN);
	if (SOCKET_ERROR == ret) { std::cout << "listen Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "listen Succeed" << std::endl; }

	init_npc();

	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), g_hIOCP, -1, 0);

	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	int addr_size = sizeof(SOCKADDR_IN);
	AcceptEx(g_s_socket, g_c_socket, g_accept_over.m_buffer, 0, addr_size + 16, addr_size + 16, 0, &g_accept_over.m_over);

	std::vector <std::thread> workers;
	for (unsigned int i = 0; i < std::thread::hardware_concurrency(); ++i)
		workers.emplace_back(worker);
	for (auto& w : workers)
		w.join();

	closesocket(g_s_socket);
	WSACleanup();
}

void worker() {
	while (true) {
		DWORD io_size;
		ULONG_PTR key;
		WSAOVERLAPPED* o;

		BOOL ret = GetQueuedCompletionStatus(g_hIOCP, &io_size, &key, &o, INFINITE);
		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(o);

		if (FALSE == ret) {
			if (eo->m_io_type == IO_ACCEPT) { std::cout << "Accept Error" << std::endl; }
			else {
				std::cout << "GQCS Error on Client[" << key << "]" << std::endl;
				disconnect(static_cast<int>(key));
				if (eo->m_io_type == IO_SEND) delete eo;
				continue;
			}
		}

		if ((eo->m_io_type == IO_RECV || eo->m_io_type == IO_SEND) && (0 == io_size)) {
			disconnect(static_cast<int>(key));
			if (eo->m_io_type == IO_SEND) delete eo;
			continue;
		}

		switch (eo->m_io_type) {
		case IO_ACCEPT: {
			int client_id = g_new_id++;
			std::shared_ptr<SESSION> p = std::make_shared<SESSION>(client_id, g_c_socket);
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), g_hIOCP, client_id, 0);
			g_clients.insert(std::make_pair(client_id, p));
			p->do_recv();

			ZeroMemory(&g_accept_over.m_over, sizeof(g_accept_over.m_over));
			g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(g_s_socket, g_c_socket, g_accept_over.m_buffer, 0, addr_size + 16, addr_size + 16, 0, &g_accept_over.m_over);
			break;
		}

		case IO_SEND: {
			delete eo;
			break;
		}

		case IO_RECV: {
			int cliend_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(cliend_id);
			if (nullptr == client) break;

			int remain_data = io_size + client->m_remained;
			char* p = eo->m_buffer;
			while (remain_data > 0) {
				int packet_size = p[0];
				if (packet_size <= remain_data) {
					process_packet(cliend_id, p);
					p = p + packet_size;
					remain_data = remain_data - packet_size;
				}
				else break;
			}

			client->m_remained = remain_data;
			if (remain_data > 0) {
				memcpy(eo->m_buffer, p, remain_data);
			}
			client->do_recv();
			break;
		}
		}
	}
}

void disconnect(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	client->m_vl.lock();
	std::unordered_set <int> vl = client->m_view_list;
	client->m_vl.unlock();
	for (auto& cl : vl) {
		std::shared_ptr<SESSION> other = g_clients.at(cl);
		if (nullptr == other) continue;

		if (ST_INGAME != other->m_state) continue;
		if (other->m_id == c_id) continue;

		other->send_remove_object(c_id);
	}

	// Delete Client from Sector
	int sx = client->m_x / SECTOR_WIDTH;
	int sy = client->m_y / SECTOR_HEIGHT;

	{
		std::lock_guard<std::mutex> lock(g_mutex[sy][sx]);
		g_sector[sy][sx].erase(c_id);
	}
	
	client->m_state = ST_CLOSE;

	g_clients.at(c_id) = nullptr;
}

void process_packet(int c_id, char* packet) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	switch (packet[1]) {
	case CS_LOGIN: {
		client->m_state = ST_INGAME;

		client->send_login_info();

		// Add Client into Sector
		short sx = client->m_x / SECTOR_WIDTH;
		short sy = client->m_y / SECTOR_HEIGHT;
		{
			std::lock_guard<std::mutex> sl(g_mutex[sy][sx]);
			g_sector[sy][sx].insert(c_id);
		}

		// Search Nearby Objects by Sector
		for (short dy = -1; dy <= 1; ++dy) {
			for (short dx = -1; dx <= 1; ++dx) {
				short nx = sx + dx;
				short ny = sy + dy;

				if (nx < 0 || ny < 0 || nx >= SECTOR_COLS || ny >= SECTOR_ROWS) { continue; }

				std::lock_guard<std::mutex> lock(g_mutex[ny][nx]);
				for (auto pl : g_sector[ny][nx]) {
					std::shared_ptr<SESSION> other = g_clients.at(pl);
					if (nullptr == other) { continue; }

					if (ST_INGAME != other->m_state) { continue; }
					if (other->m_id == c_id) { continue; }
					if (!can_see(c_id, other->m_id)) { continue; }

					client->send_add_object(other->m_id);
					if (is_player(other->m_id)) { other->send_add_object(c_id); }
					else { other->wake_up(); }
				}
			}
		}
		break;
	}

	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);

		short old_x = client->m_x;
		short old_y = client->m_y;
		short new_x = old_x;
		short new_y = old_y;

		switch (p->direction) {
		case 0: --new_y; break;
		case 1: ++new_y; break;
		case 2: --new_x; break;
		case 3: ++new_x; break;
		}

		if ((0 <= new_x) && (new_x < W_WIDTH)) { client->m_x = new_x; }
		if ((0 <= new_y) && (new_y < W_HEIGHT)) { client->m_y = new_y; }

		// Update Sector
		update_sector(c_id, old_x, old_y, new_x, new_y);

		int sx = client->m_x / SECTOR_WIDTH;
		int sy = client->m_y / SECTOR_HEIGHT;

		// Create View List by Sector
		std::unordered_set<int> near_list;
		client->m_vl.lock();
		std::unordered_set<int> old_vlist = client->m_view_list;
		client->m_vl.unlock();
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				short nx = sx + dx;
				short ny = sy + dy;

				if (nx < 0 || ny < 0 || nx >= SECTOR_COLS || ny >= SECTOR_ROWS) { continue; }

				for (auto cl : g_sector[ny][nx]) {
					std::shared_ptr<SESSION> other = g_clients.at(cl);
					if (nullptr == other) { continue; }

					if (ST_INGAME != other->m_state) { continue; }
					if (other->m_id == c_id) { continue; }
					if (can_see(c_id, other->m_id)) { near_list.insert(other->m_id); }
				}
			}
		}

		client->send_move_object(c_id);

		for (auto& cl : near_list) {
			std::shared_ptr<SESSION> other = g_clients.at(cl);
			if (nullptr == other) continue;

			if (is_player(cl)) {
				other->m_vl.lock();
				if (other->m_view_list.count(c_id)) {
					other->m_vl.unlock();
					other->send_move_object(c_id);
				}
				else {
					other->m_vl.unlock();
					other->send_add_object(c_id);
				}
			} else {
				other->wake_up();
			}

			if (!old_vlist.count(cl)) { client->send_add_object(cl); }
		}

		for (auto& cl : old_vlist) {
			std::shared_ptr<SESSION> other = g_clients.at(cl);
			if (nullptr == other) continue;

			if (!near_list.count(cl)) {
				client->send_remove_object(cl);
				if (is_player(cl)) { other->send_remove_object(c_id); }
			}
		}
		break;
	}

	case CS_ATTACK:
		short x = client->m_x;
		short y = client->m_y;
		int level = client->m_level;

		std::vector<std::pair<short, short>> attacked_coords;
		switch (level) {
		case 0: 
			if (0 <= x - 1 && x - 1 < W_WIDTH && 0 <= y - 1 && y - 1 < W_HEIGHT) attacked_coords.emplace_back(x - 1, y - 1);
			if (0 <= x + 1 && x + 1 < W_WIDTH && 0 <= y - 1 && y - 1 < W_HEIGHT) attacked_coords.emplace_back(x + 1, y - 1);
			break;
		}

		std::set<std::pair<short, short>> attacked_sectors;
		for (const auto& coord : attacked_coords) {
			attacked_sectors.emplace(coord.first / SECTOR_WIDTH, coord.second / SECTOR_HEIGHT);
		}

		std::vector<std::unique_lock<std::mutex>> sector_locks; 
		for (const auto& sector : attacked_sectors) {
			sector_locks.emplace_back(g_mutex[sector.second][sector.first]);
		}

		for (const auto& sector : attacked_sectors) {
			for (const auto& cl : g_sector[sector.second][sector.first]) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) continue;

				if (ST_INGAME != other->m_state) { continue; }
				if (other->m_id == c_id) { continue; }
				if (!can_see(c_id, other->m_id)) { continue; }

				if (is_npc(other->m_id)) { 
					for (const auto& coord : attacked_coords) {
						if ((coord.first == other->m_x) && (coord.second == other->m_y)) {
							other->damage(1 + level);
						}
					}
				}
			}
		}
		break;
	}
}

bool can_see(int from, int to) {
	std::shared_ptr<SESSION> client_from = g_clients.at(from);
	std::shared_ptr<SESSION> client_to = g_clients.at(to);
	if (!client_from || !client_to) return false;

	if (abs(client_from->m_x - client_to->m_x) > VIEW_RANGE) {
		return false;
	}
	return (abs(client_from->m_y - client_to->m_y) <= VIEW_RANGE);
}

void update_sector(int c_id, short old_x, short old_y, short new_x, short new_y) {
	short old_sx = old_x / SECTOR_WIDTH;
	short old_sy = old_y / SECTOR_HEIGHT;

	short new_sx = new_x / SECTOR_WIDTH;
	short new_sy = new_y / SECTOR_HEIGHT;

	if ((old_sx != new_sx) || (old_sy != new_sy)) {
		{
			std::lock_guard<std::mutex> lock(g_mutex[old_sy][old_sx]);
			g_sector[old_sy][old_sx].erase(c_id);
		}

		std::lock_guard<std::mutex> lock(g_mutex[new_sy][new_sx]);
		g_sector[new_sy][new_sx].insert(c_id);
	}
}

void init_npc() {
	std::cout << "NPC intialize begin" << std::endl;
	for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>(i);
		p->m_state = ST_INGAME;
		p->m_level = 4;
		g_clients.insert(std::make_pair(p->m_id, p));

		// Add Npc into Sector
		short sx = p->m_x / SECTOR_WIDTH;
		short sy = p->m_y / SECTOR_HEIGHT;
		{
			std::lock_guard<std::mutex> sl(g_mutex[sy][sx]);
			g_sector[sy][sx].insert(p->m_id);
		}
	}
	std::cout << "NPC initialize end" << std::endl;
}

bool is_player(int c_id) {
	return (c_id < MAX_USER);
}

bool is_npc(int c_id) {
	return !is_player(c_id);
}
