
    mov r7, r0
    sumi r7, #2
    shli r7, #16
    diffi r7, #32
    stwi r0, 0(r7)
    j _R_main

_R_factorial:
    mov r5, r7
    diffi r5, #108
    mov r6, r0
    sumi r6, #2
    shli r6, #16
    sumi r6, #-4096
    ltn r5, r6
    jnz r5, _IR_error_pila
    diffi r7, #12
    stwi r1, 0(r7)
    mov r5, r0
    sumi r5, #1
    stwi r5, 4(r7)
    mov r5, r0
    sumi r5, #1
    stwi r5, 8(r7)
_IR_inicio_0:
    lowi r5, 8(r7)
    lowi r6, 0(r7)
    stwi r1, -4(r7)
    mov r1, r0
    sumi r1, #-32768
    shli r1, #16
    xor r5, r1
    xor r6, r1
    gtn r5, r6
    xor r6, r1
    lowi r1, -4(r7)
    xori r5, #1
    jz r5, _IR_fin_1
    lowi r5, 4(r7)
    lowi r6, 8(r7)
    stwi r5, -32(r7)
    stwi r6, -4(r7)
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    lowi r1, -32(r7)
    lowi r2, -4(r7)
    mov r5, r1
    mulh r5, r2
    mul r1, r2
    mov r4, r1
    sari r4, #31
    xor r5, r4
    jnz r5, _IR_error_aritmetico
    mov r5, r1
    lowi r1, -28(r7)
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    stwi r5, 4(r7)
    lowi r5, 8(r7)
    mov r6, r0
    sumi r6, #1
    stwi r5, -32(r7)
    stwi r6, -4(r7)
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    lowi r1, -32(r7)
    lowi r2, -4(r7)
    mov r3, r1
    sum r1, r2
    mov r4, r3
    xor r4, r1
    mov r5, r2
    xor r5, r1
    and r4, r5
    shri r4, #31
    jnz r4, _IR_error_aritmetico
    mov r5, r1
    lowi r1, -28(r7)
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    stwi r5, 8(r7)
    j _IR_inicio_0
_IR_fin_1:
    lowi r1, 4(r7)
    j _IR_epilogo_factorial
_IR_epilogo_factorial:
    sumi r7, #12
    lowi r6, 0(r7)
    j _IR_retornos

_R_main:
    mov r5, r7
    diffi r5, #68
    mov r6, r0
    sumi r6, #2
    shli r6, #16
    sumi r6, #-4096
    ltn r5, r6
    jnz r5, _IR_error_pila
    diffi r7, #4
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    stwi r5, -12(r7)
    stwi r6, -8(r7)
    mov r1, r0
    sumi r1, #1
    stwi r1, -32(r7)
    mov r1, r0
    sumi r1, #5
    diffi r7, #32
    j _R_factorial
_IR_retorno_1:
    stwi r1, 28(r7)
    lowi r1, 4(r7)
    lowi r2, 8(r7)
    lowi r3, 12(r7)
    lowi r4, 16(r7)
    lowi r5, 20(r7)
    lowi r6, 24(r7)
    lowi r5, 28(r7)
    sumi r7, #32
    stwi r5, 0(r7)
    lowi r1, 0(r7)
    j _IR_epilogo_main
_IR_epilogo_main:
    sumi r7, #4
    lowi r6, 0(r7)
    j _IR_retornos

_IR_retornos:
    jz r6, _IR_fin_de_main
    mov r5, r6
    diffi r5, #1
    jz r5, _IR_retorno_1
_IR_error_retorno:
    j _IR_error_retorno
_IR_fin_de_main:
    sumi r7, #32
_IR_fin_programa:
    j _IR_fin_programa
_IR_error_pila:
    j _IR_error_pila
_IR_error_division_cero:
    j _IR_error_division_cero
_IR_error_exponente_negativo:
    j _IR_error_exponente_negativo
_IR_error_aritmetico:
    j _IR_error_aritmetico
_IR_error_indice:
    j _IR_error_indice
