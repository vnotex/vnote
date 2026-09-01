#ifndef PDFVIEWERADAPTER_H
#define PDFVIEWERADAPTER_H

#include "webviewadapter.h"

#include <QHash>
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

  // Ask the overlay to open its INLINE editor on a free-text box, so the Text
  // tool writes where the user clicked. Driven right after the comment is
  // minted; without it a placed box is an empty "…" placeholder whose only
  // editor is the comment dock, which is closed by default.
  void beginCommentTextEdit(const QString &p_id);

  // Whether the store will accept a write for the active file. Pushed so the
  // overlay can REFUSE to open its editor on a read-only file rather than let
  // the user type into something that is silently discarded.
  void setCommentsEditable(bool p_editable);

  bool areCommentsEditable() const;

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

  // The tool-key vocabulary shared with PdfViewerConfig and the web side.
  static QString toolToString(Tool p_tool);

  // Inverse of toolToString(). Anything unrecognized is Tool::None, so the
  // mapping is spelled in exactly one place and cannot drift.
  static Tool toolFromString(const QString &p_tool);

  // Per-tool authoring options: colour for every tool, plus `width` for ink and
  // `fontSize` for free text. Normalized on the way in (see
  // PdfToolOptions::normalize) — the toolbar is trusted-ish, but this keeps one
  // choke point and mirrors what the web side is allowed to send back.
  //
  // The payload stays JSON-PRIMITIVE (QJsonObject, not a QVariantMap of typed
  // values) because it crosses QWebChannel.
  void setToolOptions(Tool p_tool, const QJsonObject &p_options);

  QJsonObject getToolOptions(Tool p_tool) const;

  QJsonObject getToolOptions(const QString &p_tool) const;

  // Highest page index the web side will accept in an anchor. Set from
  // 'documentloaded'; 0 means "unknown", which rejects every anchor.
  int getDocumentPageCount() const;

  // ============ Viewer controls ============

  // The normalized viewer state pushed up by the web side, after validation.
  // Everything here is display state for PdfViewerToolBar; the ADAPTER is the
  // single source of truth and the toolbar repaints from it, never from its own
  // clicks.
  struct ViewerState {
    // 1-based, as pdf.js reports it. 0 means "no document".
    int m_page = 0;

    int m_pageCount = 0;

    // The numeric zoom factor actually in effect (1.0 == 100%).
    double m_scale = 1.0;

    // 'auto' | 'page-actual' | 'page-fit' | 'page-width', or a numeric string.
    QString m_scaleValue = QStringLiteral("auto");

    // One of 0 / 90 / 180 / 270.
    int m_rotation = 0;

    // pdf.js ScrollMode: 0 vertical, 1 horizontal, 2 wrapped, 3 page.
    int m_scrollMode = 0;

    // pdf.js SpreadMode: 0 none, 1 odd, 2 even.
    int m_spreadMode = 0;

    // pdf.js CursorTool: 0 select, 1 hand.
    int m_cursorTool = 0;

    bool m_sidebarOpen = false;

    // False until the first ACCEPTED state arrives. The toolbar keeps every
    // control disabled while this is false, so a blank window has no live
    // controls rather than controls that silently do nothing.
    bool m_valid = false;
  };

  const ViewerState &getViewerState() const;

  // Drop the DOCUMENT-dependent viewer state (page count, validity, find
  // query) across a reload, and ARM the replay of the page/zoom/rotation/mode
  // the user was looking at. Deliberately does NOT clear the replay values --
  // that is the entire point: a theme switch reloads the page and must come
  // back to page 7 at 150%, not to page 1.
  void clearViewerState();

  // Whether a replay is armed and still waiting for the replacement document.
  // Exposed for the gate; production code never branches on it.
  bool isViewerReplayPending() const;

  // ---- Replayed commands ----
  // These are view state a reload must not silently reset, so the adapter keeps
  // a REPLAY copy distinct from the current state and re-applies it once on the
  // replacement document.

  void gotoPage(int p_page);

  // 'auto' | 'page-actual' | 'page-fit' | 'page-width' | a numeric string.
  void setZoom(const QString &p_value);

  void stepZoom(bool p_zoomIn);

  void setRotation(int p_degrees);

  void setScrollMode(int p_mode);

  void setSpreadMode(int p_mode);

  void setCursorTool(int p_tool);

  // ---- One-shot commands, never replayed ----
  // Same reasoning as captureSelection(): a transient action only means
  // anything in a LIVE document, and replaying it after a reload would act on
  // the replacement.

  void toggleSidebar();

  void showDocumentProperties();

  // NOTE: there is deliberately no startPrint()/printDocument() and no
  // enterPresentationMode(). Neither verb can be completed from inside the
  // page: pdf.js's print service destroys its own prepared DOM on a hardcoded
  // 20 ms timer that Qt's asynchronous print cannot be sequenced against, and
  // Chromium refuses requestFullscreen() without a renderer user gesture. See
  // PdfViewWindow2::isPrintSupported() and PdfViewWindow2::setPresentationMode().

  // Drop the find highlight. One-shot: a find session belongs to a live page.
  void clearFind();

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

  // The inline free-text editor was committed. UNTRUSTED: the id is
  // length-bounded and the body is TRUNCATED (never rejected) to
  // Comment::maxTextLength() -- losing the tail of an over-long note is better
  // than losing the note.
  void requestSetCommentText(const QString &p_id, const QString &p_text);

  void requestDeleteComment(const QString &p_id);

  // The overlay finished a drag of a pdf-freetext box. UNTRUSTED: the id is
  // length-bounded, the page is re-validated against the loaded document, and
  // the coordinates must be finite. Rejected -- never clamped: clamping is the
  // page's job, and an out-of-range value here means the page is lying.
  //
  // Deliberately NARROW rather than a generic setCommentAnchor: a hostile page
  // must not be able to rewrite the anchor's type or fontSize.
  void requestMoveComment(const QString &p_id, int p_page, double p_x, double p_y);

  // The web side dropped out of an authoring tool by itself (Esc, or a one-shot
  // tool completing). UNTRUSTED, but it carries no payload beyond the fact.
  void notifyToolFinished();

  // The web side's normalized viewer state (page, zoom, rotation, scroll /
  // spread / cursor mode, sidebar). UNTRUSTED like every other slot here: page
  // must be in [1, pageCount], the scale finite and positive, the rotation one
  // of {0,90,180,270} and each mode in range. A payload failing ANY check is
  // dropped whole with a qWarning and the previous state kept -- never
  // partially applied, or the toolbar would show a mix of two documents.
  void setViewerState(const QJsonObject &p_state);

  // Signals to be connected at web side.
