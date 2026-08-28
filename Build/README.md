# Build automation

Continuous build and release for the plugins in this repository.

## Why this is not a plain MSBuild workflow

Every project targets the `v100` (Visual Studio 2010) platform toolset, and that
is not cosmetic: the plugins read Kenshi's own `std::string` and `std::set`
objects across the DLL boundary, so the STL layout has to match the game's MSVC
10 build. Compiling with a current toolset would link and then misbehave at
runtime.

GitHub hosted runners only ship the newest toolset, and MSBuild can only drive
`v100` when Visual Studio 2010 itself is installed. The workflow therefore
provisions the Windows SDK 7.1 compiler package — the freely available VC10
compiler — and invokes `cl.exe` and `link.exe` directly with flags that mirror
each project's `Release|x64` settings. `build_plugins.py` documents that mapping
at the top of the file.

Only the Boost headers are fetched, never the compiled Boost libraries. The
build defines `BOOST_ALL_NO_LIB`, so Boost's auto-link pragma does not ask for
`libboost_thread-vc100-mt-1_60.lib`, and `BOOST_ERROR_CODE_HEADER_ONLY`, which
supplies the two Boost.System symbols the plugins reference through inline code
in the KenshiLib headers. A local Visual Studio build resolves both from
`BOOST_ROOT\stage\lib` instead, linking the same definitions statically.

The staged compiler is verified before anything is built: if the banner is not
`Version 16.00 ... x64`, the build fails rather than silently producing a
binary from the wrong toolset.

## Layout

| File | Purpose |
| --- | --- |
| `plugins.json` | The eight plugins: source file, mod package folder, DLL name |
| `install_vc10.ps1` | Provisions and stages the VC10 x64 toolchain |
| `fetch_deps.py` | KenshiLib headers and import libraries, plus Boost headers |
| `build_plugins.py` | Compiles, installs the DLL into the mod folder, zips it |
| `validate_packages.py` | Manifest, `RE_Kenshi.json` and SourceData INI checks |

## Workflow

`.github/workflows/build.yml` runs on pushes to `main`, on `claude/**`
branches, on `v*` tags, and on demand.

- **validate** (Ubuntu) — package and data-pack checks, no toolchain needed.
- **build** (Windows) — provisions the toolchain, builds all eight plugins,
  uploads one zip per plugin plus the PDBs as artifacts.
- **release** (tags only) — attaches those zips to a *draft* GitHub release,
  so the packages can be checked before anyone can download them. Publish it
  from the repository's Releases page when it looks right; drop `--draft` from
  the workflow to publish automatically instead.

Each zip contains the mod folder exactly as it is installed, with the plugin's
README, CHANGELOG and licence beside it.

The toolchain and the dependencies are cached, so only the first run pays for
provisioning. Bump the `-v1` suffix on the cache keys to force a rebuild.

## Building locally

The scripts work outside CI too. With the KenshiLib dependencies already set up
the usual way (`KenshiLib_Examples_deps`), point them at that directory:

```powershell
./Build/install_vc10.ps1 -ToolchainDir C:\vc10
python Build/fetch_deps.py --dest C:\kenshi-deps --output C:\kenshi-deps\deps.json
python Build/build_plugins.py `
  --toolchain C:\vc10\toolchain.json `
  --deps C:\kenshi-deps\deps.json `
  --out-dir dist --version dev --only MultiXPMining
```

`--dry-run` prints the compiler and linker command lines without running them.

Building through Visual Studio 2010 as before still works and is unaffected;
this is an addition, not a replacement.

## Dependency versions

`KENSHILIB_TAG` (default `v0.4.0`, the version the plugins record in their
startup logs) and `BOOST_VERSION` (`1.60.0`, the version KenshiLib documents)
are set in the workflow. `workflow_dispatch` accepts a different KenshiLib tag
for testing against a newer release.
