// Focused value factories for the controller transfer-policy seam tests.

#include <controllers/notebooknodecontroller.h>

namespace tests {

vnotex::NodeTransferItemResult transferResult(vnotex::NodeTransferItemResult::Status p_status,
                                              VxCoreError p_error,
                                              const QString &p_destinationPath = QString(),
                                              const QJsonObject &p_resumeToken = QJsonObject()) {
  vnotex::NodeTransferItemResult result;
  result.m_status = p_status;
  result.m_error = p_error;
  result.m_errorMessage = QStringLiteral("reason");
  result.m_resumeToken = p_resumeToken;
  if (!p_destinationPath.isEmpty()) {
    result.m_destination.m_notebookId = QStringLiteral("destination");
    result.m_destination.m_relativePath = p_destinationPath;
    result.m_destination.m_nodeId = QStringLiteral("destination-id");
  }
  return result;
}

} // namespace tests
