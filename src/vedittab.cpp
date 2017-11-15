#include "vedittab.h"
#include <QApplication>
#include <QWheelEvent>

#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VEditTab::VEditTab(VFile *p_file, VEditArea *p_editArea, QWidget *p_parent)
    : QWidget(p_parent),
      m_file(p_file),
      m_isEditMode(false),
      m_outline(p_file),
      m_currentHeader(p_file, -1),
      m_editArea(p_editArea),
      m_checkFileChange(true),
      m_fileDiverged(false)
{
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
    updateStatus();
}

bool VEditTab::isEditMode() const
{
    return m_isEditMode;
}

bool VEditTab::isModified() const
{
    return false;
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
        updateStatus();
    } else if (isAncestorOf(p_now)) {
        emit getFocused();
        updateStatus();
    }
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

VEditTabInfo VEditTab::fetchTabInfo() const
{
    VEditTabInfo info;
    info.m_editTab = const_cast<VEditTab *>(this);

    return info;
}

const VHeaderPointer &VEditTab::getCurrentHeader() const
{
    return m_currentHeader;
}

const VTableOfContent &VEditTab::getOutline() const
{
    return m_outline;
}

void VEditTab::tryRestoreFromTabInfo(const VEditTabInfo &p_info)
{
    if (p_info.m_editTab != this) {
        m_infoToRestore.clear();
        return;
    }

    if (restoreFromTabInfo(p_info)) {
        m_infoToRestore.clear();
        return;
    }

    // Save it and restore later.
    m_infoToRestore = p_info;
}

void VEditTab::updateStatus()
{
    emit statusUpdated(fetchTabInfo());
}

void VEditTab::evaluateMagicWords()
{
}

bool VEditTab::tabHasFocus() const
{
    QWidget *wid = QApplication::focusWidget();
    return wid == this || isAncestorOf(wid);
}

void VEditTab::insertLink()
{
}

void VEditTab::applySnippet(const VSnippet *p_snippet)
{
    Q_UNUSED(p_snippet);
}

void VEditTab::applySnippet()
{
}

void VEditTab::checkFileChangeOutside()
{
    if (!m_checkFileChange) {
        return;
    }

    if (m_file->isChangedOutside()) {
        int ret = VUtils::showMessage(QMessageBox::Information,
                                      tr("Information"),
                                      tr("Note <span style=\"%1\">%2</span> has been modified by another program.")
                                        .arg(g_config->c_dataTextStyle).arg(m_file->fetchPath()),
                                      tr("Do you want to reload it?"),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::Yes,
                                      this);
        switch (ret) {
        case QMessageBox::Yes:
            reloadFromDisk();
            break;

        case QMessageBox::No:
            m_checkFileChange = false;
            m_fileDiverged = true;
            updateStatus();
            break;

        default:
            Q_ASSERT(false);
            break;
        }
    }
}

void VEditTab::reloadFromDisk()
{
    m_file->reload();
    m_fileDiverged = false;
    m_checkFileChange = true;
    reload();
}
