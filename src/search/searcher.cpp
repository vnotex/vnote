#include "searcher.h"

#include <QCoreApplication>
#include <QDebug>

#include <buffer/buffer.h>
#include <core/file.h>
#include <notebook/node.h>
#include <notebook/notebook.h>

#include "searchresultitem.h"
#include "filesearchengine.h"

using namespace vnotex;

Searcher::Searcher(QObject *p_parent)
    : QObject(p_parent)
{
}

void Searcher::clear()
{
    m_option.clear();

    if (m_engine) {
        m_engine->clear();
        m_engine.reset();
    }

    m_askedToStop = false;
}

void Searcher::stop()
{
    m_askedToStop = true;

    if (m_engine) {
        m_engine->stop();
    }
}

SearchState Searcher::search(const QSharedPointer<SearchOption> &p_option, const QList<Buffer *> &p_buffers)
{
    if (!(p_option->m_targets & SearchTarget::SearchFile)) {
        // Only File target is applicable.
        return SearchState::Finished;
    }

    if (!prepare(p_option)) {
        return SearchState::Failed;
    }

    emit logRequested(tr("Searching %n buffer(s)", "", p_buffers.size()));

    emit progressUpdated(0, p_buffers.size());
    for (int i = 0; i < p_buffers.size(); ++i) {
        if (!p_buffers[i]) {
            continue;
        }

        if (isAskedToStop()) {
            return SearchState::Stopped;
        }

        auto file = p_buffers[i]->getFile();
        if (!firstPhaseSearch(file.data())) {
            return SearchState::Failed;
        }

        emit progressUpdated(i + 1, p_buffers.size());
    }

    return SearchState::Finished;
}

SearchState Searcher::search(const QSharedPointer<SearchOption> &p_option, Node *p_folder)
{
    Q_ASSERT(p_folder->isContainer());
    if (!(p_option->m_targets & (SearchTarget::SearchFile | SearchTarget::SearchFolder))) {
        // Only File/Folder target is applicable.
        return SearchState::Finished;
    }

    if (!prepare(p_option)) {
        return SearchState::Failed;
    }

    emit logRequested(tr("Searching folder (%1)").arg(p_folder->getName()));

    QVector<SearchSecondPhaseItem> secondPhaseItems;
    if (!firstPhaseSearchFolder(p_folder, secondPhaseItems)) {
        return SearchState::Failed;
    }

    if (isAskedToStop()) {
        return SearchState::Stopped;
    }

    if (!secondPhaseItems.isEmpty()) {
        // Do second phase search.
        if (!secondPhaseSearch(secondPhaseItems)) {
            return SearchState::Failed;
        }

        if (isAskedToStop()) {
            return SearchState::Stopped;
        }

        return SearchState::Busy;
    }

    return SearchState::Finished;
}

SearchState Searcher::search(const QSharedPointer<SearchOption> &p_option, const QVector<Notebook *> &p_notebooks)
{
    if (!prepare(p_option)) {
        return SearchState::Failed;
    }

    QVector<SearchSecondPhaseItem> secondPhaseItems;

    emit progressUpdated(0, p_notebooks.size());
    for (int i = 0; i < p_notebooks.size(); ++i) {
        if (isAskedToStop()) {
            return SearchState::Stopped;
        }

        emit logRequested(tr("Searching notebook (%1)").arg(p_notebooks[i]->getName()));

        if (!firstPhaseSearch(p_notebooks[i], secondPhaseItems)) {
            return SearchState::Failed;
        }

        emit progressUpdated(i + 1, p_notebooks.size());
    }

    if (isAskedToStop()) {
        return SearchState::Stopped;
    }

    if (!secondPhaseItems.isEmpty()) {
        // Do second phase search.
        if (!secondPhaseSearch(secondPhaseItems)) {
            return SearchState::Failed;
        }

        if (isAskedToStop()) {
            return SearchState::Stopped;
        }

        return SearchState::Busy;
    }

    return SearchState::Finished;
}

bool Searcher::prepare(const QSharedPointer<SearchOption> &p_option)
{
    Q_ASSERT(!m_option);
    m_option = p_option;

    if (!SearchToken::compile(m_option->m_keyword, m_option->m_findOptions, m_token)) {
        emit logRequested(tr("Failed to compile tokens (%1)").arg(m_option->m_keyword));
        return false;
    }

    if (m_option->m_filePattern.isEmpty()) {
        m_filePattern = QRegularExpression();
    } else {
        m_filePattern = QRegularExpression(QRegularExpression::wildcardToRegularExpression(m_option->m_filePattern), QRegularExpression::CaseInsensitiveOption);
    }

    return true;
}

