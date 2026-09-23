#include "singleinstanceguard.h"

#include <QByteArray>
#include <QDataStream>
#include <QDeadlineTimer>
#include <QDebug>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

using namespace vnotex;

const QString SingleInstanceGuard::c_serverName = "vnote";

const QChar SingleInstanceGuard::c_stringListSeparator = '>';

QString SingleInstanceGuard::lockFilePath() const {
  if (!m_lockFilePathOverride.isEmpty()) {
    return m_lockFilePathOverride;
  }
  return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
         QStringLiteral("/vnote.lock");
}

QString SingleInstanceGuard::serverName() const {
  return m_serverNameOverride.isEmpty() ? c_serverName : m_serverNameOverride;
}

SingleInstanceGuard::SingleInstanceGuard(const QString &p_serverName, const QString &p_lockFilePath)
    : m_serverNameOverride(p_serverName), m_lockFilePathOverride(p_lockFilePath) {}

SingleInstanceGuard::~SingleInstanceGuard() { exit(); }

SingleInstanceGuard::TryRunResult SingleInstanceGuard::tryRun() {
  Q_ASSERT(!m_online);

  // Use a lock file for cross-platform single-instance detection.
  // QLockFile::tryLock(0) is non-blocking (no 200ms timeout).
  // QLocalServer::listen() cannot be used for detection on Windows
  // because Windows allows multiple servers on the same named pipe.
  m_lockFile.reset(new QLockFile(lockFilePath()));
  m_lockFile->setStaleLockTime(0); // We check manually; no auto-expiry.

  if (!m_lockFile->tryLock(0)) {
    // Another instance holds the lock. Connect to it for IPC.
    m_client = tryConnect();
    if (m_client) {
      return TryRunResult::Secondary;
    }

    // Lock is held but we cannot connect. Stale lock from a crash?
    // Try to remove and re-acquire.
    m_lockFile->removeStaleLockFile();
    if (!m_lockFile->tryLock(0)) {
      // FAIL CLOSED. This branch historically warned and proceeded anyway,
      // producing a second primary. Two primaries write the same config,
      // session snapshot and notebook state, and both run ConfigMgr2's
      // bundled-resource install over the same folders. The caller must exit
      // instead.
      qWarning() << "lock is held but the holder is unreachable over IPC; refusing to run";
      return TryRunResult::BusyUnreachable;
    }
  }

  m_server = tryListen();
  if (m_server) {
    qInfo() << "guard succeeds to run";
  } else {
    qWarning() << "failed to start local server for IPC";
  }

  setupServer();

  m_online = true;
  return TryRunResult::Primary;
}

bool SingleInstanceGuard::requestOpenFiles(const QStringList &p_files) {
  return sendOpenFilesRequest(p_files, OpCode::OpenFiles, "open files");
}

bool SingleInstanceGuard::requestOpenFilesDetached(const QStringList &p_files) {
  return sendOpenFilesRequest(p_files, OpCode::OpenFilesDetached, "open files detached");
}

bool SingleInstanceGuard::sendOpenFilesRequest(const QStringList &p_files, OpCode p_code,
                                               const char *p_what) {
  if (p_files.isEmpty()) {
    return true;
  }

  Q_ASSERT(!m_online);
  if (!m_client || m_client->state() != QLocalSocket::ConnectedState) {
    qWarning() << "failed to request" << p_what
               << (m_client ? m_client->errorString() : QStringLiteral("no client"));
    return false;
  }

  // Resolve to absolute paths against THIS (sending) process's working
  // directory before crossing the socket. The receiving primary instance has a
  // different, unrelated working directory and cannot correctly resolve a
  // relative path, so relative paths must never be sent over the wire.
  QStringList absFiles;
  absFiles.reserve(p_files.size());
  for (const auto &file : p_files) {
    if (file.isEmpty()) {
      continue;
    }
    absFiles << QFileInfo(file).absoluteFilePath();
  }
  if (absFiles.isEmpty()) {
    return true;
  }

  return sendRequest(m_client.data(), p_code, absFiles.join(c_stringListSeparator));
}

bool SingleInstanceGuard::requestShow() {
  Q_ASSERT(!m_online);
  if (!m_client || m_client->state() != QLocalSocket::ConnectedState) {
    qWarning() << "failed to request show"
               << (m_client ? m_client->errorString() : QStringLiteral("no client"));
    return false;
  }

  return sendRequest(m_client.data(), OpCode::Show, QString());
}

void SingleInstanceGuard::exit() {
  m_online = false;

  if (m_lockFile) {
    m_lockFile->unlock();
    m_lockFile.reset();
  }

  if (m_client) {
    m_client->disconnectFromServer();
    m_client.clear();
  }

  if (m_server) {
    m_server->close();
    m_server.clear();
  }
}

QSharedPointer<QLocalSocket> SingleInstanceGuard::tryConnect() {
  const QString name = serverName();
  auto socket = QSharedPointer<QLocalSocket>::create();
  socket->connectToServer(name);
  if (socket->waitForConnected(200)) {
    // Connected.
    qDebug() << "socket connected to server" << name;
    return socket;
  } else {
    qDebug() << "socket connect timeout";
    return nullptr;
  }
}

