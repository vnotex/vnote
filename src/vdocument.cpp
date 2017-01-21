#include "vdocument.h"
#include <QDebug>

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
    if (text == m_text) {
        return;
    }
    m_text = text;
    emit textChanged(m_text);
}

QString VDocument::getText()
{
    return m_text;
}

void VDocument::setToc(const QString &toc)
{
    if (toc == m_toc) {
        return;
    }
    m_toc = toc;
    emit tocChanged(m_toc);
}

QString VDocument::getToc()
{
    return m_toc;
}

void VDocument::scrollToAnchor(const QString &anchor)
{
    emit requestScrollToAnchor(anchor);
}

void VDocument::setHeader(const QString &anchor)
{
    if (anchor == m_header) {
        return;
    }
    m_header = anchor;
    emit headerChanged(m_header);
}

void VDocument::setHtml(const QString &html)
{
    if (html == m_html) {
        return;
    }
    m_html = html;
    emit htmlChanged(m_html);
}

void VDocument::setLog(const QString &p_log)
{
    qDebug() << "JS:" << p_log;
    emit logChanged(p_log);
}

void VDocument::keyPressEvent(int p_key)
{
    emit keyPressed(p_key);
}
