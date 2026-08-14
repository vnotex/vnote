#include "imagehostpath.h"

#include <algorithm>

#include <QDir>
#include <QRegularExpression>
#include <QUrl>

using namespace vnotex;

namespace {

// Lexically normalize separators BEFORE any QDir call. QDir::cleanPath() only
// converts backslashes on Windows, so a Windows-style path handed to a Linux or
// macOS test runner would otherwise be classified differently there.
QString normalizeSeparators(const QString &p_path) {
  QString path = p_path;
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return path;
}

// Whether @p_path is a filesystem root that carries no folder name.
// Evaluated on the slash-normalized, UNCLEANED path, so that the leading "//"
// of a UNC path is still intact.
bool isRootPath(const QString &p_slashPath) {
  if (p_slashPath.isEmpty() || p_slashPath == QStringLiteral("/")) {
    return true;
  }

  // "C:", "C:/".
  static const QRegularExpression driveRootRe(QStringLiteral("^[A-Za-z]:/?$"));
  if (driveRootRe.match(p_slashPath).hasMatch()) {
    return true;
  }

  // UNC share root "//server/share" has no usable folder name of its own.
  static const QRegularExpression uncRootRe(QStringLiteral("^//[^/]+/[^/]+/?$"));
  return uncRootRe.match(p_slashPath).hasMatch();
}

// Every http/https URL that literally occurs in @p_content, in any syntax:
// a normal markdown link, an angle-bracket destination, a reference
// definition, an HTML <img src="...">, or plain text.
//
// This scan is a best effort and is allowed to truncate a URL that contains a
// closing delimiter; referencedInText() below is the safety net that keeps such
// a URL from being deleted.
QStringList extractRemoteUrls(const QString &p_content) {
  // Stop at whitespace and at the delimiters that can close a URL in markdown
  // or HTML. Trailing punctuation that cannot be part of a URL is trimmed.
  static const QRegularExpression urlRe(QStringLiteral("(?i)https?://[^\\s<>\"'`\\)\\]]+"));

  QStringList urls;
  auto it = urlRe.globalMatch(p_content);
  while (it.hasNext()) {
    auto url = it.next().captured();
    while (!url.isEmpty() && QStringLiteral(".,;:!").contains(url.right(1))) {
      url.chop(1);
    }
    if (!url.isEmpty()) {
      urls.append(url);
    }
  }
  return urls;
}

// Decode the HTML character references that can appear in a raw <img src="...">
// so that "a&amp;b.png" / "a&#38;b.png" compare equal to "a&b.png".
QString decodeHtmlEntities(const QString &p_text) {
  QString text = p_text;

  // Numeric references first: "&#38;" and "&#x26;" are just another spelling of
  // any character, including the "&" that starts a named reference.
  static const QRegularExpression numericRe(
      QStringLiteral("&#(?:([0-9]{1,7})|[xX]([0-9a-fA-F]{1,6}));"));
  QString decoded;
  decoded.reserve(text.size());
  int pos = 0;
  auto it = numericRe.globalMatch(text);
  while (it.hasNext()) {
    const auto match = it.next();
    bool ok = false;
    const uint code = match.captured(1).isEmpty() ? match.captured(2).toUInt(&ok, 16)
                                                  : match.captured(1).toUInt(&ok, 10);
    if (!ok || code == 0 || code > 0x10FFFF) {
      continue;
    }
    decoded += text.mid(pos, match.capturedStart() - pos);
    decoded += QString::fromUcs4(&code, 1);
    pos = match.capturedEnd();
  }
  decoded += text.mid(pos);
  text = decoded;

  text.replace(QStringLiteral("&lt;"), QStringLiteral("<"), Qt::CaseInsensitive);
  text.replace(QStringLiteral("&gt;"), QStringLiteral(">"), Qt::CaseInsensitive);
  text.replace(QStringLiteral("&quot;"), QStringLiteral("\""), Qt::CaseInsensitive);
  text.replace(QStringLiteral("&apos;"), QStringLiteral("'"), Qt::CaseInsensitive);
  // Last: an entity body may itself have been written as "&amp;lt;".
  text.replace(QStringLiteral("&amp;"), QStringLiteral("&"), Qt::CaseInsensitive);
  return text;
}

QString removeWhitespace(const QString &p_text) {
  QString text = p_text;
  text.remove(QRegularExpression(QStringLiteral("\\s")));
  return text;
}

// Last-resort, deliberately over-eager "is it still mentioned?" test, used to
// veto a deletion. The URL-extraction scan above cannot be exact for every
// syntax (a path may legally contain ')' or ']' or end in '.', an HTML
// attribute may entity-escape characters, an HTML URL may be folded across
// lines), and a wrong deletion is IRREVERSIBLE, so a candidate survives if its
// object path shows up in the text under any of those relaxations.
bool referencedInText(const QString &p_candidate, const QString &p_content) {
  if (p_content.contains(p_candidate, Qt::CaseInsensitive)) {
    return true;
  }

  const QUrl url(p_candidate);
  const auto path = url.path(QUrl::PrettyDecoded);
  if (path.isEmpty() || path == QStringLiteral("/")) {
    // Nothing distinctive to look for; refuse to delete.
    return true;
  }

  const auto content = decodeHtmlEntities(p_content);
  const auto encodedPath = url.path(QUrl::FullyEncoded);
  if (content.contains(path, Qt::CaseInsensitive) ||
      content.contains(encodedPath, Qt::CaseInsensitive)) {
    return true;
  }

  // A URL folded across lines inside an HTML attribute: compare with all
  // whitespace removed, in both the decoded and the encoded spelling.
  const auto squeezedContent = removeWhitespace(content);
  return squeezedContent.contains(removeWhitespace(path), Qt::CaseInsensitive) ||
         squeezedContent.contains(removeWhitespace(encodedPath), Qt::CaseInsensitive);
}

} // namespace

