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

/* lval is a lisp value type, it can be a number, error or operator/symbol.
 * it holds a count to how many pointers are in the array cell, cell is an
 * array of pointers to lisp values. basically a linked list structure but
 * implemented as a dynamic array.
*/
typedef struct lval {
    int type;
    long num;

    char* err;
    char* sym;

    int count;
    struct lval** cell;
} lval;

lval* lval_eval(lval* v);

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };  // possible lval types

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
/* sexpr type represents a symbolic expression defined by zero or more
 * expressions wrapped in parentheses. The constructor allocates some memory
 * for a new sexpression, which is just a single lval that has no children
 * yet and points to null. This is setting up the sexpression to grow dynamically
 * as the reader adds more expressions to the sexpression. cell will point to
 * many children lvals which can be any valid expression (see formal grammar).
*/
lval* lval_sexpr(void) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

/* pointer to a new empty q expression */
lval* lval_qexpr(void) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

/* deconstructor: need to free else memory leaks */
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
        case LVAL_QEXPR: // qexpressions have similar semantics to sexpr, except you don't eval
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
/* reader takes an ast from the parser, compares the tags (expr, number, regex etc)
 * and creates lval array which represents the sexpression. Don't forget
 * the ast is one giant s-expression.
 * 
 * e.g. the line: (+ 2 2) is convert to
 * root : lval sexpr | count 3 | cell [ addr1, addr2, addr3];
 * addr1: lval symbol | count undefined | cell undefined | sym +
 * addr2: lval number | count undefined | cell undefined | num 2
 * addr2: lval number | count undefined | cell undefined | num 2
 * 
 * the root or first lval* is returned
*/
lval* lval_read(mpc_ast_t* ast) {
    if(strstr(ast->tag, "number")) { return lval_read_num(ast); }
    if(strstr(ast->tag, "symbol")) { return lval_sym(ast->contents); }

    lval* x = NULL;
    if (strcmp(ast->tag, ">") == 0) { x = lval_sexpr(); } // if the input is + 2 2 no parens exactly
    if (strstr(ast->tag, "sexpr"))  { x = lval_sexpr(); } // if the input is (+ 2 2) w/ parens
    if (strstr(ast->tag, "qexpr"))  { x = lval_qexpr(); } // if the input is {+ 2 2}

    for (int i = 0; i < ast->children_num; i++) {
        if (strcmp(ast->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(ast->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(ast->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(ast->children[i]->contents, "}") == 0) { continue; }
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
            printf("%li", v->num);
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
        case LVAL_QEXPR:
            lval_expr_print(v, '{', '}');
            break;
    }
}
/* because the ast is converted to one giant sexpr, to process it
 * you need to traverse it, all its children, hence all the 
 * for loops with pointers.
*/
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

// pops the cell array, removes lval* at i and returns it.
lval* lval_pop(lval* v, int i) {
    // find the item at i
    lval* x = v->cell[i];
    // shift memory
    memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval*) * (v->count - i - 1));
    v->count--; // decrease count of items in the list
    v->cell = realloc(v->cell, sizeof(lval*) * v->count); // reallocate memory used
    return x;
}
// takes element i in cell array of v, returns it and deletes v
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

#define LASSERT(args, cond, err) if (!(cond)) { lval_del(args); return lval_err(err); }

// built in head function to operate on q-expressions aka lists
lval* builtin_head(lval* a) {
    LASSERT(a, a->count == 1, "Function 'head' passed too many args")
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect types")
    LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}")

    lval* v = lval_take(a, 0);
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}
// built in tail function to operate on q-expressions aka lists
lval* builtin_tail(lval* a) {
  /* Check Error Conditions */
    LASSERT(a, a->count == 1, "Function 'tail' passed too many args")
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect types")
    LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}")

    /* Take first argument */
    lval* v = lval_take(a, 0);

    /* Delete first element and return */
    lval_del(lval_pop(v, 0));
    return v;
}
// built in list function, convers sexpressions to list
lval* builtin_list(lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lval* a) {
    LASSERT(a, a->count == 1, "Function 'eval' passed too many arguments");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type");

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(x);
}

lval* lval_join(lval* x, lval* y) {
    /* For each cell in 'y' add it to 'x' */
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }
    /* Delete the empty 'y' and return 'x' */
    lval_del(y);
    return x;
}

lval* builtin_join(lval* expr) {
    for (int i = 0; i < expr->count; i++) {
        LASSERT(expr, expr->cell[i]->type == LVAL_QEXPR, "Function 'join' passed incorrect type.");
    }

    lval* x = lval_pop(expr, 0);

    while (expr->count) {
        x = lval_join(x, lval_pop(expr, 0));
    }

    lval_del(expr);
    return x;
}

// lval* builtin_cons(lval* expr) {}
// lval* builtin_len(lval* expr) {}

lval* builtin(lval* expr, char* func) {
    if (strcmp("list", func) == 0) { return builtin_list(expr); }
    if (strcmp("head", func) == 0) { return builtin_head(expr); }
    if (strcmp("tail", func) == 0) { return builtin_tail(expr); }
    if (strcmp("join", func) == 0) { return builtin_join(expr); }
    if (strcmp("eval", func) == 0) { return builtin_eval(expr); }
    if (strstr("+-/*", func)) { return builtin_op(expr, func); }
    lval_del(expr);
    return lval_err("Unknown Function");
}

/* so the expression (+ 2 2) passed through eval
 * (+ 2 2) = lval sexpr [lval sym, lval num, lval num]
 * 
*/
lval* lval_eval_sexpr(lval* v) {
    // evaluate children if there are any
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(v->cell[i]);
    }
    // error checking, here the only error will be an invalid number
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
    lval* result = builtin(v, f->sym);
    lval_del(f);
    return result;
}

/* lval_eval is passed the root or first lval from the reader, the
 * reader having converted and ast to a lval list. lval type is almost
 * guarenteed to be an sexpr unless its an expr or invalid.
*/
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
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Program = mpc_new("program");

    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                               \
        number  : /-?[0-9]+/ ;                                                                      \
        symbol  : '+' | '*' | '/' | '-' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"mod\"     \
                | \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\" ;                            \
        sexpr   : '(' <expr>* ')' ;                                                                 \
        qexpr   : '{' <expr>* '}' ;                                                                 \
        expr    : <number> | <symbol> | <sexpr> | <qexpr>;                                          \
        program : /^/ <expr>* /$/ ;                                                                 \
    ", Number, Symbol, Sexpr, Qexpr, Expr, Program);

    /* start interactive prompt */
    puts("HyperLambda lisp Version 0.0.4");
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
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Program);
    return 0;
}