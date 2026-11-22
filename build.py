#!/usr/bin/env python3
"""Builds the player. One script for Windows, Linux (also the Raspberry Pi) and macOS: Python 3.9+ and a C compiler, no WSL.

    python build.py                              the guided setup: installs what it can, asks where your game is, builds, says where it is
    python build.py update                       check GitHub for a newer version and install it (the guided setup does this first, too)
    python build.py doctor                       what is installed, what is missing and how to get it
    python build.py data <arcade root>           gen + convert: the tables, the recompiled physics and the game's data (data/)
    python build.py exe [--target T] [--cc CC]   the player: build/<target>/super3dx[.exe]
    python build.py package [--target T]         build/<target>/ with the player, its libraries, data/ and play.bat/play.sh, to zip up
    python build.py all <arcade root>            data, exe and package in one go
    python build.py switch                       Switch homebrew: platform/switch/super3dx+nx.nro (needs devkitPro, see docs/SWITCH.md)

Targets: `windows` (default on Windows), `linux` (default elsewhere; the Raspberry Pi 5 is the same build, made on the Pi or
with --target pi from another machine with a sysroot, see docs/PI.md).

Compilers, in the order tried unless you pass --cc: for windows `zig cc` (one download from ziglang.org, no installer; `python
build.py setup-zig` fetches it into .super3dx/), then mingw-w64's x86_64-w64-mingw32-gcc (MSYS2, or Linux); for linux gcc or clang.
The Windows build also needs the SDL2 development files for mingw (`python build.py setup-sdl2`, or SDL2_DIR=<...>/x86_64-w64-mingw32).
zlib and the OpenGL ES headers are in third_party/ and need nothing installed.
"""
import argparse
import concurrent.futures
import glob
import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(ROOT, ".super3dx")  # downloaded toolchain pieces (git-ignored)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from setupkit import ask, yes, step, run_logged, explain_failure, find_game, browse_folder, ISSUES  # noqa: E402  (the guided setups' shared helpers)
SDL_VER = "2.30.9"
ZIG_VER = "0.17.1"
ZIG = {  # (url, sha256) from ziglang.org/download/index.json
    "x86_64-windows": ("https://ziglang.org/download/%s/zig-x86_64-windows-%s.zip" % (ZIG_VER, ZIG_VER), "b03f477a1943673e02aca0f570e8030dffd536ce1f45cbc4d3326ec31159562e"),
    "x86_64-linux": ("https://ziglang.org/download/%s/zig-x86_64-linux-%s.tar.xz" % (ZIG_VER, ZIG_VER), "3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a"),
    "aarch64-linux": ("https://ziglang.org/download/%s/zig-aarch64-linux-%s.tar.xz" % (ZIG_VER, ZIG_VER), "a1546d5646eb184437918d6635ca6b762a0e889eac09cef720ee7a1ed3fe7795"),
    "x86_64-macos": ("https://ziglang.org/download/%s/zig-x86_64-macos-%s.tar.xz" % (ZIG_VER, ZIG_VER), "9af07b20319fbd0c92ee46ad23e81d6a782b1aaea2cfb8dcbff35f1d2c7ddbb7"),
    "aarch64-macos": ("https://ziglang.org/download/%s/zig-aarch64-macos-%s.tar.xz" % (ZIG_VER, ZIG_VER), "75f1bc7a1e9852f17ec9a0c1895f767c4ec56d46bc8a834920eece1d0359dd8d"),
}
SRCS = ("platform/sdl2/main_sdl.c platform/sdl2/wheel.c platform/sdl2/bgm.c platform/sdl2/movie.c src/app.c src/render.c src/hud.c src/car.c src/collide.c src/sound.c "
        "src/anm.c src/menu.c src/bind.c src/lights.c src/spark.c src/haptic.c src/ffb.c src/net.c src/vs_host.c src/nat.c src/title.c src/phys/rc_runtime.c").split()
GENERATED = ["src/phys/phys_rc.c", "src/phys/rc_addr.h", "src/cardb.h"]


def say(*a):
    print(*a, flush=True)


def die(msg):
    sys.exit("build.py: " + msg)


