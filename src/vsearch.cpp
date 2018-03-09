#include "vsearch.h"

#include "utils/vutils.h"
#include "vfile.h"
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
}

QSharedPointer<VSearchResult> VSearch::search(const QVector<VFile *> &p_files)
{
    Q_ASSERT(!askedToStop());

    QSharedPointer<VSearchResult> result(new VSearchResult(this));

    if (p_files.isEmpty()) {
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

    if (!p_directory) {
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

    if (p_notebooks.isEmpty()) {
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

void VSearch::searchFirstPhase(VFile *p_file,
                               const QSharedPointer<VSearchResult> &p_result,
                               bool p_searchContent)
{
    Q_ASSERT(testTarget(VSearchConfig::Note));

    QString name = p_file->getName();
    if (!m_patternReg.isEmpty()) {
        if (!matchOneLine(name, m_patternReg)) {
            return;
        }
    }

    QString filePath = p_file->fetchPath();
    if (testObject(VSearchConfig::Name)) {
        if (matchOneLine(name, m_searchReg)) {
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
    if (!testTarget(VSearchConfig::Note)
        && !testTarget(VSearchConfig::Folder)) {
        return;
    }

    bool opened = p_directory->isOpened();
    if (!opened && !p_directory->open()) {
        p_result->logError(QString("Fail to open folder %1.").arg(p_directory->fetchRelativePath()));
        p_result->m_state = VSearchState::Fail;
        return;
    }

    if (testTarget(VSearchConfig::Folder)
        && testObject(VSearchConfig::Name)) {
        QString text = p_directory->getName();
        if (matchOneLine(text, m_searchReg)) {
            VSearchResultItem *item = new VSearchResultItem(VSearchResultItem::Folder,
                                                            VSearchResultItem::LineNumber,
                                                            text,
                                                            p_directory->fetchPath());
            QSharedPointer<VSearchResultItem> pitem(item);
            emit resultItemAdded(pitem);
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
        if (matchOneLine(text, m_searchReg)) {
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

        if (!matchOneLine(it.m_name, m_searchReg)) {
            continue;
        }

        if (!item) {
            item = new VSearchResultItem(VSearchResultItem::Note,
                                         VSearchResultItem::OutlineIndex,
                                         p_file->getName(),
                                         p_file->fetchPath());
        }

        VSearchResultSubItem sitem(it.m_index, it.m_name);
        item->m_matches.append(sitem);
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
    Qt::CaseSensitivity cs = testOption(VSearchConfig::CaseSensitive)
                             ? Qt::CaseSensitive : Qt::CaseInsensitive;
    while (pos < size) {
        int idx = content.indexOf(newLineReg, pos);
        if (idx == -1) {
            idx = size;
        }

        if (idx > pos) {
            QString lineText = content.mid(pos, idx - pos);
            bool matched = false;
            if (m_contentSearchReg.isEmpty()) {
                matched = lineText.contains(m_config->m_keyword, cs);
            } else {
                matched = (m_contentSearchReg.indexIn(lineText) != -1);
            }

            if (matched) {
                if (!item) {
                    item = new VSearchResultItem(VSearchResultItem::Note,
                                                 VSearchResultItem::LineNumber,
                                                 p_file->getName(),
                                                 p_file->fetchPath());
                }

                VSearchResultSubItem sitem(lineNum, lineText);
                item->m_matches.append(sitem);
            }
        }

        if (idx == size) {
            break;
        }

        pos = idx + newLineReg.matchedLength();
        ++lineNum;
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
        connect(m_engine, &ISearchEngine::resultItemAdded,
                this, &VSearch::resultItemAdded);
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
