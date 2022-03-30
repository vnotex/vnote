#include "vnotex.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <widgets/mainwindow.h>
#include "notebookmgr.h"
#include "buffermgr.h"
#include "configmgr.h"
#include "coreconfig.h"
#include "location.h"

#include "fileopenparameters.h"
#include "quickaccesshelper.h"

#include <utils/docsutils.h>
#include <task/taskmgr.h>


using namespace vnotex;

VNoteX::VNoteX(QObject *p_parent)
    : QObject(p_parent)
{
    m_instanceId = QRandomGenerator::global()->generate64();

    initThemeMgr();

    initTaskMgr();

    initNotebookMgr();

    initBufferMgr();

    initDocsUtils();

    initQuickAccess();
}

void VNoteX::initLoad()
{
    qDebug() << "start init which may take a while";
    m_notebookMgr->loadNotebooks();
    m_taskMgr->init();
}

void VNoteX::initThemeMgr()
{
    Q_ASSERT(!m_themeMgr);
    auto &configMgr = ConfigMgr::getInst();
    ThemeMgr::addSearchPath(configMgr.getAppThemeFolder());
    ThemeMgr::addSearchPath(configMgr.getUserThemeFolder());
    ThemeMgr::addSyntaxHighlightingSearchPaths(
        QStringList() << configMgr.getUserSyntaxHighlightingFolder()
                      << configMgr.getAppSyntaxHighlightingFolder());
    ThemeMgr::addWebStylesSearchPath(configMgr.getAppWebStylesFolder());
    ThemeMgr::addWebStylesSearchPath(configMgr.getUserWebStylesFolder());
    m_themeMgr = new ThemeMgr(configMgr.getCoreConfig().getTheme(), this);
}

void VNoteX::initTaskMgr()
{
    Q_ASSERT(!m_taskMgr);
    m_taskMgr = new TaskMgr(this);
    connect(m_taskMgr, &TaskMgr::taskOutputRequested,
            this, &VNoteX::showOutputRequested);
}

ThemeMgr &VNoteX::getThemeMgr() const
{
    return *m_themeMgr;
}

TaskMgr &VNoteX::getTaskMgr() const
{
    return *m_taskMgr;
}

void VNoteX::setMainWindow(MainWindow *p_mainWindow)
{
    Q_ASSERT(!m_mainWindow);
    m_mainWindow = p_mainWindow;
}

MainWindow *VNoteX::getMainWindow() const
{
    Q_ASSERT(m_mainWindow);
    return m_mainWindow;
}

void VNoteX::initNotebookMgr()
{
    Q_ASSERT(!m_notebookMgr);
    m_notebookMgr = new NotebookMgr(this);
    m_notebookMgr->init();
}

void VNoteX::initBufferMgr()
{
    BufferMgr::updateSuffixToFileType(ConfigMgr::getInst().getCoreConfig().getFileTypeSuffixes());

    Q_ASSERT(!m_bufferMgr);
    m_bufferMgr = new BufferMgr(this);
    m_bufferMgr->init();

    connect(this, &VNoteX::openNodeRequested,
            m_bufferMgr, QOverload<Node *, const QSharedPointer<FileOpenParameters> &>::of(&BufferMgr::open));

    connect(this, &VNoteX::openFileRequested,
            m_bufferMgr, QOverload<const QString &, const QSharedPointer<FileOpenParameters> &>::of(&BufferMgr::open));
}

NotebookMgr &VNoteX::getNotebookMgr() const
{
    return *m_notebookMgr;
}

BufferMgr &VNoteX::getBufferMgr() const
{
    return *m_bufferMgr;
}

void VNoteX::showStatusMessage(const QString &p_message, int p_timeoutMilliseconds)
{
    emit statusMessageRequested(p_message, p_timeoutMilliseconds);
}

void VNoteX::showStatusMessageShort(const QString &p_message)
{
    showStatusMessage(p_message, 3000);
}

void VNoteX::showTips(const QString &p_message, int p_timeoutMilliseconds)
{
    emit tipsRequested(p_message, p_timeoutMilliseconds);
}

ID VNoteX::getInstanceId() const
{
    return m_instanceId;
}

void VNoteX::initDocsUtils()
{
    auto &configMgr = ConfigMgr::getInst();
    // If we got a match in user folder, stop the search.
    DocsUtils::addSearchPath(configMgr.getUserDocsFolder());
    DocsUtils::addSearchPath(configMgr.getAppDocsFolder());

    DocsUtils::setLocale(configMgr.getCoreConfig().getLocaleToUse());
}

void VNoteX::initQuickAccess()
{
    connect(this, &VNoteX::pinToQuickAccessRequested,
            this, &QuickAccessHelper::pinToQuickAccess);
}
