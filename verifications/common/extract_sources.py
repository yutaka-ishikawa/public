#!/usr/bin/env python3
"""Extract exact production functions, never maintain copies of their logic."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INPUTS = [
    'keystone/sm/src/enclave.c', 'keystone/sm/src/enclave.h',
    'keystone/sm/src/thread.c', 'keystone/sm/src/thread.h',
    'keystone/sm/src/sm-sbi.c', 'keystone/sm/src/sm-sbi-opensbi.c',
    'keystone/sm/src/pmp.h', 'keystone/sm/src/sm.h',
    'keystone/sm/src/sm-sbi.h', 'keystone/sm/src/cpu.h',
    'keystone/sm/src/crypto.h', 'keystone/sm/src/sha3/sha3.h',
    'keystone/sm/src/ed25519/ed25519.h', 'keystone/sm/src/hkdf_sha3_512/hkdf_sha3_512.h',
    'opensbi/include/sbi/riscv_encoding.h', 'opensbi/include/sbi/sbi_const.h',
    'keystone/sm/src/platform/spacemit_k3/platform.h',
    'keystone/sm/src/platform/spacemit_k3/k3_enclave_create_preflight.h',
    'keystone/sdk/include/shared/sm_call.h', 'keystone/sdk/include/shared/sm_err.h',
    'opensbi/lib/sbi/sbi_ecall.c', 'opensbi/include/sbi/sbi_trap.h',
    'opensbi/include/sbi/sbi_ecall_interface.h', 'opensbi/include/sbi/sbi_error.h',
]


def block(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise ValueError(signature)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    out = args.output
    (out / 'include/sbi').mkdir(parents=True, exist_ok=True)
    trap = block((ROOT / 'opensbi/include/sbi/sbi_trap.h').read_text(), 'struct sbi_trap_regs {')
    (out / 'include/sbi/sbi_trap.h').write_text('#pragma once\n' + trap + ';\n')
    (out / 'include/sbi/sbi_types.h').write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef unsigned long ulong;
typedef uint32_t u32;
typedef uint64_t u64;
#define __packed __attribute__((packed))
''')
    (out / 'include/sbi/riscv_atomic.h').write_text('#pragma once\ntypedef struct { long value; } atomic_t;\n')
    selected = {
        'keystone/sm/src/thread.c': [
            'void swap_prev_smode_csrs(', 'void swap_prev_mstatus(', 'void swap_prev_state(',
            'void swap_prev_mepc(', 'void clean_smode_csrs(', 'void clean_state('],
        'keystone/sm/src/enclave.c': [
            'void enclave_init_metadata(', 'static unsigned long clean_enclave_memory(',
            'static unsigned long encl_alloc_eid(', 'static unsigned long encl_free_eid(',
            'int get_enclave_region_index(', 'unsigned long copy_enclave_create_args(',
            'static int is_create_args_valid(', 'unsigned long create_enclave(',
            'unsigned long destroy_enclave(', 'static inline unsigned long context_switch_to_enclave(',
            'static inline void context_switch_to_host(',
            'unsigned long run_enclave(', 'unsigned long resume_enclave(',
            'unsigned long stop_enclave(', 'unsigned long exit_enclave('],
        'keystone/sm/src/sm-sbi.c': [
            'unsigned long sbi_sm_create_enclave(', 'unsigned long sbi_sm_destroy_enclave(',
            'unsigned long sbi_sm_run_enclave(', 'unsigned long sbi_sm_resume_enclave(',
            'unsigned long sbi_sm_stop_enclave(', 'unsigned long sbi_sm_exit_enclave(',
            'unsigned long sbi_sm_random(', 'unsigned long sbi_sm_attest_enclave(',
            'unsigned long sbi_sm_get_sealing_key(', 'unsigned long sbi_sm_call_plugin('],
        'keystone/sm/src/sm-sbi-opensbi.c': [
            'static int keystone_ecall_return(', 'static int sbi_ecall_keystone_enclave_handler('],
        'opensbi/lib/sbi/sbi_ecall.c': ['int sbi_ecall_handler('],
    }
    result = []
    for path, signatures in selected.items():
        source = (ROOT / path).read_text()
        for signature in signatures:
            line = source[:source.index(signature)].count('\n') + 1
            result.append(f'#line {line} "{path}"\n' + block(source, signature))
    (out / 'production.inc').write_text('\n\n'.join(result) + '\n')
    (out / 'source-manifest.json').write_text(json.dumps({
        'inputs': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in INPUTS},
        'functions': selected,
        'scope': 'actual C functions; hardware, PMP transport, hashing and locking mocked',
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
