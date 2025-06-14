#define _CRT_SECURE_NO_WARNINGS

#include <WS2tcpip.h>
#include <windows.h>
#include <tchar.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include "..\Server\protocol.h"
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

HWND hText, hConnectEdit, hConnectButton;
HWND hLoginIdText, hLoginPwText, hLoginIdEdit, hLoginPwEdit, hLoginButton, hRegisterButton;
HWND hAvatarImage[3], hAvatarButton[3];

bool bGameStart = false;

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);
void ShowLoginScreen(HWND hWnd);
void ShowAvatarSelectionScreen(HWND hWnd, const std::vector<AVATAR>& avatars);
void Init(HWND hWnd);
void StartGame(HWND hWnd);

//////////////////////////////////////////////////
// Server
RECT rect;
SOCKET g_socket;
EXP_OVER g_recv_over;
int g_remained;

void init_socket();
bool try_connect_to_server(TCHAR* buffer);
void do_send(void* buff);
void process_packet(char* p);

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flags);
void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flags);

//////////////////////////////////////////////////
// Player
BITMAP player_bmp[6];
HBITMAP player_hBitmap[6];

std::vector<AVATAR> avatar_list;

short camera_x = 0;
short camera_y = 0;

std::vector<uint8_t> terrain((W_WIDTH* W_HEIGHT + 7) / 8, 0);

struct pair_hash {
	size_t operator()(const std::pair<short, short>& p) const {
		return std::hash<int>()((p.first << 16) ^ p.second);
	}
};

std::unordered_map<std::pair<short, short>, std::chrono::high_resolution_clock::time_point, pair_hash> attacked_coords;

void print_ui(HDC hDC);
bool load_terrain(const std::string& filename);
bool get_tile(int x, int y);

class PLAYER {
public:
	int	m_id;
	short m_x, m_y;
	int	m_hp; int m_max_hp;
	int	m_exp; int m_level;
	char m_name[NAME_SIZE];
	bool m_is_alive;

public:
	PLAYER() {
		m_id = -1;
		m_x = 0; m_y = 0;
		m_hp = 10; m_max_hp = 10;
		m_exp = 0; m_level = 0;
		m_is_alive = true;
	}

	void update_camera() {
		camera_x = m_x - (S_VISIBLE_TILES / 2);
		camera_y = m_y - (S_VISIBLE_TILES / 2);
	}

	void move(short dx, short dy) {
		short new_x = m_x + dx;
		short new_y = m_y + dy;

		if (true == get_tile(new_x, new_y)) { return; }

		if ((0 <= new_x) && (new_x < W_WIDTH)) { m_x = new_x; }
		if ((0 <= new_y) && (new_y < W_HEIGHT)) { m_y = new_y; }

		update_camera();
	}

	void attack() {
		auto current_time = std::chrono::high_resolution_clock::now();

		switch (m_level) {
		case PAWN:
			attacked_coords[{ m_x - 1, m_y - 1 }] = current_time;
			attacked_coords[{ m_x + 1, m_y - 1 }] = current_time;
			break;

		case BISHOP:
			attacked_coords[{ m_x - 1, m_y - 1 }] = current_time;
			attacked_coords[{ m_x + 1, m_y - 1 }] = current_time;
			attacked_coords[{ m_x - 1, m_y + 1 }] = current_time;
			attacked_coords[{ m_x + 1, m_y + 1 }] = current_time;
			break;

		case KNIGHT:
			attacked_coords[{ m_x - 1, m_y - 2 }] = current_time;
			attacked_coords[{ m_x + 1, m_y - 2 }] = current_time;
			attacked_coords[{ m_x - 1, m_y + 2 }] = current_time;
			attacked_coords[{ m_x + 1, m_y + 2 }] = current_time;
			attacked_coords[{ m_x - 2, m_y - 1 }] = current_time;
			attacked_coords[{ m_x - 2, m_y + 1 }] = current_time;
			attacked_coords[{ m_x + 2, m_y - 1 }] = current_time;
			attacked_coords[{ m_x + 2, m_y + 1 }] = current_time;
			break;

		case ROOK:
			attacked_coords[{ m_x,	   m_y - 1 }] = current_time;
			attacked_coords[{ m_x,     m_y + 1 }] = current_time;
			attacked_coords[{ m_x - 1, m_y	   }] = current_time;
			attacked_coords[{ m_x + 1, m_y     }] = current_time;
			break;

		case KING:
		case QUEEN:
			attacked_coords[{ m_x - 1, m_y - 1 }] = current_time;
			attacked_coords[{ m_x,     m_y - 1 }] = current_time;
			attacked_coords[{ m_x + 1, m_y - 1 }] = current_time;
			attacked_coords[{ m_x - 1, m_y     }] = current_time;
			attacked_coords[{ m_x + 1, m_y     }] = current_time;
			attacked_coords[{ m_x - 1, m_y + 1 }] = current_time;
			attacked_coords[{ m_x,     m_y + 1 }] = current_time;
			attacked_coords[{ m_x + 1, m_y + 1 }] = current_time;
			break;
		}
	}

