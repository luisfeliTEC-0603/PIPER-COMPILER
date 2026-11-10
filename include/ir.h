#ifndef PIPER_IR_H
#define PIPER_IR_H

/**
 * @file ir.h
 * @brief Modelo y contratos públicos de la representación intermedia de Piper.
 *
 * ESTRUCTURA ÚNICAMENTE: las funciones están declaradas, sin implementación.
 * Fuente del contrato: docs/IR/contrato-IR.md y docs/IR/todoIR.md.
 *
 * Flujo previsto:
 *   AST validado + tabla semántica final
 *       -> ir_generate -> IrProgram con rutinas y bloques
 *       -> ir_validate -> ir_write / consumidores posteriores
 *       -> ir_program_free.
 *
 * main conserva la responsabilidad de ejecutar y comprobar los análisis.
 * Las estructuras residen en el proceso compilador; no describen registros,
 * posiciones de spill, bundles ni memoria reservada para temporales en PORYGON.
 */
#include "ast.h"
#include "symbol_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Los IDs sobreviven a realloc. Su espacio se indica en cada campo:
 * símbolos/rutinas pertenecen al programa; bloques/temporales/instrucciones,
 * a una rutina. Cero es válido: usar IR_NO_ID para indicar ausencia. */
typedef size_t IrSymbolId;
typedef size_t IrRoutineId;
typedef size_t IrBlockId;
typedef size_t IrTempId;
typedef size_t IrInstructionId;
#define IR_NO_ID SIZE_MAX

/** Un fallo se expresa mediante estado, nunca mediante un valor `none`. */
typedef enum {
    IR_OK = 0,
    IR_INVALID_INPUT,
    IR_UNSUPPORTED,
    IR_OUT_OF_MEMORY,
    IR_INVALID_PROGRAM,
    IR_IO_ERROR
} IrStatus;

/**
 * Información de error por valor: no presta cadenas del contexto generador.
 * Los IDs sin ubicación asociada valen IR_NO_ID; location conserva el origen
 * Piper cuando existe. El llamador puede omitir el diagnóstico pasando NULL.
 */
typedef struct {
    IrStatus status;
    SourceLocation location;
    IrRoutineId routine;
    IrBlockId block;
    IrInstructionId instruction;
    char message[256];
} IrDiagnostic;

typedef enum {
    IR_TYPE_UNSPECIFIED = 0, /**< Solo solicitudes internas de inferencia. */
    IR_TYPE_PIPER,           /**< piper_type es válido, incluido NONE. */
    IR_TYPE_ADDRESS          /**< Dirección de 32 bits; element_type válido. */
} IrTypeKind;

/**
 * Tipo exacto de un valor. Los campos Piper son válidos según kind.
 * En una dirección, rank/rows/cols describen el objeto apuntado cuando aplique;
 * rank=0 permite direcciones a un elemento. No se usa TYPE_INT como tipo exacto.
 */
typedef struct {
    IrTypeKind kind;
    PiperType piper_type;
    PiperType element_type;
    size_t rank;
    size_t rows;
    size_t cols;
} IrType;

typedef enum {
    IR_OPERAND_NONE = 0,
    IR_OPERAND_CONSTANT,
    IR_OPERAND_SYMBOL,
    IR_OPERAND_TEMP
} IrOperandKind;

/**
 * Magnitud y signo separados para no truncar literales unsigned al usar int.
 * El tipo del operando determina rango e interpretación; bool usa 0 o 1.
 * La forma canónica de cero no lleva signo negativo.
 */
typedef struct {
    uint64_t magnitude;
    bool negative;
} IrConstant;

/**
 * Referencia a un valor: la unión se interpreta únicamente mediante kind.
 * Un operando no posee memoria. IR_OPERAND_NONE representa ausencia de valor,
 * por ejemplo en un retorno none; no representa un error de traducción.
 */
typedef struct {
    IrOperandKind kind;
    IrType type;
    union {
        IrConstant constant;
        IrSymbolId symbol;
        IrTempId temp;
    } data;
} IrOperand;

/**
 * Declaración de usuario: identidad propia, distinta de un temporal.
 * declaration y owner_scope son préstamos a la tabla semántica FINAL.
 * name es copia interna. La tabla debe vivir hasta liberar toda la IR.
 * rows/cols/rank describen el almacenamiento o parámetro de arreglo.
 */
