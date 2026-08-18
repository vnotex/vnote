#include "imagedestinationrewriter.h"

#include <QDebug>

#include <vtextedit/htmlimgscanner.h>
#include <vtextedit/markdownutils.h>

using namespace vnotex;

QString vnotex::spellMarkdownDestination(const QString &p_url) {
  if (p_url.contains(QLatin1Char(' ')) || p_url.contains(QLatin1Char('(')) ||
      p_url.contains(QLatin1Char(')'))) {
    return QLatin1Char('<') + p_url + QLatin1Char('>');
  }
  return p_url;
}

bool vnotex::rewriteImageDestination(QString &p_text, const vte::MarkdownLink &p_link,
                                     const QString &p_url) {
  if (!p_link.hasUrlSpan()) {
    return false;
  }

  if (p_link.m_syntax == vte::MarkdownLink::Syntax::Markdown) {
    p_text.replace(p_link.m_urlStart, p_link.m_urlEnd - p_link.m_urlStart,
                   spellMarkdownDestination(p_url));
    return true;
  }

  vte::RawTextState state;
  const auto tags = vte::scanHtmlImgTags(
      p_text.mid(p_link.m_regionStart, p_link.m_regionEnd - p_link.m_regionStart),
      p_link.m_regionStart, &state);
  if (tags.size() != 1) {
    qWarning() << "skipped rewriting an HTML image whose tag could not be re-scanned"
               << p_link.m_urlInLink;
    return false;
  }

  const auto *srcAttr = tags.first().attr("src");
  if (!srcAttr || srcAttr->m_attrStart < 0 || srcAttr->m_attrEnd <= srcAttr->m_attrStart) {
    qWarning() << "skipped rewriting an HTML image with no usable src attribute"
               << p_link.m_urlInLink;
    return false;
  }

  p_text.replace(srcAttr->m_attrStart, srcAttr->m_attrEnd - srcAttr->m_attrStart,
                 vte::spellHtmlSrcAttr(p_url));
  return true;
}
