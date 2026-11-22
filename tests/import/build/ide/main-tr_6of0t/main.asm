    mov r7, r0
    sumi r7, #2
    shli r7, #16
    diffi r7, #32
    stwi r0, 0(r7)
    j piper_funcion_1
piper_funcion_0:
    mov r5, r7
    diffi r5, #104
    mov r6, r0
    sumi r6, #2
    shli r6, #16
    sumi r6, #-4096
    ltn r5, r6
    jnz r5, piper_error_pila
    diffi r7, #8
    stwi r1, 0(r7)
    j piper_funcion_0_bloque_0
piper_funcion_0_bloque_0:
    lowi r1, 0(r7)
    lowi r2, 0(r7)
    mov r5, r1
    mulh r5, r2
    mul r1, r2
    mov r6, r1
    sari r6, #31
    xor r5, r6
    jnz r5, piper_error_aritmetico
    stwi r1, 4(r7)
    lowi r1, 4(r7)
    j piper_salida_funcion_0
piper_salida_funcion_0:
    sumi r7, #8
    lowi r6, 0(r7)
    j piper_buscar_retorno
piper_funcion_1:
    mov r5, r7
    diffi r5, #108
    mov r6, r0
    sumi r6, #2
    shli r6, #16
    sumi r6, #-4096
    ltn r5, r6
    jnz r5, piper_error_pila
    diffi r7, #12
    j piper_funcion_1_bloque_0
piper_funcion_1_bloque_0:
    mov r1, r0
    stwi r1, 4(r7)
    mov r1, r0
    sumi r1, #1
    stwi r1, 0(r7)
    j piper_funcion_1_bloque_1
piper_funcion_1_bloque_1:
    lowi r2, 0(r7)
    mov r1, r0
    sumi r1, #4
    mov r5, r0
    sumi r5, #-32768
    shli r5, #16
    xor r2, r5
    xor r1, r5
    ltn r2, r1
    xor r1, r5
    jnz r2, piper_funcion_1_bloque_2
    j piper_funcion_1_bloque_3
piper_funcion_1_bloque_2:
    lowi r1, 0(r7)
    stwi r1, -28(r7)
    stwi r2, -24(r7)
    stwi r3, -20(r7)
    stwi r4, -16(r7)
    stwi r5, -12(r7)
    stwi r6, -8(r7)
    mov r5, r0
    sumi r5, #1
    stwi r5, -32(r7)
    diffi r7, #32
    j piper_funcion_0
piper_volver_llamada_1:
    stwi r1, 28(r7)
    lowi r1, 4(r7)
    lowi r2, 8(r7)
    lowi r3, 12(r7)
    lowi r4, 16(r7)
    lowi r5, 20(r7)
    lowi r6, 24(r7)
    lowi r1, 28(r7)
    sumi r7, #32
    stwi r1, 8(r7)
    lowi r1, 4(r7)
    lowi r2, 8(r7)
    mov r5, r1
    mov r6, r2
    sum r1, r2
    xor r5, r1
    xor r6, r1
    and r5, r6
    shri r5, #31
    jnz r5, piper_error_aritmetico
    stwi r1, 4(r7)
    j piper_funcion_1_bloque_4
piper_funcion_1_bloque_3:
    lowi r2, 4(r7)
    mov r1, r0
    sumi r1, #10
    mov r5, r0
    sumi r5, #-32768
    shli r5, #16
    xor r2, r5
    xor r1, r5
    gtn r2, r1
    xor r1, r5
    jnz r2, piper_funcion_1_bloque_5
    j piper_funcion_1_bloque_6
piper_funcion_1_bloque_4:
    lowi r1, 0(r7)
    sumi r1, #1
    stwi r1, 0(r7)
    j piper_funcion_1_bloque_1
piper_funcion_1_bloque_5:
    lowi r1, 4(r7)
    j piper_salida_funcion_1
piper_funcion_1_bloque_6:
    mov r1, r0
    j piper_salida_funcion_1
piper_salida_funcion_1:
    sumi r7, #12
    lowi r6, 0(r7)
    j piper_buscar_retorno
piper_buscar_retorno:
    jz r6, piper_fin_main
    mov r5, r6
    diffi r5, #1
    jz r5, piper_volver_llamada_1
piper_error_retorno:
    j piper_error_retorno
piper_fin_main:
    sumi r7, #32
piper_fin_programa:
    j piper_fin_programa
piper_error_pila:
    j piper_error_pila
piper_error_division_cero:
    j piper_error_division_cero
piper_error_exponente_negativo:
    j piper_error_exponente_negativo
piper_error_aritmetico:
    j piper_error_aritmetico
piper_error_indice:
    j piper_error_indice
