#!/usr/bin/env python3
"""Render with the final macOS package and inspect its mapped locale resources.

Compile a disposable host-architecture probe using SDK headers/import frameworks,
then run using only the signed package's frameworks, helper and resources.
"""

import argparse
import os
from pathlib import Path
import platform
import re
import select
import signal
import subprocess
import sys
import tempfile
import time


RENDER_TIMEOUT = 45
SHUTDOWN_TIMEOUT = 60
CLEANUP_TIMEOUT = 15

CASES = {
    "en_US": "en-US.pak",
    "zh_CN": "zh-CN.pak",
    "ja_JP": "ja.pak",
    "ko_KR": "ko.pak",
    "de_DE": "en-US.pak",
    "en_GB": "en-US.pak",
}
PROBE = r'''
#if defined(__aarch64__)
#include <arm_acle.h>
#endif
#include <QtCore/QLocale>
#include <QtCore/QSocketNotifier>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineWidgets/QWebEngineView>
#include <cstdio>

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  QLocale::setDefault(QLocale(QString::fromUtf8(argv[1])));
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
      std::puts("READY");
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
project(VNotePackageProbe LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets)
add_executable(probe probe.cpp)
target_compile_features(probe PRIVATE cxx_std_17)
target_link_libraries(probe PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)
set_target_properties(probe PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${VNOTE_PACKAGE_FRAMEWORKS}"
    INSTALL_RPATH_USE_LINK_PATH FALSE
)
'''


def mapped_files(pid):
    # Include the renderer: proving the browser alone loaded a locale misses
    # helper/resource lookup failures in QtWebEngineProcess.
    parents = {}
    for line in subprocess.check_output(
            ["/bin/ps", "-axo", "pid=,ppid="], text=True, timeout=10).splitlines():
        child, parent = map(int, line.split())
        parents[child] = parent
    pids = {pid}
    while True:
        children = {child for child, parent in parents.items() if parent in pids}
        if children.issubset(pids):
            break
        pids.update(children)
    listing = subprocess.check_output(
        ["/usr/sbin/lsof", "-p", ",".join(map(str, sorted(pids))), "-Fn"],
        text=True, timeout=15)
    files = {}
    current = None
    for line in listing.splitlines():
        if line.startswith("p"):
            current = int(line[1:])
            files[current] = set()
        elif line.startswith("n/") and current is not None:
            files[current].add(Path(line[1:]).resolve())
    return files


def check_mappings(files, pid, frameworks, expected):
    helpers_with_locale = 0
    for process_id, paths in files.items():
        qt = {path for path in paths if re.search(r"/Qt[^/]*\.framework/", path.as_posix())}
        if any(not path.is_relative_to(frameworks) for path in qt):
            raise RuntimeError(f"Process {process_id} loaded Qt outside the package: {qt}")
        packs = {path for path in paths if path.parent.name == "qtwebengine_locales"}
        if packs and packs != {expected}:
            raise RuntimeError(f"Process {process_id} mapped unexpected locales: {packs}")
        if process_id == pid:
            if packs != {expected} or not any("QtWebEngineCore.framework" in path.parts for path in qt):
                raise RuntimeError("Probe did not map the packaged WebEngine framework and locale")
        elif any(path.name == "QtWebEngineProcess" and path.is_relative_to(frameworks)
                 for path in paths) and packs == {expected}:
            helpers_with_locale += 1
    if pid not in files or not helpers_with_locale:
        raise RuntimeError("Packaged QtWebEngineProcess did not map the expected locale")


def group_processes(pgid, timeout=10):
    listing = subprocess.check_output(
        ["/bin/ps", "-axo", "pid=,ppid=,pgid=,stat=,comm="], text=True, timeout=timeout)
    return [line for line in listing.splitlines()
            if len(line.split(None, 4)) == 5 and line.split(None, 4)[2] == str(pgid)]


def retain_diagnostics(process, locale, attempt, stage, error, log, mappings):
    diagnostics = Path(os.environ.get("RUNNER_TEMP", tempfile.gettempdir())) / "vnote-package-diagnostics"
    try:
        diagnostics.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix=f"webengine-{locale}-attempt-{attempt}-", dir=diagnostics))
        stderr = log.read_text(encoding="utf-8", errors="replace")
        (directory / "stderr.log").write_text(stderr, encoding="utf-8")
        print(stderr, file=sys.stderr)
        with (directory / "failure.log").open("w", encoding="utf-8") as output:
            output.write(f"locale={locale} attempt={attempt} stage={stage} pid={process.pid}\n")
            output.write(f"command={process.args!r} cached_returncode={process.returncode}\n")
            output.write(f"failure={error}\n")
            for pid, paths in mappings.items():
                output.write(f"mapped files for {pid}:\n" + "\n".join(sorted(map(str, paths))) + "\n")
        print(f"WebEngine diagnostics: {directory}", flush=True)
    except OSError as diagnostic_error:
        print(f"Could not retain WebEngine failure: {diagnostic_error}", file=sys.stderr)
        return False

    # Best effort, with one overall budget; inspect only this probe's process group.
    deadline = time.monotonic() + 20
    try:
        with (directory / "processes.log").open("w", encoding="utf-8") as output:
            members = group_processes(process.pid)
            output.write("PID PPID PGID STATE COMMAND\n" + "\n".join(members) + "\n")
            for member in members:
                pid = member.split()[0]
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    output.write("Stack sampling budget exhausted\n")
                    break
                output.flush()
                try:
                    result = subprocess.run(
                        ["/usr/bin/sample", pid, "1", "-file", str(directory / f"sample-{pid}.txt")],
                        stdout=output, stderr=subprocess.STDOUT, timeout=min(5, remaining))
                    output.write(f"sample {pid}: exit={result.returncode}\n")
                except (OSError, subprocess.TimeoutExpired) as diagnostic_error:
                    output.write(f"sample {pid} unavailable: {diagnostic_error}\n")
    except (OSError, subprocess.SubprocessError) as diagnostic_error:
        print(f"WebEngine process diagnostics unavailable: {diagnostic_error}", file=sys.stderr)
    return True


