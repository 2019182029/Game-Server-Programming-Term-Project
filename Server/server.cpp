#define _CRT_SECURE_NO_WARNINGS

#include "SESSION.h"

//////////////////////////////////////////////////
// IOCP
HANDLE g_hIOCP;
SOCKET g_s_socket, g_c_socket;
EXP_OVER g_accept_over{ IO_ACCEPT };

int g_new_id = 0;

void worker();
void disconnect(int c_id);
void process_packet(int c_id, char* packet);

//////////////////////////////////////////////////
// VIEW
std::array<std::array<std::unordered_set<int>, SECTOR_COLS>, SECTOR_ROWS> g_sector;
std::mutex g_mutex[SECTOR_ROWS][SECTOR_COLS];

bool can_see(int from, int to);
void update_sector(int c_id, short old_x, short old_y, short new_x, short new_y);

//////////////////////////////////////////////////
// NPC
struct Node {
	short x, y;
	int g, h;
	Node* parent;
	bool in_use = false;

	bool operator>(const Node& other) const { return f() > other.f(); }
	int f() const { return g + h; }
	void reset(short _x, short _y, int _g, int _h, Node* _parent) {
		x = _x; y = _y;
		g = _g; h = _h;
		parent = _parent;
		in_use = true;
	}
};

struct Compare {
	bool operator()(Node* a, Node* b) const {
		return a->g + a->h > b->g + b->h;
	}
};

std::mt19937 rng(std::random_device{}());

void init_npc();
bool is_player(int c_id);
bool is_npc(int c_id);
short calc_distance_sq(short npc_x, short npc_y, short player_x, short player_y);

int heuristic(int x1, int y1, int x2, int y2);
std::vector<std::pair<short, short>> a_star(short sx, short sy, short gx, short gy);

//////////////////////////////////////////////////
// TIMER
void do_timer();

//////////////////////////////////////////////////
// DB
void do_query();
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);

std::unordered_set<int> g_ids;
std::mutex g_id_lock;

//////////////////////////////////////////////////
// TERRAIN
std::vector<uint8_t> terrain((W_WIDTH* W_HEIGHT + 7) / 8, 0);

bool load_terrain(const std::string& filename);
bool get_tile(int x, int y);
bool is_valid_move(int x, int y);

//////////////////////////////////////////////////
// LUA
int API_do_npc_random_move(lua_State* L);
int API_do_npc_chase(lua_State* L);
int API_do_npc_attack(lua_State* L);
int API_do_npc_sleep(lua_State* L);
int API_is_in_chase_range(lua_State* L);
int API_is_in_attack_range(lua_State* L);
int API_get_x(lua_State* L);
int API_get_y(lua_State* L);
int API_get_vl(lua_State* L);
int API_register_event(lua_State* L);
void push_vl(lua_State* L, const std::unordered_set<int>& view_list);

