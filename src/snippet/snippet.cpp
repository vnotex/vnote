#include "snippet.h"

#include <QDebug>

#include <utils/utils.h>

using namespace vnotex;

const QString Snippet::c_defaultCursorMark = QStringLiteral("@@");

const QString Snippet::c_defaultSelectionMark = QStringLiteral("$$");

Snippet::Snippet(const QString &p_name)
    : m_name(p_name)
{
}

Snippet::Snippet(const QString &p_name,
                 const QString &p_description,
                 const QString &p_content,
                 int p_shortcut,
                 bool p_indentAsFirstLine,
                 const QString &p_cursorMark,
                 const QString &p_selectionMark)
    : m_type(Type::Text),
      m_name(p_name),
      m_description(p_description),
      m_content(p_content),
      m_shortcut(p_shortcut),
      m_indentAsFirstLine(p_indentAsFirstLine),
      m_cursorMark(p_cursorMark),
      m_selectionMark(p_selectionMark)
{
}

QJsonObject Snippet::toJson() const
{
    QJsonObject obj;

    obj[QStringLiteral("type")] = static_cast<int>(m_type);
    obj[QStringLiteral("description")] = m_description;
    obj[QStringLiteral("content")] = m_content;
    obj[QStringLiteral("shortcut")] = m_shortcut;
    obj[QStringLiteral("indent_as_first_line")] = m_indentAsFirstLine;
    obj[QStringLiteral("cursor_mark")] = m_cursorMark;
    obj[QStringLiteral("selection_mark")] = m_selectionMark;

    return obj;
}

void Snippet::fromJson(const QJsonObject &p_jobj)
{
    m_type = static_cast<Type>(p_jobj[QStringLiteral("type")].toInt());
    m_description = p_jobj[QStringLiteral("description")].toString();
    m_content = p_jobj[QStringLiteral("content")].toString();
    m_shortcut = p_jobj[QStringLiteral("shortcut")].toInt();
    m_indentAsFirstLine = p_jobj[QStringLiteral("indent_as_first_line")].toBool();
    m_cursorMark = p_jobj[QStringLiteral("cursor_mark")].toString();
    m_selectionMark = p_jobj[QStringLiteral("selection_mark")].toString();
}

bool Snippet::isValid() const
{
    return !m_name.isEmpty() && m_type != Type::Invalid;
}

const QString &Snippet::getName() const
{
    return m_name;
}

int Snippet::getShortcut() const
{
    return m_shortcut;
}

QString Snippet::getShortcutString() const
{
    if (m_shortcut == InvalidShortcut) {
        return QString();
    } else {
        return Utils::intToString(m_shortcut, 2);
    }
}

Snippet::Type Snippet::getType() const
{
    return m_type;
}

const QString &Snippet::getCursorMark() const
{
    return m_cursorMark;
}

const QString &Snippet::getSelectionMark() const
{
    return m_selectionMark;
}

bool Snippet::isIndentAsFirstLineEnabled() const
{
    return m_indentAsFirstLine;
}

const QString &Snippet::getContent() const
{
    return m_content;
}

const QString &Snippet::getDescription() const
{
    return m_description;
}

QString Snippet::apply(const QString &p_selectedText,
                       const QString &p_indentationSpaces,
                       int &p_cursorOffset)
{
    QString appliedText;
    p_cursorOffset = 0;
    if (!isValid() || m_content.isEmpty()) {
        qWarning() << "failed to apply an invalid snippet" << m_name;
        return appliedText;
    }

    // Indent each line after the first line.
    if (m_indentAsFirstLine && !p_indentationSpaces.isEmpty()) {
        auto lines = m_content.split(QLatin1Char('\n'));
        Q_ASSERT(!lines.isEmpty());
        appliedText = lines[0];
        for (int i = 1; i < lines.size(); ++i) {
            appliedText += QLatin1Char('\n') + p_indentationSpaces + lines[i];
        }
    } else {
        appliedText = m_content;
    }

    // Find the cursor mark and break the content.
    QString secondPart;
    if (!m_cursorMark.isEmpty()) {
        QStringList parts = appliedText.split(m_cursorMark);
        Q_ASSERT(!parts.isEmpty());
        if (parts.size() > 2) {
            qWarning() << "failed to apply snippet with multiple cursor marks" << m_name;
            return QString();
        }

        appliedText = parts[0];
        if (parts.size() == 2) {
            secondPart = parts[1];
        }
    }

    // Replace the selection mark.
    if (!m_selectionMark.isEmpty()) {
        if (!appliedText.isEmpty()) {
            appliedText.replace(m_selectionMark, p_selectedText);
        }

        if (!secondPart.isEmpty()) {
            secondPart.replace(m_selectionMark, p_selectedText);
        }
    }

    p_cursorOffset = appliedText.size();
    return appliedText + secondPart;
}

bool Snippet::isReadOnly() const
{
    return m_readOnly;
}

void Snippet::setReadOnly(bool p_readOnly)
{
    m_readOnly = p_readOnly;
}

void Snippet::setType(Type p_type)
{
    m_type = p_type;
}
