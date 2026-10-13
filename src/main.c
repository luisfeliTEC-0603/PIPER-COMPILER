#include "ast.h"
#include "semantic.h"
#include "ast_to_json.h"
#include "parser.h"
#include "symbol_table.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h> /* Necesario para el procesamiento de banderas */

/* Funciones generadas por Flex para configurar y liberar el scanner. */
void yyrestart(FILE *input_file);
int yylex_destroy(void);
void lexer_set_trace(int enabled);

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
    fprintf(output, "  -l, --lexical    Muestra los lexemas del análisis léxico\n");
    fprintf(output, "  -s, --syntactic  Muestra el resultado sintáctico\n");
    fprintf(output, "  -m, --symbols    Muestra la tabla de símbolos y memoria\n");
    fprintf(output, "  -e, --semantic   Muestra el recorrido semántico\n");
    fprintf(output, "  -a, --all        Muestra las cuatro fases anteriores\n");
    fprintf(output, "  -t, --ast-json   Exporta el AST a 'ast_output.json'\n");
    fprintf(output, "  -o, --output <archivo>  Ruta del JSON exportado con -t\n");
    fprintf(output, "  -h, --help       Muestra esta ayuda\n");
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
    int trace_lexical = 0;
    int trace_syntactic = 0;
    int trace_semantic = 0;
    int opt;
    static const struct option long_options[] = {
        {"lexical", no_argument, NULL, 'l'},
        {"syntactic", no_argument, NULL, 's'},
        {"symbols", no_argument, NULL, 'm'},
        {"semantic", no_argument, NULL, 'e'},
        {"all", no_argument, NULL, 'a'},
        {"ast-json", no_argument, NULL, 't'},
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    /* 1. Procesar opciones mediante getopt */
    while ((opt = getopt_long(argc, argv, "lsmeato:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'l':
            trace_lexical = 1;
            break;
        case 's':
            trace_syntactic = 1;
            break;
        case 'e':
            trace_semantic = 1;
            break;
        case 'a':
            trace_lexical = trace_syntactic = print_symbols_flag = trace_semantic = 1;
            break;
        case 't':
            export_json_flag = 1;
            break;
        case 'm':
            print_symbols_flag = 1;
            break;
        case 'o':
            output_path = optarg;
            break;
        case 'h':
            print_usage(stdout, argv[0]);
            return PIPOC_EXIT_SUCCESS;
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
    lexer_set_trace(trace_lexical);
    symbols = symbol_table_create();
    if (symbols == NULL) {
        fprintf(stderr, "error interno: no se pudo crear la tabla de símbolos\n");
        fclose(input);
        return PIPOC_EXIT_FRONTEND_ERROR;
    }

    parse_status = yyparse(&program, symbols);
    (void)yylex_destroy();

    if (parse_status == 0 && program != NULL) {
        if (trace_syntactic) {
            printf("[Sintáctico] Análisis completado correctamente.\n");
        }
        if (!analyze_semantics_with_trace(program, symbols, trace_semantic)) {
            fprintf(stderr, "Error: La verificación semántica ha fallado. Abortando compilación.\n");
            parse_status = 1;
        }
    }

    /* 2. Ejecutar acciones condicionales según las banderas activadas */

    /* Si se activó la bandera -m, se imprime la Tabla de Símbolos */
    if (print_symbols_flag) {
        symbol_table_print(stdout, symbols);
    }

    if (parse_status == 0 && program != NULL) {
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
