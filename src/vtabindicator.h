#ifndef VTABINDICATOR_H
#define VTABINDICATOR_H

#include <QWidget>
#include "vedittabinfo.h"

class QLabel;
class VButtonWithWidget;
class VEditTab;
class VWordCountPanel;
class QGroupBox;
class VTagPanel;

class VWordCountPanel : public QWidget
{
    Q_OBJECT
public:
    VWordCountPanel(QWidget *p_parent = nullptr);

    void updateReadInfo(const VWordCountInfo &p_readInfo);

    void updateEditInfo(const VWordCountInfo &p_editInfo);

    void clear();

private:
    QLabel *m_wordLabel;
    QLabel *m_charWithoutSpacesLabel;
    QLabel *m_charWithSpacesLabel;

    QLabel *m_wordEditLabel;
    QLabel *m_charWithoutSpacesEditLabel;
    QLabel *m_charWithSpacesEditLabel;

    QGroupBox *m_readBox;
    QGroupBox *m_editBox;
};


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

    // Tag panel.
    VTagPanel *m_tagPanel;

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
