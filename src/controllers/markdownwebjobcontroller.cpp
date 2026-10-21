#include "markdownwebjobcontroller.h"

#include <QJsonArray>
#include <QUuid>
#include <QVariant>
#include <utility>

#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <gui/services/themeservice.h>
#include <gui/services/webviewservice.h>
#include <gui/utils/guiutils.h>
#include <gui/utils/imageutils.h>
#include <utils/fileutils2.h>

using namespace vnotex;

namespace {
WebViewJobResult cancelledResult() {
  return {WebViewJobResult::Status::Cancelled, {}, QStringLiteral("Markdown job was cancelled")};
}
WebViewJobResult errorResult(const QString &p_error) {
  return {WebViewJobResult::Status::Error, {}, p_error};
}
QString automaticKey(const QString &p_operation, quint64 p_id) {
  return p_operation + QLatin1Char(':') + QString::number(p_id);
}
void releaseScope(WebViewJobSession *p_session, const QString &p_scope) {
  // The endpoint owns cleanup: editor destruction must not cancel it or keep the editor alive.
  p_session->submit(QStringLiteral("markdown.releaseScope"), {{QStringLiteral("scope"), p_scope}},
                    p_session, {});
}
} // namespace

struct MarkdownWebJobController::Request {
  QString operation;
  QJsonObject arguments;
  Completion completion;
  QString transportId;
  QPointer<WebViewJobSession> session;
  WebViewJobSession *endpointIdentity = nullptr;
  QString automaticKey;
  quint64 generation = 0;
  bool styleSheet = false;
};

MarkdownWebJobController::CssRuleStyle
MarkdownWebJobController::CssRuleStyle::fromJson(const QJsonObject &p_obj) {
  CssRuleStyle style;
  style.m_selector = p_obj[QStringLiteral("selector")].toString();
  style.m_color = p_obj[QStringLiteral("color")].toString();
  style.m_backgroundColor = p_obj[QStringLiteral("backgroundColor")].toString();
  style.m_fontWeight = p_obj[QStringLiteral("fontWeight")].toString();
  style.m_fontStyle = p_obj[QStringLiteral("fontStyle")].toString();
  return style;
}

QTextCharFormat MarkdownWebJobController::CssRuleStyle::toTextCharFormat() const {
  QTextCharFormat format;
  if (!m_color.isEmpty()) {
    format.setForeground(GuiUtils::toColor(m_color));
  }
  if (!m_backgroundColor.isEmpty()) {
    format.setBackground(GuiUtils::toColor(m_backgroundColor));
  }
  if (m_fontWeight.contains(QStringLiteral("bold"))) {
    format.setFontWeight(QFont::Bold);
  }
  if (m_fontStyle.contains(QStringLiteral("italic"))) {
    format.setFontItalic(true);
  }
  return format;
}

