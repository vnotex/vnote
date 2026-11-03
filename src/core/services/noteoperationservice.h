#ifndef NOTEOPERATIONSERVICE_H
#define NOTEOPERATIONSERVICE_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace vnotex {

class ServiceLocator;

enum class NoteOperationKind { Create, Replace, Append, Rename, Trash };

struct NoteOperationRequest {
  NoteOperationKind kind = NoteOperationKind::Create;
  QString notebookId, nodeId, relativePath, parentId, name, text;
  QByteArray expectedFileSha256;
  QString expectedPath;
  // Rechecked on the owner thread and immediately before worker installation.
  // Must be lifetime-safe and callable from any thread; never capture GUI objects.
  std::function<bool()> authorized;
  std::shared_ptr<std::atomic_bool> cancelled;
};

struct NoteOperationResult {
  bool committed = false;
  bool saved = false;
  bool recoveryRequired = false;
  QString notebookId, nodeId, relativePath;
  QByteArray fileSha256;
  QString errorCode, errorMessage;
};

// Owner-thread domain operations. No transport, credentials or HTTP dependencies.
// Even immediate rejection completes asynchronously after execute() returns.
class NoteOperationService : public QObject {
  Q_OBJECT
public:
  explicit NoteOperationService(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~NoteOperationService() override;
  quint64 execute(NoteOperationRequest p_request);
  void cancel(quint64 p_token);
  void shutdown();

signals:
  void finished(quint64 p_token, const NoteOperationResult &p_result);

private:
  struct Operation;
  void start(quint64 p_token);
  void runNodeOperation(quint64 p_token);
  bool validate(Operation &p_operation, NoteOperationResult *p_result) const;
  void finishNodeOperation(quint64 p_token);
  void complete(quint64 p_token, NoteOperationResult p_result);
  ServiceLocator &m_services;
  QHash<quint64, std::shared_ptr<Operation>> m_operations;
  quint64 m_nextToken = 0;
  bool m_stopping = false;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::NoteOperationKind)
Q_DECLARE_METATYPE(vnotex::NoteOperationRequest)
Q_DECLARE_METATYPE(vnotex::NoteOperationResult)

#endif // NOTEOPERATIONSERVICE_H
