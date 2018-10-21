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

typedef struct lval {
    int type; // can be NUM or ERR
    long num;

    char* err;
    char* sym;

    int count;
    struct lval** cell;
} lval; // lisp value

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };  // possible lval types

/* constructors */
lval* lval_num(long x) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* msg) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_ERR;
    v->err = (char*) malloc(strlen(msg) + 1); // because '\0'
    strcpy(v->err, msg);
    return v;
}

lval* lval_sym(char* str) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = (char*) malloc(strlen(str) + 1);
    strcpy(v->sym, str);
    return v;
}

lval* lval_sexpr(void) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

/* deconstructor */
void lval_del(lval* v) {
    switch (v->type) {
        case LVAL_NUM:
            break;
        // free the strings allocated for err and sym
        case LVAL_ERR:
            free(v->err);
            break;
        case LVAL_SYM:
            free(v->sym);
            break;
        case LVAL_SEXPR: // free each sexpr pointed to by the array of pointers: cell
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell); // free the array of lval structs
            break;
    }
    free(v); // free memory allocated to lval struct
}

/* The reader */
lval* lval_read_num(mpc_ast_t* ast) {
    errno = 0;
    long x = strtol(ast->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err("Invalid Number");
}

lval* lval_add(lval* v, lval* x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

lval* lval_read(mpc_ast_t* ast) {
    if(strstr(ast->tag, "number")) { return lval_read_num(ast); }
    if(strstr(ast->tag, "symbol")) { return lval_sym(ast->contents); }

    lval* x = NULL;
    if (strcmp(ast->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(ast->tag, "sexpr"))  { x = lval_sexpr(); }

    for (int i = 0; i < ast->children_num; i++) {
        if (strcmp(ast->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(ast->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(ast->children[i]->tag,  "regex") == 0) { continue; }
        x = lval_add(x, lval_read(ast->children[i]));
    }
    return x;
}


/* the printer */
void lval_expr_print(lval* v, char open, char close);

void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM:
            printf("%li\n", v->num);
            break;
        case LVAL_ERR:
            printf("Error: %s", v->err);
            break;
        case LVAL_SYM:
            printf("%s", v->sym);
            break;
        case LVAL_SEXPR:
            lval_expr_print(v, '(', ')');
            break;
    }
}

void lval_expr_print(lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);
        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_println(lval* v) {
    lval_print(v);
    putchar('\n');
}

lval* lval_pop(lval* v, int i) {
    // find the item at i
    lval* x = v->cell[i];
    // shift memory
    memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval*) * (v->count - i - 1));
    v->count--; // decrease count of items in the list
    v->cell = realloc(v->cell, sizeof(lval*) * v->count); // reallocate memory used
    return x;
}

lval* lval_take(lval* v, int i) {
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

lval* builtin_op(lval* a, char* op) {
    // ensure all args are numbers
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }
    // pop the first element
    lval* x = lval_pop(a, 0);
    // if no args and sub then perform unary negation
    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }
    // while there are still elements remaining
    while (a->count > 0) {
        // pop next element
        lval* y = lval_pop(a, 0);
        // perform operation
        if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { x->num *= y->num; }
        if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By Zero");
                break;
            }
            x->num /= y->num;
        }
        if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By Zero");
                break;
            }
            x->num %= y->num;
        }
        lval_del(y); // delete y as we do not need it anymore
    }
    lval_del(a); // delete input expression
    return x;
}

lval* lval_eval(lval* v);

lval* lval_eval_sexpr(lval* v) {
    // evaluate children
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(v->cell[i]);
    }
    // error checking
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
    }
    // empty expression
    if (v->count == 0) { return v; }
    // single expression
    if (v->count == 1) { return lval_take(v, 0); }
    // ensure first element is a symbol
    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_SYM) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression Does not start with symbol");
    }
    // call builtin with operator
    lval* result = builtin_op(v, f->sym);
    lval_del(f);
    return result;
}

lval* lval_eval(lval* v) {
    // evaluate S-expressions
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
    return v;
}

/* so far we define the grammar, create the parsers, parse the input, build an ast. */
int main(int argc, char** argv) {
    /* define grammar and create some parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol"); // operators, variables, functions etc
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Program = mpc_new("program");

    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                               \
        number  : /-?[0-9]+/ ;                                                                      \
        symbol  : '+' | '*' | '/' | '-' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"mod\" ;   \
        sexpr   : '(' <expr>* ')' ;                                                                 \
        expr    : <number> | <symbol> | <sexpr> ;                                                   \
        program : /^/ <expr>* /$/ ;                                                                 \
    ", Number, Symbol, Sexpr, Expr, Program);

    /* start interactive prompt */
    puts("HyperLambda lisp Version 0.0.3");
    puts("Press Ctrl+C to Exit\n");

    while(1) {
        char* input = readline("λ> ");
        add_history(input);

        mpc_result_t parse_result;
        if (mpc_parse("<stdin>", input, Program, &parse_result)) {
            lval* eval_result = lval_eval(lval_read(parse_result.output));
            lval_println(eval_result);
            lval_del(eval_result);
            mpc_ast_delete(parse_result.output);
        } else {
            mpc_err_print(parse_result.error);
            mpc_err_delete(parse_result.error);
        }
        free(input);
    }
    /* delete parsers */
    mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Program);
    return 0;
}