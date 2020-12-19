#include "vnotex.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <widgets/mainwindow.h>
#include "notebookmgr.h"
#include "buffermgr.h"
#include "configmgr.h"
#include "coreconfig.h"

#include "fileopenparameters.h"

#include <utils/docsutils.h>


using namespace vnotex;

VNoteX::VNoteX(QObject *p_parent)
    : QObject(p_parent),
      m_mainWindow(nullptr),
      m_notebookMgr(nullptr)
{
    m_instanceId = QRandomGenerator::global()->generate64();

    initThemeMgr();

    initNotebookMgr();

    initBufferMgr();

    initDocsUtils();
}

void VNoteX::initLoad()
{
    m_notebookMgr->loadNotebooks();
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
    m_themeMgr = new ThemeMgr(configMgr.getCoreConfig().getTheme(), this);
}

ThemeMgr &VNoteX::getThemeMgr() const
{
    return *m_themeMgr;
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

void VNoteX::showStatusMessage(const QString &p_message, int timeoutMilliseconds)
{
    emit statusMessageRequested(p_message, timeoutMilliseconds);
}

void VNoteX::showStatusMessageShort(const QString &p_message)
{
    showStatusMessage(p_message, 3000);
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
