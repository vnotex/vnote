#include "exportstyleresolver.h"

#include <QFileInfo>

using namespace vnotex;

QString vnotex::resolveSyntaxHighlightFile(const QString &p_optionFile,
                                           const QString &p_themeHighlightFile) {
  // The theme filename contract: the syntax-highlight stylesheet is always named "highlight.css"
  // (see Theme::getFileName(); it is private, so the literal is duplicated here intentionally).
  if (QFileInfo(p_optionFile).fileName() == QStringLiteral("highlight.css")) {
    return p_optionFile;
  }

  return p_themeHighlightFile;
}
