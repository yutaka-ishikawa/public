# SBI 3002/3003の共通基盤と初期実装

[同時実装計画](keystone-sbi-3002-3003-joint-plan.md) に沿って、共通基盤と両SBIの
QEMU開発用経路、専用verificationを追加した。実装範囲と実行確認を分けて記録する。

## 共通基盤

- [security.c](../keystone/sm/src/security/security.c) がprovider、measurement、PAコピー、
  bounded workspace、署名とHKDFのサービス入口を共通管理する。
- 既定はunavailable。明示的qemu-dev版だけが3002/3003用materialを作る。
- device root seedはprivateなホストstateに保存する。9p共有を避けるため、計画時の
  「build領域にseedを保存」から保存先を変更した。生成した開発firmware自体はsecretを含む。
- SM identityはHKDF(root, version付きcontext || SM hash)から作り、既存Keystone keypairを使用。
  deviceによるSM証明書は既存SHA3版Ed25519でSM hashと公開鍵を署名する。
- 基本実行ready、attestation_ready、sealing_readyを分離。qemu-devでは両者を同時準備する。
  hardware providerと、片側だけのready/故障注入profileはまだ提供していない。

## Measurementと独立期待値

計算式は `SHA3-512(LE64(text_size) || LE64(rodata_size) || .text || .rodata)`。
OpenSBIの_text_start/_text_endと_rodata_start/追加した_rodata_hash_end相当symbolを用いる。
実symbol名は `_keystone_rodata_hash_end`。

[ELF manifest生成](../scripts/keystone-crypto-manifest.py) はsectionのoffset/sizeから
同じ値を独立計算する。測定対象を変更するrelocationがあればbuildで拒否し、黙って期待値を作らない。
writable data/BSS、開発root、証明書、動的symbol/relocation、対象外paddingは測定対象外。
これは宣言したimmutable領域の測定であり、実機の認証済みboot image全体の測定・証明ではない。
別load/relocation方式へ移植する場合は再設計・検証が必要。

ホスト公開鍵pinは保存rootからKeystoneの既存keypair実装で生成する。
report中のdevice公開鍵を信頼起点として採用しない。

## 共通のメモリアクセス

現在実行中enclaveのmetadataをlock内でsnapshotし、そのスレッドがRUNNINGである間は
host DESTROYできない既存lifecycle条件を利用する。PAは同じEPMに完全包含されることを検査する。
UTM、SM、他領域、overflowを許可しない。

PA copyは一時的にBareへ切り替え、OpenSBIのtrap対応sbi_load_u8/sbi_store_u8を再利用する。
active enclaveのPMPは維持する。satp、mstatus、OpenSBI関数内でのmtvecを復帰し、前後でsfenceする。
独自のtrap handlerは増設しない。実enclaveでのPA copyと異常時復帰の実行確認はまだ必要。

入力を固定上限workspaceへ先にコピーし、KDF/署名を行い、完成結果を出力する。
workspaceは専用lockで直列化し、encl_lockを署名計算全体で保持しない。
成功・失敗時にworkspaceとmeasurementをvolatile byte書き込みで消去する。
HKDFのPRK/T、HMAC context等の明示的temporary消去も追加した。
vendor暗号演算の全内部stack/register residueを完全に監査したという意味ではない。

## 個別処理と不具合修正

- 3002: 正規1352 bytesのreport、data上限1024。全体ゼロ初期化、SM証明書を入れ、
  enclave hash/data_len/有効dataを署名する。
- 3003: identifier上限4096。既存の `HKDF(SM private, enclave hash || identifier)` で128 bytesを
  導出し、同じSM identityで署名。出力は192 bytes。
- [crypto_abi.h](../keystone/sdk/include/shared/crypto_abi.h) に固定形式とサイズを定義し、SM/SDKでassert。
- Eyrieは事前にVA・長さ・capacityを検査し、page-alignedの作業bufferをPAへ変換する。
  copy結果を確認し、SBI成功時だけ正確なサイズを返す。失敗時は出力を変更しない。
- SDKにcapacity付き `attest_enclave_checked` と、生入力長を検査するverifier入口を追加。
  既存APIは維持するが、旧fromBytesの呼び出し元は従来どおり十分な領域を保証する必要がある。
- JSONの長さ・hex形式を確認し、不正data_lenを署名処理へ渡さない。
- SDK verifierは外部device pinとの一致を検査してから両段署名を検証する。

新K3経路は共通サービスへdispatchする。汎用platform向けの旧get_sealing_key/
sm_derive_sealing_keyは残っているため、そのVLA/入力契約の是正を全platformで完了したとはしない。

## Build/installと試験

[verification README](../verifications/crypto/README.md) に全手順を記載した。

```bash
make build JOBS=4
make install
python3 verifications/crypto/run-qemu.py --all
```

buildはstageへ出力、installは配置だけを行う。`make crypto` / `make install-crypto` で個別処理可能。
通常auto版とRNG専用版はproviderなし、別のcrypto-qemu-dev版は `--crypto qemu-dev` で選ぶ。
既定起動を開発鍵profileへ自動変更しない。

専用runtimeのtest syscall 1099は不正PA等を実SMへ送るためのverification専用。
通常のEyrieには含まれない。root run-allは通常profileで未対応処理を確認し、
`KEYSTONE_CRYPTO_EXPECT=qemu-dev` 指定時に正常系を確認する。

## 実施した確認

- qemu-devとunavailableのSM/OpenSBIをコンパイル・リンク。
- SDK/Eyrie、既存guest verification、専用eapp A/B/unavailable、runnerをクロスbuild。
- 検証kernel objectをコンパイル。完全な対応Module.symversがないため.koリンクは未実施。
- 実QEMUでqemu-devを2回cold bootし、SM measurementと独立ELF値の一致を確認。
- 各起動のdevice→SM証明書を外部pinで検証し、2回のidentity一致と1 bit改変拒否を確認。
- 実QEMUでunavailableのcapability表示を確認。

結果は [初期化確認JSON](../verifications/crypto/initialization-results.json) に保存した。
**Linux/enclaveからの3002/3003統合試験と新実装に対する全suite回帰は未実行**。
ユーザーから受領した過去の全PASSを新しいコードのPASSへ流用しない。
実機provider、authenticated boot、DMA/IOPMP、SM image/root rotation自動試験や
片側capability故障注入は完了対象として報告しない。

## Native verifierのlink修正（KSB-025）

ユーザーのGCC 13環境で、hosted libkeystone-verifier.a内のstack protector参照が
nostdlib eappで未解決になりました。最終linkのフラグだけではarchive内の設定を変更できません。
C暗号部分をnative設定で別archive libkeystone-verifier-eapp.aへbuildするよう修正しました。
hosted verifierとLinux runnerは元のarchiveと保護設定を維持します。
強制stack protector設定でbuildと未解決参照検査を確認しました。ゲスト統合PASSではありません。
