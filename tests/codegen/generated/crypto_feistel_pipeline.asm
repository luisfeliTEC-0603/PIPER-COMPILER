
    mov r7, r0
    sumi r7, #2
    shli r7, #16
    diffi r7, #32
    stwi r0, 0(r7)
    j _R_main

_R_main:
    mov r5, r7
    diffi r5, #96
    mov r6, r0
    sumi r6, #2
    shli r6, #16
    sumi r6, #-4096
    ltn r5, r6
    jnz r5, _IR_error_pila
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    mov r1, r0
    mov r3, r0
    sumi r3, #2
    mov r4, r1
    ltn r4, r3
    jz r4, _IR_error_indice
    shli r1, #2
    mov r2, r0
    sumi r2, #1
    shli r2, #16
    sum r1, r2
    mov r5, r1
    lowi r1, -28(r7)
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    lowi r5, 0(r5)
    mov r6, r0
    eor r5, r6
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    stwi r5, -12(r7)
    mov r1, r0
    sumi r1, #1
    mov r3, r0
    sumi r3, #2
    mov r4, r1
    ltn r4, r3
    jz r4, _IR_error_indice
    shli r1, #2
    mov r2, r0
    sumi r2, #1
    shli r2, #16
    sum r1, r2
    mov r6, r1
    lowi r1, -28(r7)
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    lowi r5, -12(r7)
    lowi r6, 0(r6)
    stwi r6, -4(r7)
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    stwi r5, -12(r7)
    mov r1, r0
    lowi r2, -4(r7)
    eor r2, r1
    mov r6, r2
    lowi r1, -28(r7)
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    lowi r5, -12(r7)
    and r5, r6
    jz r5, _IR_fin_0
    mov r5, r0
    sumi r5, #291
    shli r5, #16
    sumi r5, #17767
    mov r6, r0
    sumi r6, #1
    shli r6, #16
    stwi r5, 0(r6)
    mov r5, r0
    sumi r5, #-30292
    shli r5, #16
    sumi r5, #-12817
    mov r6, r0
    sumi r6, #1
    shli r6, #16
    sumi r6, #4
    stwi r5, 0(r6)
_IR_fin_0:
    mov r1, r0
    sumi r1, #-8207
    shli r1, #16
    sumi r1, #-2198
    lopw r1, 0
    mov r1, r0
    sumi r1, #-22106
    shli r1, #16
    sumi r1, #28696
    lopw r1, 1
    mov r1, r0
    sumi r1, #-9655
    shli r1, #16
    sumi r1, #-9465
    lopw r1, 2
    mov r1, r0
    sumi r1, #5219
    shli r1, #16
    sumi r1, #6075
    lopw r1, 3
    auth
    mov r1, r0
    sumi r1, #-24141
    shli r1, #16
    sumi r1, #-15404
    write r1, 0, 0
    mov r3, r0
    sumi r3, #1
    shli r3, #16
    lowi r1, 0(r3)
    lowi r2, 4(r3)
    feistel r1, r2, 0, 0
    stwi r1, 0(r3)
    stwi r2, 4(r3)
    mov r3, r0
    sumi r3, #1
    shli r3, #16
    lowi r1, 0(r3)
    lowi r2, 4(r3)
    feistel r1, r2, 1, 0
    stwi r1, 0(r3)
    stwi r2, 4(r3)
    mov r3, r0
    sumi r3, #1
    shli r3, #16
    lowi r1, 0(r3)
    lowi r2, 4(r3)
    feistel r1, r2, 2, 0
    stwi r1, 0(r3)
    stwi r2, 4(r3)
    mov r3, r0
    sumi r3, #1
    shli r3, #16
    lowi r1, 0(r3)
    lowi r2, 4(r3)
    feistel r1, r2, 3, 0
    stwi r1, 0(r3)
    stwi r2, 4(r3)
    rvk
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    mov r1, r0
    mov r3, r0
    sumi r3, #2
    mov r4, r1
    ltn r4, r3
    jz r4, _IR_error_indice
    shli r1, #2
    mov r2, r0
    sumi r2, #1
    shli r2, #16
    sum r1, r2
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    lowi r1, 0(r1)
    j _IR_epilogo_main
_IR_epilogo_main:
    lowi r6, 0(r7)
    j _IR_retornos

_IR_retornos:
    jz r6, _IR_fin_de_main
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
