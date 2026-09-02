#include "previewhelper.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

#include <algorithm>

#include <vtextedit/texteditorconfig.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/textutils.h>
#include <vtextedit/vtextedit.h>

#include "graphvizhelper.h"
#include "markdowneditor.h"
#include "plantumlhelper.h"
#include "previewdispatchplanner.h"
#include "previewscaleutils.h"
#include <core/logging.h>

using namespace vnotex;

PreviewHelper::CodeBlockPreviewData::CodeBlockPreviewData(
    const vte::md::FencedCodeBlock &p_codeBlock)
    : m_startBlock(p_codeBlock.m_startBlock), m_endBlock(p_codeBlock.m_endBlock),
      m_lang(p_codeBlock.m_lang) {}

void PreviewHelper::CodeBlockPreviewData::updateInplacePreview(QTextDocument *p_doc,
                                                               const QPixmap &p_image,
                                                               const QString &p_imageName,
                                                               QRgb p_background,
                                                               int p_tabStopWidth) {
  const auto block = p_doc->findBlockByNumber(m_endBlock);
  if (block.isValid()) {
    m_inplacePreview.reset(new vte::PreviewItem());
    m_inplacePreview->m_startPos = block.position();
    m_inplacePreview->m_endPos = m_inplacePreview->m_startPos + block.length();
    m_inplacePreview->m_blockPos = m_inplacePreview->m_startPos;
    m_inplacePreview->m_blockNumber = m_endBlock;
    m_inplacePreview->m_padding = vte::PreviewMgr::calculateBlockMargin(block, p_tabStopWidth);
    m_inplacePreview->m_name = p_imageName;
    m_inplacePreview->m_backgroundColor = p_background;
    m_inplacePreview->m_isBlockwise = true;
    m_inplacePreview->m_image = p_image;
  } else {
    m_inplacePreview.clear();
  }
}

PreviewHelper::MathBlockPreviewData::MathBlockPreviewData(const vte::md::MathBlock &p_mathBlock)
    : m_blockNumber(p_mathBlock.m_blockNumber), m_previewedAsBlock(p_mathBlock.m_previewedAsBlock),
      m_index(p_mathBlock.m_index), m_length(p_mathBlock.m_length) {}

void PreviewHelper::MathBlockPreviewData::updateInplacePreview(QTextDocument *p_doc,
                                                               const QPixmap &p_image,
                                                               const QString &p_imageName,
                                                               int p_tabStopWidth) {
  const auto block = p_doc->findBlockByNumber(m_blockNumber);
  if (block.isValid()) {
    m_inplacePreview.reset(new vte::PreviewItem());
    m_inplacePreview->m_startPos = block.position() + m_index;
    m_inplacePreview->m_endPos = m_inplacePreview->m_startPos + m_length;
    m_inplacePreview->m_blockPos = block.position();
    m_inplacePreview->m_blockNumber = m_blockNumber;
    m_inplacePreview->m_padding = vte::PreviewMgr::calculateBlockMargin(block, p_tabStopWidth);
    m_inplacePreview->m_name = p_imageName;
    m_inplacePreview->m_isBlockwise = m_previewedAsBlock;
    m_inplacePreview->m_image = p_image;
  } else {
    m_inplacePreview.clear();
  }
}

PreviewHelper::PreviewHelper(MarkdownEditor *p_editor, QObject *p_parent)
    : QObject(p_parent),
      m_inplacePreviewSources(SourceFlag::FlowChart | SourceFlag::Mermaid | SourceFlag::WaveDrom |
                              SourceFlag::PlantUml | SourceFlag::Graphviz | SourceFlag::Math),
      m_codeBlockCache(100, nullptr), m_mathBlockCache(100, nullptr) {
  setMarkdownEditor(p_editor);

  const int interval = 1000;
  m_codeBlockTimer = new QTimer(this);
  m_codeBlockTimer->setSingleShot(true);
  m_codeBlockTimer->setInterval(interval);
  connect(m_codeBlockTimer, &QTimer::timeout, this, &PreviewHelper::handleCodeBlocksUpdate);

  m_mathBlockTimer = new QTimer(this);
  m_mathBlockTimer->setSingleShot(true);
  m_mathBlockTimer->setInterval(interval);
  connect(m_mathBlockTimer, &QTimer::timeout, this, &PreviewHelper::handleMathBlocksUpdate);

  // Response-side debounce. Async preview results arrive one per block; without
  // this, N diagrams cause N relayouts of the whole document.
  const int publishInterval = 50;
  m_codeBlockPublishTimer = new QTimer(this);
  m_codeBlockPublishTimer->setSingleShot(true);
  m_codeBlockPublishTimer->setInterval(publishInterval);
  connect(m_codeBlockPublishTimer, &QTimer::timeout, this,
          &PreviewHelper::updateEditorInplacePreviewCodeBlock);

  m_mathBlockPublishTimer = new QTimer(this);
  m_mathBlockPublishTimer->setSingleShot(true);
  m_mathBlockPublishTimer->setInterval(publishInterval);
  connect(m_mathBlockPublishTimer, &QTimer::timeout, this,
          &PreviewHelper::updateEditorInplacePreviewMathBlock);

  // Diagnostics only. Both timers stay stopped unless lcPerfPreview is enabled.
  m_perfClock.start();

  m_perfHeartbeatTimer = new QTimer(this);
  m_perfHeartbeatTimer->setInterval(100);
  connect(m_perfHeartbeatTimer, &QTimer::timeout, this, [this]() {
    const auto now = perfNowMs();
    const auto lateness = now - m_perfHeartbeatLastMs - m_perfHeartbeatTimer->interval();
    m_perfHeartbeatLastMs = now;
    ++m_perfStats.m_heartbeatTicks;
    if (lateness > m_perfStats.m_heartbeatMaxLatenessMs) {
      m_perfStats.m_heartbeatMaxLatenessMs = lateness;
    }
  });

  m_perfSummaryTimer = new QTimer(this);
  m_perfSummaryTimer->setSingleShot(true);
  m_perfSummaryTimer->setInterval(2000);
  connect(m_perfSummaryTimer, &QTimer::timeout, this, &PreviewHelper::perfReportSummary);
}

