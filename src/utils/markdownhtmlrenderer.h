#ifndef MARKDOWNHTMLRENDERER_H
#define MARKDOWNHTMLRENDERER_H

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <vtextedit/markdowndocument.h>

namespace vnotex {

struct MarkdownHtmlOptions {
  bool htmlEnabled = true;
  bool autoBreaks = false;
  bool linkify = true;
};

struct MarkdownHtmlHeading {
  int line = -1;
  int level = 0;
  QString title;
  QString anchor;
};

struct MarkdownHtmlResult {
  QString html;
  QVector<MarkdownHtmlHeading> headings;
  vte::MarkdownDocumentPtr document;
  QString error;
};

// Pure, worker-safe conversion. A supplied snapshot is never accepted by revision alone.
class MarkdownHtmlRenderer {
public:
  static int parseOptions(const MarkdownHtmlOptions &p_options);
  // Input/output are source-ordered; duplicate IDs follow final HTML traversal order.
  // Hidden headings retain empty ID slots but do not reserve slugs.
  static QVector<MarkdownHtmlHeading>
  buildHeadings(const QVector<vte::md::HeadingInfo> &p_headings);
  static MarkdownHtmlResult render(const QByteArray &p_source, const MarkdownHtmlOptions &p_options,
                                   const vte::MarkdownDocumentPtr &p_document = {},
                                   bool p_emitHtml = true);
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::MarkdownHtmlResult)

#endif // MARKDOWNHTMLRENDERER_H
