#!/usr/bin/env python3
"""Update an offline ext4 guest image using a checked adjacent copy."""
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 3:
        raise SystemExit('Usage: update-keystone-rootfs.py IMAGE OVERLAY')
    image = Path(sys.argv[1]).resolve(strict=True)
    overlay = Path(sys.argv[2]).resolve(strict=True)
    scripts = Path(__file__).resolve().parent
    if not image.is_file() or not overlay.is_dir():
        raise SystemExit('Regular image and overlay directory required')
    for backing in Path('/sys/block').glob('loop*/loop/backing_file'):
        try:
            attached = backing.read_text().strip()
        except (PermissionError, FileNotFoundError):
            continue
        if attached and Path(attached).resolve() == image:
            raise SystemExit('Image has a loop device attached. Unmount and detach it first.')
    with image.open('r+b') as original:
        try:
            # Whole-file POSIX lock conflicts with QEMU's byte-range disk locks.
            fcntl.lockf(original, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Image is in use. Shut down QEMU before make install.')
        subprocess.run(['e2fsck', '-fn', str(image)], check=True)
        with tempfile.TemporaryDirectory(prefix='.keystone-update-', dir=image.parent) as tmp:
            work = Path(tmp)
            staged = work / 'rootfs.img'
            subprocess.run(['cp', '--reflink=auto', '--sparse=always', '--preserve=mode',
                            str(image), str(staged)], check=True)
            subprocess.run([sys.executable, str(scripts / 'populate-keystone-rootfs.py'),
                            str(overlay), str(staged), str(work)], check=True)
            subprocess.run(['e2fsck', '-fn', str(staged)], check=True)
            with staged.open('rb') as stream:
                os.fsync(stream.fileno())
            os.replace(staged, image)
        print(f'Updated Keystone image: {image}')


if __name__ == '__main__':
    main()
