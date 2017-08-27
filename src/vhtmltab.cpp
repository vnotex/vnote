#include <QtWidgets>
#include <QFileInfo>
#include "vhtmltab.h"
#include "vedit.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vnotebook.h"
#include "dialog/vfindreplacedialog.h"
#include "veditarea.h"
#include "vconstants.h"

extern VConfigManager *g_config;

VHtmlTab::VHtmlTab(VFile *p_file, VEditArea *p_editArea,
                   OpenFileMode p_mode, QWidget *p_parent)
    : VEditTab(p_file, p_editArea, p_parent)
{
    V_ASSERT(m_file->getDocType() == DocType::Html);

    m_file->open();

    setupUI();

    if (p_mode == OpenFileMode::Edit) {
        showFileEditMode();
    } else {
        showFileReadMode();
    }
}

void VHtmlTab::setupUI()
{
    m_editor = new VEdit(m_file, this);
    connect(m_editor, &VEdit::textChanged,
            this, &VHtmlTab::handleTextChanged);
    connect(m_editor, &VEdit::saveAndRead,
            this, &VHtmlTab::saveAndRead);
    connect(m_editor, &VEdit::discardAndRead,
            this, &VHtmlTab::discardAndRead);
    connect(m_editor, &VEdit::editNote,
            this, &VHtmlTab::editFile);
    connect(m_editor, &VEdit::statusMessage,
            this, &VEditTab::statusMessage);
    connect(m_editor, &VEdit::vimStatusUpdated,
            this, &VEditTab::vimStatusUpdated);

    m_editor->reloadFile();

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_editor);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(mainLayout);
}

void VHtmlTab::handleTextChanged()
{
    V_ASSERT(m_file->isModifiable());

    if (m_modified) {
        return;
    }

    updateStatus();
}

void VHtmlTab::showFileReadMode()
{
    m_isEditMode = false;

    m_editor->setReadOnly(true);

    updateStatus();
}

void VHtmlTab::showFileEditMode()
{
    if (!m_file->isModifiable()) {
        return;
    }

    m_isEditMode = true;

    m_editor->beginEdit();
    m_editor->setFocus();

    updateStatus();
}

bool VHtmlTab::closeFile(bool p_forced)
{
    if (p_forced && m_isEditMode) {
        // Discard buffer content
        m_editor->reloadFile();
        m_editor->endEdit();

        showFileReadMode();
    } else {
        readFile();
    }

    return !m_isEditMode;
}

void VHtmlTab::editFile()
{
    if (m_isEditMode || !m_file->isModifiable()) {
        return;
    }

    showFileEditMode();
}

void VHtmlTab::readFile()
{
    if (!m_isEditMode) {
        return;
    }

    if (m_editor && m_editor->isModified()) {
        // Prompt to save the changes.
        int ret = VUtils::showMessage(QMessageBox::Information, tr("Information"),
                                      tr("Note <span style=\"%1\">%2</span> has been modified.")
                                        .arg(g_config->c_dataTextStyle).arg(m_file->getName()),
                                      tr("Do you want to save your changes?"),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                      QMessageBox::Save, this);
        switch (ret) {
        case QMessageBox::Save:
            saveFile();
            // Fall through

        case QMessageBox::Discard:
            m_editor->reloadFile();
            break;

        case QMessageBox::Cancel:
            // Nothing to do if user cancel this action
            return;

        default:
            qWarning() << "wrong return value from QMessageBox:" << ret;
            return;
        }
    }

    if (m_editor) {
        m_editor->endEdit();
    }

    showFileReadMode();
}

bool VHtmlTab::saveFile()
{
    if (!m_isEditMode || !m_editor->isModified()) {
        return true;
    }

    bool ret;
    // Make sure the file already exists. Temporary deal with cases when user delete or move
    // a file.
    QString filePath = m_file->retrivePath();
    if (!QFileInfo::exists(filePath)) {
        qWarning() << filePath << "being written has been removed";
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("File <span style=\"%1\">%2</span> being written has been removed.")
                              .arg(g_config->c_dataTextStyle).arg(filePath),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return false;
    }

    m_editor->saveFile();
    ret = m_file->save();
    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("Fail to write to disk when saving a note. Please try it again."),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        m_editor->setModified(true);
    }

    updateStatus();

    return ret;
}

void VHtmlTab::saveAndRead()
{
    saveFile();
    readFile();
}

void VHtmlTab::discardAndRead()
{
    readFile();
}

void VHtmlTab::scrollToAnchor(const VAnchor & /* p_anchor */)
{
}

void VHtmlTab::insertImage()
{
}

void VHtmlTab::findText(const QString &p_text, uint p_options, bool p_peek,
                        bool p_forward)
{
    if (p_peek) {
        m_editor->peekText(p_text, p_options);
    } else {
        m_editor->findText(p_text, p_options, p_forward);
    }
}

void VHtmlTab::replaceText(const QString &p_text, uint p_options,
                           const QString &p_replaceText, bool p_findNext)
{
    if (m_isEditMode) {
        m_editor->replaceText(p_text, p_options, p_replaceText, p_findNext);
    }
}

void VHtmlTab::replaceTextAll(const QString &p_text, uint p_options,
                              const QString &p_replaceText)
{
    if (m_isEditMode) {
        m_editor->replaceTextAll(p_text, p_options, p_replaceText);
    }
}

QString VHtmlTab::getSelectedText() const
{
    QTextCursor cursor = m_editor->textCursor();
    return cursor.selectedText();
}

void VHtmlTab::clearSearchedWordHighlight()
{
    m_editor->clearSearchedWordHighlight();
}

void VHtmlTab::zoom(bool /* p_zoomIn */, qreal /* p_step */)
{
}

void VHtmlTab::focusChild()
{
    m_editor->setFocus();
}

void VHtmlTab::requestUpdateVimStatus()
{
    m_editor->requestUpdateVimStatus();
}
