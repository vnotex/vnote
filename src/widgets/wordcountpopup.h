#ifndef WORDCOUNTPOPUP_H
#define WORDCOUNTPOPUP_H

#include "buttonpopup.h"

#include "viewwindow.h"

class QToolButton;
class QLabel;

namespace vnotex
{
    class WordCountPanel : public QWidget
    {
        Q_OBJECT
    public:
        explicit WordCountPanel(QWidget *p_parent = nullptr);

        void updateCount(bool p_isSelection, int p_words, int p_charsWithoutSpace, int p_charsWithSpace);

    private:
        QLabel *m_selectionLabel = nullptr;
        QLabel *m_wordLabel = nullptr;
        QLabel *m_charWithoutSpaceLabel = nullptr;
        QLabel *m_charWithSpaceLabel = nullptr;
    };

    class WordCountPopup : public ButtonPopup
    {
        Q_OBJECT
    public:
        WordCountPopup(QToolButton *p_btn, const ViewWindow *p_viewWindow, QWidget *p_parent = nullptr);

        void updateCount(const ViewWindow::WordCountInfo &p_info);

    private:
        void setupUI();

        WordCountPanel *m_panel = nullptr;

        const ViewWindow *m_viewWindow = nullptr;
    };
}

#endif // WORDCOUNTPOPUP_H
