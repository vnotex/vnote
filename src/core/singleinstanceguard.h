#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H

#include <QLockFile>
#include <QObject>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace vnotex {
class SingleInstanceGuard : public QObject {
  Q_OBJECT
public:
  SingleInstanceGuard() = default;

  ~SingleInstanceGuard();

  // Try to run. Return true on success.
  bool tryRun();

  // Server API.
public:
  // A running instance requests to exit.
  void exit();

  // Clients API.
public:
  void requestOpenFiles(const QStringList &p_files);

  void requestOpenFilesDetached(const QStringList &p_files);

  void requestShow();

signals:
  void openFilesRequested(const QStringList &p_files);

  void openFilesDetachedRequested(const QStringList &p_files);

  void showRequested();

private:
  enum OpCode { Null = 0, Show, OpenFiles, OpenFilesDetached };

  struct Command {
    void clear() {
      m_opCode = OpCode::Null;
      m_size = 0;
    }

    OpCode m_opCode = OpCode::Null;
    int m_size = 0;
  };

  QSharedPointer<QLocalSocket> tryConnect();

  QSharedPointer<QLocalServer> tryListen();

  void setupServer();

  void receiveCommand(QLocalSocket *p_socket);

  // Shared body for requestOpenFiles / requestOpenFilesDetached: validates the
  // connection, resolves each path to absolute against THIS process's working
  // directory, and sends it under p_code. p_what labels the operation in logs.
  void sendOpenFilesRequest(const QStringList &p_files, OpCode p_code, const char *p_what);

  void sendRequest(QLocalSocket *p_socket, OpCode p_code, const QString &p_payload);

  QString lockFilePath() const;

  // Whether succeeded to run.
  bool m_online = false;

  QSharedPointer<QLocalSocket> m_client;

  QSharedPointer<QLocalServer> m_server;

  bool m_ongoingConnect = false;

  Command m_command;

  QScopedPointer<QLockFile> m_lockFile;

  static const QString c_serverName;

  static const QChar c_stringListSeparator;
};
} // namespace vnotex

#endif // SINGLEINSTANCEGUARD_H