def which(*names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    return None


# --------------------------------------------------------------------------------------------- downloads (setup-*)
def _sha_ok(path, sha256):
    if not sha256:
        return True
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest() == sha256


def _fetch_with_system_tool(url, part):
    """Python's own HTTPS can fail on a PC whose certificates it cannot see (an antivirus that inspects HTTPS, a company proxy, a missing root
    certificate: "CERTIFICATE_VERIFY_FAILED", issue 10). Windows' own downloaders use the system's certificates: try them. True when it worked."""
    cmds = []
    if shutil.which("curl"):  # (curl.exe ships with Windows 10 and later, and is on most Linux and Mac systems)
        cmds.append(["curl", "-L", "--fail", "-sS", "--connect-timeout", "30", "-o", part, url])
    if os.name == "nt":
        ps = shutil.which("powershell")
        if ps:
            cmds.append([ps, "-NoProfile", "-Command",
                         "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
                         "$ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri '%s' -OutFile '%s'" % (url, part)])
    for c in cmds:
        try:
            if subprocess.run(c, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800).returncode == 0 and os.path.getsize(part) > 0:
                return True
        except (OSError, subprocess.SubprocessError):
            pass
        try:
            os.remove(part)
        except OSError:
            pass
    return False


def download(url, dest, sha256=None):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.isfile(dest) and _sha_ok(dest, sha256):  # (saved there by hand, from a browser)
        say("using the file already there:", dest)
        return
    say("downloading", url)
    part = dest + ".part"
    try:
        with urllib.request.urlopen(url, timeout=60) as r, open(part, "wb") as f:
            shutil.copyfileobj(r, f)
    except OSError as e:  # (URLError, timeouts, a full disk, a refused write are all OSErrors)
        say("Python could not download it (%s); trying the system's downloader" % e)
        if not _fetch_with_system_tool(url, part):
            die("could not download %s\n  (%s)\n  Check your internet connection (a firewall, VPN or proxy can block it) and run this again.\n"
                "  If it says CERTIFICATE_VERIFY_FAILED, an antivirus or proxy is intercepting HTTPS: switch it off for a moment, or use another network.\n"
                "  Or download that file yourself, in a browser, and save it as exactly this name:\n    %s" % (url, e, dest))
    if not _sha_ok(part, sha256):
        os.remove(part)
        die("the download's SHA-256 is not the expected one (%s): try again" % os.path.basename(dest))
    os.replace(part, dest)


def setup_zig():
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}.get(platform.machine().lower())
    os_ = {"Windows": "windows", "Linux": "linux", "Darwin": "macos"}.get(platform.system())
    if not arch or not os_ or "%s-%s" % (arch, os_) not in ZIG:
        die("no Zig download for this machine: install a C compiler instead (see docs/BUILDING.md)")
    url, sha = ZIG["%s-%s" % (arch, os_)]
    dest = os.path.join(TOOLS, os.path.basename(url))
    out = os.path.join(TOOLS, "zig")
    if not glob.glob(os.path.join(out, "zig*")):
        download(url, dest, sha)
        tmp = os.path.join(TOOLS, "zig.tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        (zipfile.ZipFile(dest) if dest.endswith(".zip") else tarfile.open(dest)).extractall(tmp)
        inner = os.path.join(tmp, os.listdir(tmp)[0])
        shutil.rmtree(out, ignore_errors=True)
        shutil.move(inner, out)
        shutil.rmtree(tmp, ignore_errors=True)
        os.remove(dest)
    say("zig:", find_zig())


def find_zig():
    exe = "zig.exe" if os.name == "nt" else "zig"
    p = os.path.join(TOOLS, "zig", exe)
    return p if os.path.isfile(p) else which("zig")


def setup_sdl2():
    out = os.path.join(TOOLS, "SDL2-%s" % SDL_VER)
    if not os.path.isdir(out):
        url = "https://github.com/libsdl-org/SDL/releases/download/release-%s/SDL2-devel-%s-mingw.zip" % (SDL_VER, SDL_VER)
        dest = os.path.join(TOOLS, "sdl2-mingw.zip")
        # (a zip saved by hand under the name it has on the download page counts too)
        by_hand = sorted(glob.glob(os.path.join(TOOLS, "SDL2-devel-*mingw*.zip")))
        if by_hand and not os.path.isfile(dest):
            dest = by_hand[-1]
        download(url, dest)
        try:
            zipfile.ZipFile(dest).extractall(TOOLS)
        except zipfile.BadZipFile:
            os.remove(dest)
            die("%s is not a complete zip (the download was cut off): it was removed, run this again" % dest)
        os.remove(dest)
    say("SDL2 (mingw):", sdl2_mingw())


ANGLE_DLLS = ["libEGL.dll", "libGLESv2.dll"]


def sdl2_mingw():
    if os.environ.get("SDL2_DIR"):
        return os.environ["SDL2_DIR"]
    for pat in (os.path.join(TOOLS, "SDL2-*", "x86_64-w64-mingw32"), "/opt/mt3dx/win/SDL2-*/x86_64-w64-mingw32", "/usr/x86_64-w64-mingw32"):
        m = sorted(glob.glob(pat))
        if m and os.path.isdir(os.path.join(m[-1], "include")):
            return m[-1]
    return None


# ------------------------------------------------------------------------------------------------------- compile
class Toolchain:
    """a compiler command (a list), what it makes for, and the flags that depend on it"""

    def __init__(self, target, cc=None):
        self.target = target
        self.kind = None
        if target == "windows":
            if cc:
                self.cc = cc.split()
            elif find_zig():
                self.cc = [find_zig(), "cc", "-target", "x86_64-windows-gnu"]
            elif which("x86_64-w64-mingw32-gcc-posix", "x86_64-w64-mingw32-gcc"):
                self.cc = [which("x86_64-w64-mingw32-gcc-posix", "x86_64-w64-mingw32-gcc")]
            else:
                die("no compiler for Windows: `python build.py setup-zig` (or install mingw-w64, see docs/BUILDING.md)")
            self.kind = "zig" if "zig" in os.path.basename(self.cc[0]).lower() else "gcc"
        else:
            if cc:
                self.cc = cc.split()
            elif target == "pi" and os.environ.get("SYSROOT"):
                self.cc = [(os.environ.get("CROSS", "aarch64-linux-gnu") + "-gcc")]
            else:
                found = which("gcc", "cc", "clang")
                if not found:
                    die("no C compiler: install gcc or clang (see docs/BUILDING.md)")
                self.cc = [found]
            self.kind = "gcc"


def newer(src, dst):
    return not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst)


def run(cmd, **kw):
    try:
        r = subprocess.run(cmd, **kw)
    except OSError as e:
        die("could not start %s: %s" % (cmd[0], e))
    if r.returncode:
        die("a build command failed (exit code %d): %s ...\n  The compiler's or tool's own message is just above this line." % (r.returncode, " ".join(str(c) for c in cmd[:4])))


def gl_names():
    """the OpenGL ES functions the game calls: its sources' gl* words that the GLES2 headers declare"""
    declared = set()
    for h in glob.glob(os.path.join(ROOT, "third_party", "khronos", "GLES2", "*.h")):
        declared |= set(re.findall(r"\b(gl[A-Z]\w*)\s*\(", open(h, encoding="utf-8", errors="replace").read()))
    used = set()
    for s in SRCS:
        used |= set(re.findall(r"\b(gl[A-Z]\w*)\b", open(os.path.join(ROOT, s), encoding="utf-8", errors="replace").read()))
    return sorted(used & declared)


