#!/usr/bin/env python3
"""Compile all real Keystone SM translation units against a chosen K3 OpenSBI.

Compile/link checking is not an enclave integration test. The unavailable
provider has no keys; explicit qemu-dev builds include extractable development
material and do not provide hardware endorsement.
"""
import argparse
import pathlib
import subprocess
import sys

SOURCES = [
    'security/security.c', 'rng/rng.c', 'attest.c', 'cpu.c', 'crypto.c', 'enclave.c', 'ipi.c',
    'k3_pmp_ipi_transport.c', 'pmp.c', 'sm.c', 'sm-sbi.c',
    'sm-sbi-opensbi.c', 'sbi_trap_hack.c', 'thread.c',
    'plugins/multimem.c', 'plugins/plugins.c',
    'sha3/sha3.c', 'ed25519/fe.c', 'ed25519/ge.c',
    'ed25519/keypair.c', 'ed25519/sc.c', 'ed25519/sign.c',
    'hkdf_sha3_512/hkdf_sha3_512.c', 'hmac_sha3/hmac_sha3.c',
    'mprv.S', 'trap.S', 'platform/spacemit_k3/platform.c',
    'platform/spacemit_k3/k3_dev_embedded_key.c',
    'platform/spacemit_k3/k3_b1_handoff.c',
]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--opensbi', type=pathlib.Path, required=True)
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--compiler', default='clang')
    p.add_argument('--dev-embedded-key', action='store_true',
                   help='Diagnostic only: compile with build-time-generated dev key. Never production.')
    p.add_argument("--trace", action="store_true", help="Enable SM progress markers for hang diagnosis")
    p.add_argument("--rng", choices=["auto", "zkr", "jupiter2", "counter"], default="auto")
    p.add_argument("--crypto", choices=["unavailable", "qemu-dev"], default="unavailable")
    a=p.parse_args()
    sm=pathlib.Path(__file__).resolve().parents[1]/'src'
    a.output.mkdir(parents=True,exist_ok=True)
    flags=['--target=riscv64-unknown-elf','-march=rv64imafdc_zicsr_zifencei',
           '-mabi=lp64','-mcmodel=medany','-fPIE','-ffreestanding','-fno-builtin',
           '-fno-stack-protector','-D__riscv_xlen=64',
           # SM is linked into OpenSBI; no standalone SM address macros.
           '-DKEYSTONE_K3_PMP_N_REG=16',
           '-DK3_QEMU_PMP16=1',
           '-DTARGET_PLATFORM_HEADER="platform/spacemit_k3/platform.h"',
           '-I'+str(sm),'-I'+str(a.opensbi/'include'),
           '-I'+str(a.opensbi/'lib/utils/libfdt'),
           '-I'+str(a.opensbi/'platform/generic/include'),
           '-I'+str(sm.parent/'../sdk/include'),'-I'+str(sm.parent.parent/'sdk/include/shared'),'-Wno-unused-function']
    flags.append("-DKEYSTONE_RNG_BACKEND=" + str(["auto", "zkr", "jupiter2", "counter"].index(a.rng)))
    flags.append("-I" + str(a.output))
    if a.crypto == "qemu-dev":
        flags.append("-DKEYSTONE_CRYPTO_QEMU_DEV=1")
    import os
    if os.environ.get("KEYSTONE_MULTIMEM", "1") == "1":
        flags.append("-DPLUGIN_ENABLE_MULTIMEM=1")
    elif os.environ.get("KEYSTONE_MULTIMEM") != "0":
        raise SystemExit("KEYSTONE_MULTIMEM must be 0 or 1")
    if a.trace:
        flags.append("-DKEYSTONE_SM_TRACE=1")
    if a.dev_embedded_key:
        flags.append('-DKEYSTONE_K3_DEV_EMBED_KEY=1')
    failed=[]
    for src in SOURCES:
        s=sm/src
        o=a.output/(src.replace('/','__').rsplit('.',1)[0]+'.o')
        o.parent.mkdir(parents=True,exist_ok=True)
        cmd=[a.compiler,*flags,'-c',str(s),'-o',str(o)]
        r=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        print(f'{"PASS" if r.returncode==0 else "FAIL"} {src}',flush=True)
        if r.stdout: print(r.stdout[-6000:],flush=True)
        if r.returncode: failed.append(src)
    print(f'RESULT: {len(SOURCES)-len(failed)}/{len(SOURCES)} translation units compiled; failures={failed}')
    return 1 if failed else 0
if __name__=='__main__': sys.exit(main())
