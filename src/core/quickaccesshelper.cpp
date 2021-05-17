#include "quickaccesshelper.h"

#include "configmgr.h"
#include "sessionconfig.h"

using namespace vnotex;

void QuickAccessHelper::pinToQuickAccess(const QStringList &p_files)
{
    if (p_files.isEmpty()) {
        return;
    }

    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    auto qaFiles = sessionConfig.getQuickAccessFiles();
    qaFiles.append(p_files);
    qaFiles.removeDuplicates();
    sessionConfig.setQuickAccessFiles(qaFiles);
}
