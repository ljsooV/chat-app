#include "chat_window.h"

#include <QAbstractSocket>
#include <QFont>
#include <QListWidgetItem>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ui_chat_window.h"

using namespace std;

namespace
{
QString without_timestamp(const QString& payload)
{
    if (payload.startsWith('['))
    {
        const int end = payload.indexOf("] ");
        if (end > 0)
        {
            return payload.mid(end + 2);
        }
    }
    return payload;
}

QString room_name_from_list_item(const QString& text)
{
    const int suffix = text.indexOf(" (");
    return suffix > 0 ? text.left(suffix).trimmed() : text.trimmed();
}

QString extract_owner_name(const QString& room_text)
{
    const QString OWNER_PREFIX = "(owner: ";
    const int owner_index = room_text.indexOf(OWNER_PREFIX, 0, Qt::CaseInsensitive);
    if (owner_index < 0)
    {
        return QString();
    }

    const int end_index = room_text.indexOf(')', owner_index);
    if (end_index < 0)
    {
        return QString();
    }

    return room_text.mid(owner_index + OWNER_PREFIX.size(), end_index - owner_index - OWNER_PREFIX.size()).trimmed();
}
}

chat_window::chat_window(QWidget* parent)
    : QMainWindow(parent),
      m_ui(new Ui::chat_window),
      m_socket(new QTcpSocket(this)),
      m_handshake_complete(false)
{
    setup_ui();

    connect(m_ui->connect_button, &QPushButton::clicked, this, &chat_window::connect_to_server);
    connect(m_ui->send_button, &QPushButton::clicked, this, &chat_window::send_message);
    connect(m_ui->message_edit, &QLineEdit::returnPressed, this, &chat_window::send_message);
    connect(m_socket, &QTcpSocket::connected, this, &chat_window::socket_connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &chat_window::socket_disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &chat_window::read_from_socket);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &chat_window::socket_error_occurred);
    connect(m_ui->room_action_button, &QPushButton::clicked, this, &chat_window::create_or_join_room);
    connect(m_ui->room_edit, &QLineEdit::returnPressed, this, &chat_window::create_or_join_room);
    connect(m_ui->refresh_rooms_button, &QPushButton::clicked, this, &chat_window::refresh_room_data);
    connect(m_ui->leave_room_button, &QPushButton::clicked, this, &chat_window::leave_current_room);
    connect(m_ui->room_list, &QListWidget::itemDoubleClicked, this, &chat_window::join_selected_room);
    connect(m_ui->user_list, &QListWidget::itemDoubleClicked, this, &chat_window::prepare_whisper);
    connect(m_ui->user_list, &QListWidget::itemClicked, this, &chat_window::select_user_for_admin);

    connect(m_ui->kick_user_button, &QPushButton::clicked, this, [this]() {
        const QString target = m_ui->selected_user_label->text().trimmed();
        if (!target.isEmpty() && target != "None")
        {
            send_packet(MESSAGE_TYPE::CHAT, "/kick " + target);
        }
    });

    connect(m_ui->close_room_button, &QPushButton::clicked, this, &chat_window::close_current_room);

    set_connected_ui_state(false);
}

chat_window::~chat_window()
{
    if (m_socket != nullptr)
    {
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
        {
            m_socket->abort();
        }
        m_socket->setParent(nullptr);

        delete m_socket;
        m_socket = nullptr;
    }

    delete m_ui;
}

