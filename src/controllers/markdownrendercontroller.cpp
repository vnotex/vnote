#include "markdownrendercontroller.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <utility>

#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

namespace {
constexpr size_t c_maxHeadingRequests = 8;
using Cancellation = std::shared_ptr<std::atomic_bool>;
using HeadingCompletion =
    std::function<void(const MarkdownRenderController::HeadingAnchorResult &)>;

struct NativeResult {
  // Destruction releases plaintext before dropping the protected-operation barrier.
  std::shared_ptr<ProtectedBufferLease> lease;
  MarkdownHtmlResult result;
};

MarkdownRenderController::HeadingAnchorResult cancelledHeading() {
  return {false, {}, QStringLiteral("Markdown heading request was cancelled"), true};
}
} // namespace

struct MarkdownRenderController::Input {
  QByteArray source;
  MarkdownHtmlOptions options;
  vte::MarkdownDocumentPtr document;

  MarkdownHtmlResult execute(const Cancellation &p_cancelled, bool p_emitHtml) {
    if (p_cancelled->load(std::memory_order_relaxed))
      return {};
    const int profile = MarkdownHtmlRenderer::parseOptions(options);
    if (!document || document->parseOptions() != profile || document->source() != source)
      document = vte::MarkdownDocument::parse(source, profile);
    // Parsing is not interruptible; never begin the render phase after cancellation.
    if (p_cancelled->load(std::memory_order_relaxed))
      return {};
    if (!document) {
      MarkdownHtmlResult result;
      result.error = QStringLiteral("Failed to parse Markdown document");
      return result;
    }
    auto result = MarkdownHtmlRenderer::render(source, options, document, p_emitHtml);
    if (p_cancelled->load(std::memory_order_relaxed))
      return {};
    return result;
  }

  static QFuture<NativeResult> run(Input p_input, Cancellation p_cancelled,
                                   std::shared_ptr<ProtectedBufferLease> p_lease, bool p_emitHtml) {
    return QtConcurrent::run([input = std::move(p_input), cancelled = std::move(p_cancelled),
                              lease = std::move(p_lease), p_emitHtml]() mutable {
      NativeResult output;
      output.lease = std::move(lease);
      try {
        output.result = input.execute(cancelled, p_emitHtml);
      } catch (...) {
        output.result.error = QStringLiteral("Failed to process Markdown document");
      }
      // Qt may retain the runnable after reporting completion. Leave no source/tree in it.
      input = {};
      return output;
    });
  }
};

struct MarkdownRenderController::PreviewRequest {
  std::shared_ptr<ProtectedBufferLease> lease;
  Input input;
  QString token;
  Cancellation cancelled = std::make_shared<std::atomic_bool>(false);
};

struct MarkdownRenderController::HeadingRequest {
  std::shared_ptr<ProtectedBufferLease> lease;
  Input input;
  int line = -1;
  HeadingCompletion completion;
  Cancellation cancelled = std::make_shared<std::atomic_bool>(false);
};

