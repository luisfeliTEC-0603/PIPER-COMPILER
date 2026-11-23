#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."

out=$(mktemp -d)
trap 'rm -rf -- "$out"' EXIT

compile() {
    local name=$1 source_file=$2
    shift 2
    bin/pipoc mem "$source_file" -o "$out/$name.mem" --emit-ir "$out/$name.ir" "$@"
    test -s "$out/$name.mem"
}

expect_error() {
    local expected=$1 status=0
    shift
    "$@" >"$out/error.log" 2>&1 || status=$?
    if [[ $status -ne 1 ]] || ! grep -Fq "$expected" "$out/error.log"; then
        printf 'FAIL (estado %s): %s\n' "$status" "$*" >&2
        cat "$out/error.log" >&2
        return 1
    fi
}

compile direct tests/archivadas/import/basic.rwt
grep -q '^function cuadrado(' "$out/direct.ir"
test "$(grep -c '^function ' "$out/direct.ir")" -eq 2
printf 'PASS importación directa y selectiva\n'

cp tests/archivadas/import/basic.rwt "$out/include.rwt"
compile include "$out/include.rwt" -I tests/archivadas/import
printf 'PASS búsqueda con -I\n'

cat >"$out/bridge.rwt" <<'RWT'
bring cuadrado from "math_ops.rwt".
routine B32 sumar_uno[B32 x] { output x + 1. }
routine B32 calcular[B32 x] { output sumar_uno[cuadrado[x]]. }
RWT
cat >"$out/transitive.rwt" <<'RWT'
bring calcular from "bridge.rwt".
bring cuadrado from "./math_ops.rwt".
routine B32 main[] { output calcular[3] + cuadrado[2]. }
RWT
compile transitive "$out/transitive.rwt" -I tests/archivadas/import
grep -q '^function sumar_uno(' "$out/transitive.ir"
test "$(grep -c '^function cuadrado(' "$out/transitive.ir")" -eq 1
printf 'PASS importación transitiva con dependencia compartida\n'

expect_error "'resta' no ha sido declarado" bin/pipoc --check tests/archivadas/import/main.rwt
printf 'PASS rechazo de una llamada a una rutina sin importar\n'

cat >"$out/missing-file.rwt" <<'RWT'
bring ausente from "ausente.rwt".
routine B32 main[] { output 7. }
RWT
cat >"$out/missing-symbol.rwt" <<'RWT'
bring ausente from "math_ops.rwt".
routine B32 main[] { output 7. }
RWT
for name in missing-file missing-symbol; do
    if [[ $name == missing-file ]]; then
        expected="no se encontró el archivo 'ausente.rwt'"
    else
        expected="no se encontró el símbolo 'ausente'"
    fi
    expect_error "$expected" bin/pipoc --check "$out/$name.rwt" -I tests/archivadas/import
    expect_error "$expected" bin/pipoc "$out/$name.rwt" -I tests/archivadas/import -o "$out/$name.bin"
    test ! -e "$out/$name.bin"
    printf 'PASS %s\n' "$name"
done

cat >"$out/cycle-main.rwt" <<'RWT'
bring circular from "cycle-lib.rwt".
routine B32 main[] { output circular[]. }
RWT
cat >"$out/cycle-lib.rwt" <<'RWT'
bring main from "cycle-main.rwt".
routine B32 circular[] { output 7. }
RWT
expect_error 'importación circular detectada' bin/pipoc --check "$out/cycle-main.rwt"
printf 'PASS rechazo de importación circular\n'

cat >"$out/conflict.rwt" <<'RWT'
bring cuadrado from "math_ops.rwt".
routine B32 cuadrado[B32 x] { output x. }
routine B32 main[] { output cuadrado[6]. }
RWT
expect_error "conflicto de nombres: 'cuadrado'" bin/pipoc --check "$out/conflict.rwt" -I tests/archivadas/import
printf 'PASS rechazo de conflicto de nombres\n'
