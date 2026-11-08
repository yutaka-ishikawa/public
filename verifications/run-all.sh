#!/usr/bin/env bash
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
for id in 2001 2002 2003 2004; do
  bash "sbi-$id/run-tests.sh" "$@"
done
bash edgecall/run-tests.sh "$@"
