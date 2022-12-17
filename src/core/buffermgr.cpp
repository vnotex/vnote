#include "buffermgr.h"

#include <QUrl>
#include <QDebug>

#include <notebook/node.h>
#include <buffer/filetypehelper.h>
#include <buffer/markdownbufferfactory.h>
#include <buffer/textbufferfactory.h>
#include <buffer/pdfbufferfactory.h>
#include <buffer/buffer.h>
#include <buffer/nodebufferprovider.h>
#include <buffer/filebufferprovider.h>
#include <utils/widgetutils.h>
#include <utils/processutils.h>
#include "notebookmgr.h"
#include "vnotex.h"
#include "externalfile.h"
#include "sessionconfig.h"
#include "configmgr.h"

#include "fileopenparameters.h"

using namespace vnotex;

QMap<QString, QString> BufferMgr::s_suffixToFileType;

BufferMgr::BufferMgr(QObject *p_parent)
    : QObject(p_parent)
{
}

BufferMgr::~BufferMgr()
{
    Q_ASSERT(m_buffers.isEmpty());
}

void BufferMgr::init()
{
    initBufferServer();
}

void BufferMgr::initBufferServer()
{
    m_bufferServer.reset(new NameBasedServer<IBufferFactory>);

    const auto &helper = FileTypeHelper::getInst();

    // Markdown.
    auto markdownFactory = QSharedPointer<MarkdownBufferFactory>::create();
    m_bufferServer->registerItem(helper.getFileType(FileType::Markdown).m_typeName, markdownFactory);

    // Text.
    auto textFactory = QSharedPointer<TextBufferFactory>::create();
    m_bufferServer->registerItem(helper.getFileType(FileType::Text).m_typeName, textFactory);

    // Pdf.
    auto pdfFactory = QSharedPointer<PdfBufferFactory>::create();
    m_bufferServer->registerItem(helper.getFileType(FileType::Pdf).m_typeName, pdfFactory);
}

void BufferMgr::open(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (!p_node) {
        return;
    }

    if (p_node->isContainer()) {
        return;
    }

    if (!p_node->checkExists()) {
        auto msg = QString("Failed to open node that does not exist (%1)").arg(p_node->fetchAbsolutePath());
        qWarning() << msg;
        VNoteX::getInst().showStatusMessageShort(msg);
        return;
    }

    const auto nodePath = p_node->fetchAbsolutePath();

    auto fileType = p_paras->m_fileType;
    if (fileType.isEmpty()) {
        // Check if we need to open it with external program by default according to the suffix.
        fileType = findFileTypeByFile(nodePath);
        if (openWithExternalProgram(nodePath, fileType)) {
            return;
        }
    }

    auto buffer = findBuffer(p_node);
    if (!buffer || !isSameTypeBuffer(buffer, fileType)) {
        auto nodeFile = p_node->getContentFile();
        Q_ASSERT(nodeFile);
        if (fileType.isEmpty()) {
            fileType = nodeFile->getContentType().m_typeName;
        } else if (fileType != nodeFile->getContentType().m_typeName) {
            nodeFile->setContentType(FileTypeHelper::getInst().getFileTypeByName(fileType).m_type);
        }

        auto factory = m_bufferServer->getItem(fileType);
        if (!factory) {
            // No factory to open this file type.
            qInfo() << "file will be opened by default program" << nodePath;
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(nodePath));
            return;
        }

        BufferParameters paras;
        paras.m_provider.reset(new NodeBufferProvider(p_node->sharedFromThis(), nodeFile));
        buffer = factory->createBuffer(paras, this);
        addBuffer(buffer);
    }

    Q_ASSERT(buffer);
    emit bufferRequested(buffer, p_paras);
}

