#include "singleinstanceguard.h"

#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDataStream>
#include <QByteArray>

#include <utils/utils.h>

using namespace vnotex;

const QString SingleInstanceGuard::c_serverName = "vnote";

const QChar SingleInstanceGuard::c_stringListSeparator = '>';

SingleInstanceGuard::~SingleInstanceGuard()
{
    exit();
}

bool SingleInstanceGuard::tryRun()
{
    Q_ASSERT(!m_online);

    // On Windows, multiple servers on the same name are allowed.
    m_client = tryConnect();
    if (m_client) {
        // There is one server running and we are now connected, so we could not continue.
        return false;
    }

    m_server = tryListen();
    if (m_server) {
        // We are the lucky one.
        qInfo() << "guard succeeds to run";
    } else {
        // We still allow the guard to run. There maybe a bug need to fix.
        qWarning() << "failed to connect to an existing instance or establish a new local server";
    }

    setupServer();

    m_online = true;
    return true;
}

void SingleInstanceGuard::requestOpenFiles(const QStringList &p_files)
{
    if (p_files.isEmpty()) {
        return;
    }

    Q_ASSERT(!m_online);
    if (!m_client || m_client->state() != QLocalSocket::ConnectedState) {
        qWarning() << "failed to request open files" << m_client->errorString();
        return ;
    }

    sendRequest(m_client.data(), OpCode::OpenFiles, p_files.join(c_stringListSeparator));
}

void SingleInstanceGuard::requestShow()
{
    Q_ASSERT(!m_online);
    if (!m_client || m_client->state() != QLocalSocket::ConnectedState) {
        qWarning() << "failed to request show" << m_client->errorString();
        return ;
    }

    sendRequest(m_client.data(), OpCode::Show, QString());
}

void SingleInstanceGuard::exit()
{
    m_online = false;

    if (m_client) {
        m_client->disconnectFromServer();
        m_client.clear();
    }

    if (m_server) {
        m_server->close();
        m_server.clear();
    }
}

QSharedPointer<QLocalSocket> SingleInstanceGuard::tryConnect()
{
    auto socket = QSharedPointer<QLocalSocket>::create();
    socket->connectToServer(c_serverName);
    if (socket->waitForConnected(200)) {
        // Connected.
        qDebug() << "socket connected to server" << c_serverName;
        return socket;
    } else {
        qDebug() << "socket connect timeout";
        return nullptr;
    }
}

QSharedPointer<QLocalServer> SingleInstanceGuard::tryListen()
{
    auto server = QSharedPointer<QLocalServer>::create();
    bool ret = server->listen(c_serverName);
    if (!ret && server->serverError() == QAbstractSocket::AddressInUseError) {
        // On Unix, a previous crash may leave a server running.
        // Clean up and try again.
        QLocalServer::removeServer(c_serverName);
        ret = server->listen(c_serverName);
    }

    if (ret) {
        qDebug() << "local server listening on" << c_serverName;
        return server;
    } else {
        qDebug() << "failed to start local server";
        return nullptr;
    }
}

void SingleInstanceGuard::setupServer()
{
    if (!m_server) {
        return;
    }

    connect(m_server.data(), &QLocalServer::newConnection,
            this, [this]() {
                auto socket = m_server->nextPendingConnection();
                if (socket) {
                    qInfo() << "local server receives new connect" << socket;
                    if (m_ongoingConnect) {
                        qWarning() << "drop the connection since there is one ongoing connect";
                        socket->disconnectFromServer();
                        socket->deleteLater();
                        return;
                    }

                    m_ongoingConnect = true;
                    m_command.clear();

                    connect(socket, &QLocalSocket::disconnected,
                            this, [this, socket]() {
                                Q_ASSERT(m_ongoingConnect);
                                socket->deleteLater();
                                m_ongoingConnect = false;
                            });
                    connect(socket, &QLocalSocket::readyRead,
                            this, [this, socket]() {
                                receiveCommand(socket);
                            });
                }
            });
}

void SingleInstanceGuard::receiveCommand(QLocalSocket *p_socket)
{
    QDataStream inStream;
    inStream.setDevice(p_socket);
    inStream.setVersion(QDataStream::Qt_5_12);

    while (p_socket->bytesAvailable() > 0) {
        if (m_command.m_opCode == OpCode::Null) {
            // Relies on the fact that QDataStream serializes a quint32 into
            // sizeof(quint32) bytes.
            if (p_socket->bytesAvailable() < (int)sizeof(quint32) * 2) {
                return;
            }

            quint32 opCode = 0;
            inStream >> opCode;
            m_command.m_opCode = static_cast<OpCode>(opCode);
            inStream >> m_command.m_size;
        }

        if (p_socket->bytesAvailable() < m_command.m_size) {
            return;
        }

        qDebug() << "op code" << m_command.m_opCode << m_command.m_size << p_socket->bytesAvailable();

        switch (m_command.m_opCode) {
        case OpCode::Show:
            Q_ASSERT(m_command.m_size == 0);
            emit showRequested();
            break;

        case OpCode::OpenFiles:
        {
            Q_ASSERT(m_command.m_size != 0);
            QString payload;
            inStream >> payload;
            const auto files = payload.split(c_stringListSeparator);
            emit openFilesRequested(files);
            break;
        }

        default:
            qWarning() << "unknown op code" << m_command.m_opCode;
            m_command.clear();
            return;
        }

        m_command.clear();
    }
}

void SingleInstanceGuard::sendRequest(QLocalSocket *p_socket, OpCode p_code, const QString &p_payload)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_12);
    out << static_cast<quint32>(p_code);
    out << static_cast<quint32>(p_payload.size());
    if (p_payload.size() > 0) {
        out << p_payload;
    }
    p_socket->write(block);
    if (p_socket->waitForBytesWritten(3000)) {
        qDebug() << "request sent" << p_code << p_payload.size();
    } else {
        qWarning() << "failed to send request" << p_code;
    }
}
