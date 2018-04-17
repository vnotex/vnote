#include "vtextblockdata.h"

#include <QDebug>

VTextBlockData::VTextBlockData()
    : QTextBlockUserData(),
      m_codeBlockIndentation(-1)
{
}

VTextBlockData::~VTextBlockData()
{
    for (auto it : m_previews) {
        delete it;
    }

    m_previews.clear();
}

bool VTextBlockData::insertPreviewInfo(VPreviewInfo *p_info)
{
    bool tsUpdated = false;
    bool inserted = false;
    for (auto it = m_previews.begin(); it != m_previews.end();) {
        VPreviewInfo *ele = *it;

        if (p_info->m_imageInfo < ele->m_imageInfo) {
            // Insert p_info here.
            m_previews.insert(it, p_info);
            inserted = true;
            break;
        } else if (p_info->m_imageInfo == ele->m_imageInfo) {
            // Update the timestamp.
            delete ele;
            *it = p_info;
            inserted = true;
            tsUpdated = true;
            break;
        } else if (p_info->m_imageInfo.intersect(ele->m_imageInfo)) {
            // The new one intersect with an old one.
            // Remove the old one.
            Q_ASSERT(ele->m_timeStamp < p_info->m_timeStamp);
            delete ele;
            it = m_previews.erase(it);
        } else {
            ++it;
        }
    }

    if (!inserted) {
        // Append it.
        m_previews.append(p_info);
    }

    Q_ASSERT(checkOrder());

    return tsUpdated;
}

QString VTextBlockData::toString() const
{
    QString ret;
    for (int i = 0; i < m_previews.size(); ++i) {
        ret += QString("preview %1: source %2 ts %3 image %4\n")
                      .arg(i)
                      .arg((int)m_previews[i]->m_source)
                      .arg(m_previews[i]->m_timeStamp)
                      .arg(m_previews[i]->m_imageInfo.toString());
    }

    return ret;
}

bool VTextBlockData::checkOrder() const
{
    for (int i = 1; i < m_previews.size(); ++i) {
        if (!(m_previews[i - 1]->m_imageInfo < m_previews[i]->m_imageInfo)) {
            return false;
        }
    }

    return true;
}

bool VTextBlockData::clearObsoletePreview(long long p_timeStamp, PreviewSource p_source)
{
    bool deleted = false;
    for (auto it = m_previews.begin(); it != m_previews.end();) {
        VPreviewInfo *ele = *it;
        if (ele->m_source == p_source
            && ele->m_timeStamp != p_timeStamp) {
            // Remove it.
            qDebug() << "clear obsolete preview" << ele->m_imageInfo.toString();
            delete ele;
            it = m_previews.erase(it);
            deleted = true;
        } else {
            ++it;
        }
    }

    return deleted;
}
