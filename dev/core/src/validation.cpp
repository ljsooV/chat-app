#include "validation.h"

#include "chat_types.h"

using namespace std;

namespace chat
{
    bool is_valid_message_type(uint32_t type_value)
    {
        return type_value >= static_cast<uint32_t>(MESSAGE_TYPE::NICKNAME)
            && type_value <= static_cast<uint32_t>(MESSAGE_TYPE::ROOM_CHANGED);
    }

    bool contains_control_characters(string_view text)
    {
        for (const unsigned char character : text)
        {
            if ((character < 32 && character != '\n' && character != '\r' && character != '\t') || character == 127)
            {
                return true;
            }
        }

        return false;
    }

    bool is_valid_nickname(string_view nickname)
    {
        if (nickname.empty()
            || nickname.size() > max_nickname_length
            || contains_control_characters(nickname))
        {
            return false;
        }

        return true;
    }

    bool is_valid_username(string_view username)
    {
        if (username.empty() || username.size() > max_username_length || contains_control_characters(username))
        {
            return false;
        }

        for (const unsigned char character : username)
        {
            const bool is_alpha = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
            const bool is_digit = character >= '0' && character <= '9';

            if (!is_alpha && !is_digit && character != '_' && character != '-')
            {
                return false;
            }
        }

        return true;
    }

    bool is_valid_password(string_view password)
    {
        if (password.empty() ||
            password.size() > max_password_length || 
            contains_control_characters(password))
        {
            return false;
        }

        return true;
    }

    bool is_valid_chat_message(string_view message)
    {
        if (message.empty() ||
            message.size() > max_chat_length || 
            contains_control_characters(message))
        {
            return false;
        }

        return true;
    }

    bool is_valid_room_name(string_view room_name)
    {
        if (room_name.empty() || 
            room_name.size() > max_room_name_length || 
            contains_control_characters(room_name)  || 
            string_view::npos != room_name.find(' '))
        {
            return false;
        }

        return true;
    }
}
