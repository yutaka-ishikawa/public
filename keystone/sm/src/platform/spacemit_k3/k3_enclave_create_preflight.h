/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_ENCLAVE_CREATE_PREFLIGHT_H
#define K3_ENCLAVE_CREATE_PREFLIGHT_H
#include <stdint.h>
#include <stddef.h>

/* Side-effect-free validation. Necessary but insufficient for enclave
 * admission: region must separately be available, owned, protected on all
 * harts and excluded from DMA. No user-provided 'verified' flag is accepted. */
/* Reject a caller-provided private or shared region that intersects the
 * OpenSBI+SM firmware image range. Caller must first validate lengths and address
 * overflow; this routine independently checks them for safe standalone use.
 * An empty firmware image range (size zero) is NOT an authorization. */
static inline int k3_enclave_create_excludes_sm(uintptr_t epm, size_t epmsz,
                                                 uintptr_t utm, size_t utmsz,
                                                 uintptr_t smbase, size_t smsize)
{
    uintptr_t epmend, utmend, smend;
    if (!epmsz || !utmsz || !smsize ||
        epm > UINTPTR_MAX - epmsz ||
        utm > UINTPTR_MAX - utmsz ||
        smbase > UINTPTR_MAX - smsize)
        return 0;
    epmend = epm + epmsz;
    utmend = utm + utmsz;
    smend = smbase + smsize;
    return (epmend <= smbase || smend <= epm) &&
           (utmend <= smbase || smend <= utm);
}

static inline int k3_enclave_create_layout_valid(uintptr_t epm, size_t epmsz,
                                                uintptr_t utm, size_t utmsz,
                                                uintptr_t runtime,
                                                uintptr_t user,
                                                uintptr_t free_start)
{
    const uintptr_t page = 4096;
    uintptr_t epm_end, utm_end;
    if (!epmsz || !utmsz || (epm & (page-1)) || (utm & (page-1)) ||
        (epmsz & (page-1)) || (utmsz & (page-1)))
        return 0;
    if (epm > UINTPTR_MAX - epmsz || utm > UINTPTR_MAX - utmsz)
        return 0;
    epm_end = epm + epmsz;
    utm_end = utm + utmsz;
    if (!(epm_end <= utm || utm_end <= epm))
        return 0;
    if (runtime < epm || runtime >= epm_end ||
        user < runtime || user >= epm_end ||
        free_start < user || free_start > epm_end)
        return 0;
    return 1;
}
#endif
