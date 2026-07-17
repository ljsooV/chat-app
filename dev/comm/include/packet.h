#pragma once

#include <cstddef>
#include <cstdint>

enum class MESSAGE_TYPE : uint32_t
{
    NICKNAME = 1,
    CHAT = 2,
    SYSTEM = 3,
    NICKNAME_ACCEPTED = 4,
    NICKNAME_REJECTED = 5,
    SYSTEM_INFO = 6,
    SYSTEM_JOIN = 7,
    SYSTEM_LEAVE = 8,
    SYSTEM_ERROR = 9,
    USER_LIST = 10,
    NICKNAME_CHANGED = 11,
    WHISPER = 12,
    ROOM_LIST = 13,
    ROOM_CHANGED = 14,
};

constexpr size_t MAX_PACKET_PAYLOAD_SIZE = 64 * 1024;
constexpr size_t MAX_NICKNAME_LENGTH = 20;
constexpr size_t MAX_USERNAME_LENGTH = 24;
constexpr size_t MAX_PASSWORD_LENGTH = 32;
constexpr size_t MAX_ROOM_NAME_LENGTH = 24;
constexpr size_t MAX_CHAT_LENGTH = 512;
constexpr size_t MAX_CONNECTED_CLIENTS = 10;

struct PACKET_HEADER
{
    uint32_t type = 0;
    uint32_t size = 0;
};
