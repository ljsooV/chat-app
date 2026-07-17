#include "client_session.h"

const char* DEFAULT_IP = "127.0.0.1";
const char* DEFAULT_PORT = "9000";

static bool resolve_host(const string& host, const string& port, SOCKADDR_IN& addr)
{
    ADDRINFOA hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOA* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr)
    {
        return false;
    }

    memcpy(&addr, result->ai_addr, sizeof(SOCKADDR_IN));
    freeaddrinfo(result);
    return true;
}

static bool is_valid_port_string(const string& port)
{
    if (port.empty())
    {
        return false;
    }

    for (size_t i = 0; i < port.size(); ++i)
    {
        if (port[i] < '0' || port[i] > '9')
        {
            return false;
        }
    }

    const int port_number = atoi(port.c_str());
    return port_number > 0 && port_number <= 65535;
}

static void print_client_message(MESSAGE_TYPE type, const string& payload)
{
    if (payload.empty())
    {
        return;
    }

    switch (type)
    {
    case MESSAGE_TYPE::SYSTEM:
    case MESSAGE_TYPE::SYSTEM_INFO:
        cout << "[SYSTEM] " << payload << endl;
        break;
    case MESSAGE_TYPE::SYSTEM_JOIN:
        cout << "[JOIN] " << payload << endl;
        break;
    case MESSAGE_TYPE::SYSTEM_LEAVE:
        cout << "[LEAVE] " << payload << endl;
        break;
    case MESSAGE_TYPE::SYSTEM_ERROR:
        cerr << "[ERROR] " << payload << endl;
        break;
    case MESSAGE_TYPE::USER_LIST:
        cout << "[USERS] " << payload << endl;
        break;
    case MESSAGE_TYPE::NICKNAME_CHANGED:
        cout << "[NAME] " << payload << endl;
        break;
    case MESSAGE_TYPE::WHISPER:
        cout << "[WHISPER] " << payload << endl;
        break;
    case MESSAGE_TYPE::ROOM_LIST:
        cout << "[ROOMS] " << payload << endl;
        break;
    case MESSAGE_TYPE::ROOM_CHANGED:
        cout << "[ROOM] " << payload << endl;
        break;
    case MESSAGE_TYPE::CHAT:
        cout << payload << endl;
        break;
    case MESSAGE_TYPE::NICKNAME_ACCEPTED:
        cout << "[CONNECTED] " << payload << endl;
        break;
    case MESSAGE_TYPE::NICKNAME_REJECTED:
        cerr << "[REJECTED] " << payload << endl;
        break;
    default:
        cerr << "[UNKNOWN] " << payload << endl;
        break;
    }
}

client_session::client_session()
    : m_host(DEFAULT_IP), m_port(DEFAULT_PORT), m_running(false), m_wsa_ready(false)
{
}

client_session::~client_session()
{
    close();

    if (m_wsa_ready)
    {
        WSACleanup();
        m_wsa_ready = false;
    }
}

bool client_session::init()
{
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        print_wsa_error("WSAStartup() error");
        return false;
    }

    m_wsa_ready = true;
    m_user_info.sock = socket(PF_INET, SOCK_STREAM, 0);
    if (m_user_info.sock == INVALID_SOCKET)
    {
        print_wsa_error("socket() error");
        WSACleanup();
        m_wsa_ready = false;
        return false;
    }

    return true;
}

void client_session::close()
{
    lock_guard<mutex> lock(m_state_lock);
    m_running = false;

    if (m_user_info.sock != INVALID_SOCKET)
    {
        shutdown(m_user_info.sock, SD_BOTH);
        closesocket(m_user_info.sock);
        m_user_info.sock = INVALID_SOCKET;
    }
}

void client_session::configure_endpoint(const string& host, const string& port)
{
    m_host = host.empty() ? DEFAULT_IP : host;
    m_port = is_valid_port_string(port) ? port : DEFAULT_PORT;

    if (m_port != port || host.empty())
    {
        cout << "[SYSTEM] invalid or empty server input. using "
             << m_host << ":" << m_port << endl;
    }
}

