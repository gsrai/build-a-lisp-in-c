#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv; // basically so you don't need to type out struct lenv every time

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUNC };  // possible lval types

char* ltype_name(int t) {
    switch(t) {
        case LVAL_FUNC:
            return "Function";
        case LVAL_NUM:
            return "Number";
        case LVAL_ERR:
            return "Error";
        case LVAL_SYM:
            return "Symbol";
        case LVAL_SEXPR:
            return "S-Expression";
        case LVAL_QEXPR:
            return "Q-Expression";
        default:
            return "Unknown";
    }
}

typedef lval*(*lbuiltin)(lenv*, lval*);

// forward declaration TODO: move to header file
lval* lval_eval(lenv* env, lval* value);
lval* lval_copy(lval* value);
void lval_del(lval* v);
lval* lval_err(char* fmt, ...);
lval* lval_pop(lval* v, int i);
lval* builtin_eval(lenv* env, lval* args);
lval* builtin_list(lenv* env, lval* a);

/* lval is a lisp value type, it can be a number, error or operator/symbol.
 * it holds a count to how many pointers are in the array cell, cell is an
 * array of pointers to lisp values. basically a linked list structure but
 * implemented as a dynamic array.
*/
struct lval {
    int type;

    long num;
    char* err;
    char* sym;
    
    lbuiltin builtin; // if null then it is user defined function
    lenv* env;
    lval* params;
    lval* body;

    int count;
    lval** cell;
};

/* environment struct holds name/symbol value associations */
struct lenv {
    lenv* parenv; // parent environment
    int count;
    char** symbols;
    lval** values;
};

lenv* lenv_new(void) {
    lenv* env = malloc(sizeof(lenv));
    env->count = 0;
    env->symbols = NULL;
    env->values = NULL;
    env->parenv = NULL;
    return env;
};

void lenv_del(lenv* env) {
    for (int i = 0; i < env->count; i++) {
        free(env->symbols[i]);
        lval_del(env->values[i]);
    }
    free(env->symbols);
    free(env->values);
    free(env);
}

// takes the environment and the symbol, returns the value
lval* lenv_get(lenv* env, lval* val) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->symbols[i], val->sym) == 0) {
            return lval_copy(env->values[i]);
        }
    }
    // if symbol was not found, look in parent environment
    if (env->parenv) {
        return lenv_get(env->parenv, val);
    } else {
        return lval_err("Unbound Symbol '%s'", val->sym);
    }
}

void lenv_put(lenv* env, lval* symbol, lval* value) {
    /* Iterate over all items in environment */
    /* This is to see if variable already exists */
    for (int i = 0; i < env->count; i++) {
        /* If variable is found delete item at that position */
        /* And replace with variable supplied by user */
        if (strcmp(env->symbols[i], symbol->sym) == 0) {
            lval_del(env->values[i]);
            env->values[i] = lval_copy(value);
            return;
        }
    }

    /* If no existing entry found allocate space for new entry */
    env->count++;
    env->values = realloc(env->values, sizeof(lval*) * env->count);
    env->symbols = realloc(env->symbols, sizeof(char*) * env->count);

    /* Copy contents of lval and symbol string into new location */
    env->values[env->count-1] = lval_copy(value);
    env->symbols[env->count-1] = malloc(strlen(symbol->sym)+1);
    strcpy(env->symbols[env->count-1], symbol->sym);
}

// defining a variable in the global scope
void lenv_def(lenv* env, lval* symbol, lval* value) {
    while (env->parenv) { env = env->parenv; }
    lenv_put(env, symbol, value);
}

lenv* lenv_copy(lenv* env) {
    lenv* new_env = malloc(sizeof(lenv));
    new_env->parenv = env->parenv;
    new_env->count = env->count;
    new_env->symbols = malloc(sizeof(char*) * env->count);
    new_env->values  = malloc(sizeof(lval*) * env->count);
    for (int i = 0; i < env->count; i++) {
        new_env->symbols[i] = malloc(strlen(env->symbols[i]) + 1);
        strcpy(new_env->symbols[i], env->symbols[i]);
        new_env->values[i] = lval_copy(env->values[i]);
    }
    return new_env;
}

