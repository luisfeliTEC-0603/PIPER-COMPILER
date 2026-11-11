#ifndef PIPER_IR_H
#define PIPER_IR_H

/**
 * @file ir.h
 * @brief Modelo mínimo de TAC tipado y bloques básicos de Piper.
 *
 * I1 implementa almacenamiento/liberación; I3 traduce expresiones y asignaciones
 * escalares. Orquestación, validación completa e impresión siguen pendientes.
 * Flujo: ir_generate(AST, símbolos) -> IrProgram -> ir_write/consumidores
 * -> ir_program_free. ir_generate debe validar antes de publicar su resultado.
 * La tabla semántica final se presta y debe sobrevivir a todos los consumidores.
 * Diccionario completo: docs/IR/glosario-IR.md. Los IR_OP_* son nombres internos;
 * la impresión usa =, +, -, *, comparaciones, jump, if y return.
 */
#include "ast.h"
#include "symbol_table.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Identidad numérica, no dirección de una variable de Piper.
 * Los IDs de bloque/temporal pertenecen a una rutina y no cambian al mover un
 * arreglo C. Los índices de rutina/instrucción solo localizan datos actuales.
 * Cero es válido; IR_NO_ID expresa ausencia, incluido un camino terminado. */
typedef size_t IrId;
#define IR_NO_ID SIZE_MAX

typedef enum {
    IR_OK = 0, IR_INVALID_INPUT, IR_UNSUPPORTED,
    IR_OUT_OF_MEMORY, IR_INVALID_PROGRAM, IR_IO_ERROR
} IrStatus;

/** Diagnóstico por valor; instruction es la posición actual dentro del bloque,
 * no un ID de resultado. Los campos no aplicables valen IR_NO_ID. */
typedef struct {
    IrStatus status;
    SourceLocation location;
    IrId routine, block, instruction;
    char message[256];
} IrDiagnostic;

/** Tipo de un valor, no regla de promoción automática al operando más grande.
 * piper conserva el tipo resuelto por semántica; address=true lo interpreta como
 * tipo del elemento apuntado por una dirección de 32 bits. Dimensiones en Symbol.
 * En un operando ausente, este campo no se interpreta. */
typedef struct {
    PiperType piper;
    bool address;
} IrType;

typedef enum {
    IR_OPERAND_NONE = 0, IR_OPERAND_CONSTANT, IR_OPERAND_SYMBOL, IR_OPERAND_TEMP
} IrOperandKind;

/** Valor sin memoria propia. symbol identifica directamente la declaración
 * final, incluso con nombres repetidos. int64_t conserva todos los enteros
 * válidos de Piper, incluidos uB32 y los negativos, sin una estructura de signo.
 * El tipo del operando limita el valor: no se amplía Piper a enteros de 64 bits.
 * NONE es ausencia válida de resultado; no informa si la traducción falló. */
typedef struct {
    IrOperandKind kind;
    IrType type;
    union {
        int64_t constant;
        const Symbol *symbol;
        IrId temp;
    } value;
} IrOperand;

/* Cada opcode distingue una operación. No hacen falta estructuras distintas
 * para suma, copia, comparación o acceso a memoria. */
typedef enum {
    IR_OP_INVALID = 0, IR_OP_COPY,
    IR_OP_ADD, IR_OP_SUB, IR_OP_MUL, IR_OP_DIV, IR_OP_MOD, IR_OP_POW, IR_OP_NEG,
    IR_OP_EQ, IR_OP_NE, IR_OP_LT, IR_OP_LE, IR_OP_GT, IR_OP_GE,
    IR_OP_AND, IR_OP_OR, IR_OP_XOR, IR_OP_NOT,
    IR_OP_ADDR, IR_OP_LOAD, IR_OP_STORE, IR_OP_CHECK_BOUNDS,
    IR_OP_CALL, IR_OP_INTRINSIC, IR_OP_JUMP, IR_OP_BRANCH, IR_OP_RETURN
} IrOpcode;

typedef enum {
    IR_INTRINSIC_AUTH, IR_INTRINSIC_WRITE_KEY,
    IR_INTRINSIC_FEISTEL, IR_INTRINSIC_REVOKE
} IrIntrinsic;

/* Atributos/efectos combinables. CHECKED exige comprobar que el resultado quepa
 * en result.type; no anuncia un error del compilador ni un fallo esperado del
 * ejemplo. MAY_TRAP conserva la posibilidad de error dinámico. Los otros bits
 * describen lecturas/escrituras observables de memoria o estado criptográfico. */
typedef enum {
    IR_CHECKED      = 1u << 0,
    IR_READ_MEMORY  = 1u << 1,
    IR_WRITE_MEMORY = 1u << 2,
    IR_READ_CRYPTO  = 1u << 3,
    IR_WRITE_CRYPTO = 1u << 4,
    IR_MAY_TRAP     = 1u << 5
} IrFlags;

