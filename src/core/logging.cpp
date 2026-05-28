#include "logging.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSync, "vnote.sync")
Q_LOGGING_CATEGORY(lcBuffer, "vnote.buffer")
Q_LOGGING_CATEGORY(lcWorkspace, "vnote.workspace")
Q_LOGGING_CATEGORY(lcWebJs, "vnote.web.js")
Q_LOGGING_CATEGORY(lcVim, "vnote.vim")
Q_LOGGING_CATEGORY(lcVxBridge, "vnote.vxbridge")
Q_LOGGING_CATEGORY(lcUi, "vnote.ui")
Q_LOGGING_CATEGORY(lcConfig, "vnote.config")

namespace vnotex {

void installDefaultLoggingRules() {
  QString rules = QStringLiteral("*.debug=false\n"
                                 "vnote.web.js.debug=false\n"
                                 "vnote.vim.debug=false\n"
                                 "qt.qpa.*.debug=false\n"
                                 // vtextedit emits bare qDebug() calls that are caught by
                                 // the global *.debug=false above. These extra rules are
                                 // belt-and-suspenders in case vtextedit later adopts
                                 // categories like qt.text.* or vnote.text.*:
                                 "qt.text.*.debug=false\n"
                                 "vtextedit.*.debug=false");

  QByteArray extra = qgetenv("VNOTE_LOG_RULES");
  if (!extra.isEmpty()) {
    rules += '\n';
    rules += QString::fromLocal8Bit(extra);
  }

  QLoggingCategory::setFilterRules(rules);
}

} // namespace vnotex
