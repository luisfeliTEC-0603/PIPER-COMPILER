#ifndef SEMANTIC_H
#define SEMANTIC_H
#include "ast.h"
#include "symbol_table.h"
/* Retorna 1 si el AST es válido; acumula errores en stderr y muestra en stdout
 * cada comprobación aprobada, con su línea y contexto de ámbito.
 * Reconstruye la tabla desde las declaraciones del AST, incluso con errores. */
int analyze_semantics(AstNode *root, SymbolTable *symbol_table);
#endif
