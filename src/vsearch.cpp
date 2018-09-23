#include "vsearch.h"

#include "utils/vutils.h"
#include "vnotefile.h"
#include "vdirectory.h"
#include "vnotebook.h"
#include "veditarea.h"
#include "vmainwindow.h"
#include "vtableofcontent.h"
#include "vsearchengine.h"

extern VMainWindow *g_mainWin;

VSearch::VSearch(QObject *p_parent)
    : QObject(p_parent),
      m_askedToStop(false),
      m_engine(NULL)
{
    m_slashReg = QRegExp("[\\/]");
}

QSharedPointer<VSearchResult> VSearch::search(const QVector<VFile *> &p_files)
{
    Q_ASSERT(!askedToStop());

    QSharedPointer<VSearchResult> result(new VSearchResult(this));

    if (p_files.isEmpty() || m_config->isEmpty()) {
        result->m_state = VSearchState::Success;
        return result;
    }

    if (!testTarget(VSearchConfig::Note)) {
        qDebug() << "search is not applicable for note";
        result->m_state = VSearchState::Success;
        return result;
    }

    result->m_state = VSearchState::Busy;

    for (auto const & it : p_files) {
        if (!it) {
            continue;
        }

        searchFirstPhase(it, result, true);

        if (askedToStop()) {
            qDebug() << "asked to cancel the search";
            result->m_state = VSearchState::Cancelled;
            break;
        }
    }

    if (result->m_state == VSearchState::Busy) {
        result->m_state = VSearchState::Success;
    }

    return result;
}

QSharedPointer<VSearchResult> VSearch::search(VDirectory *p_directory)
{
    Q_ASSERT(!askedToStop());

    QSharedPointer<VSearchResult> result(new VSearchResult(this));

    if (!p_directory || m_config->isEmpty()) {
        result->m_state = VSearchState::Success;
        return result;
    }

    if ((!testTarget(VSearchConfig::Note)
         && !testTarget(VSearchConfig::Folder))
        || testObject(VSearchConfig::Outline)) {
        qDebug() << "search is not applicable for folder";
        result->m_state = VSearchState::Success;
        return result;
    }

    result->m_state = VSearchState::Busy;

    searchFirstPhase(p_directory, result);

    if (result->hasSecondPhaseItems()) {
        searchSecondPhase(result);
    } else if (result->m_state == VSearchState::Busy) {
        result->m_state = VSearchState::Success;
    }

    return result;
}

QSharedPointer<VSearchResult> VSearch::search(const QVector<VNotebook *> &p_notebooks)
{
    Q_ASSERT(!askedToStop());

    QSharedPointer<VSearchResult> result(new VSearchResult(this));

    if (p_notebooks.isEmpty() || m_config->isEmpty()) {
        result->m_state = VSearchState::Success;
        return result;
    }

    if (testObject(VSearchConfig::Outline)) {
        qDebug() << "search is not applicable for notebook";
        result->m_state = VSearchState::Success;
        return result;
    }

    result->m_state = VSearchState::Busy;

    for (auto const & nb : p_notebooks) {
        if (!nb) {
            continue;
        }

        searchFirstPhase(nb, result);

        if (askedToStop()) {
            qDebug() << "asked to cancel the search";
            result->m_state = VSearchState::Cancelled;
            break;
        }
    }

    if (result->hasSecondPhaseItems()) {
        searchSecondPhase(result);
    } else if (result->m_state == VSearchState::Busy) {
        result->m_state = VSearchState::Success;
    }

    return result;
}

QSharedPointer<VSearchResult> VSearch::search(const QString &p_directoryPath)
{
    Q_ASSERT(!askedToStop());

    QSharedPointer<VSearchResult> result(new VSearchResult(this));

    if (p_directoryPath.isEmpty() || m_config->isEmpty()) {
        result->m_state = VSearchState::Success;
        return result;
    }

    if ((!testTarget(VSearchConfig::Note)
         && !testTarget(VSearchConfig::Folder))
        || testObject(VSearchConfig::Outline)
        || testObject(VSearchConfig::Tag)) {
        qDebug() << "search is not applicable for directory";
        result->m_state = VSearchState::Success;
        return result;
    }

    result->m_state = VSearchState::Busy;

    searchFirstPhase(p_directoryPath, p_directoryPath, result);

    if (result->hasSecondPhaseItems()) {
        searchSecondPhase(result);
    } else if (result->m_state == VSearchState::Busy) {
        result->m_state = VSearchState::Success;
    }

    return result;
}

