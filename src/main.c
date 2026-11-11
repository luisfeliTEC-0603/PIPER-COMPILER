#define _XOPEN_SOURCE 700
/**
 * @file main.c
 * @brief CLI oficial del compilador y ensamblador Piper/PORYGON.
 * @details Ejecuta frontend, codegen, calendarización y emisión binaria una
 * sola vez, con artefactos auxiliares independientes y conversión .bin a .mem.
 */
#include "ast.h"
#include "ir.h"
#include "cfg.h"
#include "codegen.h"
#include "parser.h"
#include "assembler/asm_program_parser.h"   // antes "porygon_assembler/..."
#include "scheduler/scheduler.h"            // antes "scheduler/..." (sin prefijo)
#include "emit/binary_format.h"             // antes "binary_format.h" desde include/
#include "semantic.h"
#include "clone_detector.h"
#include "diagnostic.h"
#include "symbol_table.h"
#include "analysis/ll1/ll1.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void yyrestart(FILE *input_file);
int yylex_destroy(void);
void lexer_set_trace(int enabled);

enum {
    PIPOC_EXIT_SUCCESS = 0,
    PIPOC_EXIT_COMPILE_ERROR = 1,
    PIPOC_EXIT_USAGE = 2,
    PIPOC_EXIT_IO_ERROR = 3,
    OPT_AST_JSON = 1000,
    OPT_EMIT_MEM,
    OPT_EMIT_BUNDLES,
    OPT_EMIT_ASM,
    OPT_BIN_TO_MEM,
    OPT_DISASM_BIN,
    OPT_EMIT_SYMBOLS,
    OPT_EMIT_IR,
    OPT_EMIT_CFG,
    OPT_LL1,
    OPT_LL1_EXPECTED,
    OPT_CHECK,
    OPT_DIAGNOSTICS_JSON,
    OPT_CLONES,
    OPT_CLONES_EXACT,
    OPT_CLONES_RENAMED,
};

typedef enum {
    PIPOC_COMMAND_DEFAULT,
    PIPOC_COMMAND_ASM,
    PIPOC_COMMAND_SCHEDULE,
    PIPOC_COMMAND_MEM,
    PIPOC_COMMAND_INSPECT,
    PIPOC_COMMAND_ALL,
    PIPOC_COMMAND_IR
} pipoc_command_t;

typedef struct {
    pipoc_command_t command;
    const char *source_path;
    const char *asm_input_path;
    const char *binary_input_path;
    const char *binary_output_path;
    const char *asm_output_path;
    const char *mem_output_path;
    const char *bundles_output_path;
    const char *json_output_path;
    const char *symbols_output_path;
    const char *ir_output_path;
    const char *cfg_output_path;
    const char *requested_output_path;
    int keep_asm;
    int verbose;
    int print_ast;
    int print_symbols;
    int print_packages;
    int ll1;
    int ll1_expected;
    int check_only;
    int diagnostics_json;
    int clones_min_nodes;      /* 0 = desactivado */
    int clones_mode;           /* 0 = exact, 1 = renamed */
} cli_options_t;

typedef porygon_schedule_result_t (*schedule_writer_t)(
    FILE *, const porygon_machine_program_t *, const porygon_schedule_t *);

static void print_usage(FILE *output, const char *program)
{
    fprintf(output,
            "Uso:\n"
            "  %s <fuente.rwt> [-o salida.bin] [opciones]\n"
            "  %s asm <fuente.rwt> [-o salida.asm]\n"
            "  %s sch <fuente.rwt> [-o salida.bundles]\n"
            "  %s mem <fuente.rwt> [-o salida.mem]\n"
            "  %s ins <fuente.rwt> [opciones]\n"
            "  %s ir <fuente.rwt> [-o salida.ir]\n"
            "  %s all <fuente.rwt> [-o salida.bin]\n"
            "  %s -x <entrada.asm> [-o salida.bin] [opciones]\n"
            "  %s --bin-to-mem <archivo.bin> [-o salida.mem]\n"
            "  %s --ll1\n"
            "  %s --ll1-expected <prefijo.rwt>\n"
            "Comandos simples:\n"
            "  asm                     Genera el ASM lineal sin calendarizar\n"
            "  sch                     Genera los bundles calendarizados legibles\n"
            "  mem                     Genera la imagen para $readmemh\n"
            "  ins                     Genera <fuente>.ast.json y <fuente>.symbols.txt\n"
            "  ir                      Genera únicamente IR tipada con bloques básicos\n"
            "  all                     Genera .bin, .asm, .bundles, .mem, AST y símbolos\n"
            "Opciones:\n"
            "  -o <ruta>              Salida principal (.bin/.asm/.bundles/.mem/.ir)\n"
            "  -s                      Conserva el ASM intermedio\n"
            "  -v                      Muestra las fases de compilación\n"
            "  -t                      Imprime el AST legible\n"
            "  -m                      Imprime símbolos y distribución de memoria\n"
            "  -x <entrada.asm>        Ensambla directamente, sin frontend Piper\n"
            "  -p                      Imprime paquetes numéricos y mnemónicos\n"
            "  --ast-json <ruta>       Exporta el AST como JSON\n"
            "  --emit-asm <ruta>       Conserva el ASM en la ruta indicada\n"
            "  --emit-mem <ruta>       Emite texto hexadecimal para $readmemh\n"
            "  --emit-bundles <ruta>   Emite los bundles en formato legible\n"
            "  --emit-cfg <ruta>       Exporta el grafo de bloques como DOT\n"
            "  --emit-ir <ruta>        Exporta IR además de la salida principal\n"
            "  --emit-symbols <ruta>   Exporta la tabla de símbolos legible\n"
            "  --bin-to-mem <bin>      Convierte un ejecutable validado a .mem\n"
            "  --disasm-bin <bin>      Alias de --bin-to-mem\n"
            "  --ll1                   Muestra solamente la tabla predictiva\n"
            "  --ll1-expected          Lista terminales válidos al final del archivo\n"
            "  --check                 Revisa léxico, sintaxis y semántica sin generar archivos\n"
            "  --diagnostics-json      Emite JSON por stdout; requiere --check\n"
            "  --clones <N>            Detecta clones Tipo I/II con N nodos mínimos (defecto 8)\n"
            "  --clones-exact          Fuerza detección de clones Tipo I (exactos)\n"
            "  --clones-renamed        Fuerza detección de clones Tipo II (renombrados)\n"
            "  -h, --help              Muestra esta ayuda\n",
            program, program, program, program, program, program, program, program,
            program, program, program);
}

