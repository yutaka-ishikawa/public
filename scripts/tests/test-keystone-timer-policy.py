#!/usr/bin/env python3
"""Compile the production K3 timer policy against real libfdt and mocked CSRs."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TimerPolicyTests(unittest.TestCase):
    def test_extensions_and_both_device_tree_isa_encodings(self):
        headers = {
            "sbi/riscv_asm.h": "#include <stdint.h>\nvoid observe_csr(unsigned long,unsigned long);\n#define csr_clear(c,v) observe_csr(c,v)\n",
            "sbi/riscv_encoding.h": "#define CSR_MENVCFG 0x30a\n#define ENVCFG_STCE (1UL << 63)\n",
            "sbi/sbi_bitops.h": "static inline int __test_bit(int n,const unsigned long *p){return !!(*p & (1UL<<n));}\nstatic inline void __clear_bit(int n,unsigned long *p){*p &= ~(1UL<<n);}\n",
            "sbi/sbi_hart.h": "#pragma once\n#include <stdbool.h>\n#define SBI_HART_EXT_SSTC 0\n#define SBI_HART_PRIV_VER_1_12 3\nstruct sbi_hart_features {int priv_version;unsigned long extensions[1];};\n",
            "sbi/sbi_error.h": "#define SBI_ENOMEM (-9)\n",
            "sbi/sbi_heap.h": "#include <stdlib.h>\n#define sbi_malloc malloc\n#define sbi_free free\n",
            "sbi/sbi_string.h": "#include <string.h>\n#define sbi_memcmp memcmp\n#define sbi_memcpy memcpy\n",
        }
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>
#include <spacemit/k3_keystone_timer.h>
static int csr_calls;
void observe_csr(unsigned long csr,unsigned long mask){assert(csr==0x30a && mask==(1UL<<63));csr_calls++;}
static int addcpu(void *dt,int cpus,const char *name) {
 int n=fdt_add_subnode(dt,cpus,name);assert(n>=0);assert(!fdt_setprop_string(dt,n,"device_type","cpu"));return n;
}
int main(void) {
 struct sbi_hart_features features={.priv_version=3,.extensions={3}};
 assert(!k3_keystone_timer_extensions(NULL,&features));assert(csr_calls==1 && features.extensions[0]==2);
 assert(!k3_keystone_timer_extensions(NULL,&features));assert(csr_calls==1 && features.extensions[0]==2);
 char dt[16384];assert(!fdt_create_empty_tree(dt,sizeof(dt)));
 int cpus=fdt_add_subnode(dt,0,"cpus");assert(cpus>=0);
 int n=addcpu(dt,cpus,"cpu@0");
 assert(!fdt_setprop_string(dt,n,"riscv,isa","rv64imafdc_sstc1p0_zicsr_zifencei"));
 static const char extensions[]="i\0sstc\0zicsr\0xsstc\0sstcx";
 assert(!fdt_setprop(dt,n,"riscv,isa-extensions",extensions,sizeof(extensions)));
 n=addcpu(dt,cpus,"cpu@1");assert(!fdt_setprop_string(dt,n,"riscv,isa","rv64imafdc_zicsr_sstc"));
 n=addcpu(dt,cpus,"cpu@2");assert(!fdt_setprop_string(dt,n,"riscv,isa-extensions","sstc"));
 n=fdt_add_subnode(dt,cpus,"clock");assert(n>=0);
 assert(!fdt_setprop_string(dt,n,"device_type","timer"));assert(!fdt_setprop_string(dt,n,"riscv,isa","sstc"));
 assert(!k3_keystone_timer_fdt_fixup(dt,NULL));
 int len;n=fdt_path_offset(dt,"/cpus/cpu@0");assert(n>=0);
 const char *isa=fdt_getprop(dt,n,"riscv,isa",&len);assert(isa && !strcmp(isa,"rv64imafdc_zicsr_zifencei"));
 assert(fdt_stringlist_search(dt,n,"riscv,isa-extensions","sstc")==-FDT_ERR_NOTFOUND);
 assert(fdt_stringlist_search(dt,n,"riscv,isa-extensions","xsstc")>=0);
 assert(fdt_stringlist_search(dt,n,"riscv,isa-extensions","sstcx")>=0);
 assert(fdt_stringlist_search(dt,n,"riscv,isa-extensions","zicsr")>=0);
 n=fdt_path_offset(dt,"/cpus/cpu@1");isa=fdt_getprop(dt,n,"riscv,isa",&len);assert(!strcmp(isa,"rv64imafdc_zicsr"));
 n=fdt_path_offset(dt,"/cpus/clock");isa=fdt_getprop(dt,n,"riscv,isa",&len);assert(!strcmp(isa,"sstc"));
 assert(!k3_keystone_timer_fdt_fixup(dt,NULL)); /* Idempotent, including empty lists. */
 n=fdt_path_offset(dt,"/cpus/cpu@1");assert(!fdt_setprop(dt,n,"riscv,isa","bad",3));
 assert(k3_keystone_timer_fdt_fixup(dt,NULL)==-FDT_ERR_BADVALUE);
 puts("Sstc access policy and legacy/list DT advertisements: passed");
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            for name, contents in headers.items():
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            source = temp / "test.c"
            binary = temp / "test"
            source.write_text(code)
            # Optional paths allow testing with user-local Debian development packages.
            includes = ["-I" + str(temp), "-I" + str(ROOT / "opensbi/platform/generic/include")]
            libraries = []
            if os.environ.get("K3_TEST_SYSROOT"):
                prefix = Path(os.environ["K3_TEST_SYSROOT"])
                includes += ["-I" + str(prefix / "usr/include")]
                libraries += ["-L" + str(prefix / "usr/lib/x86_64-linux-gnu")]
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-D__riscv_xlen=64",
                            *includes, str(ROOT / "opensbi/platform/generic/spacemit/k3_keystone_timer.c"),
                            str(source), *libraries, "-lfdt", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