void BufferMgr::open(const QString &p_filePath, const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (p_filePath.isEmpty()) {
        return;
    }

    // Check if it is requested to open with external program.
    if (openWithExternalProgram(p_filePath, p_paras->m_fileType)) {
        return;
    }

    QFileInfo finfo(p_filePath);
    if (!finfo.exists()) {
        auto msg = QString("Failed to open file that does not exist (%1)").arg(p_filePath);
        qWarning() << msg;
        VNoteX::getInst().showStatusMessageShort(msg);
        WidgetUtils::openUrlByDesktop(QUrl::fromUserInput(p_filePath));
        return;
    }

    // Check if it is an internal node or not.
    auto node = VNoteX::getInst().getNotebookMgr().loadNodeByPath(p_filePath);
    if (node) {
        if (node->hasContent()) {
            open(node.data(), p_paras);
            return;
        } else {
            // Folder node. Currently just locate to it.
            emit VNoteX::getInst().locateNodeRequested(node.data());
            return;
        }
    }

    if (finfo.isDir()) {
        WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(p_filePath));
        return;
    }

    auto fileType = p_paras->m_fileType;
    if (fileType.isEmpty()) {
        // Check if we need to open it with external program by default according to the suffix.
        fileType = findFileTypeByFile(p_filePath);
        if (openWithExternalProgram(p_filePath, fileType)) {
            return;
        }
    }

    auto buffer = findBuffer(p_filePath);
    if (!buffer || !isSameTypeBuffer(buffer, fileType)) {
        // Open it as external file.
        auto externalFile = QSharedPointer<ExternalFile>::create(p_filePath);
        if (fileType.isEmpty()) {
            fileType = externalFile->getContentType().m_typeName;
        } else if (fileType != externalFile->getContentType().m_typeName) {
            externalFile->setContentType(FileTypeHelper::getInst().getFileTypeByName(fileType).m_type);
        }

        auto factory = m_bufferServer->getItem(fileType);
        if (!factory) {
            // No factory to open this file type.
            qInfo() << "file will be opened by default program" << p_filePath;
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(p_filePath));
            return;
        }

        BufferParameters paras;
        paras.m_provider.reset(new FileBufferProvider(externalFile,
                                                      p_paras->m_nodeAttachedTo,
                                                      p_paras->m_readOnly));
        buffer = factory->createBuffer(paras, this);
        addBuffer(buffer);
    }

    Q_ASSERT(buffer);
    emit bufferRequested(buffer, p_paras);
}

Buffer *BufferMgr::findBuffer(const Node *p_node) const
{
    auto it = std::find_if(m_buffers.constBegin(),
                           m_buffers.constEnd(),
                           [p_node](const Buffer *p_buffer) {
                               return p_buffer->match(p_node);
                           });
    if (it != m_buffers.constEnd()) {
        return *it;
    }

    return nullptr;
}

Buffer *BufferMgr::findBuffer(const QString &p_filePath) const
{
    auto it = std::find_if(m_buffers.constBegin(),
                           m_buffers.constEnd(),
                           [p_filePath](const Buffer *p_buffer) {
                               return p_buffer->match(p_filePath);
                           });
    if (it != m_buffers.constEnd()) {
        return *it;
    }

    return nullptr;
}

void BufferMgr::addBuffer(Buffer *p_buffer)
{
    m_buffers.push_back(p_buffer);
    connect(p_buffer, &Buffer::attachedViewWindowEmpty,
            this, [this, p_buffer]() {
                qDebug() << "delete buffer without attached view window"
                         << p_buffer->getName();
                m_buffers.removeAll(p_buffer);
                p_buffer->close();
                p_buffer->deleteLater();
            });
}

bool BufferMgr::openWithExternalProgram(const QString &p_filePath, const QString &p_name) const
{
    if (p_name.isEmpty()) {
        return false;
    }

    if (p_name == FileTypeHelper::s_systemDefaultProgram) {
        // Open it by system default program.
        qInfo() << "file will be opened by default program" << p_filePath;
        WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(p_filePath));
        return true;
    }

    if (auto pro = ConfigMgr::getInst().getSessionConfig().findExternalProgram(p_name)) {
        const auto command = pro->fetchCommand(p_filePath);
        qDebug() << "external program" << command;
        if (!command.isEmpty()) {
            ProcessUtils::startDetached(command);
        }
        return true;
    }

    return false;
}

bool BufferMgr::isSameTypeBuffer(const Buffer *p_buffer, const QString &p_typeName) const
{
    if (p_typeName.isEmpty()) {
        return true;
    }

    auto factory = m_bufferServer->getItem(p_typeName);
    Q_ASSERT(factory);
    if (factory) {
        return factory->isBufferCreatedByFactory(p_buffer);
    }

    return true;
}

void BufferMgr::updateSuffixToFileType(const QVector<CoreConfig::FileTypeSuffix> &p_fileTypeSuffixes)
{
    s_suffixToFileType.clear();

    for (const auto &fts : p_fileTypeSuffixes) {
        for (const auto &suf : fts.m_suffixes) {
            auto it = s_suffixToFileType.find(suf);
            if (it != s_suffixToFileType.end()) {
                qWarning() << "suffix conflicts for file types" << fts.m_name << it.value();
                it.value() = fts.m_name;
            } else {
                s_suffixToFileType.insert(suf, fts.m_name);
            }
        }
    }
}

QString BufferMgr::findFileTypeByFile(const QString &p_filePath)
{
    QFileInfo fi(p_filePath);
    auto suffix = fi.suffix().toLower();
    auto it = s_suffixToFileType.find(suffix);
    if (it != s_suffixToFileType.end()) {
        return it.value();
    } else {
        return QString();
    }
}
