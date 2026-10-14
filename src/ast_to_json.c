#include "ast_to_json.h"
#include <stdio.h>

int ast_export_json(const AstNode *root, const char *filename) {
    int failed = 0;
    if (!root || !filename) return 0;

    FILE *out = fopen(filename, "w");
    if (!out) {
        perror("Error al abrir archivo para exportar JSON");
        return 0;
    }

    ast_print_json(out, root);
    if (fputc('\n', out) == EOF || ferror(out)) {
        failed = 1;
    }

    if (fclose(out) != 0) {
        failed = 1;
    }
    return !failed;
}
