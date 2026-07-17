#include "chatwindow.h"

#include <QAbstractSocket>
#include <QFont>
#include <QListWidgetItem>
#include <QStatusBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QtEndian>

#include <cstring>

#include "ui_chatwindow.h"

namespace
{
constexpr int kHeaderSize = static_cast<int>(sizeof(PacketHeader));

QString withoutTimestamp(const QString& payload)
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

QString roomNameFromListItem(const QString& text)
{
    const int suffix = text.indexOf(" (");
    return suffix > 0 ? text.left(suffix).trimmed() : text.trimmed();
}

QString extractOwnerName(const QString& roomText)
{
    const QString ownerPrefix = "(owner: ";
    const int ownerIndex = roomText.indexOf(ownerPrefix, 0, Qt::CaseInsensitive);
    if (ownerIndex < 0)
    {
        return QString();
    }

    const int endIndex = roomText.indexOf(')', ownerIndex);
    if (endIndex < 0)
    {
        return QString();
    }

    return roomText.mid(ownerIndex + ownerPrefix.size(), endIndex - ownerIndex - ownerPrefix.size()).trimmed();
}
}

ChatWindow::ChatWindow(QWidget* parent)
    : QMainWindow(parent),
      m_ui(new Ui::ChatWindow),
      m_socket(new QTcpSocket(this)),
      m_handshakeComplete(false)
{
    setupUi();

    connect(m_ui->connectButton, &QPushButton::clicked, this, &ChatWindow::connectToServer);
    connect(m_ui->sendButton, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    connect(m_ui->messageEdit, &QLineEdit::returnPressed, this, &ChatWindow::sendMessage);
    connect(m_socket, &QTcpSocket::connected, this, &ChatWindow::socketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ChatWindow::socketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ChatWindow::readFromSocket);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ChatWindow::socketErrorOccurred);
    connect(m_ui->roomActionButton, &QPushButton::clicked, this, &ChatWindow::createOrJoinRoom);
    connect(m_ui->roomEdit, &QLineEdit::returnPressed, this, &ChatWindow::createOrJoinRoom);
    connect(m_ui->refreshRoomsButton, &QPushButton::clicked, this, &ChatWindow::refreshRoomData);
    connect(m_ui->leaveRoomButton, &QPushButton::clicked, this, &ChatWindow::leaveCurrentRoom);
    connect(m_ui->roomList, &QListWidget::itemDoubleClicked, this, &ChatWindow::joinSelectedRoom);
    connect(m_ui->userList, &QListWidget::itemDoubleClicked, this, &ChatWindow::prepareWhisper);
    connect(m_ui->userList, &QListWidget::itemClicked, this, &ChatWindow::selectUserForAdmin);
    connect(m_ui->kickUserButton, &QPushButton::clicked, this, [this]() {
        const QString target = m_ui->selectedUserLabel->text().trimmed();
        if (!target.isEmpty() && target != "None")
        {
            sendPacket(MessageType::Chat, "/kick " + target);
        }
    });
    connect(m_ui->closeRoomButton, &QPushButton::clicked, this, &ChatWindow::closeCurrentRoom);

    setConnectedUiState(false);
}

ChatWindow::~ChatWindow()
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