MarkdownRenderController::MarkdownRenderController(ServiceLocator &p_services,
                                                   const Buffer2 &p_buffer, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_buffer(p_buffer),
      m_tokenPrefix(QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char(':')) {
  if (auto *buffers = m_services.get<BufferService>()) {
    m_locked = m_buffer.isEncrypted() && buffers->isProtectedLocking();
    connect(buffers->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
            SLOT(onProtectedLockingChanged(bool)), Qt::DirectConnection);
  }
  if (auto *hooks = m_services.get<HookManager>()) {
    m_closeHook = hooks->addAction<ViewWindowCloseEvent>(
        HookNames::ViewWindowAfterClose,
        [this](HookContext &, const ViewWindowCloseEvent &p_event) {
          if (p_event.bufferId == m_buffer.id() && !contextIsCurrent())
            revoke();
        });
    m_notebookCloseHook = hooks->addAction<NotebookCloseEvent>(
        HookNames::NotebookAfterClose, [this](HookContext &, const NotebookCloseEvent &p_event) {
          if (p_event.notebookId == m_buffer.nodeId().notebookId)
            revoke();
        });
  }
}

MarkdownRenderController::~MarkdownRenderController() {
  m_destroying = true;
  if (auto *hooks = m_services.get<HookManager>()) {
    hooks->removeAction(m_closeHook);
    hooks->removeAction(m_notebookCloseHook);
  }
  revoke();
}

bool MarkdownRenderController::contextIsCurrent(
    std::shared_ptr<ProtectedBufferLease> *p_lease) const {
  if (m_destroying || m_revoked || m_locked || !m_buffer.isValid() ||
      m_buffer.getBuffer().isEmpty())
    return false;
  if (!m_buffer.isEncrypted())
    return true;
  // No new authorization while locking; only the already-admitted lease outlives the GUI call.
  if (auto *buffers = m_services.get<BufferService>(); buffers && buffers->isProtectedLocking())
    return false;
  auto lease = m_buffer.acquireProtectedLease();
  const bool authorized = bool(lease);
  if (p_lease)
    *p_lease = std::move(lease);
  return authorized;
}

bool MarkdownRenderController::acceptsWork(std::shared_ptr<ProtectedBufferLease> *p_lease) const {
  return !m_suspended && contextIsCurrent(p_lease);
}

bool MarkdownRenderController::isCurrent(const QString &p_token) const {
  return !p_token.isEmpty() && p_token == m_currentToken && acceptsWork();
}

bool MarkdownRenderController::isBusy() const {
  return m_deliveries != 0 || m_previewActive || m_previewPending || m_headingActive ||
         !m_headingsPending.empty();
}

QString MarkdownRenderController::render(const QByteArray &p_source,
                                         const MarkdownHtmlOptions &p_options,
                                         const vte::MarkdownDocumentPtr &p_document) {
  const auto token = m_tokenPrefix + QString::number(++m_nextToken);
  m_currentToken.clear();
  m_previewPending.reset();
  if (m_previewActive)
    m_previewActive->cancelled->store(true, std::memory_order_relaxed);
  std::shared_ptr<ProtectedBufferLease> lease;
  if (!acceptsWork(&lease))
    return {};

  m_currentToken = token;
  m_previewPending = std::make_unique<PreviewRequest>();
  m_previewPending->lease = std::move(lease);
  m_previewPending->input = {p_source, p_options, p_document};
  m_previewPending->token = token;
  m_wasBusy = true;
  startPreview();
  return token;
}

void MarkdownRenderController::fetchHeadingAnchor(const QByteArray &p_source, int p_line,
                                                  const MarkdownHtmlOptions &p_options,
                                                  const vte::MarkdownDocumentPtr &p_document,
                                                  HeadingCompletion p_callback) {
  if (!p_callback)
    return;
  std::shared_ptr<ProtectedBufferLease> lease;
  if (!acceptsWork(&lease)) {
    p_callback(cancelledHeading());
    return;
  }
  if (p_line < 0) {
    p_callback({false, {}, QStringLiteral("Invalid Markdown heading line"), false});
    return;
  }
  if (m_headingsPending.size() + (m_headingActive ? 1 : 0) >= c_maxHeadingRequests) {
    p_callback({false, {}, QStringLiteral("Too many pending Markdown heading requests"), false});
    return;
  }
  auto request = std::make_unique<HeadingRequest>();
  request->lease = std::move(lease);
  request->input = {p_source, p_options, p_document};
  request->line = p_line;
  request->completion = std::move(p_callback);
  m_headingsPending.push_back(std::move(request));
  m_wasBusy = true;
  startHeading();
}

void MarkdownRenderController::startPreview() {
  if (m_previewActive || !m_previewPending)
    return;
  if (!acceptsWork()) {
    invalidate();
    return;
  }
  m_previewActive = std::move(m_previewPending);
  auto *watcher = new QFutureWatcher<NativeResult>();
  const QPointer<MarkdownRenderController> guard(this);
  // The watcher owns its cleanup, not the view: receiver destruction cannot orphan inputs.
  connect(
      watcher, &QFutureWatcher<NativeResult>::finished, watcher,
      [watcher, guard]() {
        // takeResult releases the stored payload. Replacing the future with a default
        // finished future would enqueue another completion before deferred deletion.
        auto output = watcher->future().takeResult();
        watcher->deleteLater();
        if (guard)
          guard->finishPreview(std::move(output.result), std::move(output.lease));
      },
      Qt::SingleShotConnection);
  watcher->setFuture(Input::run(std::move(m_previewActive->input), m_previewActive->cancelled,
                                std::move(m_previewActive->lease), true));
}

void MarkdownRenderController::startHeading() {
  if (m_headingActive || m_headingsPending.empty())
    return;
  if (!acceptsWork()) {
    invalidate();
    return;
  }
  m_headingActive = std::move(m_headingsPending.front());
  m_headingsPending.pop_front();
  auto *watcher = new QFutureWatcher<NativeResult>();
  const QPointer<MarkdownRenderController> guard(this);
  connect(
      watcher, &QFutureWatcher<NativeResult>::finished, watcher,
      [watcher, guard]() {
        auto output = watcher->future().takeResult();
        watcher->deleteLater();
        if (guard)
          guard->finishHeading(std::move(output.result), std::move(output.lease));
      },
      Qt::SingleShotConnection);
  watcher->setFuture(Input::run(std::move(m_headingActive->input), m_headingActive->cancelled,
                                std::move(m_headingActive->lease), false));
}

void MarkdownRenderController::finishPreview(MarkdownHtmlResult p_result,
                                             std::shared_ptr<ProtectedBufferLease> p_lease) {
  ++m_deliveries;
  const auto token = std::move(m_previewActive->token);
  const bool cancelled = m_previewActive->cancelled->load(std::memory_order_relaxed);
  m_previewActive.reset();
  const QPointer<MarkdownRenderController> guard(this);
  if (!cancelled && isCurrent(token))
    emit rendered(token, p_result);
  p_result = {};
  p_lease.reset();
  if (!guard)
    return;
  --m_deliveries;
  startPreview();
  if (guard)
    notifyDrained();
}

void MarkdownRenderController::finishHeading(MarkdownHtmlResult p_result,
                                             std::shared_ptr<ProtectedBufferLease> p_lease) {
  ++m_deliveries;
  auto request = std::move(m_headingActive);
  auto completion = std::move(request->completion);
  HeadingAnchorResult anchor;
  if (request->cancelled->load(std::memory_order_relaxed) || !acceptsWork()) {
    anchor = cancelledHeading();
  } else {
    anchor.error = p_result.error;
    if (anchor.error.isEmpty()) {
      for (const auto &heading : p_result.headings) {
        if (heading.line == request->line) {
          anchor.found = true;
          anchor.anchor = heading.anchor;
          break;
        }
      }
    }
  }
  request.reset();
  p_result = {};
  const QPointer<MarkdownRenderController> guard(this);
  if (completion)
    completion(anchor);
  completion = {};
  anchor = {};
  p_lease.reset();
  if (!guard)
    return;
  --m_deliveries;
  startHeading();
  if (guard)
    notifyDrained();
}

void MarkdownRenderController::invalidate() {
  m_currentToken.clear();
  m_previewPending.reset();
  if (m_previewActive)
    m_previewActive->cancelled->store(true, std::memory_order_relaxed);

  std::deque<HeadingCompletion> completions;
  if (m_headingActive) {
    m_headingActive->cancelled->store(true, std::memory_order_relaxed);
    if (m_headingActive->completion)
      completions.push_back(std::move(m_headingActive->completion));
  }
  for (auto &request : m_headingsPending)
    completions.push_back(std::move(request->completion));
  m_headingsPending.clear();
  // Settle every explicit action once, even if its callback destroys the controller.
  const QPointer<MarkdownRenderController> guard(this);
  for (auto &completion : completions)
    completion(cancelledHeading());
  completions.clear();
  if (guard)
    notifyDrained();
}

void MarkdownRenderController::setSuspended(bool p_suspended) {
  if (m_suspended == p_suspended)
    return;
  m_suspended = p_suspended;
  if (m_suspended)
    invalidate();
}

void MarkdownRenderController::revoke() {
  if (m_revoked)
    return;
  m_revoked = true;
  invalidate();
}

void MarkdownRenderController::onProtectedLockingChanged(bool p_locking) {
  if (!m_buffer.isEncrypted() || m_revoked)
    return;
  m_locked = p_locking;
  if (p_locking)
    invalidate();
}

void MarkdownRenderController::notifyDrained() {
  if (m_wasBusy && !isBusy()) {
    m_wasBusy = false;
    if (!m_destroying)
      emit drained();
  }
}
