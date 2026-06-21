import urllib.request
import json
import zipfile
import os
import subprocess
import sys
import shutil

BUILD_DIR   = os.path.dirname(os.path.abspath(__file__))
COMPILER_DIR = os.path.join(BUILD_DIR, "mingw_temp")

def get_mingw_url():
    print("Fetching latest winlibs MinGW release info...")
    req = urllib.request.Request(
        "https://api.github.com/repos/brechtsanders/winlibs_mingw/releases/latest",
        headers={"User-Agent": "openal-proxy-builder"}
    )
    with urllib.request.urlopen(req) as r:
        data = json.loads(r.read())
    for asset in data["assets"]:
        name = asset["name"]
        if ("x86_64" in name and "posix" in name and "seh" in name
                and name.endswith(".zip") and "llvm" not in name.lower()):
            print(f"Found: {name}")
            return asset["browser_download_url"], name
    raise RuntimeError("Could not find a suitable MinGW release asset")

def download_mingw(url, filename):
    dest = os.path.join(BUILD_DIR, filename)
    if os.path.exists(dest):
        print(f"Already downloaded: {filename}")
        return dest
    print(f"Downloading {filename} (this may take a minute)...")
    def progress(count, block, total):
        pct = count * block * 100 // total
        print(f"\r  {pct}%", end="", flush=True)
    urllib.request.urlretrieve(url, dest, reporthook=progress)
    print()
    return dest

def extract_gcc(zip_path):
    if os.path.isdir(COMPILER_DIR):
        shutil.rmtree(COMPILER_DIR)
    print("Extracting compiler (skipping share/ docs only)...")
    os.makedirs(COMPILER_DIR, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as z:
        members = [m for m in z.namelist() if "/share/" not in m]
        total = len(members)
        for i, m in enumerate(members, 1):
            if i % 500 == 0:
                print(f"\r  {i}/{total}", end="", flush=True)
            z.extract(m, COMPILER_DIR)
    print(f"\r  {total}/{total} done.")
    print("Extraction done.")

def find_gcc():
    for root, dirs, files in os.walk(COMPILER_DIR):
        if "gcc.exe" in files:
            return os.path.join(root, "gcc.exe")
    raise FileNotFoundError("gcc.exe not found after extraction")

def compile_dll(gcc):
    src = os.path.join(BUILD_DIR, "openal_proxy.c")
    out = os.path.join(BUILD_DIR, "OpenAL64.dll")

    cmd = [
        gcc,
        "-shared",
        "-O2",
        "-m64",
        "-static-libgcc",
        "-static",
        "-o", out,
        src,
        "-lkernel32",
        "-lole32",
    ]
    print("Compiling proxy DLL...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("COMPILATION FAILED:")
        print(result.stderr)
        sys.exit(1)
    print(f"Built: {out}")
    return out

def main():
    url, filename = get_mingw_url()
    zip_path = download_mingw(url, filename)
    if not os.path.isdir(COMPILER_DIR):
        extract_gcc(zip_path)
    gcc = find_gcc()
    print(f"Compiler: {gcc}")
    dll = compile_dll(gcc)
    print(f"\nDone! Built {dll}")
    print("This only compiles the proxy - it does not install/deploy it.")
    print("Run install.bat to deploy it (and the bundled OpenAL Soft engine,")
    print("alsoft.ini, etc.) to your Starsector install(s).")

if __name__ == "__main__":
    main()