void chat_window::setup_ui()
{
    m_ui->setupUi(this);
    resize(1280, 800);
    setMinimumSize(1020, 680);

    centralWidget()->setStyleSheet(
        "QWidget#central_widget { background:#f3f5f6; color:#24343b; font-size:14px; }"
        "QFrame#connection_card_frame, QFrame#workspace_frame { background:#ffffff; border:1px solid #d8e0e3; border-radius:12px; }"
        "QFrame#landing_frame { background:#243b44; border-radius:14px; }"
        "QLabel { background:transparent; border:none; }"
        "QFrame#landing_frame QLabel { background:transparent; border:none; }"
        "QLineEdit, QListWidget, QTextEdit { background:#ffffff; border:1px solid #cbd5d9; border-radius:8px; padding:8px; }"
        "QLineEdit:focus, QListWidget:focus, QTextEdit:focus { border:1px solid #237985; }"
        "QPushButton { background:#237985; color:white; border:0; border-radius:8px; padding:9px 14px; font-weight:600; }"
        "QPushButton:hover { background:#1b6872; }"
        "QPushButton:disabled { background:#aab7bb; }"
        "QTabWidget::pane { border:1px solid #d8e0e3; border-radius:8px; background:#ffffff; }"
        "QTabBar::tab { background:#e7edef; padding:8px 14px; margin-right:3px; }"
        "QTabBar::tab:selected { background:#ffffff; font-weight:700; }");

    m_ui->connection_card_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_ui->connection_card_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ui->connection_card_frame->setFixedHeight(112);

    m_ui->landing_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_ui->landing_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_ui->landing_title_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ui->landing_text_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ui->landing_layout->setSpacing(12);
    m_ui->landing_layout->insertStretch(0, 1);
    m_ui->landing_layout->addStretch(1);
    m_ui->connection_layout->setColumnStretch(1, 1);
    m_ui->connection_layout->setColumnStretch(3, 1);
    m_ui->connection_layout->setColumnStretch(5, 1);

    m_ui->connection_title_label->setStyleSheet("font-size:18px; font-weight:700;");
    m_ui->landing_title_label->setStyleSheet("color:#ffffff; font-size:26px; font-weight:700;");
    m_ui->landing_text_label->setStyleSheet("color:#c8d7da; font-size:14px;");
    m_ui->workspace_title_label->setStyleSheet("font-size:20px; font-weight:700;");
    m_ui->current_room_label->setStyleSheet("background:#e7f1f2; color:#17636d; border-radius:8px; padding:6px 10px; font-weight:700;");
    m_ui->owner_state_label->setStyleSheet("color:#66767c;");
    m_ui->selected_user_label->setStyleSheet("font-weight:700;");

    m_ui->log_view->setReadOnly(true);
    m_ui->log_view->document()->setMaximumBlockCount(500);
    QFont log_font("Consolas");
    log_font.setPointSize(10);
    m_ui->log_view->setFont(log_font);
}

void chat_window::set_connected_ui_state(bool connected)
{
    m_handshake_complete = connected;
    m_ui->landing_frame->setVisible(!connected);
    m_ui->workspace_frame->setVisible(connected);
    m_ui->host_edit->setEnabled(!connected);
    m_ui->port_edit->setEnabled(!connected);
    m_ui->nickname_edit->setEnabled(!connected);
    m_ui->connect_button->setText(connected ? "Disconnect" : "Connect");

    update_status(connected ? "Connected" : "Disconnected");

    if (!connected)
    {
        m_ui->room_list->clear();
        m_ui->user_list->clear();
        m_ui->current_room_label->setText("Lobby");
        m_ui->owner_state_label->setText("No owner");
        m_ui->selected_user_label->setText("None");
    }

    update_owner_controls();
}

void chat_window::update_status(const QString& text)
{
    m_ui->status_label->setText(text);
    m_ui->status_label->setStyleSheet(text == "Connected" ? "color:#24704a; font-weight:700;" : "color:#a23b32; font-weight:700;");
}

void chat_window::connect_to_server()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
    {
        m_socket->disconnectFromHost();

        return;
    }

    const QString nickname = m_ui->nickname_edit->text().trimmed();
    const QString host = m_ui->host_edit->text().trimmed();
    bool port_ok = false;
    const quint16 port = m_ui->port_edit->text().toUShort(&port_ok);

    if (nickname.isEmpty() || host.isEmpty() || !port_ok || port == 0)
    {
        update_status("Check connection input");
        statusBar()->showMessage("Host, port, and nickname are required.", 4000);

        return;
    }

    m_buffer.clear();

    m_ui->connect_button->setEnabled(false);

    update_status("Connecting...");

    m_socket->connectToHost(host, port);
}

void chat_window::socket_connected()
{
    m_ui->connect_button->setEnabled(true);
    send_packet(MESSAGE_TYPE::NICKNAME, m_ui->nickname_edit->text().trimmed());
}

void chat_window::socket_disconnected()
{
    set_connected_ui_state(false);
    statusBar()->showMessage("Disconnected");
}

void chat_window::socket_error_occurred(QAbstractSocket::SocketError)
{
    append_log("ERROR", m_socket->errorString(), QColor("#b33a32"));
    m_ui->connect_button->setEnabled(true);

    if (!m_handshake_complete)
    {
        update_status("Connection failed");
    }

    statusBar()->showMessage(m_socket->errorString(), 5000);
}

