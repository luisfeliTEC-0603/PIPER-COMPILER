/** @file ir_generate.c
 * @brief Orquestación del futuro recorrido de IR; solo declaraciones.
 * No integrar con main hasta completar y comprobar el módulo.
 */
#include "ir_internal.h"

/** Entradas: AST_PROGRAM aprobado y tabla semántica final de la misma ejecución.
 * Salidas: IrStatus explícito, diagnóstico opcional y *out propio solo en éxito.
 *
 * Secuencia que implementará este punto de entrada:
 * 1. Comprobar precondiciones: out válido con *out=NULL; entradas prestadas.
 * 2. Crear programa y contexto; IDs opcionales en IR_NO_ID, error.status=IR_OK.
 * 3. Llamar ir_build_program para producir el TAC en bloques.
 * 4. Validar el modelo antes de publicarlo; copiar el diagnóstico si hubo fallo.
 * 5. Liberar pila del contexto siempre y programa parcial solo al fallar.
 *
 * No ejecuta análisis previos, no abre archivos y no modifica AST ni símbolos.
 * La tabla debe sobrevivir a la IR y a los consumidores posteriores. */
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);

/** Entrada: ctx->root y programa provisional; salida: rutinas/bloques o error.
 * Registra las funciones globales por su Symbol resuelto y traduce cuerpos con
 * ir_stmt. Preparar firmas no amplía la visibilidad aprobada por el semántico.
 * Por rutina: fijar current_routine, entrada y contador de temporales; mantener
 * la pila de break vacía al entrar/salir. El ámbito se toma de function_scope:
 * el AST_BLOCK del cuerpo no está necesariamente anotado.
 * Los datos permanecen en la tabla; solo las sentencias globales requieren un
 * arranque sintético que llama a main. Sin registros, prólogos ni ejecución.
 * Auxiliares exclusivos de este archivo serán static cuando se implementen. */
void ir_build_program(IrContext *ctx);