typedef struct {
    IrSymbolId id;
    const Symbol *declaration;
    const Scope *owner_scope;
    char name[SYMBOL_NAME_MAX];
    SymbolClass symbol_class;
    IrType type;
    size_t rank;
    size_t rows;
    size_t cols;
} IrSymbol;

/** Resultado virtual de una sola definición; no almacena ubicación física. */
typedef struct {
    IrTempId id;
    IrType type;
    SourceLocation location;
} IrTemporary;

typedef enum {
    IR_OP_INVALID = 0,
    IR_OP_COPY,
    IR_OP_ADD, IR_OP_SUB, IR_OP_MUL, IR_OP_DIV, IR_OP_MOD, IR_OP_POW,
    IR_OP_NEG,
    IR_OP_EQ, IR_OP_NE, IR_OP_LT, IR_OP_LE, IR_OP_GT, IR_OP_GE,
    IR_OP_AND, IR_OP_OR, IR_OP_XOR, IR_OP_NOT, IR_OP_TO_BOOL,
    IR_OP_ADDR, IR_OP_LOAD, IR_OP_STORE, IR_OP_CHECK_BOUNDS,
    IR_OP_CALL, IR_OP_INTRINSIC,
    IR_OP_JUMP, IR_OP_BRANCH, IR_OP_RETURN
} IrOpcode;

typedef enum {
    IR_INTRINSIC_AUTH,
    IR_INTRINSIC_WRITE_KEY,
    IR_INTRINSIC_FEISTEL,
    IR_INTRINSIC_REVOKE
} IrIntrinsic;

/**
 * Efectos observables; los calculará centralmente ir_instruction_effects.
 * may_trap expresa salida de error implícita, sin arista de retorno normal.
 * Una llamada sin análisis interprocedimental requiere efectos conservadores.
 */
typedef struct {
    bool memory_read;
    bool memory_write;
    bool crypto_read;
    bool crypto_write;
    bool may_trap;
} IrEffects;

/** Lista propia de CALL/INTRINSIC: valores ya evaluados, en orden de origen. */
typedef struct {
    IrOperand *items;
    size_t count;
    size_t capacity;
} IrArguments;

/**
 * Operación TAC y sus atributos.
 *
 * Convención de campos simples (los ausentes llevan IR_OPERAND_NONE):
 *   COPY/unaria: arg1 -> result; binaria: arg1 op arg2 -> result.
 *   ADDR: arg1=símbolo -> result=dirección temporal.
 *   LOAD: arg1=dirección -> result; STORE: arg1=dirección, arg2=valor.
 *   CHECK_BOUNDS: arg1=índice, arg2=longitud, sin resultado.
 *   CALL/INTRINSIC: argumentos en extra; result opcional.
 *   BRANCH: arg1=bool; RETURN: arg1=valor opcional; JUMP sin operandos.
 *
 * operation_type conserva la interpretación del cálculo; result.type conserva
 * el tipo de su destino. checked solicita las comprobaciones del backend actual.
 * La unión extra solo se interpreta según op; sus listas son propiedad de la IR.
 */
typedef struct {
    IrInstructionId id;
    IrOpcode op;
    IrOperand arg1;
    IrOperand arg2;
    IrOperand result;
    IrType operation_type;
    bool checked;
    SourceLocation location;
    IrEffects effects;
    union {
        struct { IrRoutineId callee; IrArguments arguments; } call;
        struct { IrIntrinsic kind; IrArguments arguments; } intrinsic;
        struct { IrBlockId target; } jump;
        struct { IrBlockId if_true; IrBlockId if_false; } branch;
    } extra;
} IrInstruction;

/**
 * Bloque en construcción o finalizado. instructions excluye al terminador;
 * has_terminator indica si el campo terminator es válido. Toda IR publicada
 * exige has_terminator=true. No contiene predecesores ni sucesores del CFG.
 */
typedef struct {
    IrBlockId id;
    IrInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    bool has_terminator;
    IrInstruction terminator;
} IrBlock;

/**
 * Rutina con almacenamiento propio para nombre y arreglos.
 * parameters contiene IDs de símbolos en orden de firma; locals, declaraciones
 * escalares o arreglos de todos sus ámbitos. symbol=IR_NO_ID para el arranque.
 * IDs internos se resuelven por identidad, no por suposición de índice físico.
 */
