"""The Raspberry Pi setup (`python build.py pi`, or setup-pi.bat / ./setup-pi.sh): run it AFTER the main setup.

It connects to a Pi over SSH, builds the player on the Pi itself (so no cross compiler is needed, on any PC), installs the game, the
settings and a service that starts the game at boot, and (for composite video) sets up the Pi's video output. Works from Windows
(10 or later: it uses Windows' own ssh), Linux and macOS. Safe to run again: it only sends the data when it changed."""
import hashlib
import json
import os
import posixpath
import shlex
import subprocess
import sys
import tarfile
import time

from setupkit import ROOT, say, ask, yes, step, which, choose_build, run_logged, explain_failure

FILES = os.path.join(ROOT, "platform", "pi", "files")
# what the player's build needs, from the project folder (relative paths)
SRC_ITEMS = ["build.py", "src", "platform/sdl2", "third_party", "tools/setupkit.py"]
CMDLINE_VIDEO = "video=Composite-1:720x480,tv_mode=NTSC-J drm_rp1_vec.cmode=27000:721/16/64/58,480/6/6/34"


# the composite video settings of the Pi 5 (the lines docs/PI.md lists for config.txt and cmdline.txt); safe to run again, keeps a .super3dx.bak of each file
COMPOSITE_SCRIPT = r"""
set -e
CFG=/boot/firmware/config.txt; [ -f $CFG ] || CFG=/boot/config.txt
CMD=/boot/firmware/cmdline.txt; [ -f $CMD ] || CMD=/boot/cmdline.txt
before=$(cat $CFG $CMD | md5sum)
[ -f $CFG.super3dx.bak ] || sudo cp $CFG $CFG.super3dx.bak; [ -f $CMD.super3dx.bak ] || sudo cp $CMD $CMD.super3dx.bak
if ! grep -q composite $CFG; then
  sudo sed -i 's/^dtoverlay=vc4-kms-v3d[[:space:]]*$/dtoverlay=vc4-kms-v3d,composite/' $CFG
  printf '\n# Super3DX+\nenable_tvout=1\ndisable_splash=1\n' | sudo tee -a $CFG >/dev/null
fi
if ! grep -q Composite-1 $CMD; then
  sudo sed -i '1s|$| """ + CMDLINE_VIDEO.replace("|", "\\|") + r"""|' $CMD
fi
[ "$before" = "$(cat $CFG $CMD | md5sum)" ] && echo UNCHANGED || echo CHANGED
"""


# ------------------------------------------------------------------------------------------------------------ ssh
def ssh_base(target, tty=False):
    cmd = ["ssh", "-o", "StrictHostKeyChecking=accept-new", "-o", "ServerAliveInterval=30", "-o", "ConnectTimeout=15"]
    return cmd + (["-t"] if tty else []) + [target]


def remote(target, script, capture=False, check=False, tty=False, inp=None):
    """run a shell script on the Pi; returns the CompletedProcess (stdout as text if capture)"""
    r = subprocess.run(ssh_base(target, tty) + [script], capture_output=capture, text=True, encoding="utf-8", errors="replace", input=inp)
    if check and r.returncode:
        raise SystemExit("a command on the Pi failed (exit code %d):\n  %s" % (r.returncode, script.strip().splitlines()[0][:150]))
    return r


def ensure_key(target):
    """password-less login: installs this PC's SSH key on the Pi (asks for the Pi's password once)"""
    if subprocess.run(ssh_base(target) + ["-o", "BatchMode=yes", "true"], capture_output=True).returncode == 0:
        return True
    ssh_dir = os.path.join(os.path.expanduser("~"), ".ssh")
    pub = next((os.path.join(ssh_dir, f) for f in ("id_ed25519.pub", "id_ecdsa.pub", "id_rsa.pub") if os.path.isfile(os.path.join(ssh_dir, f))), None)
    if not pub:
        os.makedirs(ssh_dir, exist_ok=True)
        say("Making an SSH key for this PC (so the setup can log in to the Pi without asking for its password again)...")
        key = os.path.join(ssh_dir, "id_ed25519")
        if subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key]).returncode:
            raise SystemExit("could not make an SSH key (ssh-keygen failed)")
        pub = key + ".pub"
    say("\nThe Pi will now ask for its password, once. (Nothing shows while you type; that is normal.)")
    r = subprocess.run(ssh_base(target) + ["mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"],
                       input=open(pub, encoding="utf-8").read(), text=True)
    return r.returncode == 0 and subprocess.run(ssh_base(target) + ["-o", "BatchMode=yes", "true"], capture_output=True).returncode == 0


