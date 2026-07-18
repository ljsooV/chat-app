#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace std;

namespace chat
{
    inline constexpr uint16_t default_port          = 9000;
    inline constexpr string_view DEFAULT_ROOM       = "Lobby";

    inline constexpr size_t packet_header_size      = 8;
    inline constexpr size_t max_packet_payload_size = 64 * 1024;
    inline constexpr size_t max_nickname_length     = 20;
    inline constexpr size_t max_username_length     = 24;
    inline constexpr size_t max_password_length     = 32;
    inline constexpr size_t max_room_name_length    = 24;
    inline constexpr size_t max_chat_length         = 512;
    inline constexpr size_t max_connected_clients   = 10;

    enum class MESSAGE_TYPE : uint32_t
    {
        UNKNOWN = 0,
        NICKNAME = 1,
        CHAT,
        SYSTEM,
        NICKNAME_ACCEPTED,
        NICKNAME_REJECTED,
        SYSTEM_INFO,
        SYSTEM_JOIN,
        SYSTEM_LEAVE,
        SYSTEM_ERROR,
        USER_LIST,
        NICKNAME_CHANGED,
        WHISPER,
        ROOM_LIST,
        ROOM_CHANGED,
    };

    struct packet_header
    {
        uint32_t m_type = 0;
        uint32_t m_size = 0;
    };
}
