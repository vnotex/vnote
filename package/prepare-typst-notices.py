#!/usr/bin/env python3
"""Prepare required Typst runtime notices at deployment, never at configure/runtime.

Usage: python package/prepare-typst-notices.py --manifest <helper/Cargo.toml>
       --qt-prefix <SDK-prefix> --qt-version <version> --output-dir <directory>

Build prerequisites (provision separately, with network access):
  cargo +1.99.0 install cargo-about --locked --version 0.9.2 --features cli
  cargo +1.99.0 fetch --locked --manifest-path <helper/Cargo.toml>
The fetch must run from the helper directory and include non-host dependencies.

Outputs: LICENSE.typst-dependencies, LICENSE.vtextedit-typst,
qtpdf-<version>.spdx.json, qtwebengine-chromium-pdf-<version>.spdx.json.

Pinned JSON contract (licenses[].used_by[].crate and crates[].package):
https://github.com/EmbarkStudios/cargo-about/blob/0.9.2/src/generate.rs
https://github.com/EmbarkStudios/cargo-about/blob/0.9.2/src/cargo-about/generate.rs
"""

import argparse
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


CARGO = ["cargo", "+1.99.0"]
ABOUT_VERSION = "cargo-about 0.9.2"
NOTICE_PREFIXES = ("LICENSE", "COPYING", "NOTICE", "OFL")


def required_text(value, description):
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Missing or empty {description}")
    return value


def license_text(value, description):
    value = required_text(value, description)
    if value.strip().upper() in ("NONE", "NOASSERTION", "UNKNOWN", "IGNORE"):
        raise ValueError(f"Unresolved {description}: {value!r}")
    return value


def required_list(document, field, description):
    value = document.get(field)
    if not isinstance(value, list) or not value:
        raise ValueError(f"Missing or empty {description}: {field}")
    return value


def file_bytes(path, description):
    if not path.is_file():
        raise ValueError(f"Missing {description}: {path}")
    content = path.read_bytes()
    if not content.strip():
        raise ValueError(f"Empty {description}: {path}")
    return content


def run(command, cwd):
    # Inherit stderr so Cargo/license diagnostics survive a packaging failure.
    return subprocess.run(
        command, cwd=cwd, check=True, stdout=subprocess.PIPE,
        encoding="utf-8", errors="strict",
    ).stdout


def qt_attributions(prefix, version):
    inventories = {}
    for component in ("qtpdf", "qtwebengine-chromium-pdf"):
        filename = f"{component}-{version}.spdx.json"
        path = prefix / "sbom" / filename
        if not path.is_file():
            raise ValueError(
                f"Missing SDK attribution for Qt {version}: required {path}. "
                "Install the matching qtpdf and qtwebengine SDK attribution packages; "
                "inventories from another Qt version cannot be substituted."
            )
        content = file_bytes(path, f"SDK attribution for Qt {version}")
        try:
            document = json.loads(content)
            if document.get("spdxVersion") != "SPDX-2.3":
                raise ValueError("expected an SPDX-2.3 document")
            # Chromium's inventory is named 'Pdf' internally, not its Qt version.
            expected_name = f"qtpdf-{version}" if component == "qtpdf" else "Pdf"
            if document.get("name") != expected_name:
                raise ValueError(f"expected document name {expected_name!r}")
            extracted = required_list(
                document, "hasExtractedLicensingInfos", "SDK extracted license texts"
            )
            for entry in extracted:
                identifier = required_text(entry.get("licenseId"), "SDK license ID")
                license_text(entry.get("extractedText"), f"SDK license text {identifier}")
        except (ValueError, TypeError, AttributeError) as error:
            raise ValueError(f"Invalid SDK attribution for Qt {version}: {path}: {error}") from error
        inventories[filename] = content
    return inventories


def crate_key(package):
    # Never include machine-specific manifest paths or local Cargo package IDs.
    return (package["name"], package["version"], package.get("source") or "")


