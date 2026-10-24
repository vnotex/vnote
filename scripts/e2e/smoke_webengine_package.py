#!/usr/bin/env python3
"""Render with the final macOS package and inspect its mapped locale resources.

Compile a disposable host-architecture probe using SDK headers/import frameworks,
then run using only the signed package's frameworks, helper and resources.
"""

import argparse
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import tempfile


CASES = {
    "en_US": "en-US.pak",
    "zh_CN": "zh-CN.pak",
    "ja_JP": "ja.pak",
    "ko_KR": "ko.pak",
    "de_DE": "en-US.pak",
    "en_GB": "en-US.pak",
}
PROBE = r'''
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


def smoke(app, qt_prefix):
    frameworks = (app / "Contents/Frameworks").resolve(strict=True)
    locales = frameworks / "QtWebEngineCore.framework/Versions/A/Resources/qtwebengine_locales"
    with tempfile.TemporaryDirectory(prefix="vnote-webengine-package-") as directory:
        root = Path(directory)
        source = root / "probe.cpp"
        source.write_text(PROBE, encoding="utf-8")
        probe = root / "probe"
        command = ["/usr/bin/clang++", "-std=c++17", str(source), "-o", str(probe),
                   "-F" + str(qt_prefix / "lib"), "-Wl,-rpath," + str(frameworks)]
        for framework in ("QtCore", "QtGui", "QtWidgets", "QtWebEngineCore", "QtWebEngineWidgets"):
            command += ["-framework", framework]
        subprocess.run(command, check=True, timeout=120)
        clean_env = {key: value for key, value in os.environ.items()
                     if not key.upper().startswith(("QT", "QML", "CARGO", "RUST", "TYPST", "VTEXTEDIT", "LD_", "DYLD"))
                     and key not in ("CMAKE_PREFIX_PATH", "LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME")}
        clean_env.update(PATH="/usr/bin:/bin:/usr/sbin:/sbin", QT_QPA_PLATFORM="cocoa")
        # Locale resources are process-static: never reuse a process across cases.
        for locale, pak in CASES.items():
            home = root / locale
            home.mkdir()
            env = dict(clean_env, HOME=str(home), CFFIXED_USER_HOME=str(home),
                       XDG_CONFIG_HOME=str(home), XDG_DATA_HOME=str(home), XDG_CACHE_HOME=str(home))
            expected = (locales / pak).resolve(strict=True)
            log = home / "stderr.log"
            with log.open("w", encoding="utf-8") as stderr:
                process = subprocess.Popen([str(probe), locale], cwd=home, env=env,
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                           stderr=stderr, text=True)
                try:
                    ready, _, _ = select.select([process.stdout], [], [], 45)
                    if not ready or process.stdout.readline().strip() != "READY":
                        raise RuntimeError(f"{locale}: WebEngine did not render the smoke document")
                    check_mappings(mapped_files(process.pid), process.pid, frameworks, expected)
                    process.communicate(input="\n", timeout=15)
                    if process.returncode != 0:
                        raise RuntimeError(f"{locale}: probe exited {process.returncode}")
                except Exception:
                    if process.poll() is None:
                        process.kill()
                    process.communicate(timeout=15)
                    print(log.read_text(encoding="utf-8"), file=sys.stderr)
                    raise
            print(f"PASS: {locale} -> {pak}; DOM rendered; browser/helper mapped packaged Qt and locale", flush=True)


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
