/**
 * @file ir_memory.c
 * @brief Estructura de traducción de indexación e inicialización/copia de datos.
 *
 * Solo declaraciones. Flujo previsto de lectura: índices -> comprobaciones
 * -> dirección estable -> LOAD en el generador de expresiones.
 * Para escritura, la dirección precede al origen y a un único STORE.
 */
#include "ir_internal.h"

/**
 * @brief Calculará una dirección de elemento, sin cargar su contenido.
 * @param ctx Contexto para emitir valores y comprobaciones, en orden.
 * @param index AST_INDEX_EXPRESSION validado de vector o matriz.
 * @param out_address Temporal tipado como dirección, definido solo en IR_OK.
 * @return Estado; índices fuera de rango dinámico se representan con una
 * operación CHECK_BOUNDS potencialmente fallida, no se ejecutan al compilar.
 *
 * Cada índice se evalúa y comprueba una vez. Para matrices usar orden por filas:
 * base + (fila * columnas + columna) * piper_type_storage_size(tipo_elemento).
 * Un parámetro de arreglo aporta una base por referencia, no una nueva copia.
 */
IrStatus ir_generate_index_address(IrContext *ctx, const AstNode *index,
                                   IrOperand *out_address);

/**
 * @brief Emitirá inicialización desde literal o copia de un arreglo completo.
 * @param ctx Estado mutable que recibe las operaciones por elemento.
 * @param target Destino arreglo validado y enlazado con su declaración final.
 * @param value Literal rectangular o arreglo compatible, ambos prestados.
 * @return Estado; no devuelve valor escalar ni asigna memoria de temporales.
 * @note Recorre elementos por filas en el orden actual del backend. No cambia
 * el orden por una copia masiva ni presupone que dos referencias no sean alias.
 * Las declaraciones sin inicializador no introducen escrituras nuevas.
 */
IrStatus ir_generate_array_assignment(IrContext *ctx, const AstNode *target,
                                      const AstNode *value);
