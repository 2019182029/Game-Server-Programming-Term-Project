#include <windows.h>
#include <tchar.h>
#include "..\protocol.h"

//#pragma comment(linker,"/entry:WinMainCRTStartup /subsystem:console")

HINSTANCE g_hInst;
LPCTSTR lpszClass = L"Window Class";
LPCTSTR lpszWindowName = L"Game Server Programming";

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);

RECT rect;

//////////////////////////////////////////////////
// Background
char tile_map[W_WIDTH][W_HEIGHT];
int camera_x = 0;
int camera_y = 0;

class BACKGROUND {
public:
	BITMAP m_bmp[2];
	HBITMAP m_hBitmap[2];

	void print(HDC mDC) const {
		for (int y = 0; y < S_VISIBLE_TILES; ++y) {
			for (int x = 0; x < S_VISIBLE_TILES; ++x) {
				int map_x = camera_x + x;
				int map_y = camera_y + y;

				char tile_type = tile_map[map_y][map_x];
				HDC tileDC = CreateCompatibleDC(mDC);
				HGDIOBJ hBitmap = SelectObject(tileDC, m_hBitmap[tile_type]);

				StretchBlt(mDC, x * S_TILE_WIDTH, y * S_TILE_HEIGHT, S_TILE_WIDTH, S_TILE_HEIGHT,
					tileDC, 0, 0, m_bmp[tile_type].bmWidth, m_bmp[tile_type].bmHeight, SRCCOPY);

				SelectObject(tileDC, hBitmap);
				DeleteDC(tileDC);
			}
		}
	}
};

BACKGROUND bg;

//////////////////////////////////////////////////
// WINMAIN
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE	hPrevInstance, _In_ LPSTR lpszCmdParam, _In_ int nCmdShow) {
	HWND hWnd;
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

	hWnd = CreateWindow(lpszClass, lpszWindowName, WS_OVERLAPPEDWINDOW,
		0, 0, S_WIDTH, S_HEIGHT,
		NULL, (HMENU)NULL, hInstance, NULL);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	while (GetMessage(&Message, 0, 0, 0)) {
		TranslateMessage(&Message);
		DispatchMessage(&Message);
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
		bg.m_hBitmap[0] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\white.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		bg.m_hBitmap[1] = (HBITMAP)LoadImage(g_hInst, TEXT("Resource\\black.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		GetObject(bg.m_hBitmap[0], sizeof(BITMAP), &bg.m_bmp[0]);
		GetObject(bg.m_hBitmap[1], sizeof(BITMAP), &bg.m_bmp[1]);
		GetClientRect(hWnd, &rect);

		for (int y = 0; y < 2000; ++y) {
			for (int x = 0; x < 2000; ++x) {
				tile_map[y][x] = ((x / 3) + (y / 3)) % 2;
			}
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
