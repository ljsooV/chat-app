#include "server_session.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
const char* DEFAULT_ROOM = "Lobby";
}

static string current_timestamp()
{
    using namespace std::chrono;

    const system_clock::time_point now = system_clock::now();
    const time_t now_time = system_clock::to_time_t(now);
    tm local_tm = {};
    localtime_s(&local_tm, &now_time);

    ostringstream oss;
    oss << put_time(&local_tm, "%H:%M:%S");
    return oss.str();
}

static string timestamp_message(const string& message)
{
    return "[" + current_timestamp() + "] " + message;
}

static void log_server(const char* level, const string& message)
{
    ostream& stream = string(level) == "ERROR" ? cerr : cout;
    stream << "[" << current_timestamp() << "] "
           << "[" << level << "] "
           << message << "\n";
}

static bool starts_with_command(const string& payload, const string& command)
{
    return payload.rfind(command, 0) == 0;
}

static bool parse_single_argument_command(const string& payload, const string& command, string& argument)
{
    if (!starts_with_command(payload, command))
    {
        return false;
    }

    argument = payload.substr(command.size());
    return !argument.empty();
}

static bool parse_whisper_command(const string& payload, string& target_nickname, string& message)
{
    size_t command_offset = string::npos;
    if (starts_with_command(payload, "/whisper "))
    {
        command_offset = 9;
    }
    else if (starts_with_command(payload, "/w "))
    {
        command_offset = 3;
    }
    else
    {
        return false;
    }

    const size_t first_space = payload.find(' ', command_offset);
    if (first_space == string::npos)
    {
        return false;
    }

    target_nickname = payload.substr(command_offset, first_space - command_offset);
    message = payload.substr(first_space + 1);
    return !target_nickname.empty() && !message.empty();
}

static bool parse_kick_command(const string& payload, string& target_nickname)
{
    if (!starts_with_command(payload, "/kick "))
    {
        return false;
    }

    target_nickname = payload.substr(6);
    return !target_nickname.empty();
}

server_session::server_session()
    : m_serv_sock(INVALID_SOCKET), m_running(false), m_wsa_ready(false)
{
}

server_session::~server_session()
{
    close();
}

bool server_session::init()
{
    WSADATA wsa_data;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        print_wsa_error("WSAStartup() error!");
        return false;
    }

    m_wsa_ready = true;
    m_serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (m_serv_sock == INVALID_SOCKET)
    {
        print_wsa_error("socket() error!");
        WSACleanup();
        m_wsa_ready = false;
        return false;
    }

    return true;
}

