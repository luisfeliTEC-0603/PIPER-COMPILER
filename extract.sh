#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Uso: ./extract.sh <ejecución|dump.mem> <dirección> <tamaño> <salida>

El primer argumento puede ser:
  - El nombre de una ejecución creada por run.sh, por ejemplo call_simple.
  - La ruta explícita a un archivo de volcado .mem.

Ejemplos:
  ./extract.sh call_simple 0x10000 32 salida.bin
  ./extract.sh build/runs/call_simple/memory.dump.mem 0x10000 32 salida.bin

PORYGON se detecta en ../../ProyectoArqui/PORYGON. Para otra ubicación:
  PORYGON_DIR=/ruta/a/PORYGON ./extract.sh call_simple 0x10000 32 salida.bin
EOF
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 2
}

absolute_file() {
    local path=$1
    local directory
    [[ -f "$path" ]] || fail "no existe el archivo '$path'"
    directory=$(cd "$(dirname "$path")" && pwd -P)
    printf '%s/%s\n' "$directory" "$(basename "$path")"
}

compiler_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
[[ $# -gt 0 ]] || { usage >&2; exit 2; }
[[ $1 != "-h" && $1 != "--help" ]] || { usage; exit 0; }
[[ $# -eq 4 ]] || { usage >&2; exit 2; }

dump_selector=$1
address=$2
size=$3
output_file=$4

[[ $address =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]] ||
    fail "dirección inválida '$address'"
[[ $size =~ ^[1-9][0-9]*$ ]] || fail "el tamaño debe ser un entero positivo"
[[ -n "$output_file" ]] || fail "la ruta de salida está vacía"

if [[ -f "$dump_selector" ]]; then
    dump_file=$(absolute_file "$dump_selector")
elif [[ $dump_selector != */* ]]; then
    dump_file="$compiler_dir/build/runs/$dump_selector/memory.dump.mem"
    [[ -f "$dump_file" ]] || fail \
        "no existe la ejecución '$dump_selector' en build/runs/"
else
    fail "no existe el dump '$dump_selector'"
fi

output_parent=$(dirname "$output_file")
[[ -d "$output_parent" ]] || fail "no existe el directorio de salida '$output_parent'"
output_file=$(cd "$output_parent" && pwd -P)/$(basename "$output_file")
[[ $output_file != "$dump_file" ]] || fail "la salida no puede sobrescribir el dump"

if [[ -n ${PORYGON_DIR:-} ]]; then
    porygon_candidate=$PORYGON_DIR
else
    porygon_candidate="$compiler_dir/../../ProyectoArqui/PORYGON"
fi
[[ -d "$porygon_candidate" ]] || fail \
    "no se encontró PORYGON; defina PORYGON_DIR=/ruta/a/PORYGON"
porygon_dir=$(cd "$porygon_candidate" && pwd -P)
extractor="$porygon_dir/tools/data_loader/extract_data.py"
[[ -f "$extractor" ]] || fail "no existe el extractor '$extractor'"

"${PYTHON:-python3}" "$extractor" \
    --memory "$dump_file" \
    --address "$address" \
    --size "$size" \
    --output "$output_file"

printf '[piper] dump   : %s\n' "$dump_file"
printf '[piper] salida : %s (%s bytes desde %s)\n' "$output_file" "$size" "$address"
