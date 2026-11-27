#!/usr/bin/env python3
"""Check AppImage ELF architecture and render using its packaged WebEngine runtime."""

import argparse
import ctypes
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import tempfile
import time


PROBE = r'''
#if defined(__aarch64__)
#include <arm_acle.h>
#endif
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSocketNotifier>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineWidgets/QWebEngineView>
#include <cstdio>

bool checkBrowserMappings(const QString &root) {
  QFile maps(QStringLiteral("/proc/self/maps"));
  if (!maps.open(QIODevice::ReadOnly)) {
    qCritical() << "Cannot inspect packaged browser" << maps.errorString();
    return false;
  }
  bool foundQt = false;
  // procfs files report size zero; read until EOF instead of trusting atEnd().
  while (true) {
    const auto line = maps.readLine();
    if (line.isEmpty()) break;
    const auto start = line.indexOf('/');
    if (start < 0) continue;
    const QFileInfo file(QString::fromUtf8(line.mid(start).trimmed()));
    const auto name = file.fileName();
    const bool qt = name.startsWith(QStringLiteral("libQt6"));
    if (!qt) continue;
    if (!file.canonicalFilePath().startsWith(root)) {
      qCritical() << "Runtime dependency escapes AppDir:" << file.filePath();
      return false;
    }
    foundQt |= qt;
  }
  return foundQt;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const auto root = QFileInfo(QString::fromUtf8(argv[1])).canonicalFilePath() + '/';
  QApplication app(argc, argv);
  QWebEngineView view;
  QSocketNotifier input(fileno(stdin), QSocketNotifier::Read);
  QObject::connect(&input, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
  QTimer::singleShot(45000, &app, [&app]() { app.exit(3); });
  QObject::connect(&view, &QWebEngineView::loadFinished, &app, [&](bool ok) {
    if (!ok) { app.exit(4); return; }
    view.page()->runJavaScript(
        "document.getElementById('package-smoke').textContent", [&](const QVariant &value) {
      if (value.toString() != QStringLiteral("Packaged WebEngine rendered")) {
        app.exit(5);
        return;
      }
      if (!checkBrowserMappings(root) || view.page()->renderProcessPid() <= 0) {
        app.exit(6);
        return;
      }
      // The unprivileged browser cannot inspect its non-dumpable sandboxed renderer.
      std::printf("READY %lld\n", static_cast<long long>(view.page()->renderProcessPid()));
      std::fflush(stdout);
    });
  });
  view.resize(640, 320);
  view.show();
  view.setHtml("<!doctype html><html><body><h1 id='package-smoke'>"
               "Packaged WebEngine rendered</h1></body></html>");
  return app.exec();
}
'''

PROBE_CMAKE = r'''
cmake_minimum_required(VERSION 3.21)
project(VNoteLinuxPackageProbe LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)
add_executable(probe probe.cpp)
target_compile_features(probe PRIVATE cxx_std_17)
target_link_libraries(probe PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)
set_target_properties(probe PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${VNOTE_PACKAGE_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE
)
'''


def verify_elfs(appdir, architecture):
    expected = {"x64": 62, "arm64": 183}[architecture]
    count = 0
    for path in appdir.rglob("*"):
        if not path.is_file():
            continue
        with path.open("rb") as stream:
            header = stream.read(20)
        if header[:4] != b"\x7fELF":
            continue
        if (len(header) != 20 or header[4:6] != b"\x02\x01"
                or int.from_bytes(header[18:20], "little") != expected):
            raise RuntimeError(f"Packaged ELF does not match {architecture}: {path}")
        count += 1
    if not count:
        raise RuntimeError("AppDir contains no ELF binaries")
    print(f"Verified {count} packaged {architecture} ELF files", flush=True)


def packaged_file(appdir, name):
    matches = {path.resolve(strict=True) for path in appdir.rglob(name) if path.is_file()}
    if len(matches) != 1:
        raise RuntimeError(f"Expected one packaged {name}: {matches}")
    path = matches.pop()
    if not path.is_relative_to(appdir):
        raise RuntimeError(f"Packaged {name} escapes AppDir: {path}")
    return path


def check_renderer_mappings(contents, appdir):
    found_qt = False
    found_helper = False
    for line in contents.splitlines():
        fields = line.split(None, 5)
        if len(fields) != 6 or not fields[5].startswith("/"):
            continue
        path = Path(fields[5])
        qt = path.name.startswith("libQt6")
        helper = path.name == "QtWebEngineProcess"
        if not qt and not helper:
            continue
        if not path.resolve(strict=True).is_relative_to(appdir):
            raise RuntimeError(f"Renderer dependency escapes AppDir: {path}")
        found_qt |= qt
        found_helper |= helper
    if not found_qt or not found_helper:
        raise RuntimeError("Renderer did not map the packaged Qt libraries and helper")


def enable_subreaper():
    # This standalone supervisor owns all children below. Adopt orphaned sandbox
    # helpers too, including descendants that leave the probe's process group.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))


