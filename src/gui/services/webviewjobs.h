#ifndef WEBVIEWJOBS_H
#define WEBVIEWJOBS_H

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <list>
#include <optional>

class QWebChannel;
class QWebEnginePage;

namespace vnotex {

struct WebViewJobResult {
  enum class Status { Success, Error, Cancelled };

  Status status = Status::Error;
  QJsonValue value;
  QString error;
};

// Register before the page's first QWebChannel handshake. The supplied page and
// channel remain caller-owned; the session only registers its own vxJobs object.
class WebViewJobSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString generation READ generation NOTIFY generationChanged)

public:
  using Completion = std::function<void(const WebViewJobResult &)>;

  WebViewJobSession(QWebEnginePage *p_page, QWebChannel *p_channel, QObject *p_parent = nullptr);
  ~WebViewJobSession() override;

  // A null owner scopes the request to this session. Destroyed owners receive
  // no callback; explicit cancellation settles each live owner exactly once.
  QString submit(const QString &p_operation, const QJsonObject &p_arguments, QObject *p_owner,
                 Completion p_completion);
  void cancel(const QString &p_id);
  void cancelOwner(QObject *p_owner);

  // Retire this runtime. Only a subsequent page load can make it ready again.
  // Already running JS retains its serial slot until it settles or is torn down.
  void invalidate();

  // A provider can reject a missing/invalid bootstrap without faking readiness.
  // The failed generation accepts no more work until the next page load.
  void fail(const QString &p_error);
  bool isReady() const;
  bool isIdle() const;
  QString generation() const;

signals:
  void readyChanged(bool p_ready);
  void idleChanged(bool p_idle);
  void generationChanged();
  void jobRequested(const QString &p_id, const QString &p_operation,
                    const QJsonObject &p_arguments);
  void jobCancelled(const QString &p_id);

public slots:
  void setReady(const QString &p_generation);
  void complete(const QString &p_id, bool p_success, const QJsonValue &p_value,
                const QString &p_error);

private:
  struct Job {
    QString id;
    QString operation;
    QJsonObject arguments;
    QPointer<QObject> owner;
    Completion completion;
    QMetaObject::Connection ownerDestroyed;
    bool cancelled = false;
  };

  void scheduleDispatch();
  void dispatchNext();
  void cancelJobs(QObject *p_owner, const QString &p_id = {});
  void reset(WebViewJobResult::Status p_status, const QString &p_error, bool p_accepting,
             bool p_retireActive);
  void takeActive(std::list<Job> &p_jobs, bool p_retainSlot);
  static void disconnectOwners(std::list<Job> &p_jobs);
  static void finish(Job &p_job, const WebViewJobResult &p_result);
  static void finish(std::list<Job> &p_jobs, const WebViewJobResult &p_result);

  QPointer<QWebEnginePage> m_page;
  QPointer<QWebChannel> m_channel;
  std::list<Job> m_queue;
  std::optional<Job> m_active;
  QString m_generation;
  quint64 m_nextId = 0;
  bool m_seenLoad = false;
  bool m_accepting = true;
  bool m_ready = false;
  bool m_dispatchScheduled = false;
};

} // namespace vnotex

#endif // WEBVIEWJOBS_H