//////////////////////////////////////////////////
// MAIN
int main() {
	if (false == load_terrain("terrain.bin")) {
		std::cout << "Terrain Loading Failed" << std::endl;
		return 0;
	}

	WSADATA WSAData;
	auto ret = WSAStartup(MAKEWORD(2, 0), &WSAData);
	if (0 != ret) { std::cout << "WSAStartup Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSAStartup Succeed" << std::endl; }

	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);;
	if (INVALID_SOCKET == g_s_socket) { std::cout << "Client Socket Create Failed : " << WSAGetLastError() << std::endl; }
	else { std::cout << "WSASocket Succeed" << std::endl; }

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_NUM);
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
	std::thread timer_thread(do_timer);
	std::thread query_thread(do_query);

	setlocale(LC_ALL, "korean");

	for (auto& w : workers)
		w.join();
	timer_thread.join();

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
			int client_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) break;

			int remain_data = io_size + client->m_remained;
			char* p = eo->m_buffer;
			while (remain_data > 0) {
				int packet_size = p[0];
				if (packet_size <= remain_data) {
					process_packet(client_id, p);
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

		case IO_LOGIN: {
			int client_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) { break; }

			client->send_login_info();

			// Add Client into Sector
			short sx = client->m_x / SECTOR_WIDTH;
			short sy = client->m_y / SECTOR_HEIGHT;
			{
				std::lock_guard<std::mutex> sl(g_mutex[sy][sx]);
				g_sector[sy][sx].insert(client_id);
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
						if (other->m_id == client_id) { continue; }

						if (true == can_see(client_id, other->m_id)) {
							client->send_add_object(other->m_id);

							if (is_player(other->m_id)) { 
								other->send_add_object(client_id);
							} else { 
								other->try_wake_up(client_id);
							}
						}
					}
				}
			}

			delete eo;
			break;
		}

		case IO_LOGIN_OK: {
			int client_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) { break; }

			client->send_login_ok(eo->m_avatars);

			delete eo;
			break;
		}

		case IO_LOGIN_FAIL: {
			int client_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) { break; }
			
			client->send_login_fail(eo->m_error_code);

			delete eo;
			break;
		}

		case IO_PLAYER_DIE: {
			int client_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) { break; }

			// Create View List
			client->m_vl.lock();
			std::unordered_set<int> near_list = client->m_view_list;
			client->m_vl.unlock();

			client->send_death(client_id);

			for (auto& cl : near_list) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) { continue; }

				if (false == is_player(other->m_id)) { continue; }
				if (ST_INGAME != other->m_state) { continue; }
				if (other->m_id == client_id) { continue; }

				if (true == can_see(client_id, other->m_id)) {
					other->send_death(client_id);
				}
			}

			timer_lock.lock();
			timer_queue.emplace(event{ client_id, INVALID_ID, std::chrono::high_resolution_clock::now() + std::chrono::seconds(5), EV_PLAYER_RESPAWN });
			timer_lock.unlock();

			delete eo;
			break;
		}

		case IO_PLAYER_RESPAWN: {
			int client_id = static_cast<int>(key);

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) { break; }

			short old_x = client->m_x;
			short old_y = client->m_y;

			short new_x, new_y;

			do {
				new_x = rand() % W_WIDTH;
				new_y = rand() % W_HEIGHT;
			} while (true == get_tile(new_x, new_y));

			client->respawn(new_x, new_y);

			client->m_x = new_x;
			client->m_y = new_y;

			// Update Sector
			update_sector(client_id, old_x, old_y, new_x, new_y);

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
						if (other->m_id == client_id) { continue; }

						if (true == can_see(client_id, other->m_id)) { 
							near_list.insert(other->m_id); 
						}
					}
				}
			}

			client->send_move_object(client_id);

			for (auto& cl : near_list) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) { continue; }

				if (is_player(cl)) {
					other->m_vl.lock();
					if (other->m_view_list.count(client_id)) {
						other->m_vl.unlock();
						other->send_move_object(client_id);
					} else {
						other->m_vl.unlock();
						other->send_add_object(client_id);
					}
				} else {
					other->try_wake_up(client_id);
				}

				if (!old_vlist.count(cl)) { client->send_add_object(cl); }
			}

			for (auto& cl : old_vlist) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) { continue; }

				if (!near_list.count(cl)) {
					client->send_remove_object(cl);
					if (is_player(cl)) { other->send_remove_object(client_id); }
				}
			}

			delete eo;
			break;
		}

		case IO_NPC_MOVE: {
			int npc_id = static_cast<int>(key);

			std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
			if (nullptr == npc) { break; }

			if (npc->m_is_active) {
				switch (npc->m_level) {
				case KNIGHT: {
					auto L = npc->m_lua;

					npc->m_lua_lock.lock();

					lua_getglobal(L, "do_npc_random_move");
					lua_pcall(L, 0, 0, 0);

					npc->m_lua_lock.unlock();
					break;
				}

				case QUEEN: {
					auto L = npc->m_lua;

					npc->m_lua_lock.lock();

					lua_getglobal(L, "do_npc_chase");
					lua_pushnumber(L, eo->m_target_id);
					lua_pcall(L, 1, 0, 0);

					npc->m_lua_lock.unlock();
					break;
				}
				}
			}

			delete eo;
			break;
		}

		case IO_NPC_ATTACK: {
			int npc_id = static_cast<int>(key);

			std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
			if (nullptr == npc) { break; }

			if (npc->m_is_active) {
				auto L = npc->m_lua;

				npc->m_lua_lock.lock();

				lua_getglobal(L, "do_npc_attack");
				lua_pushnumber(L, eo->m_target_id);
				lua_pcall(L, 1, 0, 0);

				npc->m_lua_lock.unlock();
				break;
			}

			delete eo;
			break;
		}

		case IO_NPC_DIE: {
			// Npc
			int npc_id = static_cast<int>(key);

			std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
			if (nullptr == npc) break;

			npc->sleep();

			int sx = npc->m_x / SECTOR_WIDTH;
			int sy = npc->m_y / SECTOR_HEIGHT;

			// Create View List by Sector
			std::unordered_set<int> npc_vl;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					short nx = sx + dx;
					short ny = sy + dy;

					if (nx < 0 || ny < 0 || nx >= SECTOR_COLS || ny >= SECTOR_ROWS) { continue; }

					for (auto cl : g_sector[ny][nx]) {
						std::shared_ptr<SESSION> other = g_clients.at(cl);
						if (nullptr == other) { continue; }

						if (false == is_player(other->m_id)) { continue; }
						if (ST_INGAME != other->m_state) { continue; }
						if (other->m_id == npc->m_id) { continue; }

						if (can_see(npc->m_id, other->m_id)) { 
							npc_vl.insert(other->m_id);
						}
					}
				}
			}

			for (auto& cl : npc_vl) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) continue;

				other->send_remove_object(npc->m_id);
			}

			// Delete Npc from Sector
			{
				std::lock_guard<std::mutex> lock(g_mutex[sy][sx]);
				g_sector[sy][sx].erase(npc->m_id);
			}

			timer_lock.lock();
			timer_queue.emplace(event{ npc->m_id, INVALID_ID, std::chrono::high_resolution_clock::now() + std::chrono::seconds(5), EV_NPC_RESPAWN });
			timer_lock.unlock();

			// Player
			int client_id = eo->m_target_id;

			std::shared_ptr<SESSION> client = g_clients.at(client_id);
			if (nullptr == client) break;

			int exp = 0;
			bool level_up = client->earn_exp(exp);

			// Send Earn Exp Packet to Player
			client->send_earn_exp(exp);

			if (level_up) {
				// Send Level Up Packet to Player
				client->m_vl.lock();
				std::unordered_set<int> client_vl = client->m_view_list;
				client->m_vl.unlock();

				client->send_level_up(client_id);

				for (auto& cl : client_vl) {
					std::shared_ptr<SESSION> other = g_clients.at(cl);
					if (nullptr == other) continue;

					if (false == is_player(other->m_id)) { continue; }
					if (ST_INGAME != other->m_state) { continue; }
					if (other->m_id == client_id) { continue; }

					if (true == can_see(client_id, other->m_id)) {
						other->send_level_up(client_id);
					}
				}
			}

			delete eo;
			break;
		}

		case IO_NPC_RESPAWN: {
			int npc_id = static_cast<int>(key);

			std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
			if (nullptr == npc) break;

			short new_x, new_y;

			do {
				new_x = rand() % W_WIDTH;
				new_y = rand() % W_HEIGHT;
			} while (true == get_tile(new_x, new_y));

			npc->respawn(new_x, new_y);

			// Add Npc into Sector
			short sx = npc->m_x / SECTOR_WIDTH;
			short sy = npc->m_y / SECTOR_HEIGHT;

			{
				std::lock_guard<std::mutex> sl(g_mutex[sy][sx]);
				g_sector[sy][sx].insert(npc_id);
			}

			// Search Nearby Objects by Sector
			for (short dy = -1; dy <= 1; ++dy) {
				for (short dx = -1; dx <= 1; ++dx) {
					short nx = sx + dx;
					short ny = sy + dy;

					if (nx < 0 || ny < 0 || nx >= SECTOR_COLS || ny >= SECTOR_ROWS) { continue; }

					std::lock_guard<std::mutex> lock(g_mutex[ny][nx]);
					for (auto cl : g_sector[ny][nx]) {
						std::shared_ptr<SESSION> other = g_clients.at(cl);
						if (nullptr == other) { continue; }

						if (false == is_player(other->m_id)) { continue; }
						if (ST_INGAME != other->m_state) { continue; }
						if (other->m_id == npc_id) { continue; }

						if (true == can_see(npc_id, other->m_id)) {
							other->send_add_object(npc_id);
							npc->try_wake_up(other->m_id);
						}
					}
				}
			}

			delete eo;
			break;
		}
		}
	}
}

void disconnect(int c_id) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	client->m_state = ST_CLOSE;

	client->m_vl.lock();
	std::unordered_set <int> vl = client->m_view_list;
	client->m_vl.unlock();

	for (auto& cl : vl) {
		std::shared_ptr<SESSION> other = g_clients.at(cl);
		if (nullptr == other) continue;

		if (false == is_player(other->m_id)) { continue; }
		if (ST_INGAME != other->m_state) continue;
		if (other->m_id == c_id) continue;

		if (true == can_see(c_id, other->m_id)) {
			other->send_remove_object(c_id);
		}
	}

	// Delete Client from Sector
	int sx = client->m_x / SECTOR_WIDTH;
	int sy = client->m_y / SECTOR_HEIGHT;

	{
		std::lock_guard<std::mutex> lock(g_mutex[sy][sx]);
		g_sector[sy][sx].erase(c_id);
	}

	if (client->m_account_id) {
		std::lock_guard<std::mutex> lock(g_id_lock);
		g_ids.erase(std::stoi(client->m_name));
	}

	g_clients.at(c_id) = nullptr;
}

