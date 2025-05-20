#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <concurrent_unordered_map.h>

#include "..\protocol.h"
#include "SESSION.h"

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

constexpr short SERVER_PORT = 3000;

HANDLE g_hIOCP;
SOCKET g_s_socket, g_c_socket;
EXP_OVER g_accept_over{ IO_ACCEPT };

int g_new_id = 0;
concurrency::concurrent_unordered_map<int, std::atomic<std::shared_ptr<SESSION>>> g_clients;

void worker();

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
			continue;
		}

		if ((eo->m_io_type == IO_RECV || eo->m_io_type == IO_SEND) && (0 == io_size)) {
			continue;
		}

		switch (eo->m_io_type) {
		case IO_ACCEPT: {
			int client_id = g_new_id++;
			std::shared_ptr<SESSION> p = std::make_shared<SESSION>();
			p->m_id = client_id;
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), g_hIOCP, client_id, 0);
			p->m_c_socket = g_c_socket;
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

			break;
		}
		}
	}
}