bool Searcher::isAskedToStop() const
{
    QCoreApplication::sendPostedEvents();
    return m_askedToStop;
}

static QString tryGetRelativePath(const File *p_file)
{
    const auto node = p_file->getNode();
    if (node) {
        return node->fetchPath();
    }
    return p_file->getFilePath();
}

bool Searcher::firstPhaseSearch(const File *p_file)
{
    if (!p_file) {
        return true;
    }

    Q_ASSERT(testTarget(SearchTarget::SearchFile));

    const auto name = p_file->getName();
    if (!isFilePatternMatched(name)) {
        return true;
    }

    const auto filePath = p_file->getFilePath();
    const auto relativePath = tryGetRelativePath(p_file);

    if (testObject(SearchObject::SearchName)) {
        if (isTokenMatched(name)) {
            emit resultItemAdded(SearchResultItem::createBufferItem(filePath, relativePath, -1, name));
        }
    }

    if (testObject(SearchObject::SearchPath)) {
        if (isTokenMatched(relativePath)) {
            emit resultItemAdded(SearchResultItem::createBufferItem(filePath, relativePath, -1, name));
        }
    }

    if (testObject(SearchObject::SearchOutline)) {
        emit logRequested(tr("Searching outline is not supported yet"));
    }

    if (testObject(SearchObject::SearchTag)) {
        emit logRequested(tr("Searching tag is not supported yet"));
    }

    // Make SearchContent always the last one to check.
    if (testObject(SearchObject::SearchContent)) {
        if (!searchContent(p_file)) {
            return false;
        }
    }

    return true;
}

bool Searcher::isFilePatternMatched(const QString &p_name) const
{
    if (m_option->m_filePattern.isEmpty()) {
        return true;
    }

    return m_filePattern.match(p_name).hasMatch();
}

bool Searcher::testTarget(SearchTarget p_target) const
{
    return m_option->m_targets & p_target;
}

bool Searcher::testObject(SearchObject p_object) const
{
    return m_option->m_objects & p_object;
}

bool Searcher::isTokenMatched(const QString &p_text) const
{
    return m_token.matched(p_text);
}

bool Searcher::searchContent(const File *p_file)
{
    const auto content = p_file->read();
    if (content.isEmpty()) {
        return true;
    }

    const bool shouldStartBatchMode = m_token.shouldStartBatchMode();
    if (shouldStartBatchMode) {
        m_token.startBatchMode();
    }

    const auto filePath = p_file->getFilePath();
    const auto relativePath = tryGetRelativePath(p_file);

    QSharedPointer<SearchResultItem> resultItem;

    int lineNum = 0;
    int pos = 0;
    int contentSize = content.size();
    QRegularExpression newlineRegExp("\\n|\\r\\n|\\r");
    while (pos < contentSize) {
        if (isAskedToStop()) {
            break;
        }

        QRegularExpressionMatch match;
        int idx = content.indexOf(newlineRegExp, pos, &match);
        if (idx == -1) {
            idx = contentSize;
        }

        if (idx > pos) {
            QString lineText = content.mid(pos, idx - pos);
            bool matched = false;
            if (!shouldStartBatchMode) {
                matched = m_token.matched(lineText);
            } else {
                matched = m_token.matchedInBatchMode(lineText);
            }

            if (matched) {
                if (resultItem) {
                    resultItem->addLine(lineNum, lineText);
                } else {
                    resultItem = SearchResultItem::createBufferItem(filePath, relativePath, lineNum, lineText);
                }
            }
        }

        if (idx == contentSize) {
            break;
        }

        if (shouldStartBatchMode && m_token.readyToEndBatchMode()) {
            break;
        }

        pos = idx + match.capturedLength();
        ++lineNum;
    }

    if (shouldStartBatchMode) {
        bool allMatched = m_token.readyToEndBatchMode();
        m_token.endBatchMode();

        if (!allMatched) {
            // This file does not meet all the tokens.
            resultItem.reset();
        }
    }

    if (resultItem) {
        emit resultItemAdded(resultItem);
    }

    return true;
}