bool client_session::connect_server()
{
    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof(addr));

    if (!resolve_host(m_host, m_port, addr))
    {
        cerr << "[ERROR] failed to resolve host " << m_host << ":" << m_port << endl;
        if (m_host != DEFAULT_IP || m_port != DEFAULT_PORT)
        {
            cout << "[SYSTEM] falling back to " << DEFAULT_IP << ":" << DEFAULT_PORT << endl;
            m_host = DEFAULT_IP;
            m_port = DEFAULT_PORT;
            if (!resolve_host(m_host, m_port, addr))
            {
                cerr << "[ERROR] failed to resolve fallback host\n";
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    if (connect(m_user_info.sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        print_wsa_error("connect() error!");
        return false;
    }

    m_running = true;
    cout << "[SYSTEM] connected target " << m_host << ":" << m_port << endl;
    return true;
}

void client_session::recv_loop()
{
    while (m_running)
    {
        MESSAGE_TYPE type = MESSAGE_TYPE::SYSTEM;
        string payload;

        if (!recv_packet(m_user_info.sock, type, payload))
        {
            if (m_running)
            {
                cerr << "[DISCONNECTED] server closed connection\n";
            }
            break;
        }

        if (type == MESSAGE_TYPE::CHAT
            || type == MESSAGE_TYPE::SYSTEM
            || type == MESSAGE_TYPE::SYSTEM_INFO
            || type == MESSAGE_TYPE::SYSTEM_JOIN
            || type == MESSAGE_TYPE::SYSTEM_LEAVE
            || type == MESSAGE_TYPE::SYSTEM_ERROR
            || type == MESSAGE_TYPE::USER_LIST
            || type == MESSAGE_TYPE::NICKNAME_CHANGED
            || type == MESSAGE_TYPE::WHISPER
            || type == MESSAGE_TYPE::ROOM_LIST
            || type == MESSAGE_TYPE::ROOM_CHANGED)
        {
            print_client_message(type, payload);
        }
    }

    m_running = false;
}

bool client_session::send_nickname(const string& nickname)
{
    if (!is_valid_nickname(nickname))
    {
        cerr << "invalid nickname. use 1-" << MAX_NICKNAME_LENGTH << " visible characters\n";
        return false;
    }

    if (!send_packet(m_user_info.sock, MESSAGE_TYPE::NICKNAME, nickname))
    {
        print_wsa_error("send nickname error");
        m_running = false;
        return false;
    }

    return true;
}

bool client_session::send_chat(const string& msg)
{
    if (!m_running)
    {
        return false;
    }

    if (!is_valid_chat_message(msg))
    {
        cerr << "invalid chat message. use 1-" << MAX_CHAT_LENGTH << " characters\n";
        return false;
    }

    if (!send_packet(m_user_info.sock, MESSAGE_TYPE::CHAT, msg))
    {
        print_wsa_error("send chat error");
        m_running = false;
        return false;
    }

    return true;
}

bool client_session::wait_for_nickname_response()
{
    MESSAGE_TYPE type = MESSAGE_TYPE::SYSTEM;
    string payload;

    if (!recv_packet(m_user_info.sock, type, payload))
    {
        cerr << "[DISCONNECTED] server closed connection during handshake\n";
        m_running = false;
        return false;
    }

    if (type == MESSAGE_TYPE::NICKNAME_ACCEPTED)
    {
        print_client_message(type, payload);
        return true;
    }

    if (type == MESSAGE_TYPE::NICKNAME_REJECTED)
    {
        print_client_message(type, payload);
        m_running = false;
        return false;
    }

    cerr << "unexpected handshake response from server\n";
    m_running = false;
    return false;
}

bool client_session::is_running() const
{
    return m_running;
}
