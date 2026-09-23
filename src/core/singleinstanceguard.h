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
  // Outcome of tryRun().
  //
  // This used to be a bool with a FAIL-OPEN third case: when the lock was held
  // but IPC was unreachable, the guard logged a warning and returned true, i.e.
  // it became a SECOND primary. That is unsafe -- two primaries write the same
  // config, session snapshot and notebook state, and both run ConfigMgr2's
  // bundled-resource install over the same folders -- so the case is now
  // surfaced explicitly and the caller must fail closed.
  enum class TryRunResult {
    // This process is the single running instance and owns the IPC server.
    Primary,
    // Another instance is running and reachable; this process forwarded its
    // request over IPC and must exit without initializing.
    Secondary,
    // The lock is held but the holder cannot be reached over IPC. Previously
    // this silently became a second primary. The caller MUST show a message and
    // exit WITHOUT initializing.
    BusyUnreachable,
  };

  SingleInstanceGuard() = default;

  // Test seam (unconditional, per ADR-6): overrides the IPC server name and the
  // lock file path so a test can exercise the guard WITHOUT colliding with a
  // real running VNote (which shares the process-wide names "vnote" /
  // <temp>/vnote.lock, and would otherwise be sent IPC commands by the test).
  //
  // Production code uses the default constructor.
  SingleInstanceGuard(const QString &p_serverName, const QString &p_lockFilePath);

  ~SingleInstanceGuard();

  // Try to run. See TryRunResult.
  TryRunResult tryRun();

  // Server API.
public:
  // A running instance requests to exit.
  void exit();

  // Clients API.
public:
  // True only after acceptance by the primary (or an empty file list).
  bool requestOpenFiles(const QStringList &p_files);

  bool requestOpenFilesDetached(const QStringList &p_files);

  bool requestShow();

signals:
  void openFilesRequested(const QStringList &p_files);

  void openFilesDetachedRequested(const QStringList &p_files);

  void showRequested();

private:
  enum OpCode { Null = 0, Show, OpenFiles, OpenFilesDetached };

  QSharedPointer<QLocalSocket> tryConnect();

  QSharedPointer<QLocalServer> tryListen();

  void setupServer();

  void receiveCommand(QLocalSocket *p_socket);

  // Shared body for requestOpenFiles / requestOpenFilesDetached: validates the
  // connection, resolves each path to absolute against THIS process's working
  // directory, and sends it under p_code. p_what labels the operation in logs.
  bool sendOpenFilesRequest(const QStringList &p_files, OpCode p_code, const char *p_what);

  bool sendRequest(QLocalSocket *p_socket, OpCode p_code, const QString &p_payload);

  QString lockFilePath() const;

  QString serverName() const;

  // Whether succeeded to run.
  bool m_online = false;

  QSharedPointer<QLocalSocket> m_client;

  QSharedPointer<QLocalServer> m_server;

  QScopedPointer<QLockFile> m_lockFile;

  // Empty unless overridden through the testing constructor.
  QString m_serverNameOverride;
  QString m_lockFilePathOverride;

  static const QString c_serverName;

  static const QChar c_stringListSeparator;
};
} // namespace vnotex

#endif // SINGLEINSTANCEGUARD_H
