#ifndef VEDITTAB_H
#define VEDITTAB_H

#include <QWidget>
#include <QString>
#include <QPointer>
#include "vtableofcontent.h"
#include "vfile.h"
#include "utils/vvim.h"
#include "vedittabinfo.h"
#include "vwordcountinfo.h"

class VEditArea;
class VSnippet;

// VEditTab is the base class of an edit tab inside VEditWindow.
class VEditTab : public QWidget
{
    Q_OBJECT

public:
    VEditTab(VFile *p_file, VEditArea *p_editArea, QWidget *p_parent = 0);

    virtual ~VEditTab();

    // Close current tab.
    // @p_forced: if true, discard the changes.
    virtual bool closeFile(bool p_forced) = 0;

    // Enter read mode.
    virtual void readFile() = 0;

    // Save file.
    virtual bool saveFile() = 0;

    bool isEditMode() const;

    virtual bool isModified() const;

    void focusTab();

    // Whether this tab has focus.
    bool tabHasFocus() const;

    // Scroll to @p_header.
    // Will emit currentHeaderChanged() if @p_header is valid.
    virtual void scrollToHeader(const VHeaderPointer &p_header) { Q_UNUSED(p_header) }

    VFile *getFile() const;

    // User requests to insert image.
    virtual void insertImage() = 0;

    // User requests to insert link.
    virtual void insertLink();

    // Search @p_text in current note.
    virtual void findText(const QString &p_text, uint p_options, bool p_peek,
                          bool p_forward = true) = 0;

    // Replace @p_text with @p_replaceText in current note.
    virtual void replaceText(const QString &p_text, uint p_options,
                             const QString &p_replaceText, bool p_findNext) = 0;

    virtual void replaceTextAll(const QString &p_text, uint p_options,
                                const QString &p_replaceText) = 0;

    // Return selected text.
    virtual QString getSelectedText() const = 0;

    virtual void clearSearchedWordHighlight() = 0;

    // Request current tab to propogate its status about Vim.
    virtual void requestUpdateVimStatus() = 0;

    // Insert decoration markers or decorate selected text.
    virtual void decorateText(TextDecoration p_decoration, int p_level = -1)
    {
        Q_UNUSED(p_decoration);
        Q_UNUSED(p_level);
    }

    // Create a filled VEditTabInfo.
    virtual VEditTabInfo fetchTabInfo(VEditTabInfo::InfoType p_type = VEditTabInfo::InfoType::All) const;

    const VTableOfContent &getOutline() const;

    const VHeaderPointer &getCurrentHeader() const;

    // Restore status from @p_info.
    // If this tab is not ready yet, it will restore once it is ready.
    void tryRestoreFromTabInfo(const VEditTabInfo &p_info);

    // Emit signal to update current status.
    virtual void updateStatus();

    // Called by evaluateMagicWordsByCaptain() to evaluate the magic words.
    virtual void evaluateMagicWords();

    // Insert snippet @p_snippet.
    virtual void applySnippet(const VSnippet *p_snippet);

    // Prompt for user to apply a snippet.
    virtual void applySnippet();

    // Check whether this file has been changed outside.
    void checkFileChangeOutside();

    // Reload the editor from file.
    virtual void reload() = 0;

    // Reload file from disk and reload the editor.
    void reloadFromDisk();

    // Handle the change of file or directory, such as the file has been moved.
    virtual void handleFileOrDirectoryChange(bool p_isFile, UpdateAction p_act);

    // Fetch tab stat info.
    virtual VWordCountInfo fetchWordCountInfo(bool p_editMode) const;

    virtual void toggleLivePreview()
    {
    }

public slots:
    // Enter edit mode
    virtual void editFile() = 0;

    virtual void handleVimCmdCommandCancelled();

    virtual void handleVimCmdCommandFinished(VVim::CommandLineType p_type, const QString &p_cmd);

    virtual void handleVimCmdCommandChanged(VVim::CommandLineType p_type, const QString &p_cmd);

    virtual QString handleVimCmdRequestNextCommand(VVim::CommandLineType p_type, const QString &p_cmd);

    virtual QString handleVimCmdRequestPreviousCommand(VVim::CommandLineType p_type, const QString &p_cmd);

    virtual QString handleVimCmdRequestRegister(int p_key, int p_modifiers);

protected:
    void wheelEvent(QWheelEvent *p_event) Q_DECL_OVERRIDE;

    // Called when VEditTab get focus. Should focus the proper child widget.
    virtual void focusChild() = 0;

    // Called to zoom in/out content.
    virtual void zoom(bool p_zoomIn, qreal p_step = 0.25) = 0;

    // Restore from @p_fino.
    // Return true if succeed.
    virtual bool restoreFromTabInfo(const VEditTabInfo &p_info) = 0;

    // Write modified buffer content to backup file.
    virtual void writeBackupFile();

    // File related to this tab.
    QPointer<VFile> m_file;

    bool m_isEditMode;

    // Table of content of this tab.
    VTableOfContent m_outline;

    // Current header in m_outline of this tab.
    VHeaderPointer m_currentHeader;

    VEditArea *m_editArea;

    // Tab info to restore from once ready.
    VEditTabInfo m_infoToRestore;

    // Whether check the file change outside.
    bool m_checkFileChange;

    // File has diverged from disk.
    bool m_fileDiverged;

    // Tab has been ready or not.
    int m_ready;

    // Whether backup file is enabled.
    bool m_enableBackupFile;

signals:
    void getFocused();

    void outlineChanged(const VTableOfContent &p_outline);

    void currentHeaderChanged(const VHeaderPointer &p_header);

    // The status of current tab has updates.
    void statusUpdated(const VEditTabInfo &p_info);

    // Emit when want to show message in status bar.
    void statusMessage(const QString &p_msg);

    void vimStatusUpdated(const VVim *p_vim);

    // Request to close itself.
    void closeRequested(VEditTab *p_tab);

    // Request main window to show Vim cmd line.
    void triggerVimCmd(VVim::CommandLineType p_type);

private slots:
    // Called when app focus changed.
    void handleFocusChanged(QWidget *p_old, QWidget *p_now);
};

#endif // VEDITTAB_H