void process_packet(int c_id, char* packet) {
	std::shared_ptr<SESSION> client = g_clients.at(c_id);
	if (nullptr == client) return;

	switch (packet[1]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

		{
			std::lock_guard<std::mutex> lock(g_id_lock);

			if (g_ids.count(std::stoi(p->id))) {
				disconnect(c_id);
				break;
			}
		}

		query q{ c_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), QU_LOGIN };
		strncpy(q.client_id, p->id, ID_SIZE);
		strncpy(q.client_pw, p->pw, PW_SIZE);

		query_lock.lock();
		query_queue.emplace(q);
		query_lock.unlock();
		break;
	}

	case CS_USER_LOGIN: {
		CS_USER_LOGIN_PACKET* p = reinterpret_cast<CS_USER_LOGIN_PACKET*>(packet);

		{
			std::lock_guard<std::mutex> lock(g_id_lock);

			if (g_ids.count(std::stoi(p->id))) {
				client->send_login_fail(DUPLICATED);
				break;
			}
		}

		query q{ c_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), QU_USER_LOGIN };
		strncpy(q.client_id, p->id, ID_SIZE);
		strncpy(q.client_pw, p->pw, PW_SIZE);

		query_lock.lock();
		query_queue.emplace(q);
		query_lock.unlock();
		break;
	}

	case CS_USER_REGISTER: {
		CS_USER_LOGIN_PACKET* p = reinterpret_cast<CS_USER_LOGIN_PACKET*>(packet);

		query q{ c_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), QU_USER_REGISTER };
		strncpy(q.client_id, p->id, ID_SIZE);
		strncpy(q.client_pw, p->pw, PW_SIZE);

		query_lock.lock();
		query_queue.emplace(q);
		query_lock.unlock();
		break;
	}

	case CS_SELECT_AVATAR: {
		CS_SELECT_AVATAR_PACKET* p = reinterpret_cast<CS_SELECT_AVATAR_PACKET*>(packet);

		query q{ c_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), QU_SELECT_AVATAR };
		q.set_avatar_id(p->avatar_id);

		query_lock.lock();
		query_queue.emplace(q);
		query_lock.unlock();
		break;
	}

	case CS_CREATE_AVATAR: {
		CS_CREATE_AVATAR_PACKET* p = reinterpret_cast<CS_CREATE_AVATAR_PACKET*>(packet);

		query q{ c_id, std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(10), QU_CREATE_AVATAR };
		q.set_slot(p->slot_id);

		query_lock.lock();
		query_queue.emplace(q);
		query_lock.unlock();
		break;
	}

	case CS_CHAT: {	
		CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);

		client->m_vl.lock();
		std::unordered_set<int> vlist = client->m_view_list;
		client->m_vl.unlock();

		for (auto& cl : vlist) {
			std::shared_ptr<SESSION> other = g_clients.at(cl);
			if (nullptr == other) continue;

			if (false == is_player(other->m_id)) { continue; }
			if (ST_INGAME != other->m_state) { continue; }
			if (other->m_id == c_id) { continue; }

			if (true == can_see(c_id, other->m_id)) {
				other->send_chat(c_id, p->mess);
			}
		}
		break;
	}

	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);

		if (false == client->is_alive()) {
			// Send Packet in order to Prevent Cheating
			client->send_move_object(c_id);
			break;
		}

		client->m_last_move_time = p->move_time;

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

		if (false == is_valid_move(new_x, new_y)) {
			// Send Packet in order to Prevent Cheating
			client->send_move_object(c_id);
			break;
		}

		client->m_x = new_x;
		client->m_y = new_y;

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

					if (true == can_see(c_id, other->m_id)) { 
						near_list.insert(other->m_id); 
					}
				}
			}
		}

		client->send_move_object(c_id);

		for (auto& cl : near_list) {
			std::shared_ptr<SESSION> other = g_clients.at(cl);
			if (nullptr == other) { continue; }

			if (is_player(cl)) {
				other->m_vl.lock();
				if (other->m_view_list.count(c_id)) {
					other->m_vl.unlock();
					other->send_move_object(c_id);
				} else {
					other->m_vl.unlock();
					other->send_add_object(c_id);
				}
			} else {
				other->try_wake_up(c_id);
			}

			if (!old_vlist.count(cl)) { client->send_add_object(cl); }
		}

		for (auto& cl : old_vlist) {
			std::shared_ptr<SESSION> other = g_clients.at(cl);
			if (nullptr == other) { continue; }

			if (!near_list.count(cl)) {
				client->send_remove_object(cl);

				if (is_player(cl)) { 
					other->send_remove_object(c_id); 
				}
			}
		}
		break;
	}

	case CS_ATTACK:
		if (false == client->is_alive()) { 
			break; 
		}

		short x = client->m_x;
		short y = client->m_y;
		int level = client->m_level;

		// Get Attacked Coords
		std::vector<std::pair<short, short>> attacked_coords;

		switch (level) {
		case PAWN:
			if ((0 <= x - 1) && (x - 1 < W_WIDTH) && (0 <= y - 1) && (y - 1 < W_HEIGHT)) attacked_coords.emplace_back(x - 1, y - 1);
			if ((0 <= x + 1) && (x + 1 < W_WIDTH) && (0 <= y - 1) && (y - 1 < W_HEIGHT)) attacked_coords.emplace_back(x + 1, y - 1);
			break;

		case BISHOP:
			if ((0 <= x - 1) && (x - 1 < W_WIDTH) && (0 <= y - 1) && (y - 1 < W_HEIGHT)) attacked_coords.emplace_back(x - 1, y - 1);
			if ((0 <= x + 1) && (x + 1 < W_WIDTH) && (0 <= y - 1) && (y - 1 < W_HEIGHT)) attacked_coords.emplace_back(x + 1, y - 1);
			if ((0 <= x - 1) && (x - 1 < W_WIDTH) && (0 <= y + 1) && (y + 1 < W_HEIGHT)) attacked_coords.emplace_back(x - 1, y + 1);
			if ((0 <= x + 1) && (x + 1 < W_WIDTH) && (0 <= y + 1) && (y + 1 < W_HEIGHT)) attacked_coords.emplace_back(x + 1, y + 1);
			break;

		case ROOK:
			if ((0 <= x) && (x < W_WIDTH) && (0 <= y - 1) && (y - 1 < W_HEIGHT)) attacked_coords.emplace_back(x, y - 1);
			if ((0 <= x) && (x < W_WIDTH) && (0 <= y + 1) && (y + 1 < W_HEIGHT)) attacked_coords.emplace_back(x, y + 1);
			if ((0 <= x - 1) && (x - 1 < W_WIDTH) && (0 <= y) && (y < W_HEIGHT)) attacked_coords.emplace_back(x - 1, y);
			if ((0 <= x + 1) && (x + 1 < W_WIDTH) && (0 <= y) && (y < W_HEIGHT)) attacked_coords.emplace_back(x + 1, y);
			break;

		case KING:
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if ((dx == 0) && (dy == 0)) { continue; }

					int nx = x + dx;
					int ny = y + dy;

					if ((0 <= nx) && (nx < W_WIDTH) && (0 <= ny) && (ny < W_HEIGHT)) {
						attacked_coords.emplace_back(nx, ny);
					}
				}
			}
			break;
		}
		
		// Emplace Attacked Sectors into Ordered Set in order to Avoid Deadlock
		std::set<std::pair<short, short>> attacked_sectors;
		for (const auto& coord : attacked_coords) {
			attacked_sectors.emplace(coord.first / SECTOR_WIDTH, coord.second / SECTOR_HEIGHT);
		}

		// Sector Lock
		std::vector<std::unique_lock<std::mutex>> sector_locks; 
		for (const auto& sector : attacked_sectors) {
			sector_locks.emplace_back(g_mutex[sector.second][sector.first]);
		}

		// Damage Logic
		for (const auto& sector : attacked_sectors) {
			for (const auto& cl : g_sector[sector.second][sector.first]) {
				std::shared_ptr<SESSION> npc = g_clients.at(cl);
				if (nullptr == npc) continue;

				if (false == is_npc(npc->m_id)) { continue; }
				if (ST_INGAME != npc->m_state) { continue; }
				if (npc->m_id == c_id) { continue; }

				if (true == can_see(c_id, npc->m_id)) {
					for (const auto& coord : attacked_coords) {
						if ((coord.first == npc->m_x) && (coord.second == npc->m_y)) {
							npc->receive_damage(1 + level, c_id);
						}
					}
				}
			}
		}

		// Send Attack Packet to Player
		client->m_vl.lock();
		std::unordered_set<int> vlist = client->m_view_list;
		client->m_vl.unlock();

		for (auto& cl : vlist) {
			std::shared_ptr<SESSION> other = g_clients.at(cl);
			if (nullptr == other) continue;

			if (false == is_player(other->m_id)) { continue; }
			if (ST_INGAME != other->m_state) { continue; }
			if (other->m_id == c_id) { continue; }

			if (true == can_see(c_id, other->m_id)) {
				other->send_attack(c_id);
			}
		}
		break;
	}
}

