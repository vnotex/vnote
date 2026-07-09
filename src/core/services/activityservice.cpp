#include "activityservice.h"

#include <QGuiApplication>
#include <QTimer>

#include "core/hookevents.h"
#include "core/hooknames.h"
#include "core/services/hookmanager.h"

#include <vxcore/vxcore.h>

namespace vnotex {

// Periodic focus-time flush cadence. A crash therefore loses <= this window of
// active time.
static constexpr int kFocusFlushIntervalMs = 60 * 1000;

ActivityService::ActivityService(VxCoreContextHandle p_context, HookManager *p_hookManager,
                                 QObject *p_parent)
    : QObject(p_parent), m_context(p_context), m_hookManager(p_hookManager) {
  m_flushTimer = new QTimer(this);
  m_flushTimer->setInterval(kFocusFlushIntervalMs);
  connect(m_flushTimer, &QTimer::timeout, this, &ActivityService::flushFocusDelta);

  // Track application activation state to compute focused wall-time.
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          &ActivityService::onApplicationStateChanged);

  // Forward "note read" into vxcore on every file open (best-effort, raw count).
  if (m_hookManager) {
    m_fileAfterOpenHookId = m_hookManager->addAction<FileOpenEvent>(
        HookNames::FileAfterOpen, [this](HookContext &, const FileOpenEvent &p_event) {
          if (!m_context || p_event.notebookId.isEmpty()) {
            return;
          }
          vxcore_activity_record_read(m_context, p_event.notebookId.toUtf8().constData(),
                                      p_event.filePath.toUtf8().constData());
        });

    // Flush pending focus delta when the main window is closing.
    m_mainWindowBeforeCloseHookId = m_hookManager->addAction(
        HookNames::MainWindowBeforeClose,
        [this](HookContext &, const QVariantMap &) { flush(); });
  }

  // Seed focus state from the current application state so time spent focused
  // before the first state change is not lost.
  if (qApp->applicationState() == Qt::ApplicationActive) {
    beginFocus();
  }
}

ActivityService::~ActivityService() {
  flush();
  if (m_hookManager) {
    if (m_fileAfterOpenHookId != -1) {
      m_hookManager->removeAction(m_fileAfterOpenHookId);
    }
    if (m_mainWindowBeforeCloseHookId != -1) {
      m_hookManager->removeAction(m_mainWindowBeforeCloseHookId);
    }
  }
}

void ActivityService::onApplicationStateChanged(Qt::ApplicationState p_state) {
  if (p_state == Qt::ApplicationActive) {
    beginFocus();
  } else {
    endFocus();
  }
}

void ActivityService::beginFocus() {
  if (m_focused) {
    return;
  }
  m_focused = true;
  m_elapsed.start();
  m_flushTimer->start();
}

void ActivityService::endFocus() {
  if (!m_focused) {
    return;
  }
  flushFocusDelta();
  m_focused = false;
  m_flushTimer->stop();
}

void ActivityService::flushFocusDelta() {
  if (!m_context) {
    return;
  }
  // Push the focused-time delta (accumulated in vxcore memory) ...
  if (m_focused && m_elapsed.isValid()) {
    // restart() returns ms since last start/restart and resets the baseline.
    const qint64 deltaMs = m_elapsed.restart();
    if (deltaMs > 0) {
      vxcore_activity_add_focus_time(m_context, static_cast<int64_t>(deltaMs));
    }
  }
  // ... then persist all pending in-memory activity (focus + reads + edits from
  // events) to activity.db in one batched transaction. Recording itself is
  // in-memory and cheap; this periodic/on-close flush is the only disk write.
  vxcore_activity_flush(m_context);
}

void ActivityService::flush() { flushFocusDelta(); }

} // namespace vnotex
