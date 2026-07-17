#pragma once

#include <QtCore/QtEndian>
#include <cstdint>

enum class MessageType : uint32_t
{
    Nickname = 1,
    Chat = 2,
    System = 3,
    NicknameAccepted = 4,
    NicknameRejected = 5,
    SystemInfo = 6,
    SystemJoin = 7,
    SystemLeave = 8,
    SystemError = 9,
    UserList = 10,
    NicknameChanged = 11,
    Whisper = 12,
    RoomList = 13,
    RoomChanged = 14,
};

struct PacketHeader
{
    uint32_t type = 0;
    uint32_t size = 0;
};

inline QByteArray buildPacket(MessageType type, const QString& payload)
{
    const QByteArray utf8 = payload.toUtf8();
    PacketHeader header;
    header.type = qToBigEndian(static_cast<uint32_t>(type));
    header.size = qToBigEndian(static_cast<uint32_t>(utf8.size()));

    QByteArray packet;
    packet.append(reinterpret_cast<const char*>(&header), static_cast<int>(sizeof(header)));
    packet.append(utf8);
    return packet;
}