static int parse_command(const char *argument, pipoc_command_t *command)
{
    if (strcmp(argument, "asm") == 0) *command = PIPOC_COMMAND_ASM;
    else if (strcmp(argument, "sch") == 0) *command = PIPOC_COMMAND_SCHEDULE;
    else if (strcmp(argument, "mem") == 0) *command = PIPOC_COMMAND_MEM;
    else if (strcmp(argument, "ins") == 0) *command = PIPOC_COMMAND_INSPECT;
    else if (strcmp(argument, "ir") == 0) *command = PIPOC_COMMAND_IR;
    else if (strcmp(argument, "all") == 0) *command = PIPOC_COMMAND_ALL;
    else return 0;
    return 1;
}

static int ends_with(const char *path, const char *extension)
{
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);
    return path_length > extension_length &&
           strcmp(path + path_length - extension_length, extension) == 0;
}

static char *replace_extension(const char *path, const char *extension)
{
    const char *base = strrchr(path, '/');
    const char *dot;
    size_t prefix, extension_length = strlen(extension);
    char *result;
    base = base == NULL ? path : base + 1;
    dot = strrchr(base, '.');
    prefix = dot == NULL ? strlen(path) : (size_t)(dot - path);
    if (prefix > SIZE_MAX - extension_length - 1) return NULL;
    result = malloc(prefix + extension_length + 1);
    if (result == NULL) return NULL;
    memcpy(result, path, prefix);
    memcpy(result + prefix, extension, extension_length + 1);
    return result;
}

static int default_output_path(const char *input, const char *extension,
                               const char **output, char **owned)
{
    if (*output != NULL) return 1;
    *owned = replace_extension(input, extension);
    if (*owned == NULL) {
        fputs("error: memoria insuficiente para construir una ruta de salida\n", stderr);
        return 0;
    }
    *output = *owned;
    return 1;
}

static int assign_once(const char **destination, const char *value,
                       const char *option)
{
    if (*destination != NULL) {
        fprintf(stderr, "error: la opción %s se especificó más de una vez\n", option);
        return 0;
    }
    *destination = value;
    return 1;
}

