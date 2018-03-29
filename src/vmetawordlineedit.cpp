#include "vmetawordlineedit.h"

#include <QDebug>
#include <QToolTip>

#include "utils/vmetawordmanager.h"

extern VMetaWordManager *g_mwMgr;


VMetaWordLineEdit::VMetaWordLineEdit(QWidget *p_parent)
    : VLineEdit(p_parent)
{
    init();
}

VMetaWordLineEdit::VMetaWordLineEdit(const QString &p_contents, QWidget *p_parent)
    : VLineEdit(p_contents, p_parent)
{
    init();
}

void VMetaWordLineEdit::handleTextChanged(const QString &p_text)
{
    m_evaluatedText = g_mwMgr->evaluate(p_text);

    if (m_evaluatedText == p_text) {
        return;
    }

    // Display tooltip at bottom-left.
    QPoint pos = mapToGlobal(QPoint(0, height()));
    QToolTip::showText(pos, m_evaluatedText, this);
}

void VMetaWordLineEdit::init()
{
    m_evaluatedText = g_mwMgr->evaluate(text());

    connect(this, &VLineEdit::textChanged,
            this, &VMetaWordLineEdit::handleTextChanged);
}

const QString &VMetaWordLineEdit::getEvaluatedText() const
{
    return m_evaluatedText;
}

QString VMetaWordLineEdit::evaluateText(const QString &p_text) const
{
    return g_mwMgr->evaluate(p_text);
}