def build_exe(target, cc, opt, jobs):
    for f in GENERATED:
        if not os.path.isfile(os.path.join(ROOT, f)):
            die("missing %s: run `python build.py data <arcade root>` first (the tables and the recompiled physics are made from your copy of the game)" % f)
    tc = Toolchain(target, cc)
    win = target == "windows"
    out = os.path.join(ROOT, "build", target)
    obj = os.path.join(out, "obj")
    os.makedirs(obj, exist_ok=True)
    inc = ["-Isrc", "-Isrc/phys", "-Iplatform/sdl2", "-Ithird_party/stb", "-Ithird_party/zlib", "-Ithird_party/khronos"]
    flags = [opt, "-std=gnu99", "-Wall", "-Wno-unused-function", "-ffp-contract=off", "-DRC_FPT=double", "-DWMMT_EGL"]
    link = []
    if win:
        sdl = sdl2_mingw()
        if not sdl:
            die("no SDL2 development files for mingw: `python build.py setup-sdl2` (or set SDL2_DIR)")
        flags += ["-D__USE_MINGW_ANSI_STDIO=1", "-DWMMT_WIN", "-DGL_APICALL=", "-D_POSIX_THREAD_SAFE_FUNCTIONS", "-D_FILE_OFFSET_BITS=64",
                  "-include", "platform/sdl2/winport.h", "-Iplatform/sdl2/winshim", "-I" + os.path.join(sdl, "include", "SDL2")]
        if os.environ.get("WMMT_NO_AUTO_ANGLE"):  # a build that keeps the driver's own OpenGL ES on AMD graphics (they start on ANGLE otherwise)
            flags.append("-DWMMT_NO_AUTO_ANGLE")
        link = ["-L" + os.path.join(sdl, "lib"), "-lmingw32", "-lSDL2main", "-lSDL2", "-lm", "-lws2_32", "-ladvapi32"]
        # what the static SDL2 itself needs from Windows (MinGW links most of these by default, Zig does not)
        link += ["-luser32", "-lgdi32", "-lwinmm", "-limm32", "-lversion", "-lsetupapi", "-lole32", "-loleaut32", "-lcfgmgr32", "-lshell32", "-luuid"]
        if tc.kind == "gcc":
            link += ["-static-libgcc", "-Wl,-Bstatic,--whole-archive", "-lwinpthread", "-Wl,--no-whole-archive,-Bdynamic"]
        exe = os.path.join(out, "super3dx.exe")
    else:
        flags += ["-fPIC"]
        if target == "pi":
            flags += ["-mcpu=cortex-a76"] if platform.machine().lower() not in ("aarch64", "arm64") or os.environ.get("SYSROOT") else []
        env = dict(os.environ)
        sysroot = os.environ.get("SYSROOT")
        if sysroot:
            flags += ["--sysroot=" + sysroot]
            env["PKG_CONFIG_SYSROOT_DIR"] = sysroot
            env["PKG_CONFIG_LIBDIR"] = "%s/usr/lib/aarch64-linux-gnu/pkgconfig:%s/usr/share/pkgconfig" % (sysroot, sysroot)
        pkg = which("pkg-config")
        sdlc = sdll = None
        if pkg:
            r = subprocess.run([pkg, "--cflags", "--libs", "sdl2"], capture_output=True, text=True, env=env)
            if r.returncode == 0:
                parts = r.stdout.split()
                sdlc = [p for p in parts if p.startswith(("-I", "-D"))]
                sdll = [p for p in parts if not p.startswith(("-I", "-D"))]
        if sdlc is None and which("sdl2-config"):
            sdlc = subprocess.run(["sdl2-config", "--cflags"], capture_output=True, text=True).stdout.split()
            sdll = subprocess.run(["sdl2-config", "--libs"], capture_output=True, text=True).stdout.split()
        if sdlc is None:
            die("SDL2 development files not found: install libsdl2-dev (and libegl1-mesa-dev, libgles2-mesa-dev), see docs/BUILDING.md")
        flags += sdlc
        link = sdll + ["-lEGL", "-lGLESv2", "-lm", "-lpthread"]
        if sysroot:
            link = ["--sysroot=" + sysroot] + link  # (the link too: libm.so's script names /lib/... inside the sysroot)
        exe = os.path.join(out, "super3dx")
    cflags = flags + inc
    key = " ".join(tc.cc + cflags)
    kf = os.path.join(out, "cflags.txt")
    if not os.path.exists(kf) or open(kf).read() != key:
        for o in glob.glob(os.path.join(obj, "*.o")):
            os.remove(o)
        open(kf, "w").write(key)

    jobs_list = []  # (source, object, extra flags)
    phys = os.path.join(obj, "phys_rc.o")
    if newer(os.path.join(ROOT, "src/phys/phys_rc.c"), phys) or newer(os.path.join(ROOT, "src/phys/rc_runtime.h"), phys):
        say("compiling the arcade physics (a minute or two)...")
        jobs_list.append(("src/phys/phys_rc.c", phys, ["-w"]))
    objs = [phys]
    hdrs = glob.glob(os.path.join(ROOT, "src", "*.h")) + glob.glob(os.path.join(ROOT, "platform", "sdl2", "*.h")) + glob.glob(os.path.join(ROOT, "src", "phys", "*.h"))
    newest_hdr = max(os.path.getmtime(h) for h in hdrs)
    zl = sorted(glob.glob(os.path.join(ROOT, "third_party", "zlib", "*.c")))
    for s in SRCS + [os.path.relpath(z, ROOT).replace(os.sep, "/") for z in zl]:
        o = os.path.join(obj, s.replace("/", "_") + ".o")
        objs.append(o)
        stale = not os.path.exists(o) or newer(os.path.join(ROOT, s), o) or os.path.getmtime(o) < newest_hdr
        if stale:
            jobs_list.append((s, o, ["-w"] if s.startswith("third_party") else []))

    if win:  # GL: every gl* function the game calls, as a jump through a pointer filled at start (gl_load, after the context)
        names = gl_names()
        tramp, load = os.path.join(out, "gl_tramp.S"), os.path.join(out, "gl_load.c")
        with open(tramp, "w", newline="\n") as f:
            f.write("/* generated by build.py: GL entry points as trampolines */\n.text\n")
            for n in names:
                f.write(".globl %s\n%s:\n\tjmp *p_%s(%%rip)\n" % (n, n, n))
            f.write(".bss\n")
            for n in names:
                f.write(".globl p_%s\np_%s: .quad 0\n" % (n, n))
        with open(load, "w", newline="\n") as f:
            f.write("/* generated by build.py */\n#include <SDL.h>\n")
            for n in names:
                f.write("extern void *p_%s;\n" % n)
            f.write("int gl_load(void) {\n  int missing = 0;\n")
            for n in names:
                f.write('  if (!(p_%s = SDL_GL_GetProcAddress("%s"))) { missing++; if (missing < 6) SDL_Log("GL: no %s"); }\n' % (n, n, n))
            f.write("  return missing;\n}\n")
        jobs_list.append((tramp, os.path.join(obj, "gl_tramp.o"), []))
        jobs_list.append((load, os.path.join(obj, "gl_load.o"), []))
        objs += [os.path.join(obj, "gl_tramp.o"), os.path.join(obj, "gl_load.o")]

    def compile_one(job):
        s, o, extra = job
        src = s if os.path.isabs(s) else os.path.join(ROOT, s)
        cmd = tc.cc + ["-c", src, "-o", o] if src.endswith(".S") else tc.cc + cflags + extra + ["-c", src, "-o", o]  # (an assembly file takes none of the C flags)
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        return s, r

    say("%s: %d files to compile with %s" % (target, len(jobs_list), " ".join(tc.cc)))
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        failed = False
        for s, r in ex.map(compile_one, jobs_list):
            if r.returncode:
                failed = True
                sys.stderr.write("--- %s\n%s%s\n" % (s, r.stdout, r.stderr))
            elif r.stderr.strip() and "-Wall" in " ".join(cflags):
                pass
        if failed:
            die("compile errors above")
    cmd = tc.cc + [o for o in objs] + ["-o", exe] + link
    run(cmd, cwd=ROOT)
    if win:
        shutil.copyfile(os.path.join(sdl2_mingw(), "bin", "SDL2.dll"), os.path.join(out, "SDL2.dll"))
        for f in ANGLE_DLLS:  # the OpenGL ES fallback for PCs whose driver has none (third_party/angle)
            shutil.copyfile(os.path.join(ROOT, "third_party", "angle", f), os.path.join(out, f))
    say("built", exe)
    return exe


