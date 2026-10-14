#!/usr/bin/env python3
"""Package an existing device app for signing by its recipient."""
import argparse
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import tempfile
import zipfile


def package(app: Path, output: Path) -> None:
    app = app.resolve()
    output = output.resolve()
    with (app / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    executable = app / info['CFBundleExecutable']
    if not executable.is_file():
        raise ValueError(f'Missing app executable: {executable}')
    # Reject the two startup omissions that can pass linking but abort in UIKit.
    scenes = info.get('UIApplicationSceneManifest', {}).get('UISceneConfigurations', {})
    configurations = scenes.get('UIWindowSceneSessionRoleApplication', [])
    delegates = [item.get('UISceneDelegateClassName') for item in configurations]
    if 'MarathonSceneDelegate' not in delegates:
        raise ValueError('Missing MarathonSceneDelegate scene lifecycle configuration')
    if not info.get('NSBluetoothAlwaysUsageDescription', '').strip():
        raise ValueError('Missing Bluetooth usage description required by controller discovery')
    symbols = subprocess.check_output(['nm', str(executable)], text=True)
    if '_OBJC_CLASS_$_MarathonSceneDelegate' not in symbols:
        raise ValueError('Scene delegate is configured but absent from the linked executable')
    if output.is_relative_to(app):
        raise ValueError('IPA output must be outside the source app')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='marathon-ipa-') as directory:
        root = Path(directory)
        copied = root / 'Payload' / app.name
        shutil.copytree(app, copied, symlinks=True)
        for path in list(copied.rglob('*')):
            if path.name in ('_CodeSignature', 'embedded.mobileprovision', '.DS_Store'):
                if path.is_dir() and not path.is_symlink():
                    shutil.rmtree(path)
                else:
                    path.unlink()
        macho_magic = {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xce\xfa\xed\xfe',
                       b'\xfe\xed\xfa\xce', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
                       b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
        for path in copied.rglob('*'):
            if path.is_file() and not path.is_symlink():
                with path.open('rb') as stream:
                    magic = stream.read(4)
                if magic in macho_magic:
                    # An unsigned Mach-O has nothing to remove.
                    signed = subprocess.run(['codesign', '--display', str(path)], capture_output=True)
                    if signed.returncode == 0:
                        subprocess.run(['codesign', '--remove-signature', str(path)], check=True)
        archive = root / 'app.ipa'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(copied.rglob('*')):
                name = path.relative_to(root).as_posix()
                if path.is_symlink():
                    entry = zipfile.ZipInfo(name)
                    entry.create_system = 3
                    entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                    bundle.writestr(entry, str(path.readlink()))
                elif path.is_file():
                    bundle.write(path, name)
        with zipfile.ZipFile(archive) as bundle:
            bad = bundle.testzip()
            if bad:
                raise ValueError(f'Corrupt IPA entry: {bad}')
        shutil.copyfile(archive, output)
    print(f'Unsigned IPA: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    package(args.app, args.output)
