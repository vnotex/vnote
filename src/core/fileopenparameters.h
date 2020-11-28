#ifndef FILEOPENPARAMETERS_H
#define FILEOPENPARAMETERS_H

namespace vnotex
{
    class Node;

    struct FileOpenParameters
    {
        // Some modes may be not supported by some editors.
        enum Mode
        {
            Read,
            Edit,
            FullPreview,
            FocusPreview
        };

        Mode m_mode = Mode::Read;

        // Whether focus to the opened window.
        bool m_focus = true;

        // Whether it is a new file.
        bool m_newFile = false;

        // If this file is an attachment of a node, this field indicates it.
        Node *m_nodeAttachedTo = nullptr;

        // Open as read-only.
        bool m_readOnly = false;
    };
}

#endif // FILEOPENPARAMETERS_H