bool PreviewHelper::perfEnabled() const { return lcPerfPreview().isDebugEnabled(); }

qint64 PreviewHelper::perfNowMs() const { return m_perfClock.elapsed(); }

void PreviewHelper::perfBeginGeneration() {
  // A previous generation may still have a summary pending. Report it (or drop
  // it if it never started) BEFORE the record is overwritten, so two passes are
  // never averaged together.
  if (m_perfSummaryTimer->isActive()) {
    m_perfSummaryTimer->stop();
    perfReportSummary();
  }
  m_perfHeartbeatTimer->stop();

  m_perfStats = PreviewPerfStats();
  // The request debounce restarts belong to the generation they delayed, and
  // are consumed exactly once here.
  m_perfStats.m_requestRestarts = m_perfPendingRequestRestarts;
  m_perfPendingRequestRestarts = 0;
  m_perfStats.m_timeStamp = m_codeBlockTimeStamp;
  m_perfStats.m_generationStartMs = perfNowMs();
  m_perfStats.m_generationStartEpochMs = QDateTime::currentMSecsSinceEpoch();
  m_perfReported = false;
  m_perfIdleAttempts = 0;
  m_perfRequestMs.clear();
}

void PreviewHelper::perfStartHeartbeat() {
  if (!m_perfHeartbeatTimer->isActive()) {
    m_perfHeartbeatLastMs = perfNowMs();
    m_perfHeartbeatTimer->start();
  }
}

void PreviewHelper::perfNoteActivity() {
  // Armed on COMPLETION only. Arming it on dispatch would let a first result
  // slower than the idle interval print a summary of a still-running batch.
  m_perfSummaryTimer->start();
  perfStartHeartbeat();
}

// One completion of a dispatched request, whatever served it. A failed or empty
// result still closes a request and must be counted, or the summary would wait
// for a completion that is never coming.
void PreviewHelper::perfNoteResult(quint64 p_id, qint64 p_entryMs, qint64 p_decodeMs, int p_bytes,
                                   bool p_failed) {
  ++m_perfStats.m_results;
  if (p_failed) {
    ++m_perfStats.m_failures;
  } else {
    m_perfStats.m_decodeMs.append(p_decodeMs);
    m_perfStats.m_payloadBytes.append(p_bytes);
  }
  m_perfStats.m_lastResultMs = p_entryMs;

  const qint64 requestMs =
      (p_id < static_cast<quint64>(m_perfRequestMs.size())) ? m_perfRequestMs[p_id] : 0;
  if (requestMs > 0) {
    // Request dispatch -> handler entry. Subtracting the JS-side compute+raster
    // duration reported by graphpreviewer.js leaves the transport plus
    // GUI-thread queueing cost, which is H5.
    m_perfStats.m_roundTripMs.append(p_entryMs - requestMs);
  }

  perfNoteActivity();
}

namespace {
// min / p50 / p90 / max of a copy of @p_values, as a single log-friendly string.
//
// Nearest-rank on a sorted array: idx = min(n - 1, floor(p * n)). The SAME rule
// is used by GraphPreviewer.perfQuantiles() and GraphRenderer.reportTiming() in
// the web layer, because these three summaries are meant to be read side by
// side - a p50 must mean one thing across all of them. Change one, change all
// three.
QString perfQuartiles(QVector<qint64> p_values) {
  if (p_values.isEmpty()) {
    return QStringLiteral("n/a");
  }
  std::sort(p_values.begin(), p_values.end());
  const auto count = p_values.size();
  const auto at = [&p_values, count](double p) {
    auto idx = static_cast<decltype(count)>(p * static_cast<double>(count));
    if (idx < 0) {
      idx = 0;
    } else if (idx > count - 1) {
      idx = count - 1;
    }
    return p_values[idx];
  };
  return QStringLiteral("min=%1 p50=%2 p90=%3 max=%4")
      .arg(p_values.first())
      .arg(at(0.5))
      .arg(at(0.9))
      .arg(p_values.last());
}
} // namespace

