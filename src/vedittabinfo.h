#ifndef VEDITTABINFO_H
#define VEDITTABINFO_H

#include "vwordcountinfo.h"

class VEditTab;

struct VEditTabInfo
{
    enum InfoType
    {
        // Update all information.
        All = 0,

        // Update only cursor information.
        Cursor
    };

    VEditTabInfo()
        : m_type(InfoType::All),
          m_editTab(NULL),
          m_cursorBlockNumber(-1),
          m_cursorPositionInBlock(-1),
          m_blockCount(-1),
          m_headerIndex(-1)
    {
    }

    void clear()
    {
        m_type = InfoType::All;
        m_editTab = NULL;
        m_cursorBlockNumber = -1;
        m_cursorPositionInBlock = -1;
        m_blockCount = -1;
        m_wordCountInfo.clear();
        m_headerIndex = -1;
    }

    InfoType m_type;

    VEditTab *m_editTab;

    // Cursor information. -1 for invalid info.
    int m_cursorBlockNumber;
    int m_cursorPositionInBlock;
    int m_blockCount;

    VWordCountInfo m_wordCountInfo;

    // Header index in outline.
    int m_headerIndex;
};

#endif // VEDITTABINFO_H