void VSearch::searchFirstPhase(VFile *p_file,
                               const QSharedPointer<VSearchResult> &p_result,
                               bool p_searchContent)
{
    Q_ASSERT(testTarget(VSearchConfig::Note));

    QString name = p_file->getName();
    if (!matchPattern(name)) {
        return;
    }

    QString filePath = p_file->fetchPath();
    if (testObject(VSearchConfig::Name)) {
        if (matchNonContent(name)) {
            VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Note,
                                                            VSearchResultItem::LineNumber,
                                                            name,
                                                            filePath);
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (testObject(VSearchConfig::Path)) {
        QString normFilePath;
        if (p_file->getType() == FileType::Note) {
            normFilePath = static_cast<VNoteFile *>(p_file)->fetchRelativePath();
        } else {
            normFilePath = filePath;
        }

        removeSlashFromPath(normFilePath);
        if (matchNonContent(normFilePath)) {
            VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Note,
                                                            VSearchResultItem::LineNumber,
                                                            name,
                                                            filePath);
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (testObject(VSearchConfig::Outline)) {
        VSearchResultItem *item = searchForOutline(p_file);
        if (item) {
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (testObject(VSearchConfig::Tag)) {
        VSearchResultItem *item = searchForTag(p_file);
        if (item) {
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (testObject(VSearchConfig::Content)) {
        // Search content in first phase.
        if (p_searchContent) {
            VSearchResultItem *item = searchForContent(p_file);
            if (item) {
                QSharedPointer<VSearchResultItem> pitem(item);
                emit resultItemAdded(pitem);
            }
        } else {
            // Add an item for second phase process.
            p_result->addSecondPhaseItem(filePath);
        }
    }
}

void VSearch::searchFirstPhase(VDirectory *p_directory,
                               const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(testTarget(VSearchConfig::Note) || testTarget(VSearchConfig::Folder));

    bool opened = p_directory->isOpened();
    if (!opened && !p_directory->open()) {
        p_result->logError(QString("Fail to open folder %1.").arg(p_directory->fetchRelativePath()));
        p_result->m_state = VSearchState::Fail;
        return;
    }

    if (testTarget(VSearchConfig::Folder)) {
        QString name = p_directory->getName();
        QString dirPath = p_directory->fetchPath();
        if (testObject(VSearchConfig::Name)) {
            if (matchNonContent(name)) {
                VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Folder,
                                                                VSearchResultItem::LineNumber,
                                                                name,
                                                                dirPath);
                QSharedPointer<VSearchResultItem> pitem(item);
                emit resultItemAdded(pitem);
            }
        }

        if (testObject(VSearchConfig::Path)) {
            QString normPath(p_directory->fetchRelativePath());
            removeSlashFromPath(normPath);
            if (matchNonContent(normPath)) {
                VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Folder,
                                                                VSearchResultItem::LineNumber,
                                                                name,
                                                                dirPath);
                QSharedPointer<VSearchResultItem> pitem(item);
                emit resultItemAdded(pitem);
            }
        }
    }

    // Search files.
    if (testTarget(VSearchConfig::Note)) {
        for (auto const & file : p_directory->getFiles()) {
            if (askedToStop()) {
                qDebug() << "asked to cancel the search";
                p_result->m_state = VSearchState::Cancelled;
                goto exit;
            }

            searchFirstPhase(file, p_result);
        }
    }

    // Search subfolders.
    for (auto const & dir : p_directory->getSubDirs()) {
        if (askedToStop()) {
            qDebug() << "asked to cancel the search";
            p_result->m_state = VSearchState::Cancelled;
            goto exit;
        }

        searchFirstPhase(dir, p_result);
    }

exit:
    if (!opened) {
        p_directory->close();
    }
}

void VSearch::searchFirstPhase(VNotebook *p_notebook,
                               const QSharedPointer<VSearchResult> &p_result)
{
    bool opened = p_notebook->isOpened();
    if (!opened && !p_notebook->open()) {
        p_result->logError(QString("Fail to open notebook %1.").arg(p_notebook->getName()));
        p_result->m_state = VSearchState::Fail;
        return;
    }

    if (testTarget(VSearchConfig::Notebook)
        && testObject(VSearchConfig::Name)) {
        QString text = p_notebook->getName();
        if (matchNonContent(text)) {
            VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Notebook,
                                                            VSearchResultItem::LineNumber,
                                                            text,
                                                            p_notebook->getPath());
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (!testTarget(VSearchConfig::Note)
        && !testTarget(VSearchConfig::Folder)) {
        goto exit;
    }

    // Search for subfolders.
    for (auto const & dir : p_notebook->getRootDir()->getSubDirs()) {
        if (askedToStop()) {
            qDebug() << "asked to cancel the search";
            p_result->m_state = VSearchState::Cancelled;
            goto exit;
        }

        searchFirstPhase(dir, p_result);
    }

exit:
    if (!opened) {
        p_notebook->close();
    }
}

void VSearch::searchFirstPhase(const QString &p_basePath,
                               const QString &p_directoryPath,
                               const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(testTarget(VSearchConfig::Note) || testTarget(VSearchConfig::Folder));
    Q_ASSERT(!p_directoryPath.isEmpty());

    QDir dir(p_directoryPath);
    if (!dir.exists()) {
        p_result->logError(QString("Directory %1 does not exist.").arg(p_directoryPath));
        p_result->m_state = VSearchState::Fail;
        return;
    }

    Q_ASSERT(dir.isAbsolute());

    if (testTarget(VSearchConfig::Folder)) {
        QString name = dir.dirName();
        if (testObject(VSearchConfig::Name)) {
            if (matchNonContent(name)) {
                VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Folder,
                                                                VSearchResultItem::LineNumber,
                                                                name,
                                                                p_directoryPath);
                QSharedPointer<VSearchResultItem> pitem(item);
                emit resultItemAdded(pitem);
            }
        }

        if (testObject(VSearchConfig::Path)) {
            QString normPath(QDir(p_basePath).relativeFilePath(p_directoryPath));
            removeSlashFromPath(normPath);
            if (matchNonContent(normPath)) {
                VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Folder,
                                                                VSearchResultItem::LineNumber,
                                                                name,
                                                                p_directoryPath);
                QSharedPointer<VSearchResultItem> pitem(item);
                emit resultItemAdded(pitem);
            }
        }
    }

    if (testTarget(VSearchConfig::Note)) {
        QStringList files = dir.entryList(QDir::Files);
        for (auto const & file : files) {
            if (askedToStop()) {
                qDebug() << "asked to cancel the search";
                p_result->m_state = VSearchState::Cancelled;
                return;
            }

            searchFirstPhaseFile(p_basePath, dir.absoluteFilePath(file), p_result);
        }
    }

    // Search subfolders.
    QStringList subdirs = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    for (auto const & sub : subdirs) {
        if (askedToStop()) {
            qDebug() << "asked to cancel the search";
            p_result->m_state = VSearchState::Cancelled;
            return;
        }

        searchFirstPhase(p_basePath, dir.absoluteFilePath(sub), p_result);
    }
}

void VSearch::searchFirstPhaseFile(const QString &p_basePath,
                                   const QString &p_filePath,
                                   const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(testTarget(VSearchConfig::Note));

    QString name = VUtils::fileNameFromPath(p_filePath);
    if (!matchPattern(name)) {
        return;
    }

    if (testObject(VSearchConfig::Name)) {
        if (matchNonContent(name)) {
            VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Note,
                                                            VSearchResultItem::LineNumber,
                                                            name,
                                                            p_filePath);
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (testObject(VSearchConfig::Path)) {
        QString normFilePath(QDir(p_basePath).relativeFilePath(p_filePath));
        removeSlashFromPath(normFilePath);
        if (matchNonContent(normFilePath)) {
            VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Note,
                                                            VSearchResultItem::LineNumber,
                                                            name,
                                                            p_filePath);
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
        }
    }

    if (testObject(VSearchConfig::Content)) {
        // Add an item for second phase process.
        p_result->addSecondPhaseItem(p_filePath);
    }
}

VSearchResultItem *VSearch::searchForOutline(const VFile *p_file) const
{
    VEditTab *tab = g_mainWin->getEditArea()->getTab(p_file);
    if (!tab) {
        return NULL;
    }

    const VTableOfContent &toc = tab->getOutline();
    const QVector<VTableOfContentItem> &table = toc.getTable();
    VSearchResultItem *item = NULL;
    for (auto const & it: table) {
        if (it.isEmpty()) {
            continue;
        }

        if (!matchNonContent(it.m_name)) {
            continue;
        }

        if (!item) {
            item = new VSearchResultItem(VSearchResultItem::Note,
                                         VSearchResultItem::OutlineIndex,
                                         p_file->getName(),
                                         p_file->fetchPath(),
                                         m_config);
        }

        VSearchResultSubItem sitem(it.m_index, it.m_name);
        item->m_matches.append(sitem);
    }

    return item;
}

VSearchResultItem *VSearch::searchForTag(const VFile *p_file) const
{
    if (p_file->getType() != FileType::Note) {
        return NULL;
    }

    const VNoteFile *file = static_cast<const VNoteFile *>(p_file);
    const QStringList &tags = file->getTags();

    VSearchToken &contentToken = m_config->m_contentToken;
    bool singleToken = contentToken.tokenSize() == 1;
    if (!singleToken) {
        contentToken.startBatchMode();
    }

    VSearchResultItem *item = NULL;
    bool allMatched = false;

    for (int i = 0; i < tags.size(); ++i) {
        const QString &tag = tags[i];
        if (tag.isEmpty()) {
            continue;
        }

        bool matched = false;
        if (singleToken) {
            matched = contentToken.matched(tag);
        } else {
            matched = contentToken.matchBatchMode(tag);
        }

        if (matched) {
            if (!item) {
                item = new VSearchResultItem(VSearchResultItem::Note,
                                             VSearchResultItem::LineNumber,
                                             file->getName(),
                                             file->fetchPath());
            }

            VSearchResultSubItem sitem(i, tag);
            item->m_matches.append(sitem);
        }

        if (!singleToken && contentToken.readyToEndBatchMode(allMatched)) {
            break;
        }
    }

    if (!singleToken) {
        contentToken.readyToEndBatchMode(allMatched);
        contentToken.endBatchMode();

        if (!allMatched && item) {
            // This file does not meet all the tokens.
            delete item;
            item = NULL;
        }
    }

    return item;
}

VSearchResultItem *VSearch::searchForContent(const VFile *p_file) const
{
    Q_ASSERT(p_file->isOpened());
    const QString &content = p_file->getContent();
    if (content.isEmpty()) {
        return NULL;
    }

    VSearchResultItem *item = NULL;
    int lineNum = 1;
    int pos = 0;
    int size = content.size();
    QRegExp newLineReg = QRegExp("\\n|\\r\\n|\\r");
    VSearchToken &contentToken = m_config->m_contentToken;
    bool singleToken = contentToken.tokenSize() == 1;
    if (!singleToken) {
        contentToken.startBatchMode();
    }

    bool allMatched = false;

    while (pos < size) {
        int idx = content.indexOf(newLineReg, pos);
        if (idx == -1) {
            idx = size;
        }

        if (idx > pos) {
            QString lineText = content.mid(pos, idx - pos);
            bool matched = false;
            if (singleToken) {
                matched = contentToken.matched(lineText);
            } else {
                matched = contentToken.matchBatchMode(lineText);
            }

            if (matched) {
                if (!item) {
                    item = new VSearchResultItem(VSearchResultItem::Note,
                                                 VSearchResultItem::LineNumber,
                                                 p_file->getName(),
                                                 p_file->fetchPath(),
                                                 m_config);
                }

                VSearchResultSubItem sitem(lineNum, lineText);
                item->m_matches.append(sitem);
            }
        }

        if (idx == size) {
            break;
        }

        if (!singleToken && contentToken.readyToEndBatchMode(allMatched)) {
            break;
        }

        pos = idx + newLineReg.matchedLength();
        ++lineNum;
    }

    if (!singleToken) {
        contentToken.readyToEndBatchMode(allMatched);
        contentToken.endBatchMode();

        if (!allMatched && item) {
            // This file does not meet all the tokens.
            delete item;
            item = NULL;
        }
    }

    return item;
}

void VSearch::searchSecondPhase(const QSharedPointer<VSearchResult> &p_result)
{
    delete m_engine;
    m_engine = NULL;

    switch (m_config->m_engine) {
    case VSearchConfig::Internal:
    {
        m_engine = new VSearchEngine(this);
        m_engine->search(m_config, p_result);
        break;
    }

    default:
        p_result->m_state = VSearchState::Success;
        break;
    }

    if (m_engine) {
        connect(m_engine, &ISearchEngine::finished,
                this, &VSearch::finished);
        connect(m_engine, &ISearchEngine::resultItemsAdded,
                this, &VSearch::resultItemsAdded);
    }
}

void VSearch::clear()
{
    m_config.clear();

    if (m_engine) {
        m_engine->clear();

        delete m_engine;
        m_engine = NULL;
    }

    m_askedToStop = false;
}

void VSearch::stop()
{
    qDebug() << "VSearch asked to stop";
    m_askedToStop = true;

    if (m_engine) {
        m_engine->stop();
    }
}
