/**
 * @file ir_generate.c
 * @brief Estructura del recorrido AST -> rutinas -> bloques de IR.
 *
 * Solo declaraciones, sin lógica de traducción ni integración con main.
 * Secuencia prevista de ir_generate:
 * 1. Revisar precondiciones y crear programa/contexto.
 * 2. Registrar declaraciones y firmas con sus enlaces semánticos finales.
 * 3. Generar arranque opcional y cuerpos de rutinas, sin repetir análisis.
 * 4. Finalizar bloques y validar el programa completo.
 * 5. Liberar contexto y publicar *out solo en éxito; descartar todo al fallar.
 */
#include "ir_internal.h"

/**
 * @brief Será la única entrada pública de construcción de IR desde el AST.
 * @param root AST_PROGRAM validado y prestado.
 * @param symbols Tabla semántica final, prestada hasta liberar la IR.
 * @param out Dirección válida con *out=NULL; recibirá propiedad solo en IR_OK.
 * @param diagnostic Error localizado opcional; ninguna cadena queda prestada.
 * @return Estado de generación/validación; en fallo no publica programa parcial.
 * @note Los retornos de lexer/parser/semántico se comprueban en el orquestador,
 * no se leen de archivos ni de mensajes. Esta función no ejecuta esos análisis.
 */
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);

/**
 * @brief Distribuirá declaraciones globales, sentencias de arranque y rutinas.
 * @param ctx Contexto con programa vacío y enlaces semánticos disponibles.
 * @param root AST_PROGRAM prestado.
 * @return Estado; las rutinas/bloques creados permanecen en ctx->program.
 * @note Registrar firmas antes de emitir cuerpos permite referencias por ID,
 * pero no admite llamadas que el semántico haya rechazado. Los globales son
 * datos; solo las sentencias ejecutables se incluyen en el arranque sintético.
 */
IrStatus ir_generate_program(IrContext *ctx, const AstNode *root);

/**
 * @brief Traducirá una rutina global ya registrada a su secuencia de bloques.
 * @param ctx Contexto del programa; establecerá/restaurará rutina y ámbito.
 * @param routine Nodo AST_ROUTINE_DECLARATION aprobado.
 * @param id ID de la rutina creada previamente.
 * @return Estado; agrega parámetros simbólicos, locales, temporales y bloques.
 * @note El ámbito del cuerpo se obtiene de la rutina/function_scope: el nodo
 * AST_BLOCK del cuerpo no está necesariamente decorado por el semántico.
 * Reinicia contadores por rutina y la pila de break. Sin prólogo ni registros.
 */
IrStatus ir_generate_routine(IrContext *ctx, const AstNode *routine,
                             IrRoutineId id);
