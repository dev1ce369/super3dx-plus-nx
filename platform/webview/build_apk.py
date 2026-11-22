#!/usr/bin/env python3
"""Builds a thin Android WebView shell for the public https://wmmtd.xyz/ site.

    python platform/webview/build_apk.py --sdk <Android SDK> --jdk <JDK 17> --out <file.apk>

The APK contains only this Java shell: no native library, no game dump, no
converted data, and no copy of the site. It needs a network connection at run
time and opens the site in a fullscreen WebView.

  --sdk       Android SDK (build-tools 35+ and an installed platform)
  --jdk       JDK 17 or newer (javac, keytool)
  --out       output APK path
  --pkg       application id (default com.wmmt.webview)
  --label     launcher label (default "Wangan Browser")
  --keystore  signing key. Updates must use the SAME key; this defaults to
              .super3dx/android/debug.keystore so the existing Android key is reused.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STORE_PASS = "super3dxdebug"


def say(*a):
    print(*a, flush=True)


def vkey(name):
    return [int(x) if x.isdigit() else 0 for x in re.split(r"[.\-]", name)]


def latest(folder):
    names = [n for n in os.listdir(folder) if not n.startswith(".")]
    if not names:
        raise SystemExit("%s is empty" % folder)
    return sorted(names, key=vkey)[-1]


def tool(directory, name, kinds=(".exe", ".bat", ".cmd", "")):
    for k in kinds:
        p = os.path.join(directory, name + k)
        if os.path.isfile(p):
            return p
    raise SystemExit("%s was not found in %s: is the Android SDK complete?" % (name, directory))


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], cwd=kw.pop("cwd", ROOT), **kw)
    if r.returncode:
        raise SystemExit("a build command failed (exit code %d): %s ...\n  Its own message is just above." %
                         (r.returncode, " ".join(str(c) for c in cmd[:3])))


def build(sdk, jdk, out_apk, pkg="com.wmmt.webview", label="Wangan Browser", keystore=None, work=None):
    if not sdk or not jdk:
        raise SystemExit("give --sdk (or set ANDROID_HOME) and --jdk (or set JAVA_HOME)")
    bt = os.path.join(sdk, "build-tools", latest(os.path.join(sdk, "build-tools")))
    plat = os.path.join(sdk, "platforms", latest(os.path.join(sdk, "platforms")))
    android_jar = os.path.join(plat, "android.jar")
    if vkey(os.path.basename(bt)) < [35]:
        raise SystemExit("build-tools %s is too old: zipalign -P needs 35 or newer" % os.path.basename(bt))
    if not os.path.isfile(android_jar):
        raise SystemExit("android.jar not found in %s" % plat)

    env = dict(os.environ, JAVA_HOME=jdk, PATH=os.path.join(jdk, "bin") + os.pathsep + os.environ.get("PATH", ""))
    work = work or os.path.join(ROOT, "build", "webview")
    shutil.rmtree(os.path.join(work, "apk"), ignore_errors=True)
    shutil.rmtree(os.path.join(work, "classes"), ignore_errors=True)
    shutil.rmtree(os.path.join(work, "res"), ignore_errors=True)
    for d in ("apk", "classes", os.path.join("res", "mipmap")):
        os.makedirs(os.path.join(work, d), exist_ok=True)

    say("compiling the WebView shell...")
    run([tool(os.path.join(jdk, "bin"), "javac"), "-source", "8", "-target", "8", "-nowarn",
         "-classpath", android_jar, "-d", os.path.join(work, "classes"),
         os.path.join("platform", "webview", "java", "com", "wmmt", "webview", "MainActivity.java")], env=env)
    class_files = [os.path.join(dp, f) for dp, _, fn in os.walk(os.path.join(work, "classes")) for f in fn if f.endswith(".class")]
    run([tool(bt, "d8"), "--min-api", "21", "--lib", android_jar,
         "--output", os.path.join(work, "apk")] + class_files, env=env)

    say("adding the launcher icon...")
    icon = os.path.join(ROOT, "data", "icon", "Icon-76@2x.png")
    if not os.path.isfile(icon):
        icon = os.path.join(work, "icon.png")
        run([sys.executable, os.path.join(ROOT, "tools", "dev", "placeholder_icon.py"), icon], env=env)
    shutil.copyfile(icon, os.path.join(work, "res", "mipmap", "icon.png"))
    res_zip = os.path.join(work, "res.zip")
    run([tool(bt, "aapt2"), "compile", "--dir", os.path.join(work, "res"), "-o", res_zip], env=env)

    manifest = os.path.join(ROOT, "platform", "webview", "AndroidManifest.xml")
    text = open(manifest, encoding="utf-8").read()
    text = text.replace('package="com.wmmt.webview"', 'package="%s"' % pkg)
    text = re.sub(r'android:label="[^"]*"', 'android:label="%s"' % label, text)
    work_manifest = os.path.join(work, "AndroidManifest.xml")
    open(work_manifest, "w", encoding="utf-8").write(text)

    base = os.path.join(work, "base.apk")
    run([tool(bt, "aapt2"), "link", "-o", base, "-I", android_jar, "--manifest", work_manifest,
         "--min-sdk-version", "21", "--target-sdk-version", "34",
         "--version-code", "1", "--version-name", "1.0", res_zip], env=env)

    say("packing (no native library, no data)...")
    with zipfile.ZipFile(base, "a") as z:
        z.write(os.path.join(work, "apk", "classes.dex"), "classes.dex", zipfile.ZIP_DEFLATED)

    aligned = os.path.join(work, "aligned.apk")
    if os.path.exists(aligned):
        os.remove(aligned)
    run([tool(bt, "zipalign"), "-f", "-P", "16", "4", base, aligned], env=env)

    keystore = keystore or os.path.join(ROOT, ".super3dx", "android", "debug.keystore")
    if not os.path.isfile(keystore):
        os.makedirs(os.path.dirname(keystore), exist_ok=True)
        run([tool(os.path.join(jdk, "bin"), "keytool"), "-genkeypair", "-keystore", keystore,
             "-storepass", STORE_PASS, "-keypass", STORE_PASS, "-alias", "super3dx",
             "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Super3DX debug"], env=env)
    os.makedirs(os.path.dirname(os.path.abspath(out_apk)), exist_ok=True)
    run([tool(bt, "apksigner"), "sign", "--v4-signing-enabled", "false",
         "--ks", keystore, "--ks-key-alias", "super3dx",
         "--ks-pass", "pass:" + STORE_PASS, "--key-pass", "pass:" + STORE_PASS,
         "--out", out_apk, aligned], env=env)
    say("built %s (%d bytes)" % (out_apk, os.path.getsize(out_apk)))
    return out_apk


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", default=os.environ.get("ANDROID_HOME"))
    ap.add_argument("--jdk", default=os.environ.get("JAVA_HOME"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--pkg", default="com.wmmt.webview")
    ap.add_argument("--label", default="Wangan Browser")
    ap.add_argument("--keystore")
    a = ap.parse_args()
    build(a.sdk, a.jdk, a.out, a.pkg, a.label, a.keystore)


if __name__ == "__main__":
    main()