/* constructors */
lval* lval_num(long x) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* fmt, ...) {
    lval* v = (lval*) malloc(sizeof(lval));
    v->type = LVAL_ERR;

    /* Create a va list and initialize it */
    va_list va;
    va_start(va, fmt);

    /* Allocate 512 bytes of space */
    v->err = malloc(512);

    /* printf the error string with a maximum of 511 characters */
    vsnprintf(v->err, 511, fmt, va);

    /* Reallocate to number of bytes actually used */
    v->err = realloc(v->err, strlen(v->err)+1);

    /* Cleanup our va list */
    va_end(va);
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

lval* lval_func(lbuiltin func) {
    lval* val = malloc(sizeof(lval));
    val->type = LVAL_FUNC;
    val->builtin = func;
    return val;
}

lval* lval_lambda(lval* params, lval* body) {
    lval* lambda = malloc(sizeof(lval));
    lambda->type = LVAL_FUNC;
    lambda->builtin = NULL;
    lambda->env = lenv_new();
    lambda->params = params;
    lambda->body = body;
    return lambda;
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
        case LVAL_FUNC:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->params);
                lval_del(v->body);
            }
            break; // do nothing for function pointers
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
        case LVAL_FUNC:
            if (v->builtin) {
                printf("<builtin>"); 
            } else {
                printf("(\\");
                lval_print(v->params);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
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

/* Lval utils */
lval* lval_copy(lval* v) {
    lval* x = malloc(sizeof(lval)); // create new lval
    x->type = v->type; // copy type info

    switch (v->type) {
        /* Copy Functions and Numbers Directly */
        case LVAL_FUNC:
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->params = lval_copy(v->params);
                x->body = lval_copy(v->body);
            }
            break;
        case LVAL_NUM:
            x->num = v->num;
            break;
        /* Copy Strings using malloc and strcpy */
        case LVAL_ERR:
            x->err = malloc(strlen(v->err) + 1); // this is how you copy a string
            strcpy(x->err, v->err);
            break;
        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym);
            break;
        /* Copy Lists by copying each sub-expression */
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval*) * x->count); // copy the array
            for (int i = 0; i < x->count; i++) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
            break;
    }
    return x;
}

