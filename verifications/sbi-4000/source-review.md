# FID 4000 実ソースの検証

2026-11-09。対象は同梱K3 RV64構成。

## ABIと互換性

`keystone/sdk/include/shared/sm_call.h`の4000は既存の引数配置を維持する。
plugin=1、call=1/2でサイズ/PAを、a2が指すruntimeの仮想アドレスへ1 word出力する。
a3は予約済みで0のみ。成功0、未知IDはNOT_IMPLEMENTED、未登録はNOT_ACCESSIBLE、
不正出力先はILLEGAL_ARGUMENT、host呼出しはSBI_PROHIBITED。

追加領域の登録はK3拡張FID **2006 (CREATE_ENCLAVE_V2)** とする。
既存2001の構造体や引数解釈は変更しない。2006のa0は
`keystone_sbi_create_v2`へのポインタ。a1〜a3は0、version=1、bytes=sizeof構造体。
baseは旧CREATE情報、backingは物理開始/長さ。versionは拡張構造体の初版を示す。
旧firmwareが2006を知らない場合は失敗し、追加領域が黙って無視されることはない。
旧CREATEは有効/無効profileの両方で維持する。C++ SDK利用者は再コンパイル・再リンクが必要。

## Linux driverとSDK

新IOCTL `KEYSTONE_IOC_BACKING_INIT`はmagic 0xa4、nr 0x08、version/bytes、
file所有UID、要求長、出力PA/実長を持つ。PAはdriverが選び、利用者からの指定を拒否する。
page単位・非0・ページ数上限を検査し、既存`epm_init()`のbuddy/CMA確保を再利用する。
丸めにより実長が要求長と異なる場合は実長を返す。backingへのmmap APIは今回追加しない。

`Params::setBackingMemSize()`で要求し、`KeystoneDevice::finalize()`が先に割当を行う。
backingありの場合のみ2006を使用する。実長/PAはEnclaveの
`getBackingMemorySize()` / `getBackingMemoryBase()`でも取得できる。

fileごとのmutexでioctl/mmapの操作を直列化し、他fileのUIDの操作を拒否する。
FINALIZE後の追加・再FINALIZE・二重割当を拒否する。失敗時はIDR所有権を保持し、
DESTROY/closeで一度だけ回収する。SM DESTROYが失敗した場合はページを解放しない。
closeでその失敗が起きた場合もIDRに保持して再利用を防ぐが、自動回復機構は未実装。

## SM / OpenSBI

`sm-sbi-opensbi.c`が拡張引数を既存のMPRVコピー経路で取得しheaderを検査する。
`enclave.c`は長さ/overflow/整列、EPM/UTM/firmwareとの重複を検査し、
OpenSBIの`sbi_domain_check_addr_range()`でS-modeからの非MMIO read/write許可を確認する。
このdomain検査単独で物理RAMの割当所有権を証明するものではない。
通常利用でのページ確保・保持はdriverが担い、PMP descriptorが他登録領域との重複も拒否する。

`REGION_BACKING`はUTMや私有の`REGION_OTHER`と区別する。
PMP_PRI_ANYでTOR/NAPOTに対応し、RUN/RESUMEではローカルRW（非実行）を許可する。
host-visibleなので全hartへNO_PERMを設定しない。host復帰とRUN失敗rollbackは
PMPをOFFへ戻し、OSのcatch-allを利用する。DESTROYは全hartの解除成功後にdescriptorを解放する。
PMP更新には既存Keystone経路とOpenSBI IPI transportを再利用する。

照会は実行中の現在のenclaveに限定する。Sv39の出力先をsoftware walkし、
page table自体と書込み先がprivate EPM内であることを確認する。
NULL、不整列、非canonical、UTM、書込み不可leafは拒否する。
Bare時も出力先をEPM内に制限する。書込みは既存のtrap対応MPRV helperを使う。
実行中enclaveはDESTROYできず単一スレッドのため、照会中に登録情報を解放しない。

OpenSBIにはKeystoneのmultimemサービスそのものはなく、汎用domain/ecall/lock/IPIを
再利用する。OpenSBI内部heapをLinuxの大容量backing確保へ流用しない。
`PLUGIN_ENABLE_MULTIMEM`の有効化はK3 audit compilerから行い、cファイルの二重includeを除去した。

## 確認できていない範囲

実機、DMA、全hart固定/競合の網羅、PMP/IPI失敗のfault injection、古いSDK/firmwareとの
全組合せ、全Sv39 superpage/異常PTEの網羅は未検証。guestでの代表的異常系と
source inspectionを区別する。pagingは既定OFFのまま。SDKのfd等のhost資源解放不足はKSB-035として別記し、
今回のbackingページ回収PASSからSDK全体の解放完了を推定しない。

他enclaveのbacking PAを検証用kernel moduleから拡張CREATEに渡し、
SMが重複登録を拒否することも確認する。このprobeはCAP_SYS_RAWIO限定で、
通常driverの任意PA登録APIではない。
