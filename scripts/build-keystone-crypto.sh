#!/usr/bin/env bash
# Build/stage an explicit QEMU development identity, never the normal profile.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
BUILD=${1:-$ROOT/.build-keystone}
STAGE=${2:-$BUILD/stage}
JOBS=${JOBS:-$(nproc)}
BUILD=$(realpath -m "$BUILD")
STAGE=$(realpath -m "$STAGE")
seed=${KEYSTONE_CRYPTO_SEED_FILE:-${XDG_STATE_HOME:-$HOME/.local/state}/keystone-k3/qemu-dev.seed}
KEYSTONE_CRYPTO_PROVIDER=qemu-dev KEYSTONE_CRYPTO_SEED_FILE="$seed" \
 bash "$ROOT/scripts/build-install-k3.sh" --only firmware --prefix "$BUILD/crypto/install" --build-dir "$BUILD/crypto/k3" --jobs "$JOBS"
make -C "$ROOT/verifications/crypto" guest KEYSTONE_BUILD_DIR="$BUILD"
firmware="$STAGE/firmware/k3/crypto-qemu-dev"
public="$STAGE/share/keystone/crypto"
mkdir -p "$firmware" "$public"
cp "$BUILD/crypto/install/firmware/k3/"fw_dynamic.* "$firmware/"
cp "$BUILD/crypto/k3/firmware/crypto-public/"* "$public/"
for app in eapp eapp-b; do
 python3 "$ROOT/scripts/keystone-crypto-vectors.py" "$seed" "$public" \
  "$ROOT/verifications/crypto/build/pkg/loader.bin" "$ROOT/verifications/crypto/build/pkg/eyrie-rt" \
  "$ROOT/verifications/crypto/build/pkg/$app" "$public/$app-key-digest.bin"
done
(cd "$ROOT/verifications/crypto/build/pkg" && sha256sum eapp eapp-b eyrie-rt loader.bin) > "$public/package-sha256.txt"
echo 'Built qemu-dev crypto firmware and public verifier metadata (not hardware attestation).'
