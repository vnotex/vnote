#include "vtableofcontent.h"
#include "vconstants.h"

#include <QXmlStreamReader>
#include <QDebug>


VTableOfContent::VTableOfContent()
    : m_file(NULL), m_type(VTableOfContentType::Anchor)
{
}

VTableOfContent::VTableOfContent(const VFile *p_file)
    : m_file(p_file), m_type(VTableOfContentType::Anchor)
{
}

void VTableOfContent::update(const VFile *p_file,
                             const QVector<VTableOfContentItem> &p_table,
                             VTableOfContentType p_type)
{
    m_file = p_file;
    m_table = p_table;
    m_type = p_type;
}

static bool parseTocUl(QXmlStreamReader &p_xml,
                       QVector<VTableOfContentItem> &p_table,
                       int p_level);

static bool parseTocLi(QXmlStreamReader &p_xml,
                       QVector<VTableOfContentItem> &p_table,
                       int p_level)
{
    Q_ASSERT(p_xml.isStartElement() && p_xml.name() == "li");

    if (p_xml.readNextStartElement()) {
        if (p_xml.name() == "a") {
            QString anchor = p_xml.attributes().value("href").toString().mid(1);
            QString name;
            // Read till </a>.
            int nrStart = 1;
            while (p_xml.readNext()) {
                if (p_xml.tokenString() == "Characters") {
                    name += p_xml.text().toString();
                } else if (p_xml.isEndElement()) {
                    --nrStart;
                    if (nrStart < 0) {
                        qWarning() << "end elements more than start elements in <a>" << anchor << p_xml.name();
                        return false;
                    }

                    if (p_xml.name() == "a") {
                        break;
                    }
                } else if (p_xml.isStartElement()) {
                    ++nrStart;
                }
            }

            if (p_xml.hasError()) {
                // Error
                qWarning() << "fail to parse an entire <a> element" << anchor << name;
                return false;
            }

            VTableOfContentItem header(name, p_level, anchor, p_table.size());
            p_table.append(header);
        } else if (p_xml.name() == "ul") {
            // Such as header 3 under header 1 directly
            VTableOfContentItem header(c_emptyHeaderName, p_level, "", p_table.size());
            p_table.append(header);
            parseTocUl(p_xml, p_table, p_level + 1);
        } else {
            qWarning() << "TOC HTML <li> should contain <a> or <ul>" << p_xml.name();
            return false;
        }
    }

    while (p_xml.readNext()) {
        if (p_xml.isEndElement()) {
            if (p_xml.name() == "li") {
                return true;
            }

            continue;
        }

        if (p_xml.name() == "ul") {
            // Nested unordered list
            if (!parseTocUl(p_xml, p_table, p_level + 1)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

static bool parseTocUl(QXmlStreamReader &p_xml,
                       QVector<VTableOfContentItem> &p_table,
                       int p_level)
{
    bool ret = true;
    Q_ASSERT(p_xml.isStartElement() && p_xml.name() == "ul");

    while (p_xml.readNextStartElement()) {
        if (p_xml.name() == "li") {
            if (!parseTocLi(p_xml, p_table, p_level)) {
                ret = false;
                break;
            }
        } else {
            qWarning() << "TOC HTML <ul> should contain <li>" << p_xml.name();
            ret = false;
            break;
        }
    }

    return ret;
}

bool VTableOfContent::parseTableFromHtml(const QString &p_html)
{
    bool ret = true;
    m_table.clear();

    if (!p_html.isEmpty()) {
        QXmlStreamReader xml(p_html);
        if (xml.readNextStartElement()) {
            if (xml.name() == "ul") {
                ret = parseTocUl(xml, m_table, 1);
            } else {
                qWarning() << "TOC HTML does not start with <ul>" << p_html;
                ret = false;
            }
        }

        if (xml.hasError()) {
            qWarning() << "fail to parse TOC in HTML" << p_html;
            ret = false;
        }
    }

    return ret;
}

int VTableOfContent::indexOfItemByAnchor(const QString &p_anchor) const
{
    if (p_anchor.isEmpty()
        || isEmpty()
        || m_type != VTableOfContentType::Anchor) {
        return -1;
    }

    for (int i = 0; i < m_table.size(); ++i) {
        if (m_table[i].m_anchor == p_anchor) {
            return i;
        }
    }

    return -1;
}

int VTableOfContent::indexOfItemByBlockNumber(int p_blockNumber) const
{
    if (p_blockNumber == -1
        || isEmpty()
        || m_type != VTableOfContentType::BlockNumber) {
        return -1;
    }

    for (int i = m_table.size() - 1; i >= 0; --i) {
        if (!m_table[i].isEmpty()
            && m_table[i].m_blockNumber <= p_blockNumber) {
            return i;
        }
    }

    return -1;
}

bool VTableOfContent::operator==(const VTableOfContent &p_outline) const
{
    return m_file == p_outline.getFile()
           && m_type == p_outline.getType()
           && m_table == p_outline.getTable();
}
