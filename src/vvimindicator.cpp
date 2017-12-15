#include "vvimindicator.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QStringList>
#include <QFontMetrics>
#include <QFont>
#include <QHeaderView>
#include <QInputMethod>
#include <QGuiApplication>

#include "vconfigmanager.h"
#include "vbuttonwithwidget.h"
#include "vedittab.h"
#include "vpalette.h"

extern VConfigManager *g_config;

extern VPalette *g_palette;

VVimIndicator::VVimIndicator(QWidget *p_parent)
    : QWidget(p_parent), m_vim(NULL)
{
    setupUI();
}

void VVimIndicator::setupUI()
{
    m_cmdLineEdit = new VVimCmdLineEdit(this);
    m_cmdLineEdit->setProperty("VimCommandLine", true);
    connect(m_cmdLineEdit, &VVimCmdLineEdit::commandCancelled,
            this, [this](){
                if (m_editTab) {
                    m_editTab->focusTab();
                }

                // NOTICE: m_cmdLineEdit should not hide itself before setting
                // focus to edit tab.
                m_cmdLineEdit->hide();

                if (m_vim) {
                    m_vim->processCommandLineCancelled();
                }
            });

    connect(m_cmdLineEdit, &VVimCmdLineEdit::commandFinished,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd){
                if (m_editTab) {
                    m_editTab->focusTab();
                }

                m_cmdLineEdit->hide();

                // Hide the cmd line edit before execute the command.
                // If we execute the command first, we will get Chinese input
                // method enabled after returning to read mode.
                if (m_vim) {
                    m_vim->processCommandLine(p_type, p_cmd);
                }
            });

    connect(m_cmdLineEdit, &VVimCmdLineEdit::commandChanged,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd){
                if (m_vim) {
                    m_vim->processCommandLineChanged(p_type, p_cmd);
                }
            });

    connect(m_cmdLineEdit, &VVimCmdLineEdit::requestNextCommand,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd){
                if (m_vim) {
                    QString cmd = m_vim->getNextCommandHistory(p_type, p_cmd);
                    if (!cmd.isNull()) {
                        m_cmdLineEdit->setCommand(cmd);
                    } else {
                        m_cmdLineEdit->restoreUserLastInput();
                    }
                }
            });

    connect(m_cmdLineEdit, &VVimCmdLineEdit::requestPreviousCommand,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd){
                if (m_vim) {
                    QString cmd = m_vim->getPreviousCommandHistory(p_type, p_cmd);
                    if (!cmd.isNull()) {
                        m_cmdLineEdit->setCommand(cmd);
                    }
                }
            });

    connect(m_cmdLineEdit, &VVimCmdLineEdit::requestRegister,
            this, [this](int p_key, int p_modifiers){
                if (m_vim) {
                    QString val = m_vim->readRegister(p_key, p_modifiers);
                    if (!val.isEmpty()) {
                        m_cmdLineEdit->setText(m_cmdLineEdit->text() + val);
                    }
                }
            });

    m_cmdLineEdit->hide();

    m_modeLabel = new QLabel(this);
    m_modeLabel->setProperty("VimIndicatorModeLabel", true);

    QTreeWidget *regTree = new QTreeWidget(this);
    regTree->setProperty("ItemBorder", true);
    regTree->setRootIsDecorated(false);
    regTree->setColumnCount(2);
    regTree->header()->setStretchLastSection(true);
    QStringList headers;
    headers << tr("Register") << tr("Value");
    regTree->setHeaderLabels(headers);

    m_regBtn = new VButtonWithWidget("\"",
                                     regTree,
                                     this);
    m_regBtn->setToolTip(tr("Registers"));
    m_regBtn->setProperty("StatusBtn", true);
    m_regBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_regBtn, &VButtonWithWidget::popupWidgetAboutToShow,
            this, &VVimIndicator::updateRegistersTree);

    QTreeWidget *markTree = new QTreeWidget(this);
    markTree->setProperty("ItemBorder", true);
    markTree->setRootIsDecorated(false);
    markTree->setColumnCount(4);
    markTree->header()->setStretchLastSection(true);
    headers.clear();
    headers << tr("Mark") << tr("Line") << tr("Column") << tr("Text");
    markTree->setHeaderLabels(headers);

    m_markBtn = new VButtonWithWidget("[]",
                                      markTree,
                                      this);
    m_markBtn->setToolTip(tr("Marks"));
    m_markBtn->setProperty("StatusBtn", true);
    m_markBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_markBtn, &VButtonWithWidget::popupWidgetAboutToShow,
            this, &VVimIndicator::updateMarksTree);

    m_keyLabel = new QLabel(this);
    m_keyLabel->setProperty("VimIndicatorKeyLabel", true);
    QFontMetrics metric(font());
    m_keyLabel->setMinimumWidth(metric.width('A') * 5);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addStretch();
    mainLayout->addWidget(m_cmdLineEdit);
    mainLayout->addWidget(m_modeLabel);
    mainLayout->addWidget(m_regBtn);
    mainLayout->addWidget(m_markBtn);
    mainLayout->addWidget(m_keyLabel);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

