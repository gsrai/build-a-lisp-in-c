# buildyourownlisp notes #

Hacking is the philosophy of exploration, personal expression, pushing boundaries, and breaking the rules. It stands against hierarchy and bureaucracy. It celebrates the individual. Hacking baits you with fun, learning, and glory. Hacking is the promise that with a computer and access to the internet, you have the agency to change the world.

## languages ##

In languages a statement can itself contain a new statement, for example an if
statement contains a list of new statements, but each of these new statements
can itself be another if statement.

These repeated structures and replacements are called *rewrite rules* as they
tell you how one thing can be rewritten as something else.

In a language there are an infinite number of things that can be said, however
it is possible to process and understand every possibility with a finite set of re-write rules.

A Set of re-write rules is called a grammar.
A computer needs a formal description of these re-write rules (grammar) for it to understand the
rules of the language. It will then build a structured internal representation of the language,
which will allow the computer to easily understand, evaluate and process it.

To build an internal representation of a language you need a parser. A parser combinator library
can make your life easier as it lets you build parsers and describe grammar simply. It is called
a parser combinator as you define multiple parsers and combine them to build a formal grammar.
With a parser combinator you can describe grammar with code or with natural language.

example describing a parser for decimals (natural grammar):

```c
mpc_parser_t* Digit = mpc_new("digit");
mpc_parser_t* DecimalPoint = mpc_new("dp");
mpc_parser_t* Decimal = mpc_new("decimal");

mpca_lang(MPCA_LANG_DEFAULT,
"
    digit   : '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9';  \
    dp      : '.';                                                        \
    decimal : <digit>+ <dp> <digit>+;                                     \
",
Digit, DecimalPoint, Decimal); // + means one or more

mpc_cleanup(3, Digit, DecimalPoint, Decimal);
```

### Parsing ###

Polish notation represents a mathematical subset of lisp, it is a notation for arithmetic where the operator
comes before the operands. E.g. `+ 1 2 3` is the same as `1 + 2 + 3`.

We can describe a formal grammar for polish notation like so:
Program => an operator followed by one or more expressions.
Expression => either a number or parentheses which have an operator followed by one or more expressions.
Operator => + - * /
Number => one or more characters with an optional - to represent negative numbers. This can be encoded by the regex rule `/-?[0-9]+/`.

In order to use the grammar to build a structure representation of the language (an AST (abstract sytax tree)) we need to use a parser.
A Parser will take in a formal grammar specification and some input, and will return a computable representation of the input in the
form of an abstract syntax tree.

floating point regex: `number  : /-?[0-9]*\\.?[0-9]+/ ;`?

### Evaluation ###

With the parser we have an internal structure now, but we have yet to evaluate it. The evaluation step
actually performs the computations encoded by the language.

The internal structure is called an abstract syntax tree, where the leaves are numbers and operators,
and the branches are the rules used to produce the tree which encodes information of how to traverse and evaluate it.

The AST is represented by the struct `mpc_ast_t` (part of the micro parser combinators library). The fields it contains are:
`tag` => a string representing a list of all the rules used to parse an item.
`contents` => a string of the content (leaf), it will be an operator or number in our case.
`state` => struct for storing the state of the parser.
`children_num` => a count for the pointer to the children of that node (it is a tree after all).
`children` => an array of pointers to struct of another ast. (** is either a multi dimentional array or an array of refs which is the same thing.)

The benefit of a tree structure is that it is a recursive definition, a tree has children that are also trees. We can use this
property of trees to traverse them generically (work on any tree) using recursive functions.

#### Errors ####

So far the interpreter will output any syntax errors caught by the parser, but what about eval errors, like divide by zero.
One method of catch errors is to define a type returned by eval (lisp val or lval) which can be an error or a number.
Using enums and switch statements we can add error handling logic as part of eval rather than a second thought.

### S-Expressions ###

In lisps, data and code use the same structures, symbolic expressions or s-expressions for short. An s-expression
is a list built up recursively of numbers, symbols and other lists. It could also be described as a variable length
list of other values.

#### The stack and the heap ####

It is important to understand the memory model of programming languages seeing as we are developing one. Memory in
programming languages is usually split between the stack and the heap, usually a stack has a fixed size (hence stack overflow)
and there can be many stacks but only one heap.

The stack is where the program lives, all teh temporary variables and data structures. Every time you call a function a new
area of the stack is allocated and local variables (and arguements) are copied into this area, as well as meta data. Once the
function completes its execution it is unallocated.

The heap is a section of memory for long lived objects, memory here has to be manually allocated and deallocated (requested from
the operating system using syscalls).

In conclusion, the stack and the heap are sections of memory with individual purposes, the stack is fixed size, whereas the heap
is only limited by the OS or real memory capacity.

We need to modify the parser grammar to accept a Symbol and an Sexpression. A Symbol is just an operator like `add`,
an Sexpression is any expression (number or symbol or sexpression) between paretheses.

#### Read and Eval and the REPL (Read Eval Print Loop) ###

Adding sexpressions means we need to read and construct a list of lisp values (lvals) from the ast in order to evaluate it.
So the first step is to convert the AST to an sexpression, and the second step is to evaluate it using normal lisp rules.

An sexpression in lisp is usually implemented as a linked list. It can be an atom (such as a symbol of number) or two other
sexpressions cons(joined) together (cons is an operation, short for construct memory object, it is a fundamental function in lisps).
An sexpression in the list is know as a cell (or a cons cell).

### Q-Expressions ###

There is a common pattern when adding new features to a language, it involves the following steps:
Syntax => Add a new rule to the language grammar.
Representation => Add a new data type variation to represent the feature.
Parsing => Add new functions for reading the feature from the ast.
Semantics => Add a new function for evaluating and manipulating the feature.

Using this pattern we can add a new feature, Quoted Expressions. A Q-expression will be a new type of lisp value.
It is not meant to be evaluated by lisp, so when encountered by the eval function it is left as it is. They are useful
for storing and manipulating numbers, symbols and other sexpressions. In actual lisp implementations they use macros
like `'` to prevent evaluation.

Q-expressions here will be a list type, we will need to define some inbuilt operations like:
List => constructs a q-expr
Head => returns a q-expr of the first element
Tail => returns a q-expr of the rest of the list
join => join the two lists into one
eval => eval the q-expression

### Variables ###

In order to make the language practical we need a way to name values, using variables. In order to have variables
in our language we need to create a structure which stores name value associations of everything in our program,
we can call this the environment.

A valid variable name is represented by the following regex:
Deus ex Regex: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/
Basically anything within the range specifier `[]` makes special characters lose meaning.
A valid variable can be any upper lower alphanumeric string, with underscore and the arithmetic and equality symbols

#### Environment ####

The environment is a structure that holds name value associations for our program. To hold names and values the struct
will have two lists: one for lisp values `lval` and one for the symbol itself `the variable name`.
(I am not sure why we would not use a sorted structure (tree) or a hashmap as these would have quicker symbol lookup,
the arrays are not ordered, only by order of insertions)

#### defn ####

We have used the environment to register the built in functions to variables in the environment. We need a way to all the
user to define its own functions.

## differences from other lisps ##

Q-expressions are implemented as macros
Other lisps use cons and linked list data structure over a dynamic array.