def stop_probe(process):
    deadline = time.monotonic() + CLEANUP_TIMEOUT
    try:
        # Do not poll/reap before signalling: the unreaped leader owns this group ID.
        # communicate() may time out on a helper's inherited stdout after the leader exits.
        if process.returncode is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.communicate(timeout=max(0, deadline - time.monotonic()))
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, CLEANUP_TIMEOUT)
            members = group_processes(process.pid, timeout=remaining)
            if not any(not member.split()[3].startswith("Z") for member in members):
                return
            time.sleep(min(0.05, remaining))
    finally:
        for stream in (process.stdin, process.stdout):
            if stream is not None:
                stream.close()


def run_locale(command, locale, root, clean_env, frameworks, expected, allow_retry):
    for attempt in range(1, 3 if allow_retry else 2):
        home = root / f"{locale}-{attempt}"
        home.mkdir()
        env = dict(clean_env, HOME=str(home), CFFIXED_USER_HOME=str(home),
                   XDG_CONFIG_HOME=str(home), XDG_DATA_HOME=str(home), XDG_CACHE_HOME=str(home))
        log = home / "stderr.log"
        with log.open("w", encoding="utf-8") as stderr:
            process = subprocess.Popen(command, cwd=home, env=env, start_new_session=True,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=stderr, text=True)
            stage = "render"
            shutdown_timeout = False
            mappings = {}
            try:
                ready, _, _ = select.select([process.stdout], [], [], RENDER_TIMEOUT)
                if not ready or process.stdout.readline().strip() != "READY":
                    raise RuntimeError(f"{locale}: WebEngine did not render the smoke document")
                stage = "mappings"
                mappings = mapped_files(process.pid)
                check_mappings(mappings, process.pid, frameworks, expected)
                stage = "shutdown"
                try:
                    process.communicate(input="\n", timeout=SHUTDOWN_TIMEOUT)
                except subprocess.TimeoutExpired:
                    shutdown_timeout = True
                    raise
                if process.returncode != 0:
                    raise RuntimeError(f"{locale}: probe exited {process.returncode}")
            except Exception as error:
                retained = retain_diagnostics(process, locale, attempt, stage, error, log, mappings)
                try:
                    stop_probe(process)
                except (OSError, subprocess.SubprocessError) as cleanup_error:
                    print(f"WebEngine cleanup failed; not retrying: {cleanup_error}", file=sys.stderr)
                    raise error from cleanup_error
                if shutdown_timeout and allow_retry and attempt == 1 and retained:
                    print(f"::warning::WebEngine {locale} shutdown timed out after rendering and mapping "
                          "checks passed; retrying once with a fresh profile", flush=True)
                    continue
                raise
        if attempt == 2:
            print(f"::warning::WebEngine {locale} recovered on its only retry; first-attempt "
                  "diagnostics retained", flush=True)
        print(f"PASS: {locale} -> {expected.name}; DOM rendered; browser/helper mapped packaged Qt "
              "and locale; clean shutdown", flush=True)
        return attempt == 2


def smoke(app, qt_prefix):
    frameworks = (app / "Contents/Frameworks").resolve(strict=True)
    locales = frameworks / "QtWebEngineCore.framework/Versions/A/Resources/qtwebengine_locales"
    with tempfile.TemporaryDirectory(prefix="vnote-webengine-package-") as directory:
        root = Path(directory)
        source = root / "probe.cpp"
        source.write_text(PROBE, encoding="utf-8")
        (root / "CMakeLists.txt").write_text(PROBE_CMAKE, encoding="utf-8")
        build = root / "build"
        probe = build / "probe"
        commands = (
            ["cmake", "-S", str(root), "-B", str(build), "-G", "Unix Makefiles",
             "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_OSX_ARCHITECTURES={platform.machine()}",
             f"-DQt6_DIR={qt_prefix / 'lib/cmake/Qt6'}",
             f"-DVNOTE_PACKAGE_FRAMEWORKS={frameworks}"],
            ["cmake", "--build", str(build), "--config", "Release"],
        )
        # Configuration and compilation share the original compiler budget.
        deadline = time.monotonic() + 120
        for command in commands:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(command, 120)
            subprocess.run(command, check=True, timeout=remaining)
        clean_env = {key: value for key, value in os.environ.items()
                     if not key.upper().startswith(("QT", "QML", "CARGO", "RUST", "TYPST", "VTEXTEDIT", "LD_", "DYLD"))
                     and key not in ("CMAKE_PREFIX_PATH", "LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME")}
        clean_env.update(PATH="/usr/bin:/bin:/usr/sbin:/sbin", QT_QPA_PLATFORM="cocoa")
        # Locale resources are process-static: never reuse a process across cases.
        # Share one retry across the entire gate, not one retry per locale.
        retry_available = True
        for locale, pak in CASES.items():
            expected = (locales / pak).resolve(strict=True)
            if run_locale([str(probe), locale], locale, root, clean_env,
                          frameworks, expected, retry_available):
                retry_available = False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    args = parser.parse_args()
    try:
        if sys.platform != "darwin":
            raise RuntimeError("This smoke requires macOS and its native Qt SDK")
        smoke(args.app.resolve(strict=True), args.qt_prefix.resolve(strict=True))
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"WebEngine package smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
