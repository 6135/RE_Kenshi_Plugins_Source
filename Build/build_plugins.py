#!/usr/bin/env python3
"""Compile the plugins with the Visual C++ 2010 (v100) toolchain and package them.

The .vcxproj files target the v100 toolset, which MSBuild can only drive when
Visual Studio 2010 itself is installed. The Windows SDK 7.1 compiler package
provides cl.exe and link.exe without those MSBuild targets, so this script
invokes them directly with flags that mirror the projects' Release|x64 settings:

    Optimization           MaxSpeed            /O2
    IntrinsicFunctions     true                /Oi
    FunctionLevelLinking   true                /Gy
    WholeProgramOptimization true              /GL and /LTCG
    RuntimeLibrary         MultiThreadedDLL    /MD
    CharacterSet           Unicode             /D UNICODE /D _UNICODE
    ExceptionHandling      Sync                /EHsc
    OptimizeReferences     true                /OPT:REF
    EnableCOMDATFolding    true                /OPT:ICF
    SubSystem              Console             /SUBSYSTEM:CONSOLE
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile

# BOOST_ALL_NO_LIB disables Boost's #pragma comment(lib, ...) auto-linking.
# Several KenshiLib headers include boost/thread, so the pragma asks for
# libboost_thread-vc100-mt-1_60.lib even though the plugins only include those
# headers for type layout and never call into a compiled Boost library. A local
# Visual Studio build satisfies the pragma from BOOST_ROOT\stage\lib instead.
# If a plugin ever does reference a Boost symbol the link fails with an
# unresolved external rather than changing behaviour silently.
COMPILE_FLAGS = [
    "/c", "/nologo", "/W3", "/O2", "/Oi", "/Gy", "/GL", "/EHsc", "/MD", "/GS",
    "/Zi", "/Zc:wchar_t", "/Zc:forScope", "/fp:precise",
    "/D", "NDEBUG", "/D", "_CONSOLE", "/D", "UNICODE", "/D", "_UNICODE", "/D", "_WINDLL",
    "/D", "BOOST_ALL_NO_LIB",
]

LINK_FLAGS = [
    "/NOLOGO", "/DLL", "/LTCG", "/OPT:REF", "/OPT:ICF", "/DEBUG",
    "/SUBSYSTEM:CONSOLE", "/MACHINE:X64", "/DYNAMICBASE", "/NXCOMPAT",
]

SYSTEM_LIBS = [
    "kernel32.lib", "user32.lib", "gdi32.lib", "advapi32.lib", "shell32.lib",
    "ole32.lib", "oleaut32.lib", "uuid.lib",
]

PLUGIN_LIBS = ["KenshiLib.lib", "OgreMain_x64.lib"]

DOC_FILES = ("README.md", "README_JA.md", "CHANGELOG.md", "LICENSE.md", "LICENSE.txt",
             "LICENSE", "KNOWN_LIMITATIONS.md", "DATA_PACK_AUTHOR_GUIDE.md",
             "MOD_AUTHOR_GUIDE.md")


def log(message):
    print(f"[build] {message}", flush=True)


def load_json(path):
    with open(path, encoding="utf-8-sig") as handle:
        return json.load(handle)


def build_environment(toolchain, deps):
    env = dict(os.environ)
    env["INCLUDE"] = os.pathsep.join(toolchain["include"] + deps["include"])
    env["LIB"] = os.pathsep.join(toolchain["lib"] + deps["lib"])
    env["PATH"] = os.pathsep.join([os.path.dirname(toolchain["cl"]), env.get("PATH", "")])
    return env


def run(args, env, cwd, dry_run):
    log("$ " + subprocess.list2cmdline(args))
    if dry_run:
        return
    subprocess.run(args, env=env, cwd=cwd, check=True)


def compile_plugin(plugin, repo_root, toolchain, deps, work_dir, dry_run):
    name = plugin["name"]
    source = os.path.join(repo_root, plugin["source"])
    if not os.path.isfile(source) and not dry_run:
        raise SystemExit(f"{name}: source file is missing: {source}")

    obj_dir = os.path.join(work_dir, name)
    os.makedirs(obj_dir, exist_ok=True)
    obj_path = os.path.join(obj_dir, f"{name}.obj")
    dll_path = os.path.join(obj_dir, plugin["dll"])
    env = build_environment(toolchain, deps)

    run([toolchain["cl"]] + COMPILE_FLAGS + [
        f"/Fo{obj_path}",
        f"/Fd{os.path.join(obj_dir, name + '.compiler.pdb')}",
        source,
    ], env, obj_dir, dry_run)

    run([toolchain["link"]] + LINK_FLAGS + [
        f"/OUT:{dll_path}",
        f"/IMPLIB:{os.path.join(obj_dir, name + '.lib')}",
        f"/PDB:{os.path.join(obj_dir, name + '.pdb')}",
        obj_path,
    ] + PLUGIN_LIBS + SYSTEM_LIBS, env, obj_dir, dry_run)

    return dll_path


def package_plugin(plugin, repo_root, dll_path, out_dir, version, dry_run):
    """Zip the mod folder exactly as it is installed, with the plugin's docs beside it."""
    package_dir = os.path.join(repo_root, plugin["package"])
    docs_dir = os.path.join(repo_root, plugin["docs"])
    mod_folder = os.path.basename(package_dir.rstrip("/\\"))
    zip_path = os.path.join(out_dir, f"{plugin['name']}-{version}.zip")

    if dry_run:
        log(f"would package {package_dir} -> {zip_path}")
        return zip_path

    shutil.copy2(dll_path, os.path.join(package_dir, plugin["dll"]))
    log(f"installed {plugin['dll']} into {package_dir}")

    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for root, _, files in os.walk(package_dir):
            for file_name in files:
                absolute = os.path.join(root, file_name)
                relative = os.path.relpath(absolute, package_dir)
                archive.write(absolute, os.path.join(mod_folder, relative))
        for doc in DOC_FILES:
            doc_path = os.path.join(docs_dir, doc)
            if os.path.isfile(doc_path):
                archive.write(doc_path, doc)
    log(f"wrote {zip_path} ({os.path.getsize(zip_path) / 1024:.1f} KB)")
    return zip_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", required=True, help="toolchain.json from install_vc10.ps1")
    parser.add_argument("--deps", required=True, help="deps.json from fetch_deps.py")
    parser.add_argument("--manifest", default=os.path.join(os.path.dirname(__file__), "plugins.json"))
    parser.add_argument("--out-dir", required=True, help="where the packaged zips are written")
    parser.add_argument("--work-dir", default=os.path.join(os.path.dirname(__file__), "obj"))
    parser.add_argument("--version", default="dev")
    parser.add_argument("--only", action="append", help="build just these plugins (repeatable)")
    parser.add_argument("--dry-run", action="store_true", help="print the commands without running them")
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    toolchain = load_json(args.toolchain)
    deps = load_json(args.deps)
    plugins = load_json(args.manifest)
    if args.only:
        wanted = {name.lower() for name in args.only}
        plugins = [p for p in plugins if p["name"].lower() in wanted]
        if not plugins:
            raise SystemExit(f"No plugin in the manifest matched {args.only}")

    log(f"compiler: {toolchain.get('banner', toolchain['cl'])}")
    built = []
    for plugin in plugins:
        log(f"--- {plugin['name']}")
        dll_path = compile_plugin(plugin, repo_root, toolchain, deps, args.work_dir, args.dry_run)
        built.append(package_plugin(plugin, repo_root, dll_path, args.out_dir, args.version, args.dry_run))

    log(f"built {len(built)} plugin package(s)")
    for path in built:
        log(f"  {path}")


if __name__ == "__main__":
    sys.exit(main())