void ChatWindow::setupUi()
{
    m_ui->setupUi(this);
    resize(1280, 800);
    setMinimumSize(1020, 680);

    centralWidget()->setStyleSheet(
        "QWidget { background:#f3f5f6; color:#24343b; font-size:14px; }"
        "QFrame#connectionCardFrame, QFrame#workspaceFrame { background:#ffffff; border:1px solid #d8e0e3; border-radius:12px; }"
        "QFrame#landingFrame { background:#243b44; border-radius:14px; }"
        "QLineEdit, QListWidget, QTextEdit { background:#ffffff; border:1px solid #cbd5d9; border-radius:8px; padding:8px; }"
        "QLineEdit:focus, QListWidget:focus, QTextEdit:focus { border:1px solid #237985; }"
        "QPushButton { background:#237985; color:white; border:0; border-radius:8px; padding:9px 14px; font-weight:600; }"
        "QPushButton:hover { background:#1b6872; }"
        "QPushButton:disabled { background:#aab7bb; }"
        "QTabWidget::pane { border:1px solid #d8e0e3; border-radius:8px; background:#ffffff; }"
        "QTabBar::tab { background:#e7edef; padding:8px 14px; margin-right:3px; }"
        "QTabBar::tab:selected { background:#ffffff; font-weight:700; }");

    m_ui->connectionTitleLabel->setStyleSheet("font-size:18px; font-weight:700;");
    m_ui->workspaceTitleLabel->setStyleSheet("font-size:20px; font-weight:700;");
    m_ui->currentRoomLabel->setStyleSheet("background:#e7f1f2; color:#17636d; border-radius:8px; padding:6px 10px; font-weight:700;");
    m_ui->ownerStateLabel->setStyleSheet("color:#66767c;");
    m_ui->selectedUserLabel->setStyleSheet("font-weight:700;");

    m_ui->logView->setReadOnly(true);
    m_ui->logView->document()->setMaximumBlockCount(500);
    QFont logFont("Consolas");
    logFont.setPointSize(10);
    m_ui->logView->setFont(logFont);
}

void ChatWindow::setConnectedUiState(bool connected)
{
    m_handshakeComplete = connected;
    m_ui->landingFrame->setVisible(!connected);
    m_ui->workspaceFrame->setVisible(connected);
    m_ui->hostEdit->setEnabled(!connected);
    m_ui->portEdit->setEnabled(!connected);
    m_ui->nicknameEdit->setEnabled(!connected);
    m_ui->connectButton->setText(connected ? "Disconnect" : "Connect");
    updateStatus(connected ? "Connected" : "Disconnected");

    if (!connected)
    {
        m_ui->roomList->clear();
        m_ui->userList->clear();
        m_ui->currentRoomLabel->setText("Lobby");
        m_ui->ownerStateLabel->setText("No owner");
        m_ui->selectedUserLabel->setText("None");
    }

    updateOwnerControls();
}

void ChatWindow::updateStatus(const QString& text)
{
    m_ui->statusLabel->setText(text);
    m_ui->statusLabel->setStyleSheet(text == "Connected"
        ? "color:#24704a; font-weight:700;"
        : "color:#a23b32; font-weight:700;");
}

void ChatWindow::connectToServer()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
    {
        m_socket->disconnectFromHost();
        return;
    }

    const QString nickname = m_ui->nicknameEdit->text().trimmed();
    const QString host = m_ui->hostEdit->text().trimmed();
    bool portOk = false;
    const quint16 port = m_ui->portEdit->text().toUShort(&portOk);
    if (nickname.isEmpty() || host.isEmpty() || !portOk || port == 0)
    {
        updateStatus("Check connection input");
        statusBar()->showMessage("Host, port, and nickname are required.", 4000);
        return;
    }

    m_buffer.clear();
    m_ui->connectButton->setEnabled(false);
    updateStatus("Connecting...");
    m_socket->connectToHost(host, port);
}

void ChatWindow::socketConnected()
{
    m_ui->connectButton->setEnabled(true);
    sendPacket(MessageType::Nickname, m_ui->nicknameEdit->text().trimmed());
}

void ChatWindow::socketDisconnected()
{
    setConnectedUiState(false);
    statusBar()->showMessage("Disconnected");
}

void ChatWindow::socketErrorOccurred(QAbstractSocket::SocketError)
{
    appendLog("ERROR", m_socket->errorString(), QColor("#b33a32"));
    m_ui->connectButton->setEnabled(true);
    if (!m_handshakeComplete)
    {
        updateStatus("Connection failed");
    }
    statusBar()->showMessage(m_socket->errorString(), 5000);
}

void ChatWindow::sendMessage()
{
    const QString text = m_ui->messageEdit->text().trimmed();
    if (!m_handshakeComplete || text.isEmpty())
    {
        return;
    }

    sendPacket(MessageType::Chat, text);
    m_ui->messageEdit->clear();
}

