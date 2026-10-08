#include "ast.h"
#include "parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    fprintf(output, "Uso: %s <archivo.rwt>\n", program_name);
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
    const char *input_path;
    FILE *input = NULL;
    AstNode *program = NULL;
    int parse_status;
    int exit_status = PIPOC_EXIT_FRONTEND_ERROR;

    if (argc != 2) {
        print_usage(stderr, argv[0]);
        return PIPOC_EXIT_USAGE;
    }

    input_path = argv[1];
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
    parse_status = yyparse(&program);
    (void)yylex_destroy();

    if (parse_status == 0 && program != NULL) {
        ast_print(stdout, program);
        exit_status = PIPOC_EXIT_SUCCESS;
    } else if (parse_status == 0) {
        fprintf(stderr, "error interno: el parser no produjo un AST\n");
    }

    /* ast_free(NULL) es válido; este camino cubre éxito y error. */
    ast_free(program);
    program = NULL;

    if (fclose(input) != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s': %s\n",
                input_path, strerror(errno));
        if (exit_status == PIPOC_EXIT_SUCCESS) {
            exit_status = PIPOC_EXIT_IO_ERROR;
        }
    }

    return exit_status;
}
