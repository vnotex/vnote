#ifndef PREVIEWHELPER_H
#define PREVIEWHELPER_H

#include <QElapsedTimer>
#include <QObject>
#include <QPixmap>
#include <QVector>

#include <vtextedit/global.h>
#include <vtextedit/lrucache.h>
#include <vtextedit/markdownhighlighterdata.h>
#include <vtextedit/previewmgr.h>

#include "graphpreviewdata.h"
#include "markdownvieweradapter.h"
#include <core/global.h>

class QTimer;
class QTextDocument;

namespace vnotex {
class MarkdownEditor;

// Helper to manage in-place preview and focus preview.
class PreviewHelper : public QObject {
  Q_OBJECT
public:
  enum SourceFlag {
    FlowChart = 0x1,
    Mermaid = 0x2,
    WaveDrom = 0x4,
    PlantUml = 0x8,
    Graphviz = 0x10,
    Math = 0x20
  };
  Q_DECLARE_FLAGS(SourceFlags, SourceFlag);

  PreviewHelper(MarkdownEditor *p_editor, QObject *p_parent = nullptr);

  void setMarkdownEditor(MarkdownEditor *p_editor);

  void setWebPlantUmlEnabled(bool p_enabled);

  void setWebGraphvizEnabled(bool p_enabled);

  void setInplacePreviewSources(SourceFlags p_srcs);

  void setInplacePreviewCodeBlocksEnabled(bool p_enabled);

  void setInplacePreviewMathBlocksEnabled(bool p_enabled);

  // Pure helper: the zoom ratio of the editor, clamped to [0.25, 4.0].
  // Returns 1.0 when @p_baseFontPointSize is not positive.
  static qreal editorZoomRatio(int p_fontPointSize, int p_baseFontPointSize);

  // Current editor zoom ratio. 1.0 when there is no editor.
  qreal editorZoomFactor() const;

  // Notify that the editor zoom changed, to re-render the in-place previews.
  void editorZoomChanged();

public slots:
  void codeBlocksUpdated(vte::TimeStamp p_timeStamp,
                         const QVector<vte::md::FencedCodeBlock> &p_codeBlocks);

  void mathBlocksUpdated(const QVector<vte::md::MathBlock> &p_mathBlocks);

  void handleGraphPreviewData(const MarkdownViewerAdapter::PreviewData &p_data);

  void handleMathPreviewData(const MarkdownViewerAdapter::PreviewData &p_data);

signals:
  // Request to preview graph.
  // There must be a corresponding call to handleGraphPreviewData().
  // @p_scale: the editor zoom ratio (without any DPI factor).
  void graphPreviewRequested(quint64 p_id, TimeStamp p_timeStamp, const QString &p_lang,
                             const QString &p_text, qreal p_scale);

  // Request to preview math.
  // There must be a corresponding call to handleMathPreviewData().
  // @p_scale: the editor zoom ratio (without any DPI factor).
  void mathPreviewRequested(quint64 p_id, TimeStamp p_timeStamp, const QString &p_text,
                            qreal p_scale);

  // Request to do in-place preview for @p_previewItems.
  void
  inplacePreviewCodeBlockUpdated(const QVector<QSharedPointer<vte::PreviewItem>> &p_previewItems);

  // Request to do in-place preview for @p_previewItems.
  void
  inplacePreviewMathBlockUpdated(const QVector<QSharedPointer<vte::PreviewItem>> &p_previewItems);

  void potentialObsoletePreviewBlocksUpdated(const QList<int> &p_blocks);

private:
  // Preview data of each related code block.
  struct CodeBlockPreviewData {
    CodeBlockPreviewData() = default;

    CodeBlockPreviewData(const vte::md::FencedCodeBlock &p_codeBlock);

    void updateInplacePreview(QTextDocument *p_doc, const QPixmap &p_image,
                              const QString &p_imageName, QRgb p_background, int p_tabStopWidth);

    // Start and end block of the fenced code block.
    int m_startBlock = 0;
    int m_endBlock = 0;

    QString m_lang;

    // Including the fence text.
    // Will be filled only when preview is needed.
    QString m_text;

