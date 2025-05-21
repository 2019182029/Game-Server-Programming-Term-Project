#include <WS2tcpip.h>
#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <unordered_map>

#include "..\protocol.h"
#include "EXP_OVER.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(linker,"/entry:WinMainCRTStartup /subsystem:console")

//////////////////////////////////////////////////
// Windows
HWND g_hWnd;
HINSTANCE g_hInst;
LPCTSTR lpszClass = L"Window Class";
LPCTSTR lpszWindowName = L"Game Server Programming";

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);

//////////////////////////////////////////////////
// Server
constexpr short SERVER_PORT = 3000;

RECT rect;
SOCKET g_socket;
EXP_OVER g_recv_over;
int g_remained;

void init_socket();
void do_send(void* buff);
void process_packet(char* p);

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flags);
void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flags);

//////////////////////////////////////////////////
// Player
HBITMAP player_hBitmap[5];
BITMAP player_bmp[5];

short camera_x = 0;
short camera_y = 0;

class PLAYER {
public:
	int	m_id;
	short m_x, m_y;
	int	m_hp; int m_max_hp;
	int	m_exp; int m_level;

public:
	void update() {
		camera_x = m_x - (S_VISIBLE_TILES / 2);
		camera_y = m_y - (S_VISIBLE_TILES / 2);
	}

	void move(short dx, short dy) {
		short new_x = m_x + dx;
		short new_y = m_y + dy;

		if ((0 <= new_x) && (new_x < W_WIDTH)) { m_x = new_x; }
		if ((0 <= new_y) && (new_y < W_HEIGHT)) { m_y = new_y; }

		update();
	}

	void print(HDC hDC, short x, short y) const {
		HDC mDC = CreateCompatibleDC(hDC);
		HGDIOBJ hBitmap = SelectObject(mDC, player_hBitmap[m_level]);

		TransparentBlt(hDC, x * S_TILE_WIDTH, y * S_TILE_HEIGHT, S_TILE_WIDTH, S_TILE_HEIGHT,
			mDC, 0, 0, player_bmp[m_level].bmWidth, player_bmp[m_level].bmHeight, RGB(255, 255, 255));

		SelectObject(mDC, hBitmap);
		DeleteDC(mDC);
	}
};

PLAYER player;
std::unordered_map<int, PLAYER> others;

//////////////////////////////////////////////////
// Background
char tile_map[W_WIDTH][W_HEIGHT];

class BACKGROUND {
public:
	HBITMAP m_hBitmap[2];
	BITMAP m_bmp[2];

public:
	void print(HDC hDC) const {
		for (short y = 0; y < S_VISIBLE_TILES; ++y) {
			for (short x = 0; x < S_VISIBLE_TILES; ++x) {
				// Tile
				short map_x = camera_x + x;
				short map_y = camera_y + y;

				if (map_x < 0 || map_y < 0 || map_x >= W_WIDTH || map_y >= W_HEIGHT) { continue; }

				char tile_type = tile_map[map_y][map_x];
				HDC tileDC = CreateCompatibleDC(hDC);
				HGDIOBJ hBitmap = SelectObject(tileDC, m_hBitmap[tile_type]);

				StretchBlt(hDC, x * S_TILE_WIDTH, y * S_TILE_HEIGHT, S_TILE_WIDTH, S_TILE_HEIGHT,
					tileDC, 0, 0, m_bmp[tile_type].bmWidth, m_bmp[tile_type].bmHeight, SRCCOPY);

				SelectObject(tileDC, hBitmap);
				DeleteDC(tileDC);

				// Player
				if ((map_x == player.m_x) && (map_y == player.m_y)) {
					player.print(hDC, x, y);
				}

				// Others
				for (const auto& other : others) {
					if ((map_x == other.second.m_x) && (map_y == other.second.m_y)) {
						other.second.print(hDC, x, y);
					}
				}
			}
		}
	}
};

BACKGROUND bg;

//////////////////////////////////////////////////
// WINMAIN
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE	hPrevInstance, _In_ LPSTR lpszCmdParam, _In_ int nCmdShow) {
	MSG Message;
	WNDCLASSEX WndClass;
	g_hInst = hInstance;

	WndClass.cbSize = sizeof(WndClass);
	WndClass.style = CS_HREDRAW | CS_VREDRAW;
	WndClass.lpfnWndProc = (WNDPROC)WndProc;
	WndClass.cbClsExtra = 0;
	WndClass.hInstance = hInstance;
	WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	WndClass.lpszMenuName = NULL;
	WndClass.lpszClassName = lpszClass;
	WndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	RegisterClassEx(&WndClass);

	g_hWnd = CreateWindow(lpszClass, lpszWindowName, WS_OVERLAPPEDWINDOW,
		0, 0, S_WIDTH, S_HEIGHT,
		NULL, (HMENU)NULL, hInstance, NULL);

	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);

	while (GetMessage(&Message, 0, 0, 0)) {
		TranslateMessage(&Message);
		DispatchMessage(&Message);

		SleepEx(0, TRUE);
	}
	return Message.wParam;
}