void chat_window::send_message()
{
    const QString text = m_ui->message_edit->text().trimmed();

    if (!m_handshake_complete || text.isEmpty())
    {
        return;
    }

    send_packet(MESSAGE_TYPE::CHAT, text);
    m_ui->message_edit->clear();
}

void chat_window::join_selected_room(QListWidgetItem* item)
{
    if (item != nullptr)
    {
        send_packet(MESSAGE_TYPE::CHAT, "/join " + room_name_from_list_item(item->text()));
    }
}

void chat_window::create_or_join_room()
{
    const QString room = m_ui->room_edit->text().trimmed();
    if (!m_handshake_complete || room.isEmpty())
    {
        return;
    }

    bool exists = false;
    for (int i = 0; i < m_ui->room_list->count(); ++i)
    {
        if (room_name_from_list_item(m_ui->room_list->item(i)->text()) == room)
        {
            exists = true;
            break;
        }
    }

    send_packet(MESSAGE_TYPE::CHAT, (exists ? "/join " : "/create ") + room);
    m_ui->room_edit->clear();
}

void chat_window::leave_current_room()
{
    if (m_handshake_complete)
    {
        send_packet(MESSAGE_TYPE::CHAT, "/leave");
    }
}

void chat_window::refresh_room_data()
{
    if (!m_handshake_complete)
    {
        return;
    }

    send_packet(MESSAGE_TYPE::CHAT, "/rooms");
    send_packet(MESSAGE_TYPE::CHAT, "/list");
}

void chat_window::prepare_whisper(QListWidgetItem* item)
{
    if (item != nullptr)
    {
        m_ui->message_edit->setText("/w " + item->text().trimmed() + " ");
        m_ui->message_edit->setFocus();
        m_ui->message_edit->setCursorPosition(m_ui->message_edit->text().size());
    }
}

void chat_window::select_user_for_admin(QListWidgetItem* item)
{
    m_ui->selected_user_label->setText(item == nullptr ? "None" : item->text().trimmed());

    update_owner_controls();
}

void chat_window::close_current_room()
{
    if (m_handshake_complete)
    {
        send_packet(MESSAGE_TYPE::CHAT, "/close");
    }
}

void chat_window::update_owner_controls()
{
    const bool custom_room = m_handshake_complete && current_room_name().compare("Lobby", Qt::CaseInsensitive) != 0;
    const bool owner = custom_room && current_owner_name() == m_ui->nickname_edit->text().trimmed();
    const QString selected = m_ui->selected_user_label->text().trimmed();
    const bool valid_target = !selected.isEmpty() && selected != "None" && selected != m_ui->nickname_edit->text().trimmed();

    m_ui->kick_user_button->setEnabled(owner && valid_target);
    m_ui->close_room_button->setEnabled(owner);
    m_ui->leave_room_button->setEnabled(custom_room);
    m_ui->sidebar_tabs->setTabEnabled(2, custom_room);
}

void chat_window::append_log(const QString& tag, const QString& text, const QColor& color)
{
    QTextCursor cursor(m_ui->log_view->document());
    cursor.movePosition(QTextCursor::End);

    if (!m_ui->log_view->document()->isEmpty())
    {
        cursor.insertBlock();
    }

    QTextBlockFormat block;
    block.setBackground(QColor("#f5f7f7"));
    block.setTopMargin(4);
    block.setBottomMargin(4);
    block.setLeftMargin(8);
    cursor.setBlockFormat(block);

    QTextCharFormat tag_format;
    tag_format.setForeground(color);
    tag_format.setFontWeight(QFont::Bold);
    cursor.insertText("[" + tag + "] ", tag_format);

    QTextCharFormat text_format;
    text_format.setForeground(QColor("#2e3d43"));
    cursor.insertText(text, text_format);
    m_ui->log_view->setTextCursor(cursor);
    m_ui->log_view->ensureCursorVisible();
}

void chat_window::update_user_list(const QString& payload)
{
    QString cleaned = without_timestamp(payload);
    const QString PREFIX = "room [";
    const int users_index = cleaned.indexOf("] users: ", 0, Qt::CaseInsensitive);
    if (cleaned.startsWith(PREFIX, Qt::CaseInsensitive) && users_index > 0)
    {
        cleaned = cleaned.mid(users_index + 9);
    }

    m_ui->user_list->clear();
    const QStringList users = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& user : users)
    {
        if (user != "(none)")
        {
            m_ui->user_list->addItem(user);
        }
    }

    update_owner_controls();
}

