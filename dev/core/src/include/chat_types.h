#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace std;

namespace chat
{
    inline constexpr uint16_t DEFAULT_PORT          = 9000;
    inline constexpr string_view DEFAULT_ROOM       = "Lobby";

    inline constexpr size_t PACKET_HEADER_SIZE      = 8;
    inline constexpr size_t MAX_PACKET_PAYLOAD_SIZE = 64 * 1024;
    inline constexpr size_t MAX_NICKNAME_LENGTH     = 20;
    inline constexpr size_t MAX_USERNAME_LENGTH     = 24;
    inline constexpr size_t MAX_PASSWORD_LENGTH     = 32;
    inline constexpr size_t MAX_ROOM_NAME_LENGTH    = 24;
    inline constexpr size_t MAX_CHAT_LENGTH         = 512;
    inline constexpr size_t MAX_CONNECTED_CLIENTS   = 10;

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