def wait_for_exit(process, timeout):
    deadline = time.monotonic() + timeout
    while True:
        status = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
        if status is not None:
            return status.si_status if status.si_code == os.CLD_EXITED else -status.si_status
        if time.monotonic() >= deadline:
            raise subprocess.TimeoutExpired(process.args, timeout)
        time.sleep(0.05)


def stop_probe(process):
    deadline = time.monotonic() + 15
    # wait_for_exit deliberately leaves the leader unreaped, reserving its PID/PGID.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=max(0.001, deadline - time.monotonic()))
    children = Path(f"/proc/self/task/{os.getpid()}/children")
    while True:
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            break
        if pid:
            continue
        # Only unreaped children of this dedicated supervisor can appear here;
        # their PIDs cannot be reused until this supervisor reaps them.
        for child in children.read_text().split():
            try:
                os.kill(int(child), signal.SIGKILL)
            except ProcessLookupError:
                pass
        if time.monotonic() >= deadline:
            raise RuntimeError("Probe descendants did not exit during cleanup")
        time.sleep(0.05)
    for stream in (process.stdin, process.stdout):
        if stream is not None:
            stream.close()


def smoke(appdir, qt_prefix):
    library_dir = (appdir / "usr/lib").resolve(strict=True)
    helper = packaged_file(appdir, "QtWebEngineProcess")
    resources = packaged_file(appdir, "qtwebengine_resources.pak").parent
    locales = packaged_file(appdir, "en-US.pak").parent
    with tempfile.TemporaryDirectory(prefix="vnote-linux-webengine-") as directory:
        root = Path(directory)
        (root / "probe.cpp").write_text(PROBE, encoding="utf-8")
        (root / "CMakeLists.txt").write_text(PROBE_CMAKE, encoding="utf-8")
        build = root / "build"
        deadline = time.monotonic() + 120
        for command in (
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Unix Makefiles",
             "-DCMAKE_BUILD_TYPE=Release", f"-DQt6_DIR={qt_prefix / 'lib/cmake/Qt6'}",
             f"-DVNOTE_PACKAGE_LIBDIR={library_dir}"],
            ["cmake", "--build", str(build), "--target", "probe"],
        ):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(command, 120)
            subprocess.run(command, check=True, timeout=remaining)
        home = root / "home"
        home.mkdir()
        runtime = root / "runtime"
        runtime.mkdir(mode=0o700)
        clean_env = {key: value for key, value in os.environ.items()
                     if not key.upper().startswith(("QT", "QML", "CARGO", "RUST", "TYPST", "VTEXTEDIT", "LD_", "DYLD"))
                     and key not in ("CMAKE_PREFIX_PATH", "LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME")}
        clean_env.update(
            PATH="/usr/bin:/bin", HOME=str(home), XDG_CONFIG_HOME=str(home),
            XDG_DATA_HOME=str(home), XDG_CACHE_HOME=str(home), XDG_RUNTIME_DIR=str(runtime),
            LD_LIBRARY_PATH=str(library_dir), QT_PLUGIN_PATH=str(appdir / "usr/plugins"),
            QT_QPA_PLATFORM="xcb", LIBGL_ALWAYS_SOFTWARE="1", LANG="en_US.UTF-8",
            LC_ALL="en_US.UTF-8", QTWEBENGINEPROCESS_PATH=str(helper),
            QTWEBENGINE_RESOURCES_PATH=str(resources), QTWEBENGINE_LOCALES_PATH=str(locales),
        )
        command = [str(build / "probe"), str(appdir)]
        enable_subreaper()
        process = subprocess.Popen(command, cwd=home, env=clean_env, start_new_session=True,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            if not select.select([process.stdout], [], [], 45)[0]:
                raise subprocess.TimeoutExpired(command, 45)
            ready = re.fullmatch(r"READY ([1-9][0-9]*)\n", process.stdout.readline())
            if ready is None:
                raise RuntimeError("Probe exited without confirming a rendered DOM")
            # Chromium marks renderers non-dumpable. Elevate only this bounded,
            # read-only procfs inspection, never the browser or renderer itself.
            maps = subprocess.check_output(
                ["sudo", "-n", "cat", "--", f"/proc/{int(ready[1])}/maps"],
                text=True, timeout=10)
            check_renderer_mappings(maps, appdir)
            process.stdin.write("\n")
            process.stdin.flush()
            result = wait_for_exit(process, 60)
            if result:
                raise subprocess.CalledProcessError(result, command)
        finally:
            stop_probe(process)
        print("Packaged WebEngine rendered with package-only Qt and helper mappings", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appdir", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--architecture", choices=("x64", "arm64"), required=True)
    args = parser.parse_args()
    try:
        if sys.platform != "linux":
            raise RuntimeError("This smoke requires Linux and its native Qt SDK")
        appdir = args.appdir.resolve(strict=True)
        verify_elfs(appdir, args.architecture)
        smoke(appdir, args.qt_prefix.resolve(strict=True))
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Linux WebEngine package smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
