#include "vfilesessioninfo.h"

#include <QSettings>

#include "vedittabinfo.h"
#include "vtableofcontent.h"
#include "vedittab.h"


VFileSessionInfo::VFileSessionInfo()
    : m_mode(OpenFileMode::Read),
      m_active(false),
      m_headerIndex(-1),
      m_cursorBlockNumber(-1),
      m_cursorPositionInBlock(-1)
{
}

VFileSessionInfo::VFileSessionInfo(const QString &p_file,
                                   OpenFileMode p_mode)
    : m_file(p_file),
      m_mode(p_mode),
      m_active(false),
      m_headerIndex(-1),
      m_cursorBlockNumber(-1),
      m_cursorPositionInBlock(-1)
{
}

// Fetch VFileSessionInfo from @p_tabInfo.
VFileSessionInfo VFileSessionInfo::fromEditTabInfo(const VEditTabInfo *p_tabInfo)
{
    Q_ASSERT(p_tabInfo);
    VEditTab *tab = p_tabInfo->m_editTab;
    VFileSessionInfo info(tab->getFile()->fetchPath(),
                          tab->isEditMode() ? OpenFileMode::Edit : OpenFileMode::Read);
    info.m_headerIndex = p_tabInfo->m_headerIndex;
    info.m_cursorBlockNumber = p_tabInfo->m_cursorBlockNumber;
    info.m_cursorPositionInBlock = p_tabInfo->m_cursorPositionInBlock;

    return info;
}

void VFileSessionInfo::toEditTabInfo(VEditTabInfo *p_tabInfo) const
{
    p_tabInfo->m_headerIndex = m_headerIndex;
    p_tabInfo->m_cursorBlockNumber = m_cursorBlockNumber;
    p_tabInfo->m_cursorPositionInBlock = m_cursorPositionInBlock;
}

VFileSessionInfo VFileSessionInfo::fromSettings(const QSettings *p_settings)
{
    VFileSessionInfo info;
    info.m_file = p_settings->value(FileSessionConfig::c_file).toString();
    int tmpMode = p_settings->value(FileSessionConfig::c_mode).toInt();
    if (tmpMode >= (int)OpenFileMode::Read && tmpMode < (int)OpenFileMode::Invalid) {
        info.m_mode = (OpenFileMode)tmpMode;
    } else {
        info.m_mode = OpenFileMode::Read;
    }

    info.m_active = p_settings->value(FileSessionConfig::c_active).toBool();
    info.m_headerIndex = p_settings->value(FileSessionConfig::c_headerIndex).toInt();
    info.m_cursorBlockNumber = p_settings->value(FileSessionConfig::c_cursorBlockNumber).toInt();
    info.m_cursorPositionInBlock = p_settings->value(FileSessionConfig::c_cursorPositionInBlock).toInt();

    return info;
}

void VFileSessionInfo::toSettings(QSettings *p_settings) const
{
    p_settings->setValue(FileSessionConfig::c_file, m_file);
    p_settings->setValue(FileSessionConfig::c_mode, (int)m_mode);
    p_settings->setValue(FileSessionConfig::c_active, m_active);
    p_settings->setValue(FileSessionConfig::c_headerIndex, m_headerIndex);
    p_settings->setValue(FileSessionConfig::c_cursorBlockNumber, m_cursorBlockNumber);
    p_settings->setValue(FileSessionConfig::c_cursorPositionInBlock, m_cursorPositionInBlock);
}
