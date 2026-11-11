# OpenSBIバックポート候補・全件台帳

調査基準、件数の意味、試験・段階計画は[比較文書](opensbi-upstream-delta-2026-11-11.md)を参照。

対象：上流v1.6→`ed70256dd22c09962d146e433834239825ef0616`の501 commitを確認し、修正・防御強化として分類したもの。
機能追加後に必要な修正も含む。これは現行版で再現済みの不具合件数ではない。全commitは[CSV](opensbi-upstream-commit-inventory.csv)に記録する。

判定の「バックポート対象」は実装計画への登録であり、適用・動作検証済みではない。
`git apply --check`は原patchの機械的適用確認に限り、依存充足や正しさの証明にはしない。
条件付きは先行機能を導入する時に同梱し、対象外はK3へ適用しない。

## BP01：MPXY

優先度：P0。試験：`T-MPXY`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [30437eb2](https://github.com/riscv-software-src/opensbi/commit/30437eb2042c163bd1dc2870ba239514230b7233) / v1.7 | lib: sbi: Fix capability bit assignment in MPXY framework | **取込済**：SDK e522eba9・逆適用確認 |
| [ce57cb57](https://github.com/riscv-software-src/opensbi/commit/ce57cb572e3dec5ae0cf122e5f1e71f3d8e647ff) / v1.7 | lib: sbi: Add parameter check in sbi_mpxy_set_shmem() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [8b026abc](https://github.com/riscv-software-src/opensbi/commit/8b026abc5a9f05f60897e63bb6ff3e2aee32dfa9) / v1.7 | lib: sbi: Fix SHMEM_PHYS_ADDR for RV32 | **条件付きバックポート**：RV32向け。対象K3はRV64 |
| [2bb76326](https://github.com/riscv-software-src/opensbi/commit/2bb76326496440996b44bf20f3ceab127c7f72ae) / v1.7 | lib: utils: Fix fdt_mpxy_init() not returning error code | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [51fe6a8b](https://github.com/riscv-software-src/opensbi/commit/51fe6a8bc958166ff79805cf69bafe5e297776f4) / v1.8 | lib: utils: Use SBI_DOMAIN_MMIO to check MMIO device permissions | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [ae8f28a8](https://github.com/riscv-software-src/opensbi/commit/ae8f28a868513a76e0934dbd2a0a852c295c7051) / v1.9 | lib: utils: Fix LLVM compile error in MPXY client driver for RPMI MM | **条件付きバックポート**：現行にRPMI MM driverなし |
| [e92c8fd0](https://github.com/riscv-software-src/opensbi/commit/e92c8fd0836eaaea8e2e0126bd09ac16c24beee0) / v1.9 | sbi: mpxy: define INVALID_ADDR using unsigned long width | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [c175c97a](https://github.com/riscv-software-src/opensbi/commit/c175c97a2774318145dcc16ebb6d39f635002489) / v1.9 | lib: sbi: Fix LLVM compile error observed in sbi_mpxy.c | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [c0f87f10](https://github.com/riscv-software-src/opensbi/commit/c0f87f10d1bfb9e72a84ddfafb5604ee1bfe9d04) / v1.9後master | lib: utils/mpxy: bind channel access to owning domain | **条件付きバックポート**：channel owner導入時に全K3/OPTEED driverのowner設定が必要 |
| [eb6099e7](https://github.com/riscv-software-src/opensbi/commit/eb6099e7aab05495bc7b73f61a5c8e89b24f6043) / v1.9後master | lib: utils/mpxy: mm: Add missing RPMI_MM_SRV_ENABLE_NOTIFICATION entry | **条件付きバックポート**：RPMI MM driver導入時 |
| [c5e90d42](https://github.com/riscv-software-src/opensbi/commit/c5e90d4286211e5a68c157f435f12745705dfa51) / v1.9後master | lib: utils/mpxy: logging: Use correct enum for enable notification | **条件付きバックポート**：RPMI logging driver導入時 |
| [2de7dfb6](https://github.com/riscv-software-src/opensbi/commit/2de7dfb6813facba8e376897290bedb18ef38401) / v1.9後master | lib: sbi_mpxy: Fix shared memory size rounding | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [2afa7775](https://github.com/riscv-software-src/opensbi/commit/2afa77759241d425a6130e6a52560ef6fa2a6dac) / v1.9後master | lib: sbi_mpxy: fix integer overflow in attribute range endpoint | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP02：PMU

優先度：P0/P1。試験：`T-PMU`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [3943ddba](https://github.com/riscv-software-src/opensbi/commit/3943ddbaab00247b391128ae0a1f9258d4007f4f) / v1.7 | lib: sbi: pmu: fix usage of sbi_pmu_irq_bit() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [5c7e2c83](https://github.com/riscv-software-src/opensbi/commit/5c7e2c8334325e9e1e2e0541863812146efacb0d) / v1.7 | lib: sbi: pmu: add the PMU SSE event only if overflow IRQ is supported | **条件付きバックポート**：動的SSE event追加93f7d819が前提 |
| [69a0f024](https://github.com/riscv-software-src/opensbi/commit/69a0f0245f39ea3af4685cab4cb2dda90acd17cd) / v1.7 | lib: sbi: pmu: Return SBI_EINVAL if cidx_mask is 0 | **取込済**：SDK 8bd2cbdf・現行mask=0拒否確認 |
| [f30a54f3](https://github.com/riscv-software-src/opensbi/commit/f30a54f3b3a091c225a00476f4039bf399badd1f) / v1.7 | lib: sbi: pmu: Remove MIP clearing from pmu_sse_enable() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [1db95da2](https://github.com/riscv-software-src/opensbi/commit/1db95da2997b80f78c4b488a731801edc8c6379b) / v1.8 | lib: sbi: sbi_pmu: fixed hw counters start for hart | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [63aacbd7](https://github.com/riscv-software-src/opensbi/commit/63aacbd7826c539a46c108d0e055db57cb335eb2) / v1.8 | lib: sbi: sbi_pmu: fixed alignment | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [ac16c6b6](https://github.com/riscv-software-src/opensbi/commit/ac16c6b604961525bb096c0513c6ad4dbf5a5695) / v1.8 | lib: sbi: sbi_pmu: added checks for ctr_idx in match | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [c9f856e2](https://github.com/riscv-software-src/opensbi/commit/c9f856e23f61e7ccbd7d8414bb98c45a5f4f04f4) / v1.8 | lib: sbi_pmu: Fix xINH bits configuring | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f7b81034](https://github.com/riscv-software-src/opensbi/commit/f7b81034aa4c19199afac6be4e033270f3755bf5) / v1.8 | lib: sbi_pmu: Fix multiple start and stop operations of FW counters | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [a95c36f1](https://github.com/riscv-software-src/opensbi/commit/a95c36f16566213c5c58937b1f1b0c8209e60dbd) / v1.9 | lib: sbi_pmu: Fix multiple FW counter start operations with custom PMU device | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [9656943b](https://github.com/riscv-software-src/opensbi/commit/9656943bd3a2cad9418465c2a168f907ea52189b) / v1.9 | lib: sbi_pmu: Add FW counter index validation when reading high bits on RV64 | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [65bb705f](https://github.com/riscv-software-src/opensbi/commit/65bb705f7b59d8f2a5683160f31db15b4e2c64d7) / v1.9 | lib/sbi_pmu: Don't fallback to fixed counters when sscofpmf && !smcntrpmf | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [3d29f380](https://github.com/riscv-software-src/opensbi/commit/3d29f380a66c2b5608faf7e4f4b0ff3c945d0d64) / v1.9 | lib: sbi_pmu: Honor CLEAR_VALUE/AUTO_START for all hardware event types | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [0dfe6be0](https://github.com/riscv-software-src/opensbi/commit/0dfe6be08778102bf2fde61c5c99ea2cceb0b73a) / v1.9 | lib: sbi_pmu: fix integer overflow in pmu_ctr_idx_validate | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [d97cfb33](https://github.com/riscv-software-src/opensbi/commit/d97cfb33ae1ac0d9eebce1a03c96497a1f8f4c67) / v1.9後master | lib: sbi_pmu: Return invalid param error for reserved event_idx bits | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [d34a39df](https://github.com/riscv-software-src/opensbi/commit/d34a39df779aa55a2dd929717a32e48c3a80df66) / v1.9後master | lib: sbi_pmu: Match raw event selector only against raw event map entries | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [c5077d49](https://github.com/riscv-software-src/opensbi/commit/c5077d497d0f7c7c9a74218ef2c448bc1a6d8a31) / v1.9後master | lib: sbi_pmu: Fix counter and event info error codes as per SBI v3.0 spec | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [9f37e25f](https://github.com/riscv-software-src/opensbi/commit/9f37e25f8035f05bcadccd17517f89d9e684f8ad) / v1.9後master | lib: sbi: reject PMU skip-match counter class mismatches | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [16fa280b](https://github.com/riscv-software-src/opensbi/commit/16fa280b6949463c506dc7f0c1030a208afecdbf) / v1.9後master | lib: sbi_pmu: Fix race on pmu_ctr_write_hw | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [0ce50ae8](https://github.com/riscv-software-src/opensbi/commit/0ce50ae8fae048384759f3298ae5331b1468c53f) / v1.9後master | lib: sbi_pmu: unmap shared memory on the early exit in event_get_info | **バックポート対象**：d97cfb33のearly-return追加と同時採用。現行単独では当該新経路なし |
| [f45b9433](https://github.com/riscv-software-src/opensbi/commit/f45b9433ed03588edf7c5a9a803c67b2c9d09aee) / v1.9後master | lib: sbi_pmu: fix integer overflow and zero-address in event_get_info | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [2bbee7d8](https://github.com/riscv-software-src/opensbi/commit/2bbee7d82acec9c4a46b0a75855d33472b65cec4) / v1.9後master | lib: sbi_pmu: Fix fixed-counter events reported as unsupported | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [1e418fc8](https://github.com/riscv-software-src/opensbi/commit/1e418fc8186e433d55175009238ffd00d9425e77) / v1.9後master | lib: sbi_pmu: do not ignore pmu_ctr_start_hw() error in cfg_match | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [7a7a60dc](https://github.com/riscv-software-src/opensbi/commit/7a7a60dc87a41c79fb63d5972943e9d8fe4abf77) / v1.9後master | lib: sbi_pmu: reject out-of-bounds counter index in pmu_ctr_write_hw() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [827f8bf2](https://github.com/riscv-software-src/opensbi/commit/827f8bf26b23c0a492b9883592b6c9bdd3e704a7) / v1.9後master | lib: sbi_pmu: fix mapping reset for already stopped counters | **条件付きバックポート**：PMU error-code変更c5077d49と組にする |

## BP03：DBTR

優先度：P0/P1。試験：`T-DBTR`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [033e0e23](https://github.com/riscv-software-src/opensbi/commit/033e0e2353ddab76039e62b46c9f77e178bac689) / v1.7 | lib: sbi: dbtr: Fix shared memory layout | **取込済**：SDK 5b966295・現行union layout確認 |
| [32402142](https://github.com/riscv-software-src/opensbi/commit/324021423d063702a5cb2d4207a3a9c4e999549f) / v1.7 | lib: sbi: dbtr: Fix update_triggers to match SBI | **取込済**：SDK a778a435・現行trig_count ABI確認 |
| [6a1f53bc](https://github.com/riscv-software-src/opensbi/commit/6a1f53bc2d91310adc44c158596dd1c3efa18080) / v1.8 | dbtr: Fix sbi_dbtr_read_trig to read from CSRs | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [3afe63d4](https://github.com/riscv-software-src/opensbi/commit/3afe63d4e3aff1a777e3e880b8b433ca225a311c) / v1.9 | lib: sbi: dbtr: do not unconditionally access tdata2/tdata3 CSRs | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [3e21ee34](https://github.com/riscv-software-src/opensbi/commit/3e21ee3496684e27f1884e8947a1faf6e3195e09) / v1.9後master | lib: sbi: dbtr: return SBI_ERR_BAD_RANGE for invalid trigger index range | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [548518e6](https://github.com/riscv-software-src/opensbi/commit/548518e675d0ed387ff3ee2d9a2226a6e93090a4) / v1.9後master | lib: sbi: dbtr: return SBI_ERR_INVALID_PARAM for invalid trigger configuration | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [27d95453](https://github.com/riscv-software-src/opensbi/commit/27d954538410a7fbecf0ed68ce319706d3e3b142) / v1.9後master | lib: sbi: dbtr: add platform device for per-slot trigger capabilities | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [a14e4d24](https://github.com/riscv-software-src/opensbi/commit/a14e4d24a1f51a9d6d263f1da4e23c01bd89f3ce) / v1.9後master | lib: sbi: dbtr: fix integer overflow in read_trig bounds check | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [ed70256d](https://github.com/riscv-software-src/opensbi/commit/ed70256dd22c09962d146e433834239825ef0616) / v1.9後master | lib: sbi: fix trigger read/update/install range check | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP04：RPMI mailbox/CPPC/reset

優先度：P0/P1。試験：`T-RPMI`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [17b8d190](https://github.com/riscv-software-src/opensbi/commit/17b8d1900d56df1e14edd8829acf22686fa08eac) / v1.8 | lib: utils/reset: Hang the hart after RPMI system reset message | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [95c79808](https://github.com/riscv-software-src/opensbi/commit/95c7980851171ef7b0fd909214ec5838f34ee12d) / v1.9後master | lib: utils/mailbox: reject undersized RPMI shmem queues | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [9bc84122](https://github.com/riscv-software-src/opensbi/commit/9bc841226d8a63aaa59fc6b26a4452fe548c6589) / v1.9後master | lib: utils/mailbox: Reject invalid RPMI queue indices | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [fde5dfe9](https://github.com/riscv-software-src/opensbi/commit/fde5dfe96f99f6894547876ad9a868f33e104ced) / v1.9後master | include: sbi_utils/mailbox/rpmi_msgprot: Fix warm reboot failure | **バックポート対象**：RPMI protocol version番号変更。K3側serverとの互換性を先に確認 |
| [33279821](https://github.com/riscv-software-src/opensbi/commit/332798211e7232da60158b0545b23c61b5338516) / v1.9後master | include: mailbox: correct RPMI CPPC request field ordering | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP05：ECALL/string/scratch

優先度：P0/P1。試験：`T-CORE`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [56341e95](https://github.com/riscv-software-src/opensbi/commit/56341e95aed220c114f28cfea8e2e62e1366f50b) / v1.7 | lib: sbi: Fix potential garbage data in string copy functions | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [ef4ed2dd](https://github.com/riscv-software-src/opensbi/commit/ef4ed2dda76648171d551537443a16b5db712284) / v1.7 | lib: sbi_scratch: Apply bounds check to platform hart_count | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [2727c43f](https://github.com/riscv-software-src/opensbi/commit/2727c43f8dcef9f6a0c3c117e30d00de145bde11) / v1.9 | lib: sbi: Fix undefined behavior in signed shifts in sbi_hart.c | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [2bf0de88](https://github.com/riscv-software-src/opensbi/commit/2bf0de88c8897ff4be5b89c04115c17421ad5687) / v1.9 | lib: Fix sbi_strchr to correctly handle null terminator search | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f95648d3](https://github.com/riscv-software-src/opensbi/commit/f95648d3955d72f77e13315a990a6135303978a5) / v1.9後master | lib: sbi: clamp sbi_ecall_get_extensions_str buffer offset | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f6abdaf2](https://github.com/riscv-software-src/opensbi/commit/f6abdaf25dfe0608277fc634f3927c1f184ede6d) / v1.9後master | lib: sbi_pmu: Do not return uninitialized stack data on FW read errors | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP06：HSM/IPI/boot

優先度：P0/P1。試験：`T-HSM`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [1f64fef9](https://github.com/riscv-software-src/opensbi/commit/1f64fef919fc5739df6c3395a9abd256dc88cb24) / v1.7 | lib: sbi: Fix non-root domain startup | **条件付きバックポート**：93f7d819導入時の起動順修正を必ず同梱 |
| [37eaca4a](https://github.com/riscv-software-src/opensbi/commit/37eaca4ab33d8a392a30fd69d87d5a4886ce03b7) / v1.7 | lib: sbi_ipi: Return error for invalid hartids | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [7dd09bfe](https://github.com/riscv-software-src/opensbi/commit/7dd09bfeca310f540d73e29c600958faead0849e) / v1.7 | lib: sbi: Revert entry_count before doing hsm stop in hsm wait loop | **条件付きバックポート**：HSM waitをstopに変えるd844deadが前提 |
| [55135abc](https://github.com/riscv-software-src/opensbi/commit/55135abcd555b72ca4edf481461fb37acd27e9cc) / v1.8 | lib: sbi_hsm: Save mideleg across non-retentive suspend | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [110f2851](https://github.com/riscv-software-src/opensbi/commit/110f28518a15e84498bcc6337242160c500857fe) / v1.9後master | lib: sbi_ipi: Validate hartids against domain, not HSM state | **バックポート対象**：37eaca4aと同梱。停止hartを含むRFENCE/IPI回帰を防ぐ |
| [131bda94](https://github.com/riscv-software-src/opensbi/commit/131bda944704293fa75295060aec96061cdb3749) / v1.9後master | lib: sbi_hsm: Validate state before updating start context | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f84bdb98](https://github.com/riscv-software-src/opensbi/commit/f84bdb98772b44d20bddb1ccae65ee40ac36991b) / v1.9後master | firmware: Clear scratch->trap_context on warm boot | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [00b9807b](https://github.com/riscv-software-src/opensbi/commit/00b9807b780b1adadb49648568dab6386eb5aaac) / v1.9後master | firmware: fw_base: add an acquire fence after the _boot_status wait loop | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP07：hart CSR/MPRV/expected trap

優先度：P0/P1。試験：`T-CSR`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [d2166a9d](https://github.com/riscv-software-src/opensbi/commit/d2166a9d4045013654b04a1a46ab43fdb7dabb95) / v1.7 | lib: sbi: Handle length of extension name string exceed buffer size error | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [009f77a9](https://github.com/riscv-software-src/opensbi/commit/009f77a9f06bcb5a3a6252b3333171adb8c84512) / v1.7 | lib: sbi_hart: reset hstatus | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [6b877fb5](https://github.com/riscv-software-src/opensbi/commit/6b877fb53b8da84c69f0832c7f2a343239ba609e) / v1.7 | lib: sbi_hart: reset sstateen and hstateen | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [8c814b5c](https://github.com/riscv-software-src/opensbi/commit/8c814b5c9b03aac0d6bb2366e3c8fbce132ca4d9) / v1.7 | lib: sbi_hart: fix sstateen emulation | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [dac15cb9](https://github.com/riscv-software-src/opensbi/commit/dac15cb91058f324df909dba4bfcb52f42ff9224) / v1.7 | lib: sbi_hart: reset mstateen0 | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [316daaf1](https://github.com/riscv-software-src/opensbi/commit/316daaf1c299c29ac46e52145f65521f48ec63b5) / v1.7 | lib: sbi_hart: properly reset Ssstateen | **条件付きバックポート**：Ssstateen検出937118caとCSR64 helper依存 |
| [5b305e30](https://github.com/riscv-software-src/opensbi/commit/5b305e30a55d93d507006dd11685bdeb7e7207e8) / v1.8 | lib: sbi: Only enable TM bit in scounteren | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f04ae482](https://github.com/riscv-software-src/opensbi/commit/f04ae48263533503a0432e6a7ba31cc20518c310) / v1.8 | lib: sbi_hart: Do not call delegate_traps() in the resume flow | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [34657b37](https://github.com/riscv-software-src/opensbi/commit/34657b377f35ab4d0602390e885d1db91e82acf6) / v1.8 | lib: sbi_hart: return error when insufficient PMP entries available | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [fbb5fe81](https://github.com/riscv-software-src/opensbi/commit/fbb5fe811d453c11dbcacf8cfe2fa7e445834475) / v1.8 | lib: sbi: expected trap must always clear MPRV | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [9dcd9f93](https://github.com/riscv-software-src/opensbi/commit/9dcd9f936510c3c3d3d8a7a3bcbb07730a6c53ff) / v1.9後master | lib: sbi: use unsigned masks for HPM counter 31 | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [3a51dc07](https://github.com/riscv-software-src/opensbi/commit/3a51dc070e7707e0ff11c13bfc4dce470532f1a3) / v1.9後master | lib: sbi_hart: Reset mhpmcounter after probing its bits | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [d28bc6f8](https://github.com/riscv-software-src/opensbi/commit/d28bc6f89837de34aa94a0c95201b916af6aaf8a) / v1.9後master | lib: sbi_hart: Retrun zero length of mhpmcounter when probing failed | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [5dd2a061](https://github.com/riscv-software-src/opensbi/commit/5dd2a0619e51fd0785bf33b198bd7d848818d4a8) / v1.9後master | lib: sbi: select expected trap handler per hart | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [6e0338f4](https://github.com/riscv-software-src/opensbi/commit/6e0338f4258e59120eb78148c6e7b15af76a7e59) / v1.9後master | lib: sbi: Install the next-stage entry as a direct-mode xtvec | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP08：PMP/domain/FWFT

優先度：P1設計審査。試験：`T-PMP`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [1ad19912](https://github.com/riscv-software-src/opensbi/commit/1ad199124479b9944d5dddc2119e6da9f088f7a7) / v1.7 | lib: sbi: fwft: Return SBI_ERR_DENIED_LOCKED when setting a locked feature | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [771c6561](https://github.com/riscv-software-src/opensbi/commit/771c656181c360ed351a4a96a3e25102cbc140b9) / v1.7 | lib: sbi: fwft: Use only the provided PMLEN value | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f7d060c2](https://github.com/riscv-software-src/opensbi/commit/f7d060c26a7a3c691e29dc0d89957a6ff4e1cb31) / v1.8 | lib: sbi: Add error handling to switch_to_next_domain_context | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [38c31ffb](https://github.com/riscv-software-src/opensbi/commit/38c31ffb8f8f9aa3730403d8c3812904ad74fb39) / v1.8 | lib: sbi: Add hart context init when first call enter | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [e3eb59a3](https://github.com/riscv-software-src/opensbi/commit/e3eb59a396ac55975e3debecfc5b71418eb62248) / v1.8 | lib: sbi: Prevent target domain same as the current | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [631efeeb](https://github.com/riscv-software-src/opensbi/commit/631efeeb4913f278b8f5e5920b8c233ac250ff28) / v1.8 | lib: sbi_domain: ensure consistent firmware PMP entries | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [0250db4d](https://github.com/riscv-software-src/opensbi/commit/0250db4dad85248c08a53df4056634dac5ed7169) / v1.8 | lib: sbi_domain_context: preserve firmware PMP entries during domain context switch | **条件付きバックポート**：FW PMP flag/slot追跡依存。Keystone domain gateを保持 |
| [4997eb28](https://github.com/riscv-software-src/opensbi/commit/4997eb28dab2916d7c4e8d53bfae5ec0d40a0742) / v1.8 | lib: sbi: fix covered regions handling in sanitize_domain() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [11173809](https://github.com/riscv-software-src/opensbi/commit/111738090c6cdf2d852717b3ad15b65e2085d237) / v1.8 | lib: sbi: Flush TLBs upon FWFT ADUE change | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [ea459237](https://github.com/riscv-software-src/opensbi/commit/ea45923765de4145d174b98abc6b945c8407265e) / v1.9 | lib: sbi_hart_pmp: disable unconfigured PMP entries | **条件付きバックポート**：新PMP backend向け。現行への一括slot解除移植は禁止 |
| [3baca100](https://github.com/riscv-software-src/opensbi/commit/3baca10015393359ec46a2500c8ebe7daa9672c6) / v1.9 | include: sbi: Fix illegal shift in sbi_domain.h | **バックポート対象**：現行lib/sbi/sbi_domain.c:is_region_subsetへ意味を移植（同名headerはない） |
| [5f13f9e1](https://github.com/riscv-software-src/opensbi/commit/5f13f9e13f8a8c08e5339cd0cf590282ef6a39a4) / v1.9 | lib: sbi: Fix hw a/d updating defaults | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [9595829a](https://github.com/riscv-software-src/opensbi/commit/9595829a336793744f56b978b2651350d0a1723b) / v1.9 | lib: sbi_domain: reject overflowing address range in check_addr_range() | **取込済**：SDK d8ff6346・現行はzero-sizeも拒否 |
| [73bc794b](https://github.com/riscv-software-src/opensbi/commit/73bc794b40fc917feb146afd34970cf44563b561) / v1.9 | lib: sbi_domain_context: Flush TLB after SATP mode switch | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [6dc5a896](https://github.com/riscv-software-src/opensbi/commit/6dc5a896b1bb5cf1ab671458436680f80e786ad5) / v1.9後master | lib: sbi_domain: Check possible harts in sbi_domain_context_enter/exit() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP09：FDT/serial/regmap

優先度：P1。試験：`T-FDT`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [5e90e54a](https://github.com/riscv-software-src/opensbi/commit/5e90e54a1a53173128b264f4e22cfa5884f78526) / v1.7 | lib: utils:Check that hartid is valid | **バックポート対象**：9f64f061を同梱し非連続hartidの回帰を防ぐ |
| [0ffe265f](https://github.com/riscv-software-src/opensbi/commit/0ffe265fd969f5ac689c820a4b40a99cd2f1e9a9) / v1.7 | lib: utils/fdt: Respect compatible string fallback priority | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [321ca806](https://github.com/riscv-software-src/opensbi/commit/321ca8063b7ea9863cdeb6c80fe8bce340bcd04b) / v1.7 | lib: utils: Make sure that hartid and the scratch are aligned | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [3f8159aa](https://github.com/riscv-software-src/opensbi/commit/3f8159aa068e6b2447abcf2df00b913b7f852456) / v1.7 | lib: utils: hsm: Do not fail on EALREADY in rpmi-hsm fixup. | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [6f8bcae4](https://github.com/riscv-software-src/opensbi/commit/6f8bcae4cb12d92e0c198ba3b7e2f56e2fbb7289) / v1.7 | lib: utils/irqchip: always parse msi information for each aplic device | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [9f64f061](https://github.com/riscv-software-src/opensbi/commit/9f64f0619334eea34df66a1816b1be589870ac4e) / v1.7 | lib: utils: Fix fdt_parse_aclint_node() for non-contiguous hartid | **条件付きバックポート**：5e90e54aの後続。先行修正を単独導入しない |
| [b8f370aa](https://github.com/riscv-software-src/opensbi/commit/b8f370aa3714ef37023e523c37c1981b0a5d1e12) / v1.8 | lib: utils/serial: Clear LSR status and check RBR status | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [84044ee8](https://github.com/riscv-software-src/opensbi/commit/84044ee83c051fd2f39e34b29854993ecc95d7f0) / v1.8 | lib: utils: fdt: fix "ranges" translation | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [35aece21](https://github.com/riscv-software-src/opensbi/commit/35aece218a1da347e7c276a18c4ccdbda32ebec9) / v1.8 | lib: utils/cache: Handle last-level cache correctly in fdt_cache_add() | **条件付きバックポート**：現行にFDT cache libraryなし。導入時に同梱 |
| [9a46ef43](https://github.com/riscv-software-src/opensbi/commit/9a46ef43a45ee1c462c17a0fa62e03c955911192) / v1.8 | include: Fix LLVM compile error in sbi_utils/hsm/fdt_hsm_sifive_inst.h | **条件付きバックポート**：現行に対象SiFive headerなし |
| [6d68f3be](https://github.com/riscv-software-src/opensbi/commit/6d68f3bebd206314a5b194004249524dbc0da075) / v1.9 | lib: utils/irqchip: Fix context_map init in irqchip_plic_update_context_map() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [8cd279e3](https://github.com/riscv-software-src/opensbi/commit/8cd279e321ce8eedd2750d3e2ed9a906e43e6e38) / v1.9 | lib: fdt_domain: Default boot-hart to coldboot HART for multi-domain boot | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [8536a351](https://github.com/riscv-software-src/opensbi/commit/8536a351fd3af50e5eba571e562a33247b99870d) / v1.9 | lib: utils/regmap: Fix reg_stride calculation in syscon regmap | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [0ffaa1bc](https://github.com/riscv-software-src/opensbi/commit/0ffaa1bc65afe89985bcf3f0c9f564c6b09e3091) / v1.9後master | lib/utils/fdt: Reserve exactly enough FDT space for fdt_cpu_fixup | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP10：SSE

優先度：P1/P2。試験：`T-SSE`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [ecab71e1](https://github.com/riscv-software-src/opensbi/commit/ecab71e19a951a0e42dbbb81b0e4d43840551f7e) / v1.7 | lib: sbi: sse: return SBI_ENOTSUPP for unsupported events | **条件付きバックポート**：動的SSE event対応と一緒に判断 |
| [f354400e](https://github.com/riscv-software-src/opensbi/commit/f354400ebf91f2988a20da52a6399ac51988fc95) / v1.7 | lib: sbi: sse: fix invalid errors returned for sse_hart_mask/unmask() | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [b2e8e698](https://github.com/riscv-software-src/opensbi/commit/b2e8e6986d676005f23fc5e3b3845b027fc4330f) / v1.7 | lib: sbi: Return SBI_EALREADY error code if SSE event is present | **条件付きバックポート**：動的SSE event追加後のエラー契約 |
| [1e7258d6](https://github.com/riscv-software-src/opensbi/commit/1e7258d6a81097e7a003aed860f98bcb1d8c58a7) / v1.7 | lib: sbi: sse: Return SBI_EDENIED for read only parameters. | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [6d5b2b9b](https://github.com/riscv-software-src/opensbi/commit/6d5b2b9b056826e072c387a068e3f8e6700b5cae) / v1.9 | lib: sbi: fix sse_event_inject() status setting | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [35511bc6](https://github.com/riscv-software-src/opensbi/commit/35511bc6ee1c9c17b6a89b44c52e2044bb51b979) / v1.9後master | lib: sbi: sse: clear SPV for non-virtualized events | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |

## BP11：misaligned/vector emulator

優先度：P1/P2。試験：`T-EMU`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [afc24152](https://github.com/riscv-software-src/opensbi/commit/afc24152bb55927297716c72ef37f9d529893c55) / v1.8 | include: sbi: Ignore rs1 and rd fields in FENCE.TSO. | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [1157eb37](https://github.com/riscv-software-src/opensbi/commit/1157eb37e29500b5e30d4c70bda6979fb516c7fc) / v1.9 | lib: sbi_trap_v_ldst: Redirect unhandled traps | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [eba121b4](https://github.com/riscv-software-src/opensbi/commit/eba121b459889868513e5e87639d5273012728f7) / v1.9 | lib: sbi: Do not override emulator callback for vector load/store | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [56c39d1f](https://github.com/riscv-software-src/opensbi/commit/56c39d1f08cb7aa1132e115b7850715c63212d57) / v1.9 | lib: sbi: Rework load/store emulator instruction decoding | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [a8be5e94](https://github.com/riscv-software-src/opensbi/commit/a8be5e9478be08f09a720f7013ec30d6e2bd9fcc) / v1.9 | lib: sbi: Rework misaligned vector load/store | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [663786ed](https://github.com/riscv-software-src/opensbi/commit/663786edae9fdbd97f58391ec16848f1e0e33649) / v1.9後master | lib: sbi_trap_v_ldst: fix mask buffer overflow on VLEN > 128 | **条件付きバックポート**：a8be5e94導入時に必須。現行mask読出しはvlenb bytes |

## BP12：build/firmware

優先度：P2。試験：`T-BUILD`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [8573a9b8](https://github.com/riscv-software-src/opensbi/commit/8573a9b8580938fd8c5f1bc1e7b59ef841ce8564) / v1.7 | scripts: Fix firmware binaries compilation in create-binary-archive.sh | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [017a1617](https://github.com/riscv-software-src/opensbi/commit/017a161788310ef515831821f14597e657512f97) / v1.7 | Makefile: fix missing .debug_frame DWARF section for GCC | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [663b05a5](https://github.com/riscv-software-src/opensbi/commit/663b05a5f745f6b1360f073587bb57fe9af6a40a) / v1.8 | include: sbi: fix swap errors with newer gcc -Werror=sequence-point | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [4825a3f8](https://github.com/riscv-software-src/opensbi/commit/4825a3f87f8d3ee45b7f9b76ed62de7bd1f2dfa8) / v1.8 | include: sbi: Don't use #pragma when preprocessing device tree sources | **条件付きバックポート**：sbi_visibility.h/86c01a73導入時のみ |
| [bbe9a230](https://github.com/riscv-software-src/opensbi/commit/bbe9a23060e956d68357adc2740e5df190009a7d) / v1.8 | platform: generic: Pack the FDT after applying fixups | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [944db4ec](https://github.com/riscv-software-src/opensbi/commit/944db4eced291f248724d9f662585257b4876e16) / v1.8 | lib: utils/irqchip: fix aplic lock mechanism in xmsiaddrcfg(h) | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [fade4399](https://github.com/riscv-software-src/opensbi/commit/fade4399d2a47a3ff9c5ecd5dd4c3cba7a4eace7) / v1.8 | lib: utils/irqchip: plic: context_id is signed | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [d65c1e95](https://github.com/riscv-software-src/opensbi/commit/d65c1e95a737d8365a6b170c2074c27b56fc5bcb) / v1.8 | include: sbi: Make "s8" actually signed. | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [4339e857](https://github.com/riscv-software-src/opensbi/commit/4339e85794571077f6436d8998b09dbd5755d258) / v1.8 | platform: generic: Keep some empty space in FDT passed to next stage | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [f6fa62bd](https://github.com/riscv-software-src/opensbi/commit/f6fa62bd169e55f479b137da13ca83b27bb304a9) / v1.8 | lib: atomics: fix AMO test macros | **条件付きバックポート**：Zalrsc/Zaamo fallback系列導入時。RV64命令も確認 |
| [74434f25](https://github.com/riscv-software-src/opensbi/commit/74434f255873d74e56cc50aa762d1caf24c099f8) / v1.9 | lib: sbi: Fix behavior on platform without HART protection | **条件付きバックポート**：現行にhart_protection abstractionなし |
| [b5348006](https://github.com/riscv-software-src/opensbi/commit/b5348006e98077076a0b1a690c3a9f63c7151a4b) / v1.9 | lib: Use proper add opcode on RV32 with Zalrsc | **条件付きバックポート**：RV32かつZalrsc。対象K3はRV64 |
| [895201cc](https://github.com/riscv-software-src/opensbi/commit/895201cc5a4dfca2df523287bef872197eb95d16) / v1.9 | platform: Fix payload alignment when FW_TEXT_START isn't 2M/4M aligned | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [b508e6e2](https://github.com/riscv-software-src/opensbi/commit/b508e6e25eb23a6a188f8cfea94f338c85c75cab) / v1.9 | lib: sbi: Apply budget restriction when polling Zkr CSR state transition | **条件付きバックポート**：OpenSBI stack guard用Zkr polling導入時。SM RNGとは別 |
| [7dfaa467](https://github.com/riscv-software-src/opensbi/commit/7dfaa4678213521e714a8a7cecb7f7a722dcd18a) / v1.9 | include: sbi: set FS dirty in vsstatus when V=1 | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [bf10d3bf](https://github.com/riscv-software-src/opensbi/commit/bf10d3bf89c05cbc4e266de8c9aff7eb55aee748) / v1.9 | firmware: Fix comment after relocation completion | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [7384968e](https://github.com/riscv-software-src/opensbi/commit/7384968e020f3bf9d524a00c6dc5638f3383aa92) / v1.9後master | lib: sbi: respect scounteren when emulating timeh | **バックポート対象**：未適用差分。採用時に機能/設定/後続修正を再確認 |
| [10b5ac72](https://github.com/riscv-software-src/opensbi/commit/10b5ac72222fcb4a9c868360776d3cd63cfeab1f) / v1.9後master | firmware: payload: test: Relocate GOT entries before calling C code | **条件付きバックポート**：上流test payloadをPIE/GOT対応で導入する場合 |

## BP13：新機能依存の修正

優先度：機能導入時。試験：`対応機能の実guest試験`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [0d81a78e](https://github.com/riscv-software-src/opensbi/commit/0d81a78ec54be2f7c62906b1aa945240ce519c6b) / v1.9 | lib: sbi_irqchip: Check full range for existing handlers in sbi_irqchip_register_handler() | **条件付きバックポート**：新irqchip handler登録API導入時 |
| [92bea7bd](https://github.com/riscv-software-src/opensbi/commit/92bea7bdb2334a9274a824f9592026093febec7c) / v1.9後master | lib: sbi_irqchip: fix device lookup by caps when first is NULL | **条件付きバックポート**：新irqchip device capability lookup導入時 |
| [b2ead3b0](https://github.com/riscv-software-src/opensbi/commit/b2ead3b037c0b864b0770891586d121c0dc68761) / v1.9後master | lib: utils/irqchip/imsic: track IRQ enable state and restore EIE on warm init | **条件付きバックポート**：IMSIC新irqchip framework移行と一緒に採用 |
| [7a90f41f](https://github.com/riscv-software-src/opensbi/commit/7a90f41fb966e48a160637e841b91864e7a2c81c) / v1.9後master | lib: sbi_irqchip: fix MSI EIID gap and tail allocation logic in register_msi | **条件付きバックポート**：新irqchip MSI allocator導入時 |
| [bf459f85](https://github.com/riscv-software-src/opensbi/commit/bf459f85bf75d86cbf028e136067ce96aefde221) / v1.9後master | lib: sbi_irqchip: error handling in sbi_irqchip_raw_handler_default() | **条件付きバックポート**：新irqchip raw handler導入時 |
| [7774b82e](https://github.com/riscv-software-src/opensbi/commit/7774b82e4aaa6fa98d1430b02d614eadd339a8a2) / v1.9後master | lib: sbi: Fix FP state enable in sbi_fp_save | **条件付きバックポート**：FP helper 718e1d19導入時に必須 |
| [5930665a](https://github.com/riscv-software-src/opensbi/commit/5930665a17388b52003b1a3579c1c773d831be22) / v1.9後master | lib: sbi_timer: Restart events from the restart list | **条件付きバックポート**：timer event list b7fa8a24が現行にない |
| [97bf6797](https://github.com/riscv-software-src/opensbi/commit/97bf67970805eb0f758c6668f3bdf69c1f3d87f9) / v1.9後master | lib: utils/irqchip/imsic: Fix the MSI capability assignment | **条件付きバックポート**：IMSIC新irqchip移行193b1d9e導入時 |
| [bcf587a9](https://github.com/riscv-software-src/opensbi/commit/bcf587a987573057968a11b941729d70799947f4) / v1.9後master | lib: sbi_timer: Insert timer event before the later event | **条件付きバックポート**：timer event list b7fa8a24導入時に必須 |
| [a0531af7](https://github.com/riscv-software-src/opensbi/commit/a0531af7ad08b8d87a0a19ad8b667a501abc7098) / v1.9後master | lib: sbi: Fix stale prev_context in RNMI handler | **条件付きバックポート**：RNMI handler 00fec20b導入時 |
| [54fad5bd](https://github.com/riscv-software-src/opensbi/commit/54fad5bddae2782f6c84c9f2e57e3ceef2559623) / v1.9後master | lib: sbi: extract smrnmi_hart_init() and call on non-retentive resume | **条件付きバックポート**：Smrnmi導入時 |
| [894435d2](https://github.com/riscv-software-src/opensbi/commit/894435d28fe2d364c3d3576fb9182c28b18c2ea3) / v1.9後master | lib: sbi: report smrnmi_handlers_init failures instead of ignoring them | **条件付きバックポート**：Smrnmi platform callback導入時に必須 |
| [2c7788e4](https://github.com/riscv-software-src/opensbi/commit/2c7788e4c3efa63d670ee90402dac2a9799d0bd7) / v1.9後master | lib: sbi: allow NULL smrnmi_handlers_init and report init mode at boot | **条件付きバックポート**：Smrnmi callback/vector policy導入時に必須 |

## BP14：他platform

優先度：K3対象外。試験：`対象platform別`。

| 上流commit / 初出 | 修正内容（上流件名） | 判定・依存 |
| --- | --- | --- |
| [ac2e428c](https://github.com/riscv-software-src/opensbi/commit/ac2e428c4be6bb876b160643984617911d60a5e6) / v1.7 | platform: rzfive: Call andes_pma_setup_regions() only during cold boot | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [0b7c2e0d](https://github.com/riscv-software-src/opensbi/commit/0b7c2e0d6091e6f48b4b3b2a345fd588eb95ddfc) / v1.8 | platform: openpiton: fix uninitialized plic_data struct | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [126c9d34](https://github.com/riscv-software-src/opensbi/commit/126c9d34d25454ac9d0dcdbfface48d78c1fd3eb) / v1.8 | platform: generic: spacemit: add missing objects.mk | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [6545b78b](https://github.com/riscv-software-src/opensbi/commit/6545b78bcd82dabf07a14c65fce32e6fcd85394b) / v1.9 | platform: generic: mips p8700: fix powering up other cluster | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [9169290c](https://github.com/riscv-software-src/opensbi/commit/9169290ca9f17bc64df20139526831367ecaf26c) / v1.9 | platform: generic: mips eyeq7h: fix NCORE registers access from clusters 1..2 | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [f07be546](https://github.com/riscv-software-src/opensbi/commit/f07be546e5ec992fe7c766491a5403c8bc864ceb) / v1.9 | platform: generic: p8700: fix MIPS specific CSRs | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [7bff4e52](https://github.com/riscv-software-src/opensbi/commit/7bff4e529e6c32df5c9364960c174fa1a682cd5d) / v1.9 | platform: generic: eswin: Add eic770x_hsm and fix warm reset issues | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [a2077d44](https://github.com/riscv-software-src/opensbi/commit/a2077d44cc5743b2fae084dd3cac9225e81bc2fb) / v1.9 | platform: generic: mips eyeq7h: fix boot with JTAG | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [cc9b4ef8](https://github.com/riscv-software-src/opensbi/commit/cc9b4ef8f3b465f4625ca40bf6af5e033df2bd75) / v1.9 | platform: generic: spacemit: k1: fix wrong address definitions | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [344428d8](https://github.com/riscv-software-src/opensbi/commit/344428d8322e03cc5059a25769694f7622fb9333) / v1.9後master | lib: utils/cache: fix andes_llcache_enable() return value | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [4c5d9d9f](https://github.com/riscv-software-src/opensbi/commit/4c5d9d9f93c77805dbc023cdef6a68c039b87970) / v1.9後master | lib: utils/andes: add the SMU and LLC registers to the root domain | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [733867c0](https://github.com/riscv-software-src/opensbi/commit/733867c0c8f4941a78144eb49d8e0c5d086d8567) / v1.9後master | platform: generic/andes: pair non-retentive CSR save/restore with a flag | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [9ceaf473](https://github.com/riscv-software-src/opensbi/commit/9ceaf4738a62ec990dcd0e797243bb6e78508291) / v1.9後master | lib: utils/suspend: restore Andes CSRs on system-suspend resume | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [e01bd792](https://github.com/riscv-software-src/opensbi/commit/e01bd7926b5d808d883df18c6ac919dfa9ab04e7) / v1.9後master | lib: utils/hsm: wake sleeping Andes harts with an IPI | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [0fb47993](https://github.com/riscv-software-src/opensbi/commit/0fb479930775cf86efdfd5c50ad21b4cf8353c76) / v1.9後master | lib: utils/suspend: wait for the secondary Andes harts to sleep | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [06af8bd6](https://github.com/riscv-software-src/opensbi/commit/06af8bd61b37bbf0db93adfa9902b176d0158ea1) / v1.9後master | lib: utils/andes: arm the SMU sleep command last | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [22a5f98b](https://github.com/riscv-software-src/opensbi/commit/22a5f98b3965adb2c5b2224da7ec4590807a618a) / v1.9後master | platform: generic/andes: fix 32-bit shift overflow in decode_pmaaddrx() | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [acc2508c](https://github.com/riscv-software-src/opensbi/commit/acc2508c4745fbf7d59631e6cc1fd6df745fb031) / v1.9後master | platform: generic: spacemit: k1: move hart init to nascent hook | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |
| [c9a7d009](https://github.com/riscv-software-src/opensbi/commit/c9a7d00992b87aca6db83b71e10622e19436a7dd) / v1.9後master | platform: generic/starfive: deny S-mode access to the JH7110 firmware alias | **K3対象外**：K1/MIPS/Andes/OpenPiton/ESWIN固有。K3へ適用しない |