# -------------------------------------------------------------------------------------------------------- package
def package(target):
    out = os.path.join(ROOT, "build", target)
    exe = os.path.join(out, "super3dx.exe" if target == "windows" else "super3dx")
    if not os.path.isfile(exe):
        die("no player: python build.py exe --target %s" % target)
    data = os.path.join(ROOT, "data")
    if not os.path.isdir(data):
        die("no data/: python build.py data <arcade root>")
    pk = os.path.join(out, "package")
    shutil.rmtree(pk, ignore_errors=True)
    os.makedirs(pk)
    shutil.copy(exe, pk)
    if target == "windows":
        shutil.copy(os.path.join(out, "SDL2.dll"), pk)
        for f in ANGLE_DLLS:
            shutil.copy(os.path.join(out, f), pk)
        open(os.path.join(pk, "play.bat"), "w", newline="\r\n").write(
            '@echo off\nrem Optional: super3dx.exe can be double-clicked as it is. This one adds options, e.g. play.bat --size 1600x1200 (the layout is 4:3); saves go to the save folder.\n'
            'cd /d "%~dp0"\nif not exist save mkdir save\nsuper3dx.exe "%~dp0data" --save "%~dp0save" %*\n')
    else:
        p = os.path.join(pk, "play.sh")
        open(p, "w", newline="\n").write('#!/bin/sh\ncd "$(dirname "$0")"\nmkdir -p save\nexec ./super3dx data --save save "$@"\n')
        os.chmod(p, 0o755)
    say("copying data/ (it is large)...")
    shutil.copytree(data, os.path.join(pk, "data"))
    open(os.path.join(pk, "README.txt"), "w").write(
        "Super3DX+ (see the project's README): the player and the game's data, converted from your own copy of the arcade game.\n"
        "For personal use: the data is the arcade's, do not share it.\n")
    say("package:", pk, "(zip this folder to move it)")


# --------------------------------------------------------------------------------------------------------- doctor
def doctor():
    ok = True

    def line(name, found, hint):
        nonlocal ok
        say("  %-26s %s" % (name, found if found else "MISSING - " + hint))
        ok &= bool(found)

    say("python:", sys.version.split()[0], sys.executable)
    for m in ("capstone", "unicorn", "PIL", "numpy"):
        try:
            __import__(m)
            line("python module " + m, "ok", "")
        except ImportError:
            line("python module " + m, None, "pip install -r tools/requirements.txt")
    line("C++ demangler", which("c++filt", "llvm-cxxfilt") or ("built with Zig or a C++ compiler" if (find_zig() or which("g++", "clang++")) else None), "python build.py setup-zig (Windows) or apt install binutils")
    line("ffmpeg (for the sound/movies)", os.environ.get("FFMPEG") or which("ffmpeg"), "a full build with the Bink decoder: winget install Gyan.FFmpeg, apt install ffmpeg")
    say("compilers:")
    line("zig", find_zig(), "python build.py setup-zig")
    line("mingw-w64 gcc", which("x86_64-w64-mingw32-gcc-posix", "x86_64-w64-mingw32-gcc"), "MSYS2 (pacman -S mingw-w64-x86_64-gcc) or apt install gcc-mingw-w64-x86-64-posix")
    line("gcc / clang", which("gcc", "clang", "cc"), "apt install build-essential")
    line("SDL2 for mingw", sdl2_mingw(), "python build.py setup-sdl2")
    line("SDL2 (Linux dev files)", which("sdl2-config") or (which("pkg-config") and subprocess.run([which("pkg-config"), "--exists", "sdl2"]).returncode == 0 and "pkg-config"), "apt install libsdl2-dev libegl1-mesa-dev libgles2-mesa-dev")
    say("Windows needs one of (zig, mingw-w64) plus SDL2 for mingw; Linux needs gcc/clang plus the SDL2 dev files. The rest is for the data.")


def _has(mod):
    try:
        __import__(mod)
        return True
    except ImportError:
        return False


def pipeline(*args):
    run([sys.executable, os.path.join(ROOT, "tools", "pipeline.py")] + list(args), cwd=ROOT)


# -------------------------------------------------------------------------------------------------------- wizard
# The guided path for people who just want the game: `python build.py` (or double-click setup.bat / run ./setup.sh).
# It installs what it can by itself, asks where your copy of the game is, checks it, builds, and says where the result is.
KNOWN = {"22b9bbcc5a0cefd42a4e83b2dd892c98": "Japan", "aac96862372ee774a14096792fc40c13": "Export"}
REQ_MODULES = ["capstone", "unicorn", "PIL", "numpy"]