void chat_window::update_room_list(const QString& payload)
{
    QString cleaned = without_timestamp(payload);
    const QString PREFIX = "rooms: ";

    if (cleaned.startsWith(PREFIX, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(PREFIX.size());
    }

    m_ui->room_list->clear();
    const QStringList rooms = cleaned.split(", ", Qt::SkipEmptyParts);

    for (const QString& room : rooms)
    {
        m_ui->room_list->addItem(room);
    }
}

void chat_window::update_current_room(const QString& payload)
{
    QString cleaned = without_timestamp(payload);
    const QString PREFIX = "current room: ";

    if (cleaned.startsWith(PREFIX, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(PREFIX.size());
    }

    m_ui->current_room_label->setText(cleaned);
    m_ui->owner_state_label->setText(current_owner_name().isEmpty() ? "No owner" : "Owner: " + current_owner_name());
    m_ui->selected_user_label->setText("None");
    update_owner_controls();
}

QString chat_window::current_owner_name() const
{
    return extract_owner_name(m_ui->current_room_label->text());
}

QString chat_window::current_room_name() const
{
    return room_name_from_list_item(m_ui->current_room_label->text());
}

void chat_window::send_packet(MESSAGE_TYPE type, const QString& payload)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
    {
        const QByteArray utf8 = payload.toUtf8();
        const vector<uint8_t> packet = chat::make_packet(type, string_view(utf8.constData(), static_cast<size_t>(utf8.size())));

        m_socket->write(reinterpret_cast<const char*>(packet.data()), static_cast<qint64>(packet.size()));
    }
}

void chat_window::read_from_socket()
{
    m_buffer.append(m_socket->readAll());

    while (process_buffered_packet())
    {
    }
}

bool chat_window::process_buffered_packet()
{
    const chat::parse_result result = chat::try_parse_packet(reinterpret_cast<const uint8_t*>(m_buffer.constData()), static_cast<size_t>(m_buffer.size()));

    if (result.m_status == chat::PARSE_STATUS::NEED_MORE_DATA)
    {
        return false;
    }

    if (result.m_status == chat::PARSE_STATUS::INVALID_PACKET)
    {
        append_log("ERROR", "Invalid packet received. Connection closed.", QColor("#b33a32"));
        m_buffer.clear();
        m_socket->disconnectFromHost();
        return false;
    }

    m_buffer.remove(0, static_cast<int>(result.m_consumed_size));
    handle_packet(result.m_type, QString::fromUtf8(result.m_payload.data(), static_cast<int>(result.m_payload.size())));

    return true;
}

void chat_window::handle_packet(MESSAGE_TYPE type, const QString& payload)
{
    switch (type)
    {
        case MESSAGE_TYPE::NICKNAME_ACCEPTED:
            set_connected_ui_state(true);
            append_log("CONNECTED", payload, QColor("#24704a"));
            send_packet(MESSAGE_TYPE::CHAT, "/rooms");
            send_packet(MESSAGE_TYPE::CHAT, "/list");
            m_ui->message_edit->setFocus();

            break;

        case MESSAGE_TYPE::NICKNAME_REJECTED:
            update_status(without_timestamp(payload));
            statusBar()->showMessage(payload, 5000);
            m_socket->disconnectFromHost();

            break;

        case MESSAGE_TYPE::CHAT:
            append_log("CHAT", payload, QColor("#237985"));

            break;

        case MESSAGE_TYPE::SYSTEM:
        case MESSAGE_TYPE::SYSTEM_INFO:
            append_log("SYSTEM", payload, QColor("#66767c"));

            break;

        case MESSAGE_TYPE::SYSTEM_JOIN:
            append_log("JOIN", payload, QColor("#24704a"));

            break;

        case MESSAGE_TYPE::SYSTEM_LEAVE:
            append_log("LEAVE", payload, QColor("#a05a32"));

            break;

        case MESSAGE_TYPE::SYSTEM_ERROR:
            append_log("ERROR", payload, QColor("#b33a32"));

            break;

        case MESSAGE_TYPE::USER_LIST:
            update_user_list(payload);

            break;

        case MESSAGE_TYPE::ROOM_LIST:
            update_room_list(payload);

            break;

        case MESSAGE_TYPE::ROOM_CHANGED:
            update_current_room(payload);

            break;

        case MESSAGE_TYPE::WHISPER:
            append_log("WHISPER", payload, QColor("#76529b"));

            break;

        case MESSAGE_TYPE::NICKNAME_CHANGED:
            append_log("NAME", payload, QColor("#237985"));

            break;

        default:
            break;
    }
}
