#include "viewwindow.h"

#include <QVBoxLayout>
#include <QAction>
#include <QTimer>
#include <QMenu>
#include <QToolBar>
#include <QDebug>
#include <QToolButton>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QApplication>
#include <QFocusEvent>
#include <QShortcut>
#include <QWheelEvent>

#include <core/fileopenparameters.h>
#include "toolbarhelper.h"
#include "vnotex.h"
#include <utils/iconutils.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include "messageboxhelper.h"
#include "editreaddiscardaction.h"
#include "viewsplit.h"
#include "attachmentpopup.h"
#include "outlinepopup.h"
#include "dragdropareaindicator.h"
#include "attachmentdragdropareaindicator.h"
#include "exception.h"
#include "findandreplacewidget.h"
#include "editors/statuswidget.h"
#include "propertydefs.h"

using namespace vnotex;

QIcon ViewWindow::s_savedIcon;

QIcon ViewWindow::s_modifiedIcon;

ViewWindow::ViewWindow(QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI();

    initIcons();

    setupShortcuts();

    // Need to use this global-wise way, especially for the WebView.
    connect(qApp, &QApplication::focusChanged,
            this, [this](QWidget *p_old, QWidget *p_now) {
                if (p_now == this || isAncestorOf(p_now)) {
                    bool hadFocus = p_old && (p_old == this || isAncestorOf(p_old));
                    if (!hadFocus) {
                        emit focused(this);
                    }
                }
            });

    m_syncBufferContentTimer = new QTimer(this);
    m_syncBufferContentTimer->setSingleShot(true);
    m_syncBufferContentTimer->setInterval(500);
    connect(m_syncBufferContentTimer, &QTimer::timeout,
            this, [this]() {
                Q_ASSERT(getBuffer());
                if (getBuffer()->getRevision() != m_bufferRevision) {
                    syncEditorFromBufferContent();
                }
            });

    connect(this, &ViewWindow::bufferChanged,
            this, [this]() {
                auto buffer = getBuffer();
                if (buffer) {
                    connect(buffer, &Buffer::modified,
                            this, &ViewWindow::statusChanged);

                    // To make it convenient to disconnect, do not connect directly to
                    // the timer.
                    connect(buffer, &Buffer::contentsChanged,
                            this, [this]() {
                                m_syncBufferContentTimer->start();
                            });

                    connect(buffer, &Buffer::nameChanged,
                            this, &ViewWindow::nameChanged);

                    connect(buffer, &Buffer::attachmentChanged,
                            this, &ViewWindow::attachmentChanged);
                }

                handleBufferChangedInternal();
            });
}

ViewWindow::~ViewWindow()
{
    Q_ASSERT(!m_buffer);
    if (m_statusWidget) {
        m_statusWidget->setParent(nullptr);
    }
}

void ViewWindow::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    m_topLayout = new QVBoxLayout();
    m_topLayout->setContentsMargins(0, 0, 0, 0);
    m_topLayout->setSpacing(0);
    m_mainLayout->addLayout(m_topLayout, 0);

    m_bottomLayout = new QVBoxLayout();
    m_bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_bottomLayout->setSpacing(0);
    m_mainLayout->addLayout(m_bottomLayout, 0);
}

void ViewWindow::initIcons()
{
    if (!s_savedIcon.isNull()) {
        return;
    }

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const QString savedIconName("saved.svg");
    const QString unsavedIconFg("base#icon#warning#fg");
    s_savedIcon = IconUtils::fetchIcon(themeMgr.getIconFile(savedIconName));
    s_modifiedIcon = IconUtils::fetchIcon(themeMgr.getIconFile(savedIconName),
                                          themeMgr.paletteColor(unsavedIconFg));
}

Buffer *ViewWindow::getBuffer() const
{
    return m_buffer;
}

void ViewWindow::attachToBuffer(Buffer *p_buffer)
{
    Q_ASSERT(p_buffer);
    Q_ASSERT(m_buffer != p_buffer);

    detachFromBuffer();

    m_buffer = p_buffer;
    m_buffer->attachViewWindow(this);

    emit bufferChanged();

    if (m_buffer->getAttachViewWindowCount() == 1) {
        QTimer::singleShot(1000, this, &ViewWindow::checkBackupFileOfPreviousSession);
    }
}

