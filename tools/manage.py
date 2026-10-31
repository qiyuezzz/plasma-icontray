#!/usr/bin/env python3
"""Build, install and uninstall IconTray without changing the panel layout."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PLUGIN = "org.kde.plasma.icontray.so"
ENV_MARKER = "# Managed by IconTray installer"
RECEIPT = Path("share/icontray/install.json")
HELPERS = {Path("share/icontray/manage.py"), Path("bin/icontray-uninstall"), RECEIPT}


def run(args, **kwargs):
    print("+", shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def owned_file(path, prefix):
    """Accept only this project's installed artifacts, never arbitrary manifests."""
    if not path.is_absolute() or ".." in path.parts or not path.is_relative_to(prefix):
        return False
    relative = path.relative_to(prefix)
    return (relative in HELPERS
            or (path.name == PLUGIN and path.parent.parts[-2:] == ("plasma", "applets"))
            or (path.name == "icontray.mo" and path.parent.name == "LC_MESSAGES" and "locale" in relative.parts))


def read_receipt(prefix):
    path = prefix / RECEIPT
    if not path.exists():
        return None
    data = json.loads(path.read_text())
    if data.get("schema") != 1 or data.get("prefix") != str(prefix):
        raise ValueError(f"Invalid IconTray installation record: {path}")
    if not isinstance(data.get("files"), list) or not all(isinstance(p, str) for p in data["files"]):
        raise ValueError(f"Invalid file list: {path}")
    return data


def sudo_for(system):
    return ["sudo"] if system and os.geteuid() != 0 else []


def save_receipt(prefix, data, system):
    with tempfile.TemporaryDirectory(prefix="icontray-receipt-") as temporary:
        source = Path(temporary) / "install.json"
        source.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
        run([*sudo_for(system), "install", "-Dm644", source, prefix / RECEIPT])


def find_cmake(requested, build):
    if requested:
        return shutil.which(requested)
    executable = shutil.which("cmake")
    if executable:
        return executable
    cache = build / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_COMMAND:INTERNAL="):
                return shutil.which(line.partition("=")[2])
    return None


def restart_shell(plugin_dirs=(), data_dir=None):
    service = "plasma-plasmashell.service"
    if not shutil.which("systemctl") or subprocess.run(
            ["systemctl", "--user", "is-active", "--quiet", service]).returncode:
        print("Plasma service is not running. Log out and back in to refresh widgets.")
        return
    if plugin_dirs:
        environment = dict(line.split("=", 1) for line in subprocess.check_output(
            ["systemctl", "--user", "show-environment"], text=True).splitlines() if "=" in line)
        plugins = list(dict.fromkeys([*map(str, plugin_dirs), *filter(None, environment.get("QT_PLUGIN_PATH", "").split(":"))]))
        assignments = ["QT_PLUGIN_PATH=" + ":".join(plugins)]
        if data_dir:
            data_paths = list(dict.fromkeys([str(data_dir), *environment.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":")]))
            assignments.append("XDG_DATA_DIRS=" + ":".join(data_paths))
        run(["systemctl", "--user", "set-environment", *assignments])
    print("Refreshing Plasma; the panel will briefly reload. Open applications stay running.")
    run(["systemctl", "--user", "restart", service])