bool server_session::start_server()
{
    SOCKADDR_IN serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9000);

    if (bind(m_serv_sock, (SOCKADDR*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR)
    {
        print_wsa_error("bind() error!");
        return false;
    }

    if (listen(m_serv_sock, 5) == SOCKET_ERROR)
    {
        print_wsa_error("listen() error!");
        return false;
    }

    m_running = true;
    log_server("INFO", "server started on port 9000");
    return true;
}

void server_session::accept_loop()
{
    while (m_running)
    {
        SOCKADDR_IN clnt_addr;
        int sz_clnt_addr = sizeof(clnt_addr);

        SOCKET clnt_sock = accept(m_serv_sock, (SOCKADDR*)&clnt_addr, &sz_clnt_addr);
        if (clnt_sock == INVALID_SOCKET)
        {
            if (m_running)
            {
                print_wsa_error("accept() error");
            }
            continue;
        }

        if (client_count() >= MAX_CONNECTED_CLIENTS)
        {
            send_packet(clnt_sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("server is full"));
            closesocket(clnt_sock);
            continue;
        }

        USER_INFO user;
        user.sock = clnt_sock;
        user.nickname = "anonymous";
        user.room = DEFAULT_ROOM;

        {
            lock_guard<mutex> lock(m_client_lock);
            m_client_info[clnt_sock] = user;
        }

        log_server("INFO", "client connected. sock=" + to_string(static_cast<long long>(clnt_sock)));

        lock_guard<mutex> thread_lock(m_thread_lock);
        m_client_threads.emplace_back(&server_session::handle_client, this, clnt_sock);
    }
}

void server_session::handle_client(SOCKET sock)
{
    MESSAGE_TYPE type = MESSAGE_TYPE::SYSTEM;
    string payload;

    if (!recv_packet(sock, type, payload))
    {
        remove_client(sock);
        closesocket(sock);
        return;
    }

    string nickname;
    string current_room = DEFAULT_ROOM;
    if (!accept_nickname(sock, type, payload, nickname, current_room))
    {
        remove_client(sock);
        closesocket(sock);
        return;
    }

    const string owner_name = get_room_owner(current_room);
    const string enter_msg = timestamp_message(
        nickname + " joined room [" + current_room + "]"
        + (owner_name.empty() ? "." : " (owner: " + owner_name + ")."));
    log_server("INFO", nickname + " joined room [" + current_room + "]");
    broadcast_room_message(MESSAGE_TYPE::SYSTEM_JOIN, enter_msg, current_room, INVALID_SOCKET);
    notify_room_state(current_room);
    notify_room_users(current_room);
    notify_room_catalog();

    while (m_running)
    {
        if (!recv_packet(sock, type, payload))
        {
            break;
        }

        if (type != MESSAGE_TYPE::CHAT)
        {
            log_server("ERROR", "unexpected packet type from sock=" + to_string(static_cast<long long>(sock)));
            break;
        }

        if (!is_valid_chat_message(payload))
        {
            continue;
        }

        if (process_command(sock, payload, nickname, current_room))
        {
            continue;
        }

        process_chat_message(sock, payload, nickname, current_room);
    }

    const string previous_room = current_room;
    const string leave_msg = timestamp_message(nickname + " left room [" + previous_room + "].");
    log_server("INFO", nickname + " left room [" + previous_room + "]");
    broadcast_room_message(MESSAGE_TYPE::SYSTEM_LEAVE, leave_msg, previous_room, INVALID_SOCKET);
    remove_client(sock);
    notify_room_state(previous_room);
    notify_room_users(previous_room);
    notify_room_catalog();
    closesocket(sock);
}

bool server_session::accept_nickname(
    SOCKET sock,
    MESSAGE_TYPE first_type,
    const string& payload,
    string& nickname,
    string& current_room)
{
    if (first_type != MESSAGE_TYPE::NICKNAME)
    {
        send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("first packet must be nickname"));
        return false;
    }

    nickname = payload;
    if (!is_valid_nickname(nickname))
    {
        send_packet(sock, MESSAGE_TYPE::NICKNAME_REJECTED, timestamp_message("invalid nickname"));
        return false;
    }

    if (nickname_exists(nickname, sock))
    {
        send_packet(sock, MESSAGE_TYPE::NICKNAME_REJECTED, timestamp_message("nickname already in use"));
        return false;
    }

    {
        lock_guard<mutex> lock(m_client_lock);
        map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
        if (it != m_client_info.end())
        {
            it->second.nickname = nickname;
            current_room = it->second.room;
        }
    }

    return send_packet(sock, MESSAGE_TYPE::NICKNAME_ACCEPTED, timestamp_message("nickname accepted"));
}