lval* lval_call(lenv* env, lval* func, lval* args) {
    /* If Builtin then simply apply that */
    if (func->builtin) { return func->builtin(env, args); }
    /* Record Argument Counts */
    int given = args->count;
    int total = func->params->count;
    /* While arguments still remain to be processed */
    while (args->count) {
        /* If we've ran out of formal arguments to bind */
        if (func->params->count == 0) {
            lval_del(args);
            return lval_err("Function passed too many arguments. Got %i, Expected %i.", given, total);
        }
        lval* sym = lval_pop(func->params, 0); // Pop the first symbol from the formals

        // special case for variable argument list using '&'
        if (strcmp(sym->sym, "&") == 0) {
            if (func->params->count != 1) {
                lval_del(args);
                return lval_err("Function format invalid. Symbol '&' not followed by 1 or more symbols");
            }
            lval* next_sym = lval_pop(func->params, 0);
            lenv_put(func->env, next_sym, builtin_list(env, args));
            lval_del(sym);
            lval_del(next_sym);
            break;
        }

        lval* val = lval_pop(args, 0); // Pop the next argument from the list
        lenv_put(func->env, sym, val); // Bind a copy into the function's environment
        lval_del(sym);
        lval_del(val);
    }
    /* Argument list is now bound so can be cleaned up */
    lval_del(args);

    /* If '&' remains in formal list bind to empty list */
    if (func->params->count > 0 && strcmp(func->params->cell[0]->sym, "&") == 0) {
        /* Check to ensure that & is not passed invalidly. */
        if (func->params->count != 2) {
            return lval_err("Function format invalid. Symbol '&' not followed by single symbol.");
        }
        /* Pop and delete '&' symbol */
        lval_del(lval_pop(func->params, 0));
        /* Pop next symbol and create empty list */
        lval* sym = lval_pop(func->params, 0);
        lval* val = lval_qexpr();

        /* Bind to environment and delete */
        lenv_put(func->env, sym, val);
        lval_del(sym);
        lval_del(val);
    }

    /* If all params have been bound evaluate */
    if (func->params->count == 0) {
        /* Set environment parent to evaluation environment */
        func->env->parenv = env;
        return builtin_eval(func->env, lval_add(lval_sexpr(), lval_copy(func->body)));
    } else {
        return lval_copy(func); // Otherwise return partially evaluated function
    }
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

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

#define LASSERT_TYPE(func, args, index, expect) \
    LASSERT(args, args->cell[index]->type == expect, \
        "Function '%s' passed incorrect type for argument %i. Got %s, Expected %s.", \
        func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM_ARGS(func, args, num) \
    LASSERT(args, args->count == num, \
        "Function '%s' passed incorrect number of arguments. Got %i, Expected %i.", \
        func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
    LASSERT(args, args->cell[index]->count != 0, \
        "Function '%s' passed {} for argument %i.", func, index);

/* Built in operations */
lval* builtin_op(lenv* env, lval* args, char* op) { // only arithmetic
    // ensure all args are numbers
    for (int i = 0; i < args->count; i++) {
        LASSERT_TYPE(op, args, i, LVAL_NUM);
    }
    // pop the first element
    lval* x = lval_pop(args, 0);
    // if no args and sub then perform unary negation
    if ((strcmp(op, "-") == 0) && args->count == 0) {
        x->num = -x->num;
    }
    // while there are still elements remaining
    while (args->count > 0) {
        // pop next element
        lval* y = lval_pop(args, 0);
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
    lval_del(args); // delete input expression
    return x;
}

// built in head function to operate on q-expressions aka lists
lval* builtin_head(lenv* env, lval* a) {
    LASSERT_NUM_ARGS("head", a, 1);
    LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("head", a, 0);

    lval* v = lval_take(a, 0);
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}
// built in tail function to operate on q-expressions aka lists
lval* builtin_tail(lenv* env, lval* a) {
    /* Check Error Conditions */
    LASSERT_NUM_ARGS("tail", a, 1);
    LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("tail", a, 0);

    /* Take first argument */
    lval* v = lval_take(a, 0);

    /* Delete first element and return */
    lval_del(lval_pop(v, 0));
    return v;
}
// built in list function, convers sexpressions to list
lval* builtin_list(lenv* env, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lenv* env, lval* args) {
    LASSERT_NUM_ARGS("eval", args, 1);
    LASSERT_TYPE("eval", args, 0, LVAL_QEXPR);

    lval* x = lval_take(args, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(env, x);
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

lval* builtin_join(lenv* env, lval* expr) {
    for (int i = 0; i < expr->count; i++) {
        LASSERT_TYPE("join", expr, i, LVAL_QEXPR);
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

lval* builtin_add(lenv* env, lval* args) {
    return builtin_op(env, args, "+");
}

lval* builtin_sub(lenv* env, lval* args) {
    return builtin_op(env, args, "-");
}

lval* builtin_mul(lenv* env, lval* args) {
    return builtin_op(env, args, "*");
}

lval* builtin_div(lenv* env, lval* args) {
    return builtin_op(env, args, "/");
}

lval* builtin_mod(lenv* env, lval* args) {
    return builtin_op(env, args, "%");
}

// register builtin functions into the environment
void lenv_add_builtin(lenv* env, char* name, lbuiltin func) {
    lval* key = lval_sym(name);
    lval* val = lval_func(func);
    lenv_put(env, key, val);
    lval_del(key);
    lval_del(val);
}

lval* builtin_var(lenv* env, lval* args, char* func) {
    LASSERT_TYPE(func, args, 0, LVAL_QEXPR);
    lval* symbols = args->cell[0]; // first arg is symbol list
    for (int i = 0; i < symbols->count; i++) { // ensure all symbols are actually symbols
        LASSERT(args, symbols->cell[i]->type == LVAL_SYM,
            "Function '%s' cannot define non-symbol. Received %s, Expected %s.", func,
            ltype_name(symbols->cell[i]->type), ltype_name(LVAL_SYM));
    }

    /* Check correct number of symbols and values */
    LASSERT(args, (symbols->count == args->count-1),
        "Function '%s' passed too many arguments for symbols. "
        "Got %i, Expected %i.", func, symbols->count, args->count-1);

    /* Assign copies of values to symbols */
    for (int i = 0; i < symbols->count; i++) {
        /* If 'def' define in globally. If 'put' define in locally */
        if (strcmp(func, "def") == 0) {
            lenv_def(env, symbols->cell[i], args->cell[i+1]);
        }

        if (strcmp(func, "=")   == 0) {
            lenv_put(env, symbols->cell[i], args->cell[i+1]);
        }
        
    }

    lval_del(args);
    return lval_sexpr();
}

lval* builtin_def(lenv* env, lval* args) {
    return builtin_var(env, args, "def");   
}

lval* builtin_put(lenv* env, lval* args) {
    return builtin_var(env, args, "=");   
}

lval* builtin_lambda(lenv* env, lval* args) {
    // a lambda must have exactly two arguements of type list
    LASSERT_NUM_ARGS("lambda", args, 2);
    LASSERT_TYPE("lambda", args, 0, LVAL_QEXPR);
    LASSERT_TYPE("lambda", args, 1, LVAL_QEXPR);

    char* expected_type = ltype_name(LVAL_SYM);
    // check the list of symbols is all symbols
    for (int i = 0; i < args->cell[0]->count; i++) { // args->cell[0] is the list of symbols
        int arg_type = args->cell[0]->cell[i]->type;
        LASSERT(args, (arg_type == LVAL_SYM),
            "Cannot define non-symbol. Received %s, Expected %s.",
            ltype_name(arg_type), expected_type);
    }

    lval* params = lval_pop(args, 0);
    lval* body = lval_pop(args, 0);
    lval_del(args); // wish i had a garbage collector
    return lval_lambda(params, body);
}

void lenv_add_builtins(lenv* env) {
    /* List Functions */
    lenv_add_builtin(env, "list", builtin_list);
    lenv_add_builtin(env, "head", builtin_head);
    lenv_add_builtin(env, "tail", builtin_tail);
    lenv_add_builtin(env, "eval", builtin_eval);
    lenv_add_builtin(env, "join", builtin_join);

    /* Mathematical Functions */
    lenv_add_builtin(env, "+", builtin_add);
    lenv_add_builtin(env, "-", builtin_sub);
    lenv_add_builtin(env, "*", builtin_mul);
    lenv_add_builtin(env, "/", builtin_div);
    lenv_add_builtin(env, "%", builtin_mod);

    /* Variable Functions */
    lenv_add_builtin(env, "def", builtin_def);
    lenv_add_builtin(env, "=", builtin_put);
    lenv_add_builtin(env, "\\", builtin_lambda);
}

/* Evaluation
 * so the expression (+ 2 2) passed through eval
 * (+ 2 2) = lval sexpr [lval sym, lval num, lval num]
 * 
*/
lval* lval_eval_sexpr(lenv* env, lval* v) {
    // evaluate children if there are any
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(env, v->cell[i]);
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
    lval* func = lval_pop(v, 0);
    if (func->type != LVAL_FUNC) {
        lval* err = lval_err("S-Expression starts with incorrect type. Got %s, Expected %s.",
            ltype_name(func->type), ltype_name(LVAL_FUNC));
        lval_del(func);
        lval_del(v);
        return err;
    }
    // call builtin with operator
    lval* result = lval_call(env, func, v);
    lval_del(func);
    return result;
}

/* lval_eval is passed the root or first lval from the reader, the
 * reader having converted and ast to a lval list. lval type is almost
 * guarenteed to be an sexpr unless its an expr or invalid.
*/
lval* lval_eval(lenv* env, lval* value) {
    if (value->type == LVAL_SYM) {
        lval* x = lenv_get(env, value);
        lval_del(value);
        return x;
    }
    // evaluate S-expressions
    if (value->type == LVAL_SEXPR) { return lval_eval_sexpr(env, value); }
    return value;
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
        symbol  : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;                                                \
        sexpr   : '(' <expr>* ')' ;                                                                 \
        qexpr   : '{' <expr>* '}' ;                                                                 \
        expr    : <number> | <symbol> | <sexpr> | <qexpr>;                                          \
        program : /^/ <expr>* /$/ ;                                                                 \
    ", Number, Symbol, Sexpr, Qexpr, Expr, Program);

    /* start interactive prompt */
    puts("HyperLambda lisp Version 0.0.4");
    puts("Press Ctrl+C to Exit\n");

    lenv* env = lenv_new();
    lenv_add_builtins(env);

    while(1) {
        char* input = readline("λ> ");
        add_history(input);

        mpc_result_t parse_result;
        if (mpc_parse("<stdin>", input, Program, &parse_result)) {
            lval* eval_result = lval_eval(env, lval_read(parse_result.output));
            lval_println(eval_result);
            lval_del(eval_result);
            mpc_ast_delete(parse_result.output);
        } else {
            mpc_err_print(parse_result.error);
            mpc_err_delete(parse_result.error);
        }
        free(input);
    }
    /* delete environment */
    lenv_del(env);
    /* delete parsers */
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Program);
    return 0;
}