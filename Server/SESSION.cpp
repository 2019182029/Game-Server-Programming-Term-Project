#include <iostream>

#include "SESSION.h"

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

SESSION::SESSION(int id, SOCKET c_socket) : m_c_socket(c_socket), m_id(id) {
	m_remained = 0;

	do_recv();
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