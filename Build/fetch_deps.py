#!/usr/bin/env python3
"""Fetch the KenshiLib headers, import libraries and Boost headers a plugin build needs.

The layout produced here mirrors what KenshiLib_Examples_deps sets up locally, so
the same include and library paths work in CI and on a developer machine:

    <dest>/kenshilib/Include        KenshiLib + Kenshi + Ogre + MyGUI headers
    <dest>/kenshilib/Libraries      KenshiLib.lib, OgreMain_x64.lib, MyGUIEngine_x64.lib
    <dest>/boost_1_60_0/boost       Boost headers (kenshi/util/OgreUnordered.h needs them)
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

KENSHILIB_REPO = "https://github.com/BFrizzleFoShizzle/KenshiLib"
KENSHILIB_API = "https://api.github.com/repos/BFrizzleFoShizzle/KenshiLib"
BOOST_URL = "https://archives.boost.io/release/{version}/source/boost_{underscored}.zip"


def log(message):
    print(f"[deps] {message}", flush=True)


def run(args, **kwargs):
    log("$ " + " ".join(args))
    subprocess.run(args, check=True, **kwargs)


def http_get(url, token=None):
    request = urllib.request.Request(url)
    request.add_header("User-Agent", "re-kenshi-plugins-ci")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def download(url, destination, token=None):
    log(f"downloading {url}")
    data = http_get(url, token=token)
    with open(destination, "wb") as handle:
        handle.write(data)
    log(f"wrote {destination} ({os.path.getsize(destination) / 1024 / 1024:.1f} MB)")


def clone_kenshilib(dest, tag):
    """Clone the KenshiLib headers. The repo also carries the Ogre/MyGUI import libs."""
    target = os.path.join(dest, "kenshilib")
    if os.path.isdir(os.path.join(target, "Include")):
        log(f"kenshilib already present at {target}")
        return target
    shutil.rmtree(target, ignore_errors=True)
    try:
        run(["git", "clone", "--depth", "1", "--branch", tag, KENSHILIB_REPO, target])
    except subprocess.CalledProcessError:
        log(f"tag {tag} not found, falling back to the default branch")
        run(["git", "clone", "--depth", "1", KENSHILIB_REPO, target])
    return target


def stage_libraries(kenshilib_dir, tag, token):
    """Collect the import libraries next to each other in kenshilib/Libraries."""
    libraries = os.path.join(kenshilib_dir, "Libraries")
    os.makedirs(libraries, exist_ok=True)
    for subdir in ("ogre", "mygui"):
        source = os.path.join(libraries, subdir)
        if not os.path.isdir(source):
            continue
        for name in os.listdir(source):
            if name.lower().endswith(".lib"):
                shutil.copy2(os.path.join(source, name), os.path.join(libraries, name))
                log(f"staged {name}")

    kenshilib_lib = os.path.join(libraries, "KenshiLib.lib")
    if os.path.isfile(kenshilib_lib):
        log("KenshiLib.lib already present")
        return libraries

    # KenshiLib.lib is published as a release asset rather than committed.
    release_url = f"{KENSHILIB_API}/releases/tags/{tag}"
    try:
        release = json.loads(http_get(release_url, token=token))
    except urllib.error.HTTPError as error:
        log(f"release {tag} lookup failed ({error.code}), falling back to the latest release")
        release = json.loads(http_get(f"{KENSHILIB_API}/releases/latest", token=token))

    assets = release.get("assets", [])
    log(f"release {release.get('tag_name')} assets: {[asset['name'] for asset in assets]}")

    direct = [a for a in assets if a["name"].lower() == "kenshilib.lib"]
    archives = [a for a in assets if a["name"].lower().endswith((".zip", ".7z"))]

    if direct:
        download(direct[0]["browser_download_url"], kenshilib_lib, token=token)
    elif archives:
        archive_path = os.path.join(libraries, archives[0]["name"])
        download(archives[0]["browser_download_url"], archive_path, token=token)
        if archive_path.lower().endswith(".zip"):
            with zipfile.ZipFile(archive_path) as archive:
                for member in archive.namelist():
                    if os.path.basename(member).lower() == "kenshilib.lib":
                        log(f"extracting {member}")
                        with archive.open(member) as src, open(kenshilib_lib, "wb") as dst:
                            shutil.copyfileobj(src, dst)
                        break
    if not os.path.isfile(kenshilib_lib):
        raise SystemExit(
            "KenshiLib.lib could not be obtained. Assets seen: "
            + ", ".join(asset["name"] for asset in assets)
        )
    return libraries


def fetch_boost(dest, version):
    """Extract only the header tree; the compiled Boost libraries are not needed."""
    underscored = version.replace(".", "_")
    root = os.path.join(dest, f"boost_{underscored}")
    if os.path.isdir(os.path.join(root, "boost")):
        log(f"boost already present at {root}")
        return root

    archive_path = os.path.join(dest, f"boost_{underscored}.zip")
    if not os.path.isfile(archive_path):
        download(BOOST_URL.format(version=version, underscored=underscored), archive_path)

    prefix = f"boost_{underscored}/boost/"
    log(f"extracting {prefix}* from the Boost archive")
    extracted = 0
    with zipfile.ZipFile(archive_path) as archive:
        for member in archive.namelist():
            if member.startswith(prefix) and not member.endswith("/"):
                archive.extract(member, dest)
                extracted += 1
    log(f"extracted {extracted} Boost headers")
    if extracted == 0:
        raise SystemExit("The Boost archive did not contain the expected header tree.")
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", required=True, help="directory to populate")
    parser.add_argument("--kenshilib-tag", default="v0.4.0")
    parser.add_argument("--boost-version", default="1.60.0")
    parser.add_argument("--output", help="write the resolved paths as JSON here")
    args = parser.parse_args()

    os.makedirs(args.dest, exist_ok=True)
    token = os.environ.get("GITHUB_TOKEN")

    kenshilib_dir = clone_kenshilib(args.dest, args.kenshilib_tag)
    libraries = stage_libraries(kenshilib_dir, args.kenshilib_tag, token)
    boost_dir = fetch_boost(args.dest, args.boost_version)

    include_dir = os.path.join(kenshilib_dir, "Include")
    resolved = {
        "kenshilib": kenshilib_dir,
        "include": [
            include_dir,
            os.path.join(include_dir, "ogre"),
            os.path.join(include_dir, "kenshi"),
            boost_dir,
        ],
        "lib": [libraries],
    }
    for path in resolved["include"] + resolved["lib"]:
        if not os.path.isdir(path):
            raise SystemExit(f"Expected directory is missing: {path}")

    log("resolved dependency paths:")
    print(json.dumps(resolved, indent=2))
    if args.output:
        with open(args.output, "w", encoding="utf-8") as handle:
            json.dump(resolved, handle, indent=2)
        log(f"wrote {args.output}")


if __name__ == "__main__":
    sys.exit(main())