bool server_session::process_command(SOCKET sock, const string& payload, string& nickname, string& current_room)
{
    if (payload == "/list")
    {
        send_packet(sock, MESSAGE_TYPE::USER_LIST, timestamp_message(build_user_list(current_room)));
        return true;
    }

    if (payload == "/rooms")
    {
        send_packet(sock, MESSAGE_TYPE::ROOM_LIST, timestamp_message(build_room_list()));
        return true;
    }

    string requested_room;
    if (parse_single_argument_command(payload, "/create ", requested_room)
        || parse_single_argument_command(payload, "/join ", requested_room))
    {
        if (!is_valid_room_name(requested_room))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("invalid room name"));
            return true;
        }

        const bool create_request = starts_with_command(payload, "/create ");
        if (requested_room == current_room)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_INFO, timestamp_message("already in room [" + current_room + "]"));
            return true;
        }

        if (create_request && room_exists(requested_room))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("room already exists: " + requested_room));
            return true;
        }

        if (!create_request && !room_exists(requested_room))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("room not found: " + requested_room));
            return true;
        }

        const string previous_room = current_room;
        {
            lock_guard<mutex> lock(m_client_lock);
            map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
            if (it != m_client_info.end())
            {
                if (create_request)
                {
                    m_room_owners[requested_room] = nickname;
                }

                it->second.room = requested_room;
                cleanup_room_state_locked(previous_room);
            }
        }

        current_room = requested_room;
        broadcast_room_message(MESSAGE_TYPE::SYSTEM_LEAVE, timestamp_message(nickname + " left room [" + previous_room + "]."), previous_room, INVALID_SOCKET);
        broadcast_room_message(MESSAGE_TYPE::SYSTEM_JOIN, timestamp_message(nickname + " joined room [" + current_room + "]."), current_room, INVALID_SOCKET);
        notify_room_state(previous_room);
        notify_room_state(current_room);
        notify_room_users(previous_room);
        notify_room_users(current_room);
        notify_room_catalog();
        return true;
    }

    if (payload == "/leave")
    {
        if (current_room == DEFAULT_ROOM)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_INFO, timestamp_message("already in room [Lobby]"));
            return true;
        }

        const string previous_room = current_room;
        {
            lock_guard<mutex> lock(m_client_lock);
            map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
            if (it != m_client_info.end())
            {
                it->second.room = DEFAULT_ROOM;
                cleanup_room_state_locked(previous_room);
            }
        }

        current_room = DEFAULT_ROOM;
        broadcast_room_message(MESSAGE_TYPE::SYSTEM_LEAVE, timestamp_message(nickname + " left room [" + previous_room + "]."), previous_room, INVALID_SOCKET);
        broadcast_room_message(MESSAGE_TYPE::SYSTEM_JOIN, timestamp_message(nickname + " joined room [Lobby]."), DEFAULT_ROOM, INVALID_SOCKET);
        notify_room_state(previous_room);
        notify_room_state(DEFAULT_ROOM);
        notify_room_users(previous_room);
        notify_room_users(DEFAULT_ROOM);
        notify_room_catalog();
        return true;
    }

    if (payload == "/close")
    {
        if (current_room == DEFAULT_ROOM)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("Lobby cannot be closed"));
            return true;
        }

        if (get_room_owner(current_room) != nickname)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("only the room owner can close this room"));
            return true;
        }

        vector<SOCKET> moved_users;
        const string closed_room = current_room;
        {
            lock_guard<mutex> lock(m_client_lock);
            for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
            {
                if (it->second.sock == INVALID_SOCKET || it->second.room != closed_room)
                {
                    continue;
                }

                it->second.room = DEFAULT_ROOM;
                moved_users.push_back(it->second.sock);
            }

            m_room_owners.erase(closed_room);
        }

        current_room = DEFAULT_ROOM;
        for (size_t i = 0; i < moved_users.size(); ++i)
        {
            send_packet(moved_users[i], MESSAGE_TYPE::SYSTEM_INFO, timestamp_message("room [" + closed_room + "] was closed by " + nickname + ". moved to [Lobby]."));
        }

        notify_room_state(DEFAULT_ROOM);
        notify_room_users(DEFAULT_ROOM);
        notify_room_catalog();
        return true;
    }

    string kick_target;
    if (parse_kick_command(payload, kick_target))
    {
        if (current_room == DEFAULT_ROOM)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("kick is available only inside custom rooms"));
            return true;
        }

        if (get_room_owner(current_room) != nickname)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("only the room owner can kick users"));
            return true;
        }

        SOCKET target_sock = find_client_by_nickname_in_room(kick_target, current_room, sock);
        if (target_sock == INVALID_SOCKET)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("user not found in room: " + kick_target));
            return true;
        }

        {
            lock_guard<mutex> lock(m_client_lock);
            map<SOCKET, USER_INFO>::iterator it = m_client_info.find(target_sock);
            if (it != m_client_info.end())
            {
                it->second.room = DEFAULT_ROOM;
            }
            cleanup_room_state_locked(current_room);
        }

        send_packet(target_sock, MESSAGE_TYPE::SYSTEM_INFO, timestamp_message("you were removed from room [" + current_room + "] by " + nickname + ". moved to [Lobby]."));
        broadcast_room_message(MESSAGE_TYPE::SYSTEM_LEAVE, timestamp_message(kick_target + " was removed from room [" + current_room + "] by " + nickname + "."), current_room, INVALID_SOCKET);
        notify_room_state(current_room);
        notify_room_users(current_room);
        notify_room_state(DEFAULT_ROOM);
        notify_room_users(DEFAULT_ROOM);
        notify_room_catalog();
        return true;
    }

    if (starts_with_command(payload, "/name "))
    {
        const string new_nickname = payload.substr(6);
        if (!is_valid_nickname(new_nickname))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("invalid nickname"));
            return true;
        }

        if (nickname_exists(new_nickname, sock))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("nickname already in use"));
            return true;
        }

        const string old_nickname = nickname;
        {
            lock_guard<mutex> lock(m_client_lock);
            map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
            if (it != m_client_info.end())
            {
                it->second.nickname = new_nickname;
            }

            for (map<string, string>::iterator room_it = m_room_owners.begin(); room_it != m_room_owners.end(); ++room_it)
            {
                if (room_it->second == old_nickname)
                {
                    room_it->second = new_nickname;
                }
            }
        }

        nickname = new_nickname;
        broadcast_room_message(MESSAGE_TYPE::NICKNAME_CHANGED, timestamp_message(old_nickname + " is now known as " + nickname + " in room [" + current_room + "]"), current_room, INVALID_SOCKET);
        notify_room_state(current_room);
        notify_room_users(current_room);
        notify_room_catalog();
        return true;
    }

    string target_nickname;
    string whisper_message;
    if (parse_whisper_command(payload, target_nickname, whisper_message))
    {
        if (!is_valid_chat_message(whisper_message))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("invalid whisper message"));
            return true;
        }

        SOCKET target_sock = find_client_by_nickname(target_nickname);
        if (target_sock == INVALID_SOCKET)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("user not found: " + target_nickname));
            return true;
        }

        if (target_sock == sock)
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("cannot whisper to yourself"));
            return true;
        }

        if (!send_packet(target_sock, MESSAGE_TYPE::WHISPER, timestamp_message("[from " + nickname + "] " + whisper_message)))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("failed to deliver whisper"));
            return true;
        }

        send_packet(sock, MESSAGE_TYPE::WHISPER, timestamp_message("[to " + target_nickname + "] " + whisper_message));
        return true;
    }

    if (starts_with_command(payload, "/whisper") || starts_with_command(payload, "/w"))
    {
        send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("usage: /whisper <nickname> <message>"));
        return true;
    }

    return false;
}

