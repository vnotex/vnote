#include "syncerrorpresenter.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace vnotex {

namespace {

// Case-insensitive regex match for Win32 error code 8 pattern.
// Matches: "Win32 error code 8", "Win32 error code: 8", "win32 error 8", etc.
bool matchesWin32ErrorEight(const QString &p_message) {
  // Pattern: optional "code" or "error", optional colon, optional whitespace, "8"
  QRegularExpression re(QStringLiteral("Win32\\s+error(?:\\s+code)?\\s*:?\\s*8\\b"),
                        QRegularExpression::CaseInsensitiveOption);
  return re.match(p_message).hasMatch();
}

// Case-insensitive substring search
bool containsInsensitive(const QString &p_haystack, const QString &p_needle) {
  return p_haystack.contains(p_needle, Qt::CaseInsensitive);
}

} // namespace

SyncErrorPresenter::PresentedError SyncErrorPresenter::present(Context p_ctx, VxCoreError p_code,
                                                               const QString &p_rawMessage) {
  PresentedError result;
  result.details = p_rawMessage;

#define VX_TR(text) QCoreApplication::translate("SyncErrorPresenter", text)

  // Rule 1: Win32 error code 8 in CredentialWrite context → vault saturation message.
  if (p_ctx == Context::CredentialWrite && matchesWin32ErrorEight(p_rawMessage)) {
    result.primary = VX_TR(
        "Windows Credential Manager rejected the request (error 8). Your saved credential "
        "vault may be full of stale entries. Run this in PowerShell to inspect: `cmdkey /list | "
        "findstr VNote`. To clean up, run `cmdkey /delete:<target>` for each stale entry. Then "
        "try again.");
    return result;
  }

  // Rule 2: Access denied in CredentialWrite context → permission guidance.
  if (p_ctx == Context::CredentialWrite && containsInsensitive(p_rawMessage, "access denied")) {
    result.primary = VX_TR(
        "Access denied while saving credentials. Verify that your user account has permission "
        "to access the Windows Credential Manager. Try running VNote with administrator "
        "privileges.");
    return result;
  }

  // Rule 3: Not found in CredentialRead context → re-enter token guidance.
  if (p_ctx == Context::CredentialRead && containsInsensitive(p_rawMessage, "not found")) {
    result.primary =
        VX_TR("Stored credential not found. The token may have been deleted from the Credential "
              "Manager. Please re-enter your authentication token in the Sync settings.");
    return result;
  }

  // Rule 4: Keychain unavailable message → build-without-keychain message.
  if (containsInsensitive(p_rawMessage, "secure-keychain-unavailable")) {
    result.primary =
        VX_TR("VNote was built without secure credential storage support. Credentials cannot be "
              "stored safely. Please rebuild VNote with QtKeychain support or use a build that "
              "includes it.");
    return result;
  }

  // Rule 5: Auth failed error code → auth failure guidance.
  if (p_code == VXCORE_ERR_SYNC_AUTH_FAILED) {
    result.primary =
        VX_TR("Authentication failed. Your credentials may be expired or incorrect. Please verify "
              "your username and password, or re-enter your authentication token in the Sync "
              "settings.");
    return result;
  }

  // Rule 6: Network error code → network failure guidance.
  if (p_code == VXCORE_ERR_SYNC_NETWORK) {
    result.primary =
        VX_TR("Network error while synchronizing. Please check your internet connection and verify "
              "that your remote repository URL is accessible.");
    return result;
  }

  // Rule 7: Fallback to generic message with raw details.
  result.primary = VX_TR("Sync operation failed.");

#undef VX_TR

  return result;
}

} // namespace vnotex
