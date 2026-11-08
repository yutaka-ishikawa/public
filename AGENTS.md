# Coding style

The user has requested K&R style for all future C/C++ work in this repository.
Follow the root `.clang-format` and the approved verification example:

- Use a 4-column indentation step and an 8-column tab width. The first level
  uses four spaces; the second level uses one tab; the third uses a tab plus
  four spaces. Use tabs where an entire 8-column tab stop fits.
- Put the opening brace of a function definition on the next line.
- Put opening braces for `if`, `else`, `for`, `while`, and `switch` on the same line.
- Always use braces around `for`, `if`, and `else` bodies, even for one
  statement. Apply the same rule to other loop bodies. Never collapse the
  braced body onto a single line.
- Put each statement on its own line and use spaces around operators and after commas.
- Declare local variables at the beginning of their block. When moving a declaration,
  keep any initializer with side effects at its original execution point.
- Separate logical test cases with blank lines and wrap long lines near 80 columns.
- Keep modern function prototypes (for example, `int main(void)`); K&R refers to
  layout, not obsolete argument declarations.

Apply this style to newly written or modified C/C++ code. Do not reformat unrelated
vendor code as part of another task. Python, shell, and Makefiles should retain their
language-appropriate indentation; embedded C definitions should follow the C style.

Verification tests now run through the actual K3 QEMU guest, OpenSBI and SM.
Do not add mocked SBI/PMP/CSR tests or extract production functions into tests.
Build with `make -C verifications guest` and `make -C verifications kernel-modules`.
Run `bash verifications/run-all.sh` only inside the system guest. If a complete
matching kernel or a running guest is unavailable, report integration execution
as unverified; compilation alone is not a guest PASS.

# Keystone bug report maintenance

Maintain [doc/keystone-bug-report.md](doc/keystone-bug-report.md) as the ongoing
Keystone-related bug register. When a new SDK, Eyrie, SM, driver or integration
bug is discovered, add an entry even before fixing it. Use the next stable
KSB-NNN ID; do not renumber or reuse existing IDs. Record affected sources,
conditions, evidence, origin/attribution, impact, fix status and verification
limits. Distinguish confirmed defects from portability constraints or open
questions; do not attribute port-specific bugs to upstream without evidence.
When fixing a registered bug or receiving new verification results, update the
same entry with the fix commit and the actual scope of validation. Keep the
summary table and related detail documents consistent. Do not claim a build or
a host-side check as a QEMU integration PASS, and do not include secret key data.
