#include "htmlutils.h"

#include <QRegExp>

using namespace vnotex;

bool HtmlUtils::hasOnlyImgTag(const QString &p_html)
{
    // Tricky.
    QRegExp reg(QStringLiteral("<(?:p|span|div) "));
    return !p_html.contains(reg);
}

QString HtmlUtils::escapeHtml(QString p_text)
{
    p_text.replace(">", "&gt;").replace("<", "&lt;").replace("&", "&amp;");
    return p_text;
}
