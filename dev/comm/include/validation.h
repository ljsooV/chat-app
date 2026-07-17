#pragma once

#include <string>

#include "packet.h"

static bool is_valid_message_type(uint32_t type_value)
{
    return type_value == static_cast<uint32_t>(MESSAGE_TYPE::NICKNAME)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::CHAT)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::SYSTEM)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::NICKNAME_ACCEPTED)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::NICKNAME_REJECTED)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::SYSTEM_INFO)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::SYSTEM_JOIN)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::SYSTEM_LEAVE)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::SYSTEM_ERROR)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::USER_LIST)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::NICKNAME_CHANGED)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::WHISPER)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::ROOM_LIST)
        || type_value == static_cast<uint32_t>(MESSAGE_TYPE::ROOM_CHANGED);
}

static bool contains_control_characters(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if ((ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch == 127)
        {
            return true;
        }
    }

    return false;
}

static bool is_valid_nickname(const std::string& nickname)
{
    return !nickname.empty()
        && nickname.size() <= MAX_NICKNAME_LENGTH
        && !contains_control_characters(nickname);
}

static bool is_valid_username(const std::string& username)
{
    if (username.empty()
        || username.size() > MAX_USERNAME_LENGTH
        || contains_control_characters(username))
    {
        return false;
    }

    for (size_t i = 0; i < username.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(username[i]);
        const bool is_alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool is_digit = ch >= '0' && ch <= '9';
        if (!is_alpha && !is_digit && ch != '_' && ch != '-')
        {
            return false;
        }
    }

    return true;
}

static bool is_valid_password(const std::string& password)
{
    return !password.empty()
        && password.size() <= MAX_PASSWORD_LENGTH
        && !contains_control_characters(password);
}

static bool is_valid_chat_message(const std::string& message)
{
    return !message.empty()
        && message.size() <= MAX_CHAT_LENGTH
        && !contains_control_characters(message);
}

static bool is_valid_room_name(const std::string& room_name)
{
    return !room_name.empty()
        && room_name.size() <= MAX_ROOM_NAME_LENGTH
        && !contains_control_characters(room_name)
        && room_name.find(' ') == std::string::npos;
}