	void print(HDC hDC, short x, short y) const {
		HDC mDC = CreateCompatibleDC(hDC);
		HGDIOBJ hBitmap = SelectObject(mDC, player_hBitmap[m_level]);

		// Character
		TransparentBlt(hDC, x * S_TILE_WIDTH, y * S_TILE_HEIGHT, S_TILE_WIDTH, S_TILE_HEIGHT,
			mDC, 0, 0, player_bmp[m_level].bmWidth, player_bmp[m_level].bmHeight, RGB(255, 255, 255));

		// Name
		SetBkMode(hDC, TRANSPARENT);
		SetTextColor(hDC, RGB(255, 255, 255));

		RECT nameRect;
		nameRect.top = (y - 1) * S_TILE_HEIGHT;
		nameRect.bottom = (y + 1) * S_TILE_HEIGHT;
		nameRect.left = (x - 1) * S_TILE_WIDTH;
		nameRect.right = (x + 2) * S_TILE_WIDTH;

		DrawTextA(hDC, m_name, -1, &nameRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

		SelectObject(mDC, hBitmap);
		DeleteDC(mDC);
	}
};

PLAYER player;
std::unordered_map<int, PLAYER> others;

//////////////////////////////////////////////////
// UI
HPEN cPen, hpPen, ePen;
HBRUSH rBrush, gBrush, wBrush;

bool chat = false;
std::string chat_input;
std::vector<std::string> chat_log;
constexpr int MAX_CHAT_LINE = 10;

void draw_chat_box(HDC hDC, int x, int y, int width, int height) {
	HDC memDC = CreateCompatibleDC(hDC);
	HBITMAP hBitmap = CreateCompatibleBitmap(hDC, width, height);
	HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

	HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50)); 
	RECT rc = { 0, 0, width, height };
	FillRect(memDC, &rc, hBrush);
	DeleteObject(hBrush);

	BLENDFUNCTION blend = {};
	blend.BlendOp = AC_SRC_OVER;
	blend.SourceConstantAlpha = 192;
	blend.AlphaFormat = 0;

	AlphaBlend(hDC, x, y, width, height, memDC, 0, 0, width, height, blend);

	SelectObject(memDC, oldBitmap);
	DeleteObject(hBitmap);
	DeleteDC(memDC);
}

void print_ui(HDC hDC) {
	HDC mDC = CreateCompatibleDC(hDC);
	HGDIOBJ hBitmap = SelectObject(mDC, player_hBitmap[player.m_level]);

	// Character
	HPEN oldPen = (HPEN)SelectObject(hDC, cPen);
	HBRUSH oldBrush = (HBRUSH)SelectObject(hDC, wBrush);
	RoundRect(hDC, 5, 5, 5 + 100, 5 + 100, 10, 10);
	TransparentBlt(hDC, 5, 5, 100, 100,
		mDC, 0, 0, player_bmp[player.m_level].bmWidth, player_bmp[player.m_level].bmHeight, RGB(255, 255, 255));

	// Hp
	SelectObject(hDC, hpPen);
	SelectObject(hDC, rBrush);
	for (int i = 0; i < player.m_hp; ++i) {
		Rectangle(hDC, 110 + 25 * i, 5,
			110 + 25 * (i + 1), 25);
	}

	// Exp
	SelectObject(hDC, ePen);
	SelectObject(hDC, gBrush);
	if (3 != player.m_level) {
		float current_exp = (player.m_exp % 100) / 100.0f;
		Rectangle(hDC, 110, 35,
			110 + static_cast<int>(current_exp * 250), 60);

		SelectObject(hDC, wBrush);
		Rectangle(hDC, 110 + static_cast<int>(current_exp * 250), 35,
			110 + 250, 60);
	} else {
		Rectangle(hDC, 110, 35,
			110 + 250, 60);
	}

	// Chat
	int line_height = 20;

	int chat_width = 400;
	int chat_height = 120;

	int base_x = 10;
	int base_y = rect.bottom - line_height - 10;

	SetBkMode(hDC, TRANSPARENT);
	SetBkColor(hDC, RGB(0, 0, 0));
	SetTextColor(hDC, RGB(255, 255, 255));

	if (chat) {
		draw_chat_box(hDC, base_x, base_y + 3, chat_width, line_height);
		RECT r = { base_x, base_y + 3, base_x + chat_width, base_y + line_height + 3 };
		DrawTextA(hDC, chat_input.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE);
	}

	if (!chat_log.empty()) {
		int total = static_cast<int>(chat_log.size());
		int visible = min(total, MAX_CHAT_LINE);

		draw_chat_box(hDC, base_x, base_y - total * line_height, chat_width, total * line_height);
		for (int i = 0; i < visible; ++i) {
			int log_index = total - visible + i;
			RECT r = { base_x, base_y - (visible - i) * line_height, base_x + chat_width, base_y - (visible - 1 - i) * line_height };
			DrawTextA(hDC, chat_log[log_index].c_str(), -1, &r, DT_LEFT | DT_SINGLELINE);
		}
	}

	SelectObject(hDC, oldPen);
	SelectObject(hDC, oldBrush);

	SelectObject(mDC, hBitmap);
	DeleteDC(mDC);
}