    QSharedPointer<vte::PreviewItem> m_inplacePreview;
  };

  struct MathBlockPreviewData {
    MathBlockPreviewData() = default;

    MathBlockPreviewData(const vte::md::MathBlock &p_mathBlock);

    void updateInplacePreview(QTextDocument *p_doc, const QPixmap &p_image,
                              const QString &p_imageName, int p_tabStopWidth);

    // Block number for in-place preview.
    int m_blockNumber = -1;

    // Whether it should be previewed as block or not.
    bool m_previewedAsBlock = false;

    // Start index wihtin block with number m_blockNumber, including the start mark.
    int m_index = -1;

    // Length of this math in block with number m_blockNumber, including the end mark.
    int m_length = -1;

    // Including the guarding marks.
    // Will be filled only when preview is needed.
    QString m_text;

    QSharedPointer<vte::PreviewItem> m_inplacePreview;
  };

  // Return <InplacePreview, FocusPreview>.
  QPair<bool, bool> isLangNeedPreview(const QString &p_lang) const;

  bool isInplacePreviewSourceEnabled(SourceFlag p_flag) const;

  bool checkPreviewSourceLang(SourceFlag p_flag, const QString &p_lang) const;

  // Inplace preview code block m_codeBlocksData[@p_blockPreviewIdx].
  void inplacePreviewCodeBlock(int p_blockPreviewIdx);

  void inplacePreviewMathBlock(int p_blockPreviewIdx);

  void updateEditorInplacePreviewCodeBlock();

  void updateEditorInplacePreviewMathBlock();

  void handleLocalData(quint64 p_id, TimeStamp p_timeStamp, const QString &p_format,
                       const QString &p_data, bool p_forcedBackground);

  // @p_zoomRatio: the generation's zoom ratio.
  qreal getEditorScaleFactor(qreal p_zoomRatio) const;

  bool needForcedBackground(const QString &p_lang) const;

  void handleCodeBlocksUpdate();

  void handleMathBlocksUpdate();

  // Request a debounced publication of the in-place previews. Async results
  // arrive one at a time (one per diagram/math block), and each publication
  // costs a full document relayout; coalescing the ones that land close
  // together keeps the editor from churning for seconds on a preview-heavy
  // document. The publish always emits the WHOLE current data set, so a delayed
  // publish is inherently the latest state.
  void requestUpdateEditorInplacePreviewCodeBlock();

  void requestUpdateEditorInplacePreviewMathBlock();

  MarkdownEditor *m_editor = nullptr;

  QTextDocument *m_document = nullptr;

  // Need to init it in the constructor.
  SourceFlags m_inplacePreviewSources;

  bool m_inplacePreviewCodeBlocksEnabled = true;

  bool m_inplacePreviewMathBlocksEnabled = true;

  TimeStamp m_codeBlockTimeStamp = 0;

  TimeStamp m_mathBlockTimeStamp = 0;

  // Zoom ratio snapshotted once per generation.
  qreal m_codeBlockRequestZoomRatio = 1;

  qreal m_mathBlockRequestZoomRatio = 1;

  // Sorted by startBlock in ascending order.
  QVector<CodeBlockPreviewData> m_codeBlocksData;

  QVector<MathBlockPreviewData> m_mathBlocksData;

  // Tab stop width of the editor, used for block margin calculation.
  int m_tabStopWidth = 4;

  // To record the size of previous inplace preview of code block.
  int m_previousInplacePreviewCodeBlockSize = 0;

  // To record the size of previous inplace preview of math block.
  int m_previousInplacePreviewMathBlockSize = 0;

  // {text} -> GraphPreviewData.
  vte::LruCache<QString, QSharedPointer<GraphPreviewData>> m_codeBlockCache;

  vte::LruCache<QString, QSharedPointer<GraphPreviewData>> m_mathBlockCache;

  bool m_webPlantUmlEnabled = true;

  bool m_webGraphvizEnabled = true;

  QVector<vte::md::FencedCodeBlock> m_pendingCodeBlocks;

  QTimer *m_codeBlockTimer = nullptr;

  QVector<vte::md::MathBlock> m_pendingMathBlocks;

