#ifndef VEDITTABINFO_H
#define VEDITTABINFO_H

class VEditTab;

struct VEditTabInfo
{
    VEditTabInfo()
        : m_editTab(NULL),
          m_cursorBlockNumber(-1),
          m_cursorPositionInBlock(-1),
          m_blockCount(-1),
          m_anchorIndex(-1)
    {
    }

    VEditTab *m_editTab;

    // Cursor information. -1 for invalid info.
    int m_cursorBlockNumber;
    int m_cursorPositionInBlock;
    int m_blockCount;

    // Anchor index in outline.
    int m_anchorIndex;
};

#endif // VEDITTABINFO_H
