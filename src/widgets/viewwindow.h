#ifndef VIEWWINDOW_H
#define VIEWWINDOW_H

#include <QFrame>
#include <QIcon>
#include <QSharedPointer>

#include <buffer/buffer.h>
#include <core/global.h>
#include <core/fileopenparameters.h>

#include "viewwindowtoolbarhelper.h"
#include "viewwindowsession.h"

class QVBoxLayout;
class QTimer;
class QToolBar;
class QMenu;
class QActionGroup;

namespace vnotex
{
    class ViewSplit;
    class DragDropAreaIndicator;
    class DragDropAreaIndicatorInterface;
    class OutlineProvider;
    class EditReadDiscardAction;
    class FindAndReplaceWidget;
    class StatusWidget;
    class FloatingWidget;

    class ViewWindow : public QFrame
    {
        Q_OBJECT
    public:
        enum WindowFlag
        {
            None = 0,
            AutoReload = 0x1
        };
        Q_DECLARE_FLAGS(WindowFlags, WindowFlag);

        struct WordCountInfo
        {
            bool m_isSelection = false;
            int m_wordCount = 0;
            int m_charWithoutSpaceCount = 0;
            int m_charWithSpaceCount = 0;
        };

        explicit ViewWindow(QWidget *p_parent = nullptr);

        virtual ~ViewWindow();

        Buffer *getBuffer() const;

        void attachToBuffer(Buffer *p_buffer, const QSharedPointer<FileOpenParameters> &p_paras);

        void detachFromBuffer(bool p_quiet = false);

        // User request to open the buffer attached to this ViewWindow again.
        virtual void openTwice(const QSharedPointer<FileOpenParameters> &p_paras) = 0;

        virtual QIcon getIcon() const;

        virtual QString getName() const;

        QString getTitle() const;

        ViewSplit *getViewSplit() const;
        void setViewSplit(ViewSplit *p_split);

        QSharedPointer<QWidget> statusWidget();

        // Whether should show standalone status widget.
        void setStatusWidgetVisible(bool p_visible);

        // Get latest content from editor instead of buffer.
        virtual QString getLatestContent() const = 0;

        // Will be called before close.
        // Return true if it is OK to proceed.
        bool aboutToClose(bool p_force);

        ViewWindowMode getMode() const;
        virtual void setMode(ViewWindowMode p_mode) = 0;

        virtual QSharedPointer<OutlineProvider> getOutlineProvider();

        // Called by upside.
        void checkFileMissingOrChangedOutsidePeriodically();

        virtual ViewWindowSession saveSession() const;

        WindowFlags getWindowFlags() const;
        void setWindowFlags(WindowFlags p_flags);

        virtual void applySnippet(const QString &p_name) = 0;

        virtual void applySnippet() = 0;

        // Take ownership of @p_widget.
        // Return the result from the FloatingWidget.
        QVariant showFloatingWidget(FloatingWidget *p_widget);

        bool isSessionEnabled() const;

        virtual QString selectedText() const;

        virtual void fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const = 0;

    public slots:
        virtual void handleEditorConfigChange() = 0;

        void findNext(const QString &p_text, FindOptions p_options);

        void replace(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

        void replaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

    signals:
        // Emit when the attached buffer is changed.
        void bufferChanged();

        // Emit when this ViewWindow get focused.
        void focused(ViewWindow *p_win);

        // Emit when the status of this ViewWindow has changed,
        // such as modification state.
        void statusChanged();

        void modeChanged();

        void nameChanged();

        void attachmentChanged();

    protected:
        enum TypeAction
        {
            Heading1,
            Heading2,
            Heading3,
            Heading4,
            Heading5,
            Heading6,
            HeadingNone,

            // Make sure the order is identical with ViewWindowToolBarHelper::Action.
            Bold,
            Italic,
            Strikethrough,
            UnorderedList,
            OrderedList,
            TodoList,
            CheckedTodoList,
            Code,
            CodeBlock,
            Math,
            MathBlock,
            Quote,
            Link,
            Image,
            Table,
            Mark
        };

    protected slots:
        // Handle current buffer change.
        virtual void handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras) = 0;

        // Handle all kinds of type action.
        virtual void handleTypeAction(TypeAction p_action);

        virtual void handleSectionNumberOverride(OverrideState p_state);

        virtual void handleImageHostChanged(const QString &p_hostName);

        virtual void handleFindTextChanged(const QString &p_text, FindOptions p_options);

        virtual void handleFindNext(const QStringList &p_texts, FindOptions p_options);

        virtual void handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

        virtual void handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

        virtual void handleFindAndReplaceWidgetClosed();

        virtual void handleFindAndReplaceWidgetOpened();

        virtual void toggleDebug();

        virtual void print();

    protected:
        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

        void wheelEvent(QWheelEvent *p_event) Q_DECL_OVERRIDE;

        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    protected:
        void setCentralWidget(QWidget *p_widget);

        void replaceCentralWidget(QWidget *p_widget);

        void addTopWidget(QWidget *p_widget);

        void addToolBar(QToolBar *p_bar);

        void addBottomWidget(QWidget *p_widget);

        void setStatusWidget(const QSharedPointer<StatusWidget> &p_widget);

