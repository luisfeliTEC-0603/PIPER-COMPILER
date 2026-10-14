#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Uso: ./run.sh <programa.rwt> [ciclos] [opciones]

Opciones:
  --data <datos.mem>       Inicializa la memoria de datos del procesador.
  --out <directorio>       Guarda todos los artefactos en este directorio.
  --expect <clave=valor>   Verifica PC, errores o un registro r0-r7. Repetible.
  -h, --help               Muestra esta ayuda.

PORYGON se detecta en ../../ProyectoArqui/PORYGON. Para otra ubicación:
  PORYGON_DIR=/ruta/a/PORYGON ./run.sh programa.rwt

Ejemplos:
  ./run.sh tests/codegen/call_simple.rwt
  ./run.sh tests/codegen/call_simple.rwt 300 --expect r1=10
  ./run.sh programa.rwt 500 --data datos.mem --expect errors=0
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

source_file=$(absolute_file "$1")
shift
cycles=200
if [[ $# -gt 0 && $1 =~ ^[1-9][0-9]*$ ]]; then
    cycles=$1
    shift
fi

data_file=""
output_dir=""
expectations=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --data)
            [[ $# -ge 2 ]] || fail "--data requiere una ruta"
            data_file=$(absolute_file "$2")
            shift 2
            ;;
        --out)
            [[ $# -ge 2 ]] || fail "--out requiere un directorio"
            output_dir=$2
            shift 2
            ;;
        --expect)
            [[ $# -ge 2 ]] || fail "--expect requiere clave=valor"
            [[ $2 == *=* ]] || fail "expectativa inválida '$2'; use clave=valor"
            expectations+=("$2")
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *) fail "opción desconocida '$1'" ;;
    esac
done

if [[ -n ${PORYGON_DIR:-} ]]; then
    porygon_candidate=$PORYGON_DIR
else
    porygon_candidate="$compiler_dir/../../ProyectoArqui/PORYGON"
fi
[[ -d "$porygon_candidate" ]] || fail \
    "no se encontró PORYGON; defina PORYGON_DIR=/ruta/a/PORYGON"
porygon_dir=$(cd "$porygon_candidate" && pwd -P)
[[ -x "$porygon_dir/sim.sh" ]] || fail "falta el ejecutable '$porygon_dir/sim.sh'"

program_name=$(basename "$source_file")
program_name=${program_name%.*}
if [[ -z "$output_dir" ]]; then
    output_dir="$compiler_dir/build/runs/$program_name"
elif [[ $output_dir != /* ]]; then
    output_dir="$(pwd -P)/$output_dir"
fi
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd -P)
instruction_file="$output_dir/program.mem"
compiler_build_log="$output_dir/compiler-build.log"

printf 'Piper → PORYGON\n'
printf '  fuente : %s\n' "$source_file"
printf '  ciclos : %s\n' "$cycles"
printf '  salida : %s\n\n' "$output_dir"

if ! make -s -C "$compiler_dir" bin/pipoc >"$compiler_build_log" 2>&1; then
    printf 'La compilación de pipoc falló. Últimas líneas del log:\n' >&2
    tail -20 "$compiler_build_log" >&2
    exit 1
fi
"$compiler_dir/bin/pipoc" mem "$source_file" -o "$instruction_file"

simulation_args=("$instruction_file" "$cycles" --out "$output_dir")
if [[ -n "$data_file" ]]; then
    simulation_args+=(--data "$data_file")
fi
for expectation in "${expectations[@]}"; do
    simulation_args+=(--expect "$expectation")
done

"$porygon_dir/sim.sh" "${simulation_args[@]}"