void server_session::process_chat_message(SOCKET sock, const string& payload, const string& nickname, const string& current_room)
{
    const string chat_msg = timestamp_message("[" + current_room + "] " + nickname + " : " + payload);
    log_server("CHAT", "[" + current_room + "] " + nickname + " : " + payload);
    broadcast_room_message(MESSAGE_TYPE::CHAT, chat_msg, current_room, sock);
}

void server_session::broadcast_message(MESSAGE_TYPE type, const string& msg, SOCKET sender_sock)
{
    vector<SOCKET> sockets;
    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
        {
            if (it->second.sock != INVALID_SOCKET)
            {
                sockets.push_back(it->second.sock);
            }
        }
    }

    for (size_t i = 0; i < sockets.size(); ++i)
    {
        if (!send_packet(sockets[i], type, msg))
        {
            log_server("ERROR", "send() error. sock=" + to_string(static_cast<long long>(sockets[i])));
            remove_client(sockets[i]);
            closesocket(sockets[i]);
        }
    }
}

void server_session::broadcast_room_message(MESSAGE_TYPE type, const string& msg, const string& room_name, SOCKET sender_sock)
{
    vector<SOCKET> sockets;
    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
        {
            if (it->second.sock == INVALID_SOCKET || it->second.room != room_name)
            {
                continue;
            }

            sockets.push_back(it->second.sock);
        }
    }

    for (size_t i = 0; i < sockets.size(); ++i)
    {
        if (!send_packet(sockets[i], type, msg))
        {
            log_server("ERROR", "send() error. sock=" + to_string(static_cast<long long>(sockets[i])));
            remove_client(sockets[i]);
            closesocket(sockets[i]);
        }
    }
}

