#include "guiutils.h"

#include <QRegularExpression>

using namespace vnotex;

QColor GuiUtils::toColor(const QString &p_color) {
  // rgb(123, 123, 123).
  QRegularExpression rgbTripleRegExp(R"(^rgb\((\d+)\s*,\s*(\d+)\s*,\s*(\d+)\)$)",
                                     QRegularExpression::CaseInsensitiveOption);
  auto match = rgbTripleRegExp.match(p_color);
  if (match.hasMatch()) {
    return QColor(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
  }

  return QColor(p_color);
}

