#ifndef LOGGING_H
#define LOGGING_H

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcSync)
Q_DECLARE_LOGGING_CATEGORY(lcBuffer)
Q_DECLARE_LOGGING_CATEGORY(lcWorkspace)
Q_DECLARE_LOGGING_CATEGORY(lcWebJs)
Q_DECLARE_LOGGING_CATEGORY(lcVim)
Q_DECLARE_LOGGING_CATEGORY(lcVxBridge)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
Q_DECLARE_LOGGING_CATEGORY(lcConfig)

namespace vnotex {

void installDefaultLoggingRules();

} // namespace vnotex

#endif // LOGGING_H