void ChatWindow::joinSelectedRoom(QListWidgetItem* item)
{
    if (item != nullptr)
    {
        sendPacket(MessageType::Chat, "/join " + roomNameFromListItem(item->text()));
    }
}

void ChatWindow::createOrJoinRoom()
{
    const QString room = m_ui->roomEdit->text().trimmed();
    if (!m_handshakeComplete || room.isEmpty())
    {
        return;
    }

    bool exists = false;
    for (int i = 0; i < m_ui->roomList->count(); ++i)
    {
        if (roomNameFromListItem(m_ui->roomList->item(i)->text()) == room)
        {
            exists = true;
            break;
        }
    }

    sendPacket(MessageType::Chat, (exists ? "/join " : "/create ") + room);
    m_ui->roomEdit->clear();
}

void ChatWindow::leaveCurrentRoom()
{
    if (m_handshakeComplete)
    {
        sendPacket(MessageType::Chat, "/leave");
    }
}

void ChatWindow::refreshRoomData()
{
    if (!m_handshakeComplete)
    {
        return;
    }

    sendPacket(MessageType::Chat, "/rooms");
    sendPacket(MessageType::Chat, "/list");
}

void ChatWindow::prepareWhisper(QListWidgetItem* item)
{
    if (item != nullptr)
    {
        m_ui->messageEdit->setText("/w " + item->text().trimmed() + " ");
        m_ui->messageEdit->setFocus();
        m_ui->messageEdit->setCursorPosition(m_ui->messageEdit->text().size());
    }
}

void ChatWindow::selectUserForAdmin(QListWidgetItem* item)
{
    m_ui->selectedUserLabel->setText(item == nullptr ? "None" : item->text().trimmed());
    updateOwnerControls();
}

void ChatWindow::closeCurrentRoom()
{
    if (m_handshakeComplete)
    {
        sendPacket(MessageType::Chat, "/close");
    }
}

void ChatWindow::updateOwnerControls()
{
    const bool customRoom = m_handshakeComplete && currentRoomName().compare("Lobby", Qt::CaseInsensitive) != 0;
    const bool owner = customRoom && currentOwnerName() == m_ui->nicknameEdit->text().trimmed();
    const QString selected = m_ui->selectedUserLabel->text().trimmed();
    const bool validTarget = !selected.isEmpty() && selected != "None" && selected != m_ui->nicknameEdit->text().trimmed();

    m_ui->kickUserButton->setEnabled(owner && validTarget);
    m_ui->closeRoomButton->setEnabled(owner);
    m_ui->leaveRoomButton->setEnabled(customRoom);
    m_ui->sidebarTabs->setTabEnabled(2, customRoom);
}

void ChatWindow::appendLog(const QString& tag, const QString& text, const QColor& color)
{
    QTextCursor cursor(m_ui->logView->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_ui->logView->document()->isEmpty())
    {
        cursor.insertBlock();
    }

    QTextBlockFormat block;
    block.setBackground(QColor("#f5f7f7"));
    block.setTopMargin(4);
    block.setBottomMargin(4);
    block.setLeftMargin(8);
    cursor.setBlockFormat(block);

    QTextCharFormat tagFormat;
    tagFormat.setForeground(color);
    tagFormat.setFontWeight(QFont::Bold);
    cursor.insertText("[" + tag + "] ", tagFormat);

    QTextCharFormat textFormat;
    textFormat.setForeground(QColor("#2e3d43"));
    cursor.insertText(text, textFormat);
    m_ui->logView->setTextCursor(cursor);
    m_ui->logView->ensureCursorVisible();
}

void ChatWindow::updateUserList(const QString& payload)
{
    QString cleaned = withoutTimestamp(payload);
    const QString prefix = "room [";
    const int usersIndex = cleaned.indexOf("] users: ", 0, Qt::CaseInsensitive);
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive) && usersIndex > 0)
    {
        cleaned = cleaned.mid(usersIndex + 9);
    }

    m_ui->userList->clear();
    const QStringList users = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& user : users)
    {
        if (user != "(none)")
        {
            m_ui->userList->addItem(user);
        }
    }

    updateOwnerControls();
}

