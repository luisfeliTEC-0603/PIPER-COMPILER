
    mov r7, r0
    sumi r7, #1
    shli r7, #16
    diffi r7, #32
    stwi r0, 0(r7)
    j main

leer:
    mov r5, r7
    diffi r5, #76
    mov r6, r0
    sumi r6, #15
    shli r6, #12
    ltn r5, r6
    jnz r5, error_pila
    diffi r7, #12
    stwi r1, 0(r7)
    stwi r2, 4(r7)
    stwi r3, 8(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    lowi r1, 4(r7)
    mov r3, r0
    sumi r3, #2
    mov r4, r1
    ltn r4, r3
    jz r4, error_indice
    lowi r2, 8(r7)
    mov r3, r0
    sumi r3, #2
    mov r4, r2
    ltn r4, r3
    jz r4, error_indice
    mul r1, r3
    sum r1, r2
    shli r1, #2
    lowi r2, 0(r7)
    sum r1, r2
    lowi r2, -24(r7)
    lowi r3, -20(r7)
    lowi r4, -16(r7)
    lowi r1, 0(r1)
    j epilogo_leer
epilogo_leer:
    sumi r7, #12
    lowi r6, 0(r7)
    j retornos

main:
    mov r5, r7
    diffi r5, #80
    mov r6, r0
    sumi r6, #15
    shli r6, #12
    ltn r5, r6
    jnz r5, error_pila
    diffi r7, #16
    mov r5, r0
    sumi r5, #1
    mov r6, r7
    stwi r5, 0(r6)
    mov r5, r0
    sumi r5, #2
    mov r6, r7
    sumi r6, #4
    stwi r5, 0(r6)
    mov r5, r0
    sumi r5, #3
    mov r6, r7
    sumi r6, #8
    stwi r5, 0(r6)
    mov r5, r0
    sumi r5, #4
    mov r6, r7
    sumi r6, #12
    stwi r5, 0(r6)
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    stwi r5, -12(r7)
    stwi r6, -8(r7)
    mov r1, r0
    sumi r1, #1
    stwi r1, -32(r7)
    mov r1, r7
    mov r2, r0
    diffi r2, #1
    mov r3, r0
    diffi r7, #32
    j leer
retorno_1:
    stwi r1, 28(r7)
    lowi r1, 4(r7)
    lowi r2, 8(r7)
    lowi r3, 12(r7)
    lowi r4, 16(r7)
    lowi r5, 20(r7)
    lowi r6, 24(r7)
    lowi r1, 28(r7)
    sumi r7, #32
    j epilogo_main
epilogo_main:
    sumi r7, #16
    lowi r6, 0(r7)
    j retornos

retornos:
    jz r6, fin_de_main
    mov r5, r6
    diffi r5, #1
    jz r5, retorno_1
error_retorno:
    j error_retorno
fin_de_main:
    sumi r7, #32
fin_programa:
    j fin_programa
error_pila:
    j error_pila
error_division_cero:
    j error_division_cero
error_exponente_negativo:
    j error_exponente_negativo
error_aritmetico:
    j error_aritmetico
error_indice:
    j error_indice
