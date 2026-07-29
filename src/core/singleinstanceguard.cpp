#include "singleinstanceguard.h"

#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

#include <utils/utils.h>

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

SingleInstanceGuard::SingleInstanceGuard(const QString &p_serverName,
                                         const QString &p_lockFilePath)
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
      // producing a second primary. With the incremental updater that second
      // primary could reach normal initialization -- mapping Qt, VTextEdit and
      // vxcore modules -- while an applier is swapping those very files. The
      // caller must exit instead.
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

void SingleInstanceGuard::requestOpenFiles(const QStringList &p_files) {
  sendOpenFilesRequest(p_files, OpCode::OpenFiles, "open files");
}

void SingleInstanceGuard::requestOpenFilesDetached(const QStringList &p_files) {
  sendOpenFilesRequest(p_files, OpCode::OpenFilesDetached, "open files detached");
}

void SingleInstanceGuard::sendOpenFilesRequest(const QStringList &p_files, OpCode p_code,
                                               const char *p_what) {
  if (p_files.isEmpty()) {
    return;
  }

  Q_ASSERT(!m_online);
  if (!m_client || m_client->state() != QLocalSocket::ConnectedState) {
    qWarning() << "failed to request" << p_what
               << (m_client ? m_client->errorString() : QStringLiteral("no client"));
    return;
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
    return;
  }

  sendRequest(m_client.data(), p_code, absFiles.join(c_stringListSeparator));
}

void SingleInstanceGuard::requestShow() {
  Q_ASSERT(!m_online);
  if (!m_client || m_client->state() != QLocalSocket::ConnectedState) {
    qWarning() << "failed to request show" << m_client->errorString();
    return;
  }

  sendRequest(m_client.data(), OpCode::Show, QString());
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

      connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
        Q_ASSERT(m_ongoingConnect);
        socket->deleteLater();
        m_ongoingConnect = false;
      });
      connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { receiveCommand(socket); });
    }
  });
}

void SingleInstanceGuard::receiveCommand(QLocalSocket *p_socket) {
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

    case OpCode::OpenFiles: {
      Q_ASSERT(m_command.m_size != 0);
      QString payload;
      inStream >> payload;
      const auto files = payload.split(c_stringListSeparator);
      emit openFilesRequested(files);
      break;
    }

    case OpCode::OpenFilesDetached: {
      Q_ASSERT(m_command.m_size != 0);
      QString payload;
      inStream >> payload;
      const auto files = payload.split(c_stringListSeparator);
      emit openFilesDetachedRequested(files);
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

void SingleInstanceGuard::sendRequest(QLocalSocket *p_socket, OpCode p_code,
                                      const QString &p_payload) {
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