void PreviewHelper::perfReportSummary() {
  if (!perfEnabled() || m_perfReported || m_perfStats.m_results == 0) {
    m_perfHeartbeatTimer->stop();
    return;
  }

  if (m_perfStats.m_results < m_perfStats.m_dispatched && m_perfIdleAttempts < 5) {
    // Still draining. Wait for another idle interval rather than finalizing a
    // partial batch.
    ++m_perfIdleAttempts;
    m_perfSummaryTimer->start();
    return;
  }

  m_perfHeartbeatTimer->stop();
  m_perfReported = true;

  const bool incomplete = m_perfStats.m_results < m_perfStats.m_dispatched;
  if (incomplete) {
    // A dispatched request never came back. Reported as its own diagnostic so
    // it is never mistaken for a completed batch: graphPreviewRequested is
    // silently dropped when the adapter is not ready.
    qCDebug(lcPerfPreview) << "inplace preview INCOMPLETE ts=" << m_perfStats.m_timeStamp
                           << "dispatched=" << m_perfStats.m_dispatched
                           << "results=" << m_perfStats.m_results;
  }

  qint64 bytes = 0;
  for (auto b : m_perfStats.m_payloadBytes) {
    bytes += b;
  }

  qCDebug(lcPerfPreview) << "inplace preview summary ts=" << m_perfStats.m_timeStamp
                         << "startEpochMs=" << m_perfStats.m_generationStartEpochMs
                         << "blocks=" << m_perfStats.m_blocks
                         << "needPreview=" << m_perfStats.m_needPreview
                         << "cacheHit=" << m_perfStats.m_cacheHits
                         << "cacheMiss=" << m_perfStats.m_cacheMisses
                         << "dispatched=" << m_perfStats.m_dispatched
                         << "dispatchMs=" << m_perfStats.m_dispatchMs
                         << "requestRestarts=" << m_perfStats.m_requestRestarts
                         << "results=" << m_perfStats.m_results
                         << "failures=" << m_perfStats.m_failures << "firstToLastMs="
                         << (m_perfStats.m_lastResultMs - m_perfStats.m_generationStartMs)
                         << "payloadBytes=" << bytes << "roundTripMs["
                         << perfQuartiles(m_perfStats.m_roundTripMs) << "]"
                         << "pixmapDecodeMs[" << perfQuartiles(m_perfStats.m_decodeMs) << "]"
                         << "handlerMs[" << perfQuartiles(m_perfStats.m_handlerMs) << "]"
                         << "publishStarts=" << m_perfStats.m_publishStarts
                         << "publishRestarts=" << m_perfStats.m_publishRestarts
                         << "publishes=" << m_perfStats.m_publishes
                         << "publishInvocations=" << m_perfStats.m_publishInvocations
                         << "publishMaxLatenessMs=" << m_perfStats.m_publishMaxLatenessMs
                         << "publishTotalMs=" << m_perfStats.m_publishTotalMs
                         << "emitPreviewMs=" << m_perfStats.m_emitPreviewMs
                         << "emitObsoleteMs=" << m_perfStats.m_emitObsoleteMs
                         << "guiHeartbeatTicks=" << m_perfStats.m_heartbeatTicks
                         << "guiHeartbeatMaxLatenessMs=" << m_perfStats.m_heartbeatMaxLatenessMs;
}

void PreviewHelper::requestUpdateEditorInplacePreviewCodeBlock() {
  if (perfEnabled()) {
    // Read isActive() BEFORE start(): start() on an active single-shot timer
    // resets its deadline, and restart-vs-start is the whole of H1.
    if (m_codeBlockPublishTimer->isActive()) {
      ++m_perfStats.m_publishRestarts;
    } else {
      ++m_perfStats.m_publishStarts;
    }
    m_perfStats.m_publishDeadlineMs = perfNowMs() + m_codeBlockPublishTimer->interval();
  }
  m_codeBlockPublishTimer->start();
}

void PreviewHelper::requestUpdateEditorInplacePreviewMathBlock() {
  m_mathBlockPublishTimer->start();
}

void PreviewHelper::codeBlocksUpdated(vte::TimeStamp p_timeStamp,
                                      const QVector<vte::md::FencedCodeBlock> &p_codeBlocks) {
  Q_UNUSED(p_timeStamp);
  if (!m_inplacePreviewCodeBlocksEnabled) {
    return;
  }

  m_pendingCodeBlocks = p_codeBlocks;
  if (perfEnabled()) {
    if (m_codeBlockTimer->isActive()) {
      ++m_perfPendingRequestRestarts;
    } else {
      qCDebug(lcPerfPreview) << "codeBlocksUpdated first ts=" << p_timeStamp
                             << "codeBlocks=" << p_codeBlocks.size()
                             << "atMs=" << QDateTime::currentMSecsSinceEpoch();
    }
  }
  m_codeBlockTimer->start();
}

