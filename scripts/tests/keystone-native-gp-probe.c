/* SPDX-License-Identifier: BSD-2-Clause */
/* SDK entry CPU probe under QEMU user mode, not an SM/SBI verification. */
static volatile unsigned long value = 31;

void EAPP_RETURN(unsigned long status) __attribute__((noreturn));

void EAPP_RETURN(unsigned long status)
{
    register unsigned long a0 __asm__("a0") = status;
    register unsigned long a7 __asm__("a7") = 93;

    /* Linux exit terminates this standalone CPU probe. No SBI is emulated. */
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    __builtin_unreachable();
}

int main(void)
{
    unsigned long actual;
    unsigned long expected;

    __asm__ volatile("mv %0, gp\n"
		     ".option push\n"
		     ".option norelax\n"
		     "lla %1, __global_pointer$\n"
		     ".option pop\n"
		     : "=&r"(actual), "=&r"(expected)
		     :
		     : "memory");
    if (actual != expected) {
	EAPP_RETURN(41);
    }
    if (value != 31) {
	return 42;
    }
    value = 32;
    return value == 32 ? 0 : 43;
}
