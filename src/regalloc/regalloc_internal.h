#ifndef PIPER_REGALLOC_INTERNAL_H
#define PIPER_REGALLOC_INTERNAL_H

#include "regalloc.h"

/* Descarta solo liveness, conserva los mapas de regalloc_prepare y permite
 * reintentar tras un fallo de reserva. Compartida con el destructor general. */
void regalloc_liveness_clear(RegallocAnalysis *analysis);

#endif