bool load_terrain(const std::string& filename) {
	std::ifstream ifs(filename, std::ios::binary);

	if (!ifs.is_open()) { return false; }

	terrain.resize((2000 * 2000 + 7) / 8);

	ifs.read(reinterpret_cast<char*>(terrain.data()), terrain.size());
	ifs.close();

	return true;
}

bool get_tile(int x, int y) {
	int idx = y * W_WIDTH + x;

	return (terrain[idx / 8] >> (idx % 8)) & 1;
}

//////////////////////////////////////////////////
// Background
char tile_map[W_WIDTH][W_HEIGHT];

class BACKGROUND {
public:
	HBITMAP m_hBitmap[3];
	BITMAP m_bmp[3];

public:
	void visualize_attack(HDC hDC, short x, short y) const {
		HDC mDC = CreateCompatibleDC(hDC);
		HBITMAP hBmp = CreateCompatibleBitmap(hDC, S_TILE_WIDTH, S_TILE_HEIGHT);
		HGDIOBJ oldBmp = SelectObject(mDC, hBmp);

		HBRUSH redBrush = CreateSolidBrush(RGB(255, 0, 0));
		RECT rect = { 0, 0, S_TILE_WIDTH, S_TILE_HEIGHT };
		FillRect(mDC, &rect, redBrush);
		DeleteObject(redBrush);

		BLENDFUNCTION blend = {};
		blend.BlendOp = AC_SRC_OVER;
		blend.SourceConstantAlpha = 128;
		blend.AlphaFormat = 0;

		AlphaBlend(hDC, x * S_TILE_WIDTH, y * S_TILE_HEIGHT, S_TILE_WIDTH, S_TILE_HEIGHT,
			mDC, 0, 0, S_TILE_WIDTH, S_TILE_HEIGHT, blend);

		SelectObject(mDC, oldBmp);
		DeleteObject(hBmp);
		DeleteDC(mDC);
	};

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
			}
		}

		auto current_time = std::chrono::high_resolution_clock::now();

		for (short y = 0; y < S_VISIBLE_TILES; ++y) {
			for (short x = 0; x < S_VISIBLE_TILES; ++x) {
				short map_x = camera_x + x;
				short map_y = camera_y + y;

				// Others
				for (const auto& other : others) {
					if (true == other.second.m_is_alive) {
						if ((map_x == other.second.m_x) && (map_y == other.second.m_y)) {
							other.second.print(hDC, x, y);
						}
					}
				}

				// Player
				if ((map_x == player.m_x) && (map_y == player.m_y)) {
					if (true == player.m_is_alive) {
						player.print(hDC, x, y);
					}
				}

				// Attacked Coords
				auto it = attacked_coords.find({ map_x, map_y });

				if (it != attacked_coords.end()) {

					if ((current_time - it->second) < std::chrono::milliseconds(100)) {
						visualize_attack(hDC, x, y);
					} else {
						// Delete Coords from Attacked Coords
						attacked_coords.erase(it);
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
int CenterX(int controlWidth) { return (S_WIDTH - controlWidth) / 2; }
int CenterY(int controlHeight) { return (S_HEIGHT - controlHeight) / 2; }

LRESULT WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam) {
	PAINTSTRUCT ps;
	HDC hDC, mDC;

	switch (iMessage) {
	case WM_CREATE: {
		Init(hWnd);
		init_socket();

		int y = CenterY(90);

		// IP Address Input Control
		hText = CreateWindow(L"STATIC", L"Enter Server IP Address",
			WS_CHILD | WS_VISIBLE | SS_CENTER,
			CenterX(200), y, 200, 20,
			hWnd, (HMENU)1001, nullptr, nullptr);

		hConnectEdit = CreateWindow(L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_BORDER,
			CenterX(200), y + 30, 200, 25,
			hWnd, (HMENU)1002, nullptr, nullptr);

		hConnectButton = CreateWindow(L"BUTTON", L"Confirm",
			WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			CenterX(80), y + 65, 80, 25,
			hWnd, (HMENU)1003, nullptr, nullptr);
		break;
	}

	case WM_COMMAND: {
		switch (LOWORD(wParam)) {
		case 1003: {  // IP Address 
			TCHAR buffer[64];
			GetWindowText(hConnectEdit, buffer, sizeof(buffer));

			if (try_connect_to_server(buffer)) {
				ShowLoginScreen(hWnd);
			} else {
				MessageBox(hWnd, L"Server Connection Error", L"Error", MB_OK | MB_ICONERROR);
			}
			break;
		}

		case 2003: {  // Login
			char id_buffer[ID_SIZE] = {};
			char pw_buffer[PW_SIZE] = {};

			GetWindowTextA(hLoginIdEdit, id_buffer, ID_SIZE);
			GetWindowTextA(hLoginPwEdit, pw_buffer, PW_SIZE);

			strncpy_s(player.m_name, id_buffer, ID_SIZE - 1);

			CS_USER_LOGIN_PACKET p;
			p.size = sizeof(CS_USER_LOGIN_PACKET);
			p.type = CS_USER_LOGIN;
			strncpy_s(p.id, id_buffer, ID_SIZE - 1);
			strncpy_s(p.pw, pw_buffer, PW_SIZE - 1);
			do_send(&p);

			DWORD recv_bytes;
			DWORD recv_flag = 0;
			auto ret = WSARecv(g_socket, g_recv_over.m_wsabuf, 1, &recv_bytes, &recv_flag, &g_recv_over.m_over, recv_callback);
			break;
		}

		case 2004: {  // Register
			char id_buffer[ID_SIZE] = {};
			char pw_buffer[PW_SIZE] = {};

			GetWindowTextA(hLoginIdEdit, id_buffer, ID_SIZE);
			GetWindowTextA(hLoginPwEdit, pw_buffer, PW_SIZE);

			strncpy_s(player.m_name, id_buffer, ID_SIZE - 1);

			CS_USER_REGISTER_PACKET p;
			p.size = sizeof(CS_USER_REGISTER_PACKET);
			p.type = CS_USER_REGISTER;
			strncpy_s(p.id, id_buffer, ID_SIZE - 1);
			strncpy_s(p.pw, pw_buffer, PW_SIZE - 1);
			do_send(&p);

			DWORD recv_bytes;
			DWORD recv_flag = 0;
			auto ret = WSARecv(g_socket, g_recv_over.m_wsabuf, 1, &recv_bytes, &recv_flag, &g_recv_over.m_over, recv_callback);
			break;
		}

		case 3000:
		case 3001:
		case 3002: {
			int id = LOWORD(wParam);

			HWND hButton = GetDlgItem(hWnd, id);

			wchar_t text[16];
			GetWindowText(hButton, text, 16);

			if (wcscmp(text, L"Select") == 0) {
				CS_SELECT_AVATAR_PACKET p;
				p.size = sizeof(CS_SELECT_AVATAR_PACKET);
				p.type = CS_SELECT_AVATAR;
				p.avatar_id = avatar_list[id - 3000].avatar_id;
				do_send(&p);
			} else if (wcscmp(text, L"Create") == 0) {
				CS_CREATE_AVATAR_PACKET p;
				p.size = sizeof(CS_CREATE_AVATAR_PACKET);
				p.type = CS_CREATE_AVATAR;
				p.slot_id = id - 3000;
				do_send(&p);
			}
			break;
		}
		}
		break;
	}

	case WM_TIMER:
		InvalidateRect(g_hWnd, NULL, FALSE);
		break;

	case WM_CHAR:
		if (chat) {
			if ((wParam >= 32) && (wParam < 127)) {
				if (chat_input.size() < CHAT_SIZE) {
					chat_input.push_back((char)wParam);
				}
			}
		}
		InvalidateRect(g_hWnd, NULL, FALSE);
		break;

	case WM_KEYDOWN: 
		if (chat) {
			switch (wParam) {
			case VK_BACK:
				if (!chat_input.empty()) {
					chat_input.pop_back();
				}
				break;

			case VK_RETURN:
				if (!chat_input.empty()) {
					CS_CHAT_PACKET p;
					p.size = static_cast<unsigned char>(sizeof(CS_CHAT_PACKET));
					p.type = CS_CHAT;
					strcpy_s(p.mess, chat_input.c_str());
					do_send(&p);

					char buf[128];
					sprintf_s(buf, "[%s] : %s", player.m_name, chat_input);
					chat_log.emplace_back(buf);
					if (chat_log.size() > MAX_CHAT_LINE) {
						chat_log.erase(chat_log.begin());
					}
				}
				chat_input.clear();
				chat = false;
				break;

			case VK_ESCAPE:
				chat_input.clear();
				chat = false;
				break;
			}
		} else {
			switch (wParam) {
			case VK_UP:
			case VK_DOWN:
			case VK_LEFT:
			case VK_RIGHT: {
				if (player.m_is_alive) {
					CS_MOVE_PACKET p;
					p.size = sizeof(CS_MOVE_PACKET);
					p.type = CS_MOVE;
					switch (wParam) {
					case VK_UP:    player.move(0, -1);	p.direction = 0; break;
					case VK_DOWN:  player.move(0, 1);	p.direction = 1; break;
					case VK_LEFT:  player.move(-1, 0);	p.direction = 2; break;
					case VK_RIGHT: player.move(1, 0);	p.direction = 3; break;
					}
					do_send(&p);
				}
				break;
			}

			case VK_SPACE: {
				if (player.m_is_alive) {
					CS_ATTACK_PACKET p;
					p.size = sizeof(CS_ATTACK_PACKET);
					p.type = CS_ATTACK;
					do_send(&p);

					player.attack();
				}
				break;
			}

			case VK_RETURN:
				chat = true;
				break;

			case VK_ESCAPE: {
				CS_LOGOUT_PACKET p;
				p.size = sizeof(CS_LOGOUT_PACKET);
				p.type = CS_LOGOUT;
				do_send(&p);
				break;
			}
			}
		}
		InvalidateRect(g_hWnd, NULL, FALSE);
		break;

	case WM_PAINT: {
		hDC = BeginPaint(hWnd, &ps);
		mDC = CreateCompatibleDC(hDC);

		HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rect.right, rect.bottom);
		HGDIOBJ old_hBitmap = SelectObject(mDC, hBitmap);

		if (bGameStart) {
			bg.print(mDC);
			print_ui(mDC);
		}

		BitBlt(hDC, 0, 0, rect.right, rect.bottom, mDC, 0, 0, SRCCOPY);

		SelectObject(mDC, old_hBitmap);
		DeleteObject(hBitmap);
		DeleteDC(mDC);
		EndPaint(hWnd, &ps);
		break;
	}

	case WM_DESTROY:
		for (int i = 0; i < 3; ++i) {
			HBITMAP hBmp = (HBITMAP)SendMessage(hAvatarImage[i], STM_GETIMAGE, IMAGE_BITMAP, 0);
			if (hBmp) DeleteObject(hBmp);
		}

		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, iMessage, wParam, lParam);
}

void ShowLoginScreen(HWND hWnd) {
	// Hide Ip Address Input Control
	ShowWindow(hText, SW_HIDE);
	ShowWindow(hConnectEdit, SW_HIDE);
	ShowWindow(hConnectButton, SW_HIDE);
	
	// ID, PW
	const int label_width = 70;
	const int edit_width = 200;
	const int button_width = 80;
	const int control_height = 25;
	const int vertical_spacing = 15;
	const int button_height = 30;
	const int button_spacing = 20;

	const int total_height = (control_height * 2) + (vertical_spacing * 2) + button_height;
	const int total_button_width = (button_width * 2) + button_spacing;

	int x = CenterX(total_button_width);
	int y = CenterY(total_height);

	hLoginIdText = CreateWindow(L"STATIC", L"ID :",
		WS_CHILD | WS_VISIBLE,
		CenterX(edit_width) - (label_width + 10), y, label_width, control_height,
		hWnd, nullptr, nullptr, nullptr);

	hLoginIdEdit = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER,
		CenterX(edit_width), y, edit_width, control_height,
		hWnd, (HMENU)2001, nullptr, nullptr);

	y += control_height + vertical_spacing;

	hLoginPwText = CreateWindow(L"STATIC", L"Password :",
		WS_CHILD | WS_VISIBLE,
		CenterX(edit_width) - (label_width + 10), y, label_width, control_height,
		hWnd, nullptr, nullptr, nullptr);

	hLoginPwEdit = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD,
		CenterX(edit_width), y, edit_width, control_height,
		hWnd, (HMENU)2002, nullptr, nullptr);

	y += control_height + vertical_spacing;

	hLoginButton = CreateWindow(L"BUTTON", L"Login",
		WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
		x, y, button_width, button_height,
		hWnd, (HMENU)2003, nullptr, nullptr);

	hRegisterButton = CreateWindow(L"BUTTON", L"Register",
		WS_CHILD | WS_VISIBLE,
		x + button_width + button_spacing, y, button_width, button_height,
		hWnd, (HMENU)2004, nullptr, nullptr);
}