void ViewWindow::detachFromBuffer(bool p_quiet)
{
    if (!m_buffer) {
        return;
    }

    detachFromBufferInternal();

    disconnect(this, 0, m_buffer, 0);
    disconnect(m_buffer, 0, this, 0);

    m_buffer->detachViewWindow(this);
    m_buffer = nullptr;

    if (!p_quiet) {
        emit bufferChanged();
    }
}

const QIcon &ViewWindow::getIcon() const
{
    if (m_buffer) {
        return m_buffer->isModified() ? s_modifiedIcon : s_savedIcon;
    } else {
        return s_savedIcon;
    }
}

QString ViewWindow::getName() const
{
    if (m_buffer) {
        return m_buffer->getName();
    } else {
        return tr("[No Buffer]");
    }
}

ViewSplit *ViewWindow::getViewSplit() const
{
    return m_viewSplit;
}

void ViewWindow::setViewSplit(ViewSplit *p_split)
{
    m_viewSplit = p_split;
}

QString ViewWindow::getTitle() const
{
    if (m_buffer) {
        return m_buffer->getPath();
    } else {
        return tr("[No Buffer]");
    }
}

void ViewWindow::setCentralWidget(QWidget *p_widget)
{
    Q_ASSERT(!m_centralWidget);
    m_centralWidget = p_widget;

    // Insert after top layout.
    m_mainLayout->insertWidget(1, m_centralWidget, 1);

    setFocusProxy(m_centralWidget);

    m_centralWidget->show();
}

void ViewWindow::addTopWidget(QWidget *p_widget)
{
    m_topLayout->addWidget(p_widget);
}

void ViewWindow::addBottomWidget(QWidget *p_widget)
{
    m_bottomLayout->addWidget(p_widget);
}

void ViewWindow::setStatusWidget(const QSharedPointer<StatusWidget> &p_widget)
{
    m_statusWidget = p_widget;
    m_bottomLayout->insertWidget(0, p_widget.data());
    p_widget->show();
}

QSharedPointer<QWidget> ViewWindow::statusWidget()
{
    return m_statusWidget;
}

void ViewWindow::setStatusWidgetVisible(bool p_visible)
{
    if (m_statusWidget) {
        if (p_visible) {
            // Need to add it to the right layout again since global status widget will set it as
            // a child of the main status bar.
            setStatusWidget(m_statusWidget);
        } else {
            m_statusWidget->hide();
        }
    }
}

