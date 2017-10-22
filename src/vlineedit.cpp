#include "vlineedit.h"

#include <QDebug>
#include <QToolTip>

#include "utils/vmetawordmanager.h"


extern VMetaWordManager *g_mwMgr;

VLineEdit::VLineEdit(QWidget *p_parent)
    : QLineEdit(p_parent)
{
    init();
}

VLineEdit::VLineEdit(const QString &p_contents, QWidget *p_parent)
    : QLineEdit(p_contents, p_parent)
{
    init();
}

void VLineEdit::handleTextChanged(const QString &p_text)
{
    m_evaluatedText = g_mwMgr->evaluate(p_text);
    qDebug() << "evaluate text:" << m_evaluatedText;

    if (m_evaluatedText == p_text) {
        return;
    }

    // Display tooltip at bottom-left.
    QPoint pos = mapToGlobal(QPoint(0, height()));
    QToolTip::showText(pos, m_evaluatedText, this);
}

void VLineEdit::init()
{
    m_evaluatedText = g_mwMgr->evaluate(text());

    connect(this, &QLineEdit::textChanged,
            this, &VLineEdit::handleTextChanged);
}

const QString VLineEdit::getEvaluatedText() const
{
    return m_evaluatedText;
}