HBITMAP ResizeBitmap(HBITMAP hBmp, int width, int height) {
	HDC hdcSrc = CreateCompatibleDC(NULL);
	HDC hdcDst = CreateCompatibleDC(NULL);

	BITMAP bmp;
	GetObject(hBmp, sizeof(BITMAP), &bmp);

	HBITMAP hResized = CreateCompatibleBitmap(hdcSrc, width, height);
	HBITMAP hOldSrc = (HBITMAP)SelectObject(hdcSrc, hBmp);
	HBITMAP hOldDst = (HBITMAP)SelectObject(hdcDst, hResized);

	SetStretchBltMode(hdcDst, HALFTONE);
	StretchBlt(hdcDst, 0, 0, width, height, hdcSrc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);

	SelectObject(hdcSrc, hOldSrc);
	SelectObject(hdcDst, hOldDst);
	DeleteDC(hdcSrc);
	DeleteDC(hdcDst);

	return hResized;
}

HBITMAP CreateWhiteBitmap(int width, int height) {
	HDC hdcScreen = GetDC(NULL);
	HDC hdcMem = CreateCompatibleDC(hdcScreen);
	HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
	HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBitmap);

	HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
	RECT rect = { 0, 0, width, height };
	FillRect(hdcMem, &rect, hWhiteBrush);

	SelectObject(hdcMem, hOld);
	DeleteDC(hdcMem);
	ReleaseDC(NULL, hdcScreen);

	return hBitmap;
}

