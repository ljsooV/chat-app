#include "socket_io.h"

#include "packet_codec.h"
#include "validation.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace std;

namespace
{
    bool send_all(SOCKET socket, const char* data, int length)
    {
        int total_sent = 0;

        while (total_sent < length)
        {
            const int sent = send(socket, data + total_sent, length - total_sent, 0);

            if (sent == SOCKET_ERROR || sent == 0)
            {
                return false;
            }

            total_sent += sent;
        }

        return true;
    }

    bool recv_all(SOCKET socket, char* data, int length)
    {
        int total_received = 0;

        while (total_received < length)
        {
            const int received = recv(socket, data + total_received, length - total_received, 0);

            if (received == SOCKET_ERROR || received == 0)
            {
                return false;
            }

            total_received += received;
        }

        return true;
    }
}

void print_wsa_error(const char* message)
{
    cerr << message << " (WSA error: " << WSAGetLastError() << ")\n";
}

bool send_packet(SOCKET socket, chat::MESSAGE_TYPE type, const string& payload)
{
    const vector<uint8_t> packet = chat::make_packet(type, payload);

    return send_all(socket, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()));
}

bool recv_packet(SOCKET socket, chat::MESSAGE_TYPE& type, string& payload)
{
    chat::packet_header header;
    if (false == recv_all(socket, reinterpret_cast<char*>(&header), static_cast<int>(sizeof(header))))
    {
        return false;
    }

    const uint32_t type_value = ntohl(header.m_type);
    const uint32_t payload_size = ntohl(header.m_size);

    if (!chat::is_valid_message_type(type_value) || payload_size > chat::max_packet_payload_size)
    {
        return false;
    }

    type = static_cast<chat::MESSAGE_TYPE>(type_value);
    payload.clear();

    if (payload_size == 0)
    {
        return true;
    }

    vector<char> buffer(payload_size);
    if (false == recv_all(socket, buffer.data(), static_cast<int>(payload_size)))
    {
        return false;
    }

    payload.assign(buffer.begin(), buffer.end());

    return true;
}
