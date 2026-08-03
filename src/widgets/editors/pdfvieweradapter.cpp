#include "pdfvieweradapter.h"

#include <QDebug>

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

PdfViewerAdapter::PdfViewerAdapter(QObject *p_parent) : WebViewAdapter(p_parent) {}

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
