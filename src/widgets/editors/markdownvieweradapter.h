#ifndef MARKDOWNVIEWERADAPTER_H
#define MARKDOWNVIEWERADAPTER_H

#include "webviewadapter.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QSize>
#include <QString>

#include <optional>

#include <core/global.h>

namespace vte {
class TypstMathRenderer;
}

namespace vnotex {
class ServiceLocator;

// Adapter and interface between CPP and JS.
class MarkdownViewerAdapter : public WebViewAdapter {
  Q_OBJECT
  Q_PROPERTY(QJsonObject sectionNumberOptions READ getSectionNumberOptions NOTIFY
                 sectionNumberOptionsChanged)
public:
  struct Position {
    Position() = default;

    Position(int p_lineNumber, const QString &p_anchor);

    QJsonObject toJson() const;

    int m_lineNumber = -1;

    QString m_anchor;
  };

  struct Heading {
    Heading() = default;

    Heading(const QString &p_name, int p_level, const QString &p_anchor = QString());

    static Heading fromJson(const QJsonObject &p_obj);

    QString m_name;

    int m_level = -1;

    bool m_isPlaceholder = false;

    QString m_anchor;
  };

  explicit MarkdownViewerAdapter(QObject *p_parent = nullptr);

  // ServiceLocator-aware constructor for new architecture.
  explicit MarkdownViewerAdapter(ServiceLocator &p_services, QObject *p_parent = nullptr);

  virtual ~MarkdownViewerAdapter();

  void setProtectedView(bool p_protected);

  // Native render token; independent of buffer and editor revisions.
  void setRenderedHtml(QString p_generation, const QString &p_html, int p_lineNumber = -1);
  const QString &documentGeneration() const { return m_generation; }
  bool isDocumentReady() const {
    return isReady() && !m_generation.isEmpty() && m_finishedGeneration == m_generation;
  }

  void scrollToPosition(const Position &p_pos);

  int getTopLineNumber() const;

  const QVector<MarkdownViewerAdapter::Heading> &getHeadings() const;
  int getCurrentHeadingIndex() const;

  QJsonObject getSectionNumberOptions() const;
  void setSectionNumberOptions(bool p_enabled, const QString &p_pattern,
                               bool p_detectHeading1ForSectionNumber);
  bool getHeadingsHaveSectionNumber() const;

  void scrollToHeading(int p_idx);

  void scroll(bool p_up);

  const QStringList &getCrossCopyTargets() const;

  QString getCrossCopyTargetDisplayName(const QString &p_target) const;

  void setPresentationMode(bool p_active, bool p_darkTheme);

  void saveContent();

  quint64 requestImageView(const QString &p_url, const QSizeF &p_viewportHint = QSizeF());
  void cancelImageView();

  // Retire document commands and graph callbacks before WebViewer.setHtml().
  void reset();

  // Tell the web side that the toggle of the task list item at 0-based source
  // line @p_lineNumber was rejected.
  void emitTaskListToggleRejected(const QString &p_generation, int p_lineNumber);

  // Functions to be called from web side.
public slots:
  // Returns only a validated passive image data URL, or an empty string.
  QString protectedImageUrl(const QString &p_url);

  void activateProtectedLink(const QString &p_url);

  void setWorkFinished(const QString &p_generation);

  void setPresentationState(bool p_active, const QString &p_error);

  void setImageViewState(quint64 p_id, bool p_opened);

  // The line number at the top.
  void setTopLineNumber(int p_lineNumber);

  // Set the headings.
  void setHeadings(const QJsonArray &p_headings, bool p_hasSectionNumber);

  // Set current heading anchor.
  void setCurrentHeadingAnchor(int p_index, const QString &p_anchor);

  void setKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta);

  void zoom(bool p_zoomIn);

  void setCrossCopyTargets(const QJsonArray &p_targets);

  void setCrossCopyResult(quint64 p_id, quint64 p_timeStamp, const QString &p_html);

  void setSavedContent(const QString &p_generation, const QString &p_headContent,
                       const QString &p_styleContent, const QString &p_content,
                       const QString &p_bodyClassList);

  void onPdfRenderReady();

  // Call local CPP code to render graph.
  void renderGraph(quint64 p_id, quint64 p_index, const QString &p_format, const QString &p_lang,
                   const QString &p_text, int p_imageIndex);

  void renderTypstMath(const QString &p_id, const QString &p_scope, const QString &p_source,
                       bool p_display, const QString &p_foreground);

  void cancelTypstMath(const QString &p_scope);

  // Web side requests to toggle the task list item at 0-based source line
  // @p_lineNumber to @p_checked.
  void toggleTaskListItem(const QString &p_generation, int p_lineNumber, bool p_checked);