void PreviewHelper::handleCodeBlocksUpdate() {
  ++m_codeBlockTimeStamp;
  m_codeBlockRequestZoomRatio = editorZoomFactor();
  m_codeBlocksData.clear();

  const bool perf = perfEnabled();
  if (perf) {
    perfBeginGeneration();
  }

  QVector<PreviewDispatchRequest> needPreviewBlocks;

  for (int i = 0; i < m_pendingCodeBlocks.size(); ++i) {
    const auto &cb = m_pendingCodeBlocks[i];

    const auto needPreview = isLangNeedPreview(cb.m_lang);
    if (!needPreview.first && !needPreview.second) {
      continue;
    }

    m_codeBlocksData.append(CodeBlockPreviewData(cb));
    const int blockPreviewIdx = m_codeBlocksData.size() - 1;

    bool cacheHit = false;
    // Take a copy of the shared pointer: mutating through it updates the cached
    // entry in place, since LruCache stores the same shared pointer.
    auto cachedData = m_codeBlockCache.get(cb.m_text);
    if (cachedData) {
      const auto action = PreviewScaleUtils::cacheAction(
          cachedData->m_needScale, cachedData->m_appliedZoomRatio, m_codeBlockRequestZoomRatio);
      if (action == PreviewScaleUtils::CacheAction::Rerasterize) {
        // Re-render locally from the retained payload. No webview, no process
        // spawn, no network.
        cachedData->rasterize(getEditorScaleFactor(m_codeBlockRequestZoomRatio));
        cachedData->m_appliedZoomRatio = m_codeBlockRequestZoomRatio;
      }

      if (action != PreviewScaleUtils::CacheAction::Miss) {
        cacheHit = true;
        cachedData->m_timeStamp = m_codeBlockTimeStamp;
        m_codeBlocksData[blockPreviewIdx].updateInplacePreview(
            m_document, cachedData->m_image, cachedData->m_name, cachedData->m_background,
            m_tabStopWidth);
      }
    }

    if (m_inplacePreviewCodeBlocksEnabled && needPreview.first && !cacheHit) {
      m_codeBlocksData[blockPreviewIdx].m_text = cb.m_text;
      needPreviewBlocks.append({blockPreviewIdx, cb.m_startBlock, cb.m_endBlock});
    }

    if (perf) {
      ++m_perfStats.m_needPreview;
      if (cacheHit) {
        ++m_perfStats.m_cacheHits;
      } else {
        ++m_perfStats.m_cacheMisses;
      }
    }
  }

  if (perf) {
    m_perfStats.m_blocks = m_pendingCodeBlocks.size();
    m_perfStats.m_dispatched = needPreviewBlocks.size();
    m_perfRequestMs.fill(0, m_codeBlocksData.size());
    qCDebug(lcPerfPreview) << "handleCodeBlocksUpdate ts=" << m_codeBlockTimeStamp
                           << "blocks=" << m_perfStats.m_blocks
                           << "needPreview=" << m_perfStats.m_needPreview
                           << "cacheHit=" << m_perfStats.m_cacheHits
                           << "cacheMiss=" << m_perfStats.m_cacheMisses
                           << "dispatching=" << needPreviewBlocks.size()
                           << "atMs=" << m_perfStats.m_generationStartEpochMs;
  }

  QPair<int, int> visibleRange(-1, -1);
  auto *textEdit = m_editor ? m_editor->getTextEdit() : nullptr;
  auto *viewport = textEdit ? textEdit->viewport() : nullptr;
  auto *window = textEdit ? textEdit->window() : nullptr;
  if (m_editor && textEdit && m_document && viewport && window && m_document->documentLayout() &&
      textEdit->document() == m_document && m_editor->isVisible() && textEdit->isVisible() &&
      viewport->isVisible() && window->isVisible() && viewport->width() > 0 &&
      viewport->height() > 0 && !(window->windowState() & Qt::WindowMinimized)) {
    visibleRange = vte::TextEditUtils::visibleBlockRange(textEdit);
  }
  const auto dispatchRequests =
      PreviewDispatchPlanner::visibleFirst(needPreviewBlocks, m_document, visibleRange);

  const qint64 dispatchStartMs = perf ? perfNowMs() : 0;
  for (const auto &request : dispatchRequests) {
    if (perf && request.m_previewId < m_perfRequestMs.size()) {
      m_perfRequestMs[request.m_previewId] = perfNowMs();
    }
    inplacePreviewCodeBlock(request.m_previewId);
  }

  if (perf) {
    m_perfStats.m_dispatchMs = perfNowMs() - dispatchStartMs;
    if (!needPreviewBlocks.isEmpty()) {
      // Start the GUI heartbeat at dispatch (a stall before the first result is
      // exactly what it is there to catch), but leave the idle summary timer
      // unarmed until a result actually lands.
      perfStartHeartbeat();
    }
  }

  updateEditorInplacePreviewCodeBlock();

  m_pendingCodeBlocks.clear();
}