typedef struct {
    IrRoutineId id;
    IrSymbolId symbol;
    bool is_startup;
    char *name;
    IrType return_type;
    IrSymbolId *parameters;
    size_t parameter_count;
    size_t parameter_capacity;
    IrSymbolId *locals;
    size_t local_count;
    size_t local_capacity;
    IrTemporary *temporaries;
    size_t temporary_count;
    size_t temporary_capacity;
    IrBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    IrBlockId entry;
} IrRoutine;

/**
 * Raíz del modelo. Posee todos sus arreglos, no la tabla symbols_source.
 * globals son IDs de símbolos; startup identifica la rutina sintética opcional.
 * El AST se presta solo durante generación; aquí no se conserva ningún nodo.
 */
typedef struct {
    const SymbolTable *symbols_source;
    IrSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    IrSymbolId *globals;
    size_t global_count;
    size_t global_capacity;
    IrRoutine *routines;
    size_t routine_count;
    size_t routine_capacity;
    IrRoutineId startup;
} IrProgram;

typedef enum { IR_ACCESS_READ, IR_ACCESS_WRITE } IrAccessKind;

/**
 * Receptor futuro de lecturas/definiciones explícitas, sin calcular liveness.
 * value es préstamo válido durante la visita; devolver fallo detiene el recorrido.
 * Los efectos indirectos por alias/memoria se consultan por separado en effects.
 */
typedef IrStatus (*IrAccessVisitor)(IrAccessKind access,
                                    const IrOperand *value, void *user_data);

/**
 * @brief Traducirá AST validado a IR tipada, dividida en bloques y validada.
 * @param root AST_PROGRAM aprobado; prestado e inmutable.
 * @param symbols Tabla semántica final; préstamo que sobrevivirá a la IR.
 * @param out Dirección de puntero inicialmente NULL; recibe propiedad en éxito.
 * @param diagnostic Diagnóstico opcional por valor.
 * @return IR_OK solo al publicar un programa completo; error deja *out=NULL.
 * @note Declaración sin cuerpo; no se invoca todavía desde main.
 */
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);

/**
 * @brief Comprobará estructura, tipos, IDs, firmas y definiciones de temporales.
 * @param program Programa prestado que no se modifica.
 * @param diagnostic Error opcional con rutina/bloque/instrucción.
 * @return IR_OK si cumple el contrato; no requiere CFG ni análisis de liveness.
 */
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic);

/**
 * @brief Imprimirá TAC legible, estable y con tipos/efectos relevantes.
 * @param output Stream prestado, abierto por el llamador.
 * @param program IR previamente validada, prestada e inmutable.
 * @return IR_OK o error de entrada/E/S; no abre, cierra ni renombra archivos.
 * @note Publicación atómica y comprobación de fclose pertenecen al orquestador.
 */
IrStatus ir_write(FILE *output, const IrProgram *program);

/**
 * @brief Liberará el modelo y todas sus listas propias; aceptará NULL.
 * @param program Programa cuya propiedad termina aquí; no libera AST ni tabla.
 * @note Pendiente: no existe un cuerpo vacío que aparente liberar memoria.
 */
void ir_program_free(IrProgram *program);

/**
 * @brief Calculará efectos desde operación, atributos y símbolos del programa.
 * @param program Programa prestado para reconocer globales y firmas.
 * @param instruction Operación a describir, sin modificarla.
 * @param out Efectos definidos solamente cuando el retorno sea IR_OK.
 */
IrStatus ir_instruction_effects(const IrProgram *program,
                               const IrInstruction *instruction, IrEffects *out);

/**
 * @brief Enumerará usos/definiciones explícitos de una instrucción, sin sets vivos.
 * @param instruction Operación prestada; incluye argumentos de llamadas.
 * @param visitor Receptor de símbolos/temporales, no constantes ni ausentes.
 * @param user_data Contexto prestado del receptor; puede ser NULL.
 * @return IR_OK o primer error; los efectos indirectos no se inventan como IDs.
 */
IrStatus ir_instruction_visit_accesses(const IrInstruction *instruction,
                                      IrAccessVisitor visitor, void *user_data);

#endif
