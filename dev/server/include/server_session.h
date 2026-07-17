#pragma once

#include "def.h"

class server_session
{
public:
    server_session();
    ~server_session();

    bool init();
    bool start_server();
    void accept_loop();
    void handle_client(SOCKET sock);
    void broadcast_message(MESSAGE_TYPE type, const string& msg, SOCKET sender_sock = INVALID_SOCKET);
    void broadcast_room_message(MESSAGE_TYPE type, const string& msg, const string& room_name, SOCKET sender_sock = INVALID_SOCKET);
    size_t client_count();
    SOCKET find_client_by_nickname(const string& nickname);
    SOCKET find_client_by_nickname_in_room(const string& nickname, const string& room_name, SOCKET exclude_sock = INVALID_SOCKET);
    bool nickname_exists(const string& nickname, SOCKET exclude_sock = INVALID_SOCKET);
    string build_user_list(const string& room_name);
    string build_room_list();
    string get_client_room(SOCKET sock);
    string get_room_owner(const string& room_name);
    bool room_exists(const string& room_name);
    void remove_client(SOCKET sock);
    void close();

private:
    bool process_command(SOCKET sock, const string& payload, string& nickname, string& current_room);
    void process_chat_message(SOCKET sock, const string& payload, const string& nickname, const string& current_room);
    bool accept_nickname(SOCKET sock, MESSAGE_TYPE first_type, const string& payload, string& nickname, string& current_room);
    void notify_room_catalog();
    void notify_room_state(const string& room_name);
    void notify_room_users(const string& room_name);
    void cleanup_room_state_locked(const string& room_name);

    SOCKET m_serv_sock;
    map<SOCKET, USER_INFO> m_client_info;
    map<string, string> m_room_owners;
    mutex m_client_lock;
    vector<thread> m_client_threads;
    mutex m_thread_lock;

    atomic<bool> m_running;
    atomic<bool> m_wsa_ready;
};
