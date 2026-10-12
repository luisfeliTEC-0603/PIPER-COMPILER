#ifndef AST_TO_JSON_H
#define AST_TO_JSON_H

#include "ast.h"
#include <stdio.h>

// Exporta la raíz del AST a un archivo en formato JSON.
// Retorna 1 en caso de éxito, 0 en caso de error.
int ast_export_json(const AstNode *root, const char *filename);

#endif