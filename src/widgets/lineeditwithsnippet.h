#ifndef LINEEDITWITHSNIPPET_H
#define LINEEDITWITHSNIPPET_H

#include "lineedit.h"

namespace vnotex
{
    // A line edit with snippet support.
    class LineEditWithSnippet : public LineEdit
    {
        Q_OBJECT
    public:
        explicit LineEditWithSnippet(QWidget *p_parent = nullptr);

        LineEditWithSnippet(const QString &p_contents, QWidget *p_parent = nullptr);

        // Get text with snippets evaluated.
        QString evaluatedText() const;

    private:
        void setTips();
    };
}

#endif // LINEEDITWITHSNIPPET_H