QAction *ViewWindow::addAction(QToolBar *p_toolBar, ViewWindowToolBarHelper::Action p_action)
{
    QAction *act = nullptr;
    switch (p_action) {
    case ViewWindowToolBarHelper::Save:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        connect(this, &ViewWindow::statusChanged,
                this, [this, act]() {
                    auto buffer = getBuffer();
                    act->setEnabled(buffer ? !buffer->isReadOnly() && buffer->isModified() : false);
                });
        connect(act, &QAction::triggered,
                this, [this]() {
                    if (Normal != checkFileMissingOrChangedOutside()) {
                        return;
                    }

                    save(false);
                });
        break;
    }

    case ViewWindowToolBarHelper::EditReadDiscard:
    {
        // A combined button with Edit/Read/Discard.
        Q_ASSERT(!m_editReadDiscardAct);
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        m_editReadDiscardAct = dynamic_cast<EditReadDiscardAction *>(act);
        connect(this, &ViewWindow::modeChanged,
                this, [this]() {
                    updateEditReadDiscardActionState(m_editReadDiscardAct);
                });
        connect(m_editReadDiscardAct, QOverload<EditReadDiscardAction::Action>::of(&EditReadDiscardAction::triggered),
                this, [this](EditReadDiscardAction::Action p_act) {
                    switch (p_act) {
                    case EditReadDiscardAction::Action::Edit:
                        edit();
                        break;
                    case EditReadDiscardAction::Action::Read:
                        read(true);
                        break;
                    case EditReadDiscardAction::Action::Discard:
                        read(false);
                        break;
                    }
                });
        break;
    }

    case ViewWindowToolBarHelper::TypeHeading:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        connect(this, &ViewWindow::modeChanged,
                this, [this, act]() {
                    act->setEnabled(inModeCanInsert() && getBuffer() && !getBuffer()->isReadOnly());
                });

        auto toolBtn = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(act));
        Q_ASSERT(toolBtn);
        // MUST use the menu for the event. QToolButton won't trigger the event
        // if we call action->trigger().
        connect(toolBtn->menu(), &QMenu::triggered,
                this, [this](QAction *p_act) {
                    TypeAction action = static_cast<TypeAction>(TypeAction::Heading1 + p_act->data().toInt() - 1);
                    handleTypeAction(action);
                });
        break;
    }

    case ViewWindowToolBarHelper::TypeBold:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeItalic:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeStrikethrough:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeUnorderedList:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeOrderedList:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeTodoList:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeCheckedTodoList:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeCode:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeCodeBlock:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeMath:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeMathBlock:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeQuote:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeLink:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeImage:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeTable:
        Q_FALLTHROUGH();
    case ViewWindowToolBarHelper::TypeMark:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        connect(this, &ViewWindow::modeChanged,
                this, [this, act]() {
                    act->setEnabled(inModeCanInsert() && getBuffer() && !getBuffer()->isReadOnly());
                });
        connect(act, &QAction::triggered,
                this, [this, p_action]() {
                    handleTypeAction(toolBarActionToTypeAction(p_action));
                });
        break;
    }

    case ViewWindowToolBarHelper::Attachment:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        auto popup = static_cast<AttachmentPopup *>(static_cast<QToolButton *>(p_toolBar->widgetForAction(act))->menu());
        connect(this, &ViewWindow::bufferChanged,
                this, [this, act, popup]() {
                    auto buffer = getBuffer();
                    act->setEnabled(buffer ? buffer->isAttachmentSupported() : false);
                    act->setIcon(getAttachmentIcon(buffer));
                    popup->setBuffer(buffer);
                });
        connect(this, &ViewWindow::attachmentChanged,
                this, [this, act]() {
                    act->setIcon(getAttachmentIcon(getBuffer()));
                });
        break;
    }

    case ViewWindowToolBarHelper::Outline:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        auto popup = static_cast<OutlinePopup *>(static_cast<QToolButton *>(p_toolBar->widgetForAction(act))->menu());
        popup->setOutlineProvider(getOutlineProvider());
        break;
    }

    case ViewWindowToolBarHelper::FindAndReplace:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        connect(act, &QAction::triggered,
                this, [this]() {
                    if (findAndReplaceWidgetVisible()) {
                        hideFindAndReplaceWidget();
                    } else {
                        showFindAndReplaceWidget();
                    }
                });
        break;
    }

    case ViewWindowToolBarHelper::SectionNumber:
    {
        act = ViewWindowToolBarHelper::addAction(p_toolBar, p_action);
        connect(this, &ViewWindow::modeChanged,
                this, [this, act]() {
                    act->setEnabled(inModeCanInsert() && getBuffer() && !getBuffer()->isReadOnly());
                });
        auto toolBtn = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(act));
        Q_ASSERT(toolBtn);
        connect(toolBtn->menu(), &QMenu::triggered,
                this, [this](QAction *p_act) {
                    OverrideState state = static_cast<OverrideState>(p_act->data().toInt());
                    handleSectionNumberOverride(state);
                });
        break;
    }
    }

    return act;
}

const QIcon &ViewWindow::getAttachmentIcon(Buffer *p_buffer) const
{
    static QIcon emptyIcon = ToolBarHelper::generateIcon("attachment_editor.svg");
    static QIcon fullIcon = ToolBarHelper::generateIcon("attachment_full_editor.svg");
    if (p_buffer && p_buffer->hasAttachment()) {
        return fullIcon;
    } else {
        return emptyIcon;
    }
}

bool ViewWindow::saveInternal(bool p_force)
{
    if (m_buffer) {
        auto code = m_buffer->save(p_force);
        if (code == Buffer::OperationCode::Success) {
            setModified(false);
            return true;
        }
        return false;
    }
    return true;
}