void server_session::remove_client(SOCKET sock)
{
    lock_guard<mutex> lock(m_client_lock);
    map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
    if (it != m_client_info.end())
    {
        const string room_name = it->second.room;
        m_client_info.erase(it);
        cleanup_room_state_locked(room_name);
    }
}

void server_session::close()
{
    m_running = false;

    if (m_serv_sock != INVALID_SOCKET)
    {
        shutdown(m_serv_sock, SD_BOTH);
    }

    vector<SOCKET> client_sockets;
    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
        {
            if (it->second.sock != INVALID_SOCKET)
            {
                client_sockets.push_back(it->second.sock);
            }
        }
    }

    for (size_t i = 0; i < client_sockets.size(); ++i)
    {
        shutdown(client_sockets[i], SD_BOTH);
    }

    vector<thread> client_threads;
    {
        lock_guard<mutex> thread_lock(m_thread_lock);
        client_threads.swap(m_client_threads);
    }

    for (size_t i = 0; i < client_threads.size(); ++i)
    {
        if (client_threads[i].joinable())
        {
            client_threads[i].join();
        }
    }

    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
        {
            if (it->second.sock != INVALID_SOCKET)
            {
                closesocket(it->second.sock);
            }
        }
        m_client_info.clear();
    }

    if (m_serv_sock != INVALID_SOCKET)
    {
        closesocket(m_serv_sock);
        m_serv_sock = INVALID_SOCKET;
    }

    if (m_wsa_ready)
    {
        WSACleanup();
        m_wsa_ready = false;
    }
}

bool server_session::nickname_exists(const string& nickname, SOCKET exclude_sock)
{
    lock_guard<mutex> lock(m_client_lock);
    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->first == exclude_sock)
        {
            continue;
        }

        if (it->second.nickname == nickname)
        {
            return true;
        }
    }
    return false;
}

size_t server_session::client_count()
{
    lock_guard<mutex> lock(m_client_lock);
    return m_client_info.size();
}

SOCKET server_session::find_client_by_nickname(const string& nickname)
{
    lock_guard<mutex> lock(m_client_lock);
    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock != INVALID_SOCKET && it->second.nickname == nickname)
        {
            return it->second.sock;
        }
    }
    return INVALID_SOCKET;
}

SOCKET server_session::find_client_by_nickname_in_room(const string& nickname, const string& room_name, SOCKET exclude_sock)
{
    lock_guard<mutex> lock(m_client_lock);
    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->first == exclude_sock)
        {
            continue;
        }

        if (it->second.sock != INVALID_SOCKET
            && it->second.room == room_name
            && it->second.nickname == nickname)
        {
            return it->second.sock;
        }
    }
    return INVALID_SOCKET;
}

string server_session::build_user_list(const string& room_name)
{
    lock_guard<mutex> lock(m_client_lock);

    string user_list = "room [" + room_name + "] users: ";
    bool first = true;
    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock == INVALID_SOCKET || it->second.room != room_name)
        {
            continue;
        }

        if (!first)
        {
            user_list += ", ";
        }

        user_list += it->second.nickname;
        first = false;
    }

    if (first)
    {
        user_list += "(none)";
    }

    return user_list;
}

