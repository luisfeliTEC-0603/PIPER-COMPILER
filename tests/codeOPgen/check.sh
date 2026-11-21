#!/usr/bin/env bash
# Regresion CLI/emisor. --sim añade ejecucion del RTL existente de PORYGON.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
out="$root/build/runs/codeOPgen"
mkdir -p "$out"
simulator="$root/../PORYGON/tools/sim.sh"

check() {
    local name=$1 source=$2 expected=$3
    shift 3
    local base="$out/$name"
    bin/pipoc mem "$source" -o "$base.mem" --emit-asm "$base.asm" \
        --emit-bundles "$base.bundles" "$@" > "$base.compile.log" 2>&1 || {
        cat "$base.compile.log"; return 1;
    }
    if [[ ${simulate:-0} == 1 ]]; then
        local expectation="r1=$expected"
        if [[ $expected == piper_error_* ]]; then
            # Los errores software paran en su etiqueta, sin convertirse en
            # errores de acceso del RTL. Verificar el PC evita un falso PASS.
            local pc
            pc=$(awk -v label="$expected" '$1=="LABEL" && $2==label {print $3*8}' "$base.bundles")
            [[ -n $pc ]]
            expectation="halt_pc=$pc"
        fi
        "$simulator" "$base.mem" --out "$base.sim" --max-cycles 50000 \
            --expect "$expectation" --expect errors=0 > "$base.sim.log" 2>&1 || {
            cat "$base.sim.log"; return 1;
        }
    fi
    printf 'PASS %s\n' "$name"
}

simulate=0
if [[ ${1:-} == --sim ]]; then simulate=1; fi
for mode in plain optimized; do
    flags=()
    if [[ $mode == optimized ]]; then flags=(--unroll 2 --dce); fi
    while read -r name source expected; do
        check "$name-$mode" "$source" "$expected" "${flags[@]}"
    done <<'CASES'
scalar tests/ir/scalar.rwt 14
branches tests/ir/branches.rwt 1
break tests/ir/loop_break.rwt 2
matrix tests/ir/matrix.rwt 10
startup tests/cfg/calls.rwt 3
effects tests/dce/calls.rwt 16
effects-branch tests/dce/branches.rwt 6
effects-loop tests/dce/loop.rwt 2
pruning tests/dce/routines.rwt 7
arithmetic tests/codeOPgen/arithmetic.rwt 238
arguments tests/codeOPgen/arguments.rwt 21
loop tests/codeOPgen/loop.rwt 10
recursive tests/codeOPgen/recursive.rwt 120
signed tests/codeOPgen/signed.rwt 7
unsigned tests/codeOPgen/unsigned.rwt 2863311530
negative-boundary tests/codeOPgen/negative_boundary.rwt 2147483648
crypto tests/codeOPgen/crypto.rwt 164495
array-parameter tests/codeOPgen/array_parameter.rwt 12
startup-spill tests/codeOPgen/startup_spill.rwt 21
stack tests/codeOPgen/stack.rwt piper_error_pila
overflow tests/codeOPgen/overflow.rwt piper_error_aritmetico
divzero tests/codeOPgen/divzero.rwt piper_error_division_cero
negative-power tests/codeOPgen/negative_power.rwt piper_error_exponente_negativo
bounds tests/codeOPgen/bounds.rwt piper_error_indice
CASES
done
# Tres decisiones independientes: U/D modifican codigo; S solo se necesita
# con presion. Verificar las ocho combinaciones sobre un caso que cabe.
for unroll in off 2; do
    for dce in off on; do
        for spill in off on; do
            flags=(--unroll "$unroll" --spill "$spill")
            if [[ $dce == on ]]; then flags+=(--dce); fi
            check "choices-$unroll-$dce-$spill" tests/codeOPgen/loop.rwt 10 "${flags[@]}"
        done
    done
done
# Una entrega incompleta debe fallar antes de publicar un nuevo ejecutable.
rejected="$out/spill-disabled.mem"
if bin/pipoc mem tests/codeOPgen/arguments.rwt --spill off -o "$rejected" \
    > "$out/spill-disabled.log" 2>&1; then
    printf 'FAIL: se acepto presion sin spilling\n' >&2; exit 1
fi
grep -q 'Habilite "Permitir spilling"' "$out/spill-disabled.log"
printf 'PASS spill-disabled: diagnostico accionable\n'
# Este estrechamiento es rechazado por semantica antes de llegar al emisor.
if bin/pipoc --check tests/codeOPgen/narrow.rwt > "$out/narrow.log" 2>&1; then
    printf 'FAIL: se acepto estrechamiento no permitido\n' >&2; exit 1
fi
grep -q 'Tipo o rango incompatible' "$out/narrow.log"
printf 'PASS narrow: rechazo semantico\n'
