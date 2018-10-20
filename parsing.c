#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <string.h>

static const short BUF_SIZE = 2048;
static char buffer[BUF_SIZE];

/* mock readline function for portability */
char* readline(char* prompt) {
    fputs(prompt, stdout);
    fgets(buffer, BUF_SIZE, stdin);
    char* cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';
    return cpy;
}

void add_history(char* unused) { /* mock */ }

#else
#include <editline/readline.h>
#endif

/* so far we define the grammar, create the parsers, parse the input, build an ast. */
int main(int argc, char** argv) {
    /* define grammar and create some parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Operator = mpc_new("operator");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Program = mpc_new("program");

    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                   \
        number  : /-?[0-9]+/ ;                                                          \
        operator: '+' | '*' | '/' | '-' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" ; \
        expr    : <number> | '(' <operator> <expr>+ ')' ;                               \
        program : /^/ <operator> <expr>+ /$/ ;                                          \
    ", Number, Operator, Expr, Program);

    /* start interactive prompt */
    puts("HyperLambda lisp Version 0.0.1");
    puts("Press Ctrl+C to Exit\n");

    while(1) {
        char* input = readline("λ> ");
        add_history(input);

        mpc_result_t result;
        if (mpc_parse("<stdin>", input, Program, &result)) {
            // on success print the AST
            mpc_ast_print(result.output);
            mpc_ast_delete(result.output);
        } else {
            mpc_err_print(result.error);
            mpc_err_delete(result.error);
        }

        free(input);
    }

    /* delete parsers */
    mpc_cleanup(4, Number, Operator, Expr, Program);
    return 0;
}