bool can_see(int from, int to) {
	std::shared_ptr<SESSION> client_from = g_clients.at(from);
	std::shared_ptr<SESSION> client_to = g_clients.at(to);

	if ((nullptr == client_from) || (nullptr == client_to)) { return false; }

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
		if ((i % 10'000) == 0) {
			std::cout << "NPC initializing : " << (i - MAX_USER) << " initialized" << std::endl;
		}

		// Session
		std::shared_ptr<SESSION> p = std::make_shared<SESSION>(i);
		p->m_state = ST_INGAME;

		short x, y;

		do {
			x = rand() % W_WIDTH;
			y = rand() % W_HEIGHT;
		} while (true == get_tile(x, y));

		p->m_x = x;
		p->m_y = y;
		p->m_level = (rand() % 2) + KNIGHT;
		snprintf(p->m_name, sizeof(p->m_name), "Npc %d", i);

		// Lua
		p->m_lua = luaL_newstate();
		luaL_openlibs(p->m_lua);
		luaL_loadfile(p->m_lua, "npc.lua");
		lua_pcall(p->m_lua, 0, 0, 0);

		lua_getglobal(p->m_lua, "init_npc");
		lua_pushnumber(p->m_lua, i);
		lua_pushnumber(p->m_lua, p->m_level);
		lua_pcall(p->m_lua, 2, 0, 0);

		lua_register(p->m_lua, "API_do_npc_random_move", API_do_npc_random_move);
		lua_register(p->m_lua, "API_do_npc_chase", API_do_npc_chase);
		lua_register(p->m_lua, "API_do_npc_attack", API_do_npc_attack);
		lua_register(p->m_lua, "API_do_npc_sleep", API_do_npc_sleep);
		lua_register(p->m_lua, "API_is_in_chase_range", API_is_in_chase_range);
		lua_register(p->m_lua, "API_is_in_attack_range", API_is_in_attack_range);
		lua_register(p->m_lua, "API_get_x", API_get_x);
		lua_register(p->m_lua, "API_get_y", API_get_y);
		lua_register(p->m_lua, "API_get_vl", API_get_vl);
		lua_register(p->m_lua, "API_register_event", API_register_event);

		// Add Npc into Sector
		g_clients.insert(std::make_pair(p->m_id, p));

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

short calc_distance_sq(short npc_x, short npc_y, short player_x, short player_y) {
	short dx = (npc_x - player_x);
	short dy = (npc_y - player_y);

	return (dx * dx) + (dy * dy);
}

int heuristic(int x1, int y1, int x2, int y2) {
	return abs(x1 - x2) + abs(y1 - y2);  // Manhattan Diatance
}

std::vector<std::pair<short, short>> a_star(short sx, short sy, short gx, short gy) {
	using p = std::pair<short, short>;

	// Node Pool
	static thread_local std::deque<Node> node_pool;
	static thread_local std::vector<Node*> free_nodes;

	std::priority_queue<Node*, std::vector<Node*>, Compare> open;
	std::unordered_map<int, Node*> visited;

	auto hash = [](int x, int y) { return y * W_WIDTH + x; };

	auto reset_pool = [&]() {
		free_nodes.clear();

		if (512 < node_pool.size()) {
			node_pool.resize(512);
		}

		for (auto& node : node_pool) {
			node.in_use = false;
			free_nodes.emplace_back(&node);
		}
	};

	auto alloc_node = [&](short x, short y, int g, int h, Node* parent) -> Node* {
		// Find Reusable Node
		if (!free_nodes.empty()) {
			Node* n = free_nodes.back();
			free_nodes.pop_back();
			n->reset(x, y, g, h, parent);
			return n;
		}

		node_pool.emplace_back(); 
		Node* n = &node_pool.back();
		n->reset(x, y, g, h, parent);
		return n;
	};

	reset_pool();

	Node* start = alloc_node(sx, sy, 0, heuristic(sx, sy, gx, gy), nullptr);
	open.push(start);
	visited[hash(sx, sy)] = start;

	constexpr std::array<std::pair<short, short>, 8> directions = {
		std::make_pair( 0, -1),  // Up
		std::make_pair( 0,  1),  // Down
		std::make_pair(-1,  0),  // Left
		std::make_pair( 1,  0),  // Right
		std::make_pair(-1, -1),  // Up-Left
		std::make_pair( 1, -1),  // Up-Right
		std::make_pair(-1,  1),  // Down-Left
		std::make_pair( 1,  1)   // Down-Right
	};

	while (!open.empty()) {
		Node* curr = open.top(); open.pop();

		if ((curr->x == gx) && (curr->y == gy)) {
			std::vector<std::pair<short, short>> path;

			while (curr) {
				path.emplace_back(curr->x, curr->y);
				curr = curr->parent;
			}

			std::reverse(path.begin(), path.end());

			return path;
		}

		for (auto [dx, dy] : directions) {
			short nx = curr->x + dx;
			short ny = curr->y + dy;

			if (true == is_valid_move(nx, ny)) {
				int h = heuristic(nx, ny, gx, gy);
				int g = curr->g + 1;

				int key = hash(nx, ny);

				if ((visited.contains(key)) && 
					(visited[key]->g <= g)) { continue; }

				Node* next = alloc_node(nx, ny, g, h, curr);
				visited[key] = next;
				open.push(next);
			}
		}
	}

	return {};
}

void do_timer() {
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		while (true) {
			timer_lock.lock();
			if (timer_queue.empty()) {
				timer_lock.unlock();
				break;
			}

			auto& k = timer_queue.top();

			if (k.wakeup_time > std::chrono::high_resolution_clock::now()) {
				timer_lock.unlock();
				break;
			}
			timer_lock.unlock();

			switch (k.event_id) {
			case EV_PLAYER_DIE: {
				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_PLAYER_DIE;
				PostQueuedCompletionStatus(g_hIOCP, 0, k.obj_id, &o->m_over);
				break;
			}

			case EV_PLAYER_RESPAWN: {
				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_PLAYER_RESPAWN;
				PostQueuedCompletionStatus(g_hIOCP, 0, k.obj_id, &o->m_over);
				break;
			}

			case EV_NPC_MOVE: {
				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_NPC_MOVE;
				o->m_target_id = k.target_id;
				PostQueuedCompletionStatus(g_hIOCP, 0, k.obj_id, &o->m_over);
				break;
			}

			case EV_NPC_ATTACK: {
				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_NPC_ATTACK;
				o->m_target_id = k.target_id;
				PostQueuedCompletionStatus(g_hIOCP, 0, k.obj_id, &o->m_over);
				break;
			}

			case EV_NPC_DIE: {
				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_NPC_DIE;
				o->m_target_id = k.target_id;
				PostQueuedCompletionStatus(g_hIOCP, 0, k.obj_id, &o->m_over);
				break;
			}

			case EV_NPC_RESPAWN: {
				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_NPC_RESPAWN;
				PostQueuedCompletionStatus(g_hIOCP, 0, k.obj_id, &o->m_over);
				break;
			}
			}

			timer_lock.lock();
			timer_queue.pop();
			timer_lock.unlock();
		}
	}
}

std::wstring to_wstring(const char* str) {
	int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	std::wstring wstr(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], len);
	wstr.pop_back(); 
	return wstr;
}

