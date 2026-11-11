/** @file ir_generate.c
 * @brief Estructura del orquestador. Solo declaraciones, sin integración con main.
 * Flujo: crear programa/contexto -> ir_build_program -> ir_validate -> publicar.
 * Ante fallo: liberar pila y programa parcial; dejar *out=NULL.
 */
#include "ir_internal.h"

/** Entradas: AST aprobado y tabla final prestada. Salidas: estado, diagnóstico
 * opcional y programa propio solo en éxito. No repite lexer/parser/semántico.
 * La pila pertenece al contexto; el modelo solo se transfiere tras validarlo. */
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);

/** Entrada: ctx->root. Salida: rutinas/bloques en ctx->program o ctx->error.
 * Registra funciones por su Symbol final y traduce cada cuerpo con ir_stmt.
 * Usa function_scope de la rutina; no exige anotación en el bloque del cuerpo.
 * Los datos siguen en la tabla; las sentencias globales van en arranque que
 * llama a main. No introduce prólogos, asignación de registros ni código máquina.
 * Helpers exclusivos de este archivo serán static cuando se implementen. */
void ir_build_program(IrContext *ctx);
