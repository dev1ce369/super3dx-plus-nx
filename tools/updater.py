"""The setup's update check: is there a newer Super3DX+ on GitHub, and if so, bring this folder up to date (build.py calls it at the start of the guided setup).

A git checkout is updated with git (fetch, then a fast-forward pull). A folder from a downloaded zip has no git: the latest sources are downloaded as a zip, each file is compared
with the one here, and the files that differ are replaced (the old ones are kept in build/update-backup/<time>/, so a change of your own is not lost). Only the program's own files
are touched: never data/, build/, the tools downloaded into .super3dx/, a built game folder or a save. What the build does after (the quick player rebuild, or converting the data again)
is the setup's own decision, from what changed.

Testing: SUPER3DX_UPDATE_FROM=<a git repo folder> takes the "latest" from that repo's HEAD instead of GitHub; SUPER3DX_NO_UPDATE=1 skips the check.
"""
import json
import os
import shutil
import subprocess
import tempfile
import time
import zipfile

REPO = "smolderyguest/Super3DXPlus"
BRANCH = "main"
KEEP = ("data/", "build/", ".super3dx/", ".git/")  # never replaced or removed by an update (the zip has none of them, but a rule is safer than a hope)
SELF_SAFE_MARK = b"updater-safe"  # setup.bat with this in it can be replaced while it runs (its tail is one block that exits); an older one is saved as setup.bat.new instead


def _git(root, *args):
    r = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def is_git(root, which):
    return bool(which("git")) and os.path.isdir(os.path.join(root, ".git"))


def _state_path(root):
    return os.path.join(root, ".super3dx", "update.json")


