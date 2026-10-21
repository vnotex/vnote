#include "markdownhtmlrenderer.h"

#include <QStringList>
#include <algorithm>
#include <numeric>
#include <utility>

#include <vtextedit/markdownhighlighterdata.h>

#include "headingslugger.h"

using namespace vnotex;

int MarkdownHtmlRenderer::parseOptions(const MarkdownHtmlOptions &p_options) {
  return vte::MarkdownDocument::viewerParseOptions(p_options.htmlEnabled, p_options.linkify);
}

QVector<MarkdownHtmlHeading>
MarkdownHtmlRenderer::buildHeadings(const QVector<vte::md::HeadingInfo> &p_headings) {
  QVector<MarkdownHtmlHeading> headings;
  headings.reserve(p_headings.size());
  for (const auto &heading : p_headings)
    headings.append({heading.m_startLine, heading.m_level, heading.m_title, {}});

  HeadingSlugger slugger;
  const auto byRenderOrder = [](const vte::md::HeadingInfo &p_left,
                                const vte::md::HeadingInfo &p_right) {
    return p_left.m_renderOrder < p_right.m_renderOrder;
  };
  if (std::is_sorted(p_headings.cbegin(), p_headings.cend(), byRenderOrder)) {
    for (qsizetype i = 0; i < p_headings.size(); ++i)
      if (p_headings[i].m_renderOrder >= 0)
        headings[i].anchor = slugger.slug(p_headings[i].m_anchorText);
  } else {
    // Footnote relocation can put earlier source headings after the main document.
    // Only those documents need an auxiliary permutation; keep source positions intact.
    QVector<qsizetype> order(p_headings.size());
    std::iota(order.begin(), order.end(), qsizetype(0));
    std::sort(order.begin(), order.end(), [&](qsizetype p_left, qsizetype p_right) {
      return byRenderOrder(p_headings[p_left], p_headings[p_right]);
    });
    for (auto i : order)
      if (p_headings[i].m_renderOrder >= 0)
        headings[i].anchor = slugger.slug(p_headings[i].m_anchorText);
  }
  return headings;
}

MarkdownHtmlResult MarkdownHtmlRenderer::render(const QByteArray &p_source,
                                                const MarkdownHtmlOptions &p_options,
                                                const vte::MarkdownDocumentPtr &p_document,
                                                bool p_emitHtml) {
  MarkdownHtmlResult result;
  const int options = parseOptions(p_options);
  result.document =
      p_document && p_document->parseOptions() == options && p_document->source() == p_source
          ? p_document
          : vte::MarkdownDocument::parse(p_source, options);
  if (!result.document) {
    result.error = QStringLiteral("Failed to parse Markdown document");
    return result;
  }

  const auto &sourceHeadings = result.document->headings();
  result.headings = buildHeadings(sourceHeadings);
  QStringList ids;
  if (p_emitHtml)
    ids.reserve(result.headings.size());
  qsizetype visibleCount = 0;
  for (qsizetype i = 0; i < sourceHeadings.size(); ++i) {
    // cmark binds against all source headings, including unused footnote definitions.
    if (p_emitHtml)
      ids.append(result.headings[i].anchor);
    if (sourceHeadings[i].m_renderOrder < 0)
      continue;
    if (visibleCount != i)
      result.headings[visibleCount] = std::move(result.headings[i]);
    ++visibleCount;
  }
  result.headings.resize(visibleCount);

  if (p_emitHtml) {
    result.html = result.document->renderHtml(ids, p_options.autoBreaks, p_options.htmlEnabled);
    if (result.html.isNull()) {
      result = {};
      result.error = QStringLiteral("Failed to render Markdown document");
    }
  }
  return result;
}
