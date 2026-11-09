#!/usr/bin/env python3
"""Package the RISC-V HPL executable and its matching static-MPI launcher."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

import hpl


def main():
    build = hpl.BUILD
    dist = hpl.ROOT / 'dist'
    dist.mkdir(exist_ok=True)
    strip = os.environ.get('STRIP', 'strip')
    info = json.loads((build / 'build.json').read_text())
    if info['target'] != 'RISCV64_ZVL256B':
        raise RuntimeError('Package target must be the Jupiter2 X100 RISCV64_ZVL256B build')
    with tempfile.TemporaryDirectory(prefix='package-', dir=build) as temp:
        root = Path(temp) / 'hpl-jupiter2-riscv64'
        (root / 'bin').mkdir(parents=True)
        reports = []
        for src, name in [(hpl.BINARY, 'xhpl'), (build/'mpich/bin/mpiexec', 'mpiexec'),
                          (build/'mpich/bin/hydra_pmi_proxy', 'hydra_pmi_proxy')]:
            dest = root / 'bin' / name
            shutil.copyfile(src, dest)
            dest.chmod(0o755)
            subprocess.run([strip, str(dest)], check=True)
            header = subprocess.check_output(['readelf', '-h', str(dest)], text=True)
            dynamic = subprocess.check_output(['readelf', '-d', str(dest)], text=True)
            if 'RISC-V' not in header:
                raise RuntimeError(f'Not a RISC-V executable: {src}')
            needed = re.findall(r'Shared library: \[(.*?)\]', dynamic)
            if any(any(x in n.lower() for x in ['libmpi', 'libpmi', 'libopenblas']) for n in needed):
                raise RuntimeError(f'MPI/BLAS is dynamically linked: {needed}')
            reports.append(f'{name}\n{header}\n{dynamic}')
        (root/'ELF-report.txt').write_text('\n'.join(reports))
        for name in ['hpl.py', 'HPL-32GiB.dat']:
            shutil.copyfile(hpl.ROOT/name, root/name)
        (root/'build.json').write_text(json.dumps(info, indent=2)+'\n')
        (root/'licenses').mkdir()
        for src, name in [(hpl.ROOT/'vendor/HPL-COPYRIGHT', 'HPL-COPYRIGHT'),
                          (build/'OpenBLAS-0.3.34/LICENSE', 'OpenBLAS-LICENSE'),
                          (build/'mpich-4.3.2/COPYRIGHT', 'MPICH-COPYRIGHT')]:
            shutil.copyfile(src, root/'licenses'/name)
        launcher = root/'run-hpl'
        launcher.write_text('''#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export HPL_BINARY="$here/bin/xhpl"
export HPL_BUILD_MANIFEST="$here/build.json"
export MPIEXEC="$here/bin/mpiexec"
exec python3 "$here/hpl.py" run "$@"
''')
        launcher.chmod(0o755)
        (root/'Makefile').write_text('''.DEFAULT_GOAL := help
.PHONY: help plan smoke run
help:
\t@echo 'Prebuilt RISC-V: make smoke, make plan, make run'
plan:
\tpython3 hpl.py plan
smoke:
\t./run-hpl --smoke
run:
\t./run-hpl
''')
        (root/'README.md').write_text('''# Jupiter2 X100用 HPL 実行バイナリ

MPIとOpenBLASを静的リンクしたRISC-V ELFです。MPIの別途インストールは不要です。
MPIランチャーとhydra_pmi_proxyは必ず本配布物の組み合わせを使用してください。
OSの標準libc/libmと `/lib/ld-linux-riscv64-lp64d.so.1` は必要です。
glibc 2.38以降（ELFで確認）。Bianbu/Ubuntuの該当環境で使用してください。
Python 3.11以降を使用します。ビルド済み実行ではtar展開機能は使用しません。

```sh
tar -xzf hpl-jupiter2-riscv64.tar.gz
cd hpl-jupiter2-riscv64
./run-hpl --smoke            # 小規模、8 MPI ranks / X100 CPU 0-7
python3 hpl.py plan          # 32 GiB上限で現在のメモリから最大Nを計算
./run-hpl                    # 最大Nで測定
# 比較用の固定問題サイズ：
N=12000 ./run-hpl
```

既定：NB=192、P=2、Q=4、CPU_LIST=0-7、BLASは1 thread。
RESERVE_GIB=2、AVAILABLE_RESERVE_GIB=1。32 GiBが全て見える理想条件ではN=61440。
実際はMemTotal/MemAvailableから小さくなる場合があります。
`results/<UTC>/`にパラメーター・メモリ計画・MPIログ・残差判定を保存します。
HPL.datの手編集ではなく、環境変数 N/NB/P/Q/CPU_LIST で変更してください。
A100へstock ZVL256Bを移動したり、CPU0-15を混成使用したりしないでください。

MPI=MPICH4.3.2 ch3:sock、OpenBLAS0.3.34 RISCV64_ZVL256B、HPL2.3、LP64、FP64。
これはJupiter2の性能保証や最大Nの実機検証完了を意味しません。
検証範囲とソースはリポジトリ benchmarks/hpl/README.md を参照してください。

This product includes software developed at the University of Tennessee, Knoxville, Innovative Computing Laboratory.
''')
        sums = []
        for f in sorted(root.rglob('*')):
            if f.is_file():
                sums.append(f'{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.relative_to(root)}')
        (root/'SHA256SUMS').write_text('\n'.join(sums)+'\n')
        archive = dist/'hpl-jupiter2-riscv64.tar.gz'
        with tarfile.open(archive, 'w:gz') as out:
            out.add(root, arcname=root.name)
        shutil.copyfile(root/'bin/xhpl', dist/'xhpl-riscv64')
        (dist/'xhpl-riscv64').chmod(0o755)
        (dist/'SHA256SUMS').write_text('\n'.join(f'{hpl.digest(dist/name)}  {name}' for name in
                                                ['hpl-jupiter2-riscv64.tar.gz', 'xhpl-riscv64'])+'\n')
        print('Packaged:', archive, archive.stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
