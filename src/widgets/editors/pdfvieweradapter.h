#ifndef PDFVIEWERADAPTER_H
#define PDFVIEWERADAPTER_H

#include "webviewadapter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace vnotex {
// Adapter and interface between CPP and JS for PDF.
class PdfViewerAdapter : public WebViewAdapter {
  Q_OBJECT
public:
  // One entry of the PDF's embedded outline (bookmark tree), flattened by a
  // pre-order DFS at the web side.
  //
  // Only @m_name and @m_level are published upward into the generic outline
  // stack (Outline::Heading carries nothing else). @m_index is the private
  // handle back to a position in the document, mirroring how
  // MarkdownViewerAdapter::Heading keeps @m_anchor to itself.
  struct Heading {
    Heading() = default;

    // Two-arg ctor is REQUIRED by OutlineProvider::makePerfectHeadings, which
    // synthesizes "[EMPTY]" filler headings for skipped levels. @m_index stays
    // -1 so a filler can never map to a destination.
    Heading(const QString &p_name, int p_level);

    Heading(const QString &p_name, int p_level, int p_index);

    static Heading fromJson(const QJsonObject &p_obj);

    QString m_name;

    // Heading level, 1-based. -1 means "invalid" — either absent/garbage in the
    // payload, or outside the accepted range (see the clamp in fromJson()).
    int m_level = -1;

    // Index into the web side's destination array; -1 means "not jumpable"
    // (a filler heading, or a bookmark carrying url/action/attachment/
    // setOCGState instead of dest).
    int m_index = -1;
  };

  explicit PdfViewerAdapter(QObject *p_parent = nullptr);

  ~PdfViewerAdapter() = default;

  void setUrl(const QString &p_url);

  const QVector<Heading> &getOutlineHeadings() const;

  // Drop the current outline. Called across a page reload, since the adapter
  // outlives the web page and WebViewAdapter::setReady() early-returns when it
  // is already ready, so nothing else would clear the stale headings.
  void clearOutline();

  // Ask the web side to jump to the destination of heading @p_idx.
  void scrollToOutlineItem(int p_idx);

  // ============ Comments ============

  // Publish the CURRENT comment set to the overlay.
  //
  // This is a REPLACE, not an append, and it is latch-based rather than
  // queued: only the newest snapshot is ever delivered. That matters across a
  // page reload — the adapter outlives the page, so a set published while the
  // replacement page is still loading would otherwise target a destroyed
  // document and never be republished. PdfViewWindow2 drives the adapter to
  // not-ready before every load, and the false->true transition flushes exactly
  // one publish of whatever the latest set is.
  void setComments(const QJsonArray &p_comments);

  const QJsonArray &getComments() const;

  // Ask the overlay to scroll to (and flash) a comment.
  void scrollToComment(const QString &p_id);

  // Ask the overlay to turn the CURRENT text selection into a comment, in
  // @p_color. This is the discoverable counterpart of the Alt+drag shortcut:
  // the page context menu drives it, so highlighting does not depend on the
  // user guessing a modifier key.
  void captureSelection(const QString &p_color);

  // The active authoring tool. Modelled on pdf.js's own toolbar because a MODE
  // is what makes highlighting cheap: arm it once and every selection is
  // captured, instead of a per-selection menu round trip.
  enum class Tool {
    None,      // plain reading; selection means "copy this text"
    Highlight, // a completed selection becomes a pdf-quads comment
    Ink,       // freehand drawing becomes a pdf-ink comment
    FreeText   // click places a pdf-freetext comment
  };

  void setTool(Tool p_tool);

  Tool getTool() const;

  // Colour used by every tool. Applied immediately to the web side so the next
  // capture (including an Alt+drag one) uses it.
  void setCommentColor(const QString &p_color);

  const QString &getCommentColor() const;

  // Highest page index the web side will accept in an anchor. Set from
  // 'documentloaded'; 0 means "unknown", which rejects every anchor.
  int getDocumentPageCount() const;

  // Functions to be called from web side.
public slots:
  // Set the flat outline. Each element is
  //   { "name": <string>, "level": <1-based int>, "index": <int> }
  // ordered by a pre-order DFS of the PDF outline tree.
  //
  // Exposed over QWebChannel, so the payload is UNTRUSTED: any script in the
  // viewer page can call this. Both the entry count and each level are clamped
  // (see the anonymous-namespace constants in the .cpp) rather than trusting the
  // web side's own limits.
  void setOutline(const QJsonArray &p_outline);

  // Report the loaded document's page count. Bounds every anchor page index
  // accepted afterwards; reset to 0 by clearComments().
  void setDocumentPageCount(int p_pageCount);

  // The overlay turned a text selection into an anchor and wants a comment made
  // from it. UNTRUSTED: validated here (type, page range, quad count, quad
  // shape, finite coordinates, text length) before anything is emitted.
  void requestAddComment(const QJsonObject &p_anchor, const QString &p_color);

  // The user clicked an existing highlight. @p_id is echoed back untrusted, so
  // it is length-bounded; the controller resolves it against the real set and
  // ignores an unknown id.
  void requestSelectComment(const QString &p_id);

  void requestDeleteComment(const QString &p_id);

  // The web side dropped out of an authoring tool by itself (Esc, or a one-shot
  // tool completing). UNTRUSTED, but it carries no payload beyond the fact.
  void notifyToolFinished();

  // Signals to be connected at web side.
signals:
  void urlUpdated(const QString &p_url);

  // @p_index is an index into the web side's destination array, NOT an index
  // into m_headings.
  void outlineItemScrollRequested(int p_index);

  void commentsUpdated(const QJsonArray &p_comments);

  void commentScrollRequested(const QString &p_id);

  void captureSelectionRequested(const QString &p_color);

  // Latched like the comment set: the web side must come up in the tool the
  // toolbar is showing, even across a reload.
  void toolChanged(const QString &p_tool);

  void commentColorChanged(const QString &p_color);

  // Signals to be connected at cpp side.
signals:
  void outlineChanged();

  // Intents from the overlay. The adapter never mutates the store itself —
  // that is CommentController's job (views/bridges emit intents only).
  void addCommentRequested(const QJsonObject &p_anchor, const QString &p_color);

  void selectCommentRequested(const QString &p_id);

  void deleteCommentRequested(const QString &p_id);

  // The web side finished an authoring gesture and dropped back to reading, so
  // the toolbar toggle must un-press itself. Emitted for one-shot tools
  // (FreeText places exactly one box per arming).
  void toolFinished();

public:
  // Drop the current comment set and the page count. Called across a reload for
  // the same reason clearOutline() is.
  void clearComments();

private:
  // Publishes m_comments if the bridge is ready; otherwise latches the intent
  // so the next false->true transition delivers exactly one publish.
  void publishComments();

  void publishTool();

  static QString toolToString(Tool p_tool);

  // Outline from web side, already passed through
  // OutlineProvider::makePerfectHeadings().
  QVector<Heading> m_headings;

  QJsonArray m_comments;

  bool m_commentsPublishPending = false;

  int m_documentPageCount = 0;

  Tool m_tool = Tool::None;

  QString m_commentColor = QStringLiteral("yellow");

  // Same latch-not-queue rule as the comment set: a reload must come up in the
  // CURRENT tool/colour, and only the newest values matter.
  bool m_toolPublishPending = false;
};
} // namespace vnotex

#endif // PDFVIEWERADAPTER_H