def _read_state(root):
    try:
        return json.load(open(_state_path(root), encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _write_state(root, sha, subject):
    os.makedirs(os.path.dirname(_state_path(root)), exist_ok=True)
    json.dump({"commit": sha, "subject": subject, "when": time.strftime("%Y-%m-%d %H:%M")}, open(_state_path(root), "w", encoding="utf-8"), indent=1)


def _latest(get_json):
    """(sha, subject, date) of the newest commit, or None if it cannot be found out"""
    src = os.environ.get("SUPER3DX_UPDATE_FROM")
    if src:
        r = subprocess.run(["git", "log", "-1", "--format=%H%n%s%n%cs", "HEAD"], cwd=src, capture_output=True, text=True)
        p = r.stdout.strip().split("\n")
        return (p[0], p[1], p[2]) if r.returncode == 0 and len(p) >= 3 else None
    d = get_json("https://api.github.com/repos/%s/commits/%s" % (REPO, BRANCH))
    try:
        return d["sha"], d["commit"]["message"].split("\n")[0], d["commit"]["committer"]["date"][:10]
    except (TypeError, KeyError):
        return None


def _same(a, b):
    return a == b or a.replace(b"\r\n", b"\n") == b.replace(b"\r\n", b"\n")


def _safe_rel(name):
    """the path inside the repo (the zip's top folder cut off), or None for anything that must not be written"""
    parts = name.replace("\\", "/").split("/")[1:]
    if not parts or not parts[-1] or any(p in ("", ".", "..") or ":" in p for p in parts):
        return None
    rel = "/".join(parts)
    return None if rel.startswith(KEEP) else rel


def _get_zip(sha, download, tmp):
    src = os.environ.get("SUPER3DX_UPDATE_FROM")
    out = os.path.join(tmp, "latest.zip")
    if src:
        r = subprocess.run(["git", "archive", "--format=zip", "--prefix=repo/", "HEAD", "-o", out], cwd=src, capture_output=True, text=True)
        return out if r.returncode == 0 else None
    return out if download("https://codeload.github.com/%s/zip/%s" % (REPO, sha), out) else None


def _changes(root, z):
    """(files to write: rel -> bytes, how many are new) comparing the zip with this folder"""
    todo, new = {}, 0
    for info in z.infolist():
        rel = _safe_rel(info.filename)
        if rel is None or info.is_dir():
            continue
        data = z.read(info)
        p = os.path.join(root, *rel.split("/"))
        if os.path.isfile(p):
            if _same(open(p, "rb").read(), data):
                continue
        else:
            new += 1
        todo[rel] = (data, (info.external_attr >> 16) & 0o777)
    return todo, new


def _apply(root, todo):
    """write the files, the old ones saved first; returns the backup folder (or None) and the files held back"""
    bak = os.path.join(root, "build", "update-backup", time.strftime("%Y%m%d-%H%M%S"))
    held, saved = [], False
    for rel, (data, mode) in sorted(todo.items()):
        p = os.path.join(root, *rel.split("/"))
        if os.path.isfile(p):
            if rel in ("setup.bat",) and SELF_SAFE_MARK not in open(p, "rb").read():
                open(p + ".new", "wb").write(data)  # (a batch file that is running must not change under cmd: the new one waits beside it)
                held.append(rel)
                continue
            b = os.path.join(bak, *rel.split("/"))
            os.makedirs(os.path.dirname(b), exist_ok=True)
            shutil.copy2(p, b)
            saved = True
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
        if mode & 0o100:
            try:
                os.chmod(p, 0o755)
            except OSError:
                pass
    return (bak if saved else None), held


def check(root, say, yes, which, get_json, download):
    """Look for a newer version and offer to install it. Returns "none" (nothing happened), "updated" (sources replaced: the caller restarts itself if
    the setup's own files changed) or "failed". Never raises for a network problem."""
    if os.environ.get("SUPER3DX_NO_UPDATE"):
        return "none"
    try:
        latest = _latest(get_json)
    except Exception:  # (a network error of any kind must not stop the setup)
        latest = None
    if not latest:
        say("\nCould not check for a new version (no internet?). Carrying on with what is here.")
        return "none"
    sha, subject, date = latest
    if is_git(root, which) and not os.environ.get("SUPER3DX_UPDATE_FROM"):
        _git(root, "fetch", "--quiet", "origin", BRANCH)
        rc, n = _git(root, "rev-list", "--count", "HEAD..origin/" + BRANCH)
        if rc != 0 or n in ("", "0"):
            say("\nSuper3DX+ is up to date (%s)." % sha[:7])
            return "none"
        say("\nA newer Super3DX+ is available: %s commit(s) ahead, the latest \"%s\" (%s)." % (n, subject, date))
        if not yes("Download the update from GitHub now? (git pull)"):
            return "none"
        r = subprocess.run(["git", "pull", "--ff-only"], cwd=root)
        if r.returncode:
            say("\nCould not update automatically (you probably have changes of your own in this folder). Carrying on with what is here.")
            return "failed"
        return "updated"
    state = _read_state(root)
    if state.get("commit") == sha:
        say("\nSuper3DX+ is up to date (%s, %s)." % (sha[:7], state.get("when", "")))
        return "none"
    tmp = tempfile.mkdtemp(prefix="super3dx-update-")
    try:
        zp = _get_zip(sha, download, tmp)
        if not zp:
            say("\nCould not download the update. Carrying on with what is here.")
            return "failed"
        with zipfile.ZipFile(zp) as z:
            todo, new = _changes(root, z)
        if not todo:
            _write_state(root, sha, subject)
            say("\nSuper3DX+ is up to date (%s)." % sha[:7])
            return "none"
        top = sorted({r.split("/")[0] for r in todo})
        say("\nA newer Super3DX+ is available: the latest is \"%s\" (%s)." % (subject, date))
        say("%d file(s) differ from yours (%d new): %s" % (len(todo), new, ", ".join(top[:8]) + (", ..." if len(top) > 8 else "")))
        say("The files are replaced by the new ones; the old ones are kept in build/update-backup/ in case you changed any yourself. Your game, saves and data are not touched.")
        if not yes("Update now?"):
            return "none"
        bak, held = _apply(root, todo)
        _write_state(root, sha, subject)
        say("Updated %d file(s)%s." % (len(todo) - len(held), (", the old ones saved in " + os.path.relpath(bak, root)) if bak else ""))
        for h in held:
            say("%s has a new version, saved beside it as %s.new: replace it by hand when this window is closed." % (h, h))
        return "updated"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def self_changed(root, before):
    """True if a file that the running setup is made of has changed since `before` (a dict path -> bytes taken at the start)"""
    for rel, old in before.items():
        try:
            if open(os.path.join(root, rel), "rb").read() != old:
                return True
        except OSError:
            return True
    return False
