#include "webviewjobs.h"

#include <QUuid>
#include <QWebChannel>
#include <QWebEnginePage>

#include <utility>

using namespace vnotex;

WebViewJobSession::WebViewJobSession(QWebEnginePage *p_page, QWebChannel *p_channel,
                                     QObject *p_parent)
    : QObject(p_parent), m_page(p_page), m_channel(p_channel),
      m_generation(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
  Q_ASSERT(p_page && p_channel);
  Q_ASSERT(!p_channel->registeredObjects().contains(QStringLiteral("vxJobs")));
  p_channel->registerObject(QStringLiteral("vxJobs"), this);

  connect(p_page, &QWebEnginePage::loadStarted, this, [this]() {
    if (!m_seenLoad) {
      // Bootstrap may call setHtml() before the submitter gets the session.
      // The initial navigation belongs to those queued cold-start requests.
      m_seenLoad = true;
      return;
    }
    reset(WebViewJobResult::Status::Cancelled, QStringLiteral("Web page reloaded"), true, true);
  });
  connect(p_page, &QWebEnginePage::loadFinished, this, [this](bool p_success) {
    if (!p_success) {
      reset(WebViewJobResult::Status::Error, QStringLiteral("Web page failed to load"), false,
            true);
    }
  });
  connect(p_page, &QWebEnginePage::renderProcessTerminated, this,
          [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
            reset(WebViewJobResult::Status::Error, QStringLiteral("Web renderer terminated"), false,
                  true);
          });
  connect(p_page, &QObject::destroyed, this, [this]() {
    m_page = nullptr;
    reset(WebViewJobResult::Status::Error, QStringLiteral("Web page destroyed"), false, true);
  });
  connect(p_channel, &QObject::destroyed, this, [this]() {
    m_channel = nullptr;
    reset(WebViewJobResult::Status::Error, QStringLiteral("Web channel destroyed"), false, true);
  });
}

WebViewJobSession::~WebViewJobSession() {
  m_accepting = false;
  m_ready = false;
  if (m_page) {
    disconnect(m_page, nullptr, this, nullptr);
  }
  if (m_channel) {
    disconnect(m_channel, nullptr, this, nullptr);
    m_channel->deregisterObject(this);
  }
  auto jobs = std::move(m_queue);
  takeActive(jobs, false);
  disconnectOwners(jobs);
  finish(jobs,
         {WebViewJobResult::Status::Cancelled, {}, QStringLiteral("Web job session destroyed")});
}

QString WebViewJobSession::submit(const QString &p_operation, const QJsonObject &p_arguments,
                                  QObject *p_owner, Completion p_completion) {
  const auto id = m_generation + QLatin1Char(':') + QString::number(++m_nextId);
  if (!m_accepting || !m_page || !m_channel) {
    if (p_completion) {
      p_completion(
          {WebViewJobResult::Status::Error, {}, QStringLiteral("Web job runtime is unavailable")});
    }
    return id;
  }

  const bool wasIdle = isIdle();
  Job job;
  job.id = id;
  job.operation = p_operation;
  job.arguments = p_arguments;
  job.owner = p_owner ? p_owner : this;
  job.completion = std::move(p_completion);
  job.ownerDestroyed =
      connect(job.owner, &QObject::destroyed, this, [this, id]() { cancelJobs(nullptr, id); });
  m_queue.push_back(std::move(job));
  scheduleDispatch();
  if (wasIdle) {
    emit idleChanged(false);
  }
  return id;
}

void WebViewJobSession::cancel(const QString &p_id) {
  if (!p_id.isEmpty()) {
    cancelJobs(nullptr, p_id);
  }
}

void WebViewJobSession::cancelOwner(QObject *p_owner) { cancelJobs(p_owner ? p_owner : this); }

void WebViewJobSession::invalidate() {
  reset(WebViewJobResult::Status::Cancelled, QStringLiteral("Web job runtime invalidated"), false,
        false);
}

void WebViewJobSession::fail(const QString &p_error) {
  reset(WebViewJobResult::Status::Error, p_error, false, true);
}

bool WebViewJobSession::isReady() const { return m_ready; }

bool WebViewJobSession::isIdle() const { return m_queue.empty() && !m_active; }

QString WebViewJobSession::generation() const { return m_generation; }

void WebViewJobSession::setReady(const QString &p_generation) {
  if (m_ready || !m_accepting || !m_page || !m_channel || p_generation != m_generation) {
    return;
  }
  m_ready = true;
  scheduleDispatch();
  emit readyChanged(true);
}