def crate_label(package):
    return f"{package['name']} {package['version']}"


def render_dependencies(about, metadata):
    packages = {}
    for package in required_list(metadata, "packages", "locked Cargo metadata"):
        identifier = required_text(package.get("id"), "Cargo package ID")
        required_text(package.get("name"), "Cargo package name")
        required_text(package.get("version"), "Cargo package version")
        required_text(package.get("manifest_path"), "Cargo package source manifest")
        if identifier in packages:
            raise ValueError(f"Duplicate Cargo package ID: {identifier}")
        packages[identifier] = package

    crates = {}
    for entry in required_list(about, "crates", "cargo-about crate inventory"):
        package = entry["package"]
        identifier = package["id"]
        if identifier not in packages or crate_key(package) != crate_key(packages[identifier]):
            raise ValueError(f"cargo-about crate is absent from locked metadata: {identifier}")
        if identifier in crates:
            raise ValueError(f"Duplicate cargo-about crate: {identifier}")
        crates[identifier] = license_text(entry.get("license"), f"crate license for {identifier}")

    root = metadata["resolve"]["root"]
    if root not in crates:
        raise ValueError("cargo-about omitted the helper crate from its license inventory")

    licenses = []
    covered = set()
    for entry in required_list(about, "licenses", "cargo-about license inventory"):
        identifier = required_text(entry.get("id"), "license ID")
        name = required_text(entry.get("name"), f"license name for {identifier}")
        text = license_text(entry.get("text"), f"license text for {identifier}")
        users = set()
        for user in required_list(entry, "used_by", f"crates using {identifier}"):
            package = user["crate"]
            package_id = package["id"]
            if package_id not in crates or crate_key(package) != crate_key(packages[package_id]):
                raise ValueError(f"License {identifier} references an unlisted crate: {package_id}")
            users.add(package_id)
        covered.update(users)
        users = sorted(users, key=lambda package_id: crate_key(packages[package_id]))
        licenses.append((identifier, name, text, users))
    missing = crates.keys() - covered
    if missing:
        names = sorted(crate_label(packages[identifier]) for identifier in missing)
        raise ValueError("Crates without resolved license texts: " + ", ".join(names))

    output = io.BytesIO()

    def emit(text):
        output.write(text.encode("utf-8"))

    emit("Typst helper dependency notices\n")
    emit("Generated with cargo-about 0.9.2 using locked, offline dependencies.\n")
    emit("Includes the shipped Windows, Linux and both macOS architectures.\n\n")
    emit("CRATE INVENTORY\n===============\n\n")
    for identifier in sorted(crates, key=lambda package_id: crate_key(packages[package_id])):
        package = packages[identifier]
        repository = package.get("repository")
        if repository is not None:
            required_text(repository, f"repository for {crate_label(package)}")
        emit(f"{crate_label(package)}\n")
        emit(f"Repository: {repository if repository is not None else '(not declared)'}\n")
        emit(f"License expression: {crates[identifier]}\n\n")

    emit("LICENSE TEXTS\n=============\n")
    for identifier, name, text, users in sorted(
        licenses,
        key=lambda entry: (entry[0], entry[1], entry[2], [crate_key(packages[i]) for i in entry[3]]),
    ):
        emit(f"\n{'=' * 80}\n{identifier}: {name}\nUsed by:\n")
        for user in users:
            emit(f"  {crate_label(packages[user])}\n")
        emit("\n")
        emit(text)
        emit("\n")

    assets = [packages[identifier] for identifier in crates
              if packages[identifier]["name"] == "typst-assets"]
    if len(assets) != 1:
        raise ValueError("Expected exactly one locked typst-assets crate in the shipped graph")
    asset = assets[0]
    source_root = Path(asset["manifest_path"]).resolve(strict=True).parent
    notices = []

    def walk_error(error):
        raise error

    for directory, subdirs, filenames in os.walk(source_root, onerror=walk_error):
        for subdir in subdirs:
            if (Path(directory) / subdir).is_symlink():
                raise ValueError(f"Cannot enumerate notices through a symlink: {directory}/{subdir}")
        for filename in filenames:
            if filename.upper().startswith(NOTICE_PREFIXES):
                path = Path(directory) / filename
                path.resolve(strict=True).relative_to(source_root)
                notices.append(path)
    # The pinned crate's root NOTICE holds embedded-font and other asset terms.
    if source_root / "LICENSE" not in notices or source_root / "NOTICE" not in notices:
        raise ValueError(f"Missing typst-assets LICENSE or embedded-font NOTICE beneath {source_root}")
    emit("\nVERBATIM TYPST-ASSETS NOTICES (INCLUDING EMBEDDED FONTS)\n")
    for path in sorted(notices, key=lambda path: path.relative_to(source_root).as_posix()):
        relative = path.relative_to(source_root).as_posix()
        emit(f"\n{'=' * 80}\n{crate_label(asset)}/{relative}\n\n")
        output.write(file_bytes(path, "typst-assets attribution"))
        emit("\n")
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--qt-prefix", required=True, type=Path)
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.qt_version):
            raise ValueError("--qt-version must be the exact SDK version (major.minor.patch)")
        manifest = args.manifest.resolve(strict=True)
        file_bytes(manifest, "helper Cargo manifest")
        file_bytes(manifest.with_name("Cargo.lock"), "helper Cargo.lock")
        config = Path(__file__).resolve().with_name("typst-about.toml")
        file_bytes(config, "checked-in cargo-about configuration")
        outputs = qt_attributions(args.qt_prefix.resolve(), args.qt_version)
        outputs["LICENSE.vtextedit-typst"] = file_bytes(
            manifest.parents[2] / "COPYING.LESSER", "vtextedit helper LGPL license"
        )

        version = run(CARGO + ["about", "--version"], manifest.parent).strip()
        if version != ABOUT_VERSION:
            raise ValueError(f"Required {ABOUT_VERSION}; found {version!r}")
        # No host filter: metadata must cover the union selected by the four-target
        # about.toml, including non-host sources provisioned by the build-time fetch.
        metadata = json.loads(run(CARGO + [
            "metadata", "--locked", "--offline", "--format-version", "1",
            "--manifest-path", str(manifest),
        ], manifest.parent))

        output_dir = args.output_dir.resolve()
        output_dir.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".typst-notices-", dir=output_dir.parent) as temporary:
            staging = Path(temporary)
            about_json = staging / "cargo-about.json"
            # A file also avoids cargo-about's PowerShell-ancestor stdout guard.
            run(CARGO + [
                "about", "generate", "--locked", "--offline", "--fail", "--format", "json",
                "--config", str(config), "--manifest-path", str(manifest),
                "--output-file", str(about_json),
            ], manifest.parent)
            about = json.loads(file_bytes(about_json, "cargo-about JSON output"))
            outputs["LICENSE.typst-dependencies"] = render_dependencies(about, metadata)
            # Publish only after every required input and license has succeeded.
            for filename, content in outputs.items():
                (staging / filename).write_bytes(content)
            output_dir.mkdir(parents=True, exist_ok=True)
            for filename in sorted(outputs):
                (staging / filename).replace(output_dir / filename)
            # Do not retain another SDK version's inventories on a reconfigured build.
            for pattern in ("qtpdf-*.spdx.json", "qtwebengine-chromium-pdf-*.spdx.json"):
                for old_inventory in output_dir.glob(pattern):
                    if old_inventory.name not in outputs:
                        old_inventory.unlink()
        print(f"Prepared Typst runtime notices in {output_dir}")
        return 0
    except subprocess.CalledProcessError as error:
        print(f"Typst notice preparation failed: command exited {error.returncode}: {error.cmd}", file=sys.stderr)
        return error.returncode if error.returncode > 0 else 1
    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
        print(f"Typst notice preparation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