MarkdownWebJobController::MarkdownWebJobController(ServiceLocator &p_services,
                                                   const Buffer2 &p_buffer, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_buffer(p_buffer),
      m_scopePrefix(QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char(':')),
      m_scope(m_scopePrefix + QString::number(m_generation)) {
  if (auto *buffers = m_services.get<BufferService>()) {
    m_locked = m_buffer.isEncrypted() && buffers->isProtectedLocking();
    connect(buffers->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
            SLOT(onProtectedLockingChanged(bool)), Qt::DirectConnection);
  }
  if (auto *hooks = m_services.get<HookManager>()) {
    m_closeHook = hooks->addAction<ViewWindowCloseEvent>(
        HookNames::ViewWindowAfterClose,
        [this](HookContext &, const ViewWindowCloseEvent &p_event) {
          if (p_event.bufferId == m_buffer.id() && !contextIsCurrent()) {
            revoke();
          }
        });
    m_notebookCloseHook = hooks->addAction<NotebookCloseEvent>(
        HookNames::NotebookAfterClose, [this](HookContext &, const NotebookCloseEvent &p_event) {
          if (p_event.notebookId == m_buffer.nodeId().notebookId) {
            revoke();
          }
        });
  }
}

MarkdownWebJobController::~MarkdownWebJobController() {
  m_destroying = true;
  revoke();
  for (auto *session : m_observed.keys()) {
    if (session->property("markdownJobController").value<QObject *>() == this) {
      session->setProperty("markdownJobController", QVariant());
    }
  }
  if (auto *hooks = m_services.get<HookManager>()) {
    hooks->removeAction(m_closeHook);
    hooks->removeAction(m_notebookCloseHook);
  }
}

bool MarkdownWebJobController::contextIsCurrent() const {
  // Buffer2::isValid only checks the handle, not whether core still owns the buffer.
  if (m_revoked || m_locked || !m_buffer.isValid() || m_buffer.getBuffer().isEmpty()) {
    return false;
  }
  // A lease is a bounded authorization check, never captured by an asynchronous job.
  return !m_buffer.isEncrypted() || bool(m_buffer.acquireProtectedLease());
}

bool MarkdownWebJobController::acceptsWebWork() const {
  return !m_suspended && contextIsCurrent();
}

bool MarkdownWebJobController::authorizeSession(WebViewJobSession *p_session) {
  if (!p_session ||
      p_session->property("markdownJobProtected").toBool() != m_buffer.isEncrypted() ||
      p_session->property("markdownJobBufferId").toString() != m_buffer.id()) {
    return false;
  }
  const auto owner = p_session->property("markdownJobController").value<QObject *>();
  if (owner && owner != this) {
    return false;
  }
  p_session->setProperty("markdownJobController", QVariant::fromValue<QObject *>(this));
  return true;
}

bool MarkdownWebJobController::preferredIsCurrent() const {
  return m_preferred && m_preferredEpoch == m_generation && m_preferred->isReady() &&
         m_preferredGeneration == m_preferred->generation();
}

void MarkdownWebJobController::observeSession(WebViewJobSession *p_session) {
  if (m_observed.contains(p_session)) {
    return;
  }
  m_observed.insert(p_session, connect(p_session, &QObject::destroyed, this, [this, p_session]() {
                      m_observed.remove(p_session);
                      // Session destruction can occur during an application-wide runtime
                      // invalidation. Every affected request must settle even if the endpoint
                      // suppresses its callbacks.
                      const auto requests = m_requests.keys();
                      for (auto id : requests) {
                        const auto request = m_requests.value(id);
                        if (request && request->endpointIdentity == p_session) {
                          finish(id, cancelledResult());
                        }
                      }
                    }));
  connect(p_session, &WebViewJobSession::readyChanged, this, [this, p_session](bool p_ready) {
    if (p_ready && acceptsWebWork()) {
      if (p_session == m_preferred) {
        releaseDrainedProtectedSession();
      }
      notifyReadyEndpoint();
    }
  });
  connect(p_session, &WebViewJobSession::generationChanged, this, [this, p_session]() {
    if (p_session == m_preferred) {
      m_preferredGeneration = p_session->generation();
      m_preferredEpoch = m_generation;
    }
  });
  connect(
      p_session, &WebViewJobSession::idleChanged, this,
      [this](bool p_idle) {
        if (p_idle)
          releaseDrainedProtectedSession();
      },
      Qt::QueuedConnection);
}

void MarkdownWebJobController::notifyReadyEndpoint() {
  if (!acceptsWebWork())
    return;
  WebViewJobSession *endpoint = preferredIsCurrent() ? m_preferred.data()
                                : m_buffer.isEncrypted()
                                    ? (m_protectedRetiring ? nullptr : m_protected.data())
                                    : m_shared.data();
  if (!endpoint || !endpoint->isReady())
    return;
  const auto generation = endpoint->generation();
  if (m_readyEndpoint == endpoint && m_readyEndpointGeneration == generation)
    return;
  if (m_readyEndpoint && m_readyEndpoint != endpoint) {
    // Earlier jobs finish at their original endpoint, then its editor-local macros are retired.
    releaseScope(m_readyEndpoint, m_scope);
  }
  m_readyEndpoint = endpoint;
  m_readyEndpointGeneration = generation;
  emit ready();
}

void MarkdownWebJobController::setPreferredSession(WebViewJobSession *p_session,
                                                   const QByteArray &p_protectedHighlightCss) {
  m_preferred = authorizeSession(p_session) ? p_session : nullptr;
  m_preferredCss = m_preferred ? p_protectedHighlightCss : QByteArray();
  m_preferredEpoch = m_generation;
  m_preferredGeneration = m_preferred ? m_preferred->generation() : QString();
  if (m_preferred) {
    observeSession(m_preferred);
    if (preferredIsCurrent()) {
      releaseDrainedProtectedSession();
      notifyReadyEndpoint();
    }
  }
}

void MarkdownWebJobController::setProtectedSession(WebViewJobSession *p_session,
                                                   const QByteArray &p_highlightCss) {
  m_protectedRequested = false;
  if (m_suspended) {
    emit protectedSessionReleaseRequested();
    return;
  }
  if (!m_buffer.isEncrypted() || !contextIsCurrent() || !authorizeSession(p_session)) {
    const auto requests = m_requests.keys();
    for (auto id : requests) {
      if (const auto request = m_requests.value(id); request && !request->session) {
        finish(id, errorResult(QStringLiteral("Protected Markdown runtime is unavailable")));
      }
    }
    return;
  }
  m_protected = p_session;
  m_protectedCss = p_highlightCss;
  m_protectedRetiring = false;
  observeSession(p_session);
  const auto requests = m_requests.keys();
  for (auto id : requests) {
    if (const auto request = m_requests.value(id); request && !request->session) {
      dispatch(id);
    }
  }
  notifyReadyEndpoint();
}

void MarkdownWebJobController::setRenderContext(qreal p_devicePixelRatio, qreal p_width) {
  m_devicePixelRatio = qMax(qreal(0.1), p_devicePixelRatio);
  m_width = qMax(qreal(1), p_width);
}

void MarkdownWebJobController::setSuspended(bool p_suspended) {
  if (m_suspended == p_suspended) {
    return;
  }
  m_suspended = p_suspended;
  if (m_suspended) {
    setPreferredSession(nullptr);
    invalidate();
  }
}

WebViewJobSession *MarkdownWebJobController::selectSession() {
  if (preferredIsCurrent()) {
    return m_preferred;
  }
  if (m_buffer.isEncrypted()) {
    if (m_protectedRetiring) {
      // A reader reload needs a fresh fallback, not one already handed off.
      // Retiring the old page cancels its remaining jobs on their original endpoint.
      m_protected = nullptr;
      m_protectedCss.clear();
      m_protectedRetiring = false;
      emit protectedSessionReleaseRequested();
      return nullptr;
    }
    return m_protected;
  }
  auto *service = m_services.get<WebViewService>();
  m_shared = service ? service->sharedSession() : nullptr;
  return m_shared;
}

void MarkdownWebJobController::releaseDrainedProtectedSession() {
  m_protectedRetiring = m_protectedRetiring || (m_protected && preferredIsCurrent());
  if (m_protected && m_protectedRetiring && m_protected->isIdle()) {
    m_protected = nullptr;
    m_protectedCss.clear();
    emit protectedSessionReleaseRequested();
    m_protectedRetiring = false;
  }
}

void MarkdownWebJobController::submit(const QString &p_operation, QJsonObject p_arguments,
                                      Completion p_completion, const QString &p_automaticKey,
                                      quint64 p_timeStamp, bool p_styleSheet) {
  if (!acceptsWebWork()) {
    p_completion(cancelledResult());
    return;
  }
  if (!p_automaticKey.isEmpty()) {
    if (m_automaticStamps.contains(p_automaticKey) &&
        p_timeStamp < m_automaticStamps.value(p_automaticKey)) {
      p_completion(cancelledResult());
      return;
    }
    cancel(m_automaticRequests.value(p_automaticKey));
    m_automaticStamps.insert(p_automaticKey, p_timeStamp);
  }
  const auto id = ++m_nextRequest;
  auto request = std::make_shared<Request>();
  request->operation = p_operation;
  request->arguments = std::move(p_arguments);
  request->completion = std::move(p_completion);
  request->generation = m_generation;
  request->automaticKey = p_automaticKey;
  request->styleSheet = p_styleSheet;
  m_requests.insert(id, std::move(request));
  if (!p_automaticKey.isEmpty()) {
    m_automaticRequests.insert(p_automaticKey, id);
  }
  dispatch(id);
}

void MarkdownWebJobController::dispatch(quint64 p_request) {
  const auto request = m_requests.value(p_request);
  if (!request || request->session) {
    return;
  }
  if (request->generation != m_generation || !acceptsWebWork()) {
    finish(p_request, cancelledResult());
    return;
  }
  auto *session = selectSession();
  if (!session) {
    if (m_buffer.isEncrypted()) {
      if (!m_protectedRequested) {
        m_protectedRequested = true;
        emit protectedSessionRequested();
      }
    } else {
      finish(p_request, errorResult(QStringLiteral("Markdown runtime is unavailable")));
    }
    return;
  }
  request->session = session;
  request->endpointIdentity = session;
  observeSession(session);
  if (request->styleSheet) {
    QString content;
    if (m_buffer.isEncrypted()) {
      content = QString::fromUtf8(session == m_preferred ? m_preferredCss : m_protectedCss);
    } else if (auto *theme = m_services.get<ThemeService>()) {
      const auto file = theme->getFile(Theme::File::HighlightStyleSheet);
      if (!file.isEmpty()) {
        const auto error = FileUtils2::readTextFile(file, &content);
        if (error) {
          content.clear();
        }
      }
    }
    if (content.isEmpty()) {
      finish(p_request,
             errorResult(QStringLiteral("Markdown highlight stylesheet is unavailable")));
      return;
    }
    request->arguments.insert(QStringLiteral("source"), content);
  }
  const QPointer<MarkdownWebJobController> guard(this);
  request->transportId = session->submit(request->operation, request->arguments, this,
                                         [guard, p_request](const WebViewJobResult &p_result) {
                                           if (guard)
                                             guard->finish(p_request, p_result);
                                         });
  // The session owns the transport payload now. Do not retain a second plaintext copy.
  request->arguments = {};
  if (guard)
    notifyReadyEndpoint();
}

void MarkdownWebJobController::finish(quint64 p_request, const WebViewJobResult &p_result) {
  const auto request = m_requests.take(p_request);
  if (!request) {
    return;
  }
  if (!request->automaticKey.isEmpty() &&
      m_automaticRequests.value(request->automaticKey) == p_request) {
    m_automaticRequests.remove(request->automaticKey);
  }
  if (!m_destroying) {
    const bool current = request->generation == m_generation && contextIsCurrent();
    request->completion(current ? p_result : cancelledResult());
  }
}

void MarkdownWebJobController::cancel(quint64 p_request) {
  const auto request = m_requests.value(p_request);
  if (!request) {
    return;
  }
  const QPointer<MarkdownWebJobController> guard(this);
  if (request->session && !request->transportId.isEmpty()) {
    request->session->cancel(request->transportId);
  }
  if (guard)
    finish(p_request, cancelledResult());
}

void MarkdownWebJobController::cancelAll() {
  const auto requests = m_requests.keys();
  m_automaticRequests.clear();
  m_automaticStamps.clear();
  const QPointer<MarkdownWebJobController> guard(this);
  for (auto id : requests) {
    cancel(id);
    if (!guard)
      return;
  }
}

void MarkdownWebJobController::invalidate() {
  const auto scope = m_scope;
  QVector<QPointer<WebViewJobSession>> sessions;
  sessions.reserve(m_observed.size());
  for (auto it = m_observed.cbegin(); it != m_observed.cend(); ++it) {
    sessions.append(it.key());
  }
  ++m_generation;
  m_scope = m_scopePrefix + QString::number(m_generation);
  m_preferredEpoch = 0;
  m_readyEndpoint = nullptr;
  m_readyEndpointGeneration.clear();
  m_protected = nullptr;
  m_protectedCss.clear();
  m_protectedRequested = false;
  const QPointer<MarkdownWebJobController> guard(this);
  m_protectedRetiring = false;
  cancelAll();
  // FIFO cleanup waits for cancelled active work to drain, without capturing this controller.
  for (const auto &session : sessions) {
    if (session)
      releaseScope(session, scope);
  }
  if (guard)
    emit protectedSessionReleaseRequested();
}

void MarkdownWebJobController::revoke() {
  if (m_revoked) {
    return;
  }
  m_revoked = true;
  m_preferred = nullptr;
  m_preferredCss.clear();
  invalidate();
}

void MarkdownWebJobController::onProtectedLockingChanged(bool p_locking) {
  if (!m_buffer.isEncrypted() || m_revoked) {
    return;
  }
  m_locked = p_locking;
  if (p_locking) {
    invalidate();
  } else if (acceptsWebWork()) {
    emit ready();
  }
}

void MarkdownWebJobController::preview(bool p_math, quint64 p_id, TimeStamp p_timeStamp,
                                       const QString &p_language, const QString &p_text,
                                       qreal p_scale) {
  const auto operation =
      p_math ? QStringLiteral("markdown.previewMath") : QStringLiteral("markdown.previewGraph");
  QJsonObject arguments{{QStringLiteral("source"), p_text},
                        {QStringLiteral("scope"), m_scope},
                        {QStringLiteral("zoom"), p_scale},
                        {QStringLiteral("devicePixelRatio"), m_devicePixelRatio},
                        {QStringLiteral("width"), m_width}};
  if (!p_math) {
    arguments.insert(QStringLiteral("language"), p_language);
  }
  submit(
      operation, std::move(arguments),
      [this, p_math, p_id, p_timeStamp, operation](const WebViewJobResult &p_result) {
        if (p_result.status == WebViewJobResult::Status::Cancelled)
          return;
        if (p_result.status != WebViewJobResult::Status::Success || !p_result.value.isObject()) {
          emit operationFailed(operation, p_id, p_timeStamp, p_result.error);
          return;
        }
        const auto object = p_result.value.toObject();
        auto data = object.value(QStringLiteral("data")).toString().toUtf8();
        if (object.value(QStringLiteral("base64")).toBool()) {
          data = QByteArray::fromBase64(data);
        }
        auto format = object.value(QStringLiteral("format")).toString();
        if (m_buffer.isEncrypted() && !data.isEmpty()) {
          QByteArray image;
          QByteArray mime;
          const bool valid = ImageUtils::protectedImageData(data, image, mime);
          data.fill(0);
          if (!valid) {
            emit operationFailed(operation, p_id, p_timeStamp,
                                 QStringLiteral("Unsafe protected preview image"));
            return;
          }
          data = std::move(image);
          format = QString::fromLatin1(mime.mid(mime.indexOf('/') + 1));
        }
        const int width = object.value(QStringLiteral("logicalWidth")).toInt();
        const int height = object.value(QStringLiteral("logicalHeight")).toInt();
        const PreviewData result(p_id, p_timeStamp, format, data,
                                 object.value(QStringLiteral("needScale")).toBool(),
                                 width > 0 && height > 0 ? QSize(width, height) : QSize());
        if (p_math)
          emit mathPreviewDataReady(result);
        else
          emit graphPreviewDataReady(result);
      },
      automaticKey(operation, p_id), p_timeStamp);
}

void MarkdownWebJobController::previewGraph(quint64 p_id, TimeStamp p_timeStamp,
                                            const QString &p_language, const QString &p_text,
                                            qreal p_scale) {
  preview(false, p_id, p_timeStamp, p_language, p_text, p_scale);
}
void MarkdownWebJobController::previewMath(quint64 p_id, TimeStamp p_timeStamp,
                                           const QString &p_text, qreal p_scale) {
  preview(true, p_id, p_timeStamp, {}, p_text, p_scale);
}

void MarkdownWebJobController::highlight(bool p_math, int p_idx, quint64 p_timeStamp,
                                         const QString &p_text) {
  const auto operation =
      p_math ? QStringLiteral("markdown.highlightMath") : QStringLiteral("markdown.highlightCode");
  submit(
      operation, {{QStringLiteral("source"), p_text}},
      [this, p_math, p_idx, p_timeStamp, operation](const WebViewJobResult &p_result) {
        if (p_result.status == WebViewJobResult::Status::Cancelled)
          return;
        if (p_result.status != WebViewJobResult::Status::Success || !p_result.value.isString()) {
          emit operationFailed(operation, p_idx, p_timeStamp, p_result.error);
          return;
        }
        if (p_math)
          emit highlightMathReady(p_idx, p_timeStamp, p_result.value.toString());
        else
          emit highlightCodeBlockReady(p_idx, p_timeStamp, p_result.value.toString());
      },
      automaticKey(operation, quint64(p_idx)), p_timeStamp);
}
void MarkdownWebJobController::highlightCodeBlock(int p_idx, quint64 p_timeStamp,
                                                  const QString &p_text) {
  highlight(false, p_idx, p_timeStamp, p_text);
}
void MarkdownWebJobController::highlightMath(int p_idx, quint64 p_timeStamp,
                                             const QString &p_text) {
  highlight(true, p_idx, p_timeStamp, p_text);
}

void MarkdownWebJobController::htmlToMarkdown(quint64 p_id, quint64 p_timeStamp,
                                              const QString &p_html) {
  const auto operation = QStringLiteral("markdown.htmlToMarkdown");
  submit(operation, {{QStringLiteral("source"), p_html}},
         [this, p_id, p_timeStamp, operation](const WebViewJobResult &p_result) {
           if (p_result.status != WebViewJobResult::Status::Success || !p_result.value.isString()) {
             emit operationFailed(operation, p_id, p_timeStamp, p_result.error);
             return;
           }
           emit htmlToMarkdownReady(p_id, p_timeStamp, p_result.value.toString());
         });
}

void MarkdownWebJobController::fetchHighlightStyles(
    std::function<void(const QVector<CssRuleStyle> *)> p_callback) {
  submit(
      QStringLiteral("markdown.parseStyleSheet"), {},
      [callback = std::move(p_callback)](const WebViewJobResult &p_result) {
        if (p_result.status != WebViewJobResult::Status::Success || !p_result.value.isArray()) {
          callback(nullptr);
          return;
        }
        QVector<CssRuleStyle> rules;
        const auto values = p_result.value.toArray();
        rules.reserve(values.size());
        for (const auto &value : values) {
          rules.append(CssRuleStyle::fromJson(value.toObject()));
        }
        callback(&rules);
      },
      {}, 0, true);
}
