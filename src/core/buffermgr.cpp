#include "buffermgr.h"

#include <QUrl>
#include <QDebug>

#include <notebook/node.h>
#include <buffer/filetypehelper.h>
#include <buffer/markdownbufferfactory.h>
#include <buffer/textbufferfactory.h>
#include <buffer/buffer.h>
#include <buffer/nodebufferprovider.h>
#include <buffer/filebufferprovider.h>
#include <utils/widgetutils.h>
#include "notebookmgr.h"
#include "vnotex.h"
#include "externalfile.h"

#include "fileopenparameters.h"

using namespace vnotex;

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
    m_bufferServer->registerItem(helper.getFileType(FileTypeHelper::Markdown).m_typeName, markdownFactory);

    // Text.
    auto textFactory = QSharedPointer<TextBufferFactory>::create();
    m_bufferServer->registerItem(helper.getFileType(FileTypeHelper::Text).m_typeName, textFactory);
}

void BufferMgr::open(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (!p_node) {
        return;
    }

    if (p_node->isContainer()) {
        return;
    }

    auto buffer = findBuffer(p_node);
    if (!buffer) {
        auto nodePath = p_node->fetchAbsolutePath();
        auto fileType = FileTypeHelper::getInst().getFileType(nodePath).m_typeName;
        auto factory = m_bufferServer->getItem(fileType);
        if (!factory) {
            // No factory to open this file type.
            qInfo() << "file will be opened by default program" << nodePath;
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(nodePath));
            return;
        }

        BufferParameters paras;
        paras.m_provider.reset(new NodeBufferProvider(p_node->sharedFromThis()));
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

    QFileInfo finfo(p_filePath);
    if (!finfo.exists()) {
        qWarning() << QString("failed to open file %1 that does not exist").arg(p_filePath);
        return;
    }

    // Check if it is an internal node or not.
    auto node = loadNodeByPath(p_filePath);
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

    auto buffer = findBuffer(p_filePath);
    if (!buffer) {
        // Open it as external file.
        auto fileType = FileTypeHelper::getInst().getFileType(p_filePath).m_typeName;
        auto factory = m_bufferServer->getItem(fileType);
        if (!factory) {
            // No factory to open this file type.
            qInfo() << "file will be opened by default program" << p_filePath;
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(p_filePath));
            return;
        }

        BufferParameters paras;
        paras.m_provider.reset(new FileBufferProvider(QSharedPointer<ExternalFile>::create(p_filePath),
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

QSharedPointer<Node> BufferMgr::loadNodeByPath(const QString &p_path)
{
    const auto &notebooks = VNoteX::getInst().getNotebookMgr().getNotebooks();
    for (const auto &nb : notebooks) {
        auto node = nb->loadNodeByPath(p_path);
        if (node) {
            return node;
        }
    }

    return nullptr;
}
