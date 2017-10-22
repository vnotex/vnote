#ifndef VTABLEOFCONTENT_H
#define VTABLEOFCONTENT_H

#include <QString>
#include <QVector>

class VFile;


struct VHeaderPointer
{
    VHeaderPointer()
        : m_file(NULL), m_index(-1)
    {
    }

    VHeaderPointer(const VFile *p_file, int p_index)
        : m_file(p_file), m_index(p_index)
    {
    }

    bool operator==(const VHeaderPointer &p_header) const
    {
        return m_file == p_header.m_file
               && m_index == p_header.m_index;
    }

    void reset()
    {
        m_index = -1;
    }

    void clear()
    {
        m_file = NULL;
        reset();
    }

    void update(const VFile *p_file, int p_index)
    {
        m_file = p_file;
        m_index = p_index;
    }

    bool isValid() const
    {
        return m_index > -1;
    }

    QString toString() const
    {
        return QString("VHeaderPointer file: %1 index: %2")
                      .arg((long long)m_file)
                      .arg(m_index);
    }

    // The corresponding file.
    const VFile *m_file;

    // Index of the header item in VTableOfContent which this instance points to.
    int m_index;
};


struct VTableOfContentItem
{
    VTableOfContentItem()
        : m_level(1), m_blockNumber(-1), m_index(-1)
    {
    }

    VTableOfContentItem(const QString &p_name,
                        int p_level,
                        const QString &p_anchor,
                        int p_index)
        : m_name(p_name),
          m_level(p_level),
          m_anchor(p_anchor),
          m_blockNumber(-1),
          m_index(p_index)
    {
    }

    VTableOfContentItem(const QString &p_name,
                        int p_level,
                        int p_blockNumber,
                        int p_index)
        : m_name(p_name),
          m_level(p_level),
          m_blockNumber(p_blockNumber),
          m_index(p_index)
    {
    }

    // Whether it is an empty item.
    // An empty item points to nothing.
    bool isEmpty() const
    {
        if (m_anchor.isEmpty()) {
            return m_blockNumber == -1;
        }

        return false;
    }

    bool isMatched(const VHeaderPointer &p_header) const
    {
        return m_index == p_header.m_index;
    }

    bool operator==(const VTableOfContentItem &p_item) const
    {
        return m_name == p_item.m_name
               && m_level == p_item.m_level
               && m_anchor == p_item.m_anchor
               && m_blockNumber == p_item.m_blockNumber
               && m_index == p_item.m_index;
    }

    // Name of the item to display.
    QString m_name;

    // Level of this item, based on 1.
    int m_level;

    // Use an anchor to identify the position of this item.
    QString m_anchor;

    // Use block number to identify the position of this item.
    // -1 to indicate invalid.
    int m_blockNumber;

    // Index in VTableOfContent, based on 0.
    // -1 for invalid value.
    int m_index;
};


enum class VTableOfContentType
{
    Anchor = 0,
    BlockNumber
};


class VTableOfContent
{
public:
    VTableOfContent();

    VTableOfContent(const VFile *p_file);

    void update(const VFile *p_file,
                const QVector<VTableOfContentItem> &p_table,
                VTableOfContentType p_type);

    // Parse m_table from html.
    bool parseTableFromHtml(const QString &p_html);

    const VFile *getFile() const;

    void setFile(const VFile *p_file);

    VTableOfContentType getType() const;

    void setType(VTableOfContentType p_type);

    void clearTable();

    const QVector<VTableOfContentItem> &getTable() const;

    void setTable(const QVector<VTableOfContentItem> &p_table);

    void clear();

    // Return the index in @m_table of @p_anchor.
    int indexOfItemByAnchor(const QString &p_anchor) const;

    // Return the last index in @m_table which has smaller block number than @p_blockNumber.
    int indexOfItemByBlockNumber(int p_blockNumber) const;

    const VTableOfContentItem *getItem(int p_idx) const;

    const VTableOfContentItem *getItem(const VHeaderPointer &p_header) const;

    bool isEmpty() const;

    // Whether @p_header is pointing to this outline.
    bool isMatched(const VHeaderPointer &p_header) const;

    bool operator==(const VTableOfContent &p_outline) const;

    QString toString() const;

private:
    // Corresponding file.
    const VFile *m_file;

    // Table of content.
    QVector<VTableOfContentItem> m_table;

    // Type of the table of content: by anchor or by block number.
    VTableOfContentType m_type;
};

inline VTableOfContentType VTableOfContent::getType() const
{
    return m_type;
}

inline void VTableOfContent::setType(VTableOfContentType p_type)
{
    m_type = p_type;
}

inline void VTableOfContent::clearTable()
{
    m_table.clear();
}

inline const QVector<VTableOfContentItem> &VTableOfContent::getTable() const
{
    return m_table;
}

inline void VTableOfContent::setTable(const QVector<VTableOfContentItem> &p_table)
{
    m_table = p_table;
}

inline void VTableOfContent::clear()
{
    m_file = NULL;
    m_table.clear();
    m_type = VTableOfContentType::Anchor;
}

inline void VTableOfContent::setFile(const VFile *p_file)
{
    m_file = p_file;
}

inline const VFile *VTableOfContent::getFile() const
{
    return m_file;
}

inline const VTableOfContentItem *VTableOfContent::getItem(int p_idx) const
{
    if (!m_file
        || p_idx < 0
        || p_idx >= m_table.size()) {
        return NULL;
    }

    return &m_table[p_idx];
}

inline const VTableOfContentItem *VTableOfContent::getItem(const VHeaderPointer &p_header) const
{
    if (p_header.m_file != m_file) {
        return NULL;
    }

    return getItem(p_header.m_index);
}

inline bool VTableOfContent::isEmpty() const
{
    return !m_file || m_table.isEmpty();
}

inline bool VTableOfContent::isMatched(const VHeaderPointer &p_header) const
{
    return m_file && m_file == p_header.m_file;
}

inline QString VTableOfContent::toString() const
{
    return QString("VTableOfContent file: %1 isAnchor: %2 tableSize: %3")
                  .arg((long long)m_file)
                  .arg(m_type == VTableOfContentType::Anchor)
                  .arg(m_table.size());
}

#endif // VTABLEOFCONTENT_H
