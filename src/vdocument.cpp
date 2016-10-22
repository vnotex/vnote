#include "vdocument.h"

#include <QtDebug>

VDocument::VDocument(QObject *parent) : QObject(parent)
{

}

VDocument::VDocument(const QString &text, QObject *parent)
    : QObject(parent)
{
    m_text = text;
}

void VDocument::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    emit textChanged(m_text);
}

QString VDocument::getText()
{
    return m_text;
}
