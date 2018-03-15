#ifndef VSEARCH_H
#define VSEARCH_H

#include <QObject>
#include <QString>
#include <QSharedPointer>
#include <QRegExp>
#include <QCoreApplication>

#include "vsearchconfig.h"

class VFile;
class VDirectory;
class VNotebook;
class ISearchEngine;


class VSearch : public QObject
{
    Q_OBJECT
public:
    explicit VSearch(QObject *p_parent = nullptr);

    void setConfig(QSharedPointer<VSearchConfig> p_config);

    // Search list of files for CurrentNote and OpenedNotes.
    QSharedPointer<VSearchResult> search(const QVector<VFile *> &p_files);

    // Search folder for CurrentFolder.
    QSharedPointer<VSearchResult> search(VDirectory *p_directory);

    // Search folder for CurrentNotebook and AllNotebooks.
    QSharedPointer<VSearchResult> search(const QVector<VNotebook *> &p_notebooks);

    // Clear resources after a search completed.
    void clear();

    void stop();

signals:
    // Emitted when a new item added as result.
    void resultItemAdded(const QSharedPointer<VSearchResultItem> &p_item);

    // Emitted when async task finished.
    void finished(const QSharedPointer<VSearchResult> &p_result);

private:
    bool askedToStop() const;

    // @p_searchContent: whether search content in first phase.
    void searchFirstPhase(VFile *p_file,
                          const QSharedPointer<VSearchResult> &p_result,
                          bool p_searchContent = false);

    void searchFirstPhase(VDirectory *p_directory,
                          const QSharedPointer<VSearchResult> &p_result);

    void searchFirstPhase(VNotebook *p_notebook,
                          const QSharedPointer<VSearchResult> &p_result);

    bool testTarget(VSearchConfig::Target p_target) const;

    bool testObject(VSearchConfig::Object p_object) const;

    bool testOption(VSearchConfig::Option p_option) const;

    bool matchNonContent(const QString &p_text) const;

    bool matchPattern(const QString &p_name) const;

    VSearchResultItem *searchForOutline(const VFile *p_file) const;

    VSearchResultItem *searchForContent(const VFile *p_file) const;

    void searchSecondPhase(const QSharedPointer<VSearchResult> &p_result);

    bool m_askedToStop;

    QSharedPointer<VSearchConfig> m_config;

    ISearchEngine *m_engine;

    // Wildcard reg to for file name pattern.
    QRegExp m_patternReg;
};

inline bool VSearch::askedToStop() const
{
    QCoreApplication::processEvents();
    return m_askedToStop;
}

inline void VSearch::setConfig(QSharedPointer<VSearchConfig> p_config)
{
    m_config = p_config;

    if (m_config->m_pattern.isEmpty()) {
        m_patternReg = QRegExp();
    } else {
        m_patternReg = QRegExp(m_config->m_pattern, Qt::CaseInsensitive, QRegExp::Wildcard);
    }
}

inline bool VSearch::testTarget(VSearchConfig::Target p_target) const
{
    return p_target & m_config->m_target;
}

inline bool VSearch::testObject(VSearchConfig::Object p_object) const
{
    return p_object & m_config->m_object;
}

inline bool VSearch::testOption(VSearchConfig::Option p_option) const
{
    return p_option & m_config->m_option;
}

inline bool VSearch::matchNonContent(const QString &p_text) const
{
    return m_config->m_token.matched(p_text);
}

inline bool VSearch::matchPattern(const QString &p_name) const
{
    if (m_patternReg.isEmpty()) {
        return true;
    }

    return p_name.contains(m_patternReg);
}
#endif // VSEARCH_H