QString VVimIndicator::modeToString(VimMode p_mode) const
{
    QString str;

    switch (p_mode) {
    case VimMode::Normal:
       str = tr("Normal");
       break;

    case VimMode::Insert:
       str = tr("Insert");
       break;

    case VimMode::Visual:
       str = tr("Visual");
       break;

    case VimMode::VisualLine:
       str = tr("VisualLine");
       break;

    case VimMode::Replace:
       str = tr("Replace");
       break;

    default:
       str = tr("Unknown");
        break;
    }

    return str;
}

static QString modeBackgroundColor(VimMode p_mode)
{
    QString color;

    switch (p_mode) {
    case VimMode::Normal:
       color = g_config->getEditorVimNormalBg();
       break;

    case VimMode::Insert:
       color = g_config->getEditorVimInsertBg();
       break;

    case VimMode::Visual:
       color = g_config->getEditorVimVisualBg();
       break;

    case VimMode::VisualLine:
       color = g_config->getEditorVimVisualBg();
       break;

    case VimMode::Replace:
       color = g_config->getEditorVimReplaceBg();
       break;

    default:
       color = "red";
       break;
    }

    return color;
}

static void fillTreeItemsWithRegisters(QTreeWidget *p_tree,
                                       const QMap<QChar, VVim::Register> &p_regs)
{
    p_tree->clear();
    for (auto const &reg : p_regs) {
        if (reg.m_value.isEmpty()) {
            continue;
        }

        QStringList itemStr;
        itemStr << reg.m_name << reg.m_value;
        QTreeWidgetItem *item = new QTreeWidgetItem(p_tree, itemStr);
        item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    }

    p_tree->resizeColumnToContents(0);
    p_tree->resizeColumnToContents(1);
}

void VVimIndicator::update(const VVim *p_vim, const VEditTab *p_editTab)
{
    m_editTab = const_cast<VEditTab *>(p_editTab);
    if (m_vim != p_vim) {
        // Disconnect from previous Vim.
        if (m_vim) {
            disconnect(m_vim.data(), 0, this, 0);
        }

        m_vim = const_cast<VVim *>(p_vim);
        if (m_vim) {
            // Connect signal.
            connect(m_vim.data(), &VVim::commandLineTriggered,
                    this, &VVimIndicator::triggerCommandLine);

            m_cmdLineEdit->hide();
        }
    }

    if (!m_vim) {
        m_cmdLineEdit->hide();
        return;
    }

    VimMode mode = VimMode::Normal;
    QChar curRegName(' ');
    QChar lastUsedMark;
    QString pendingKeys;
    if (p_vim) {
        mode = p_vim->getMode();
        curRegName = p_vim->getCurrentRegisterName();
        lastUsedMark = p_vim->getMarks().getLastUsedMark();
        pendingKeys = p_vim->getPendingKeys();
    }

    m_modeLabel->setStyleSheet(QString("background: %1;").arg(modeBackgroundColor(mode)));
    m_modeLabel->setText(modeToString(mode));

    m_regBtn->setText(curRegName);

    QString markText = QString("[%1]")
                              .arg(lastUsedMark.isNull() ? QChar(' ') : lastUsedMark);
    m_markBtn->setText(markText);

    m_keyLabel->setText(pendingKeys);
}