  QTimer *m_mathBlockTimer = nullptr;

  // Debounce timers for PUBLISHING preview results (as opposed to the
  // request-side m_codeBlockTimer/m_mathBlockTimer above).
  QTimer *m_codeBlockPublishTimer = nullptr;

  QTimer *m_mathBlockPublishTimer = nullptr;

  // ---------------------------------------------------------------------
  // Diagnostics for the edit-mode in-place preview pipeline.
  // Measurement only: nothing below changes behaviour, and every field is
  // written only while lcPerfPreview is debug-enabled. See
  // .kilo/plans/1788310000000-trace-edit-mode-preview-perf.md.
  // ---------------------------------------------------------------------
  struct PreviewPerfStats {
    // The generation this data belongs to. A zoom or an edit bumps
    // m_codeBlockTimeStamp and starts a new batch while the old one drains;
    // keying on the timestamp keeps two passes from being averaged together.
    TimeStamp m_timeStamp = 0;

    qint64 m_generationStartMs = 0;
    // Wall clock, so these records can be overlaid with the markers logged by
    // MarkdownViewWindow2 and with the JS-side summary.
    qint64 m_generationStartEpochMs = 0;
    int m_blocks = 0;
    int m_needPreview = 0;
    int m_cacheHits = 0;
    int m_cacheMisses = 0;
    int m_dispatched = 0;
    qint64 m_dispatchMs = 0;

    // Number of times codeBlocksUpdated() restarted the 1000ms request debounce
    // before the generation actually started.
    int m_requestRestarts = 0;

    int m_results = 0;
    int m_failures = 0;
    qint64 m_lastResultMs = 0;
    QVector<qint64> m_roundTripMs;
    QVector<qint64> m_decodeMs;
    QVector<qint64> m_handlerMs;
    QVector<int> m_payloadBytes;

    // Publish-side accounting. Starts and restarts are counted separately: an
    // unconditional counter would also count the first start and every start
    // after a publish, which is exactly the distinction H1 turns on.
    int m_publishStarts = 0;
    int m_publishRestarts = 0;
    // Publications that actually emitted (relayouted) vs. every call into
    // updateEditorInplacePreviewCodeBlock(), including the synchronous one
    // handleCodeBlocksUpdate() always makes and the no-op early returns.
    int m_publishes = 0;
    int m_publishInvocations = 0;
    qint64 m_publishDeadlineMs = -1;
    qint64 m_publishMaxLatenessMs = 0;
    qint64 m_publishTotalMs = 0;
    qint64 m_emitPreviewMs = 0;
    qint64 m_emitObsoleteMs = 0;

    qint64 m_heartbeatMaxLatenessMs = 0;
    int m_heartbeatTicks = 0;
  };

  bool perfEnabled() const;

  // Elapsed milliseconds on the monotonic diagnostics clock.
  qint64 perfNowMs() const;

  void perfBeginGeneration();

  void perfStartHeartbeat();

  // Called on every result; (re)arms the idle timer that prints the summary.
  void perfNoteActivity();

  void perfReportSummary();

  QElapsedTimer m_perfClock;

  PreviewPerfStats m_perfStats;

  // Request-debounce restarts observed before the next generation starts.
  int m_perfPendingRequestRestarts = 0;

  // Guards against a generation being summarized twice.
  bool m_perfReported = true;

  // Bounded retries while the batch is still draining.
  int m_perfIdleAttempts = 0;

  // Dispatch time per code block preview index, for round-trip latency.
  QVector<qint64> m_perfRequestMs;

  // 100ms tick whose own lateness measures GUI-thread stalls. A blocked GUI
  // thread and a slow renderer are indistinguishable without it.
  QTimer *m_perfHeartbeatTimer = nullptr;

  qint64 m_perfHeartbeatLastMs = 0;

  // There is no "last node" to key off on the edit path (a not-ready adapter
  // silently drops graphPreviewRequested), so the summary is printed on an idle
  // timer instead of on completion.
  QTimer *m_perfSummaryTimer = nullptr;
};
} // namespace vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::PreviewHelper::SourceFlags)

#endif // PREVIEWHELPER_H
