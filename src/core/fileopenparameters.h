#ifndef FILEOPENPARAMETERS_H
#define FILEOPENPARAMETERS_H

#include <QSharedPointer>

#include "global.h"

namespace vnotex
{
    class Node;
    class SearchToken;

    struct FileOpenParameters
    {
        ViewWindowMode m_mode = ViewWindowMode::Read;

        // Force to enter m_mode.
        bool m_forceMode = false;

        // Whether focus to the opened window.
        bool m_focus = true;

        // Whether it is a new file.
        bool m_newFile = false;

        // If this file is an attachment of a node, this field indicates it.
        Node *m_nodeAttachedTo = nullptr;

        // Open as read-only.
        bool m_readOnly = false;

        // If m_lineNumber > -1, it indicates the line to scroll to after opening the file.
        // 0-based.
        int m_lineNumber = -1;

        // Whether always open a new window for file.
        bool m_alwaysNewWindow = false;

        // If not empty, use this token to do a search text highlight.
        QSharedPointer<SearchToken> m_searchToken;
    };
}

#endif // FILEOPENPARAMETERS_H