bool ViewWindow::aboutToClose(bool p_force)
{
    if (!aboutToCloseInternal(p_force)) {
        return false;
    }

    if (m_buffer) {
        m_buffer->syncContent(this);

        if (m_buffer->getAttachViewWindowCount() == 1) {
            // Update the buffer state.
            m_buffer->checkFileExistsOnDisk();
            m_buffer->checkFileChangedOutside();
            if (m_buffer->isModified()) {
                if (p_force) {
                    // Just discard.
                    m_buffer->discard();
                } else {
                    // Check file missing or changed outside.
                    int ret = checkFileMissingOrChangedOutside();
                    switch (ret) {
                    case Normal:
                        Q_FALLTHROUGH();
                    case SavedOrReloaded:
                        break;

                    case Discarded:
                        m_buffer->discard();
                        break;

                    default:
                        return false;
                    }

                    if (m_buffer->isModified()) {
                        // Ask to save changes.
                        ret = MessageBoxHelper::questionSaveDiscardCancel(MessageBoxHelper::Question,
                            tr("Save changes before closing note (%1)?").arg(m_buffer->getName()),
                            tr("Note path (%1).").arg(m_buffer->getPath()),
                            "",
                            this);
                        switch (ret) {
                        case QMessageBox::Save:
                        {
                            if (!save(false)) {
                                return false;
                            }
                            break;
                        }

                        case QMessageBox::Discard:
                            m_buffer->discard();
                            break;

                        case QMessageBox::Cancel:
                            Q_FALLTHROUGH();
                        default:
                            return false;
                        }
                    }
                }
            }
        }

        detachFromBuffer(true);
    }

    return true;
}

ViewWindow::Mode ViewWindow::modeFromOpenParameters(const FileOpenParameters &p_paras)
{
    switch (p_paras.m_mode) {
    case FileOpenParameters::Mode::Edit:
        return ViewWindow::Mode::Edit;

    case FileOpenParameters::Mode::Read:
        Q_FALLTHROUGH();
    default:
        return ViewWindow::Mode::Read;
    }
}

ViewWindow::Mode ViewWindow::getMode() const
{
    return m_mode;
}

bool ViewWindow::reload()
{
    if (m_buffer) {
        auto code = m_buffer->reload();
        if (code == Buffer::OperationCode::Success) {
            setModified(false);
            return true;
        }
        return false;
    }
    return true;
}

void ViewWindow::discardChangesAndRead()
{
    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    buffer->syncContent(this);

    if (buffer->isModified()) {
        // Ask to save changes.
        int ret = MessageBoxHelper::questionSaveDiscardCancel(MessageBoxHelper::Question,
                                                              tr("Discard changes to note (%1)?").arg(buffer->getName()),
                                                              tr("Note path (%1).").arg(buffer->getPath()),
                                                              "",
                                                              this);
        switch (ret) {
        case QMessageBox::Save:
            // Save and read.
            if (!save(false)) {
                return;
            }
            break;

        case QMessageBox::Discard:
            // Reload buffer and read.
            if (!reload()) {
                return;
            }
            break;

        case QMessageBox::Cancel:
            Q_FALLTHROUGH();
        default:
            return;
        }
    }
    setMode(Mode::Read);
}

bool ViewWindow::inModeCanInsert() const
{
    return m_mode == Mode::Edit || m_mode == Mode::FocusPreview || m_mode == Mode::FullPreview;
}

void ViewWindow::handleTypeAction(TypeAction p_action)
{
    Q_UNUSED(p_action);
    Q_ASSERT(false);
}

void ViewWindow::handleSectionNumberOverride(OverrideState p_state)
{
    Q_UNUSED(p_state);
    Q_ASSERT(false);
}

ViewWindow::TypeAction ViewWindow::toolBarActionToTypeAction(ViewWindowToolBarHelper::Action p_action)
{
    Q_ASSERT(p_action >= ViewWindowToolBarHelper::Action::TypeBold
             && p_action <= ViewWindowToolBarHelper::Action::TypeMax);
    return static_cast<TypeAction>(TypeAction::Bold + (p_action - ViewWindowToolBarHelper::Action::TypeBold));
}

void ViewWindow::detachFromBufferInternal()
{
}

