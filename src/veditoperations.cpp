#include <QTextCursor>
#include <QTextDocument>
#include <QFontMetrics>
#include "vedit.h"
#include "veditoperations.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VEditOperations::VEditOperations(VEdit *p_editor, VFile *p_file)
    : QObject(p_editor), m_editor(p_editor), m_file(p_file), m_expandTab(false),
      m_keyState(KeyState::Normal), m_pendingTime(2)
{
    updateTabSettings();
}

void VEditOperations::insertTextAtCurPos(const QString &p_text)
{
    QTextCursor cursor = m_editor->textCursor();
    cursor.insertText(p_text);
    m_editor->setTextCursor(cursor);
}

VEditOperations::~VEditOperations()
{
}

void VEditOperations::updateTabSettings()
{
    if (vconfig.getTabStopWidth() > 0) {
        QFontMetrics metrics(vconfig.getMdEditFont());
        m_editor->setTabStopWidth(vconfig.getTabStopWidth() * metrics.width(' '));
    }
    m_expandTab = vconfig.getIsExpandTab();
    if (m_expandTab && (vconfig.getTabStopWidth() > 0)) {
        m_tabSpaces = QString(vconfig.getTabStopWidth(), ' ');
    }
}
