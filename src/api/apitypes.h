#ifndef VNOTEX_APITYPES_H
#define VNOTEX_APITYPES_H

#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace vnotex {
enum class ApiCommandKind {
  ListNotebooks,
  ListNodes,
  ReadNote,
  ReadContent,
  Search,
  CreateNote,
  ReplaceContent,
  AppendContent,
  RenameNote,
  TrashNote
};
enum class ApiContentView { Saved, Live };

struct ApiCommand {
  ApiCommandKind kind = ApiCommandKind::ListNotebooks;
  QString notebookId, nodeId, parentId, name, format, text, expectedVersion, cursor, query;
  QStringList notebookIds;
  ApiContentView view = ApiContentView::Saved;
  bool searchContent = true;
  qint64 offset = 0;
  int limit = 50;
};

struct ApiPrincipal {
  QString tokenId;
  QSet<QString> notebookIds, scopes;
};

struct ApiRequestContext {
  QString requestId;
  ApiPrincipal principal;
  std::shared_ptr<std::atomic_bool> cancelled;
  // Safe on every thread and after server/auth destruction; recheck before commitment.
  std::function<bool()> authorized;
};

struct ApiError {
  int status = 0;
  QString code, message;
};

struct ApiResult {
  QJsonObject data;
  ApiError error;
  bool committed = false;
  bool saved = false;
};

struct ApiTokenGrant {
  QString label;
  QSet<QString> notebookIds, scopes;
  // nullopt explicitly selects Never; an unconfigured grant must still fail validation.
  std::optional<QDateTime> expiresAt{QDateTime{}};
};

struct ApiIssuedToken {
  QString id;
  // The complete id.secret bearer value. Returned only after durable publication.
  QString secret;
};
} // namespace vnotex
#endif
