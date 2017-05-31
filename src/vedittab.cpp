#include "vedittab.h"
#include <QApplication>
#include <QWheelEvent>

VEditTab::VEditTab(VFile *p_file, VEditArea *p_editArea, QWidget *p_parent)
    : QWidget(p_parent), m_file(p_file), m_isEditMode(false),
      m_modified(false), m_editArea(p_editArea)
{
    m_toc.m_file = m_file;
    m_curHeader.m_file = m_file;

    connect(qApp, &QApplication::focusChanged,
            this, &VEditTab::handleFocusChanged);
}

VEditTab::~VEditTab()
{
    if (m_file) {
        m_file->close();
    }
}

void VEditTab::focusTab()
{
    focusChild();
    emit getFocused();
}

bool VEditTab::isEditMode() const
{
    return m_isEditMode;
}

bool VEditTab::isModified() const
{
    return m_modified;
}

VFile *VEditTab::getFile() const
{
    return m_file;
}

void VEditTab::handleFocusChanged(QWidget * /* p_old */, QWidget *p_now)
{
    if (p_now == this) {
        // When VEditTab get focus, it should focus to current widget.
        focusChild();

        emit getFocused();
    } else if (isAncestorOf(p_now)) {
        emit getFocused();
    }
}

void VEditTab::requestUpdateCurHeader()
{
    emit curHeaderChanged(m_curHeader);
}

void VEditTab::requestUpdateOutline()
{
    emit outlineChanged(m_toc);
}

void VEditTab::wheelEvent(QWheelEvent *p_event)
{
    QPoint angle = p_event->angleDelta();
    Qt::KeyboardModifiers modifiers = p_event->modifiers();
    if (!angle.isNull() && (angle.y() != 0) && (modifiers & Qt::ControlModifier)) {
        // Zoom in/out current tab.
        zoom(angle.y() > 0);

        p_event->accept();
        return;
    }

    p_event->ignore();
}