string server_session::build_room_list()
{
    lock_guard<mutex> lock(m_client_lock);

    map<string, size_t> room_counts;
    room_counts[DEFAULT_ROOM] = 0;
    for (map<string, string>::iterator it = m_room_owners.begin(); it != m_room_owners.end(); ++it)
    {
        room_counts[it->first] = 0;
    }

    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock != INVALID_SOCKET)
        {
            room_counts[it->second.room] += 1;
        }
    }

    string room_list = "rooms: ";
    bool first = true;
    for (map<string, size_t>::iterator it = room_counts.begin(); it != room_counts.end(); ++it)
    {
        if (!first)
        {
            room_list += ", ";
        }

        room_list += it->first + " (" + to_string(it->second) + ")";
        map<string, string>::iterator owner_it = m_room_owners.find(it->first);
        if (owner_it != m_room_owners.end() && !owner_it->second.empty())
        {
            room_list += " (owner: " + owner_it->second + ")";
        }

        first = false;
    }

    return room_list;
}

string server_session::get_client_room(SOCKET sock)
{
    lock_guard<mutex> lock(m_client_lock);
    map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
    if (it != m_client_info.end())
    {
        return it->second.room;
    }
    return DEFAULT_ROOM;
}

string server_session::get_room_owner(const string& room_name)
{
    lock_guard<mutex> lock(m_client_lock);
    map<string, string>::iterator it = m_room_owners.find(room_name);
    if (it != m_room_owners.end())
    {
        return it->second;
    }
    return "";
}

bool server_session::room_exists(const string& room_name)
{
    lock_guard<mutex> lock(m_client_lock);
    if (room_name == DEFAULT_ROOM)
    {
        return true;
    }

    if (m_room_owners.find(room_name) != m_room_owners.end())
    {
        return true;
    }

    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock != INVALID_SOCKET && it->second.room == room_name)
        {
            return true;
        }
    }

    return false;
}

void server_session::notify_room_catalog()
{
    broadcast_message(MESSAGE_TYPE::ROOM_LIST, timestamp_message(build_room_list()), INVALID_SOCKET);
}

void server_session::notify_room_state(const string& room_name)
{
    if (room_name.empty())
    {
        return;
    }

    const string owner_name = get_room_owner(room_name);
    broadcast_room_message(MESSAGE_TYPE::ROOM_CHANGED, timestamp_message("current room: " + room_name + (owner_name.empty() ? "" : " (owner: " + owner_name + ")")), room_name, INVALID_SOCKET);
}

void server_session::notify_room_users(const string& room_name)
{
    if (room_name.empty())
    {
        return;
    }

    broadcast_room_message(MESSAGE_TYPE::USER_LIST, timestamp_message(build_user_list(room_name)), room_name, INVALID_SOCKET);
}

void server_session::cleanup_room_state_locked(const string& room_name)
{
    if (room_name == DEFAULT_ROOM)
    {
        return;
    }

    size_t room_user_count = 0;
    string next_owner;
    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock == INVALID_SOCKET || it->second.room != room_name)
        {
            continue;
        }

        room_user_count += 1;
        if (next_owner.empty())
        {
            next_owner = it->second.nickname;
        }
    }

    if (room_user_count == 0)
    {
        m_room_owners.erase(room_name);
        return;
    }

    map<string, string>::iterator owner_it = m_room_owners.find(room_name);
    if (owner_it == m_room_owners.end() || owner_it->second.empty())
    {
        m_room_owners[room_name] = next_owner;
        return;
    }

    bool owner_still_present = false;
    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock != INVALID_SOCKET && it->second.room == room_name && it->second.nickname == owner_it->second)
        {
            owner_still_present = true;
            break;
        }
    }

    if (!owner_still_present)
    {
        owner_it->second = next_owner;
    }
}
