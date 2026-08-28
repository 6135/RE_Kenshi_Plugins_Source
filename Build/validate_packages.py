#!/usr/bin/env python3
"""Check the mod packages and data packs without needing a Windows toolchain.

Errors (fail the build) cover things that would ship a broken mod: a manifest
entry pointing at a file that does not exist, a package with no .mod file, a
RE_Kenshi.json that does not load the plugin's own DLL, or a SourceData INI the
plugin cannot parse.

Warnings are reported but never fail: the plugins use several different version
conventions, so version drift is surfaced for a human to judge.
"""

import argparse
import configparser
import json
import os
import re
import sys

NUMERIC_KEY = re.compile(r"(Multiplier|Cap|Ratio|Chance|Bonus|Scale)$", re.IGNORECASE)
BOOLEAN_VALUES = {"true", "false", "yes", "no", "on", "off", "1", "0"}
VERSION_IN_SOURCE = re.compile(r'k(?:Plugin)?Version\s*=\s*"([^"]+)"')
VERSION_IN_HEADING = re.compile(r"(\d+\.\d+(?:\.\d+)?(?:[-\w.]*)?)")


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, message):
        self.errors.append(message)
        print(f"ERROR   {message}", flush=True)

    def warn(self, message):
        self.warnings.append(message)
        print(f"warning {message}", flush=True)

    def ok(self, message):
        print(f"ok      {message}", flush=True)


def check_package(plugin, repo_root, report):
    name = plugin["name"]
    source = os.path.join(repo_root, plugin["source"])
    package = os.path.join(repo_root, plugin["package"])
    docs = os.path.join(repo_root, plugin["docs"])

    for label, path in (("source", source), ("package", package), ("docs", docs)):
        if not os.path.exists(path):
            report.error(f"{name}: {label} path does not exist: {plugin[label]}")
            return

    mod_files = [f for f in os.listdir(package) if f.lower().endswith(".mod")]
    if not mod_files:
        report.error(f"{name}: no .mod file in {plugin['package']}")
    else:
        report.ok(f"{name}: mod file {mod_files[0]}")

    re_kenshi = os.path.join(package, "RE_Kenshi.json")
    if os.path.isfile(re_kenshi):
        try:
            with open(re_kenshi, encoding="utf-8-sig") as handle:
                manifest = json.load(handle)
        except json.JSONDecodeError as error:
            report.error(f"{name}: RE_Kenshi.json is not valid JSON: {error}")
        else:
            plugins = manifest.get("Plugins", [])
            if plugin["dll"] not in plugins:
                report.error(
                    f"{name}: RE_Kenshi.json lists {plugins}, expected {plugin['dll']}"
                )
            else:
                report.ok(f"{name}: RE_Kenshi.json loads {plugin['dll']}")
    else:
        report.warn(f"{name}: no RE_Kenshi.json in {plugin['package']}")


def check_data_pack(plugin, repo_root, report):
    package = os.path.join(repo_root, plugin["package"])
    source_data = os.path.join(package, "SourceData")
    if not os.path.isdir(source_data):
        return

    for file_name in sorted(os.listdir(source_data)):
        if not file_name.lower().endswith(".ini"):
            continue
        path = os.path.join(source_data, file_name)
        relative = os.path.relpath(path, repo_root)
        parser = configparser.ConfigParser(strict=True, interpolation=None)
        parser.optionxform = str
        try:
            with open(path, encoding="utf-8-sig") as handle:
                parser.read_file(handle)
        except configparser.Error as error:
            report.error(f"{relative}: cannot be parsed: {error}")
            continue

        sections = [s for s in parser.sections() if s != "Metadata"]
        for section in sections:
            for key, value in parser.items(section):
                if NUMERIC_KEY.search(key):
                    try:
                        float(value)
                    except ValueError:
                        report.error(f"{relative}: [{section}] {key}={value!r} is not a number")
                elif key.lower() == "enabled":
                    if value.strip().lower() not in BOOLEAN_VALUES:
                        report.error(f"{relative}: [{section}] Enabled={value!r} is not a boolean")
            if not parser.has_option(section, "DisplayName"):
                report.warn(f"{relative}: [{section}] has no DisplayName")
        report.ok(f"{relative}: {len(sections)} source section(s)")


def check_versions(plugin, repo_root, report):
    docs = os.path.join(repo_root, plugin["docs"])
    source = os.path.join(repo_root, plugin["source"])
    if not os.path.isfile(source):
        return

    with open(source, encoding="utf-8", errors="replace") as handle:
        match = VERSION_IN_SOURCE.search(handle.read())
    if not match:
        return
    code_version = match.group(1)

    for doc_name in ("CHANGELOG.md", "README.md"):
        doc_path = os.path.join(docs, doc_name)
        if not os.path.isfile(doc_path):
            continue
        with open(doc_path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        heading = None
        for line in text.splitlines():
            if doc_name == "CHANGELOG.md" and line.startswith("## "):
                heading = line[3:].strip()
                break
            if doc_name == "README.md" and line.startswith("# "):
                heading = line[2:].strip()
                break
        if not heading:
            continue
        found = VERSION_IN_HEADING.search(heading)
        if not found:
            continue
        normalised_code = code_version.lower().replace("-", "").replace(" ", "")
        normalised_doc = found.group(1).lower().replace("-", "").replace(" ", "")
        if not normalised_code.startswith(normalised_doc) and not normalised_doc.startswith(normalised_code):
            report.warn(
                f"{plugin['name']}: source says {code_version}, {doc_name} says {found.group(1)}"
            )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default=os.path.join(os.path.dirname(__file__), "plugins.json"))
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(args.manifest, encoding="utf-8") as handle:
        plugins = json.load(handle)

    report = Report()
    for plugin in plugins:
        print(f"--- {plugin['name']}", flush=True)
        check_package(plugin, repo_root, report)
        check_data_pack(plugin, repo_root, report)
        check_versions(plugin, repo_root, report)

    print("", flush=True)
    print(f"{len(report.errors)} error(s), {len(report.warnings)} warning(s)", flush=True)

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as handle:
            handle.write(f"## Package validation\n\n")
            handle.write(f"- {len(report.errors)} error(s)\n- {len(report.warnings)} warning(s)\n\n")
            for message in report.errors:
                handle.write(f"- **error** {message}\n")
            for message in report.warnings:
                handle.write(f"- warning {message}\n")

    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
