#include "webviewadapter.h"

#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include <utility>

using namespace vnotex;

QJsonObject WebViewAdapter::FindOption::toJson() const {
  QJsonObject obj;
  obj["findBackward"] = m_findBackward;
  obj["caseSensitive"] = m_caseSensitive;
  obj["wholeWordOnly"] = m_wholeWordOnly;
  obj["regularExpression"] = m_regularExpression;
  return obj;
}

WebViewAdapter::WebViewAdapter(QObject *p_parent) : QObject(p_parent) {}

void WebViewAdapter::setReady(bool p_ready) {
  if (!p_ready) {
    m_ready = false;
    ++m_resetGeneration;
    m_pendingActions.clear();
    m_callbackPool.clear();
    return;
  }
  if (m_ready) {
    return;
  }

  m_ready = true;
  const auto generation = m_resetGeneration;
  const QPointer<WebViewAdapter> guard(this);
  const auto actions = std::exchange(m_pendingActions, {});
  for (const auto &act : actions) {
    act();
    if (!guard || guard->getResetGeneration() != generation) {
      return;
    }
  }
  emit ready();
}

void WebViewAdapter::pendAction(const std::function<void()> &p_func) {
  Q_ASSERT(!m_ready);
  m_pendingActions.append(p_func);
}

bool WebViewAdapter::isReady() const { return m_ready; }

void WebViewAdapter::invokeCallback(quint64 p_id, void *p_data) {
  m_callbackPool.call(p_id, p_data);
}

quint64 WebViewAdapter::addCallback(const CallbackPool::Callback &p_callback) {
  return m_callbackPool.add(p_callback);
}

void WebViewAdapter::findText(const QStringList &p_texts, FindOptions p_options,
                              int p_currentMatchLine) {
  FindOption opts;
  if (p_options & vnotex::FindOption::FindBackward) {
    opts.m_findBackward = true;
  }
  if (p_options & vnotex::FindOption::CaseSensitive) {
    opts.m_caseSensitive = true;
  }
  if (p_options & vnotex::FindOption::WholeWordOnly) {
    opts.m_wholeWordOnly = true;
  }
  if (p_options & vnotex::FindOption::RegularExpression) {
    opts.m_regularExpression = true;
  }

  if (isReady()) {
    emit findTextRequested(p_texts, opts.toJson(), p_currentMatchLine);
  } else {
    const auto generation = getResetGeneration();
    pendAction([this, generation, p_texts, opts, p_currentMatchLine]() {
      // Let page initialization finish clearing highlights without a nested event loop.
      QTimer::singleShot(1000, this, [this, generation, p_texts, opts, p_currentMatchLine]() {
        if (generation != getResetGeneration() || !isReady()) {
          return;
        }
        emit findTextRequested(p_texts, opts.toJson(), p_currentMatchLine);
      });
    });
  }
}

void WebViewAdapter::setFindText(const QStringList &p_texts, int p_totalMatches,
                                 int p_currentMatchIndex) {
  emit findTextReady(p_texts, p_totalMatches, p_currentMatchIndex);
}