/**
 * Una instrucción para todas las formas de TAC:
 * - Normal: result = arg1 op arg2; copia/unaria no usa arg2.
 * - Memoria: ADDR(symbol)->result; LOAD(address)->result;
 *   STORE(arg1=address, arg2=value); CHECK_BOUNDS(arg1=index, arg2=length).
 * - Control: JUMP usa targets[0]; BRANCH usa arg1=bool y targets[0/1]
 *   verdadero/falso; RETURN usa arg1=valor opcional.
 * - Llamada: argumentos ya evaluados en extra.call; result opcional.
 *
 * Los tipos de arg1/arg2 conservan signo y ancho; result.type tipa el resultado.
 * Cada operando ausente lleva IR_OPERAND_NONE. La unión extra se interpreta
 * solo por op; únicamente CALL/INTRINSIC poseen una lista dinámica de argumentos.
 */
typedef struct {
    IrOpcode op;
    IrOperand arg1, arg2, result;
    SourceLocation location;
    unsigned flags;
    union {
        IrId targets[2];
        struct {
            const Symbol *callee; /**< CALL: préstamo a la función final. */
            IrIntrinsic intrinsic; /**< INTRINSIC: identificador reservado. */
            IrOperand *arguments;  /**< Lista propia; tipos en cada valor. */
            size_t argument_count; /**< Cantidad exacta, sin otra capacidad. */
        } call;
    } extra;
} IrInstruction;

/** Convención interna: la última instrucción es JUMP, BRANCH o RETURN.
 * Los apuntes también permiten continuación secuencial; este modelo la explicita
 * para mantener destinos independientes del orden físico de los bloques.
 * No hay otro campo para el terminador ni una bandera que duplique su estado.
 * El ID permanece estable aunque una transformación reordene los bloques. */
typedef struct {
    IrId id;
    IrInstruction *instructions;
    size_t instruction_count, instruction_capacity;
} IrBlock;

/** La firma/nombre/tipos/parámetros se consultan en symbol, sin copiarlos.
 * symbol=NULL identifica el arranque sintético (retorno none, sin parámetros).
 * temp_count asigna IDs virtuales; su tipo figura en los operandos, sin otra
 * tabla de temporales. La rutina posee blocks y sus instrucciones; las variables
 * locales y parámetros siguen perteneciendo a la tabla semántica prestada. */
typedef struct {
    const Symbol *symbol;
    IrBlock *blocks;
    size_t block_count, block_capacity;
    size_t temp_count;
    IrId entry;
} IrRoutine;

/** Posee rutinas, bloques, instrucciones y argumentos; no posee symbols.
 * Globales/locales, ámbitos y dimensiones siguen en la tabla semántica final.
 * startup es índice de rutina o IR_NO_ID. El AST no se conserva en la salida. */
typedef struct {
    const SymbolTable *symbols;
    IrRoutine *routines;
    size_t routine_count, routine_capacity;
    IrId startup;
} IrProgram;

/**
 * @brief Construirá y validará todo el programa antes de entregarlo al llamador.
 * @param root AST_PROGRAM aprobado, con referencias semánticas finales por uso.
 * @param symbols Tabla final prestada; debe vivir hasta liberar IR y consumidores.
 * @param out Puntero inicialmente NULL; recibe propiedad solo al retornar IR_OK.
 * @param diagnostic Receptor opcional del estado y primer error localizado.
 * @return Estado explícito. Un fallo libera la construcción parcial y deja
 * *out=NULL; ni AST ni tabla se modifican o liberan. No imprime ni abre archivos.
 * @note Solo declaración. La integración con main espera al módulo completo.
 */
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);

/**
 * @brief Revisará la traducción, sin volver a analizar el lenguaje fuente.
 * @param program IR completa prestada: tipos, firmas, destinos y temporales.
 * @param diagnostic Receptor opcional del primer incumplimiento del contrato.
 * @return IR_OK si es consistente; error de entrada/modelo en otro caso.
 * No modifica datos ni depende de CFG, registros o análisis de variables vivas.
 */
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic);

/**
 * @brief Exportará el modelo con cabeceras tipadas, TAC sencillo y flags visibles.
 * @param output Stream prestado, abierto y escribible; no se cierra aquí.
 * @param program IR validada y tabla final aún viva, ambas consultadas sin cambios.
 * @return Estado de entrada/E/S. El llamador comprueba fclose y publica el archivo.
 * No recorre el AST. Cada IR_CHECKED muestra el tipo exacto de result.type.
 */
IrStatus ir_write(FILE *output, const IrProgram *program);

/**
 * @brief Libera un programa completo o parcialmente construido; acepta NULL.
 * @param program Propiedad a consumir: rutinas, bloques, instrucciones y las
 * listas de argumentos activas según opcode. No libera AST ni símbolos prestados.
 * Los consumidores se liberan primero; el llamador no vuelve a usar este puntero.
 */
void ir_program_free(IrProgram *program);

#endif
