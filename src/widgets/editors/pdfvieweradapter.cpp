#include "pdfvieweradapter.h"

#include <QDebug>
#include <QtMath>

#include <core/services/commenttypes.h>

#include "../outlineprovider.h"

using namespace vnotex;

namespace {
// setOutline() is a QWebChannel-exposed slot, so its argument is untrusted by
// contract: ANY script running in the viewer page can call it with arbitrary
// values, and the C++ side must not rely on the web side's own limits.
//
// Hard ceiling on the number of entries accepted. Mirrors
// VX_MAX_OUTLINE_ENTRIES in src/data/extra/web/pdf.js/pdfviewercore.js; the two
// are independent by design, and the only requirement is that this one is >=
// the JS cap. If they ever drift the other way the outline is truncated here,
// which is degraded but safe.
constexpr int c_maxOutlineEntries = 5000;

// Hard ceiling on a heading's level. OutlineProvider::makePerfectHeadings
// synthesizes one "[EMPTY]" filler per SKIPPED level, so an unclamped level is
// an O(level) allocation amplifier: two entries at levels 1 and 2000000000 would
// append ~2e9 headings on the UI thread. A level of INT_MIN additionally makes
// that function's `int curLevel = baseLevel - 1` overflow.
//
// 64 is far beyond any real PDF outline (the dock's own expand control only
// goes to 6), and the legitimate producer never emits a skipped level at all.
constexpr int c_maxOutlineLevel = 64;

// A comment id is a UUID-without-braces (36 chars) minted by C++ and echoed
// back by the page. Bounded so a hostile echo cannot be used to blow up a log
// line or a lookup key.
constexpr int c_maxCommentIdLength = 128;

// Sanity ceiling on a reported page count. Independent of any real PDF limit —
// it only has to be far above plausible documents and far below "unbounded", so
// a hostile setDocumentPageCount cannot widen the anchor page check.
constexpr int c_maxDocumentPages = 100000;
} // namespace

PdfViewerAdapter::Heading::Heading(const QString &p_name, int p_level)
    : m_name(p_name), m_level(p_level) {}

PdfViewerAdapter::Heading::Heading(const QString &p_name, int p_level, int p_index)
    : m_name(p_name), m_level(p_level), m_index(p_index) {}

PdfViewerAdapter::Heading PdfViewerAdapter::Heading::fromJson(const QJsonObject &p_obj) {
  // A missing or non-integer "index" MUST degrade to -1 (inert), never to 0 —
  // 0 is a valid destination index, so a lenient parse would silently jump to
  // the wrong place.
  const auto indexValue = p_obj.value(QStringLiteral("index"));
  const int index = indexValue.isDouble() ? indexValue.toInt(-1) : -1;

  // Anything outside [1, c_maxOutlineLevel] collapses to the same -1 sentinel a
  // missing/garbage level already produced, so hostile values cost nothing extra
  // downstream.
  const auto levelValue = p_obj.value(QStringLiteral("level"));
  int level = levelValue.isDouble() ? levelValue.toInt(-1) : -1;
  if (level < 1 || level > c_maxOutlineLevel) {
    level = -1;
  }

  return Heading(p_obj.value(QStringLiteral("name")).toString(), level, index);
}

PdfViewerAdapter::PdfViewerAdapter(QObject *p_parent) : WebViewAdapter(p_parent) {
  for (const auto &tool : PdfToolOptions::toolNames()) {
    m_toolOptions.insert(tool, PdfToolOptions::defaults(tool));
  }

  // The false->true readiness transition is the ONLY point at which a
  // replacement page's QWebChannel exists, so it is where a latched comment set
  // has to be published. Without this, a set produced while a reload was in
  // flight would be silently dropped.
  connect(this, &WebViewAdapter::ready, this, [this]() {
    if (m_commentsPublishPending) {
      m_commentsPublishPending = false;
      emit commentsUpdated(m_comments);
    }
    if (m_toolPublishPending) {
      m_toolPublishPending = false;
      // EVERY tool, not just the active one: a reloaded page must come up fully
      // configured, and the latch republishes only what the adapter holds.
      for (const auto &tool : PdfToolOptions::toolNames()) {
        emit toolOptionsChanged(tool, m_toolOptions.value(tool));
      }
      emit toolChanged(toolToString(m_tool));
      // Editability rides the same latch: it is authoring state, not document
      // state, so the replacement page has to be told about it too.
      emit commentsEditableChanged(m_commentsEditable);
    }
  });
}

