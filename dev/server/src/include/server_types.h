#pragma once

#include <string>
#include <WinSock2.h>

#include "chat_types.h"

using namespace std;

struct user_info
{
    string m_nickname;
    string m_account_name;
    string m_room = string(chat::DEFAULT_ROOM);
    int m_id = -1;
    SOCKET m_socket = INVALID_SOCKET;
};