void do_query() {
	SQLHENV henv = nullptr;
	SQLHDBC hdbc = nullptr;
	SQLHSTMT hstmt = nullptr;

	SQLRETURN retcode;

	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) { goto clean_up; }

	retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) { goto clean_up; }

	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) { goto clean_up; }

	retcode = SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) { goto clean_up; }

	retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2019182029_GSP", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) { goto clean_up; }

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) { goto clean_up; }

	// Query Loop
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		while (true) {
			query_lock.lock();
			if (query_queue.empty()) {
				query_lock.unlock();
				break;
			}

			query q = query_queue.top();

			if (q.wakeup_time > std::chrono::high_resolution_clock::now()) {
				query_lock.unlock();
				break;
			}
			query_lock.unlock();

			switch (q.query_id) {
			case QU_LOGIN: {
				std::shared_ptr<SESSION> client = g_clients.at(q.obj_id);
				if (nullptr == client) { break; }

				// ExecDirect
				std::wstring w_id = to_wstring(q.client_id);
				std::wstring w_pw = to_wstring(q.client_pw);

				wchar_t query_buf[256];
				swprintf_s(query_buf, 256, L"EXEC sp_get_avatars N'%s', N'%s'", w_id.c_str(), w_pw.c_str());

				std::wstring query = query_buf;

				retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
				if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
					HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					SQLCloseCursor(hstmt);
					disconnect(q.obj_id);
					break;
				}

				// Fetch
				SQLFetch(hstmt);

				int avatar_id, x, y, exp, level, hp;

				SQLGetData(hstmt, 1, SQL_C_SLONG, &avatar_id, 0, NULL);
				SQLGetData(hstmt, 3, SQL_C_SLONG, &exp, 0, NULL);
				SQLGetData(hstmt, 4, SQL_C_SLONG, &level, 0, NULL);
				SQLGetData(hstmt, 5, SQL_C_SLONG, &hp, 0, NULL);
				SQLGetData(hstmt, 6, SQL_C_SLONG, &x, 0, NULL);
				SQLGetData(hstmt, 7, SQL_C_SLONG, &y, 0, NULL);

				if ((-1 == avatar_id) || (-2 == avatar_id)) {
					SQLCloseCursor(hstmt);
					disconnect(q.obj_id);
				}

				{
					std::lock_guard<std::mutex> lock(g_id_lock);
					g_ids.insert(std::stoi(q.client_id));
				}

				client->m_state = ST_INGAME;
				client->m_x = x;
				client->m_y = y;
				client->m_exp = exp;
				client->m_level = level;
				strncpy(client->m_name, q.client_id, ID_SIZE);
				client->m_avatar_id = avatar_id;

				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_LOGIN;
				PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);

				SQLCloseCursor(hstmt);
				break;
			}

			case QU_USER_LOGIN: {
				std::shared_ptr<SESSION> client = g_clients.at(q.obj_id);
				if (nullptr == client) { break; }

				// ExecDirect
				std::wstring w_id = to_wstring(q.client_id);
				std::wstring w_pw = to_wstring(q.client_pw);

				wchar_t query_buf[256];
				swprintf_s(query_buf, 256, L"EXEC sp_get_avatars N'%s', N'%s'", w_id.c_str(), w_pw.c_str());

				std::wstring query = query_buf;

				retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
				if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
					HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					SQLCloseCursor(hstmt);

					EXP_OVER* o = new EXP_OVER;
					o->m_io_type = IO_LOGIN_FAIL;
					o->m_error_code = EXEC_DIRECT;
					PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);
					break;
				}

				// Fetch
				std::vector<AVATAR> avatars;
				int account_id, avatar_id, slot, level;

				while (SQLFetch(hstmt) == SQL_SUCCESS) {
					SQLGetData(hstmt, 1, SQL_C_SLONG, &account_id, 0, NULL);
					SQLGetData(hstmt, 2, SQL_C_SLONG, &avatar_id, 0, NULL);
					SQLGetData(hstmt, 3, SQL_C_SLONG, &slot, 0, NULL);
					SQLGetData(hstmt, 4, SQL_C_SLONG, &level, 0, NULL);

					if ((-1 == account_id) || (-2 == account_id)) {
						SQLCloseCursor(hstmt);

						EXP_OVER* o = new EXP_OVER;
						o->m_io_type = IO_LOGIN_FAIL;
						o->m_error_code = (-1 == account_id) ? NO_ID : WRONG_PW;
						PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);

						goto query_pop;
					}

					avatars.emplace_back(AVATAR{ avatar_id, slot, level });
				}

				// Send
				{
					std::lock_guard<std::mutex> lock(g_id_lock);
					g_ids.insert(std::stoi(q.client_id));
				}

				strncpy(client->m_name, q.client_id, ID_SIZE);
				client->m_account_id = account_id;

				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_LOGIN_OK;
				o->m_avatars = std::move(avatars);
				PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);

				SQLCloseCursor(hstmt);
				break;
			}

			case QU_USER_REGISTER: {
				std::shared_ptr<SESSION> client = g_clients.at(q.obj_id);
				if (nullptr == client) { break; }

				// ExecDirect
				std::wstring w_id = to_wstring(q.client_id);
				std::wstring w_pw = to_wstring(q.client_pw);

				wchar_t query_buf[256];
				swprintf_s(query_buf, 256, L"EXEC sp_register_user N'%s', N'%s'", w_id.c_str(), w_pw.c_str());
				std::wstring query = query_buf;

				retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
				if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
					HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					SQLCloseCursor(hstmt);

					EXP_OVER* o = new EXP_OVER;
					o->m_io_type = IO_LOGIN_FAIL;
					o->m_error_code = EXEC_DIRECT;
					PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);
					break;
				}

				// Fetch
				SQLFetch(hstmt);

				std::vector<AVATAR> avatars;
				int account_id, avatar_id, slot, level;

				SQLGetData(hstmt, 1, SQL_C_SLONG, &account_id, 0, NULL);
				SQLGetData(hstmt, 2, SQL_C_SLONG, &avatar_id, 0, NULL);
				SQLGetData(hstmt, 3, SQL_C_SLONG, &slot, 0, NULL);
				SQLGetData(hstmt, 4, SQL_C_SLONG, &level, 0, NULL);

				if (-1 == account_id) {
					SQLCloseCursor(hstmt);

					EXP_OVER* o = new EXP_OVER;
					o->m_io_type = IO_LOGIN_FAIL;
					o->m_error_code = ALREADY_EXIST;
					PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);
					break;
				}

				avatars.emplace_back(AVATAR{ avatar_id, slot, level });

				// Send
				{
					std::lock_guard<std::mutex> lock(g_id_lock);
					g_ids.insert(std::stoi(q.client_id));
				}

				strncpy(client->m_name, q.client_id, ID_SIZE);
				client->m_account_id = account_id;

				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_LOGIN_OK;
				o->m_avatars = std::move(avatars);
				PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);

				SQLCloseCursor(hstmt);
				break;
			}

			case QU_SELECT_AVATAR: {
				std::shared_ptr<SESSION> client = g_clients.at(q.obj_id);
				if (nullptr == client) { break; }

				// ExecDirect
				wchar_t query_buf[128];
				swprintf_s(query_buf, 256, L"EXEC sp_select_avatar %d, %d", q.avatar_id, client->m_account_id);
				
				std::wstring query = query_buf;

				retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
				if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
					HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					SQLCloseCursor(hstmt);

					EXP_OVER* o = new EXP_OVER;
					o->m_io_type = IO_LOGIN_FAIL;
					o->m_error_code = EXEC_DIRECT;
					PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);
					break;
				}

				// Fetch
				SQLFetch(hstmt);

				int exp, level, hp, x, y;

				SQLGetData(hstmt, 1, SQL_C_SLONG, &exp, 0, NULL);
				SQLGetData(hstmt, 2, SQL_C_SLONG, &level, 0, NULL);
				SQLGetData(hstmt, 3, SQL_C_SLONG, &hp, 0, NULL);
				SQLGetData(hstmt, 4, SQL_C_SLONG, &x, 0, NULL);
				SQLGetData(hstmt, 5, SQL_C_SLONG, &y, 0, NULL);

				// Send
				client->m_x = x;
				client->m_y = y;
				client->m_exp = exp;
				client->m_level = level;
				client->m_avatar_id = q.avatar_id;
				client->m_state = ST_INGAME;

				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_LOGIN;
				PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);

				SQLCloseCursor(hstmt);
				break;
			}

			case QU_CREATE_AVATAR: {
				std::shared_ptr<SESSION> client = g_clients.at(q.obj_id);
				if (nullptr == client) { break; }

				// ExecDirect
				wchar_t query_buf[128];
				swprintf_s(query_buf, 128, L"EXEC sp_create_avatar %d, %d", client->m_account_id, q.slot);

				std::wstring query = query_buf;

				retcode = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
				if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
					HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					SQLCloseCursor(hstmt);

					EXP_OVER* o = new EXP_OVER;
					o->m_io_type = IO_LOGIN_FAIL;
					o->m_error_code = EXEC_DIRECT;
					PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);
					break;
				}

				// Fetch 
				SQLFetch(hstmt);

				int avatar_id, exp, level, hp, x, y;
				SQLGetData(hstmt, 1, SQL_C_SLONG, &avatar_id, 0, NULL);
				SQLGetData(hstmt, 2, SQL_C_SLONG, &exp, 0, NULL);
				SQLGetData(hstmt, 3, SQL_C_SLONG, &level, 0, NULL);
				SQLGetData(hstmt, 4, SQL_C_SLONG, &hp, 0, NULL);
				SQLGetData(hstmt, 5, SQL_C_SLONG, &x, 0, NULL);
				SQLGetData(hstmt, 6, SQL_C_SLONG, &y, 0, NULL);

				// Send
				client->m_avatar_id = avatar_id;
				client->m_exp = exp;
				client->m_level = level;
				client->m_hp = hp;
				client->m_x = x;
				client->m_y = y;
				client->m_state = ST_INGAME;

				EXP_OVER* o = new EXP_OVER;
				o->m_io_type = IO_LOGIN;
				PostQueuedCompletionStatus(g_hIOCP, 0, q.obj_id, &o->m_over);

				SQLCloseCursor(hstmt);
				break;
			}
			}