void PdfViewerAdapter::setUrl(const QString &p_url) {
  // TODO: Not supported yet.
  Q_ASSERT(false);
  if (isReady()) {
    emit urlUpdated(p_url);
  } else {
    pendAction(std::bind(&PdfViewerAdapter::setUrl, this, p_url));
  }
}

void PdfViewerAdapter::setOutline(const QJsonArray &p_outline) {
  // QJsonArray::size() is int on Qt 5 and qsizetype on Qt 6; compare and narrow
  // explicitly rather than relying on either.
  const auto total = p_outline.size();
  const int count = total > c_maxOutlineEntries ? c_maxOutlineEntries : static_cast<int>(total);
  if (count < total) {
    qWarning() << "PdfViewerAdapter: outline truncated from" << total << "to" << count << "entries";
  }

  QVector<Heading> headings;
  headings.reserve(count);
  for (int i = 0; i < count; ++i) {
    headings.push_back(Heading::fromJson(p_outline.at(i).toObject()));
  }

  // Fills in "[EMPTY]" headings for skipped levels, which is why every Heading
  // carries an explicit m_index instead of relying on its position here. The
  // level clamp in fromJson() is what bounds how many fillers this can produce.
  OutlineProvider::makePerfectHeadings(headings, m_headings);

  emit outlineChanged();
}

const QVector<PdfViewerAdapter::Heading> &PdfViewerAdapter::getOutlineHeadings() const {
  return m_headings;
}

void PdfViewerAdapter::clearOutline() {
  m_headings.clear();
  emit outlineChanged();
}

void PdfViewerAdapter::scrollToOutlineItem(int p_idx) {
  if (p_idx < 0 || p_idx >= m_headings.size()) {
    return;
  }

  const int destIndex = m_headings[p_idx].m_index;
  if (destIndex < 0) {
    // Filler heading, or a bookmark with no destination. Inert by design.
    return;
  }

  if (isReady()) {
    emit outlineItemScrollRequested(destIndex);
  } else {
    // pendAction() Q_ASSERTs !m_ready, so the branch is required.
    pendAction([this, destIndex]() { emit outlineItemScrollRequested(destIndex); });
  }
}

// ============ Comments ============

void PdfViewerAdapter::setComments(const QJsonArray &p_comments) {
  m_comments = p_comments;
  publishComments();
}

const QJsonArray &PdfViewerAdapter::getComments() const { return m_comments; }

int PdfViewerAdapter::getDocumentPageCount() const { return m_documentPageCount; }

void PdfViewerAdapter::clearComments() {
  m_comments = QJsonArray();
  m_documentPageCount = 0;
  // Deliberately NOT published: the page this would target is being torn down.
  // The latch is cleared too, so an empty set cannot be replayed over the
  // replacement document's real set.
  m_commentsPublishPending = false;

  // The TOOL, its per-tool options and the editability flag are not document
  // state -- the toolbar still shows whatever the user armed -- so the
  // replacement page has to be told about them, or the toggle would look
  // pressed while the page sat in reading mode, and the next stroke would use
  // the JS defaults.
  m_toolPublishPending = true;
}

void PdfViewerAdapter::publishComments() {
  if (isReady()) {
    emit commentsUpdated(m_comments);
    return;
  }
  // Latch, do NOT queue: only the newest set matters, and pendAction would
  // replay every intermediate snapshot on the next ready transition.
  m_commentsPublishPending = true;
}

void PdfViewerAdapter::scrollToComment(const QString &p_id) {
  if (p_id.isEmpty() || p_id.size() > c_maxCommentIdLength) {
    return;
  }
  if (isReady()) {
    emit commentScrollRequested(p_id);
  } else {
    pendAction([this, p_id]() { emit commentScrollRequested(p_id); });
  }
}