        // Provide some common actions of tool bar for ViewWindow.
        QAction *addAction(QToolBar *p_toolBar, ViewWindowToolBarHelper::Action p_action);

        // ViewWindow should set editor's modification state.
        virtual void setModified(bool p_modified) = 0;

        // Return true if it is OK to proceed.
        virtual bool aboutToCloseInternal(bool p_force);

        // Sync buffer changes to editor.
        virtual void syncEditorFromBuffer() = 0;

        // Sync buffer content changes to editor.
        virtual void syncEditorFromBufferContent() = 0;

        // Whether we are in a mode that enable us to insert text.
        bool inModeCanInsert() const;

        virtual void detachFromBufferInternal();

        virtual void scrollUp() = 0;

        virtual void scrollDown() = 0;

        virtual void zoom(bool p_zoomIn) = 0;

        void showZoomFactor(qreal p_factor);

        void showZoomDelta(int p_delta);

        void showFindAndReplaceWidget();

        void hideFindAndReplaceWidget();

        bool findAndReplaceWidgetVisible() const;

        // @p_currentMatchIndex: 0-based.
        void showFindResult(const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex);

        void showReplaceResult(const QString &p_text, int p_totalReplaces);

        void edit();

        void read(bool p_save);

        // Show message in status widget if exists. Otherwise, show it in the mainwindow's status widget.
        void showMessage(const QString p_msg);

        void updateLastFindInfo(const QStringList &p_texts, FindOptions p_options);

        virtual QPoint getFloatingWidgetPosition();

        virtual void updateViewModeMenu(QMenu *p_menu);

        virtual void clearHighlights();

        static QToolBar *createToolBar(QWidget *p_parent = nullptr);

        // The revision of the buffer of the last sync content.
        int m_bufferRevision = 0;

        // Whether there is change of editor config since last update.
        // Subclass should maintain it.
        int m_editorConfigRevision = 0;

        ViewWindowMode m_mode = ViewWindowMode::Invalid;

        // Managed by QObject.
        FindAndReplaceWidget *m_findAndReplace = nullptr;

        QSharedPointer<FileOpenParameters> m_openParas;

    private:
        struct FindInfo
        {
            QStringList m_texts;
            FindOptions m_options;
        };

        void setupUI();

        void initIcons();

        void setupShortcuts();

        void discardChangesAndRead();

        void checkBackupFileOfPreviousSession();

        DragDropAreaIndicator *getAttachmentDragDropArea();

        const QIcon &getAttachmentIcon(Buffer *p_buffer) const;

        // A wrapper of saveInternal().
        bool save(bool p_force = false);

        // Save buffer content to file.
        bool saveInternal(bool p_force = false);

        // Discard changes and reload buffer content from file.
        bool reload();

        void updateEditReadDiscardActionState(EditReadDiscardAction *p_act);

        // Return code of checkFileMissingOrChangedOutside().
        enum
        {
            // File is not missing or changed outside.
            Normal,
            // Force save the buffer to file or reload the buffer from file.
            SavedOrReloaded,
            // Discard the buffer.
            Discarded,
            // User do not handle it.
            Failed
        };
        int checkFileMissingOrChangedOutside();

        void findNextOnLastFind(bool p_forward = true);

        void handleBufferChanged(const QSharedPointer<FileOpenParameters> &p_paras);

        void updateImageHostMenu();

        void executeHook(FileOpenParameters::Hook p_hook) const;

        static ViewWindow::TypeAction toolBarActionToTypeAction(ViewWindowToolBarHelper::Action p_action);

        Buffer *m_buffer = nullptr;

        // Whether check file missing or changed outside.
        bool m_fileChangeCheckEnabled = true;

        // Last find info.
        FindInfo m_findInfo;

        // Whether this ViewWindow should be recored to session.
        bool m_sessionEnabled = true;

        // Null if this window has not been added to any split.
        ViewSplit *m_viewSplit = nullptr;

        // Managed by QObject.
        QWidget *m_centralWidget = nullptr;

        // Managed by QObject.
        QVBoxLayout *m_mainLayout = nullptr;

        // Managed by QObject.
        QVBoxLayout *m_topLayout = nullptr;

        // Managed by QObject.
        QVBoxLayout *m_bottomLayout = nullptr;

        QTimer *m_syncBufferContentTimer = nullptr;

        // Managed by QObject.
        // Allocated on necessary. Use getAttachmentDragDropArea() to access.
        DragDropAreaIndicator *m_attachmentDragDropIndicator = nullptr;

        QScopedPointer<DragDropAreaIndicatorInterface> m_attachmentDragDropIndicatorInterface;

        // Managed by QObject.
        QToolBar *m_toolBar = nullptr;

        QSharedPointer<StatusWidget> m_statusWidget;

        EditReadDiscardAction *m_editReadDiscardAct = nullptr;

        WindowFlags m_flags = WindowFlag::None;

        QMenu *m_imageHostMenu = nullptr;

        QActionGroup *m_imageHostActionGroup = nullptr;

        bool m_statusWidgetInBottomLayout = false;

        static QIcon s_savedIcon;
        static QIcon s_modifiedIcon;
    };
} // ns vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::ViewWindow::WindowFlags)

#endif // VIEWWINDOW_H