query_pop:
			query_lock.lock();
			query_queue.pop();
			query_lock.unlock();
		}
	}

clean_up:
	if (hstmt) {
		SQLCancel(hstmt);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	}

	if (hdbc) {
		SQLDisconnect(hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	}

	if (henv) {
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}

void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode) {
	SQLSMALLINT iRec = 0;
	SQLINTEGER  iError;
	WCHAR       wszMessage[1000];
	WCHAR       wszState[SQL_SQLSTATE_SIZE + 1];

	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}

	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
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

bool is_valid_move(int x, int y) {
	return x >= 0 && x < W_WIDTH && y >= 0 && y < W_HEIGHT && false == get_tile(x, y);
}

int API_do_npc_random_move(lua_State* L) {
	// Lua
	int npc_id = (int)lua_tonumber(L, -2);

	std::unordered_set<int> old_vl;

	for (int i = 1; i <= lua_rawlen(L, 2); ++i) {
		lua_rawgeti(L, 2, i); 
		int id = (int)lua_tointeger(L, -1);
		old_vl.insert(id);
		lua_pop(L, 1);
	}

	lua_pop(L, 2);

	// Move
	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	if (nullptr == npc) { return 0; }

	short old_x = npc->m_x;
	short old_y = npc->m_y;
	short new_x = old_x;
	short new_y = old_y;

	std::array<std::pair<short, short>, 8> directions = {
		std::make_pair( 0, -1),  // Up
		std::make_pair(-1, -1),  // Up-Left
		std::make_pair( 1, -1),  // Up-Right
		std::make_pair( 0,  1),  // Down
		std::make_pair(-1,  1),  // Down-Left
		std::make_pair( 1,  1),  // Down-Right
		std::make_pair(-1,  0),  // Left
		std::make_pair( 1,  0)   // Right
	};

	std::shuffle(directions.begin(), directions.end(), rng);

	for (auto [dx, dy] : directions) {
		new_x = old_x + dx;
		new_y = old_y + dy;

		if (true == is_valid_move(new_x, new_y)) {
			npc->m_x = new_x;
			npc->m_y = new_y;
			break;
		}
	}

	// Update Sector
	short sx = new_x / SECTOR_WIDTH;
	short sy = new_y / SECTOR_HEIGHT;

	update_sector(npc_id, old_x, old_y, new_x, new_y);

	// Create New View List by Sector
	std::unordered_set<int> new_vl;

	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			short nx = sx + dx;
			short ny = sy + dy;

			if ((nx < 0) || (ny < 0) || (nx >= SECTOR_COLS) || (ny >= SECTOR_ROWS)) { continue; }

			std::lock_guard<std::mutex> lock(g_mutex[ny][nx]);

			for (auto cl : g_sector[ny][nx]) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) { continue; }

				if (false == is_player(other->m_id)) { continue; }
				if (ST_INGAME != other->m_state) { continue; }
				if (other->m_id == npc_id) { continue; }

				if (true == can_see(npc->m_id, other->m_id)) {
					new_vl.insert(other->m_id);
				}
			}
		}
	}

	for (auto cl : old_vl) {
		std::shared_ptr<SESSION> other = g_clients.at(cl);
		if (nullptr == other) { continue; }

		if (0 == new_vl.count(cl)) {
			other->m_vl.lock();
			if (0 != other->m_view_list.count(npc->m_id)) {
				other->m_vl.unlock();
				other->send_remove_object(npc->m_id);
			} else {
				other->m_vl.unlock();
			}
		}
	}

	for (auto cl : new_vl) {
		std::shared_ptr<SESSION> other = g_clients.at(cl);
		if (nullptr == other) { continue; }

		if (0 == old_vl.count(cl)) {
			// 플레이어의 시야에 등장
			other->send_add_object(npc->m_id);
		} else {
			// 플레이어가 계속 보고 있음.
			other->send_move_object(npc->m_id);
		}
	}

	push_vl(npc->m_lua, new_vl);

	return 1;
}