QString ImageHostPath::remotePath(const QString &p_contentDirPath, const QString &p_destFileName) {
  if (p_destFileName.isEmpty()) {
    return QString();
  }

  if (p_contentDirPath.isEmpty()) {
    return p_destFileName;
  }

  // p_contentDirPath is already the directory holding the note, so take its own
  // name. Do NOT call QFileInfo::dir() here: that climbs one level up and would
  // yield the grandparent folder of the note.
  const auto slashPath = normalizeSeparators(p_contentDirPath);
  if (isRootPath(slashPath)) {
    return p_destFileName;
  }

  const auto cleanPath = QDir::cleanPath(slashPath);
  if (isRootPath(cleanPath)) {
    return p_destFileName;
  }

  const auto folderName = QDir(cleanPath).dirName();
  if (folderName.isEmpty()) {
    return p_destFileName;
  }

  return folderName + QLatin1Char('/') + p_destFileName;
}

bool ImageHostPath::isRemoteUrl(const QString &p_url) {
  return p_url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
         p_url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

QString ImageHostPath::remoteUrlIdentity(const QString &p_url) {
  if (!isRemoteUrl(p_url)) {
    return QString();
  }

  QUrl url(p_url.trimmed());
  if (!url.isValid() || url.host().isEmpty()) {
    return QString();
  }

  // QUrl already lower-cases the scheme and the host. Query and fragment are
  // dropped: both bundled providers derive the object path from the path alone,
  // so "a.png" and "a.png?v=2" address the same remote object.
  url = url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::NormalizePathSegments);

  // An explicitly spelled default port denotes the same resource.
  const int port = url.port();
  if ((url.scheme() == QStringLiteral("http") && port == 80) ||
      (url.scheme() == QStringLiteral("https") && port == 443)) {
    url.setPort(-1);
  }

  // PrettyDecoded (not FullyDecoded): percent-encoded UNRESERVED characters are
  // decoded, so "My%20Nb" == "My Nb", while encoded delimiters stay encoded, so
  // "a%2Fb.png" stays distinct from "a/b.png". Path case is preserved on
  // purpose: image hosts serve case-sensitive object paths.
  return url.toString(QUrl::PrettyDecoded);
}

QStringList ImageHostPath::remoteUrlsToDelete(bool p_enabled, const QSet<QString> &p_candidates,
                                              const QSet<QString> &p_currentUrls,
                                              const QString &p_content) {
  if (!p_enabled) {
    // Settings -> Image Host -> "Clear obsolete images" is off: never touch the
    // image host. The gate lives here so it is covered by the same tests as the
    // classification it guards.
    return {};
  }

  QSet<QString> currentIdentities;
  const auto addIdentity = [&currentIdentities](const QString &p_url) {
    const auto identity = remoteUrlIdentity(p_url);
    if (!identity.isEmpty()) {
      currentIdentities.insert(identity);
    }
  };

  for (const auto &url : p_currentUrls) {
    addIdentity(url);
  }

  // The markdown image scan that produced p_currentUrls misses valid forms
  // (angle-bracket destinations, reference-style links, raw HTML), so the raw
  // content is scanned too. Comparing by IDENTITY rather than by literal text
  // is what makes a re-spelled URL in one of those forms safe.
  for (const auto &url : extractRemoteUrls(p_content)) {
    addIdentity(url);
  }

  QStringList result;
  for (const auto &candidate : p_candidates) {
    if (!isRemoteUrl(candidate)) {
      continue;
    }

    if (p_currentUrls.contains(candidate) || referencedInText(candidate, p_content)) {
      continue;
    }

    const auto identity = remoteUrlIdentity(candidate);
    if (identity.isEmpty() || currentIdentities.contains(identity)) {
      // An unparsable URL is never deleted: we cannot prove it is unreferenced.
      continue;
    }

    result.append(candidate);
  }

  std::sort(result.begin(), result.end());
  return result;
}