def ensure_modules():
    """the Python packages the converters use; installs them, in a private virtual environment if the system Python refuses"""
    missing = [m for m in REQ_MODULES if not _has(m)]
    if not missing:
        say("Python packages: all there.")
        return
    req = os.path.join(ROOT, "tools", "requirements.txt")
    say("Installing the Python packages the converters need (%s)..." % ", ".join(missing))
    if subprocess.run([sys.executable, "-m", "pip", "install", "-r", req]).returncode == 0:
        import importlib
        importlib.invalidate_caches()
        return
    if sys.prefix != getattr(sys, "base_prefix", sys.prefix):
        die("could not install the Python packages (pip failed, see above)")
    say("The system Python would not take them; making a private environment in .super3dx/venv ...")
    venv = os.path.join(TOOLS, "venv")
    if subprocess.run([sys.executable, "-m", "venv", venv]).returncode:
        die("could not make a virtual environment. On Debian/Ubuntu run: sudo apt install python3-venv python3-pip")
    # (the python.org Python's venv has Scripts\python.exe; MSYS2's, on Windows too, has bin/python.exe or bin/python3: take whichever is there)
    cands = [os.path.join(venv, d, n) for d in ("Scripts", "bin") for n in ("python.exe", "python3.exe", "python", "python3")]
    py = next((c for c in cands if os.path.isfile(c)), None)
    if not py:
        die("the virtual environment was made but has no Python in it (looked in %s). Install Python from python.org and run setup again, "
            "or run: %s -m pip install --break-system-packages -r %s" % (venv, sys.executable, req))
    if subprocess.run([py, "-m", "pip", "install", "-r", req]).returncode:
        die("could not install the Python packages (pip failed, see above)")
    sys.exit(subprocess.run([py] + sys.argv).returncode)  # carry on inside it


def ffmpeg_problem(path):
    """None if this ffmpeg runs and has the Bink video decoder the arcade's movies need, else what is wrong with it"""
    try:
        r = subprocess.run([path, "-hide_banner", "-decoders"], capture_output=True, text=True, timeout=60)
    except FileNotFoundError:
        return "the file does not exist or cannot be started"
    except (OSError, subprocess.SubprocessError) as e:
        return "it did not run (%s)" % e
    if r.returncode:
        return "it ran but failed (exit code %d)" % r.returncode
    if "bink" not in r.stdout.lower():
        return "it has no Bink decoder (a 'full' build is needed, not an 'essentials' one)"
    return None


def find_ffmpeg():
    """the first ffmpeg that works: FFMPEG, the PATH, the setup's own copy, a winget install (not on this process's PATH yet if it was made in this session)"""
    cands = [os.environ.get("FFMPEG"), which("ffmpeg")]
    exe = "ffmpeg.exe" if os.name == "nt" else "ffmpeg"
    cands += glob.glob(os.path.join(TOOLS, "ffmpeg", "**", exe), recursive=True)
    if os.name == "nt":
        base = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet")
        cands.append(os.path.join(base, "Links", "ffmpeg.exe"))
        cands += glob.glob(os.path.join(base, "Packages", "Gyan.FFmpeg*", "**", "bin", "ffmpeg.exe"), recursive=True)
    for c in cands:
        if not c:
            continue
        why = ffmpeg_problem(c)
        if not why:
            return c
        say("ffmpeg at %s will not do: %s" % (c, why))
    return None


# a pinned 'full' build (it has the Bink decoder), Gyan's own GitHub release: its SHA-256 as GitHub lists it for the asset
FFMPEG_ZIP = ("https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-full_build.zip",
              "759d0a9831c436a0eb331ad36f236c06cb04aaa0005da46571f0e9d3d9206f6b")


def setup_ffmpeg():
    """a portable ffmpeg (Windows), kept in .super3dx/ffmpeg: the pinned download, its SHA-256 checked. None if it could not be had"""
    if os.name != "nt":
        return None
    url, sha = FFMPEG_ZIP
    dest = os.path.join(TOOLS, os.path.basename(url))
    out = os.path.join(TOOLS, "ffmpeg")
    try:
        download(url, dest, sha)  # (stops the setup with its own message when it cannot)
    except SystemExit as e:
        say(str(e))
        return None
    try:
        shutil.rmtree(out, ignore_errors=True)
        zipfile.ZipFile(dest).extractall(out)
    except (zipfile.BadZipFile, OSError) as e:
        say("could not unpack it (%s)" % e)
        return None
    finally:
        try:
            os.remove(dest)
        except OSError:
            pass
    return find_ffmpeg()


def preflight():
    """problems that would only show up an hour into the build; returns a message, or None if all is well"""
    if sys.version_info < (3, 9):
        return "Python %d.%d is too old: Super3DX+ needs Python 3.9 or newer (https://www.python.org/downloads/)." % sys.version_info[:2]
    try:
        os.makedirs(TOOLS, exist_ok=True)
        probe = os.path.join(TOOLS, ".write-test")
        open(probe, "w").write("x")
        os.remove(probe)
    except OSError as e:
        return ("I cannot write to the project folder (%s).\n  Move the folder somewhere you can write, such as C:\\Super3DXPlus or your home folder, "
                "not Program Files or a read-only location, and run setup again.\n  (%s)" % (ROOT, e))
    if os.name == "nt" and len(ROOT) > 70:
        return ("WARN:This folder's path is long (%d characters) and Windows can fail on very long paths during the build.\n  Moving the project folder "
                "closer to the drive's root, such as C:\\Super3DXPlus, avoids that." % len(ROOT))
    try:
        ROOT.encode("ascii")
    except UnicodeEncodeError:
        return ("WARN:This folder's path has characters other than plain English letters and digits, which some of the build tools may not handle.\n"
                "  Moving the project folder to a simple path, such as C:\\Super3DXPlus, avoids that.")
    return None

SKIP_HASH = ("tools/dev/", "tools/test/", "tools/pi/", "tools/setupkit.py", "tools/setup_", "tools/headline/dev/", "tools/headline/README.md", "tools/demangle.cpp")


def _generated_in_tools(rel):
    """files the generators write into tools/ (they are git-ignored): not part of what makes the data"""
    d, f = os.path.split(rel)
    if d == "tools":
        return (f.endswith(".json") and f != "jpids.json") or f.endswith("_texts.txt") or f == "uvtab.txt"
    return rel == "tools/stage/barricades.txt" or rel.startswith("tools/vsdisc/")