QSharedPointer<QLocalServer> SingleInstanceGuard::tryListen() {
  const QString name = serverName();
  auto server = QSharedPointer<QLocalServer>::create();
  bool ret = server->listen(name);
  if (!ret && server->serverError() == QAbstractSocket::AddressInUseError) {
    // On Unix, a previous crash may leave a server running.
    // Clean up and try again.
    QLocalServer::removeServer(name);
    ret = server->listen(name);
  }

  if (ret) {
    qDebug() << "local server listening on" << name;
    return server;
  } else {
    qDebug() << "failed to start local server";
    return nullptr;
  }
}

void SingleInstanceGuard::setupServer() {
  if (!m_server) {
    return;
  }

  connect(m_server.data(), &QLocalServer::newConnection, this, [this]() {
    while (auto *socket = m_server->nextPendingConnection()) {
      qInfo() << "local server receives new connect" << socket;
      connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
        receiveCommand(socket);
        socket->deleteLater();
      });
      connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { receiveCommand(socket); });
      // Data may already be buffered before readyRead is connected.
      receiveCommand(socket);
    }
  });
}

void SingleInstanceGuard::receiveCommand(QLocalSocket *p_socket) {
  QDataStream in(p_socket);
  in.setVersion(QDataStream::Qt_5_12);

  while (p_socket->bytesAvailable() > 0) {
    // Transactions retain partial headers AND QString bodies on this socket.
    // The wire size is a UTF-16 unit count, not a serialized byte count.
    in.startTransaction();
    quint32 opCode = 0;
    quint32 size = 0;
    QString payload;
    in >> opCode >> size;
    if (size > 0) {
      in >> payload;
    }
    if (!in.commitTransaction()) {
      return;
    }
    const auto code = static_cast<OpCode>(opCode);
    if (size != quint32(payload.size()) ||
        (code == OpCode::Show
             ? size != 0
             : (code != OpCode::OpenFiles && code != OpCode::OpenFilesDetached) || size == 0)) {
      qWarning() << "invalid IPC request" << opCode << size;
      p_socket->abort();
      return;
    }

    // Queue delivery before acknowledging acceptance. GUI handlers can run
    // nested event loops; none may run inside this socket's read transaction.
    QMetaObject::invokeMethod(
        this,
        [this, code, payload]() {
          switch (code) {
          case OpCode::Show:
            emit showRequested();
            break;
          case OpCode::OpenFiles:
            emit openFilesRequested(payload.split(c_stringListSeparator));
            break;
          case OpCode::OpenFilesDetached:
            emit openFilesDetachedRequested(payload.split(c_stringListSeparator));
            break;
          default:
            break;
          }
        },
        Qt::QueuedConnection);

    // ACK: the accepted opcode and a zero size, using the same stream version.
    // Never block the primary on a client that does not read its ACK.
    if (p_socket->state() == QLocalSocket::ConnectedState) {
      QDataStream ack(p_socket);
      ack.setVersion(QDataStream::Qt_5_12);
      ack << opCode << quint32(0);
      p_socket->flush();
    }
  }
}

bool SingleInstanceGuard::sendRequest(QLocalSocket *p_socket, OpCode p_code,
                                      const QString &p_payload) {
  QByteArray block;
  QDataStream out(&block, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_12);
  out << static_cast<quint32>(p_code);
  out << static_cast<quint32>(p_payload.size());
  if (!p_payload.isEmpty()) {
    out << p_payload;
  }

  // A primary still installing bundled resources may not service IPC yet.
  // Share one bounded deadline across writing and acknowledgement; never retry
  // a request that might already have opened files.
  QDeadlineTimer deadline(30000);
  const auto fail = [p_socket, p_code](const char *p_stage) {
    qWarning() << "failed to send request" << p_code << p_stage << p_socket->errorString()
               << "state:" << p_socket->state() << "pending bytes:" << p_socket->bytesToWrite()
               << "available bytes:" << p_socket->bytesAvailable();
    p_socket->abort();
    return false;
  };
  if (p_socket->write(block) != block.size()) {
    return fail("write");
  }
  while (p_socket->bytesToWrite() > 0) {
    const auto remaining = deadline.remainingTime();
    if (remaining == 0 ||
        (!p_socket->waitForBytesWritten(int(remaining)) && p_socket->bytesToWrite() > 0)) {
      return fail("write timeout or disconnect");
    }
  }

  // Empty write buffers alone do not prove the primary received a command.
  while (p_socket->bytesAvailable() < qint64(sizeof(quint32) * 2)) {
    const auto remaining = deadline.remainingTime();
    if (remaining == 0 || p_socket->state() != QLocalSocket::ConnectedState ||
        (!p_socket->waitForReadyRead(int(remaining)) &&
         p_socket->bytesAvailable() < qint64(sizeof(quint32) * 2))) {
      return fail("acknowledgement timeout or disconnect");
    }
  }
  QDataStream in(p_socket);
  in.setVersion(QDataStream::Qt_5_12);
  quint32 code = 0;
  quint32 size = 0;
  in >> code >> size;
  if (in.status() != QDataStream::Ok || code != quint32(p_code) || size != 0) {
    return fail("invalid acknowledgement");
  }
  qDebug() << "request acknowledged" << p_code << p_payload.size();
  return true;
}