void ChatWindow::updateRoomList(const QString& payload)
{
    QString cleaned = withoutTimestamp(payload);
    const QString prefix = "rooms: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
    }

    m_ui->roomList->clear();
    const QStringList rooms = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& room : rooms)
    {
        m_ui->roomList->addItem(room);
    }
}

void ChatWindow::updateCurrentRoom(const QString& payload)
{
    QString cleaned = withoutTimestamp(payload);
    const QString prefix = "current room: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
    }

    m_ui->currentRoomLabel->setText(cleaned);
    m_ui->ownerStateLabel->setText(currentOwnerName().isEmpty() ? "No owner" : "Owner: " + currentOwnerName());
    m_ui->selectedUserLabel->setText("None");
    updateOwnerControls();
}

QString ChatWindow::currentOwnerName() const
{
    return extractOwnerName(m_ui->currentRoomLabel->text());
}

QString ChatWindow::currentRoomName() const
{
    return roomNameFromListItem(m_ui->currentRoomLabel->text());
}

void ChatWindow::sendPacket(MessageType type, const QString& payload)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
    {
        m_socket->write(buildPacket(type, payload));
    }
}

void ChatWindow::readFromSocket()
{
    m_buffer.append(m_socket->readAll());
    while (processBufferedPacket())
    {
    }
}

bool ChatWindow::processBufferedPacket()
{
    if (m_buffer.size() < kHeaderSize)
    {
        return false;
    }

    PacketHeader rawHeader;
    memcpy(&rawHeader, m_buffer.constData(), sizeof(rawHeader));

    const uint32_t typeValue = qFromBigEndian(rawHeader.type);
    const uint32_t payloadSize = qFromBigEndian(rawHeader.size);
    if (m_buffer.size() < kHeaderSize + static_cast<int>(payloadSize))
    {
        return false;
    }

    const QByteArray payloadBytes = m_buffer.mid(kHeaderSize, static_cast<int>(payloadSize));
    m_buffer.remove(0, kHeaderSize + static_cast<int>(payloadSize));
    handlePacket(static_cast<MessageType>(typeValue), QString::fromUtf8(payloadBytes));
    return true;
}

void ChatWindow::handlePacket(MessageType type, const QString& payload)
{
    switch (type)
    {
    case MessageType::NicknameAccepted:
        setConnectedUiState(true);
        appendLog("CONNECTED", payload, QColor("#24704a"));
        sendPacket(MessageType::Chat, "/rooms");
        sendPacket(MessageType::Chat, "/list");
        m_ui->messageEdit->setFocus();
        break;
    case MessageType::NicknameRejected:
        updateStatus(withoutTimestamp(payload));
        statusBar()->showMessage(payload, 5000);
        m_socket->disconnectFromHost();
        break;
    case MessageType::Chat:
        appendLog("CHAT", payload, QColor("#237985"));
        break;
    case MessageType::System:
    case MessageType::SystemInfo:
        appendLog("SYSTEM", payload, QColor("#66767c"));
        break;
    case MessageType::SystemJoin:
        appendLog("JOIN", payload, QColor("#24704a"));
        break;
    case MessageType::SystemLeave:
        appendLog("LEAVE", payload, QColor("#a05a32"));
        break;
    case MessageType::SystemError:
        appendLog("ERROR", payload, QColor("#b33a32"));
        break;
    case MessageType::UserList:
        updateUserList(payload);
        break;
    case MessageType::RoomList:
        updateRoomList(payload);
        break;
    case MessageType::RoomChanged:
        updateCurrentRoom(payload);
        break;
    case MessageType::Whisper:
        appendLog("WHISPER", payload, QColor("#76529b"));
        break;
    case MessageType::NicknameChanged:
        appendLog("NAME", payload, QColor("#237985"));
        break;
    default:
        break;
    }
}