bool PreviewHelper::checkPreviewSourceLang(SourceFlag p_flag, const QString &p_lang) const {
  switch (p_flag) {
  case SourceFlag::FlowChart:
    return p_lang == QStringLiteral("flow") || p_lang == QStringLiteral("flowchart");

  case SourceFlag::WaveDrom:
    return p_lang == QStringLiteral("wavedrom");

  case SourceFlag::Mermaid:
    return p_lang == QStringLiteral("mermaid");

  case SourceFlag::PlantUml:
    return p_lang == QStringLiteral("puml") || p_lang == QStringLiteral("plantuml");

  case SourceFlag::Graphviz:
    return p_lang == QStringLiteral("dot") || p_lang == QStringLiteral("graphviz");

  case SourceFlag::Math:
    return p_lang == QStringLiteral("mathjax");
  }

  return false;
}

QPair<bool, bool> PreviewHelper::isLangNeedPreview(const QString &p_lang) const {
  QPair<bool, bool> res(false, false);
  if ((isInplacePreviewSourceEnabled(SourceFlag::FlowChart) &&
       checkPreviewSourceLang(SourceFlag::FlowChart, p_lang)) ||
      (isInplacePreviewSourceEnabled(SourceFlag::WaveDrom) &&
       checkPreviewSourceLang(SourceFlag::WaveDrom, p_lang)) ||
      (isInplacePreviewSourceEnabled(SourceFlag::Mermaid) &&
       checkPreviewSourceLang(SourceFlag::Mermaid, p_lang)) ||
      (isInplacePreviewSourceEnabled(SourceFlag::PlantUml) &&
       checkPreviewSourceLang(SourceFlag::PlantUml, p_lang)) ||
      (isInplacePreviewSourceEnabled(SourceFlag::Graphviz) &&
       checkPreviewSourceLang(SourceFlag::Graphviz, p_lang)) ||
      (isInplacePreviewSourceEnabled(SourceFlag::Math) &&
       checkPreviewSourceLang(SourceFlag::Math, p_lang))) {
    res.first = res.second = true;
  }
  return res;
}

bool PreviewHelper::isInplacePreviewSourceEnabled(SourceFlag p_flag) const {
  return m_inplacePreviewSources & p_flag;
}

void PreviewHelper::inplacePreviewCodeBlock(int p_blockPreviewIdx) {
  const auto &blockData = m_codeBlocksData[p_blockPreviewIdx];
  Q_ASSERT(!blockData.m_text.isEmpty());
  if (checkPreviewSourceLang(SourceFlag::FlowChart, blockData.m_lang) ||
      checkPreviewSourceLang(SourceFlag::WaveDrom, blockData.m_lang) ||
      checkPreviewSourceLang(SourceFlag::Mermaid, blockData.m_lang) ||
      (checkPreviewSourceLang(SourceFlag::PlantUml, blockData.m_lang) && m_webPlantUmlEnabled) ||
      (checkPreviewSourceLang(SourceFlag::Graphviz, blockData.m_lang) && m_webGraphvizEnabled) ||
      checkPreviewSourceLang(SourceFlag::Math, blockData.m_lang)) {
    emit graphPreviewRequested(p_blockPreviewIdx, m_codeBlockTimeStamp, blockData.m_lang,
                               vte::TextUtils::removeCodeBlockFence(blockData.m_text),
                               m_codeBlockRequestZoomRatio);
    return;
  }

  if (!m_webPlantUmlEnabled && checkPreviewSourceLang(SourceFlag::PlantUml, blockData.m_lang)) {
    // Local PlantUml.
    // Per-preview: gated, so it costs nothing unless the category is enabled.
    qCDebug(lcUi) << "PreviewHelper: local PlantUml in-place preview idx=" << p_blockPreviewIdx
                  << "lang=" << blockData.m_lang;
    PlantUmlHelper::getInst().process(
        static_cast<quint64>(p_blockPreviewIdx), m_codeBlockTimeStamp, QStringLiteral("svg"),
        vte::TextUtils::removeCodeBlockFence(blockData.m_text), this,
        [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data) {
          handleLocalData(id, timeStamp, format, data, true);
        });
    return;
  }

  if (!m_webGraphvizEnabled && checkPreviewSourceLang(SourceFlag::Graphviz, blockData.m_lang)) {
    // Local PlantUml.
    GraphvizHelper::getInst().process(
        static_cast<quint64>(p_blockPreviewIdx), m_codeBlockTimeStamp, QStringLiteral("svg"),
        vte::TextUtils::removeCodeBlockFence(blockData.m_text), this,
        [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data) {
          handleLocalData(id, timeStamp, format, data, false);
        });
    return;
  }
}