signals:
  void urlUpdated(const QString &p_url);

  // @p_index is an index into the web side's destination array, NOT an index
  // into m_headings.
  void outlineItemScrollRequested(int p_index);

  void commentsUpdated(const QJsonArray &p_comments);

  void commentScrollRequested(const QString &p_id);

  void captureSelectionRequested(const QString &p_color);

  void commentTextEditRequested(const QString &p_id);

  // Latched alongside the tool: a reloaded page must come up knowing whether it
  // may author at all.
  void commentsEditableChanged(bool p_editable);

  // Latched like the comment set: the web side must come up in the tool the
  // toolbar is showing, even across a reload.
  void toolChanged(const QString &p_tool);

  // Latched alongside the tool: a reloaded page must come up FULLY configured,
  // so the false->true readiness transition republishes EVERY tool's options.
  void toolOptionsChanged(const QString &p_tool, const QJsonObject &p_options);

  // ---- Viewer control commands ----
  void pageRequested(int p_page);

  void zoomRequested(const QString &p_value);

  void zoomStepRequested(bool p_zoomIn);

  void rotationRequested(int p_degrees);

  void scrollModeRequested(int p_mode);

  void spreadModeRequested(int p_mode);

  void cursorToolRequested(int p_tool);

  void sidebarToggleRequested();

  void documentPropertiesRequested();

  void findCleared();

  // Signals to be connected at cpp side.
signals:
  void outlineChanged();

  // The viewer state changed (or was cleared). The toolbar repaints from
  // getViewerState() -- the adapter is the single source of truth.
  void viewerStateChanged();

  // Intents from the overlay. The adapter never mutates the store itself —
  // that is CommentController's job (views/bridges emit intents only).
  void addCommentRequested(const QJsonObject &p_anchor, const QString &p_color);

  void selectCommentRequested(const QString &p_id);

  void setCommentTextRequested(const QString &p_id, const QString &p_text);

  void deleteCommentRequested(const QString &p_id);

  void moveCommentRequested(const QString &p_id, int p_page, double p_x, double p_y);

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

  // Outline from web side, already passed through
  // OutlineProvider::makePerfectHeadings().
  QVector<Heading> m_headings;

  QJsonArray m_comments;

  bool m_commentsPublishPending = false;

  int m_documentPageCount = 0;

  // Defaults to FALSE so a page that comes up before C++ has said anything
  // cannot accept keystrokes the store would refuse.
  bool m_commentsEditable = false;

  Tool m_tool = Tool::None;

  // Tool key -> normalized options object. Always populated for every tool in
  // PdfToolOptions::toolNames(), so getToolOptions() is total.
  QHash<QString, QJsonObject> m_toolOptions;

  // Same latch-not-queue rule as the comment set: a reload must come up in the
  // CURRENT tool and per-tool options, and only the newest values matter.
  bool m_toolPublishPending = false;

  // ---- Viewer controls ----

  ViewerState m_viewerState;

  // The values to re-apply after a reload. Refreshed by EVERY accepted state
  // push, not only by toolbar requests: a page reached by scrolling, by an
  // outline click, from a sidebar thumbnail or with a keyboard shortcut must
  // survive the reload too.
  //
  // Deliberately distinct from m_viewerState, which clearViewerState() blanks.
  int m_replayPage = 0;

  QString m_replayZoom;

  int m_replayRotation = 0;

  int m_replayScrollMode = 0;

  int m_replaySpreadMode = 0;

  int m_replayCursorTool = 0;

  // A freshly constructed adapter has NO replay value, so a new window opens at
  // pdf.js's own defaults.
  bool m_replayValid = false;

  // Armed by clearViewerState(). While set, incoming states do NOT refresh the
  // replay values -- otherwise the replacement document's own initial
  // page-1 / default-scale push would overwrite exactly what is about to be
  // replayed.
  bool m_replayPending = false;

  // Emits the replay commands once, and disarms.
  void replayViewerState();

  // Copies the accepted state into the replay slots.
  void captureViewerReplay();
};
} // namespace vnotex

#endif // PDFVIEWERADAPTER_H
