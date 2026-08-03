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

  // Signals to be connected at web side.
signals:
  void urlUpdated(const QString &p_url);

  // @p_index is an index into the web side's destination array, NOT an index
  // into m_headings.
  void outlineItemScrollRequested(int p_index);

  // Signals to be connected at cpp side.
signals:
  void outlineChanged();

private:
  // Outline from web side, already passed through
  // OutlineProvider::makePerfectHeadings().
  QVector<Heading> m_headings;
};
} // namespace vnotex

#endif // PDFVIEWERADAPTER_H