static int parse_cli(int argc, char **argv, cli_options_t *options)
{
    int opt;
    int has_command;
    static const struct option long_options[] = {
        {"output", required_argument, NULL, 'o'},
        {"save-asm", no_argument, NULL, 's'},
        {"verbose", no_argument, NULL, 'v'},
        {"ast", no_argument, NULL, 't'},
        {"symbols", no_argument, NULL, 'm'},
        {"assemble", required_argument, NULL, 'x'},
        {"packages", no_argument, NULL, 'p'},
        {"ast-json", required_argument, NULL, OPT_AST_JSON},
        {"emit-mem", required_argument, NULL, OPT_EMIT_MEM},
        {"emit-bundles", required_argument, NULL, OPT_EMIT_BUNDLES},
        {"emit-asm", required_argument, NULL, OPT_EMIT_ASM},
        {"bin-to-mem", required_argument, NULL, OPT_BIN_TO_MEM},
        {"disasm-bin", required_argument, NULL, OPT_DISASM_BIN},
        {"emit-cfg", required_argument, NULL, OPT_EMIT_CFG},
        {"emit-ir", required_argument, NULL, OPT_EMIT_IR},
        {"emit-symbols", required_argument, NULL, OPT_EMIT_SYMBOLS},
        {"ll1", no_argument, NULL, OPT_LL1},
        {"ll1-expected", no_argument, NULL, OPT_LL1_EXPECTED},
        {"check", no_argument, NULL, OPT_CHECK},
        {"diagnostics-json", no_argument, NULL, OPT_DIAGNOSTICS_JSON},
        {"help", no_argument, NULL, 'h'},
        {"clones", required_argument, NULL, OPT_CLONES},
        {"clones-exact", no_argument, NULL, OPT_CLONES_EXACT},
        {"clones-renamed", no_argument, NULL, OPT_CLONES_RENAMED},
        {NULL, 0, NULL, 0}
    };
    memset(options, 0, sizeof(*options));
    has_command = argc > 1 && parse_command(argv[1], &options->command);
    opterr = 0;
    optind = has_command ? 2 : 1;
    while ((opt = getopt_long(argc, argv, "o:svtmx:ph", long_options, NULL)) != -1) {
        switch (opt) {
        case 'o':
            if (!assign_once(&options->binary_output_path, optarg, "-o")) return -1;
            break;
        case 's': options->keep_asm = 1; break;
        case 'v': options->verbose = 1; break;
        case 't': options->print_ast = 1; break;
        case 'm': options->print_symbols = 1; break;
        case 'p': options->print_packages = 1; break;
        case 'x':
            if (!assign_once(&options->asm_input_path, optarg, "-x")) return -1;
            break;
        case OPT_AST_JSON:
            if (!assign_once(&options->json_output_path, optarg, "--ast-json")) return -1;
            break;
        case OPT_EMIT_MEM:
            if (!assign_once(&options->mem_output_path, optarg, "--emit-mem")) return -1;
            break;
        case OPT_EMIT_BUNDLES:
            if (!assign_once(&options->bundles_output_path, optarg, "--emit-bundles")) return -1;
            break;
        case OPT_EMIT_ASM:
            if (!assign_once(&options->asm_output_path, optarg, "--emit-asm")) return -1;
            break;
        case OPT_EMIT_CFG:
            if (!assign_once(&options->cfg_output_path, optarg, "--emit-cfg")) return -1;
            break;
        case OPT_EMIT_IR:
            if (!assign_once(&options->ir_output_path, optarg, "--emit-ir")) return -1;
            break;
        case OPT_EMIT_SYMBOLS:
            if (!assign_once(&options->symbols_output_path, optarg,
                             "--emit-symbols")) return -1;
            break;
        case OPT_LL1:
            options->ll1 = 1;
            break;
        case OPT_LL1_EXPECTED:
            options->ll1_expected = 1;
            break;
        case OPT_CHECK:
            options->check_only = 1;
            break;
        case OPT_DIAGNOSTICS_JSON:
            options->diagnostics_json = 1;
            break;
        case OPT_BIN_TO_MEM:
        case OPT_DISASM_BIN:
            if (!assign_once(&options->binary_input_path, optarg,
                             opt == OPT_BIN_TO_MEM ? "--bin-to-mem" : "--disasm-bin")) return -1;
            break;
        case OPT_CLONES:
            options->clones_min_nodes = atoi(optarg);
            break;
        case OPT_CLONES_EXACT:
            options->clones_mode = 0;
            break;
        case OPT_CLONES_RENAMED:
            options->clones_mode = 1;
            break;
        case 'h':
            print_usage(stdout, argv[0]);
            return 0;
        default:
            fprintf(stderr, "error: opción inválida o incompleta\n");
            return -1;
        }
    }
    options->requested_output_path = options->binary_output_path;
    options->binary_output_path = NULL;
    if ((options->ir_output_path || options->cfg_output_path) && (options->check_only || options->clones_min_nodes > 0 ||
        options->asm_input_path || options->binary_input_path || options->ll1 || options->ll1_expected)) {
        fputs("error: --emit-ir/--emit-cfg requieren compilación Piper, sin --check/clones/LL(1)/-x/conversión\n", stderr);
        return -1;
    }
    if (options->command == PIPOC_COMMAND_IR && (options->keep_asm || options->print_packages ||
        options->asm_output_path || options->mem_output_path || options->bundles_output_path ||
        options->json_output_path || options->symbols_output_path)) {
        fputs("error: ir solo exporta IR y CFG opcional; use --emit-ir con otro modo para otras salidas\n", stderr);
        return -1;
    }
    /* La revisión es un modo de frontend aislado: no acepta salidas ni modos
     * de ensamblado/LL(1). Esto también garantiza que stdout sea JSON puro. */
    if (options->diagnostics_json && !options->check_only) {
        fputs("error: --diagnostics-json requiere --check\n", stderr);
        return -1;
    }
    if (options->clones_min_nodes < 0) {
        fputs("error: --clones requiere un número entero no negativo\n", stderr);
        return -1;
    }
    if (options->clones_min_nodes > 0 &&
        (has_command || options->asm_input_path != NULL ||
         options->binary_input_path != NULL || options->ll1 ||
         options->ll1_expected || options->check_only ||
         options->requested_output_path != NULL ||
         options->json_output_path != NULL ||
         options->symbols_output_path != NULL ||
         options->asm_output_path != NULL ||
         options->mem_output_path != NULL ||
         options->bundles_output_path != NULL)) {
        fputs("error: --clones solo acepta un fuente .rwt y --clones-exact/renamed\n",
              stderr);
        return -1;
    }
    if (options->check_only && (has_command || options->asm_input_path != NULL ||
        options->binary_input_path != NULL || options->ll1 || options->ll1_expected ||
        options->requested_output_path != NULL || options->keep_asm ||
        options->print_ast || options->print_symbols || options->print_packages ||
        options->json_output_path != NULL || options->symbols_output_path != NULL ||
        options->asm_output_path != NULL || options->mem_output_path != NULL ||
        options->bundles_output_path != NULL)) {
        fputs("error: --check solo acepta un fuente .rwt y --diagnostics-json opcional\n", stderr);
        return -1;
    }
    if (has_command &&
        (options->asm_input_path != NULL || options->binary_input_path != NULL)) {
        fputs("error: los comandos asm, sch, mem, ins, ir y all requieren un fuente .rwt\n",
              stderr);
        return -1;
    }
    if (options->ll1) {
        if (options->ll1_expected || has_command || optind != argc ||
            options->asm_input_path != NULL || options->binary_input_path != NULL ||
            options->requested_output_path != NULL || options->keep_asm ||
            options->verbose || options->print_ast || options->print_symbols ||
            options->print_packages || options->json_output_path != NULL ||
            options->asm_output_path != NULL || options->mem_output_path != NULL ||
            options->bundles_output_path != NULL || options->symbols_output_path != NULL) {
            fputs("error: --ll1 no acepta archivos ni otras opciones\n", stderr);
            return -1;
        }
        return 1;
    }
    if (options->binary_input_path != NULL) {
        if (options->asm_input_path != NULL || optind != argc || options->keep_asm ||
            options->print_ast || options->print_symbols || options->print_packages ||
            options->json_output_path != NULL || options->asm_output_path != NULL ||
            options->mem_output_path != NULL || options->bundles_output_path != NULL ||
            options->symbols_output_path != NULL || options->ll1 ||
            options->ll1_expected) {
            fputs("error: --bin-to-mem no puede combinarse con opciones de compilación\n", stderr);
            return -1;
        }
        return 1;
    }
    if (options->asm_input_path != NULL) {
        if (optind != argc || options->keep_asm || options->print_ast ||
            options->print_symbols || options->json_output_path != NULL ||
            options->asm_output_path != NULL || options->symbols_output_path != NULL ||
            options->ll1 || options->ll1_expected) {
            fputs("error: el modo -x no acepta entrada Piper ni opciones de frontend\n", stderr);
            return -1;
        }
        return 1;
    }
    if (optind + 1 != argc) {
        fputs("error: se requiere exactamente un archivo fuente .rwt\n", stderr);
        return -1;
    }
    options->source_path = argv[optind];
    if (!ends_with(options->source_path, ".rwt")) {
        fprintf(stderr, "error: el archivo fuente debe tener extensión .rwt: %s\n",
                options->source_path);
        return -1;
    }
    if (options->ll1_expected &&
        (has_command || options->requested_output_path != NULL ||
         options->keep_asm || options->verbose || options->print_ast ||
         options->print_symbols || options->print_packages ||
         options->json_output_path != NULL || options->asm_output_path != NULL ||
         options->mem_output_path != NULL || options->bundles_output_path != NULL ||
         options->symbols_output_path != NULL)) {
        fputs("error: --ll1-expected solo acepta un archivo .rwt\n",
              stderr);
        return -1;
    }
    return 1;
}