void PreviewHelper::handleGraphPreviewData(const MarkdownViewerAdapter::PreviewData &p_data) {
  const bool perf = perfEnabled();
  const qint64 entryMs = perf ? perfNowMs() : 0;

  if (p_data.m_timeStamp != m_codeBlockTimeStamp) {
    // A result from a retired generation. Deliberately NOT counted into the
    // current record: mixing it in is exactly the generation-mixing failure the
    // per-timestamp keying exists to prevent.
    return;
  }
  if (p_data.m_id >= static_cast<quint64>(m_codeBlocksData.size()) || p_data.m_data.isEmpty()) {
    if (perf) {
      perfNoteResult(p_data.m_id, entryMs, 0, 0, true);
    }
    requestUpdateEditorInplacePreviewCodeBlock();
    return;
  }

  auto &blockData = m_codeBlocksData[p_data.m_id];
  const bool forcedBackground = needForcedBackground(blockData.m_lang);
  const qint64 decodeStartMs = perf ? perfNowMs() : 0;
  auto previewData = QSharedPointer<GraphPreviewData>::create(
      p_data.m_timeStamp, p_data.m_format, p_data.m_data, p_data.m_needScale,
      forcedBackground ? m_editor->getPreviewBackground() : 0,
      p_data.m_needScale ? getEditorScaleFactor(m_codeBlockRequestZoomRatio) : 1,
      m_codeBlockRequestZoomRatio);
  const qint64 decodeMs = perf ? perfNowMs() - decodeStartMs : 0;
  m_codeBlockCache.set(blockData.m_text, previewData);
  blockData.m_text.clear();

  blockData.updateInplacePreview(m_document, previewData->m_image, previewData->m_name,
                                 previewData->m_background, m_tabStopWidth);

  if (perf) {
    perfNoteResult(p_data.m_id, entryMs, decodeMs, p_data.m_data.size(), false);
  }

  requestUpdateEditorInplacePreviewCodeBlock();

  if (perf) {
    m_perfStats.m_handlerMs.append(perfNowMs() - entryMs);
  }
}

void PreviewHelper::updateEditorInplacePreviewCodeBlock() {
  const bool perf = perfEnabled();
  const qint64 publishStartMs = perf ? perfNowMs() : 0;
  if (perf) {
    ++m_perfStats.m_publishInvocations;
    if (m_perfStats.m_publishDeadlineMs >= 0) {
      const qint64 lateness = publishStartMs - m_perfStats.m_publishDeadlineMs;
      if (lateness > m_perfStats.m_publishMaxLatenessMs) {
        m_perfStats.m_publishMaxLatenessMs = lateness;
      }
      m_perfStats.m_publishDeadlineMs = -1;
    }
  }

  // Any pending debounced publication is subsumed by this one: the publish
  // always emits the whole current m_codeBlocksData, so letting a queued timer
  // fire afterwards would only repeat the relayout.
  m_codeBlockPublishTimer->stop();

  QSet<int> obsoleteBlocks;
  QVector<QSharedPointer<vte::PreviewItem>> previewItems;
  previewItems.reserve(m_codeBlocksData.size());
  for (const auto &blockData : m_codeBlocksData) {
    if (blockData.m_inplacePreview) {
      if (!blockData.m_inplacePreview->m_image.isNull()) {
        previewItems.append(blockData.m_inplacePreview);
      } else {
        obsoleteBlocks.insert(blockData.m_inplacePreview->m_blockNumber);
      }
    } else {
      obsoleteBlocks.insert(blockData.m_endBlock);
    }
  }

  // Hint the capacity before the early return below: entries now retain their
  // source payload, so a cache grown for a preview-heavy document must still be
  // able to shrink after the document stops having previews at all. The early
  // return would otherwise starve LruCache's shrink hysteresis of the repeated
  // low hints it needs.
  m_codeBlockCache.setCapacityHint(m_codeBlocksData.size());

  if (previewItems.isEmpty() && m_previousInplacePreviewCodeBlockSize == 0) {
    if (perf) {
      m_perfStats.m_publishTotalMs += perfNowMs() - publishStartMs;
    }
    return;
  }

  const qint64 emitPreviewStartMs = perf ? perfNowMs() : 0;
  if (perf) {
    // Counted here, past the early return above, so it means "a publication
    // that actually relayouts" - which is the H1 signature.
    ++m_perfStats.m_publishes;
  }
  emit inplacePreviewCodeBlockUpdated(previewItems);
  if (perf) {
    // Connected with no explicit type between two GUI-thread objects, so
    // PreviewMgr::updateCodeBlocks() and the relayout it drives run INSIDE this
    // emit (H7).
    m_perfStats.m_emitPreviewMs += perfNowMs() - emitPreviewStartMs;
  }

  m_previousInplacePreviewCodeBlockSize = previewItems.size();

  if (!obsoleteBlocks.isEmpty()) {
    const qint64 emitObsoleteStartMs = perf ? perfNowMs() : 0;
    emit potentialObsoletePreviewBlocksUpdated(obsoleteBlocks.values());
    if (perf) {
      m_perfStats.m_emitObsoleteMs += perfNowMs() - emitObsoleteStartMs;
    }
  }

  if (perf) {
    m_perfStats.m_publishTotalMs += perfNowMs() - publishStartMs;
  }
}

void PreviewHelper::setMarkdownEditor(MarkdownEditor *p_editor) {
  Q_ASSERT(!m_editor);
  m_editor = p_editor;
  if (m_editor) {
    m_document = m_editor->document();
    m_tabStopWidth = m_editor->getConfig().m_tabStopWidth;
  }
}

void PreviewHelper::mathBlocksUpdated(const QVector<vte::md::MathBlock> &p_mathBlocks) {
  if (!m_inplacePreviewMathBlocksEnabled || !isInplacePreviewSourceEnabled(SourceFlag::Math)) {
    return;
  }

  m_pendingMathBlocks = p_mathBlocks;
  m_mathBlockTimer->start();
}