void ShowAvatarSelectionScreen(HWND hWnd, const std::vector<AVATAR>& avatars) {
	// Hide ID, PW Input Control
	ShowWindow(hLoginIdText, SW_HIDE);
	ShowWindow(hLoginIdEdit, SW_HIDE);
	ShowWindow(hLoginPwText, SW_HIDE);
	ShowWindow(hLoginPwEdit, SW_HIDE);
	ShowWindow(hLoginButton, SW_HIDE);
	ShowWindow(hRegisterButton, SW_HIDE);

	const int avatar_size = 128;
	const int spacing = 200;
	const int total_width = (avatar_size * 3) + (spacing * 2);
	const int total_height = avatar_size + 10 + 30;

	int x = CenterX(total_width);
	int y = CenterY(total_height);

	for (int i = 0; i < 3; ++i) {
		hAvatarImage[i] = CreateWindow(L"STATIC", nullptr,
			WS_CHILD | WS_VISIBLE | SS_BITMAP,
			x + i * (avatar_size + spacing), y, avatar_size, avatar_size,
			hWnd, nullptr, nullptr, nullptr);

		if (i < avatars.size()) {
			HBITMAP hBmpOriginal = player_hBitmap[avatars[i].level];
			HBITMAP hScaled = ResizeBitmap(hBmpOriginal, avatar_size, avatar_size);
			SendMessage(hAvatarImage[i], STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hScaled);

			hAvatarButton[i] = CreateWindow(L"BUTTON", L"Select",
				WS_CHILD | WS_VISIBLE,
				x + i * (avatar_size + spacing), y + avatar_size + 10, avatar_size, 30,
				hWnd, (HMENU)(3000 + i), nullptr, nullptr);
		} else {
			HBITMAP hWhite = CreateWhiteBitmap(avatar_size, avatar_size);
			SendMessage(hAvatarImage[i], STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hWhite);

			hAvatarButton[i] = CreateWindow(L"BUTTON", L"Create",
				WS_CHILD | WS_VISIBLE,
				x + i * (avatar_size + spacing), y + avatar_size + 10, avatar_size, 30,
				hWnd, (HMENU)(3000 + i), nullptr, nullptr);
		}
	}
}