/* Resolver también el directorio de salidas aún inexistentes para detectar
 * ./archivo, rutas con .. y directorios simbólicos que nombran el mismo lugar. */
static char *canonical_target(const char *path)
{
    char *resolved = realpath(path, NULL);
    if (resolved) return resolved;
    const char *slash = strrchr(path, '/');
    char *parent = slash ? strndup(path, slash == path ? 1 : (size_t)(slash - path)) : strdup(".");
    if (parent == NULL) return NULL;
    char *directory = realpath(parent, NULL);
    free(parent);
    if (directory == NULL) return NULL;
    const char *base = slash ? slash + 1 : path;
    size_t size = strlen(directory) + strlen(base) + 2;
    resolved = malloc(size);
    if (resolved) snprintf(resolved, size, "%s/%s", directory, base);
    free(directory);
    return resolved;
}

static int same_path(const char *a, const char *b)
{
    struct stat first, second;
    if (!strcmp(a, b)) return 1;
    if (stat(a, &first) == 0 && stat(b, &second) == 0 &&
        first.st_dev == second.st_dev && first.st_ino == second.st_ino) return 1;
    char *left = canonical_target(a), *right = canonical_target(b);
    int equal = left && right && !strcmp(left, right);
    free(left); free(right);
    return equal;
}

static int validate_paths(const char *input, const char *const *outputs,
                          size_t output_count)
{
    size_t first, second;
    for (first = 0; first < output_count; ++first) {
        if (outputs[first] == NULL) continue;
        if (outputs[first][0] == '\0') {
            fputs("error: una ruta de salida está vacía\n", stderr);
            return 0;
        }
        if (same_path(input, outputs[first])) {
            fprintf(stderr, "error: la salida '%s' coincide con la entrada\n", outputs[first]);
            return 0;
        }
        for (second = first + 1; second < output_count; ++second) {
            if (outputs[second] != NULL && same_path(outputs[first], outputs[second])) {
                fprintf(stderr, "error: dos artefactos usan la misma ruta '%s'\n", outputs[first]);
                return 0;
            }
        }
    }
    return 1;
}

/* Un mismo mecanismo para IR y DOT: escribir/cerrar un temporal y renombrar.
 * cfg=NULL exporta IR; de otro modo exporta su grafo. No se mezclan sus estados
 * de error: se convierten aquí a los códigos de salida de la CLI. */
static int write_analysis(const char *path, const IrProgram *ir, const CfgProgram *cfg)
{
    const char *kind = cfg ? "CFG" : "IR";
    size_t length = strlen(path);
    if (length > SIZE_MAX - 12) return PIPOC_EXIT_COMPILE_ERROR;
    char *temporary = malloc(length + 12);
    if (temporary == NULL) return PIPOC_EXIT_COMPILE_ERROR;
    snprintf(temporary, length + 12, "%s.tmp.XXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd < 0) {
        fprintf(stderr, "error: no se pudo crear temporal %s para '%s': %s\n", kind, path, strerror(errno));
        free(temporary); return PIPOC_EXIT_IO_ERROR;
    }
    FILE *file = fdopen(fd, "w");
    int result = PIPOC_EXIT_SUCCESS;
    if (file == NULL) { close(fd); result = PIPOC_EXIT_IO_ERROR; }
    else {
        if (cfg) {
            CfgStatus status = cfg_write_dot(file, cfg);
            if (status != CFG_OK) result = status == CFG_IO_ERROR ? PIPOC_EXIT_IO_ERROR : PIPOC_EXIT_COMPILE_ERROR;
        } else {
            IrStatus status = ir_write(file, ir);
            if (status != IR_OK) result = status == IR_IO_ERROR ? PIPOC_EXIT_IO_ERROR : PIPOC_EXIT_COMPILE_ERROR;
        }
        if (fclose(file) != 0) result = PIPOC_EXIT_IO_ERROR;
    }
    if (result == PIPOC_EXIT_SUCCESS && rename(temporary, path) != 0) result = PIPOC_EXIT_IO_ERROR;
    if (result != PIPOC_EXIT_SUCCESS) {
        fprintf(stderr, "error: no se pudo publicar %s en '%s'\n", kind, path);
        unlink(temporary);
    }
    free(temporary);
    return result;
}

static int write_bytes(const char *path, const char *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    int failed = 0;
    if (file == NULL) {
        fprintf(stderr, "error: no se pudo crear '%s': %s\n", path, strerror(errno));
        return 0;
    }
    if (length != 0 && fwrite(bytes, 1, length, file) != length) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) fprintf(stderr, "error: no se pudo escribir o cerrar '%s'\n", path);
    return !failed;
}

static int write_ast_json(const char *path, const AstNode *program)
{
    FILE *file = fopen(path, "w");
    int failed = 0;
    if (file == NULL) {
        fprintf(stderr, "error: no se pudo crear '%s': %s\n", path, strerror(errno));
        return 0;
    }
    ast_print_json(file, program);
    if (fputc('\n', file) == EOF || ferror(file)) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) fprintf(stderr, "error: no se pudo escribir o cerrar '%s'\n", path);
    return !failed;
}

static int write_symbols(const char *path, const SymbolTable *symbols)
{
    FILE *file = fopen(path, "w");
    int failed = 0;
    if (file == NULL) {
        fprintf(stderr, "error: no se pudo crear '%s': %s\n", path, strerror(errno));
        return 0;
    }
    symbol_table_print(file, symbols);
    if (ferror(file)) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) fprintf(stderr, "error: no se pudo escribir o cerrar '%s'\n", path);
    return !failed;
}

