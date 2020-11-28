#include "htmlutils.h"

#include <QRegExp>

using namespace vnotex;

bool HtmlUtils::hasOnlyImgTag(const QString &p_html)
{
    // Tricky.
    QRegExp reg(QStringLiteral("<(?:p|span|div) "));
    return !p_html.contains(reg);
}