void Init(HWND hWnd) {
	cPen = CreatePen(PS_SOLID, 5, RGB(255, 255, 0));
	hpPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
	ePen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));

	rBrush = CreateSolidBrush(RGB(255, 0, 0));
	gBrush = CreateSolidBrush(RGB(0, 255, 0));
	wBrush = CreateSolidBrush(RGB(255, 255, 255));

	player_hBitmap[0] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\pawn.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	player_hBitmap[1] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\bishop.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	player_hBitmap[2] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\rook.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	player_hBitmap[3] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\king.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	player_hBitmap[4] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\knight.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	player_hBitmap[5] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\queen.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);

	GetObject(player_hBitmap[0], sizeof(BITMAP), &player_bmp[0]);
	GetObject(player_hBitmap[1], sizeof(BITMAP), &player_bmp[1]);
	GetObject(player_hBitmap[2], sizeof(BITMAP), &player_bmp[2]);
	GetObject(player_hBitmap[3], sizeof(BITMAP), &player_bmp[3]);
	GetObject(player_hBitmap[4], sizeof(BITMAP), &player_bmp[4]);
	GetObject(player_hBitmap[5], sizeof(BITMAP), &player_bmp[5]);

	bg.m_hBitmap[0] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\white.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	bg.m_hBitmap[1] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\black.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	bg.m_hBitmap[2] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\terrain.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);

	GetObject(bg.m_hBitmap[0], sizeof(BITMAP), &bg.m_bmp[0]);
	GetObject(bg.m_hBitmap[1], sizeof(BITMAP), &bg.m_bmp[1]);
	GetObject(bg.m_hBitmap[2], sizeof(BITMAP), &bg.m_bmp[2]);

	GetClientRect(hWnd, &rect);
}