int API_do_npc_chase(lua_State* L) {
	// Lua
	int npc_id = (int)lua_tonumber(L, -3);
	int target_id = (int)lua_tonumber(L, -2);

	std::unordered_set<int> old_vl;

	for (int i = 1; i <= lua_rawlen(L, -1); ++i) {
		lua_rawgeti(L, -1, i);
		int id = (int)lua_tointeger(L, -1);
		old_vl.insert(id);
		lua_pop(L, 1);
	}

	lua_pop(L, 3);

	// Move
	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	std::shared_ptr<SESSION> client = g_clients.at(target_id);

	if ((nullptr == npc) || (nullptr == client)) { 
		return 0; 
	}

	auto path = a_star(npc->m_x, npc->m_y, client->m_x, client->m_y);

	if (2 >= path.size()) {
		return 0;
	}

	auto [nx, ny] = path[1];

	short old_x = npc->m_x;
	short old_y = npc->m_y;
	short new_x = old_x;
	short new_y = old_y;

	npc->m_x = new_x = nx;
	npc->m_y = new_y = ny;

	// Update Sector
	short sx = new_x / SECTOR_WIDTH;
	short sy = new_y / SECTOR_HEIGHT;

	update_sector(npc_id, old_x, old_y, new_x, new_y);

	// Create New View List by Sector
	std::unordered_set<int> new_vl;

	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			short nx = sx + dx;
			short ny = sy + dy;

			if (nx < 0 || ny < 0 || nx >= SECTOR_COLS || ny >= SECTOR_ROWS) { continue; }

			std::lock_guard<std::mutex> lock(g_mutex[ny][nx]);
			for (auto cl : g_sector[ny][nx]) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) { continue; }

				if (false == is_player(other->m_id)) { continue; }
				if (ST_INGAME != other->m_state) { continue; }
				if (other->m_id == npc->m_id) { continue; }

				if (true == can_see(npc->m_id, other->m_id)) {
					new_vl.insert(other->m_id);
				}
			}
		}
	}

	for (auto cl : old_vl) {
		std::shared_ptr<SESSION> other = g_clients.at(cl);
		if (nullptr == other) { continue; }

		if (0 == new_vl.count(cl)) {
			other->m_vl.lock();
			if (0 != other->m_view_list.count(npc->m_id)) {
				other->m_vl.unlock();
				other->send_remove_object(npc->m_id);
			} else {
				other->m_vl.unlock();
			}
		}
	}

	for (auto cl : new_vl) {
		std::shared_ptr<SESSION> other = g_clients.at(cl);
		if (nullptr == other) { continue; }

		if (0 == old_vl.count(cl)) {
			// 플레이어의 시야에 등장
			other->send_add_object(npc->m_id);
		} else {
			// 플레이어가 계속 보고 있음.
			other->send_move_object(npc->m_id);
		}
	}

	return 0;
}

