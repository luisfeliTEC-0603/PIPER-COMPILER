/**
 * @file ir.c
 * @brief Estructura del almacenamiento y descripción de operaciones de IR.
 *
 * Solo firmas: no hay asignación/liberación ni cálculo de efectos implementados.
 * Las declaraciones son puntos para añadir los cuerpos en la siguiente fase.
 * Las estructuras compartidas están en ir.h; el estado privado, en ir_internal.h.
 */
#include "ir_internal.h"

/**
 * @brief Creará una raíz vacía con IDs opcionales en IR_NO_ID.
 * @param symbols Tabla final prestada; no se copia ni se destruye.
 * @param out Puntero inicialmente NULL; adquirirá el programa solo en IR_OK.
 * @return IR_OK, IR_INVALID_INPUT o IR_OUT_OF_MEMORY.
 * @note No construirá rutinas ni recorrerá el AST.
 */
IrStatus ir_program_create(const SymbolTable *symbols, IrProgram **out);

/**
 * @brief Preparará mapas, contadores y estado de una generación.
 * @param ctx Contexto nuevo, sin recursos previos por liberar.
 * @param root AST validado, prestado.
 * @param symbols Tabla final prestada.
 * @param program Raíz mutable prestada, propiedad del orquestador.
 * @return Estado; no publicará IR ni asumirá propiedad del programa.
 */
IrStatus ir_context_init(IrContext *ctx, const AstNode *root,
                         const SymbolTable *symbols, IrProgram *program);

/**
 * @brief Liberará mapas y pila internos después del recorrido o de un error.
 * @param ctx Contexto inicializado o parcialmente inicializado; aceptará NULL.
 * @note No liberará program, AST ni símbolos. Evita una doble liberación al
 * transferir un programa correcto desde ir_generate al llamador.
 */
void ir_context_dispose(IrContext *ctx);

/**
 * @brief Registrará una declaración conservando su identidad semántica final.
 * @param ctx Contexto propietario del programa en construcción.
 * @param declaration Símbolo final prestado, nunca un temporal de usuario.
 * @param owner Ámbito final que contiene la declaración.
 * @param out ID nuevo o ya registrado para esa misma declaración en IR_OK.
 * @return Estado; copiará metadatos y conservará referencia prestada al símbolo.
 * @note Dos declaraciones con igual nombre deberán tener IDs distintos.
 */
IrStatus ir_add_symbol(IrContext *ctx, const Symbol *declaration,
                       const Scope *owner, IrSymbolId *out);

/**
 * @brief Creará la entrada de una rutina antes de traducir su cuerpo.
 * @param ctx Contexto del programa.
 * @param declaration Función resuelta; NULL solo para arranque sintético.
 * @param name Nombre prestado que se copiará al modelo.
 * @param is_startup Indica si representa las sentencias previas a main.
 * @param out ID de rutina registrado en éxito.
 * @return Estado; aún no emitirá parámetros físicos, prólogo ni instrucciones.
 */
IrStatus ir_add_routine(IrContext *ctx, const Symbol *declaration,
                        const char *name, bool is_startup, IrRoutineId *out);

/**
 * @brief Reservará la identidad de un resultado virtual en la rutina activa.
 * @param ctx Contexto con rutina activa.
 * @param type Tipo exacto ya resuelto, no IR_TYPE_UNSPECIFIED.
 * @param location Origen para diagnósticos.
 * @param out Operando temporal válido únicamente en éxito.
 * @return Estado; crear el temporal no emite su definición ni reserva pila.
 */
IrStatus ir_new_temp(IrContext *ctx, IrType type, SourceLocation location,
                     IrOperand *out);

/**
 * @brief Describirá efectos antes de registrar una instrucción.
 * @param program Metadatos prestados para distinguir globales y llamadas.
 * @param instruction Operación y atributos sin modificar.
 * @param out Efectos de memoria/cripto/error calculados en éxito.
 * @return Estado; no decidirá si la operación puede eliminarse o reordenarse.
 */
IrStatus ir_instruction_effects(const IrProgram *program,
                               const IrInstruction *instruction, IrEffects *out);

/**
 * @brief Recorrerá lecturas y definiciones explícitas de la operación.
 * @param instruction Instrucción prestada, incluidos argumentos de llamada.
 * @param visitor Callback que recibe cada acceso sin adquirir el operando.
 * @param user_data Contexto opaco prestado para el callback.
 * @return Estado o primer error del callback; no calcula variables vivas.
 */
IrStatus ir_instruction_visit_accesses(const IrInstruction *instruction,
                                      IrAccessVisitor visitor, void *user_data);

/**
 * @brief Destruirá un modelo completo o parcial siguiendo su propiedad.
 * @param program Propiedad a consumir; NULL será válido.
 * @note Deberá liberar listas de argumentos activas según opcode, instrucciones,
 * terminadores, bloques, temporales, firmas, nombres y arreglos del programa.
 * No liberará declaration, owner_scope ni symbols_source, que son préstamos.
 */
void ir_program_free(IrProgram *program);
