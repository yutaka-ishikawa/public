#!/usr/bin/env python3
"""Exercise OpenSBI's actual reset function with terminal device mocks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def extract_function(source, signature):
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


class SystemResetTests(unittest.TestCase):
    def test_actual_reset_with_and_without_keystone(self):
        source = (ROOT / 'opensbi/lib/sbi/sbi_system.c').read_text()
        reset = extract_function(source, 'void __noreturn sbi_system_reset(')
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define __noreturn __attribute__((noreturn))
typedef unsigned int u32;
struct sbi_domain { bool system_reset_allowed; };
struct sbi_scratch { int unused; };
struct sbi_system_reset_device { void (*system_reset)(u32,u32); };
static struct sbi_domain domain;
static struct sbi_scratch scratch;
static jmp_buf done;
static char events[32];
static int n, device_available, device_returns;
static u32 seen_type, seen_reason;
static void record(char c) { events[n++]=c; events[n]=0; }
static struct sbi_domain *sbi_domain_thishart_ptr(void) { return &domain; }
static struct sbi_scratch *sbi_scratch_thishart_ptr(void) { return &scratch; }
static void device_reset(u32 type,u32 reason) {
 seen_type=type;seen_reason=reason;record('R');
 if(!device_returns) longjmp(done,1);
}
static struct sbi_system_reset_device device={device_reset};
static const struct sbi_system_reset_device *sbi_system_reset_get_device(u32 type,u32 reason) {
 (void)type;(void)reason;record('D');return device_available?&device:NULL;
}
static void sbi_printf(const char *format,...) { (void)format;record('L'); }
static void __noreturn sbi_hart_hang(void) { record('H');longjmp(done,1); }
static int sbi_ipi_send_halt(unsigned long mask,unsigned long base) {
 assert(mask==0 && base==~0UL);record('I');return 0;
}
static int sbi_hsm_hart_stop(struct sbi_scratch *s,bool exitnow) {
 assert(s==&scratch && !exitnow);record('S');return -4;
}
static void __noreturn sbi_exit(struct sbi_scratch *s) {
 assert(s==&scratch);record('E');longjmp(done,1);
}
'''
        code += reset
        code += r'''
static void exercise(bool allowed,int present,int returns,u32 type,u32 reason,const char *expected) {
 domain.system_reset_allowed=allowed;device_available=present;device_returns=returns;
 n=0;events[0]=0;seen_type=99;seen_reason=99;
 if(!setjmp(done)) sbi_system_reset(type,reason);
 assert(!strcmp(events,expected));
 if(strchr(events,'R')) assert(seen_type==type && seen_reason==reason);
}
int main(void) {
 for(u32 type=0;type<3;type++) for(u32 reason=0;reason<2;reason++) {
#ifdef CONFIG_K3_KEYSTONE_SM
  /* No HALT IPI, HSM stop or exit; denial/missing/returning device never returns. */
  exercise(true,1,0,type,reason,"DR");
  exercise(true,1,1,type,reason,"DRLH");
  exercise(true,0,0,type,reason,"DLH");
  exercise(false,1,0,type,reason,"LH");
#else
  /* Existing non-Keystone reset order is preserved. */
  exercise(true,1,0,type,reason,"ISDR");
  exercise(true,1,1,type,reason,"ISDRE");
  exercise(true,0,0,type,reason,"ISDE");
  exercise(false,1,0,type,reason,"ISE");
#endif
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            path = tmp / 'reset.c'
            path.write_text(code)
            for keystone in (False, True):
                with self.subTest(keystone=keystone):
                    binary = tmp / ('keystone' if keystone else 'normal')
                    defines = ['-DCONFIG_K3_KEYSTONE_SM'] if keystone else []
                    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', *defines, str(path),
                                    '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)

    def test_driver_list_is_regenerated_after_config_change(self):
        # Execute all three actual carray rules and their generator. Previously
        # the existing reset driver list survived a defconfig update unchanged.
        makefile = (ROOT / 'opensbi/Makefile').read_text()
        begin = makefile.index('compile_carray =')
        macro = makefile[begin:makefile.index('compile_gen_dep =', begin)]
        rules = []
        lines = makefile.splitlines()
        for i, line in enumerate(lines):
            if '/%.carray.c:' in line:
                rules.append(line + '\n' + lines[i + 1] + '\n')
        self.assertEqual(len(rules), 3)
        for rule in rules:
            with self.subTest(rule=rule.splitlines()[0]), tempfile.TemporaryDirectory() as directory:
                tmp = Path(directory)
                header = tmp / 'config.h'
                header.write_text('/* old config */\n')
                output = tmp / 'out/lib/utils/reset/fdt_reset_drivers.carray.c'
                fixture = tmp / 'Makefile'
                fixture.write_text(
                    f'src_dir := {ROOT / "opensbi"}\n'
                    f'platform_src_dir := {ROOT / "opensbi"}\n'
                    f'build_dir := {tmp / "out"}\n'
                    f'platform_build_dir := {tmp / "out"}\n'
                    f'KCONFIG_AUTOHEADER := {header}\n'
                    'carray-fdt_reset_drivers-y := $(DRIVERS)\n' + macro + rule)
                command = ['make', '-s', '-f', str(fixture), str(output)]
                subprocess.run(command + ['DRIVERS=fdt_reset_htif'], check=True)
                self.assertNotIn('fdt_syscon_poweroff', output.read_text())
                header.write_text('/* syscon enabled */\n')
                stamp = output.stat().st_mtime_ns + 1
                os.utime(header, ns=(stamp, stamp))
                subprocess.run(command + ['DRIVERS=fdt_reset_htif fdt_syscon_poweroff'], check=True)
                self.assertIn('&fdt_syscon_poweroff,', output.read_text())

    def test_keystone_config_enables_qemu_reset_device(self):
        config = (ROOT / 'opensbi/platform/generic/configs/k3_keystone_audit_defconfig').read_text().splitlines()
        for name in ('FDT_RESET', 'FDT_REGMAP', 'FDT_REGMAP_SYSCON', 'FDT_RESET_SYSCON'):
            self.assertIn('CONFIG_' + name + '=y', config)


if __name__ == '__main__':
    unittest.main()
