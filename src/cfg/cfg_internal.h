#ifndef PIPER_CFG_INTERNAL_H
#define PIPER_CFG_INTERNAL_H

#include "cfg.h"

/* C1: reservar un grafo por rutina y sus nodos ENTRY/BLOCK/EXIT, todavía sin
 * aristas ni vecinos. ir debe estar validada y out apuntar inicialmente a NULL.
 * Solo entrega propiedad de ese almacenamiento al completar todas las reservas.
 * Uso interno: cfg_build lo completará con C2/C3 antes de publicarlo como CFG.
 * No calcula alcanzabilidad ni lee instrucciones para resolver conexiones. */
CfgStatus cfg_program_create(const IrProgram *ir, CfgProgram **out,
                             CfgDiagnostic *diagnostic);

#endif