//////////////////////////////////////////////////
// WNDPROC
LRESULT WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam) {
	PAINTSTRUCT ps;
	HDC hDC, mDC;

	switch (iMessage) {
	case WM_CREATE:
		player_hBitmap[0] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\pawn.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		GetObject(player_hBitmap[0], sizeof(BITMAP), &player_bmp[0]);

		bg.m_hBitmap[0] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\white.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		bg.m_hBitmap[1] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\black.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		GetObject(bg.m_hBitmap[0], sizeof(BITMAP), &bg.m_bmp[0]);
		GetObject(bg.m_hBitmap[1], sizeof(BITMAP), &bg.m_bmp[1]);
		GetClientRect(hWnd, &rect);

		for (int y = 0; y < W_HEIGHT; ++y) {
			for (int x = 0; x < W_WIDTH; ++x) {
				tile_map[y][x] = ((x / 3) + (y / 3)) % 2;
			}
		}

		init_socket();
		break;

	case WM_KEYDOWN: 
		switch (wParam) {
		case VK_UP:
		case VK_DOWN:
		case VK_LEFT:
		case VK_RIGHT:
			CS_MOVE_PACKET p;
			p.size = sizeof(CS_MOVE_PACKET);
			p.type = CS_MOVE;
			switch (wParam) {
			case VK_UP:    player.move(0, -1); p.direction = 0; break;
			case VK_DOWN:  player.move(0,  1); p.direction = 1; break;
			case VK_LEFT:  player.move(-1, 0); p.direction = 2; break;
			case VK_RIGHT: player.move(1,  0); p.direction = 3; break;
			}
			do_send(&p);
			InvalidateRect(g_hWnd, NULL, FALSE);
			break;
		}
		break;

	case WM_PAINT: {
		hDC = BeginPaint(hWnd, &ps);
		mDC = CreateCompatibleDC(hDC);

		HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rect.right, rect.bottom);
		HGDIOBJ old_hBitmap = SelectObject(mDC, hBitmap);

		bg.print(mDC);

		BitBlt(hDC, 0, 0, rect.right, rect.bottom, mDC, 0, 0, SRCCOPY);

		SelectObject(mDC, old_hBitmap);
		DeleteObject(hBitmap);
		DeleteDC(mDC);
		EndPaint(hWnd, &ps);
		break;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, iMessage, wParam, lParam);
}

// Initialize Socket and WSAConnect to Server
void init_socket() {
	WSADATA WSAData;
	auto ret = WSAStartup(MAKEWORD(2, 2), &WSAData);
	if (0 != ret) { std::cout << "WSAStartup Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSAStartup Succeed" << std::endl; }

	g_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == g_socket) { std::cout << "Client Socket Create Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSASocket Succeed" << std::endl; }

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	ret = WSAConnect(g_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(SOCKADDR_IN), NULL, NULL, NULL, NULL);
	if (SOCKET_ERROR == ret) { std::cout << "WSAConnect Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSAConnect Succeed" << std::endl; }

	CS_LOGIN_PACKET p;
	p.size = sizeof(CS_LOGIN_PACKET);
	p.type = CS_LOGIN;
	do_send(&p);

	DWORD recv_bytes;
	DWORD recv_flag = 0;
	ret = WSARecv(g_socket, g_recv_over.m_wsabuf, 1, &recv_bytes, &recv_flag, &g_recv_over.m_over, recv_callback);
}

void do_send(void* buff) {
	EXP_OVER* o = new EXP_OVER;
	unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];
	memcpy(o->m_buffer, buff, packet_size);
	o->m_wsabuf[0].len = packet_size;

	DWORD send_bytes;
	auto ret = WSASend(g_socket, o->m_wsabuf, 1, &send_bytes, 0, &(o->m_over), send_callback);
	if (ret == SOCKET_ERROR) {
		if (WSAGetLastError() != WSA_IO_PENDING) {
			delete o;
			return;
		}
	}
}

void process_packet(char* packet) {
	char packet_type = packet[1];

	switch (packet_type) {
	case SC_LOGIN_INFO: {
		SC_LOGIN_INFO_PACKET* p = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(packet);
		player.m_x = p->x;
		player.m_y = p->y;
		player.m_id = p->id;
		player.m_hp = p->hp;
		player.m_max_hp = p->max_hp;
		player.m_exp = p->exp;
		player.m_level = p->level;
		player.update();
		break;
	}

	case SC_ADD_OBJECT: {
		SC_ADD_OBJECT_PACKET* p = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(packet);
		PLAYER other;
		other.m_id = p->id;
		other.m_x = p->x;
		other.m_y = p->y;
		other.m_level = p->level;
		others.emplace(other.m_id, other);
		break;
	}

	case SC_MOVE_OBJECT: {
		SC_MOVE_OBJECT_PACKET* p = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(packet);
		if (player.m_id == p->id) {
			player.m_x = p->x;
			player.m_y = p->y;
			break;
		}
		others.at(p->id).m_x = p->x;
		others.at(p->id).m_y = p->y;
		break;
	}

	case SC_REMOVE_OBJECT: {
		SC_REMOVE_OBJECT_PACKET* p = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(packet);
		others.erase(p->id);
		break;
	}
	}

	InvalidateRect(g_hWnd, NULL, FALSE);
}

void recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flags) {
	if ((0 != err) || (0 == num_bytes)) {
		std::cout << "Server Disconnected" << std::endl;
		return;
	}

	// Process Packet
	char* p = g_recv_over.m_buffer;
	unsigned char packet_size = p[0];
	int remained = g_remained + num_bytes;

	while (packet_size <= remained) {
		process_packet(p);
		p += packet_size;
		remained -= packet_size;
		if (!remained) {
			break;
		}
		packet_size = p[0];
	}

	g_remained = remained;
	if (remained) {
		memcpy(g_recv_over.m_buffer, p, remained);
	}

	DWORD recv_bytes;
	DWORD recv_flag = 0;
	g_recv_over.m_wsabuf[0].buf = g_recv_over.m_buffer + g_remained;
	g_recv_over.m_wsabuf[0].len = sizeof(g_recv_over.m_buffer) - g_remained;
	WSARecv(g_socket, g_recv_over.m_wsabuf, 1, &recv_bytes, &recv_flag, &g_recv_over.m_over, recv_callback);
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flags) {
	EXP_OVER* p = reinterpret_cast<EXP_OVER*>(p_over);
	delete p;
}