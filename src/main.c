#include "ast.h"
#include "semantic.h"
#include "ast_to_json.h"
#include "parser.h"
#include "symbol_table.h"
#include "codegen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h> /* Necesario para el procesamiento de banderas */

/* Funciones generadas por Flex para configurar y liberar el scanner. */
void yyrestart(FILE *input_file);
int yylex_destroy(void);

enum {
    PIPOC_EXIT_SUCCESS = 0,
    PIPOC_EXIT_FRONTEND_ERROR = 1,
    PIPOC_EXIT_USAGE = 2,
    PIPOC_EXIT_IO_ERROR = 3
};

static void print_usage(FILE *output, const char *program_name)
{
    fprintf(output, "Uso: %s [opciones] <archivo.rwt>\n", program_name);
    fprintf(output, "Opciones:\n");
    fprintf(output, "  -t           Exporta el Árbol Sintáctico Abstracto (AST) a 'ast_output.json'\n");
    fprintf(output, "  -m           Imprime la tabla de símbolos y asignaciones de memoria\n");
    fprintf(output, "  -s           Genera código ASM PORYGON\n");
    fprintf(output, "  -o <archivo> Especifica el nombre del archivo de salida\n");
}

static int has_rwt_extension(const char *path)
{
    static const char extension[] = ".rwt";
    size_t path_length;
    size_t extension_length = sizeof(extension) - 1;

    if (path == NULL) {
        return 0;
    }

    path_length = strlen(path);
    return path_length > extension_length
        && strcmp(path + path_length - extension_length, extension) == 0;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    FILE *input = NULL;
    AstNode *program = NULL;
    SymbolTable *symbols = NULL;
    int parse_status;
    int exit_status = PIPOC_EXIT_FRONTEND_ERROR;

    /* Banderas de opciones */
    int export_json_flag = 0;
    int print_symbols_flag = 0;
    int generate_asm_flag = 0;
    int opt;

    /* 1. Procesar opciones mediante getopt */
    while ((opt = getopt(argc, argv, "tmso:")) != -1) {
        switch (opt) {
        case 't':
            export_json_flag = 1;
            break;
        case 'm':
            print_symbols_flag = 1;
            break;
        case 's':
            generate_asm_flag = 1;
            break;
        case 'o':
            output_path = optarg;
            break;
        default:
            print_usage(stderr, argv[0]);
            return PIPOC_EXIT_USAGE;
        }
    }

    /* Verificar que se haya proporcionado el archivo de entrada */
    if (optind >= argc) {
        fprintf(stderr, "error: falta el archivo de entrada .rwt\n");
        print_usage(stderr, argv[0]);
        return PIPOC_EXIT_USAGE;
    }

    input_path = argv[optind];
    if (!has_rwt_extension(input_path)) {
        fprintf(stderr, "error: el archivo de entrada debe tener extensión .rwt: %s\n",
                input_path);
        return PIPOC_EXIT_USAGE;
    }

    input = fopen(input_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s': %s\n",
                input_path, strerror(errno));
        return PIPOC_EXIT_IO_ERROR;
    }

    yyrestart(input);
    symbols = symbol_table_create();
    if (symbols == NULL) {
        fprintf(stderr, "error interno: no se pudo crear la tabla de símbolos\n");
        fclose(input);
        return PIPOC_EXIT_FRONTEND_ERROR;
    }

    parse_status = yyparse(&program, symbols);
    (void)yylex_destroy();

    if (parse_status == 0 && program != NULL) {
        if (!analyze_semantics(program, symbols)) {
            fprintf(stderr, "Error: La verificación semántica ha fallado. Abortando compilación.\n");
            parse_status = 1;
        } else {
            printf("Análisis semántico completado exitosamente.\n");
        }
    }

    /* 2. Ejecutar acciones condicionales según las banderas activadas */

    /* Si se activó la bandera -m, se imprime la Tabla de Símbolos */
    if (print_symbols_flag) {
        symbol_table_print(stdout, symbols);
    }

    if (parse_status == 0 && program != NULL) {
        ast_print(stdout, program);

        /* Si se activó la bandera -t, exportamos el AST a JSON antes de liberarlo */
        if (export_json_flag) {
            const char *json_path = output_path != NULL
                ? output_path : "ast_output.json";
            if (ast_export_json(program, json_path)) {
                printf("[AST] Exportado exitosamente a '%s'\n", json_path);
            } else {
                fprintf(stderr, "error: no se pudo exportar el AST a JSON\n");
            }
        }

        /* ★ Generación de ASM */
        if (generate_asm_flag) {
            const char *asm_path = output_path != NULL
                ? output_path : "programa.asm";
            FILE *asm_file = fopen(asm_path, "w");
            if (asm_file == NULL) {
                fprintf(stderr, "error: no se pudo crear '%s': %s\n",
                        asm_path, strerror(errno));
                parse_status = 1;
            } else {
                CodegenOptions opts = { .emitir_comentarios = 1,
                                        .optimizar_temporales = 0 };
                CodegenResult r = codegen_generate(program, symbols,
                                                   asm_file, &opts);
                fclose(asm_file);
                if (r != CODEGEN_OK) {
                    fprintf(stderr,
                            "error: generación de código falló (código %d)\n", r);
                    parse_status = 1;
                } else {
                    printf("[ASM] Generado exitosamente en '%s'\n", asm_path);
                }
            }
        }

        exit_status = PIPOC_EXIT_SUCCESS;
    } else if (parse_status == 0) {
        fprintf(stderr, "error interno: el parser no produjo un AST\n");
    }

    /* 3. Liberar recursos en orden inverso */
    ast_free(program);
    program = NULL;
    symbol_table_destroy(symbols);

    if (fclose(input) != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s': %s\n",
                input_path, strerror(errno));
        if (exit_status == PIPOC_EXIT_SUCCESS) {
            exit_status = PIPOC_EXIT_IO_ERROR;
        }
    }

    return exit_status;
}
