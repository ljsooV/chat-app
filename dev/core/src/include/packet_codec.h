#pragma once

#include "chat_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std;

namespace chat
{
    enum class PARSE_STATUS
    {
        COMPLETE,
        NEED_MORE_DATA,
        INVALID_PACKET,
    };

    struct parse_result
    {
        PARSE_STATUS m_status = PARSE_STATUS::NEED_MORE_DATA;
        MESSAGE_TYPE m_type = MESSAGE_TYPE::UNKNOWN;
        string m_payload;
        size_t m_consumed_size = 0;
    };

    bool is_known_message_type(uint32_t value);
    vector<uint8_t> make_packet(MESSAGE_TYPE type, string_view payload);
    parse_result try_parse_packet(const uint8_t* data, size_t size);
}