void PdfViewerAdapter::captureSelection(const QString &p_color) {
  const QString color = CommentColor::isValid(p_color) ? p_color : CommentColor::defaultToken();
  // Deliberately NOT queued when the page is not ready: a selection only exists
  // in a live document, so replaying this after a reload would act on whatever
  // happened to be selected in the REPLACEMENT document.
  if (isReady()) {
    emit captureSelectionRequested(color);
  }
}

void PdfViewerAdapter::beginCommentTextEdit(const QString &p_id) {
  if (p_id.isEmpty() || p_id.size() > c_maxCommentIdLength) {
    return;
  }
  // Deliberately NOT queued when the page is not ready, for the same reason
  // captureSelection() is not: an edit SESSION only exists in a live document,
  // and replaying it after a reload would open an editor on whatever comment
  // happened to inherit that id in the replacement document.
  if (isReady()) {
    emit commentTextEditRequested(p_id);
  }
}

void PdfViewerAdapter::setCommentsEditable(bool p_editable) {
  if (m_commentsEditable == p_editable) {
    return;
  }
  m_commentsEditable = p_editable;

  if (isReady()) {
    emit commentsEditableChanged(m_commentsEditable);
    return;
  }
  // Latch, do NOT queue: only the newest value matters.
  m_toolPublishPending = true;
}

bool PdfViewerAdapter::areCommentsEditable() const { return m_commentsEditable; }

QString PdfViewerAdapter::toolToString(Tool p_tool) {
  switch (p_tool) {
  case Tool::Highlight:
    return QStringLiteral("highlight");
  case Tool::Ink:
    return QStringLiteral("ink");
  case Tool::FreeText:
    return QStringLiteral("freetext");
  case Tool::None:
    break;
  }
  return QStringLiteral("none");
}

PdfViewerAdapter::Tool PdfViewerAdapter::toolFromString(const QString &p_tool) {
  if (p_tool == PdfToolOptions::highlightTool()) {
    return Tool::Highlight;
  }
  if (p_tool == PdfToolOptions::inkTool()) {
    return Tool::Ink;
  }
  if (p_tool == PdfToolOptions::freeTextTool()) {
    return Tool::FreeText;
  }
  return Tool::None;
}

void PdfViewerAdapter::setTool(Tool p_tool) {
  if (m_tool == p_tool) {
    return;
  }
  m_tool = p_tool;
  publishTool();
}

PdfViewerAdapter::Tool PdfViewerAdapter::getTool() const { return m_tool; }

void PdfViewerAdapter::setToolOptions(Tool p_tool, const QJsonObject &p_options) {
  const auto tool = toolToString(p_tool);
  if (!PdfToolOptions::isValidTool(tool)) {
    // Tool::None carries no options.
    return;
  }

  // One choke point for the Task 0 normalization: invalid colour -> default,
  // non-finite scalar -> default, out-of-range scalar -> clamped.
  const auto normalized = PdfToolOptions::normalize(tool, p_options);
  if (m_toolOptions.value(tool) == normalized) {
    return;
  }
  m_toolOptions.insert(tool, normalized);

  if (isReady()) {
    emit toolOptionsChanged(tool, normalized);
    return;
  }
  // Latch, do NOT queue: only the newest options matter, and pendAction would
  // replay every intermediate pick on the next ready transition.
  m_toolPublishPending = true;
}

QJsonObject PdfViewerAdapter::getToolOptions(Tool p_tool) const {
  return getToolOptions(toolToString(p_tool));
}

QJsonObject PdfViewerAdapter::getToolOptions(const QString &p_tool) const {
  return m_toolOptions.value(p_tool, PdfToolOptions::defaults(p_tool));
}

void PdfViewerAdapter::publishTool() {
  if (isReady()) {
    emit toolChanged(toolToString(m_tool));
    return;
  }
  // Latch, do NOT queue: only the newest tool matters, and pendAction would
  // replay every intermediate toggle on the next ready transition.
  m_toolPublishPending = true;
}

void PdfViewerAdapter::notifyToolFinished() {
  // The web side left the tool on its own (Esc, or a one-shot tool completing).
  // Mirror it locally so the adapter and the toolbar cannot disagree.
  m_tool = Tool::None;
  emit toolFinished();
}

