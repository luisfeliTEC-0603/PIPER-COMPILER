#include <stdio.h>
#include <stdlib.h>

/* Generada por Bison. Solicita tokens a yylex() hasta aceptar o fallar. */
int yyparse(void);

/*
 * Entrada mínima del skeleton. Por ahora analiza stdin; el issue de CLI debe
 * abrir un .rwt, asignarlo a yyin, cerrar el archivo y añadir mensajes de uso.
 */
int main(void)
{
    int parse_status;

    /* TODO(cli): aceptar el nombre del archivo y configurar yyin. */
    parse_status = yyparse();

    /* TODO(integration): imprimir y liberar la raíz del AST si hubo éxito. */
    return parse_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