void PreviewHelper::handleMathBlocksUpdate() {
  ++m_mathBlockTimeStamp;
  m_mathBlockRequestZoomRatio = editorZoomFactor();
  m_mathBlocksData.clear();
  m_mathBlocksData.reserve(m_pendingMathBlocks.size());

  bool needUpdateEditorInplacePreview = true;

  for (const auto &mb : m_pendingMathBlocks) {
    m_mathBlocksData.append(MathBlockPreviewData(mb));
    const int blockPreviewIdx = m_mathBlocksData.size() - 1;

    bool cacheHit = false;
    // Take a copy of the shared pointer: mutating through it updates the cached
    // entry in place, since LruCache stores the same shared pointer.
    auto cachedData = m_mathBlockCache.get(mb.m_text);
    if (cachedData) {
      const auto action = PreviewScaleUtils::cacheAction(
          cachedData->m_needScale, cachedData->m_appliedZoomRatio, m_mathBlockRequestZoomRatio);
      if (action == PreviewScaleUtils::CacheAction::Rerasterize) {
        cachedData->rasterize(getEditorScaleFactor(m_mathBlockRequestZoomRatio));
        cachedData->m_appliedZoomRatio = m_mathBlockRequestZoomRatio;
      }

      if (action != PreviewScaleUtils::CacheAction::Miss) {
        cacheHit = true;
        cachedData->m_timeStamp = m_mathBlockTimeStamp;
        m_mathBlocksData[blockPreviewIdx].updateInplacePreview(m_document, cachedData->m_image,
                                                               cachedData->m_name, m_tabStopWidth);
      }
    }

    if (!cacheHit) {
      needUpdateEditorInplacePreview = false;
      m_mathBlocksData[blockPreviewIdx].m_text = mb.m_text;
      inplacePreviewMathBlock(blockPreviewIdx);
    }
  }

  if (needUpdateEditorInplacePreview) {
    updateEditorInplacePreviewMathBlock();
  }

  m_pendingMathBlocks.clear();
}

void PreviewHelper::inplacePreviewMathBlock(int p_blockPreviewIdx) {
  const auto &blockData = m_mathBlocksData[p_blockPreviewIdx];
  Q_ASSERT(!blockData.m_text.isEmpty());
  emit mathPreviewRequested(p_blockPreviewIdx, m_mathBlockTimeStamp, blockData.m_text,
                            m_mathBlockRequestZoomRatio);
}

void PreviewHelper::updateEditorInplacePreviewMathBlock() {
  // See updateEditorInplacePreviewCodeBlock().
  m_mathBlockPublishTimer->stop();

  QSet<int> obsoleteBlocks;
  QVector<QSharedPointer<vte::PreviewItem>> previewItems;
  previewItems.reserve(m_mathBlocksData.size());
  for (const auto &blockData : m_mathBlocksData) {
    if (blockData.m_inplacePreview) {
      if (!blockData.m_inplacePreview->m_image.isNull()) {
        previewItems.append(blockData.m_inplacePreview);
      } else {
        obsoleteBlocks.insert(blockData.m_inplacePreview->m_blockNumber);
      }
    } else {
      obsoleteBlocks.insert(blockData.m_blockNumber);
    }
  }

  // See the note in updateEditorInplacePreviewCodeBlock(): hint before the
  // early return so the cache can shrink again once previews go away.
  m_mathBlockCache.setCapacityHint(m_mathBlocksData.size());

  if (previewItems.isEmpty() && m_previousInplacePreviewMathBlockSize == 0) {
    return;
  }

  emit inplacePreviewMathBlockUpdated(previewItems);

  m_previousInplacePreviewMathBlockSize = previewItems.size();

  if (!obsoleteBlocks.isEmpty()) {
    emit potentialObsoletePreviewBlocksUpdated(obsoleteBlocks.values());
  }
}

void PreviewHelper::handleMathPreviewData(const MarkdownViewerAdapter::PreviewData &p_data) {
  if (p_data.m_timeStamp != m_mathBlockTimeStamp) {
    return;
  }
  if (p_data.m_id >= static_cast<quint64>(m_mathBlocksData.size()) || p_data.m_data.isEmpty()) {
    requestUpdateEditorInplacePreviewMathBlock();
    return;
  }

  auto &blockData = m_mathBlocksData[p_data.m_id];
  auto previewData = QSharedPointer<GraphPreviewData>::create(
      p_data.m_timeStamp, p_data.m_format, p_data.m_data, p_data.m_needScale, 0,
      p_data.m_needScale ? getEditorScaleFactor(m_mathBlockRequestZoomRatio) : 1,
      m_mathBlockRequestZoomRatio);
  m_mathBlockCache.set(blockData.m_text, previewData);
  blockData.m_text.clear();

  blockData.updateInplacePreview(m_document, previewData->m_image, previewData->m_name,
                                 m_tabStopWidth);

  requestUpdateEditorInplacePreviewMathBlock();
}