def tools_hash():
    """a fingerprint of everything that decides what the converted data and the generated sources look like: the converters and generators
    (not the setup helpers, tests and Pi scripts). If it is unchanged since a build, that build's data is still valid."""
    h = hashlib.sha256()
    base = os.path.join(ROOT, "tools")
    for dp, dn, fn in os.walk(base):
        dn[:] = sorted(d for d in dn if d != "__pycache__")
        for f in sorted(fn):
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
            if f.endswith(".pyc") or rel.startswith(SKIP_HASH) or _generated_in_tools(rel):
                continue
            h.update(rel.encode())
            h.update(open(full, "rb").read().replace(b"\r\n", b"\n"))
    return h.hexdigest()


def read_info(folder):
    try:
        import json
        return json.load(open(os.path.join(folder, "build-info.json"), encoding="utf-8"))
    except (OSError, ValueError):
        return None


def write_info(folder, md5, region, game=None):
    import json, time
    commit = None
    if which("git") and os.path.isdir(os.path.join(ROOT, ".git")):
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        commit = r.stdout.strip() or None
    json.dump({"tools": tools_hash(), "main_md5": md5, "region": region, "built": time.strftime("%Y-%m-%d %H:%M"), "source_commit": commit, "game_dir": game},
              open(os.path.join(folder, "build-info.json"), "w", encoding="utf-8"), indent=1)


