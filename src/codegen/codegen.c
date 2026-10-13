= Orquestador principal

codegen_generate(root, table, output, options)
    │
    ├─▶ codegen_context_create(...)
    │       └─ inicializa label_counter=0, current_function=NULL, etc.
    │
    ├─▶ para cada elemento en root->data.program.elements:
    │       ├─ AST_ROUTINE_DECLARATION → codegen_routine(ctx, routine)
    │       ├─ AST_VARIABLE_DECLARATION (global) → codegen_global_decl(ctx, decl)
    │       ├─ AST_IMPORT_DECLARATION → ignorar o advertir
    │       └─ otro → error
    │
    ├─▶ codegen_context_destroy(ctx)
    │
    └─▶ retornar CODEGEN_OK o CODEGEN_ERROR_*
