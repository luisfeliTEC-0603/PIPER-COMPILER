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

/* C2: completar un almacenamiento nuevo de C1 con aristas y vecinos, sin
 * modificar IR ni calcular alcance. No llamar dos veces sobre el mismo CFG.
 * Conserva propiedad en el llamador también al fallar; descartar parciales con
 * cfg_program_free. cfg_build incorporará alcance/revisión en C3. */
CfgStatus cfg_connect(CfgProgram *cfg, CfgDiagnostic *diagnostic);

#endif
