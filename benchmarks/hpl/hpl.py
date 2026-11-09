#!/usr/bin/env python3
"""Build and run native HPL, with a memory-bounded Jupiter2 preset."""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import signal
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent
PROFILE = os.environ.get('HPL_PROFILE', '')
if PROFILE and not re.fullmatch(r'[A-Za-z0-9_-]+', PROFILE):
    raise ValueError('Invalid HPL_PROFILE')
BUILD = ROOT / '.build' / PROFILE
GIB = 1024 ** 3
HPL_SHA = '32c5c17d22330e6f2337b681aded51637fb6008d3f0eb7c277b163fadd612830'
BLAS_SHA = 'cd7e129868320cc2d033afa920e31202dfe0b8066a5b66661900ccc0f197dfed'
BLAS_URL = ('https://github.com/OpenMathLib/OpenBLAS/releases/download/'
            'v0.3.34/OpenBLAS-0.3.34.tar.gz')
MPI_SHA = '47d774587a7156a53752218c811c852e70ac44db9c502dc3f399b4cb817e3818'
MPI_URL = 'https://www.mpich.org/static/downloads/4.3.2/mpich-4.3.2.tar.gz'
BINARY = Path(os.environ.get('HPL_BINARY', BUILD / 'hpl-2.3/bin/Jupiter2/xhpl'))
BUILD_MANIFEST = Path(os.environ.get('HPL_BUILD_MANIFEST', BUILD / 'build.json'))


def execute(argv, **kwargs):
    print('+', shlex.join(map(str, argv)), flush=True)
    proc = subprocess.Popen(list(map(str, argv)), start_new_session=True, **kwargs)
    try:
        rc = proc.wait(timeout=3600)
        if rc:
            raise subprocess.CalledProcessError(rc, argv)
        return rc
    except BaseException:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        proc.wait()
        raise


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(path, expected):
    if digest(path) != expected:
        raise ValueError(f'SHA-256 mismatch: {path}')


def extract(path, expected, directory):
    verify(path, expected)
    if directory.exists():
        return
    # Python >= 3.11 with security-backported tarfile.data_filter is required.
    if not hasattr(tarfile, 'data_filter'):
        raise RuntimeError('Python with tarfile.data_filter is required (3.12+ recommended)')
    with tarfile.open(path, 'r:gz') as archive:
        tops = {Path(m.name).parts[0] for m in archive.getmembers()}
        if tops != {directory.name}:
            raise ValueError(f'Unexpected archive root: {tops}')
        with tempfile.TemporaryDirectory(prefix='unpack-', dir=BUILD) as temp:
            archive.extractall(temp, filter='data')
            (Path(temp) / directory.name).replace(directory)


