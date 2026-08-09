// tlsprobe - CI gate proving the PACKAGED Qt build can actually initialize TLS.
//
// Qt 5.15 on Windows has no Schannel TLS backend, so it must dlopen the bundled
// OpenSSL DLLs. When those DLLs cannot be loaded (e.g. they import a C runtime
// that is not present), QSslSocket::supportsSsl() is false and every HTTPS
// request dies with "TLS initialization failed" -- while the build stays green.
// This probe is run from INSIDE the packaged directory so it reproduces
// vnote.exe's real DLL load context.
//
// It is deliberately NOT installed: it must never enter the shipped package.
#include <QCoreApplication>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  const bool ok = QSslSocket::supportsSsl();
  out << "build=" << QSslSocket::sslLibraryBuildVersionString()
      << " link=" << QSslSocket::sslLibraryVersionString()
      << " supportsSsl=" << (ok ? "true" : "false") << Qt::endl;
  return ok ? 0 : 1;
}
