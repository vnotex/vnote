#include "vfile.h"

#include <QDir>
#include <QDebug>
#include <QTextEdit>
#include "utils/vutils.h"

VFile::VFile(const QString &p_name, QObject *p_parent)
    : QObject(p_parent), m_name(p_name), m_opened(false), m_modified(false),
      m_docType(VUtils::isMarkdown(p_name) ? DocType::Markdown : DocType::Html)
{
}

bool VFile::open()
{
    Q_ASSERT(!m_name.isEmpty());
    if (m_opened) {
        return true;
    }
    Q_ASSERT(m_content.isEmpty());
    Q_ASSERT(m_docType == (VUtils::isMarkdown(m_name) ? DocType::Markdown : DocType::Html));
    QString path = retrivePath();
    qDebug() << "path" << path;
    m_content = VUtils::readFileFromDisk(path);
    m_modified = false;
    m_opened = true;
    qDebug() << "file" << m_name << "opened";
    return true;
}

void VFile::close()
{
    if (!m_opened) {
        return;
    }
    m_content.clear();
    m_opened = false;
}

void VFile::deleteDiskFile()
{
    Q_ASSERT(parent());

    // Delete local images in ./images if it is Markdown
    if (m_docType == DocType::Markdown) {
        deleteLocalImages();
    }

    // Delete the file
    QString filePath = retrivePath();
    QFile file(filePath);
    if (file.remove()) {
        qDebug() << "deleted" << filePath;
    } else {
        qWarning() << "failed to delete" << filePath;
    }
}

bool VFile::save()
{
    Q_ASSERT(m_opened);
    bool ret = VUtils::writeFileToDisk(retrivePath(), m_content);
    return ret;
}

void VFile::convert(DocType p_curType, DocType p_targetType)
{
    Q_ASSERT(!m_opened);
    m_docType = p_targetType;
    if (p_curType == p_targetType) {
        return;
    }
    QString path = retrivePath();
    QString fileText = VUtils::readFileFromDisk(path);
    QTextEdit editor;
    if (p_curType == DocType::Markdown) {
        editor.setPlainText(fileText);
        fileText = editor.toHtml();
    } else {
        editor.setHtml(fileText);
        fileText = editor.toPlainText();
    }
    VUtils::writeFileToDisk(path, fileText);
    qDebug() << getName() << "converted" << (int)p_curType << (int)p_targetType;
}

void VFile::setModified(bool p_modified)
{
    m_modified = p_modified;
}

void VFile::deleteLocalImages()
{
    Q_ASSERT(m_docType == DocType::Markdown);
    QString filePath = retrivePath();
    QVector<QString> images = VUtils::imagesFromMarkdownFile(filePath);
    int deleted = 0;
    for (int i = 0; i < images.size(); ++i) {
        QFile file(images[i]);
        if (file.remove()) {
            ++deleted;
        }
    }
    qDebug() << "delete" << deleted << "images for" << filePath;
}

void VFile::setName(const QString &p_name)
{
    m_name = p_name;
    DocType newType = VUtils::isMarkdown(p_name) ? DocType::Markdown : DocType::Html;
    if (newType != m_docType) {
        qWarning() << "setName() change the DocType. A convertion should be followed";
    }
}