int API_do_npc_attack(lua_State* L) {
	// Lua
	int npc_id = (int)lua_tonumber(L, -3);
	int target_id = (int)lua_tonumber(L, -2);

	std::vector<std::pair<short, short>> attacked_coords;

	for (int i = 1; i <= lua_rawlen(L, 3); ++i) {
		lua_rawgeti(L, 3, i); 

		if (lua_istable(L, -1)) {
			lua_rawgeti(L, -1, 1); // x
			lua_rawgeti(L, -2, 2); // y

			short x = (short)lua_tointeger(L, -2);
			short y = (short)lua_tointeger(L, -1);

			attacked_coords.emplace_back(x, y);
			lua_pop(L, 2); // pop x and y
		}

		lua_pop(L, 1); // pop { x, y }
	}

	lua_pop(L, 3);

	// Attack
	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	std::shared_ptr<SESSION> client = g_clients.at(target_id);

	if ((nullptr == npc) || (nullptr == client)) {
		return 0;
	}

	// Emplace Attacked Sectors into Ordered Set in order to Avoid Deadlock
	std::set<std::pair<short, short>> attacked_sectors;
	for (const auto& coord : attacked_coords) {
		attacked_sectors.emplace(coord.first / SECTOR_WIDTH, coord.second / SECTOR_HEIGHT);
	}

	// Sector Lock
	std::vector<std::unique_lock<std::mutex>> sector_locks;
	for (const auto& sector : attacked_sectors) {
		sector_locks.emplace_back(g_mutex[sector.second][sector.first]);
	}

	// Damage Logic
	for (const auto& sector : attacked_sectors) {
		for (const auto& cl : g_sector[sector.second][sector.first]) {
			std::shared_ptr<SESSION> client = g_clients.at(cl);
			if (nullptr == client) { continue; }

			if (false == is_player(client->m_id)) { continue; }
			if (ST_INGAME != client->m_state) { continue; }
			if (client->m_id == npc_id) { continue; }

			if (true == can_see(npc_id, client->m_id)) {
				for (const auto& coord : attacked_coords) {
					if ((coord.first == client->m_x) && (coord.second == client->m_y)) {
						if (true == client->is_alive()) {
							client->receive_damage(1, npc_id);
						}
					}
				}

				// Send Attack Packet to Player
				client->send_attack(npc_id);
			}
		}
	}

	return 0;
}

int API_do_npc_sleep(lua_State* L) {
	int npc_id = (int)lua_tonumber(L, -1);

	lua_pop(L, 1);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	if (nullptr == npc) { return 0; }

	npc->sleep();

	return 0;
}

int API_is_in_chase_range(lua_State* L) {
	int npc_id = (int)lua_tonumber(L, -2);
	int target_id = (int)lua_tonumber(L, -1);

	lua_pop(L, 2);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	std::shared_ptr<SESSION> client = g_clients.at(target_id);

	if ((nullptr == npc) || (nullptr == client)) { 
		lua_pushboolean(L, false); 
		return 1; 
	}

	if ((false == client->is_alive()) ||
		(abs(npc->m_x - client->m_x) > CHASE_RANGE) ||
		(abs(npc->m_y - client->m_y) > CHASE_RANGE)) {
		lua_pushboolean(L, false);
		return 1;
	}

	lua_pushboolean(L, true);

	return 1;
}

int API_is_in_attack_range(lua_State* L) {
	int npc_id = (int)lua_tonumber(L, -2);
	int target_id = (int)lua_tonumber(L, -1);

	lua_pop(L, 2);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	std::shared_ptr<SESSION> client = g_clients.at(target_id);

	if ((nullptr == npc) || (nullptr == client)) { 
		lua_pushboolean(L, false); 
		return 1; 
	}

	short dist = calc_distance_sq(npc->m_x, npc->m_y, client->m_x, client->m_y);

	switch (npc->m_level) {
	case KNIGHT:
		if (KNIGHT_ATTACK_RANGE >= dist) {
			lua_pushboolean(L, true);
			return 1;
		}
		break;

	case QUEEN:
		if (QUEEN_ATTACK_RANGE >= dist) {
			lua_pushboolean(L, true);
			return 1;
		}
		break;
	}

	lua_pushboolean(L, false);

	return 1;
}

int API_get_x(lua_State* L) {
	int npc_id = (int)lua_tointeger(L, -1);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	if (nullptr == npc) { return -1; }

	lua_pop(L, 1);

	int x = npc->m_x;

	lua_pushnumber(L, x);

	return 1;
}

int API_get_y(lua_State* L) {
	int npc_id = (int)lua_tointeger(L, -1);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	if (nullptr == npc) { return -1; }

	lua_pop(L, 1);

	int y = npc->m_y;

	lua_pushnumber(L, y);

	return 1;
}

int API_get_vl(lua_State* L) {
	int npc_id = (int)lua_tonumber(L, -1);

	lua_pop(L, 1);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	if (nullptr == npc) { return 0; }

	// Create Old View List by Sector
	short sx = npc->m_x / SECTOR_WIDTH;
	short sy = npc->m_y / SECTOR_HEIGHT;

	std::unordered_set<int> old_vl;

	for (short dy = -1; dy <= 1; ++dy) {
		for (short dx = -1; dx <= 1; ++dx) {
			short nx = sx + dx;
			short ny = sy + dy;

			if ((nx < 0) || (ny < 0) || (nx >= SECTOR_COLS) || (ny >= SECTOR_ROWS)) { continue; }

			std::lock_guard<std::mutex> lock(g_mutex[ny][nx]);

			for (auto cl : g_sector[ny][nx]) {
				std::shared_ptr<SESSION> other = g_clients.at(cl);
				if (nullptr == other) { continue; }

				if (false == is_player(other->m_id)) { continue; }
				if (ST_INGAME != other->m_state) { continue; }
				if (other->m_id == npc_id) { continue; }

				if (true == can_see(npc->m_id, other->m_id)) {
					old_vl.insert(other->m_id);
				}
			}
		}
	}

	push_vl(npc->m_lua, old_vl);

	return 1;
}

int API_register_event(lua_State* L) {
	int npc_id = (int)lua_tonumber(L, -3);
	int target_id = (int)lua_tonumber(L, -2);
	bool should_attack = (bool)lua_toboolean(L, -1);

	lua_pop(L, 3);

	std::shared_ptr<SESSION> npc = g_clients.at(npc_id);
	if (nullptr == npc) { return 0; }

	if (true == should_attack) {
		timer_lock.lock();
		timer_queue.emplace(event{ npc_id, target_id, std::chrono::high_resolution_clock::now() + std::chrono::seconds(1), EV_NPC_ATTACK });
		timer_lock.unlock();

		return 0;
	}

	timer_lock.lock();
	timer_queue.emplace(event{ npc_id, target_id, std::chrono::high_resolution_clock::now() + std::chrono::seconds(1), EV_NPC_MOVE });
	timer_lock.unlock();

	return 0;
}

void push_vl(lua_State* L, const std::unordered_set<int>& view_list) {
	lua_newtable(L);  

	int i = 1;

	for (int id : view_list) {
		lua_pushinteger(L, id);
		lua_rawseti(L, -2, i++);
	}
}