#ifndef VEDITTAB_H
#define VEDITTAB_H

#include <QWidget>
#include <QString>
#include <QPointer>
#include "vtoc.h"
#include "vfile.h"
#include "utils/vvim.h"
#include "vedittabinfo.h"

class VEditArea;

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

    bool isModified() const;

    void focusTab();

    virtual void requestUpdateOutline();

    virtual void requestUpdateCurHeader();

    // Scroll to anchor @p_anchor.
    virtual void scrollToAnchor(const VAnchor& p_anchor) = 0;

    VFile *getFile() const;

    // User requests to insert image.
    virtual void insertImage() = 0;

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
    virtual void decorateText(TextDecoration p_decoration) {Q_UNUSED(p_decoration);};

public slots:
    // Enter edit mode
    virtual void editFile() = 0;

    // Update status of current tab. Emit statusUpdated().
    virtual void updateStatus();

protected:
    void wheelEvent(QWheelEvent *p_event) Q_DECL_OVERRIDE;

    // Called when VEditTab get focus. Should focus the proper child widget.
    virtual void focusChild() = 0;

    // Called to zoom in/out content.
    virtual void zoom(bool p_zoomIn, qreal p_step = 0.25) = 0;

    // Create a filled VEditTabInfo.
    virtual VEditTabInfo createEditTabInfo();

    // File related to this tab.
    QPointer<VFile> m_file;
    bool m_isEditMode;
    bool m_modified;
    VToc m_toc;
    VAnchor m_curHeader;
    VEditArea *m_editArea;

signals:
    void getFocused();

    void outlineChanged(const VToc &p_toc);

    void curHeaderChanged(const VAnchor &p_anchor);

    // The status of current tab has updates.
    void statusUpdated(const VEditTabInfo &p_info);

    // Emit when want to show message in status bar.
    void statusMessage(const QString &p_msg);

    void vimStatusUpdated(const VVim *p_vim);

private slots:
    // Called when app focus changed.
    void handleFocusChanged(QWidget *p_old, QWidget *p_now);
};

#endif // VEDITTAB_H
