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

void worker();
bool can_see(int from, int to);
void disconnect(int c_id);
void process_packet(int c_id, char* packet);

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

bool can_see(int from, int to) {
	std::shared_ptr<SESSION> client_from = g_clients.at(from);
	std::shared_ptr<SESSION> client_to = g_clients.at(to);
	if (!client_from || !client_to) return false;

	if (abs(client_from->m_x - client_to->m_x) > VIEW_RANGE) { 
		return false; 
	}
	return (abs(client_from->m_y - client_to->m_y) <= VIEW_RANGE);
}

void disconnect(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

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
					other->send_add_object(c_id);

					std::cout << "Client " << c_id << "now can See" << other->m_id << std::endl;
				}
			}
		}
		break;
	}

	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		short new_x = client->m_x;
		short new_y = client->m_y;
		switch (p->direction) {
		case 0: --new_y; break;
		case 1: ++new_y; break;
		case 2: --new_x; break;
		case 3: ++new_x; break;
		}
		if ((0 <= new_x) && (new_x < W_WIDTH)) { client->m_x = new_x; }
		if ((0 <= new_y) && (new_y < W_HEIGHT)) { client->m_y = new_y; }
		break;
	}
	}
}
