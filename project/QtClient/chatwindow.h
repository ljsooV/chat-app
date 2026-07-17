#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QTcpSocket>

#include "protocol.h"

namespace Ui
{
class ChatWindow;
}

class QListWidgetItem;
class QColor;

class ChatWindow : public QMainWindow
{
public:
    explicit ChatWindow(QWidget* parent = nullptr);
    ~ChatWindow();

private:
    void connectToServer();
    void sendMessage();
    void readFromSocket();
    void socketConnected();
    void socketDisconnected();
    void socketErrorOccurred(QAbstractSocket::SocketError socketError);
    void joinSelectedRoom(QListWidgetItem* item);
    void createOrJoinRoom();
    void leaveCurrentRoom();
    void refreshRoomData();
    void prepareWhisper(QListWidgetItem* item);
    void selectUserForAdmin(QListWidgetItem* item);
    void closeCurrentRoom();
    void updateOwnerControls();

    void setupUi();
    void setConnectedUiState(bool connected);
    void updateStatus(const QString& text);
    void appendLog(const QString& tag, const QString& text, const QColor& color);
    void updateUserList(const QString& payload);
    void updateRoomList(const QString& payload);
    void updateCurrentRoom(const QString& payload);
    QString currentOwnerName() const;
    QString currentRoomName() const;
    bool processBufferedPacket();
    void handlePacket(MessageType type, const QString& payload);
    void sendPacket(MessageType type, const QString& payload);

    Ui::ChatWindow* m_ui;
    QTcpSocket* m_socket;
    QByteArray m_buffer;
    bool m_handshakeComplete;
};
