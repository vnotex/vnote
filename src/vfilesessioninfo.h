#ifndef VFILESESSIONINFO_H
#define VFILESESSIONINFO_H

#include "vconstants.h"

struct VEditTabInfo;
class QSettings;

namespace FileSessionConfig
{
    static const QString c_file = "file";
    static const QString c_mode = "mode";

    // Whether it is current file.
    static const QString c_active = "active";

    // Index in outline of the anchor.
    static const QString c_headerIndex = "header_index";

    static const QString c_cursorBlockNumber = "cursor_block_number";
    static const QString c_cursorPositionInBlock = "cursor_position_in_block";
}

// Information about an opened file (session).
class VFileSessionInfo
{
public:
    VFileSessionInfo();

    VFileSessionInfo(const QString &p_file,
                     OpenFileMode p_mode);

    // Fetch VFileSessionInfo from @p_tabInfo.
    static VFileSessionInfo fromEditTabInfo(const VEditTabInfo *p_tabInfo);

    // Fill corresponding fields of @p_tabInfo.
    void toEditTabInfo(VEditTabInfo *p_tabInfo) const;

    // Fetch VFileSessionInfo from @p_settings.
    static VFileSessionInfo fromSettings(const QSettings *p_settings);

    void toSettings(QSettings *p_settings) const;

    void setActive(bool p_active)
    {
        m_active = p_active;
    }

    // Absolute path of the file.
    QString m_file;

    // Mode of this file in this session.
    OpenFileMode m_mode;

    // Whether this file is current file.
    bool m_active;

    // Index in outline of the header.
    int m_headerIndex;

    // Block number of cursor block.
    int m_cursorBlockNumber;

    // Position in block of cursor.
    int m_cursorPositionInBlock;
};

#endif // VFILESESSIONINFO_H