void PdfViewerAdapter::setDocumentPageCount(int p_pageCount) {
  // Untrusted. A negative or absurd count would widen the anchor page bound
  // that requestAddComment() relies on.
  m_documentPageCount = (p_pageCount > 0 && p_pageCount <= c_maxDocumentPages) ? p_pageCount : 0;
}

void PdfViewerAdapter::requestAddComment(const QJsonObject &p_anchor, const QString &p_color) {
  // Every field below arrives from the page and is therefore hostile input.
  //
  // Only a type this build can RENDER may be minted here. An unknown type is
  // carried through from the store untouched, but the web side must never be
  // able to invent one.
  if (!isKnownAnchorType(p_anchor)) {
    qWarning() << "PdfViewerAdapter: rejected comment anchor of unsupported type"
               << p_anchor.value(QStringLiteral("type")).toString();
    return;
  }

  if (!isAnchorStructurallyValid(p_anchor)) {
    qWarning() << "PdfViewerAdapter: rejected structurally invalid comment anchor";
    return;
  }

  // The per-type validators bound the page to >= 0 only; the real ceiling is the
  // loaded document, which the web side reported on 'documentloaded'.
  const int page = anchorPage(p_anchor);
  if (m_documentPageCount <= 0 || page < 0 || page >= m_documentPageCount) {
    qWarning() << "PdfViewerAdapter: rejected comment anchor on page" << page << "of"
               << m_documentPageCount;
    return;
  }

  QJsonObject anchor = p_anchor;
  if (anchor.value(QStringLiteral("type")).toString() == PdfQuadsAnchor::type()) {
    // Re-truncate rather than reject: an over-long selection is a plausible user
    // action, not an attack, and losing the tail is better than losing the note.
    anchor.insert(QStringLiteral("text"),
                  PdfQuadsAnchor::text(p_anchor).left(PdfQuadsAnchor::maxAnchorTextLength()));
  }

  const QString color = CommentColor::isValid(p_color) ? p_color : CommentColor::defaultToken();

  emit addCommentRequested(anchor, color);
}

void PdfViewerAdapter::requestSelectComment(const QString &p_id) {
  if (p_id.isEmpty() || p_id.size() > c_maxCommentIdLength) {
    return;
  }
  emit selectCommentRequested(p_id);
}

void PdfViewerAdapter::requestDeleteComment(const QString &p_id) {
  if (p_id.isEmpty() || p_id.size() > c_maxCommentIdLength) {
    return;
  }
  emit deleteCommentRequested(p_id);
}

void PdfViewerAdapter::requestMoveComment(const QString &p_id, int p_page, double p_x, double p_y) {
  if (p_id.isEmpty() || p_id.size() > c_maxCommentIdLength) {
    // The id itself is NOT logged: it is attacker-controlled and unbounded.
    qWarning() << "PdfViewerAdapter: rejected comment move with an invalid id of length"
               << p_id.size();
    return;
  }

  // Same ceiling requestAddComment() applies: the loaded document, as reported
  // on 'documentloaded'. A reload resets it, so a stale move is rejected here.
  if (m_documentPageCount <= 0 || p_page < 0 || p_page >= m_documentPageCount) {
    qWarning() << "PdfViewerAdapter: rejected comment move to page" << p_page << "of"
               << m_documentPageCount;
    return;
  }

  if (!qIsFinite(p_x) || !qIsFinite(p_y)) {
    qWarning() << "PdfViewerAdapter: rejected comment move with a non-finite coordinate";
    return;
  }

  emit moveCommentRequested(p_id, p_page, p_x, p_y);
}

void PdfViewerAdapter::requestSetCommentText(const QString &p_id, const QString &p_text) {
  if (p_id.isEmpty() || p_id.size() > c_maxCommentIdLength) {
    return;
  }
  // TRUNCATE rather than reject: an over-long note is a plausible user action,
  // not an attack, and losing the tail is better than losing the note. The
  // controller re-applies the same cap, which is the authoritative one.
  emit setCommentTextRequested(p_id, p_text.left(Comment::maxTextLength()));
}
