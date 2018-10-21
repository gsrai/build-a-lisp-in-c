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

typedef struct {
    int type; // can be NUM or ERR
    long num;
    int err;
} lval; // lisp value

enum { LVAL_NUM, LVAL_ERR };                        // possible lval types
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };  // possible error types

/* constructors */
lval lval_num(long x) {
    lval v;
    v.type = LVAL_NUM;
    v.num = x;
    return v;
}

lval lval_err(int x) {
    lval v;
    v.type = LVAL_ERR;
    v.err = x;
    return v;
}

/* print an lisp value */
void lval_print(lval v) {
    switch (v.type) {
        case LVAL_NUM:
            printf("%li\n", v.num);
            break;
        case LVAL_ERR:
            if (v.err == LERR_DIV_ZERO) { printf("Error: Division by Zero!"); }
            if (v.err == LERR_BAD_OP) { printf("Error: Invalid Operator!"); }
            if (v.err == LERR_BAD_NUM) { printf("Error: Invalid Number!"); }
            break;
    }
}

void lval_println(lval v) {
    lval_print(v);
    putchar('\n');
}

/* use operator string to apply the correct operator */
lval eval_op(lval x, char* op, lval y) {
    // If x or y is an error, then return it
    if (x.type == LVAL_ERR) { return x; }
    if (y.type == LVAL_ERR) { return y; }

    if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { return lval_num(x.num + y.num); }
    if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) { return lval_num(x.num - y.num); }
    if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { return lval_num(x.num * y.num); }
    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
        return y.num == 0 ? lval_err(LERR_DIV_ZERO) : lval_num(x.num / y.num);
    }
    if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
        return y.num == 0 ? lval_err(LERR_DIV_ZERO) : lval_num(x.num % y.num);
    }
    return lval_err(LERR_BAD_OP); // invalid op
}

lval eval(mpc_ast_t* ast) {
    // if node is a number, return it as there is no more children
    if (strstr(ast->tag, "number")) {
        errno = 0; // mutated by strtol
        long x = strtol(ast->contents, NULL, 10);
        return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
    }

    // if not a number, then it has children (if valid ast)
    char* op = ast->children[1]->contents; // '(+ 1 1)'[1] = '+'

    // operand could be another expression or number either case, call eval
    lval x = eval(ast->children[2]);

    int i = 3;
    while (strstr(ast->children[i]->tag, "expr")) { // don't forget a number is also an expr, operator is not
        x = eval_op(x, op, eval(ast->children[i])); // eval the op with its args
        i++;
    }

    return x;
}

/* so far we define the grammar, create the parsers, parse the input, build an ast. */
int main(int argc, char** argv) {
    /* define grammar and create some parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Operator = mpc_new("operator");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Program = mpc_new("program");

    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                               \
        number  : /-?[0-9]+/ ;                                                                      \
        operator: '+' | '*' | '/' | '-' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"mod\" ;   \
        expr    : <number> | '(' <operator> <expr>+ ')' ;                                           \
        program : /^/ <operator> <expr>+ /$/ ;                                                      \
    ", Number, Operator, Expr, Program);

    /* start interactive prompt */
    puts("HyperLambda lisp Version 0.0.1");
    puts("Press Ctrl+C to Exit\n");

    while(1) {
        char* input = readline("λ> ");
        add_history(input);

        mpc_result_t parse_result;
        if (mpc_parse("<stdin>", input, Program, &parse_result)) {
            lval eval_result = eval(parse_result.output);
            lval_println(eval_result);
            mpc_ast_delete(parse_result.output);
        } else {
            mpc_err_print(parse_result.error);
            mpc_err_delete(parse_result.error);
        }

        free(input);
    }

    /* delete parsers */
    mpc_cleanup(4, Number, Operator, Expr, Program);
    return 0;
}