static int write_schedule(const char *path, schedule_writer_t writer,
                          const porygon_machine_program_t *program,
                          const porygon_schedule_t *schedule)
{
    FILE *file = fopen(path, "w");
    porygon_schedule_result_t result;
    int close_status;
    if (file == NULL) {
        fprintf(stderr, "error: no se pudo crear '%s': %s\n", path, strerror(errno));
        return 0;
    }
    result = writer(file, program, schedule);
    close_status = fclose(file);
    if (result.status != PORYGON_SCHEDULE_OK) {
        fprintf(stderr, "error al escribir '%s': %s\n", path, result.message);
        return 0;
    }
    if (close_status != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s'\n", path);
        return 0;
    }
    return 1;
}

static int write_binary(const char *path,
                        const porygon_machine_program_t *program,
                        const porygon_schedule_t *schedule)
{
    FILE *file = fopen(path, "wb");
    porygon_binary_status_t status;
    int close_status;
    if (file == NULL) {
        fprintf(stderr, "error: no se pudo crear '%s': %s\n", path, strerror(errno));
        return 0;
    }
    status = porygon_binary_write(file, program, schedule);
    close_status = fclose(file);
    if (status != PORYGON_BINARY_OK) {
        fprintf(stderr, "error al escribir '%s': %s\n", path,
                porygon_binary_status_message(status));
        return 0;
    }
    if (close_status != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s'\n", path);
        return 0;
    }
    return 1;
}

static uint64_t encoded_bundle(const porygon_bundle_t *bundle)
{
    uint64_t value = 0;
    size_t slot;
    for (slot = 0; slot < PORYGON_BUNDLE_SLOTS; ++slot) {
        value |= (uint64_t)bundle->slots[slot].word << (16 * slot);
    }
    return value;
}

static int print_packages(const porygon_machine_program_t *program,
                          const porygon_schedule_t *schedule)
{
    porygon_schedule_result_t result;
    size_t bundle;
    puts("[Paquetes] Representación numérica:");
    for (bundle = 0; bundle < schedule->bundle_count; ++bundle) {
        printf("  %04zu: 0x%016" PRIX64 "\n", bundle,
               encoded_bundle(&schedule->bundles[bundle]));
    }
    puts("[Paquetes] Representación mnemónica:");
    result = porygon_write_bundles(stdout, program, schedule);
    if (result.status != PORYGON_SCHEDULE_OK || fflush(stdout) != 0) {
        fprintf(stderr, "error al imprimir paquetes: %s\n", result.message);
        return 0;
    }
    return 1;
}

static int schedule_program(const porygon_machine_program_t *program,
                            porygon_schedule_t *schedule)
{
    porygon_schedule_result_t result = porygon_schedule_program(program, schedule);
    if (result.status != PORYGON_SCHEDULE_OK) {
        fprintf(stderr, "error de calendarización: %s", result.message);
        if (result.bundle != SIZE_MAX) fprintf(stderr, " (bundle %zu)", result.bundle);
        fputc('\n', stderr);
        return 0;
    }
    return 1;
}

static int emit_scheduled_outputs(const cli_options_t *options,
                                  const porygon_machine_program_t *program,
                                  const porygon_schedule_t *schedule)
{
    if (options->binary_output_path != NULL &&
        !write_binary(options->binary_output_path, program, schedule)) return 0;
    if (options->mem_output_path != NULL &&
        !write_schedule(options->mem_output_path, porygon_write_mem, program, schedule)) return 0;
    if (options->bundles_output_path != NULL &&
        !write_schedule(options->bundles_output_path, porygon_write_bundles,
                        program, schedule)) return 0;
    if (options->print_packages && !print_packages(program, schedule)) return 0;
    if (options->verbose) {
        if (options->binary_output_path != NULL)
            fprintf(stderr, "[pipoc] Binario: %s (%zu bundles, %zu bytes de texto)\n",
                    options->binary_output_path, schedule->bundle_count,
                    schedule->bundle_count * 8);
        if (options->mem_output_path != NULL)
            fprintf(stderr, "[pipoc] Memoria de texto: %s\n", options->mem_output_path);
        if (options->bundles_output_path != NULL)
            fprintf(stderr, "[pipoc] Bundles: %s\n", options->bundles_output_path);
    }
    return 1;
}

static int convert_binary(const cli_options_t *options)
{
    porygon_binary_image_t image = {0};
    porygon_binary_status_t status;
    FILE *input = fopen(options->binary_input_path, "rb");
    FILE *output;
    int close_status;
    if (input == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s': %s\n",
                options->binary_input_path, strerror(errno));
        return PIPOC_EXIT_IO_ERROR;
    }
    status = porygon_binary_read(input, &image);
    close_status = fclose(input);
    if (status != PORYGON_BINARY_OK || close_status != 0) {
        fprintf(stderr, "error: '%s': %s\n", options->binary_input_path,
                status != PORYGON_BINARY_OK ? porygon_binary_status_message(status)
                                            : "no se pudo cerrar la entrada");
        porygon_binary_image_free(&image);
        return status == PORYGON_BINARY_IO_ERROR || close_status != 0
            ? PIPOC_EXIT_IO_ERROR : PIPOC_EXIT_COMPILE_ERROR;
    }
    output = fopen(options->binary_output_path, "w");
    if (output == NULL) {
        fprintf(stderr, "error: no se pudo crear '%s': %s\n",
                options->binary_output_path, strerror(errno));
        porygon_binary_image_free(&image);
        return PIPOC_EXIT_IO_ERROR;
    }
    status = porygon_binary_write_mem(output, &image);
    close_status = fclose(output);
    if (status != PORYGON_BINARY_OK || close_status != 0) {
        fprintf(stderr, "error: no se pudo escribir o cerrar '%s'\n",
                options->binary_output_path);
        porygon_binary_image_free(&image);
        return PIPOC_EXIT_IO_ERROR;
    }
    if (options->verbose) {
        fprintf(stderr, "[pipoc] Convertidos %zu bundles de %s a %s\n",
                image.bundle_count, options->binary_input_path,
                options->binary_output_path);
    }
    porygon_binary_image_free(&image);
    return PIPOC_EXIT_SUCCESS;
}

