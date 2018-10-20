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

int main(int argc, char** argv) {
    puts("HyperLambda lisp Version 0.0.1");
    puts("Press Ctrl+C to Exit\n");

    while(1) {
        char* input = readline("λ> ");
        add_history(input);
        printf("reply: %s\n", input);
        free(input);
    }
    return 0;
}

/* build command: gcc prompt.c -ledit -o prompt */