class _Progress:
    """a write-only file that counts the bytes going through it and shows a percentage"""

    def __init__(self, f, total, label):
        self.f, self.total, self.label, self.n, self.shown = f, max(total, 1), label, 0, -1

    def write(self, b):
        self.f.write(b)
        self.n += len(b)
        pct = min(100, self.n * 100 // self.total)
        if pct // 5 != self.shown:
            self.shown = pct // 5
            say("  %s: %d%%" % (self.label, pct))
        return len(b)

    def flush(self):
        self.f.flush()


def send_tree(items, base, target, remote_dir, label, skip=()):
    """stream files (paths relative to base) into a directory on the Pi as a tar archive; skip: names to leave out"""
    files = []
    for it in items:
        full = os.path.join(base, it)
        if os.path.isfile(full):
            files.append(it)
            continue
        for dp, dn, fn in os.walk(full):
            dn[:] = [d for d in dn if d != "__pycache__" and d not in skip]
            files += [os.path.relpath(os.path.join(dp, f), base) for f in fn if not f.endswith(".pyc") and f not in skip]
    total = sum(os.path.getsize(os.path.join(base, f)) for f in files)
    say("sending %s (%d files, %.0f MB)..." % (label, len(files), total / 1e6))
    p = subprocess.Popen(ssh_base(target) + ["mkdir -p %s && tar -xf - -C %s" % (shlex.quote(remote_dir), shlex.quote(remote_dir))], stdin=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=_Progress(p.stdin, total, label), mode="w|") as tar:
            for f in files:
                tar.add(os.path.join(base, f), arcname=f.replace(os.sep, "/"), recursive=False)
        p.stdin.close()
    except (BrokenPipeError, OSError):
        pass
    if p.wait():
        raise SystemExit("sending %s to the Pi failed (the connection dropped, or the Pi ran out of space)" % label)


# ------------------------------------------------------------------------------------------------------------ the setup
def run(build_py, jobs):
    total = 5
    say("")
    say("Super3DX+ setup for the Raspberry Pi")
    say("Run this after the main setup. It builds the player ON the Pi (a few minutes), installs the game there, and makes it start at boot.")
    say("You need: a Pi with 64-bit Raspberry Pi OS (Lite is fine; 'trixie' is what it was tested on) that is on your network with SSH turned on")
    say("(in Raspberry Pi Imager's settings, tick \"Enable SSH\" and set a user name and password), and the game built by the main setup.")

    # 1 ------------------------------------------------------------------------------------------------------------
    step(1, total, "find the game you built")
    chosen = choose_build("Pi")
    if not chosen:
        return 1
    folder, info, game = chosen
    stamp = {"tools": info.get("tools"), "main_md5": info.get("main_md5")}

    # 2 ------------------------------------------------------------------------------------------------------------
    step(2, total, "connect to the Pi")
    for tool in ("ssh", "ssh-keygen"):
        if not which(tool):
            say("\n`%s` is missing. On Windows 10/11: Settings > Apps > Optional features > add \"OpenSSH Client\". On Linux: sudo apt install openssh-client." % tool)
            return 1
    say("Turn the Pi on, wait for it to finish starting, and find its address (your router's device list shows it; `raspberrypi.local` or the host name you set often works too).")
    host = ask("Pi address (for example 192.168.1.50 or raspberrypi.local): ")
    if not host:
        return 1
    user = ask("User name on the Pi [pi]: ", "pi")
    target = "%s@%s" % (user, host) if "@" not in host else host
    user = target.split("@")[0]
    say("\nConnecting to %s ..." % target)
    if not ensure_key(target):
        say("\nI could not log in to %s. Check the address, the user name and the password, that the Pi is on the same network and that SSH is enabled" % target)
        say("(Raspberry Pi Imager > settings > Enable SSH, or `sudo raspi-config` > Interface Options > SSH on the Pi). Then run this again.")
        return 1
    r = remote(target, "uname -m; . /etc/os-release; echo \"$PRETTY_NAME\"; tr -d '\\0' </proc/device-tree/model 2>/dev/null; echo; echo $HOME; "
                       "free -m | awk 'NR==2{print $2}'; df -k --output=avail $HOME | tail -1; (sudo -n true 2>/dev/null && echo SUDO_OK || echo SUDO_ASK)", capture=True)
    out = [l.strip() for l in r.stdout.splitlines()]
    if r.returncode or len(out) < 8:
        say("\nCould not read the Pi's details. The output was:\n" + r.stdout + (r.stderr or ""))
        return 1
    arch, osname, model, home, mem, avail, sudo = out[0], out[1], out[2], out[3], int(out[4] or 0), int(out[5] or 0), out[6]
    say("Connected: %s, %s, %s" % (model or "unknown board", osname, arch))
    if arch != "aarch64":
        say("\nThis Pi runs a 32-bit system (%s). Super3DX+ needs the 64-bit Raspberry Pi OS: write that to the card with Raspberry Pi Imager and try again." % arch)
        return 1
    if "Raspberry Pi 5" not in model:
        say("\nNote: only the Raspberry Pi 5 has been tested. Other models may be too slow, and the composite video settings below are specific to the Pi 5.")
        if not yes("Carry on anyway?", False):
            return 1
    if mem < 1800:
        say("\nNote: this Pi has only %d MB of memory; compiling the game may fail. Using a Pi with 4 GB or more is recommended." % mem)
    if avail < 6 * 1024 * 1024:
        say("\nThe Pi has only %.1f GB free; the game needs about 5 GB. Free some space or use a bigger card." % (avail / 1024 / 1024))
        return 1
    if sudo != "SUDO_OK":
        say("\n%s on the Pi needs a password for `sudo`, and the setup cannot type it for you. On the Pi (or in an ssh session) run once:" % user)
        say("    echo '%s ALL=(ALL) NOPASSWD:ALL' | sudo tee /etc/sudoers.d/010_super3dx-nopasswd" % user)
        say("then run this again. (Raspberry Pi Imager's default user already has this.)")
        return 1
    hdmi = False
    say("\nHow is the Pi connected to the screen?\n  1) composite video to a CRT or TV (the Pi 5 needs a soldered composite jack; see docs/PI.md)\n  2) HDMI")
    hdmi = ask("Choose 1 or 2 [1]: ", "1") == "2"
    if not hdmi and "Raspberry Pi 5" not in model:
        say("Composite output is only set up for the Pi 5. On other models HDMI is the safer choice.")
        hdmi = yes("Use HDMI instead?")
    home_game = posixpath.join(home, "game")

    # 3 ------------------------------------------------------------------------------------------------------------
    step(3, total, "install what the Pi needs (a few minutes)")
    log = os.path.join(ROOT, "build", "logs", "setup-pi-%s.txt" % time.strftime("%Y%m%d-%H%M%S"))
    say("(everything below is also saved in %s)\n" % os.path.relpath(log, ROOT))
    pkgs = "build-essential python3 pkg-config libsdl2-dev libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libdrm-dev libasound2-dev zlib1g-dev libgl1-mesa-dri alsa-utils"
    script = ("set -e; export DEBIAN_FRONTEND=noninteractive; sudo apt-get update -qq; sudo apt-get install -y --no-install-recommends %s; "
              "sudo apt-get install -y --no-install-recommends libasound2t64 || sudo apt-get install -y --no-install-recommends libasound2" % pkgs)
    rc = run_logged(ssh_base(target) + [script], log)
    if rc:
        explain_failure(log, rc)
        say("(On the Pi, `apt` needs a working internet connection.)")
        return 1

    # 4 ------------------------------------------------------------------------------------------------------------
    step(4, total, "build the player on the Pi")
    say("Regenerating the sources for this version of the game on this PC...")
    rc = run_logged([sys.executable, build_py, "gensrc", game], log)
    if rc:
        explain_failure(log, rc)
        return 1
    send_tree(SRC_ITEMS, ROOT, target, "super3dx-src", "the sources", skip=("data",))
    say("\nCompiling on the Pi (the big physics file takes a few minutes)...")
    rc = run_logged(ssh_base(target) + ["set -e; cd super3dx-src && python3 build.py exe --target pi -j %d" % max(2, min(jobs, 4))], log)
    if rc:
        explain_failure(log, rc)
        return 1

    # 5 ------------------------------------------------------------------------------------------------------------
    step(5, total, "install the game and the settings")
    remote(target, "mkdir -p ~/game/data ~/game/save; sudo systemctl stop wmmt 2>/dev/null; true")
    remote(target, "cp ~/super3dx-src/build/pi/super3dx ~/game/super3dx.new && mv ~/game/super3dx.new ~/game/super3dx && chmod +x ~/game/super3dx", check=True)
    r = remote(target, "cat ~/game/build-info.json 2>/dev/null", capture=True)
    try:
        there = json.loads(r.stdout)
    except ValueError:
        there = {}
    if there.get("tools") == stamp["tools"] and there.get("main_md5") == stamp["main_md5"] and stamp["tools"]:
        say("The data on the Pi is already current: not sending it again.")
    else:
        say("Sending the game's data (several GB; a few minutes on a wired network)...")
        send_tree(["data"], folder, target, home_game, "the data", skip=("save",))
        remote(target, "cat > ~/game/build-info.json", inp=json.dumps(stamp), check=True)
    remote(target, "[ -f ~/game/crt.txt ] || cat > ~/game/crt.txt", inp=open(os.path.join(FILES, "crt.txt"), encoding="utf-8").read())
    remote(target, "[ -f ~/game/save/graphics ] || cat > ~/game/save/graphics", inp=open(os.path.join(FILES, "graphics"), encoding="utf-8").read())
    svc = open(os.path.join(FILES, "wmmt-hdmi.service" if hdmi else "wmmt.service"), encoding="utf-8").read()
    svc = svc.replace("User=wmmt", "User=" + user).replace("/home/wmmt", home)
    remote(target, "sudo tee /etc/systemd/system/wmmt.service >/dev/null", inp=svc, check=True)
    if hdmi:
        remote(target, "sudo rm -f /etc/asound.conf")
    else:
        remote(target, "sudo tee /etc/asound.conf >/dev/null", inp=open(os.path.join(FILES, "asound.conf"), encoding="utf-8").read(), check=True)
    changed = False
    if not hdmi:
        r = remote(target, COMPOSITE_SCRIPT, capture=True)
        if r.returncode:
            say("\nCould not set up the composite video output (%s). The game is installed; see docs/PI.md for the settings to add by hand." % (r.stderr or "").strip()[:200])
        else:
            changed = "CHANGED" in r.stdout
            say("Composite video output: " + ("set up (a restart is needed)." if changed else "already set up."))
    remote(target, "sudo systemctl daemon-reload && sudo systemctl enable wmmt >/dev/null 2>&1; true")
    if changed:
        if yes("\nThe Pi must restart to switch its video output. Restart it now? (the game starts by itself afterwards)"):
            remote(target, "sudo reboot")
            say("Restarting. In a minute the game should appear on the screen.")
        else:
            say("Restart it yourself when you like (unplug and replug the power, or: ssh %s sudo reboot). The game starts by itself at boot." % target)
    else:
        remote(target, "sudo systemctl restart wmmt")
        time.sleep(8)
        r = remote(target, "systemctl is-active wmmt; tail -n 3 ~/game/game.log | cut -c1-150", capture=True)
        say("\nThe game service is: " + (r.stdout.strip().splitlines() or ["(unknown)"])[0])
    aud = remote(target, "aplay -l 2>/dev/null | grep -ci usb", capture=True).stdout.strip()
    if not hdmi and aud in ("", "0"):
        say("\nNote: no USB audio device was found on the Pi. The Pi 5 has no headphone jack; plug in a USB sound card (the settings expect one named \"Audio\").")
    say("\nDone. The game is installed on the Pi and starts at every boot.")
    say("Settings: ~/game/crt.txt (the CRT picture area), saves: ~/game/save. To stop the game: ssh %s sudo systemctl stop wmmt" % target)
    say("More in docs/PI.md. To update later, run the main setup again and then this one: it only sends what changed.")
    return 0
