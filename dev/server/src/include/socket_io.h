#pragma once

#include <string>
#include <WinSock2.h>

#include "chat_types.h"

using namespace std;

#pragma comment(lib, "ws2_32.lib")

void print_wsa_error(const char* message);
bool send_packet(SOCKET socket, chat::MESSAGE_TYPE type, const string& payload);
bool recv_packet(SOCKET socket, chat::MESSAGE_TYPE& type, string& payload);