void ViewWindow::checkBackupFileOfPreviousSession()
{
    Q_ASSERT(m_buffer);
    const auto &backupFile = m_buffer->getBackupFileOfPreviousSession();
    if (backupFile.isEmpty()) {
        return;
    }

    qDebug() << "checkBackupFileOfPreviousSession" << backupFile;

    // Ask to whether save or discard backup file.
    const auto fileModifiedTime = Utils::dateTimeString(QFileInfo(m_buffer->getContentPath()).lastModified());
    const auto backupFileModifiedTime = Utils::dateTimeString(QFileInfo(backupFile).lastModified());
    int ret = MessageBoxHelper::questionYesNoCancel(MessageBoxHelper::Warning,
        tr("Found backup file (%1) of file (%2). Do you want to recover from backup file?").arg(backupFile, m_buffer->getPath()),
        tr("'Yes' to recover from backup file, 'No' to discard it, and 'Cancel' to exit."),
        tr("It may be caused by crash while editing this file before.\n\n"
           "File last modified time: %1\n"
           "Backup file last modified time: %2").arg(fileModifiedTime, backupFileModifiedTime),
        this);
    switch (ret) {
    case QMessageBox::Yes:
        m_buffer->recoverFromBackupFileOfPreviousSession();
        break;

    case QMessageBox::No:
        // Simply delete the backup file.
        m_buffer->discardBackupFileOfPreviousSession();
        break;

    case QMessageBox::Cancel:
        Q_FALLTHROUGH();
    default:
        // Close ViewWindow.
        Q_ASSERT(m_viewSplit);
        emit m_viewSplit->viewWindowCloseRequested(this);
        break;
    }
}

DragDropAreaIndicator *ViewWindow::getAttachmentDragDropArea()
{
    if (!m_attachmentDragDropIndicator) {
        m_attachmentDragDropIndicatorInterface.reset(new AttachmentDragDropAreaIndicator(this));
        m_attachmentDragDropIndicator = new DragDropAreaIndicator(m_attachmentDragDropIndicatorInterface.data(),
                                                                  tr("Drag And Drop Files To Attach"),
                                                                  this);

        m_attachmentDragDropIndicator->hide();
        addTopWidget(m_attachmentDragDropIndicator);
    }

    return m_attachmentDragDropIndicator;
}

void ViewWindow::addToolBar(QToolBar *p_bar)
{
    Q_ASSERT(!m_toolBar && !m_attachmentDragDropIndicator);

    m_toolBar = p_bar;
    addTopWidget(p_bar);

    p_bar->setAcceptDrops(true);

    // Enable Drag&Drop on it.
    p_bar->installEventFilter(this);
}

bool ViewWindow::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_obj == m_toolBar) {
        switch (p_event->type()) {
        case QEvent::DragEnter:
            if (m_buffer && m_buffer->isAttachmentSupported() && AttachmentDragDropAreaIndicator::isAccepted(dynamic_cast<QDragEnterEvent *>(p_event))) {
                getAttachmentDragDropArea()->show();
            }
            break;

        case QEvent::HoverLeave:
            if (m_attachmentDragDropIndicator) {
                m_attachmentDragDropIndicator->hide();
            }
            break;

        default:
            break;
        }
    }
    return QFrame::eventFilter(p_obj, p_event);
}

bool ViewWindow::aboutToCloseInternal(bool p_force)
{
    Q_UNUSED(p_force);
    return true;
}

QSharedPointer<OutlineProvider> ViewWindow::getOutlineProvider()
{
    return nullptr;
}

int ViewWindow::checkFileMissingOrChangedOutside()
{
    if (!m_buffer) {
        return Normal;
    }

    if (!m_buffer->checkFileExistsOnDisk()) {
        int ret = MessageBoxHelper::questionSaveDiscardCancel(MessageBoxHelper::Warning,
            tr("File is missing on disk (%1).").arg(m_buffer->getPath()),
            tr("Do you want to force to save the buffer to the file?"),
            tr("The file may be deleted from outside. Please choose to save the buffer to a new file or just discard it."),
            this);
        switch (ret) {
        case QMessageBox::Save:
            if (!save(true)) {
                return Failed;
            } else {
                m_fileChangeCheckEnabled = true;
                return SavedOrReloaded;
            }
            break;

        case QMessageBox::Discard:
            return Discarded;

        default:
            return Failed;
        }
    } else if (m_buffer->checkFileChangedOutside()) {
        int ret = MessageBoxHelper::questionSaveDiscardCancel(MessageBoxHelper::Warning,
            tr("File is changed from outside (%1).").arg(m_buffer->getPath()),
            tr("Do you want to save the buffer to the file to override, or discard the buffer?"),
            tr("The file is changed from outside. Please choose to save the buffer to the file or "
               "just discard the buffer and reload the file."),
            this);
        switch (ret) {
        case QMessageBox::Save:
            if (!save(true)) {
                return Failed;
            } else {
                m_fileChangeCheckEnabled = true;
                return SavedOrReloaded;
            }

        case QMessageBox::Discard:
            if (!reload()) {
                // Return Discarded here to let ViewWindow be closed.
                return Discarded;
            } else {
                return SavedOrReloaded;
            }

        default:
            return Failed;
        }
    }

    m_fileChangeCheckEnabled = true;
    return Normal;
}

