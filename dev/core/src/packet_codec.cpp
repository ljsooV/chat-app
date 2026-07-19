#include "packet_codec.h"

using namespace std;

namespace chat
{
    namespace
    {
        void write_u32_be(uint8_t* destination, uint32_t value)
        {
            destination[0] = static_cast<uint8_t>((value >> 24) & 0xff);
            destination[1] = static_cast<uint8_t>((value >> 16) & 0xff);
            destination[2] = static_cast<uint8_t>((value >> 8) & 0xff);
            destination[3] = static_cast<uint8_t>(value & 0xff);
        }

        uint32_t read_u32_be(const uint8_t* source)
        {
            return (static_cast<uint32_t>(source[0]) << 24)
                | (static_cast<uint32_t>(source[1]) << 16)
                | (static_cast<uint32_t>(source[2]) << 8)
                | static_cast<uint32_t>(source[3]);
        }
    }

    bool is_known_message_type(uint32_t value)
    {
        return value >= static_cast<uint32_t>(MESSAGE_TYPE::NICKNAME)
            && value <= static_cast<uint32_t>(MESSAGE_TYPE::ROOM_CHANGED);
    }

    vector<uint8_t> make_packet(MESSAGE_TYPE type, string_view payload)
    {
        vector<uint8_t> packet(PACKET_HEADER_SIZE + payload.size());

        write_u32_be(packet.data(), static_cast<uint32_t>(type));
        write_u32_be(packet.data() + 4, static_cast<uint32_t>(payload.size()));

        for (size_t index = 0; index < payload.size(); ++index)
        {
            packet[PACKET_HEADER_SIZE + index] = static_cast<uint8_t>(payload[index]);
        }

        return packet;
    }

    parse_result try_parse_packet(const uint8_t* data, size_t size)
    {
        parse_result result;
        if (data == nullptr || size < PACKET_HEADER_SIZE)
        {
            return result;
        }

        const uint32_t type_value = read_u32_be(data);
        const uint32_t payload_size = read_u32_be(data + 4);
        if (false == is_known_message_type(type_value) || payload_size > MAX_PACKET_PAYLOAD_SIZE)
        {
            result.m_status = PARSE_STATUS::INVALID_PACKET;

            return result;
        }

        const size_t packet_size = PACKET_HEADER_SIZE + payload_size;
        if (size < packet_size)
        {
            return result;
        }

        result.m_status = PARSE_STATUS::COMPLETE;
        result.m_type = static_cast<MESSAGE_TYPE>(type_value);
        result.m_payload.assign(reinterpret_cast<const char*>(data + PACKET_HEADER_SIZE), static_cast<size_t>(payload_size));
        result.m_consumed_size = packet_size;

        return result;
    }
}