def uninstall(prefix, build, system, default_env, no_session_env, restart, dry_run):
    receipt = read_receipt(prefix)
    if receipt:
        files = [Path(p) for p in receipt["files"]]
        system = system or bool(receipt.get("system"))
        env_file = Path(receipt["session_env"]) if receipt.get("session_env") else None
        plugin_dirs = receipt.get("plugin_dirs", [])
    else:
        # Upgrade path for installations made before persistent receipts existed.
        manifest = build / "install_manifest.txt"
        if not manifest.exists():
            raise ValueError(f"No IconTray installation record in {prefix}. For an older installation, pass its --build-dir.")
        files = [Path(line) for line in manifest.read_text().splitlines() if line]
        env_file = None if system else default_env
        plugin_dirs = [str(prefix / "lib/qt6/plugins")]
    for path in files:
        if not owned_file(path, prefix):
            raise ValueError(f"Refusing unexpected installation entry: {path}. Check --prefix/--system.")
    remove_env = (not system and not no_session_env and env_file and env_file.is_file()
                  and ENV_MARKER in env_file.read_text()
                  and any(shlex.quote(p) in env_file.read_text() for p in plugin_dirs))
    for path in dict.fromkeys(files):
        if path.exists() or path.is_symlink():
            if dry_run:
                print("Would remove:", path)
            else:
                run([*sudo_for(system), "rm", "--", path])
    if remove_env:
        if dry_run:
            print("Would remove:", env_file)
        else:
            env_file.unlink()
    if dry_run:
        return
    print("IconTray removed. Panel layout and widget settings were preserved.")
    if restart:
        restart_shell()
    else:
        print("Log out and back in to refresh widgets.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install", "uninstall", "build", "preview"])
    parser.add_argument("--system", action="store_true", help="Install to /usr; only installation/removal uses sudo")
    parser.add_argument("--prefix", type=Path, help="Installation prefix (default ~/.local, or /usr for --system)")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 2))
    parser.add_argument("--cmake", default=os.environ.get("CMAKE"), help="CMake executable (auto-detected by default)")
    parser.add_argument("--skip-tests", action="store_true", help="Build without Qt Test and skip tests")
    parser.add_argument("--no-session-env", action="store_true", help="Do not write/remove the Plasma session environment file")
    parser.add_argument("--restart", action="store_true", help="Reload the Plasma panel after installing or uninstalling")
    parser.add_argument("--dry-run", action="store_true", help="List files to uninstall without removing them")
    args = parser.parse_args()
    if args.dry_run and args.action != "uninstall":
        parser.error("--dry-run is available for uninstall only")
    prefix = (args.prefix or (Path("/usr") if args.system else Path.home() / ".local")).expanduser().resolve()
    build = args.build_dir.expanduser().resolve()
    plugin_dir = prefix / "lib/qt6/plugins"
    env_file = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "plasma-workspace/env/icontray.sh"
    if args.action == "uninstall":
        uninstall(prefix, build, args.system, env_file, args.no_session_env, args.restart, args.dry_run)
        return
    if args.action == "preview":
        environment = dict(os.environ)
        environment["QT_PLUGIN_PATH"] = str(build / "bin") + os.pathsep + environment.get("QT_PLUGIN_PATH", "")
        run(["plasmawindowed", "org.kde.plasma.icontray"], env=environment)
        return
    if args.action == "install" and not args.system and not args.no_session_env:
        if env_file.exists() and ENV_MARKER not in env_file.read_text():
            parser.error(f"{env_file} is not managed by IconTray. Use --no-session-env and configure QT_PLUGIN_PATH yourself.")
    cmake = find_cmake(args.cmake, build)
    if not cmake:
        parser.error("cmake not found. Install the build dependencies in README.md, or pass --cmake /path/to/cmake.")
    configure = [cmake, "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DBUILD_TESTING={'OFF' if args.skip_tests else 'ON'}"]
    if not args.system:
        configure += ["-DKDE_INSTALL_LIBDIR=lib", "-DKDE_INSTALL_PLUGINDIR=lib/qt6/plugins", "-DKDE_INSTALL_QMLDIR=lib/qt6/qml"]
    else:
        configure += ["-UKDE_INSTALL_LIBDIR", "-UKDE_INSTALL_PLUGINDIR", "-UKDE_INSTALL_QMLDIR"]
    run(configure)
    run([cmake, "--build", build, "--parallel", str(max(1, args.jobs))])
    if not args.skip_tests:
        ctest = Path(cmake).with_name("ctest")
        run([ctest, "--test-dir", build, "--output-on-failure"])
    if args.action == "build":
        print("Build and tests complete. Run ./install.sh, or ./tools/preview.sh.")
        return
    run([*sudo_for(args.system), cmake, "--install", build])
    session_env = None
    if not args.system and not args.no_session_env:
        env_file.parent.mkdir(parents=True, exist_ok=True)
        env_file.write_text(ENV_MARKER + "\nexport QT_PLUGIN_PATH=" + shlex.quote(str(plugin_dir))
                            + '${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\nexport XDG_DATA_DIRS='
                            + shlex.quote(str(prefix / "share")) + ':${XDG_DATA_DIRS:-/usr/local/share:/usr/share}\n')
        env_file.chmod(0o644)
        session_env = str(env_file)
    files = [line for line in (build / "install_manifest.txt").read_text().splitlines() if line]
    files.append(str(prefix / RECEIPT))
    if not all(owned_file(Path(p), prefix) for p in files):
        raise ValueError("Unexpected CMake install manifest; installation record was not written.")
    plugin_dirs = list(dict.fromkeys(str(Path(p).parent.parent.parent) for p in files if Path(p).name == PLUGIN))
    save_receipt(prefix, {"schema": 1, "prefix": str(prefix), "system": args.system,
                          "files": list(dict.fromkeys(files)), "session_env": session_env,
                          "plugin_dirs": plugin_dirs}, args.system)
    print("Installed IconTray. Add ‘IconTray’ from the panel widget list.")
    print("Uninstall anytime with:", prefix / "bin/icontray-uninstall")
    if args.restart:
        restart_shell(plugin_dirs if not args.system else (), prefix / "share")
    else:
        print("Log out and back in, or install with --restart to refresh the widget list now.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except (ValueError, OSError) as error:
        print("IconTray:", error, file=sys.stderr)
        sys.exit(1)
