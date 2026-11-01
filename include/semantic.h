#ifndef SEMANTIC_H
#define SEMANTIC_H
#include "ast.h"
#include "symbol_table.h"
/* Un programa requiere una rutina global main sin parámetros.
 * Retorna 1 si el AST es válido; informa errores en stderr y reconstruye la
 * tabla desde las declaraciones del AST, incluso cuando encuentra errores. */
int analyze_semantics(AstNode *root, SymbolTable *symbol_table);
/* Habilita opcionalmente la salida de comprobaciones aprobadas en stdout. */
int analyze_semantics_with_trace(AstNode *root, SymbolTable *symbol_table,
                                 int trace_enabled);
#endif
