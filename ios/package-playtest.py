#!/usr/bin/env python3
"""Package only the compiled app, without signing identity or player data."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('ipa', type=Path)
    args = parser.parse_args()
    source = args.app.resolve()
    destination = args.ipa.resolve()
    if not source.is_dir() or source.suffix != '.app':
        parser.error('Supply a compiled .app directory')
    destination.parent.mkdir(parents=True, exist_ok=True)
    # A fresh staging directory prevents Documents, saves, config and incoming game
    # data from ever being copied from a device container into the distributable.
    with tempfile.TemporaryDirectory(prefix='marathon-playtest-', dir=destination.parent) as temporary:
        app = Path(temporary) / 'Payload' / 'MarathonRecomp.app'
        app.mkdir(parents=True)
        info = plistlib.loads((source / 'Info.plist').read_bytes())
        executable = info['CFBundleExecutable']
        allowed = {executable, 'Info.plist', 'PkgInfo', 'Assets.car'}
        for path in source.iterdir():
            if path.name in allowed or re.fullmatch(r'AppIcon[^/]*\.png', path.name):
                if not path.is_file() or path.is_symlink():
                    raise RuntimeError(f'Unexpected app resource: {path.name}')
                shutil.copy2(path, app / path.name)
            elif path.name not in {'_CodeSignature', 'embedded.mobileprovision'}:
                raise RuntimeError(f'Unreviewed resource in app bundle: {path.name}')
        # Sideloaders apply the tester's own identity and profile.
        binary = app / executable
        signed = subprocess.run(['codesign', '-d', str(binary)], capture_output=True)
        if signed.returncode == 0:
            subprocess.run(['codesign', '--remove-signature', str(binary)], check=True)
        subprocess.run(['strip', '-S', str(binary)], check=True)
        # Preserve the project's bundle ID and display name for consistent
        # installs; only the tester's signing identity is removed.
        version = str(info['CFBundleShortVersionString']).removeprefix('v')
        if not re.fullmatch(r'\d+\.\d+\.\d+', version):
            raise RuntimeError('Invalid iOS version string')
        info['CFBundleShortVersionString'] = version
        info['CFBundleVersion'] = '1.0.5'
        (app / 'Info.plist').write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
        forbidden = [b'/Users/', b'/Volumes/', b'@gmail.com', b'BEGIN PRIVATE KEY']
        for path in app.iterdir():
            data = path.read_bytes()
            if any(marker in data for marker in forbidden):
                raise RuntimeError(f'Private build metadata remains in {path.name}')
        manifest = []
        with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(app.iterdir()):
                name = path.relative_to(Path(temporary)).as_posix()
                data = path.read_bytes()
                entry = zipfile.ZipInfo(name, date_time=(2026, 10, 5, 0, 0, 0))
                entry.create_system = 3
                entry.external_attr = (0o100755 if path.name == executable else 0o100644) << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, data)
                manifest.append({'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        with zipfile.ZipFile(destination) as archive:
            if archive.testzip() is not None:
                raise RuntimeError('IPA integrity check failed')
        report = {'ipa': destination.name, 'sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                  'unsigned': True, 'bundle_identifier': info['CFBundleIdentifier'], 'files': manifest}
        destination.with_suffix('.manifest.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'files'}, indent=2))


if __name__ == '__main__':
    main()