bool Searcher::firstPhaseSearchFolder(Node *p_node, QVector<SearchSecondPhaseItem> &p_secondPhaseItems)
{
    if (!p_node) {
        return true;
    }

    Q_ASSERT(p_node->isContainer());
    Q_ASSERT(testTarget(SearchTarget::SearchFile) || testTarget(SearchTarget::SearchFolder));

    p_node->load();

    if (testTarget(SearchTarget::SearchFolder)) {
        const auto name = p_node->getName();
        const auto folderPath = p_node->fetchAbsolutePath();
        const auto relativePath = p_node->fetchPath();
        if (testObject(SearchObject::SearchName)) {
            if (isTokenMatched(name)) {
                emit resultItemAdded(SearchResultItem::createFolderItem(folderPath, relativePath));
            }
        }

        if (testObject(SearchObject::SearchPath)) {
            if (isTokenMatched(relativePath)) {
                emit resultItemAdded(SearchResultItem::createFolderItem(folderPath, relativePath));
            }
        }
    }

    // Search children.
    const auto &children = p_node->getChildrenRef();
    for (const auto &child : children) {
        if (isAskedToStop()) {
            return true;
        }

        if (child->hasContent() && testTarget(SearchTarget::SearchFile)) {
            if (!firstPhaseSearch(child.data(), p_secondPhaseItems)) {
                return false;
            }
        }

        if (child->isContainer()) {
            if (!firstPhaseSearchFolder(child.data(), p_secondPhaseItems)) {
                return false;
            }
        }
    }

    return true;
}

bool Searcher::firstPhaseSearch(Node *p_node, QVector<SearchSecondPhaseItem> &p_secondPhaseItems)
{
    if (!p_node) {
        return true;
    }

    Q_ASSERT(testTarget(SearchTarget::SearchFile));

    const auto name = p_node->getName();
    if (!isFilePatternMatched(name)) {
        return true;
    }

    const auto filePath = p_node->fetchAbsolutePath();
    const auto relativePath = p_node->fetchPath();

    if (testObject(SearchObject::SearchName)) {
        if (isTokenMatched(name)) {
            emit resultItemAdded(SearchResultItem::createFileItem(filePath, relativePath, -1, name));
        }
    }

    if (testObject(SearchObject::SearchPath)) {
        if (isTokenMatched(relativePath)) {
            emit resultItemAdded(SearchResultItem::createFileItem(filePath, relativePath, -1, name));
        }
    }

    if (testObject(SearchObject::SearchOutline)) {
        emit logRequested(tr("Searching outline is not supported yet"));
    }

    if (testObject(SearchObject::SearchTag)) {
        emit logRequested(tr("Searching tag is not supported yet"));
    }

    if (testObject(SearchObject::SearchContent)) {
        p_secondPhaseItems.push_back(SearchSecondPhaseItem(filePath, relativePath));
    }

    return true;
}

bool Searcher::firstPhaseSearch(Notebook *p_notebook, QVector<SearchSecondPhaseItem> &p_secondPhaseItems)
{
    if (!p_notebook) {
        return true;
    }

    if (testTarget(SearchTarget::SearchNotebook)) {
        if (testObject(SearchObject::SearchName)) {
            const auto name = p_notebook->getName();
            if (isTokenMatched(name)) {
                emit resultItemAdded(SearchResultItem::createNotebookItem(p_notebook->getRootFolderAbsolutePath(),
                                                                          name));
            }
        }
    }

    if (!testTarget(SearchTarget::SearchFile) && !testTarget(SearchTarget::SearchFolder)) {
        return true;
    }

    auto rootNode = p_notebook->getRootNode();
    Q_ASSERT(rootNode->isLoaded());
    const auto &children = rootNode->getChildrenRef();
    for (const auto &child : children) {
        if (isAskedToStop()) {
            return true;
        }

        if (child->hasContent() && testTarget(SearchTarget::SearchFile)) {
            if (!firstPhaseSearch(child.data(), p_secondPhaseItems)) {
                return false;
            }
        }

        if (child->isContainer()) {
            if (!firstPhaseSearchFolder(child.data(), p_secondPhaseItems)) {
                return false;
            }
        }
    }

    return true;
}

bool Searcher::secondPhaseSearch(const QVector<SearchSecondPhaseItem> &p_secondPhaseItems)
{
    Q_ASSERT(!p_secondPhaseItems.isEmpty());
    qDebug() << "secondPhaseSearch" << p_secondPhaseItems.size();

    createSearchEngine();

    m_engine->search(m_option, m_token, p_secondPhaseItems);
    connect(m_engine.data(), &ISearchEngine::finished,
            this, &Searcher::finished);
    connect(m_engine.data(), &ISearchEngine::logRequested,
            this, &Searcher::logRequested);
    connect(m_engine.data(), &ISearchEngine::resultItemsAdded,
            this, &Searcher::resultItemsAdded);
    return true;
}

void Searcher::createSearchEngine()
{
    Q_ASSERT(m_option->m_engine == SearchEngine::Internal);

    m_engine.reset(new FileSearchEngine());
}