qreal PreviewHelper::getEditorScaleFactor(qreal p_zoomRatio) const {
  const qreal dpiFactor = m_editor ? m_editor->getConfig().m_scaleFactor : 1;
  return PreviewScaleUtils::rasterFactor(dpiFactor, p_zoomRatio);
}

qreal PreviewHelper::editorZoomRatio(int p_fontPointSize, int p_baseFontPointSize) {
  return PreviewScaleUtils::zoomRatio(p_fontPointSize, p_baseFontPointSize);
}

qreal PreviewHelper::editorZoomFactor() const {
  if (!m_editor) {
    return 1;
  }

  return editorZoomRatio(m_editor->editorFontPointSize(), m_editor->baseEditorFontPointSize());
}

void PreviewHelper::editorZoomChanged() {
  const qreal ratio = editorZoomFactor();
  if (!PreviewScaleUtils::isZoomRatioStale(m_codeBlockRequestZoomRatio, ratio) &&
      !PreviewScaleUtils::isZoomRatioStale(m_mathBlockRequestZoomRatio, ratio)) {
    return;
  }

  // Invalidate every in-flight response immediately, so a pre-zoom raster can
  // never be cached against the new ratio.
  //
  // A publication that is only DEBOUNCED has already passed its timestamp
  // check and stored its raster, so bumping the generation cannot stop it: the
  // timer callback carries no generation of its own. Cancel it here instead.
  // refreshPreviewHighlight() below starts a new generation, which publishes
  // the correctly scaled set when it settles.
  m_codeBlockPublishTimer->stop();
  m_mathBlockPublishTimer->stop();

  ++m_codeBlockTimeStamp;
  ++m_mathBlockTimeStamp;

  // The generation is retired here, so retire its diagnostics with it: a
  // pending summary would otherwise keep accumulating heartbeat ticks and then
  // report a batch that no longer exists.
  if (perfEnabled()) {
    if (m_perfSummaryTimer->isActive()) {
      m_perfSummaryTimer->stop();
      perfReportSummary();
    }
    m_perfHeartbeatTimer->stop();
  }

  if (m_editor) {
    m_editor->refreshPreviewHighlight();
  }
}

void PreviewHelper::setWebPlantUmlEnabled(bool p_enabled) { m_webPlantUmlEnabled = p_enabled; }

void PreviewHelper::setWebGraphvizEnabled(bool p_enabled) { m_webGraphvizEnabled = p_enabled; }

void PreviewHelper::handleLocalData(quint64 p_id, TimeStamp p_timeStamp, const QString &p_format,
                                    const QString &p_data, bool p_forcedBackground) {
  const bool perf = perfEnabled();
  const qint64 entryMs = perf ? perfNowMs() : 0;

  if (p_timeStamp != m_codeBlockTimeStamp) {
    return;
  }

  Q_UNUSED(p_format);
  Q_ASSERT(p_format == QStringLiteral("svg"));

  if (p_id >= static_cast<quint64>(m_codeBlocksData.size()) || p_data.isEmpty()) {
    if (perf) {
      perfNoteResult(p_id, entryMs, 0, 0, true);
    }
    requestUpdateEditorInplacePreviewCodeBlock();
    return;
  }

  auto &blockData = m_codeBlocksData[p_id];
  const qint64 decodeStartMs = perf ? perfNowMs() : 0;
  auto previewData = QSharedPointer<GraphPreviewData>::create(
      p_timeStamp, p_format, p_data.toUtf8(), true,
      p_forcedBackground ? m_editor->getPreviewBackground() : 0,
      getEditorScaleFactor(m_codeBlockRequestZoomRatio), m_codeBlockRequestZoomRatio);
  const qint64 decodeMs = perf ? perfNowMs() - decodeStartMs : 0;
  m_codeBlockCache.set(blockData.m_text, previewData);
  blockData.m_text.clear();

  blockData.updateInplacePreview(m_document, previewData->m_image, previewData->m_name,
                                 previewData->m_background, m_tabStopWidth);

  if (perf) {
    // Locally served blocks are counted into m_dispatched exactly like the web
    // ones, so they must close here too - otherwise a run with local PlantUml
    // or Graphviz would always be summarized as INCOMPLETE.
    perfNoteResult(p_id, entryMs, decodeMs, p_data.size(), false);
  }

  requestUpdateEditorInplacePreviewCodeBlock();

  if (perf) {
    m_perfStats.m_handlerMs.append(perfNowMs() - entryMs);
  }
}

bool PreviewHelper::needForcedBackground(const QString &p_lang) const {
  if (checkPreviewSourceLang(SourceFlag::PlantUml, p_lang)) {
    return true;
  }

  return false;
}

void PreviewHelper::setInplacePreviewSources(SourceFlags p_srcs) {
  m_inplacePreviewSources = p_srcs;
}

void PreviewHelper::setInplacePreviewCodeBlocksEnabled(bool p_enabled) {
  m_inplacePreviewCodeBlocksEnabled = p_enabled;
}

void PreviewHelper::setInplacePreviewMathBlocksEnabled(bool p_enabled) {
  m_inplacePreviewMathBlocksEnabled = p_enabled;
}