def command_version(command):
    try:
        p = subprocess.run([command, '--version'], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return p.stdout[:4000]
    except OSError as exc:
        return str(exc)


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    jobs = int(os.environ.get('JOBS', '4'))
    if jobs < 1:
        raise ValueError('JOBS must be positive')
    build_cc = os.environ.get('BUILD_CC', 'cc')
    compiler = str(BUILD / 'mpich/bin/mpicc')
    for tool in [build_cc, 'cc', 'make', 'ar', 'ranlib']:
        if not shutil.which(tool):
            raise RuntimeError(f'Missing {tool}; see README.md dependencies')
    target = os.environ.get('OPENBLAS_TARGET', 'auto')
    if target == 'auto':
        target = 'RISCV64_ZVL256B' if platform.machine() == 'riscv64' else 'GENERIC'
    if not re.fullmatch(r'[A-Z0-9_]+', target):
        raise ValueError('Invalid OPENBLAS_TARGET')
    flags = os.environ.get('HPL_CFLAGS') or '-O3'
    identity = {'openblas': '0.3.34', 'target': target,
                'hpl_sha256': HPL_SHA, 'openblas_sha256': BLAS_SHA,
                'mpicc': compiler, 'compiler_version': command_version(build_cc),
                'cc_version': command_version(build_cc), 'hpl_cflags': flags,
                'mpi': 'MPICH 4.3.2 ch3:sock, static', 'static_libc': False,
                'cross_host': os.environ.get('CROSS_HOST', ''),
                'blas_threads': 1, 'blas_integer_bits': 32}
    stamp = BUILD / 'build.json'
    if stamp.exists() and json.loads(stamp.read_text()) != identity:
        raise RuntimeError('Build settings/compiler changed. Run make clean, then make.')
    if BINARY.exists() and stamp.exists():
        print('Existing matching build:', BINARY)
        return
    # Record the configuration before compiling so a failed build cannot mix targets.
    stamp.write_text(json.dumps(identity, indent=2) + '\n')
    extract(ROOT / 'vendor/hpl-2.3.tar.gz', HPL_SHA, BUILD / 'hpl-2.3')
    build_mpi(build_cc, int(os.environ.get('JOBS', '4')))
    archive = Path(os.environ.get('OPENBLAS_ARCHIVE') or BUILD / 'OpenBLAS-0.3.34.tar.gz')
    if not archive.exists():
        if os.environ.get('OPENBLAS_ARCHIVE'):
            raise FileNotFoundError(archive)
        temp = archive.with_suffix('.download')
        print('Downloading pinned OpenBLAS release:', BLAS_URL, flush=True)
        with urllib.request.urlopen(BLAS_URL, timeout=120) as src, temp.open('wb') as dst:
            shutil.copyfileobj(src, dst)
        verify(temp, BLAS_SHA)
        temp.replace(archive)
    blas_src = BUILD / 'OpenBLAS-0.3.34'
    extract(archive, BLAS_SHA, blas_src)
    prefix = BUILD / 'openblas'
    jobs = int(os.environ.get('JOBS', '4'))
    if jobs < 1:
        raise ValueError('JOBS must be positive')
    options = [f'TARGET={target}', 'BINARY=64', 'INTERFACE64=0', f'CC={build_cc}',
               'HOSTCC=cc', f"AR={os.environ.get('AR', 'ar')}",
               f"RANLIB={(os.environ.get('RANLIB') or 'ranlib')}", 'USE_THREAD=0', 'USE_OPENMP=0', 'NO_AFFINITY=1',
               'NOFORTRAN=1', 'NO_SHARED=1', 'NO_LAPACK=1', 'NO_LAPACKE=1', 'DYNAMIC_ARCH=0']
    if not all((prefix/name).exists() for name in ['lib/libopenblas.a', 'include/cblas.h']):
        execute(['make', '-C', blas_src, f'-j{jobs}', *options, 'libs'])
        execute(['make', '-C', blas_src, *options, f'PREFIX={prefix}', 'install'])
    hpl_src = BUILD / 'hpl-2.3'
    config = f'''# Generated by benchmarks/hpl/hpl.py; do not edit.
SHELL = /bin/sh
CD = cd
CP = cp
LN_S = ln -s
MKDIR = mkdir -p
RM = /bin/rm -f
TOUCH = touch
ARCH = Jupiter2
TOPdir = {hpl_src}
INCdir = $(TOPdir)/include
BINdir = $(TOPdir)/bin/$(ARCH)
LIBdir = $(TOPdir)/lib/$(ARCH)
HPLlib = $(LIBdir)/libhpl.a
MPdir =
MPinc =
MPlib =
LAdir = {prefix}
LAinc = -I$(LAdir)/include
LAlib = $(LAdir)/lib/libopenblas.a -lm -lpthread
F2CDEFS =
HPL_INCLUDES = -I$(INCdir) -I$(INCdir)/$(ARCH) $(LAinc)
HPL_LIBS = $(HPLlib) $(LAlib) $(MPlib)
HPL_OPTS = -DHPL_CALL_CBLAS -DHPL_DETAILED_TIMING
HPL_DEFS = $(F2CDEFS) $(HPL_OPTS) $(HPL_INCLUDES)
CC = {compiler}
CCNOOPT = $(HPL_DEFS)
CCFLAGS = $(HPL_DEFS) {flags}
LINKER = $(CC)
LINKFLAGS = $(CCFLAGS) -static-mpi
ARCHIVER = {os.environ.get('AR', 'ar')}
ARFLAGS = r
RANLIB = {(os.environ.get('RANLIB') or 'ranlib')}
'''
    (hpl_src / 'Make.Jupiter2').write_text(config)
    # Upstream install prerequisites are not ordered for parallel make.
    execute(['make', '-C', hpl_src, 'startup', 'arch=Jupiter2'])
    execute(['make', '-C', hpl_src, f'-j{jobs}', 'build', 'arch=Jupiter2'])
    if not BINARY.is_file():
        raise RuntimeError('HPL did not produce xhpl')
    print('Built:', BINARY)


def build_mpi(cc, jobs):
    prefix = BUILD / 'mpich'
    if all((prefix/name).is_file() for name in
           ['lib/libmpi.a', 'bin/mpiexec', 'bin/hydra_pmi_proxy', 'include/mpi.h']):
        return
    archive = Path(os.environ.get('MPICH_ARCHIVE') or BUILD / 'mpich-4.3.2.tar.gz')
    if not archive.exists():
        if os.environ.get('MPICH_ARCHIVE'):
            raise FileNotFoundError(archive)
        temp = archive.with_suffix('.download')
        print('Downloading pinned MPI:', MPI_URL, flush=True)
        with urllib.request.urlopen(MPI_URL, timeout=120) as src, temp.open('wb') as dst:
            shutil.copyfileobj(src, dst)
        verify(temp, MPI_SHA)
        temp.replace(archive)
    source = BUILD / 'mpich-4.3.2'
    extract(archive, MPI_SHA, source)
    obj = BUILD / 'mpich-build'
    obj.mkdir(exist_ok=True)
    options = [str(source / 'configure'), f'--prefix={prefix}',
               '--with-device=ch3:sock', '--with-pm=hydra', '--with-hwloc=no',
               '--disable-fortran', '--disable-cxx', '--disable-romio',
               '--disable-shared', '--enable-static', '--disable-doc',
               f'CC={cc}', 'CFLAGS=-O2']
    cross = os.environ.get('CROSS_HOST')
    if cross:
        options += [f'--host={cross}', f'--build={platform.machine()}-pc-linux-gnu']
    if not (obj / 'Makefile').exists():
        execute(options, cwd=obj)
    execute(['make', f'-j{jobs}', 'LDFLAGS='], cwd=obj)
    execute(['make', 'install', 'LDFLAGS='], cwd=obj)


def cpu_numbers(spec):
    cpus = []
    for item in spec.split(','):
        if not re.fullmatch(r'\d+(?:-\d+)?', item):
            raise ValueError('CPU_LIST must be e.g. 0-7 or 0,2,4,6')
        lo, _, hi = item.partition('-')
        a, b = int(lo), int(hi or lo)
        if a > b or b > 65535:
            raise ValueError('Invalid CPU range')
        cpus.extend(range(a, b + 1))
    if len(cpus) != len(set(cpus)):
        raise ValueError('Duplicate CPUs')
    return cpus


def memory():
    info = {}
    for line in Path('/proc/meminfo').read_text().splitlines():
        name, value = line.split(':', 1)
        if name in ['MemTotal', 'MemAvailable']:
            info[name] = int(value.split()[0]) * 1024
    # Honor cgroup v2 constraints as well as the host's MemAvailable.
    group = Path('/sys/fs/cgroup')
    groups = [group]
    cg = Path('/proc/self/cgroup')
    if cg.exists():
        for line in cg.read_text().splitlines():
            if line.startswith('0::'):
                rel = line[3:].lstrip('/')
                if '..' not in Path(rel).parts:
                    path = group / rel
                    groups += [path, *[p for p in path.parents if p != group and group in p.parents]]
    for path in groups:
        try:
            limit = (path / 'memory.max').read_text().strip()
            if limit != 'max':
                used = int((path / 'memory.current').read_text())
                info['MemTotal'] = min(info['MemTotal'], int(limit))
                info['MemAvailable'] = min(info['MemAvailable'], max(0, int(limit) - used))
        except (OSError, ValueError):
            continue
    return info


def numroc(n, nb, rank, ranks):
    blocks, tail = divmod(n, nb)
    full, extra = divmod(blocks, ranks)
    return (full + (rank < extra)) * nb + (tail if rank == extra else 0)


def estimate(n, nb, p, q, depth=1):
    """HPL_pdtest allocation + conservative panel/MPI/BLAS overhead, bytes."""
    total = 0
    for row in range(p):
        mp = numroc(n, nb, row, p)
        ld = max(1, (max(1, mp) + 7) // 8) * 8
        while ld & (ld - 1) == 0:
            ld += 8
        for col in range(q):
            nq = numroc(n, nb, col, q)
            # Match HPL_pdtest.c: ALIGN + (ld+1)*(nq+1) doubles.
            matrix = 8 * (8 + (ld + 1) * (nq + 1))
            # Upper bound: two live panels, full L2 and U even when aliased.
            panel = 8 * (8 + mp * nb + nb * nb + nb + 1 + nb * (nq + 1))
            integer_work = 4 * (nb if p == 1 else 4 + 9*nb + 3*p + max(2*nb, p+1))
            total += matrix + (depth + 1) * (panel + integer_work) + 32 * 1024**2
    return total


def maximum_n(budget, nb, p, q):
    step = nb * math.lcm(p, q)
    lo, hi = 0, math.isqrt(max(0, budget) // 8) // step + 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if estimate(mid * step, nb, p, q) <= budget:
            lo = mid
        else:
            hi = mid
    return lo * step


def plan(smoke=False):
    p, q, nb = [int(os.environ.get(k, v)) for k, v in [('P', '2'), ('Q', '4'), ('NB', '192')]]
    cpus = cpu_numbers(os.environ.get('CPU_LIST', '0-7'))
    if min(p, q, nb) < 1 or p * q != len(cpus):
        raise ValueError('P * Q must equal the number of CPUs in CPU_LIST; P/Q/NB > 0')
    if p * q > 256 or nb > 4096:
        raise ValueError('Preset supports at most 256 ranks and NB <= 4096')
    mem = memory()
    cap = float(os.environ.get('RAM_GIB', '32'))
    reserve = float(os.environ.get('RESERVE_GIB', '2'))
    avail_reserve = float(os.environ.get('AVAILABLE_RESERVE_GIB', '1'))
    if not all(math.isfinite(v) and v >= 0 for v in [cap, reserve, avail_reserve]) or cap <= 0:
        raise ValueError('Memory settings must be finite nonnegative GiB; RAM_GIB > 0')
    total = min(mem['MemTotal'], int(cap * GIB))
    budget = min(total - int(reserve * GIB), mem['MemAvailable'] - int(avail_reserve * GIB))
    if budget <= 0:
        raise RuntimeError('Insufficient available memory after reservation')
    maximum = maximum_n(budget, nb, p, q)
    selected = '1536' if smoke else os.environ.get('N', 'auto')
    n = maximum if selected == 'auto' else int(selected)
    if not 1 <= n <= 2**31 - 1 or estimate(n, nb, p, q) > budget:
        raise ValueError(f'N does not fit the memory budget; automatic N={maximum}')
    return {'n': n, 'nb': nb, 'p': p, 'q': q, 'ranks': p*q, 'cpus': cpus,
            'depth': 1, 'alignment_doubles': 8, 'maximum_balanced_n': maximum,
            'matrix_bytes': 8*n*(n+1), 'estimated_bytes': estimate(n, nb, p, q),
            'budget_bytes': budget, 'memory': mem, 'ram_cap_gib': cap,
            'reserve_gib': reserve, 'available_reserve_gib': avail_reserve,
            'alignment_step': nb*math.lcm(p, q), 'smoke': smoke}


def dat(cfg):
    return f'''HPLinpack benchmark input file
Jupiter2 / K3; benchmarks/hpl/README.md documents the preset
HPL.out      output file name
6            output device: stdout
1            number of problem sizes
{cfg['n']}          Ns
1            number of block sizes
{cfg['nb']}            NBs
0            PMAP: row-major
1            number of grids
{cfg['p']}              Ps
{cfg['q']}              Qs
16.0         residual threshold
1            number of panel factorizations
2            PFACT: Right
1            number of recursive stopping criteria
4            NBMIN
1            number of panels in recursion
2            NDIV
1            number of recursive panel factorizations
1            RFACT: Crout
1            number of broadcasts
1            BCAST: 1ringM
1            number of lookahead depths
1            DEPTH
2            SWAP: mixed
64           swapping threshold
0            L1 transposed
0            U transposed
1            equilibration
8            alignment in doubles
'''


def worker(cpus, binary):
    names = ['OMPI_COMM_WORLD_LOCAL_RANK', 'MPI_LOCALRANKID', 'PMI_LOCAL_RANK', 'PMI_RANK']
    rank = next((os.environ[k] for k in names if k in os.environ), None)
    if rank is None or not rank.isdigit() or int(rank) >= len(cpus):
        raise RuntimeError('Cannot determine local MPI rank for CPU binding')
    cpu = cpus[int(rank)]
    os.sched_setaffinity(0, {cpu})
    if os.sched_getaffinity(0) != {cpu}:
        raise RuntimeError(f'CPU binding failed for rank {rank}')
    print(f'HPL_BIND local_rank={rank} cpu={cpu}', flush=True)
    os.execv(str(binary), [str(binary)])


def validate_output(output, cfg):
    rows = re.findall(r'^W\S+\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s*$', output, re.M)
    if len(rows) != 1:
        raise RuntimeError('Expected exactly one HPL result row')
    n, nb, p, q, seconds, gflops = rows[0]
    if list(map(int, [n, nb, p, q])) != [cfg[k] for k in ['n', 'nb', 'p', 'q']]:
        raise RuntimeError('HPL output parameters differ from requested parameters')
    residuals = re.findall(r'=\s*([\d.eE+-]+)\s+\.+\s+PASSED', output)
    if len(residuals) != 1 or not math.isfinite(float(residuals[0])) or not 0 <= float(residuals[0]) < 16:
        raise RuntimeError('Missing or invalid PASSED residual check')
    if not re.search(r'^\s*1\s+tests completed and passed residual checks', output, re.M):
        raise RuntimeError('Missing successful HPL summary')
    for label in ['failed residual checks', 'skipped because']:
        if not re.search(r'^\s*0\s+tests (?:completed and )?' + label, output, re.M):
            raise RuntimeError(f'HPL summary has {label}')
    values = [float(seconds), float(gflops)]
    if not all(math.isfinite(v) and v > 0 for v in values):
        raise RuntimeError('Invalid timing or GFLOP/s')
    bindings = re.findall(r'HPL_BIND local_rank=(\d+) cpu=(\d+)', output)
    if sorted((int(a), int(b)) for a, b in bindings) != list(enumerate(cfg['cpus'])):
        raise RuntimeError('MPI rank/CPU binding evidence is incomplete')
    return {'seconds': values[0], 'gflops': values[1], 'residual': float(residuals[0]), 'passed': True}


def run(smoke):
    if platform.machine() != 'riscv64' and os.environ.get('ALLOW_HOST') != '1':
        raise RuntimeError('Run on Jupiter2/riscv64; ALLOW_HOST=1 is for host validation only')
    cfg = plan(smoke)
    if not set(cfg['cpus']) <= os.sched_getaffinity(0):
        raise RuntimeError('Requested CPUs are outside current affinity; inspect taskset/cpuset')
    if not BINARY.exists():
        raise RuntimeError('Run make first')
    built = json.loads(BUILD_MANIFEST.read_text())
    if (built['target'] == 'RISCV64_ZVL256B' and
            not set(cfg['cpus']) <= set(range(8))):
        raise RuntimeError('The stock ZVL256B preset is X100-only (CPUs 0-7). '
                           'Do not run it on A100 or mix VLENs; see README.md.')
    launch = os.environ.get('MPIEXEC') or str(BUILD / 'mpich/bin/mpiexec')
    if not shutil.which(launch):
        raise RuntimeError(f'Missing MPI launcher: {launch}')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    directory = ROOT / 'results' / stamp
    directory.mkdir(parents=True)
    (directory / 'HPL.dat').write_text(dat(cfg))
    (directory / 'plan.json').write_text(json.dumps(cfg, indent=2) + '\n')
    manifest = {'uname': platform.uname()._asdict(), 'binary_sha256': digest(BINARY),
                'build': json.loads(BUILD_MANIFEST.read_text()),
                'mpi_version': command_version(launch), 'status': 'started'}
    cp = subprocess.run(['lscpu'], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    manifest['lscpu'] = cp.stdout
    for path in ['policy0', 'policy8']:
        base = Path('/sys/devices/system/cpu/cpufreq') / path
        manifest[path] = {name: (base/name).read_text().strip() for name in
                          ['scaling_governor', 'scaling_cur_freq', 'scaling_max_freq']
                          if (base/name).exists()}
    env = os.environ.copy()
    env.update({'OPENBLAS_NUM_THREADS': '1', 'OMP_NUM_THREADS': '1',
                'GOTO_NUM_THREADS': '1', 'OPENBLAS_MAIN_FREE': '1'})
    args = [launch, '-n', str(cfg['ranks'])]
    if 'HYDRA' in manifest['mpi_version']:
        args += ['-launcher', 'fork', '-hosts', 'localhost']
    elif any(s in manifest['mpi_version'] for s in ['Open MPI', 'OpenRTE', 'PRRTE']):
        raise RuntimeError('Use the bundled MPICH launcher; Open MPI is not ABI-compatible')
    args += [sys.executable, str(Path(__file__).resolve()), 'worker',
             ','.join(map(str, cfg['cpus'])), str(BINARY)]
    manifest['command'] = args
    record = directory / 'manifest.json'
    record.write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(cfg, indent=2), '\nResults:', directory, flush=True)
    try:
        with (directory / 'HPL.log').open('w') as log:
            proc = subprocess.Popen(args, cwd=directory, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                for line in proc.stdout:
                    print(line, end='', flush=True)
                    log.write(line)
                rc = proc.wait()
            except BaseException:
                proc.terminate()
                proc.wait()
                raise
        if rc:
            raise RuntimeError(f'MPI exited with status {rc}')
        result = validate_output((directory / 'HPL.log').read_text(), cfg)
        (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        manifest['status'] = 'passed'
        print('PASS:', result)
    except BaseException as exc:
        manifest['status'] = 'failed'
        manifest['error'] = str(exc)
        raise
    finally:
        record.write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'plan', 'config', 'run', 'worker', 'doctor', 'clean'])
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('worker_args', nargs='*')
    args = parser.parse_args()
    if args.action == 'build':
        build()
    elif args.action in ['plan', 'config']:
        cfg = plan()
        print(json.dumps(cfg, indent=2))
        if args.action == 'config':
            directory = BUILD / 'run'
            directory.mkdir(parents=True, exist_ok=True)
            (directory / 'HPL.dat').write_text(dat(cfg))
            (directory / 'plan.json').write_text(json.dumps(cfg, indent=2) + '\n')
    elif args.action == 'run':
        run(args.smoke)
    elif args.action == 'worker':
        if len(args.worker_args) != 2:
            raise ValueError('worker requires CPU list and binary path')
        worker(cpu_numbers(args.worker_args[0]), Path(args.worker_args[1]))
    elif args.action == 'doctor':
        for cmd in ['python3', os.environ.get('BUILD_CC') or 'cc', 'make']:
            print(cmd, ':', command_version(cmd).splitlines()[0])
        print('machine:', platform.machine(), '\nallowed CPUs:', sorted(os.sched_getaffinity(0)))
        print('memory:', json.dumps(memory()))
    elif args.action == 'clean':
        if BUILD.is_dir():
            shutil.rmtree(BUILD)
        print('Removed .build only; vendor sources and results are preserved')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print('ERROR:', error, file=sys.stderr)
        sys.exit(1)
