#include <vector>
#include <stdio.h>
#include <fstream>

const int MAP_WIDTH = 2000;
const int MAP_HEIGHT = 2000;

std::vector<uint8_t> terrain((MAP_WIDTH * MAP_HEIGHT + 7) / 8, 0); // 0 = 평지

void set_tile(int x, int y, bool wall) {
	int idx = y * MAP_WIDTH + x;
	if (wall)
		terrain[idx / 8] |= (1 << (idx % 8));
	else
		terrain[idx / 8] &= ~(1 << (idx % 8));
}

bool get_tile(int x, int y) {
	int idx = y * MAP_WIDTH + x;
	return (terrain[idx / 8] >> (idx % 8)) & 1;
}

void generate_noise_obstacles(float wall_chance = 0.15f) {
	for (int y = 0; y < MAP_HEIGHT; ++y) {
		for (int x = 0; x < MAP_WIDTH; ++x) {
			if (rand() / (float)RAND_MAX < wall_chance)
				set_tile(x, y, true); // 벽
		}
	}
}

void save_terrain_to_file(const std::string& filename) {
	std::ofstream ofs(filename, std::ios::binary);
	ofs.write(reinterpret_cast<const char*>(terrain.data()), terrain.size());
	ofs.close();
}

void dig_path(int x1, int y1, int x2, int y2) {
	int x = x1;
	int y = y1;

	while (x != x2 || y != y2) {
		set_tile(x, y, false); // 통로로 뚫음

		if (x < x2) ++x;
		else if (x > x2) --x;

		if (y < y2) ++y;
		else if (y > y2) --y;
	}
}

int main() {
	// 랜덤 시드 초기화
	srand(static_cast<unsigned int>(time(nullptr)));

	// 노이즈 기반 지형 생성
	generate_noise_obstacles(0.15f);  // 15% 확률로 벽 생성

	// 주요 경로 강제 연결
	dig_path(1, 1, 1999, 1999);     // 좌상 ↔ 우하 대각선
	dig_path(1999, 1, 1, 1999);     // 우상 ↔ 좌하 대각선

	dig_path(1, 1, 1, 1999);		// 좌상 ↔ 좌하 수직
	dig_path(1999, 1, 1999, 1999);  // 우상 ↔ 우하 수직

	dig_path(1, 1, 1999, 1);		// 좌상 ↔ 우상 수평
	dig_path(1, 1999, 1999, 1999);  // 좌하 ↔ 우하 수평

	dig_path(1000, 0, 1000, 1999);  // 수직 관통
	dig_path(0, 1000, 1999, 1000);  // 수평 관통

	// 저장
	save_terrain_to_file("terrain.bin");

	return 0;
}