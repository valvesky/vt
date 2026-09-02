#!/usr/bin/env bash
set -euo pipefail

ROUNDS=10
MIB=1024
BIN=${BIN:-./vt-headless}

if [[ ! -x $BIN ]]; then
	echo "missing $BIN — ./build headless" >&2
	exit 1
fi

DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT

echo "generating ${MIB} MiB payloads in $DIR"
dd if=/dev/urandom of="$DIR/garbage.bin" bs=1M count="$MIB" status=none
tr '\000-\037\177-\377' '.' <"$DIR/garbage.bin" >"$DIR/ascii.bin"

elapsed() {
	local start end

	start=$(date +%s.%N)
	"$@" >/dev/null
	end=$(date +%s.%N)
	awk -v s="$start" -v e="$end" 'BEGIN { printf "%.6f", e - s }'
}

mib_s() {
	awk -v m="$MIB" -v t="$1" 'BEGIN { printf "%.2f", m / t }'
}

DEVNULL() {
	# random garbage through the parser
	elapsed "$BIN" "$DIR/garbage.bin"
}

ASCII() {
	# random printable ASCII through the parser
	elapsed "$BIN" "$DIR/ascii.bin"
}

ascii_sum=0
devnull_sum=0

echo
echo "ASCII  ${MIB} MiB x ${ROUNDS}"
for ((i = 1; i <= ROUNDS; i++)); do
	t=$(ASCII)
	echo "  round $i  ${t}s  $(mib_s "$t") MiB/s"
	ascii_sum=$(awk -v a="$ascii_sum" -v b="$t" 'BEGIN { printf "%.6f", a + b }')
done
ASCII_AVERAGE=$(awk -v m="$MIB" -v n="$ROUNDS" -v t="$ascii_sum" \
	'BEGIN { printf "%.2f", (m * n) / t }')

echo
echo "DEVNULL  ${MIB} MiB x ${ROUNDS}"
for ((i = 1; i <= ROUNDS; i++)); do
	t=$(DEVNULL)
	echo "  round $i  ${t}s  $(mib_s "$t") MiB/s"
	devnull_sum=$(awk -v a="$devnull_sum" -v b="$t" 'BEGIN { printf "%.6f", a + b }')
done
DEVNULL_AVERAGE=$(awk -v m="$MIB" -v n="$ROUNDS" -v t="$devnull_sum" \
	'BEGIN { printf "%.2f", (m * n) / t }')

echo
printf 'ASCII_AVERAGE    %s MiB/s\n' "$ASCII_AVERAGE"
printf 'DEVNULL_AVERAGE  %s MiB/s\n' "$DEVNULL_AVERAGE"
