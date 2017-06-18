#ifndef VVIMINDICATOR_H
#define VVIMINDICATOR_H

#include <QWidget>
#include "utils/vvim.h"

class QLabel;
class VButtonWithWidget;

class VVimIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit VVimIndicator(QWidget *parent = 0);

public slots:
    void update(const VVim *p_vim);

private slots:
    void updateRegistersTree(QWidget *p_widget);

private:
    void setupUI();

    QString modeToString(VimMode p_mode) const;

    // Indicate the mode.
    QLabel *m_modeLabel;

    // Indicate the registers.
    VButtonWithWidget *m_regBtn;

    // Indicate the pending keys.
    QLabel *m_keyLabel;

    const VVim *m_vim;
};

#endif // VVIMINDICATOR_H
