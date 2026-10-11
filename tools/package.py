"""Build clean runtime, developer SDK and source archives; never include local profiles."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
MANIFEST = json.loads((ROOT / "module.json").read_text(encoding="utf-8"))
VERSION = MANIFEST["version"]
_HEADER = (ROOT / "src/link_version.h").read_text(encoding="utf-8")
if f'"{VERSION}"' not in _HEADER:
    raise SystemExit(f"src/link_version.h does not match module.json version {VERSION}")

def archive(name, files):
    target = DIST / name
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for source, destination in sorted(files, key=lambda f: f[1]):
            if not source.is_file():
                raise FileNotFoundError(source)
            z.write(source, destination)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
    print(f"{target.name}: {target.stat().st_size:,} bytes")
    return target

def collect(directory, prefix=None):
    base = ROOT / directory
    for p in base.rglob("*"):
        if p.is_file() and "__pycache__" not in p.parts:
            yield p, str(Path(prefix or directory) / p.relative_to(base)).replace("\\", "/")

def main():
    DIST.mkdir(exist_ok=True)
    notices = [(ROOT / n, n) for n in ("LICENSE", "THIRD-PARTY-NOTICES.md")]
    echo = archive("EFLink-Echo-1.0.0.zip", notices + [
        (ROOT / "examples/echo/module.json", "module.json"),
        (ROOT / "examples/echo/README.md", "README.md"),
        (ROOT / "build/eflink.example.echo.dll", "native/windows-x64/eflink.example.echo.dll")])
    runtime = archive(f"EFLink-{VERSION}.zip", notices + [
        (ROOT / "module.json", "module.json"), (ROOT / "README.md", "README.md"),
        (ROOT / "docs/VALIDATION.md", "docs/VALIDATION.md"),
        (ROOT / "docs/WORLD.md", "docs/WORLD.md"),
        (ROOT / "vendor/imgui/LICENSE.txt", "licenses/imgui.txt"),
        (ROOT / "vendor/nlohmann-LICENSE.MIT", "licenses/nlohmann.txt"),
        (ROOT / "build/eflink.core.dll", "native/windows-x64/eflink.core.dll")])
    sdk = archive(f"EFLink-SDK-{VERSION}.zip", notices + list(collect("include/EndfieldLink")) +
        list(collect("include/BetterEndfield")) + list(collect("examples")) + [
        (ROOT / "docs/SDK.md", "README.md"), (echo, echo.name),
        (ROOT / "tools/build-example.cmd", "build-example.cmd")])
    source_files = notices + [(ROOT / n, n) for n in ("README.md", "module.json", ".gitignore")]
    for directory in ("src", "include", "vendor", "tests", "docs", "tools", "examples", ".github"):
        source_files += list(collect(directory))
    source = archive(f"EFLink-Source-{VERSION}.zip", source_files)
    with (DIST / "SHA256SUMS.txt").open("w", encoding="ascii", newline="\n") as f:
        for p in (runtime, sdk, source, echo):
            f.write(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n")

if __name__ == "__main__":
    main()