void WebViewJobSession::complete(const QString &p_id, bool p_success, const QJsonValue &p_value,
                                 const QString &p_error) {
  if (!m_active || m_active->id != p_id) {
    return;
  }
  auto job = std::move(*m_active);
  m_active.reset();
  disconnect(job.ownerDestroyed);
  scheduleDispatch();
  if (isIdle()) {
    emit idleChanged(true);
  }
  // State is released before publication; a callback may enqueue, cancel, or
  // destroy this session. A cancelled job has already dropped its completion.
  finish(job, {p_success ? WebViewJobResult::Status::Success : WebViewJobResult::Status::Error,
               p_value, p_error});
}

void WebViewJobSession::scheduleDispatch() {
  if (m_dispatchScheduled || !m_ready || m_active || m_queue.empty()) {
    return;
  }
  m_dispatchScheduled = true;
  QMetaObject::invokeMethod(
      this,
      [this]() {
        m_dispatchScheduled = false;
        dispatchNext();
      },
      Qt::QueuedConnection);
}

void WebViewJobSession::dispatchNext() {
  if (!m_ready || !m_accepting || m_active || m_queue.empty()) {
    return;
  }
  m_active.emplace(std::move(m_queue.front()));
  m_queue.pop_front();
  const auto id = m_active->id;
  const auto operation = std::move(m_active->operation);
  const auto arguments = std::move(m_active->arguments);
  emit jobRequested(id, operation, arguments);
}

void WebViewJobSession::cancelJobs(QObject *p_owner, const QString &p_id) {
  const bool wasIdle = isIdle();
  std::list<Job> jobs;
  for (auto it = m_queue.begin(); it != m_queue.end();) {
    const auto current = it++;
    if (p_id.isEmpty() ? current->owner == p_owner : current->id == p_id) {
      jobs.splice(jobs.end(), m_queue, current);
    }
  }

  QString activeId;
  if (m_active && !m_active->cancelled &&
      (p_id.isEmpty() ? m_active->owner == p_owner : m_active->id == p_id)) {
    activeId = m_active->id;
    takeActive(jobs, true);
  }
  disconnectOwners(jobs);
  scheduleDispatch();
  QPointer<WebViewJobSession> guard(this);
  if (!activeId.isEmpty()) {
    emit jobCancelled(activeId);
  }
  if (guard && wasIdle != isIdle()) {
    emit idleChanged(isIdle());
  }
  finish(jobs, {WebViewJobResult::Status::Cancelled, {}, QStringLiteral("Web job cancelled")});
}

void WebViewJobSession::reset(WebViewJobResult::Status p_status, const QString &p_error,
                              bool p_accepting, bool p_retireActive) {
  const bool wasReady = m_ready;
  const bool wasIdle = isIdle();
  m_ready = false;
  m_accepting = p_accepting;
  m_seenLoad = true;
  m_generation = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_nextId = 0;
  auto jobs = std::move(m_queue);
  QString activeId;
  if (m_active && (!m_active->cancelled || p_retireActive)) {
    if (!p_retireActive) {
      activeId = m_active->id;
    }
    takeActive(jobs, !p_retireActive);
  }
  disconnectOwners(jobs);

  QPointer<WebViewJobSession> guard(this);
  if (!activeId.isEmpty()) {
    emit jobCancelled(activeId);
  }
  if (guard && wasReady) {
    emit readyChanged(false);
  }
  if (guard) {
    emit generationChanged();
  }
  if (guard && wasIdle != isIdle()) {
    emit idleChanged(isIdle());
  }
  finish(jobs, {p_status, {}, p_error});
}

void WebViewJobSession::takeActive(std::list<Job> &p_jobs, bool p_retainSlot) {
  if (!m_active) {
    return;
  }
  p_jobs.push_back(std::move(*m_active));
  m_active.reset();
  if (p_retainSlot) {
    m_active.emplace();
    m_active->id = p_jobs.back().id;
    m_active->cancelled = true;
  }
}

void WebViewJobSession::disconnectOwners(std::list<Job> &p_jobs) {
  for (auto &job : p_jobs) {
    disconnect(job.ownerDestroyed);
  }
}

void WebViewJobSession::finish(Job &p_job, const WebViewJobResult &p_result) {
  auto completion = std::move(p_job.completion);
  if (p_job.owner && completion) {
    completion(p_result);
  }
}

void WebViewJobSession::finish(std::list<Job> &p_jobs, const WebViewJobResult &p_result) {
  for (auto &job : p_jobs) {
    finish(job, p_result);
  }
}
