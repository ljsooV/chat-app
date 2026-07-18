#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QTcpSocket>

#include "chat_types.h"
#include "packet_codec.h"

using MESSAGE_TYPE = chat::MESSAGE_TYPE;

namespace Ui
{
    class chat_window;
}

class QListWidgetItem;
class QColor;

class chat_window : public QMainWindow
{
public:
    explicit chat_window(QWidget* parent = nullptr);
    ~chat_window();

private:
    void connect_to_server();
    void send_message();
    void read_from_socket();
    void socket_connected();
    void socket_disconnected();
    void socket_error_occurred(QAbstractSocket::SocketError socket_error);
    void join_selected_room(QListWidgetItem* item);
    void create_or_join_room();
    void leave_current_room();
    void refresh_room_data();
    void prepare_whisper(QListWidgetItem* item);
    void select_user_for_admin(QListWidgetItem* item);
    void close_current_room();
    void update_owner_controls();

    void setup_ui();
    void set_connected_ui_state(bool connected);
    void update_status(const QString& text);
    void append_log(const QString& tag, const QString& text, const QColor& color);
    void update_user_list(const QString& payload);
    void update_room_list(const QString& payload);
    void update_current_room(const QString& payload);

    QString current_owner_name() const;
    QString current_room_name() const;

    bool process_buffered_packet();
    void handle_packet(MESSAGE_TYPE type, const QString& payload);
    void send_packet(MESSAGE_TYPE type, const QString& payload);

    Ui::chat_window* m_ui;

    QTcpSocket* m_socket;
    QByteArray m_buffer;
    bool m_handshake_complete;
};
