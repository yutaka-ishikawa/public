# ソフトウェアPCR・Linux IMA設計の統合先

2026年11月10日（日本時間）、本書の設計内容を次の文書へ統合しました。

**[OpenSBI＋Keystone SMのPCR・Linux IMA・TPM 2.0形式Quote統合設計](../doc/keystone-tpm2-quote-design.md)**

- 第2節：元のPCR24本、SHA-256 EXTEND/READ、SBI ABI、Linux IMA、初期化・寿命、ビルド、試験・受け入れ条件。
- 第3〜8節：PCR snapshotからTPM2B_ATTEST／TPMT_SIGNATUREを作るQuoteの設計。
- 第9節：Jupiter2のAttestation Key管理。
- 第10〜12節：外部検証、統合実装計画、参照。

今後の設計更新は統合先で行います。このパスは既存リンクを維持するための案内として残します。
元の設計はGit履歴でも確認できます。PCR・IMA連携・Quoteはいずれも設計段階で、実装・動作検証は未実施です。
