#ifndef VTABINDICATOR_H
#define VTABINDICATOR_H

#include <QWidget>
#include "vedittabinfo.h"

class QLabel;
class VButtonWithWidget;
class VEditTab;
class VWordCountPanel;

class VTabIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit VTabIndicator(QWidget *p_parent = 0);

    // Update indicator.
    void update(const VEditTabInfo &p_info);

private slots:
    void updateWordCountInfo(QWidget *p_widget);

private:
    void setupUI();

    void updateWordCountBtn(const VEditTabInfo &p_info);

    // Indicate the doc type.
    QLabel *m_docTypeLabel;

    // Indicate the readonly property.
    QLabel *m_readonlyLabel;

    // Indicate whether it is a normal note or an external file.
    QLabel *m_externalLabel;

    // Indicate whether it is a system file.
    QLabel *m_systemLabel;

    // Indicate the position of current cursor.
    QLabel *m_cursorLabel;

    // Indicate the word count.
    VButtonWithWidget *m_wordCountBtn;

    VEditTab *m_editTab;

    VWordCountPanel *m_wordCountPanel;
};

#endif // VTABINDICATOR_H