def _get_json(url):
    """the JSON at a URL (the GitHub API), or None; Python's own HTTPS first, then the system's downloader (see _fetch_with_system_tool)"""
    import json, tempfile
    req = urllib.request.Request(url, headers={"User-Agent": "Super3DXPlus-setup", "Accept": "application/vnd.github+json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read().decode("utf-8"))
    except (OSError, ValueError):
        pass
    part = os.path.join(tempfile.gettempdir(), "super3dx-api.json")
    try:
        if _fetch_with_system_tool(url, part):
            return json.load(open(part, encoding="utf-8"))
    except (OSError, ValueError):
        pass
    return None


def _soft_download(url, dest):
    """a download that reports failure instead of stopping the setup"""
    say("downloading", url)
    part = dest + ".part"
    try:
        with urllib.request.urlopen(url, timeout=120) as r, open(part, "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(part, dest)
        return True
    except OSError:
        pass
    if _fetch_with_system_tool(url, part):
        os.replace(part, dest)
        return True
    return False


def self_update():
    """look for a newer version on GitHub and offer to install it (tools/updater.py); if the setup's own files changed, run the new ones"""
    import updater
    mine = ["build.py", "tools/setupkit.py", "tools/updater.py"]
    before = {m: open(os.path.join(ROOT, m), "rb").read() for m in mine}
    r = updater.check(ROOT, say, yes, which, _get_json, _soft_download)
    if r == "updated" and updater.self_changed(ROOT, before):
        say("\nThe setup itself was updated: starting the new one.\n")
        env = dict(os.environ, SUPER3DX_NO_UPDATE="1")  # (no second check)
        sys.exit(subprocess.call([sys.executable, os.path.abspath(__file__)] + sys.argv[1:], env=env))
    return r


def last_game():
    """the game folder an earlier build here was made from (build-info.json), if it is still there: the newest of them"""
    found = []
    for f in glob.glob(os.path.join(ROOT, "Super3DXPlus-*", "build-info.json")):
        info = read_info(os.path.dirname(f))
        g = info and info.get("game_dir")
        if g and os.path.isdir(g) and find_game(g):
            found.append((info.get("built", ""), g))
    return [g for _, g in sorted(found, reverse=True)]


def refresh(game, target, opt, jobs):
    """the quick update: the generated sources and the player, no data conversion"""
    pipeline("gen", game, "--phys")
    build_exe(target, None, opt, jobs)


def wizard(folder, opt, jobs, full=False):
    target = "windows" if os.name == "nt" else "linux"
    total = 4
    for stream in (sys.stdout, sys.stderr):  # a Windows console that cannot show a character must not stop the build
        try:
            stream.reconfigure(errors="replace")
        except Exception:
            pass
    self_update()
    problem = preflight()
    if problem and problem.startswith("WARN:"):
        say("\nNote: " + problem[5:])
        if not yes("Carry on anyway?", False):
            return 1
    elif problem:
        say("\nCannot start: " + problem)
        return 1
    say("")
    say("Super3DX+ setup")
    say("This builds the game from YOUR OWN copy of Wangan Midnight Maximum Tune 3DX+. It needs an internet connection for a few small")
    say("downloads, about 15 GB of free disk space, and takes an hour or more. You can leave it alone while it works.")
    free = shutil.disk_usage(ROOT).free / 2**30
    if free < 15:
        say("\nWARNING: only %.0f GB free on this drive; the build needs about 15 GB." % free)
        if not yes("Carry on anyway?", False):
            return 1

    # 1 ------------------------------------------------------------------------------------------------------------
    step(1, total, "get the tools (automatic)")
    ensure_modules()
    if target == "windows":
        if not (find_zig() or which("x86_64-w64-mingw32-gcc-posix", "x86_64-w64-mingw32-gcc")):
            setup_zig()
        else:
            say("C compiler: found.")
        if not sdl2_mingw():
            setup_sdl2()
        else:
            say("SDL2: found.")
    else:
        missing = []
        if not which("gcc", "clang", "cc"):
            missing.append("a C compiler")
        if not (which("sdl2-config") or (which("pkg-config") and subprocess.run([which("pkg-config"), "--exists", "sdl2"]).returncode == 0)):
            missing.append("the SDL2 development files")
        if missing:
            say("\nMissing: " + " and ".join(missing) + ". On Ubuntu/Debian/Raspberry Pi OS, install them with:\n")
            say("    sudo apt install build-essential libsdl2-dev libegl1-mesa-dev libgles2-mesa-dev ffmpeg\n")
            say("then run this again.")
            return 1
    ff = find_ffmpeg()
    if not ff and os.name == "nt":
        say("\nffmpeg (it converts the game's sound and movies) is not installed, or the one on this PC will not do.")
        if which("winget") and yes("Install it now with winget? (a normal Windows installer; it may ask for permission)"):
            subprocess.run(["winget", "install", "--id", "Gyan.FFmpeg", "-e", "--accept-package-agreements", "--accept-source-agreements"])
            ff = find_ffmpeg()
        if not ff and yes("Download a portable copy for the setup to use instead? (about 200 MB, kept in the setup's own folder, nothing is installed)"):
            ff = setup_ffmpeg()
    if not ff:
        say("\nffmpeg is missing. Install it (Windows: winget install Gyan.FFmpeg, or https://www.gyan.dev/ffmpeg/builds/ - the 'full' build;")
        say("Linux: sudo apt install ffmpeg), then run this again.")
        return 1
    os.environ["FFMPEG"] = ff
    say("ffmpeg: " + ff)

    # 2 ------------------------------------------------------------------------------------------------------------
    step(2, total, "find your copy of the game")
    say("I need the folder that holds your copy of the game: it has a file called `main` and a folder called `data` inside.")
    game = find_game(folder) if folder else None
    if not game and not folder:
        for g in last_game()[:1]:  # (asked once: the same copy of the game as the last build)
            say("Your last build here used this game folder:\n    " + g)
            if yes("Use it again?"):
                game = find_game(g)
    while not game:
        if folder:
            say("\nNo `main` file and `data` folder found in: %s" % folder)
        say("\nPress Enter to browse for the folder, or paste / drag the folder into this window and press Enter (q to quit).")
        a = ask("> ")
        if a.lower() == "q":
            return 1
        folder = a or browse_folder()
        if not folder:
            continue
        game = find_game(folder)
    md5 = hashlib.md5(open(os.path.join(game, "main"), "rb").read()).hexdigest()
    region = KNOWN.get(md5)
    say("\nGame folder: " + game)
    if region:
        say("Recognised: the %s version of 3DX+." % region)
    else:
        say("This `main` is not one of the versions this project was tested with (docs/BUILDS.md lists them).")
        say("It may still work, but expect problems.")
        if not yes("Carry on anyway?", False):
            return 1
        region = "Custom"

    # 3 ------------------------------------------------------------------------------------------------------------
    import time
    dest = os.path.join(ROOT, "Super3DXPlus-" + region)
    info = read_info(dest)
    exe_name = "super3dx.exe" if target == "windows" else "super3dx"
    can_update = (not full and info and info.get("main_md5") == md5 and info.get("tools") == tools_hash()
                  and os.path.isfile(os.path.join(dest, exe_name)) and os.path.isdir(os.path.join(dest, "data")))
    if can_update:
        say("\nFound your earlier build: " + dest)
        say("Built %s%s. The converted data is still valid (nothing that makes it has changed), so only the player needs rebuilding:" %
            (info.get("built", "earlier"), (" from commit " + info["source_commit"]) if info.get("source_commit") else ""))
        say("a few minutes, and your saves and data are not touched.")
        if not yes("Update it now?"):
            return 1
    else:
        if os.path.isdir(dest):
            say("\nFound an earlier build in " + dest)
            say("It cannot be updated in place (%s), so the data must be converted again (about an hour)." %
                ("you asked for a full rebuild" if full else "the data tools changed since, or it was built by an older setup"))
            say("Your saves in its `save` folder are kept: they are copied into the new build.")
        if not yes("\nReady. Start the build now? (about an hour; nothing to click once it starts)"):
            return 1
    step(3, total, "build the player" if can_update else "convert the data and build the game (about an hour; nothing to click)")
    t0 = time.time()
    log = os.path.join(ROOT, "build", "logs", "setup-%s.txt" % time.strftime("%Y%m%d-%H%M%S"))
    say("(everything below is also saved in %s)\n" % os.path.relpath(log, ROOT))
    cmd = [sys.executable, os.path.abspath(__file__), "refresh" if can_update else "all", game, "--target", target, "-O" + opt, "-j", str(jobs)]
    try:
        rc = run_logged(cmd, log)
    except KeyboardInterrupt:
        say("\nStopped. Nothing is broken: run setup again whenever you like.")
        return 1
    if rc:
        explain_failure(log, rc)
        return 1

    # 4 ------------------------------------------------------------------------------------------------------------
    step(4, total, "done")
    built = os.path.join(ROOT, "build", target)
    if can_update:
        try:
            for f in ([exe_name, "SDL2.dll"] + ANGLE_DLLS if target == "windows" else [exe_name]):
                shutil.copy2(os.path.join(built, f), os.path.join(dest, f))
        except PermissionError:
            say("\nThe game could not be updated because a file is in use: close the game (and any window showing that folder) and run setup again.")
            return 1
        write_info(dest, md5, region, game)
        say("Updated in %d minutes." % ((time.time() - t0) // 60))
    else:
        pk = os.path.join(built, "package")
        old = None
        if os.path.isdir(dest):  # replace the earlier build, keeping its saves
            old = dest + ".old"
            shutil.rmtree(old, ignore_errors=True)
            if os.path.isdir(os.path.join(dest, "save")):
                shutil.copytree(os.path.join(dest, "save"), os.path.join(pk, "save"), dirs_exist_ok=True)
            os.replace(dest, old)
        os.replace(pk, dest)
        write_info(dest, md5, region, game)
        if old:
            shutil.rmtree(old, ignore_errors=True)
        say("Built in %d minutes." % ((time.time() - t0) // 60))
    exe = os.path.join(dest, "super3dx.exe" if target == "windows" else "play.sh")
    say("\nYour game is in this folder:\n\n    " + dest + "\n")
    say("To play: open that folder and %s. Your saves are kept in its `save` folder." % ("double-click super3dx.exe" if target == "windows" else "run play.sh"))
    say("To move the game to another PC, copy or zip that whole folder. Do not share it: it contains the game's data.")
    say("To build the other version (Japan / Export), run this again and choose that game folder.")
    say("After a new version of Super3DX+ comes out, run this again: it only redoes what changed.")
    if yes("\nStart the game now?"):
        if target == "windows":
            os.startfile(exe)
        else:
            subprocess.Popen([exe])
    return 0


def _guided(which_one, jobs):
    """the Raspberry Pi / Android setups (tools/setup_pi.py, tools/setup_android.py): run after the main setup"""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except Exception:
            pass
    try:
        ensure_modules()
        mod = __import__("setup_pi" if which_one == "pi" else "setup_android")
        sys.exit(mod.run(os.path.abspath(__file__), jobs))
    except KeyboardInterrupt:
        sys.exit("\nStopped. Run it again whenever you like.")
    except SystemExit as e:
        if isinstance(e.code, str):
            say("\n" + "!" * 78 + "\n" + e.code.replace("build.py: ", "", 1) + "\n" + "!" * 78)
            sys.exit(1)
        raise
    except Exception:
        import traceback
        err = os.path.join(ROOT, "build", "logs", "setup-error.txt")
        os.makedirs(os.path.dirname(err), exist_ok=True)
        open(err, "w", encoding="utf-8").write(traceback.format_exc())
        say("\n" + "!" * 78 + "\nThe setup hit an unexpected problem (it is a bug in the setup, not your fault).\n"
            "Please open an issue at %s and attach this file:\n\n    %s\n" % (ISSUES, err) + "!" * 78)
        sys.exit(1)


def msys2_bash():
    """Find devkitPro's MSYS2 bash on Windows, even if it is not on PATH."""
    roots = [os.environ.get("DEVKITPRO"), r"C:\devkitPro", r"C:\devkitpro"]
    for root in roots:
        if not root:
            continue
        bash = os.path.join(root, "msys2", "usr", "bin", "bash.exe")
        if os.path.isfile(bash):
            return bash
    return None


def msys2_path(path):
    """C:\\Super3DXPlus -> /c/Super3DXPlus for an MSYS2 shell command line."""
    p = os.path.abspath(path).replace("\\", "/")
    if len(p) >= 2 and p[1] == ":":
        p = "/" + p[0].lower() + p[2:]
    return p


def switch_build():
    """Build the Switch NRO with devkitPro's make (see docs/SWITCH.md)."""
    for f in GENERATED:
        if not os.path.isfile(os.path.join(ROOT, f)):
            die("missing %s: run `python build.py data <arcade root>` first" % f)

    bash = msys2_bash() if os.name == "nt" else None
    if bash:
        # devkit-env.sh inside this shell sets DEVKITPRO and puts the
        # devkitA64 tools on PATH, so no Windows-side env vars are needed.
        project = msys2_path(ROOT)
        run([bash, "--login", "-c", 'cd "%s" && make -C platform/switch' % project], cwd=ROOT)
    else:
        if not which("make"):
            die("make not found: use devkitPro's MSYS2 shell, or build on Linux/macOS (see docs/SWITCH.md)")
        if not os.environ.get("DEVKITPRO"):
            die("DEVKITPRO is not set: install devkitPro, devkitA64, libnx, switch-sdl2, switch-mesa and switch-zlib (see docs/SWITCH.md)")
        run(["make", "-C", os.path.join(ROOT, "platform", "switch")], cwd=ROOT)
    say("Switch build: platform/switch/super3dx+nx.nro")


def main():
    os.environ["PYTHONUTF8"] = "1"  # every Python tool this starts reads and writes UTF-8 (the Windows default, cp1252, cannot hold the Japanese text)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", default="wizard", choices=["wizard", "update", "pi", "android", "switch", "gensrc", "refresh", "doctor", "setup-zig", "setup-sdl2", "data", "exe", "package", "all"])
    ap.add_argument("arcade", nargs="?", help="the arcade dump: a folder with main and data/ (for data, all and wizard)")
    ap.add_argument("--target", choices=["windows", "linux", "pi"], default="windows" if os.name == "nt" else "linux")
    ap.add_argument("--cc", help="the C compiler command to use (e.g. 'zig cc -target x86_64-windows-gnu')")
    ap.add_argument("-O", dest="opt", default="-O2", help="the optimisation flag (-O1 compiles quicker)")
    ap.add_argument("-j", dest="jobs", type=int, default=os.cpu_count() or 2, help="compiler processes at once")
    ap.add_argument("--no-media", action="store_true", help="data: skip the sounds, music and movies")
    ap.add_argument("--full", action="store_true", help="wizard: rebuild everything even if the earlier build is up to date")
    a = ap.parse_args()
    if a.cmd == "wizard":
        try:
            sys.exit(wizard(a.arcade, a.opt, a.jobs, a.full))
        except KeyboardInterrupt:
            sys.exit("\nStopped. Run setup again whenever you like.")
        except SystemExit as e:  # a plain-language message from a helper (a failed download, a missing tool...)
            if isinstance(e.code, str):
                say("\n" + "!" * 78 + "\n" + e.code.replace("build.py: ", "", 1) + "\n" + "!" * 78)
                sys.exit(1)
            raise
        except Exception:
            import traceback
            err = os.path.join(ROOT, "build", "logs", "setup-error.txt")
            os.makedirs(os.path.dirname(err), exist_ok=True)
            open(err, "w", encoding="utf-8").write(traceback.format_exc())
            say("\n" + "!" * 78 + "\nThe setup hit an unexpected problem (it is a bug in the setup, not your fault).\n"
                "Please open an issue at %s and attach this file:\n\n    %s\n" % (ISSUES, err) + "!" * 78)
            sys.exit(1)
    if a.cmd == "update":
        sys.exit(0 if self_update() != "failed" else 1)
    if a.cmd == "gensrc":
        if not a.arcade:
            die("gensrc needs the arcade dump folder")
        pipeline("gen", a.arcade, "--phys")
        return
    if a.cmd in ("pi", "android"):
        _guided(a.cmd, a.jobs)
        return
    if a.cmd == "switch":
        switch_build()
        return
    if a.cmd == "refresh":
        if not a.arcade:
            die("refresh needs the arcade dump folder")
        refresh(a.arcade, a.target, a.opt, a.jobs)
        return
    if a.cmd == "doctor":
        doctor()
    elif a.cmd == "setup-zig":
        setup_zig()
    elif a.cmd == "setup-sdl2":
        setup_sdl2()
    elif a.cmd in ("data", "all"):
        if not a.arcade:
            die("%s needs the arcade dump folder" % a.cmd)
        pipeline("gen", a.arcade, "--phys")
        pipeline("convert", a.arcade, *(["--no-media"] if a.no_media else []))
    if a.cmd in ("exe", "all"):
        build_exe(a.target, a.cc, a.opt, a.jobs)
    if a.cmd in ("package", "all"):
        package(a.target)


if __name__ == "__main__":
    main()
