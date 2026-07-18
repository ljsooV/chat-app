#pragma once

#include <cstdint>
#include <string_view>

using namespace std;

namespace chat
{
	bool is_valid_message_type(uint32_t type_value);
	bool contains_control_characters(string_view text);
	bool is_valid_nickname(string_view nickname);
	bool is_valid_username(string_view username);
	bool is_valid_password(string_view password);
	bool is_valid_chat_message(string_view message);
	bool is_valid_room_name(string_view room_name);
}