void VVimIndicator::updateRegistersTree(QWidget *p_widget)
{
    QTreeWidget *regTree = dynamic_cast<QTreeWidget *>(p_widget);
    if (!m_vim) {
        regTree->clear();
        return;
    }

    const QMap<QChar, VVim::Register> &regs = m_vim->getRegisters();
    fillTreeItemsWithRegisters(regTree, regs);
}

static void fillTreeItemsWithMarks(QTreeWidget *p_tree,
                                   const QMap<QChar, VVim::Mark> &p_marks)
{
    p_tree->clear();
    for (auto const &mark : p_marks) {
        if (!mark.m_location.isValid()) {
            continue;
        }

        QStringList itemStr;
        itemStr << mark.m_name << QString::number(mark.m_location.m_blockNumber + 1)
                << QString::number(mark.m_location.m_positionInBlock) << mark.m_text;
        QTreeWidgetItem *item = new QTreeWidgetItem(p_tree, itemStr);
        item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    }

    p_tree->resizeColumnToContents(0);
    p_tree->resizeColumnToContents(1);
    p_tree->resizeColumnToContents(2);
    p_tree->resizeColumnToContents(3);
}

void VVimIndicator::updateMarksTree(QWidget *p_widget)
{
    QTreeWidget *markTree = dynamic_cast<QTreeWidget *>(p_widget);
    if (!m_vim) {
        markTree->clear();
        return;
    }

    const QMap<QChar, VVim::Mark> &marks = m_vim->getMarks().getMarks();
    fillTreeItemsWithMarks(markTree, marks);
}

void VVimIndicator::triggerCommandLine(VVim::CommandLineType p_type)
{
    m_cmdLineEdit->reset(p_type);
}

VVimCmdLineEdit::VVimCmdLineEdit(QWidget *p_parent)
    : QLineEdit(p_parent), m_type(VVim::CommandLineType::Invalid),
      m_registerPending(false), m_enableInputMethod(true)
{
    // When user delete all the text, cancel command input.
    connect(this, &VVimCmdLineEdit::textChanged,
            this, [this](const QString &p_text){
                if (p_text.isEmpty()) {
                    emit commandCancelled();
                } else {
                    emit commandChanged(m_type, p_text.right(p_text.size() - 1));
                }
            });

    connect(this, &VVimCmdLineEdit::textEdited,
            this, [this](const QString &p_text){
                if (p_text.size() < 2) {
                    m_userLastInput.clear();
                } else {
                    m_userLastInput = p_text.right(p_text.size() - 1);
                }
            });

    m_originStyleSheet = styleSheet();
}

QString VVimCmdLineEdit::getCommand() const
{
    QString tx = text();
    if (tx.size() < 2) {
        return "";
    } else {
        return tx.right(tx.size() - 1);
    }
}