void StartGame(HWND hWnd) {
	for (int i = 0; i < 3; ++i) {
		ShowWindow(hAvatarImage[i], SW_HIDE);
		ShowWindow(hAvatarButton[i], SW_HIDE);
	}

	if (false == load_terrain("../Server/terrain.bin")) {
		MessageBox(hWnd, L"Terrain Loading Failed", L"Error", MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return;
	}

	for (int y = 0; y < W_HEIGHT; ++y) {
		for (int x = 0; x < W_WIDTH; ++x) {
			if (true == get_tile(x, y)) {
				tile_map[y][x] = 2;
				continue;
			}

			tile_map[y][x] = ((x / 3) + (y / 3)) % 2;
		}
	}

	bGameStart = true;

	SetTimer(hWnd, 0, 16, NULL);
}

void init_socket() {
	// Initialize Socket and WSAConnect to Server
	WSADATA WSAData;
	auto ret = WSAStartup(MAKEWORD(2, 2), &WSAData);
	if (0 != ret) { std::cout << "WSAStartup Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSAStartup Succeed" << std::endl; }

	g_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == g_socket) { std::cout << "Client Socket Create Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSASocket Succeed" << std::endl; }
}

bool try_connect_to_server(TCHAR* buffer) {
	char ip_address[64];
	WideCharToMultiByte(CP_ACP, 0, buffer, -1, ip_address, sizeof(ip_address), NULL, NULL);

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_NUM);
	inet_pton(AF_INET, ip_address, &addr.sin_addr);

	auto ret = WSAConnect(g_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(SOCKADDR_IN), NULL, NULL, NULL, NULL);

	if (SOCKET_ERROR == ret) { return false; }
	else { return true; }
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
	case SC_LOGIN_OK: {
		SC_LOGIN_OK_PACKET* p = reinterpret_cast<SC_LOGIN_OK_PACKET*>(packet);

		AVATAR* avatars = reinterpret_cast<AVATAR*>(reinterpret_cast<char*>(p) + sizeof(SC_LOGIN_OK_PACKET));
		int avatar_count = (p->size - sizeof(SC_LOGIN_OK_PACKET)) / sizeof(AVATAR);

		for (int i = 0; i < avatar_count; ++i) {
			avatar_list.emplace_back(avatars[i]);
		}

		ShowAvatarSelectionScreen(g_hWnd, avatar_list);
		break;
	}

	case SC_LOGIN_FAIL: {
		SC_LOGIN_FAIL_PACKET* p = reinterpret_cast<SC_LOGIN_FAIL_PACKET*>(packet);

		switch (p->error_code) {
		case NO_ID:
			MessageBox(g_hWnd, L"ID does not exist", L"Error", MB_OK | MB_ICONERROR);
			break;

		case WRONG_PW:
			MessageBox(g_hWnd, L"Password is incorrect", L"Error", MB_OK | MB_ICONERROR);
			break;

		case DUPLICATED:
			MessageBox(g_hWnd, L"Account is already logged in", L"Error", MB_OK | MB_ICONERROR);
			break;

		case EXEC_DIRECT:
			MessageBox(g_hWnd, L"Failed to load user data", L"Error", MB_OK | MB_ICONERROR);
			break;

		case ALREADY_EXIST:
			MessageBox(g_hWnd, L"ID already exist", L"Error", MB_OK | MB_ICONERROR);
			break;
		}
		break;
	}

	case SC_LOGIN_INFO: {
		SC_LOGIN_INFO_PACKET* p = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(packet);
		player.m_x = p->x;
		player.m_y = p->y;
		player.m_id = p->id;
		player.m_hp = p->hp;
		player.m_max_hp = p->max_hp;
		player.m_exp = p->exp;
		player.m_level = p->level;
		player.update_camera();

		StartGame(g_hWnd);
		break;
	}

	case SC_ADD_OBJECT: {
		SC_ADD_OBJECT_PACKET* p = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(packet);
		PLAYER other;
		other.m_id = p->id;
		other.m_x = p->x;
		other.m_y = p->y;
		other.m_level = p->level;
		strcpy(other.m_name, p->name);
		others.emplace(other.m_id, other);
		break;
	}

	case SC_MOVE_OBJECT: {
		SC_MOVE_OBJECT_PACKET* p = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(packet);
		if (player.m_id == p->id) {
			player.m_x = p->x;
			player.m_y = p->y;

			if (false == player.m_is_alive) {
				player.m_hp = player.m_max_hp;
				player.m_is_alive = true;
			}

			player.update_camera();
			break;
		}

		if (others.count(p->id)) {
			others.at(p->id).m_x = p->x;
			others.at(p->id).m_y = p->y;

			if (false == others.at(p->id).m_is_alive) {
				others.at(p->id).m_is_alive = true;
			}
		}
		break;
	}

	case SC_REMOVE_OBJECT: {
		SC_REMOVE_OBJECT_PACKET* p = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(packet);
		others.erase(p->id);
		break;
	}

	case SC_CHAT: {
		SC_CHAT_PACKET* p = reinterpret_cast<SC_CHAT_PACKET*>(packet);
		if (others.count(p->id)) {
			char buf[128];
			sprintf_s(buf, "[%s] : %s", others.at(p->id).m_name, p->mess);
			chat_log.emplace_back(buf);
			if (chat_log.size() > MAX_CHAT_LINE) {
				chat_log.erase(chat_log.begin());
			}
		}
		break;
	}

	case SC_ATTACK: {
		SC_ATTACK_PACKET * p = reinterpret_cast<SC_ATTACK_PACKET*>(packet);
		if (others.count(p->id)) {
			others.at(p->id).attack();
		}
		break;
	}

	case SC_EARN_EXP: {
		SC_EARN_EXP_PACKET* p = reinterpret_cast<SC_EARN_EXP_PACKET*>(packet);
		player.m_exp = p->exp;
		break;
	}

	case SC_LEVEL_UP: {
		SC_LEVEL_UP_PACKET* p = reinterpret_cast<SC_LEVEL_UP_PACKET*>(packet);

		if (player.m_id == p->id) {
			player.m_level = p->level;
			break;
		}

		if (others.count(p->id)) {
			others.at(p->id).m_level = p->level;
		}
		break;
	}

	case SC_DAMAGE: {
		SC_DAMAGE_PACKET* p = reinterpret_cast<SC_DAMAGE_PACKET*>(packet);
		player.m_hp = p->hp;
		break;
	}

	case SC_HEAL: {
		SC_HEAL_PACKET* p = reinterpret_cast<SC_HEAL_PACKET*>(packet);
		player.m_hp = p->hp;
		break;
	}

	case SC_DEATH: {
		SC_DEATH_PACKET* p = reinterpret_cast<SC_DEATH_PACKET*>(packet);

		if (player.m_id == p->id) {
			player.m_hp = 0;
			player.m_is_alive = false;
			break;
		}

		if (others.count(p->id)) {
			others.at(p->id).m_is_alive = false;
		}
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