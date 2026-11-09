#!/usr/bin/env bash
set -Eeuo pipefail
[[ $# == 5 && $1 =~ ^[0-9]+$ ]] || { echo 'Usage: generate_func.sh COUNT OUTPUT NOP BASE HEADER' >&2; exit 2; }
python3 - "$@" <<'PYGEN'
from pathlib import Path
import sys
count, output, nop, base, header = sys.argv[1:]
Path(output).write_text(Path(header).read_text() + '\n.section .text\n.globl main\nmain:\n' +
                        Path(nop).read_text() * int(count) + Path(base).read_text())
PYGEN