void ViewWindow::checkFileMissingOrChangedOutsidePeriodically()
{
    if (m_fileChangeCheckEnabled) {
        // Disable it first.
        m_fileChangeCheckEnabled = false;
        int ret = checkFileMissingOrChangedOutside();
        m_fileChangeCheckEnabled = ret != Discarded;
    }
}

bool ViewWindow::save(bool p_force)
{
    if (!p_force && !m_buffer->isModified()) {
        return true;
    }

    if (m_buffer->isReadOnly()) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("This is a read-only note (%1), on which modification is not allowed.").arg(m_buffer->getName()),
                                 tr("Please save your changes to another note."),
                                 "",
                                 this);
        return false;
    }

    if (!saveInternal(p_force)) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 tr("Failed to save note (%1).").arg(m_buffer->getName()),
                                 tr("Please check the file (%1) and try it again.").arg(m_buffer->getPath()),
                                 tr("Maybe the file is occupied by another service temporarily."),
                                 this);
        return false;
    }

    return true;
}

void ViewWindow::updateEditReadDiscardActionState(EditReadDiscardAction *p_act)
{
    switch (getMode()) {
    case Mode::Read:
        p_act->setState(BiAction::State::Default);
        break;

    default:
        p_act->setState(BiAction::State::Alternative);
        break;
    }
}

void ViewWindow::setupShortcuts()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    // FindNext.
    {
        auto shortcut = WidgetUtils::createShortcut(editorConfig.getShortcut(EditorConfig::FindNext), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        findNextOnLastFind(true);
                    });
        }
    }

    // FindPrevious.
    {
        auto shortcut = WidgetUtils::createShortcut(editorConfig.getShortcut(EditorConfig::FindPrevious), this, Qt::WidgetWithChildrenShortcut);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, [this]() {
                        findNextOnLastFind(false);
                    });
        }
    }
}

void ViewWindow::wheelEvent(QWheelEvent *p_event)
{
    if (p_event->modifiers() & Qt::ControlModifier) {
        QPoint angle = p_event->angleDelta();
        if (!angle.isNull() && (angle.y() != 0)) {
            // Zoom in/out current tab.
            zoom(angle.y() > 0);
        }

        p_event->accept();
        return;
    }

    QFrame::wheelEvent(p_event);
}

void ViewWindow::showZoomFactor(qreal p_factor)
{
    showMessage(tr("Zoomed: %1%").arg(p_factor * 100));
}

void ViewWindow::showZoomDelta(int p_delta)
{
    showMessage(tr("Zoomed: %1%2").arg(p_delta > 0 ? "+" : "").arg(p_delta));
}

void ViewWindow::showFindAndReplaceWidget()
{
    if (!m_findAndReplace) {
        m_findAndReplace = new FindAndReplaceWidget(this);
        m_mainLayout->addWidget(m_findAndReplace);

        // Connect it to slots.
        connect(m_findAndReplace, &FindAndReplaceWidget::findTextChanged,
                this, &ViewWindow::handleFindTextChanged);
        connect(m_findAndReplace, &FindAndReplaceWidget::findNextRequested,
                this, &ViewWindow::findNext);
        connect(m_findAndReplace, &FindAndReplaceWidget::replaceRequested,
                this, &ViewWindow::replace);
        connect(m_findAndReplace, &FindAndReplaceWidget::replaceAllRequested,
                this, &ViewWindow::replaceAll);
        connect(m_findAndReplace, &FindAndReplaceWidget::closed,
                this, [this]() {
                    setFocus();
                    handleFindAndReplaceWidgetClosed();
                });
        connect(m_findAndReplace, &FindAndReplaceWidget::opened,
                this, &ViewWindow::handleFindAndReplaceWidgetOpened);
    }

    m_findAndReplace->open(QString());
}

void ViewWindow::hideFindAndReplaceWidget()
{
    if (m_findAndReplace) {
        m_findAndReplace->close();
    }
}

