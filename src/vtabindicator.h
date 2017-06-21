#ifndef VTABINDICATOR_H
#define VTABINDICATOR_H

#include <QWidget>
#include "vedittabinfo.h"

class QLabel;

class VTabIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit VTabIndicator(QWidget *p_parent = 0);

    // Update indicator.
    void update(const VEditTabInfo &p_info);

private:
    void setupUI();

    // Indicate the doc type.
    QLabel *m_docTypeLabel;

    // Indicate the readonly property.
    QLabel *m_readonlyLabel;

    // Indicate the position of current cursor.
    QLabel *m_cursorLabel;
};

#endif // VTABINDICATOR_H
