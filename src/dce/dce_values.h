#ifndef PIPER_DCE_VALUES_H
#define PIPER_DCE_VALUES_H

#include "ir.h"

typedef struct {
    const Symbol *symbol;
    bool exposed;           /* Solo se utiliza durante la construccion del mapa. */
} DceLocalValue;

/* Dominio de una rutina: temporales [0, temp_count), seguidos de locales seguros.
 * locals es propio; los simbolos se prestan de la tabla semantica.
 * Este mapa no modifica IR ni el dominio de temporales usado por regalloc.
 */
typedef struct {
    size_t temp_count;
    DceLocalValue *locals;
    size_t local_count;
} DceValueMap;

/* IR aprobada por el frontend; routine_index es valido y *out empieza en cero.
 * Solo incluye escalares de esta rutina sin direccion expuesta ni referencias
 * desde otras rutinas. En fallo de reserva/overflow deja *out intacto.
 */
bool dce_values_build(const IrProgram *ir, size_t routine_index, DceValueMap *out);
void dce_values_free(DceValueMap *values);

/* IR_NO_ID significa que el operando no pertenece al dominio: NO que este muerto.
 * El mapa corresponde a la misma version del programa que el operando.
 */
size_t dce_value_id(const DceValueMap *values, IrOperand operand);

#endif