  // Signals to be connected at web side.
signals:
  void sectionNumberOptionsChanged();

  void presentationModeRequested(bool p_active, bool p_darkTheme);

  void imageViewRequested(quint64 p_id, const QString &p_url, qreal p_width, qreal p_height);
  void imageViewCancelRequested(quint64 p_id);

  // A native HTML fragment is ready for the current page.
  void htmlUpdated(const QString &p_html, const QString &p_generation);

  // Current editor line number is updated.
  void editLineNumberUpdated(int p_lineNumber);

  void anchorScrollRequested(const QString &p_anchor);

  void scrollRequested(bool p_up);

  void crossCopyRequested(quint64 p_id, quint64 p_timeStamp, const QString &p_target,
                          const QString &p_baseUrl, const QString &p_html);

  // Request to get the whole HTML content.
  void contentRequested(const QString &p_generation);

  void graphRenderDataReady(quint64 p_id, quint64 p_index, const QString &p_format,
                            const QString &p_data, bool p_success);

  void typstMathReady(const QString &p_id, const QJsonObject &p_result);

  // The toggle of the task list item at 0-based source line @p_lineNumber was
  // rejected; the web side should restore the checkbox state.
  void taskListToggleRejected(const QString &p_generation, int p_lineNumber);

  // Signals to be connected at cpp side.
signals:
  void presentationStateChanged(bool p_active, const QString &p_error);

  void imageViewStateChanged(quint64 p_id, bool p_opened);
  void imageViewCancelled(quint64 p_id);

  // All rendering work has finished.
  void workFinished();

  void headingsChanged();

  void currentHeadingChanged();

  void keyPressed(int p_key, bool p_ctrl, bool p_shift, bool p_meta);

  void zoomed(bool p_zoomIn);

  void crossCopyReady(quint64 p_id, quint64 p_timeStamp, const QString &p_html);

  void contentReady(const QString &p_headContent, const QString &p_styleContent,
                    const QString &p_content, const QString &p_bodyClassList);

  void pdfRenderReady();

  // Web side requested to toggle a task list item.
  void taskListItemToggleRequested(const QString &p_generation, int p_lineNumber, bool p_checked);

  void protectedLinkRequested(const QString &p_url);

private:
  struct TypstMathRequest {
    QString m_id;
    QString m_scope;
    QString m_source;
    QColor m_foreground;
    quint64 m_resetGeneration = 0;
    qsizetype m_sourceBytes = 0;
    bool m_display = false;
  };

  void startNextTypstMath();

  // Empty scope retires all work, or only reader work when p_readersOnly is true.
  void retireTypstMath(const QString &p_scope = QString(), bool p_readersOnly = false);

  void dispatchHtml(QString p_generation);

  void scrollToLine(int p_lineNumber);

  void scrollToAnchor(const QString &p_anchor);

  bool m_protectedView = false;

  // Never reset the sequence on page reload; delayed web replies must stay obsolete.
  quint64 m_imageViewSequence = 0;
  quint64 m_imageViewRequest = 0;
  QString m_pendingImageViewUrl;

  QString m_generation;
  QString m_finishedGeneration;
  QString m_pendingHtml;
  int m_pendingLineNumber = -1;
  bool m_contentRequested = false;
  bool m_htmlActionPending = false;
  bool m_hasPendingHtml = false;
  // Owned by dispatchHtml(), including across reentrant document resets.
  bool m_dispatchingHtml = false;

  // Source line number of the top element node at web side.
  int m_topLineNumber = -1;

  // Headings from web side.
  QVector<Heading> m_headings;
  bool m_headingsHaveSectionNumber = false;

  // Per-view options survive document resets and page reloads.
  bool m_autoSectionNumberEnabled = false;
  QString m_sectionNumberPattern = QStringLiteral("1.1.");
  bool m_detectHeading1ForSectionNumber = true;

  int m_currentHeadingIndex = -1;

  // Targets supported by cross copy. Set by web.
  QStringList m_crossCopyTargets;

  vte::TypstMathRenderer *m_typstMathRenderer = nullptr;
  std::optional<TypstMathRequest> m_typstMathActive;
  QList<TypstMathRequest> m_typstMathQueue;
  // Includes detached cancellations until each one's terminal signal is emitted.
  QSet<QString> m_typstMathIds;
  qsizetype m_typstMathQueuedBytes = 0;
  // A cancelled owner can be gone while the facade still owes its terminal callback.
  bool m_typstMathInFlight = false;
  bool m_retiringTypstMath = false;

  ServiceLocator *m_services = nullptr; // Non-owning; set by ServiceLocator-aware constructor
};
} // namespace vnotex

#endif // MARKDOWNVIEWERADAPTER_H