static int compile_assembly(const cli_options_t *options)
{
    porygon_machine_program_t program = {0};
    porygon_schedule_t schedule = {0};
    porygon_program_parse_result_t parsed;
    int exit_status = PIPOC_EXIT_COMPILE_ERROR;
    if (options->verbose) fprintf(stderr, "[pipoc] Leyendo ASM: %s\n", options->asm_input_path);
    parsed = porygon_parse_program_file(options->asm_input_path, &program);
    if (parsed.status != PORYGON_PROGRAM_OK) {
        fprintf(stderr, "%s", options->asm_input_path);
        if (parsed.line != 0) fprintf(stderr, ":%zu:%zu", parsed.line, parsed.column);
        fprintf(stderr, ": error de ASM (programa=%d, instrucción=%d)\n",
                (int)parsed.status, (int)parsed.instruction_status);
        return parsed.status == PORYGON_PROGRAM_IO_ERROR
            ? PIPOC_EXIT_IO_ERROR : PIPOC_EXIT_COMPILE_ERROR;
    }
    if (options->verbose)
        fprintf(stderr, "[pipoc] ASM válido: %zu instrucciones\n", program.instruction_count);
    if (schedule_program(&program, &schedule) &&
        emit_scheduled_outputs(options, &program, &schedule)) {
        exit_status = PIPOC_EXIT_SUCCESS;
    }
    porygon_schedule_free(&schedule);
    porygon_machine_program_free(&program);
    return exit_status;
}