QString VVimCmdLineEdit::commandLineTypeLeader(VVim::CommandLineType p_type)
{
    QString leader;
    switch (p_type) {
    case VVim::CommandLineType::Command:
        leader = ":";
        break;

    case VVim::CommandLineType::SearchForward:
        leader = "/";
        break;

    case VVim::CommandLineType::SearchBackward:
        leader = "?";
        break;

    case VVim::CommandLineType::Invalid:
        leader.clear();
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    return leader;
}

void VVimCmdLineEdit::reset(VVim::CommandLineType p_type)
{
    m_type = p_type;
    m_userLastInput.clear();
    setCommand("");
    show();
    setFocus();
    setInputMethodEnabled(p_type != VVim::CommandLineType::Command);
}

void VVimCmdLineEdit::setInputMethodEnabled(bool p_enabled)
{
    if (m_enableInputMethod != p_enabled) {
        m_enableInputMethod = p_enabled;

        QInputMethod *im = QGuiApplication::inputMethod();
        im->reset();
        // Ask input method to query current state, which will call inputMethodQuery().
        im->update(Qt::ImEnabled);
    }
}

QVariant VVimCmdLineEdit::inputMethodQuery(Qt::InputMethodQuery p_query) const
{
    if (p_query == Qt::ImEnabled) {
        return m_enableInputMethod;
    }

    return QLineEdit::inputMethodQuery(p_query);
}

// See if @p_modifiers is Control which is different on macOs and Windows.
static bool isControlModifier(int p_modifiers)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return p_modifiers == Qt::MetaModifier;
#else
    return p_modifiers == Qt::ControlModifier;
#endif
}

void VVimCmdLineEdit::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    int modifiers = p_event->modifiers();

    if (m_registerPending) {
        // Ctrl and Shift may be sent out first.
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Meta) {
            goto exit;
        }

        // Expecting a register name.
        emit requestRegister(key, modifiers);

        p_event->accept();
        setRegisterPending(false);
        return;
    }

    if ((key == Qt::Key_Return && modifiers == Qt::NoModifier)
        || (key == Qt::Key_Enter && modifiers == Qt::KeypadModifier)) {
        // Enter, complete the command line input.
        p_event->accept();
        emit commandFinished(m_type, getCommand());
        return;
    } else if (key == Qt::Key_Escape
               || (key == Qt::Key_BracketLeft && isControlModifier(modifiers))) {
        // Exit command line input.
        setText(commandLineTypeLeader(m_type));
        p_event->accept();
        emit commandCancelled();
        return;
    }

    switch (key) {
    case Qt::Key_U:
        if (isControlModifier(modifiers)) {
            // Ctrl+U, delete all user input.
            setText(commandLineTypeLeader(m_type));
            p_event->accept();
            return;
        }

        break;

    case Qt::Key_N:
        if (!isControlModifier(modifiers)) {
            break;
        }
        // Ctrl+N, request next command.
        // Fall through.
    case Qt::Key_Down:
    {
        emit requestNextCommand(m_type, getCommand());
        p_event->accept();
        return;
    }

    case Qt::Key_P:
        if (!isControlModifier(modifiers)) {
            break;
        }
        // Ctrl+P, request previous command.
        // Fall through.
    case Qt::Key_Up:
    {
        emit requestPreviousCommand(m_type, getCommand());
        p_event->accept();
        return;
    }

    case Qt::Key_R:
    {
        if (isControlModifier(modifiers)) {
            // Ctrl+R, insert the content of a register.
            setRegisterPending(true);
            p_event->accept();
            return;
        }
    }

    default:
        break;
    }

exit:
    QLineEdit::keyPressEvent(p_event);
}

void VVimCmdLineEdit::focusOutEvent(QFocusEvent *p_event)
{
    if (p_event->reason() != Qt::ActiveWindowFocusReason) {
        emit commandCancelled();
    }
}

void VVimCmdLineEdit::setCommand(const QString &p_cmd)
{
    setText(commandLineTypeLeader(m_type) + p_cmd);
}

void VVimCmdLineEdit::restoreUserLastInput()
{
    setCommand(m_userLastInput);
}

void VVimCmdLineEdit::setRegisterPending(bool p_pending)
{
    if (p_pending && !m_registerPending) {
        // Going to pending.
        setStyleSheet(QString("background: %1;").arg(g_palette->color("vim_indicator_cmd_edit_pending_bg")));
    } else if (!p_pending && m_registerPending) {
        // Leaving pending.
        setStyleSheet(m_originStyleSheet);
    }

    m_registerPending = p_pending;
}