void ViewWindow::keyPressEvent(QKeyEvent *p_event)
{
    switch (p_event->key()) {
    case Qt::Key_Escape:
        if (findAndReplaceWidgetVisible()) {
            hideFindAndReplaceWidget();
            return;
        }
        break;

    default:
        break;
    }
    QFrame::keyPressEvent(p_event);
}

bool ViewWindow::findAndReplaceWidgetVisible() const
{
    return m_findAndReplace && m_findAndReplace->isVisible();
}

void ViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options)
{
    Q_UNUSED(p_text);
    Q_UNUSED(p_options);
}

void ViewWindow::handleFindNext(const QString &p_text, FindOptions p_options)
{
    Q_UNUSED(p_text);
    Q_UNUSED(p_options);
}

void ViewWindow::handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    Q_UNUSED(p_text);
    Q_UNUSED(p_options);
    Q_UNUSED(p_replaceText);
}

void ViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    Q_UNUSED(p_text);
    Q_UNUSED(p_options);
    Q_UNUSED(p_replaceText);
}

void ViewWindow::handleFindAndReplaceWidgetClosed()
{
}

void ViewWindow::handleFindAndReplaceWidgetOpened()
{
}

void ViewWindow::findNextOnLastFind(bool p_forward)
{
    // Check if need to update the find info.
    if (m_findAndReplace && m_findAndReplace->isVisible()) {
        m_findInfo.m_text = m_findAndReplace->getFindText();
        m_findInfo.m_options = m_findAndReplace->getOptions();
    }

    if (m_findInfo.m_text.isEmpty()) {
        return;
    }

    if (p_forward) {
        handleFindNext(m_findInfo.m_text, m_findInfo.m_options & ~FindOption::FindBackward);
    } else {
        handleFindNext(m_findInfo.m_text, m_findInfo.m_options | FindOption::FindBackward);
    }
}

void ViewWindow::findNext(const QString &p_text, FindOptions p_options)
{
    m_findInfo.m_text = p_text;
    m_findInfo.m_options = p_options;
    handleFindNext(p_text, p_options);
}

void ViewWindow::replace(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    m_findInfo.m_text = p_text;
    m_findInfo.m_options = p_options;
    handleReplace(p_text, p_options, p_replaceText);
}

void ViewWindow::replaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    m_findInfo.m_text = p_text;
    m_findInfo.m_options = p_options;
    handleReplaceAll(p_text, p_options, p_replaceText);
}

void ViewWindow::showFindResult(const QString &p_text, int p_totalMatches, int p_currentMatchIndex)
{
    if (p_totalMatches == 0) {
        showMessage(tr("Pattern not found: %1").arg(p_text));
    } else {
        showMessage(tr("Match found: %1/%2").arg(p_currentMatchIndex + 1).arg(p_totalMatches));
    }
}

void ViewWindow::showReplaceResult(const QString &p_text, int p_totalReplaces)
{
    if (p_totalReplaces == 0) {
        showMessage(tr("Pattern not found: %1").arg(p_text));
    } else {
        showMessage(tr("Replaced %n match(es)", "", p_totalReplaces));
    }
}

void ViewWindow::showMessage(const QString p_msg)
{
    if (m_statusWidget) {
        m_statusWidget->showMessage(p_msg);
    } else {
        VNoteX::getInst().showStatusMessageShort(p_msg);
    }
}

void ViewWindow::edit()
{
    int ret = checkFileMissingOrChangedOutside();
    if (Normal != ret && SavedOrReloaded != ret) {
        // Recover the icon of the action.
        updateEditReadDiscardActionState(m_editReadDiscardAct);
        return;
    }

    setMode(Mode::Edit);
    setFocus();
}

void ViewWindow::read(bool p_save)
{
    int ret = checkFileMissingOrChangedOutside();
    if (Normal != ret && SavedOrReloaded != ret) {
        // Recover the icon of the action.
        updateEditReadDiscardActionState(m_editReadDiscardAct);
        return;
    }

    if (p_save) {
        if (save(false)) {
            setMode(Mode::Read);
        }
    } else {
        discardChangesAndRead();
    }
    setFocus();
}

QToolBar *ViewWindow::createToolBar(QWidget *p_parent)
{
    auto toolBar = new QToolBar(p_parent);
    toolBar->setProperty(PropertyDefs::s_viewWindowToolBar, true);
    return toolBar;
}