static int compile_piper(const cli_options_t *options)
{
    FILE *input = NULL;
    FILE *asm_stream = NULL;
    AstNode *ast = NULL;
    IrProgram *ir = NULL;
    CfgProgram *cfg = NULL;
    SymbolTable *symbols = NULL;
    char *asm_text = NULL;
    size_t asm_length = 0;
    porygon_machine_program_t program = {0};
    porygon_schedule_t schedule = {0};
    porygon_program_parse_result_t parsed;
    CodegenOptions codegen_options = {0, 0};
    CodegenResult codegen_result;
    int parse_status;
    int exit_status = PIPOC_EXIT_COMPILE_ERROR;

    input = fopen(options->source_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s': %s\n",
                options->source_path, strerror(errno));
        return PIPOC_EXIT_IO_ERROR;
    }
    if (options->verbose) fprintf(stderr, "[pipoc] Fase léxica/sintáctica\n");
    yyrestart(input);
    lexer_set_trace(0);
    symbols = symbol_table_create();
    if (symbols == NULL) {
        fputs("error interno: no se pudo crear la tabla de símbolos\n", stderr);
        if (fclose(input) != 0) {
            fprintf(stderr, "error: no se pudo cerrar '%s'\n", options->source_path);
        }
        return PIPOC_EXIT_COMPILE_ERROR;
    }
    parse_status = yyparse(&ast, symbols);
    (void)yylex_destroy();
    if (fclose(input) != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s'\n", options->source_path);
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    input = NULL;
    if (parse_status != 0 || ast == NULL) {
        if (parse_status == 0) fputs("error interno: el parser no produjo un AST\n", stderr);
        goto done;
    }
    if (options->verbose) fprintf(stderr, "[pipoc] Fase semántica\n");
    if (!analyze_semantics_with_trace(ast, symbols, 0)) {
        fputs("error: la verificación semántica falló\n", stderr);
        goto done;
    }
    if (options->clones_min_nodes > 0) {
        CloneMode mode = options->clones_mode == 0 ? CLONE_MODE_EXACT
                                                   : CLONE_MODE_RENAMED;
        CloneReport *report = clone_detect(ast, options->clones_min_nodes,
                                           options->source_path, mode);
        if (!report) {
            fputs("error: no se pudo ejecutar el detector de clones\n", stderr);
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto done;
        }
        clone_report_write_json(stdout, report, options->source_path,
                                options->clones_min_nodes, mode);
        if (fflush(stdout) != 0) {
            fputs("error: no se pudo escribir el reporte de clones\n", stderr);
            exit_status = PIPOC_EXIT_IO_ERROR;
        } else {
            exit_status = PIPOC_EXIT_SUCCESS;
        }
        clone_report_free(report);
        goto done;
    }
    if (options->check_only) {
        exit_status = PIPOC_EXIT_SUCCESS;
        goto done;
    }
    if (options->print_ast) ast_print(stdout, ast);
    if (options->print_symbols) symbol_table_print(stdout, symbols);
    if ((options->print_ast || options->print_symbols) && fflush(stdout) != 0) {
        fputs("error: no se pudo escribir la salida de diagnóstico\n", stderr);
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    if (options->json_output_path != NULL &&
        !write_ast_json(options->json_output_path, ast)) {
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    if (options->symbols_output_path != NULL &&
        !write_symbols(options->symbols_output_path, symbols)) {
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    if (options->ir_output_path != NULL || options->cfg_output_path != NULL) {
        IrDiagnostic diagnostic;
        if (options->verbose) fprintf(stderr, "[pipoc] Generación de IR\n");
        if (ir_generate(ast, symbols, &ir, &diagnostic) != IR_OK) {
            fprintf(stderr, "error IR en %d:%d: %s\n", diagnostic.location.first_line,
                diagnostic.location.first_column, diagnostic.message);
            goto done;
        }
        if (options->cfg_output_path != NULL) {
            CfgDiagnostic graph_error;
            if (options->verbose) fprintf(stderr, "[pipoc] Construcción de CFG\n");
            if (cfg_build(ir, &cfg, &graph_error) != CFG_OK) {
                fprintf(stderr, "error CFG en rutina %zu: %s\n", graph_error.routine, graph_error.message);
                goto done;
            }
        }
        if (options->ir_output_path != NULL) {
            exit_status = write_analysis(options->ir_output_path, ir, NULL);
            if (exit_status != PIPOC_EXIT_SUCCESS) goto done;
            if (options->verbose) fprintf(stderr, "[pipoc] IR: %s\n", options->ir_output_path);
        }
        if (options->cfg_output_path != NULL) {
            exit_status = write_analysis(options->cfg_output_path, ir, cfg);
            if (exit_status != PIPOC_EXIT_SUCCESS) goto done;
            if (options->verbose) fprintf(stderr, "[pipoc] CFG: %s\n", options->cfg_output_path);
        }
        /* Los fallos posteriores de codegen deben seguir siendo errores. */
        exit_status = PIPOC_EXIT_COMPILE_ERROR;
    }
    if (options->binary_output_path == NULL && options->asm_output_path == NULL &&
        options->mem_output_path == NULL && options->bundles_output_path == NULL &&
        !options->print_packages) {
        exit_status = PIPOC_EXIT_SUCCESS;
        goto done;
    }
    if (options->verbose) fprintf(stderr, "[pipoc] Generación de ASM\n");
    asm_stream = open_memstream(&asm_text, &asm_length);
    if (asm_stream == NULL) {
        fprintf(stderr, "error: no se pudo crear el buffer ASM: %s\n", strerror(errno));
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    codegen_result = codegen_generate(ast, symbols, asm_stream, &codegen_options);
    if (fclose(asm_stream) != 0) {
        asm_stream = NULL;
        fputs("error: no se pudo cerrar el buffer ASM\n", stderr);
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    asm_stream = NULL;
    if (codegen_result != CODEGEN_OK) {
        fprintf(stderr, "error: generación de código falló (código %d)\n", codegen_result);
        goto done;
    }
    if (options->asm_output_path != NULL &&
        !write_bytes(options->asm_output_path, asm_text, asm_length)) {
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    if (options->binary_output_path == NULL && options->mem_output_path == NULL &&
        options->bundles_output_path == NULL && !options->print_packages) {
        exit_status = PIPOC_EXIT_SUCCESS;
        goto done;
    }
    parsed = porygon_parse_program(asm_text, &program);
    if (parsed.status != PORYGON_PROGRAM_OK) {
        fprintf(stderr, "error: el ASM generado fue rechazado en %zu:%zu (código %d)\n",
                parsed.line, parsed.column, (int)parsed.status);
        goto done;
    }
    if (options->verbose)
        fprintf(stderr, "[pipoc] Calendarización de %zu instrucciones\n",
                program.instruction_count);
    if (!schedule_program(&program, &schedule)) goto done;
    if (!emit_scheduled_outputs(options, &program, &schedule)) {
        exit_status = PIPOC_EXIT_IO_ERROR;
        goto done;
    }
    exit_status = PIPOC_EXIT_SUCCESS;

done:
    if (asm_stream != NULL && fclose(asm_stream) != 0) {
        fputs("error: no se pudo cerrar el buffer ASM\n", stderr);
        exit_status = PIPOC_EXIT_IO_ERROR;
    }
    if (input != NULL && fclose(input) != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s'\n", options->source_path);
        exit_status = PIPOC_EXIT_IO_ERROR;
    }
    porygon_schedule_free(&schedule);
    porygon_machine_program_free(&program);
    free(asm_text);
    cfg_program_free(cfg);
    ir_program_free(ir);
    ast_free(ast);
    symbol_table_destroy(symbols);
    return exit_status;
}

static void report_command_outputs(const cli_options_t *options)
{
    if (options->ir_output_path != NULL)
        printf("[pipoc] ir: %s\n", options->ir_output_path);
    if (options->cfg_output_path != NULL)
        printf("[pipoc] cfg: %s\n", options->cfg_output_path);
    if (options->binary_output_path != NULL)
        printf("[pipoc] bin: %s\n", options->binary_output_path);
    if (options->asm_output_path != NULL)
        printf("[pipoc] asm: %s\n", options->asm_output_path);
    if (options->bundles_output_path != NULL)
        printf("[pipoc] sch: %s\n", options->bundles_output_path);
    if (options->mem_output_path != NULL)
        printf("[pipoc] mem: %s\n", options->mem_output_path);
    if (options->json_output_path != NULL)
        printf("[pipoc] ast: %s\n", options->json_output_path);
    if (options->symbols_output_path != NULL)
        printf("[pipoc] symbols: %s\n", options->symbols_output_path);
}

int main(int argc, char **argv)
{
    cli_options_t options;
    char *owned_paths[7] = {0};
    const char *outputs[8];
    const char *input_path;
    int parsed = parse_cli(argc, argv, &options);
    int exit_status;
    if (parsed == 0) return PIPOC_EXIT_SUCCESS;
    if (parsed < 0) {
        print_usage(stderr, argv[0]);
        return PIPOC_EXIT_USAGE;
    }
    if (options.check_only) {
        /* No se resuelven rutas de artefactos en este modo. El IDE recibe un
         * único reporte JSON por stdout y los mensajes humanos quedan en stderr. */
        if (options.diagnostics_json) diagnostics_begin(options.source_path);
        exit_status = compile_piper(&options);
        if (options.diagnostics_json &&
            (!diagnostics_write_json(stdout, exit_status == PIPOC_EXIT_SUCCESS) ||
             fflush(stdout) != 0)) {
            fputs("error: no se pudo producir el reporte de diagnósticos\n", stderr);
            exit_status = PIPOC_EXIT_IO_ERROR;
        }
        diagnostics_end();
        return exit_status;
    }
    if (options.ll1) {
        ll1_print_predictive_table(stdout);
        return PIPOC_EXIT_SUCCESS;
    }
    if (options.ll1_expected) {
        Ll1AnalysisResult ll1_result =
            ll1_expected_file(options.source_path, stdout);
        if (ll1_result == LL1_ANALYSIS_ACCEPTED) return PIPOC_EXIT_SUCCESS;
        if (ll1_result == LL1_ANALYSIS_IO_ERROR) return PIPOC_EXIT_IO_ERROR;
        return PIPOC_EXIT_COMPILE_ERROR;
    }
    input_path = options.binary_input_path != NULL ? options.binary_input_path :
                 options.asm_input_path != NULL ? options.asm_input_path : options.source_path;

    switch (options.command) {
    case PIPOC_COMMAND_IR:
        if (options.requested_output_path && options.ir_output_path) {
            fputs("error: ir recibió dos rutas de salida (-o y --emit-ir)\n", stderr);
            exit_status = PIPOC_EXIT_USAGE;
            goto cleanup;
        }
        if (options.requested_output_path) options.ir_output_path = options.requested_output_path;
        if (!default_output_path(input_path, ".ir", &options.ir_output_path, &owned_paths[6])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    case PIPOC_COMMAND_ASM:
        if (options.requested_output_path != NULL && options.asm_output_path != NULL) {
            fputs("error: asm recibió dos rutas de salida (-o y --emit-asm)\n", stderr);
            exit_status = PIPOC_EXIT_USAGE;
            goto cleanup;
        }
        if (options.requested_output_path != NULL)
            options.asm_output_path = options.requested_output_path;
        if (!default_output_path(input_path, ".asm", &options.asm_output_path,
                                 &owned_paths[1])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    case PIPOC_COMMAND_SCHEDULE:
        if (options.requested_output_path != NULL && options.bundles_output_path != NULL) {
            fputs("error: sch recibió dos rutas de salida (-o y --emit-bundles)\n", stderr);
            exit_status = PIPOC_EXIT_USAGE;
            goto cleanup;
        }
        if (options.requested_output_path != NULL)
            options.bundles_output_path = options.requested_output_path;
        if (!default_output_path(input_path, ".bundles", &options.bundles_output_path,
                                 &owned_paths[2])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    case PIPOC_COMMAND_MEM:
        if (options.requested_output_path != NULL && options.mem_output_path != NULL) {
            fputs("error: mem recibió dos rutas de salida (-o y --emit-mem)\n", stderr);
            exit_status = PIPOC_EXIT_USAGE;
            goto cleanup;
        }
        if (options.requested_output_path != NULL)
            options.mem_output_path = options.requested_output_path;
        if (!default_output_path(input_path, ".mem", &options.mem_output_path,
                                 &owned_paths[3])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    case PIPOC_COMMAND_INSPECT:
        if (options.requested_output_path != NULL) {
            fputs("error: ins tiene dos salidas; use --ast-json y --emit-symbols\n", stderr);
            exit_status = PIPOC_EXIT_USAGE;
            goto cleanup;
        }
        if (!default_output_path(input_path, ".ast.json", &options.json_output_path,
                                 &owned_paths[4]) ||
            !default_output_path(input_path, ".symbols.txt", &options.symbols_output_path,
                                 &owned_paths[5])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    case PIPOC_COMMAND_ALL:
        options.binary_output_path = options.requested_output_path;
        if (!default_output_path(input_path, ".bin", &options.binary_output_path,
                                 &owned_paths[0]) ||
            !default_output_path(input_path, ".asm", &options.asm_output_path,
                                 &owned_paths[1]) ||
            !default_output_path(input_path, ".bundles", &options.bundles_output_path,
                                 &owned_paths[2]) ||
            !default_output_path(input_path, ".mem", &options.mem_output_path,
                                 &owned_paths[3]) ||
            !default_output_path(input_path, ".ast.json", &options.json_output_path,
                                 &owned_paths[4]) ||
            !default_output_path(input_path, ".symbols.txt", &options.symbols_output_path,
                                 &owned_paths[5])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    case PIPOC_COMMAND_DEFAULT:
        options.binary_output_path = options.requested_output_path;
        if (!default_output_path(input_path,
                options.binary_input_path != NULL ? ".mem" : ".bin",
                &options.binary_output_path, &owned_paths[0])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
        break;
    }

    if (options.source_path != NULL && options.keep_asm && options.asm_output_path == NULL) {
        if (!default_output_path(options.source_path, ".asm", &options.asm_output_path,
                                 &owned_paths[1])) {
            exit_status = PIPOC_EXIT_COMPILE_ERROR;
            goto cleanup;
        }
    }
    outputs[0] = options.binary_output_path;
    outputs[1] = options.asm_output_path;
    outputs[2] = options.mem_output_path;
    outputs[3] = options.bundles_output_path;
    outputs[4] = options.json_output_path;
    outputs[5] = options.symbols_output_path;
    outputs[6] = options.ir_output_path;
    outputs[7] = options.cfg_output_path;
    if (options.ir_output_path || options.cfg_output_path) {
        const char *reserved[] = {".rwt", ".bin", ".asm", ".mem", ".bundles", ".ast.json", ".symbols.txt"};
        for (size_t i = 0; i < sizeof(reserved) / sizeof(*reserved); i++)
            if ((options.ir_output_path && ends_with(options.ir_output_path, reserved[i])) ||
                (options.cfg_output_path && ends_with(options.cfg_output_path, reserved[i]))) {
                fputs("error: la salida IR/CFG no puede usar la extensión de un fuente u otro artefacto\n", stderr);
                exit_status = PIPOC_EXIT_USAGE;
                goto cleanup;
            }
    }
    if ((options.ir_output_path && ends_with(options.ir_output_path, ".dot")) ||
        (options.cfg_output_path && ends_with(options.cfg_output_path, ".ir"))) {
        fputs("error: no intercambiar extensiones .ir y .dot entre las salidas\n", stderr);
        exit_status = PIPOC_EXIT_USAGE;
        goto cleanup;
    }
    if (!validate_paths(input_path, outputs, sizeof(outputs) / sizeof(outputs[0]))) {
        exit_status = PIPOC_EXIT_USAGE;
        goto cleanup;
    }
    if (options.binary_input_path != NULL) exit_status = convert_binary(&options);
    else if (options.asm_input_path != NULL) exit_status = compile_assembly(&options);
    else exit_status = compile_piper(&options);
    if (exit_status == PIPOC_EXIT_SUCCESS &&
        options.command != PIPOC_COMMAND_DEFAULT) {
        report_command_outputs(&options);
    }

cleanup:
    for (size_t index = 0; index < sizeof(owned_paths) / sizeof(owned_paths[0]); ++index)
        free(owned_paths[index]);
    return exit_status;
}
