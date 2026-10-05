// sh.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#define SH_MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_ALIASES 32
#define MAX_VARS 128
#define MAX_TOKENS 256
#define MAX_FUNCS 32

typedef enum {
    TOK_WORD, TOK_IF, TOK_THEN, TOK_ELIF, TOK_ELSE, TOK_FI,
    TOK_CASE, TOK_IN, TOK_ESAC, TOK_FOR, TOK_WHILE, TOK_DO, TOK_DONE,
    TOK_UNTIL, TOK_FUNCTION, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACK, TOK_RBRACK, TOK_AND, TOK_OR, TOK_SEMI, TOK_SEMISEMI, TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

typedef struct {
    char *name;
    char *body;
} Function;

struct { char *name; char *value; } vars[MAX_VARS];
struct { char *name; char *command; } aliases[MAX_ALIASES];
Function funcs[MAX_FUNCS];
int var_count = 0;
int alias_count = 0;
int func_count = 0;
Token *tokens;
int token_pos = 0;

char *get_var(const char *name);
void parse_and_execute(char *input);

char *strdup_safe(const char *s) {
    if (!s) return NULL;
    char *dup = malloc(strlen(s) + 1);
    if (dup) strcpy(dup, s);
    return dup;
}

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

char *expand_var(char *word) {
    if (!word || word[0] != '$') return strdup_safe(word);
    char *var_name = word + 1;
    char *value = get_var(var_name);
    return strdup_safe(value ? value : "");
}

void set_var(const char *name, const char *value) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            free(vars[i].value);
            vars[i].value = strdup_safe(value);
            return;
        }
    }
    if (var_count < MAX_VARS) {
        vars[var_count].name = strdup_safe(name);
        vars[var_count].value = strdup_safe(value);
        var_count++;
    }
}

char *get_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return vars[i].value;
    }
    return getenv(name);
}

void set_function(const char *name, const char *body) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(funcs[i].name, name) == 0) {
            free(funcs[i].body);
            funcs[i].body = strdup_safe(body);
            return;
        }
    }
    if (func_count < MAX_FUNCS) {
        funcs[func_count].name = strdup_safe(name);
        funcs[func_count].body = strdup_safe(body);
        func_count++;
    }
}

char *get_function(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(funcs[i].name, name) == 0) return funcs[i].body;
    }
    return NULL;
}

int execute(char **args) {
    if (!args[0]) return 0;

    char *func = get_function(args[0]);
    if (func) {
        parse_and_execute(func);
        return 0;
    }
    if (strcmp(args[0], "cd") == 0) {
        if (!args[1]) args[1] = get_var("HOME");
        if (chdir(args[1]) != 0) perror("cd");
        return 0;
    }
    if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; args[i]; i++) {
            char *expanded = expand_var(args[i]);
            printf("%s%s", expanded, args[i + 1] ? " " : "");
            free(expanded);
        }
        printf("\n");
        return 0;
    }
    if (strcmp(args[0], "exit") == 0) {
        exit(args[1] ? atoi(args[1]) : 0);
    }
    if (strcmp(args[0], "pwd") == 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
        else perror("pwd");
        return 0;
    }
    if (strcmp(args[0], "set") == 0) {
        if (args[1]) {
            char *eq = strchr(args[1], '=');
            if (eq) {
                *eq = 0;
                set_var(args[1], eq + 1);
            }
        }
        return 0;
    }
    if (strcmp(args[0], "test") == 0 || strcmp(args[0], "[") == 0) {
        if (!args[1] || !args[2]) return 1;
        char *arg1 = expand_var(args[2]);
        if (strcmp(args[1], "-z") == 0) {
            int result = strlen(arg1) == 0 ? 0 : 1;
            free(arg1);
            return result;
        }
        if (strcmp(args[1], "=") == 0) {
            char *arg2 = expand_var(args[3]);
            int result = strcmp(arg1, arg2) == 0 ? 0 : 1;
            free(arg1);
            free(arg2);
            return result;
        }
        if (strcmp(args[1], "!=") == 0) {
            char *arg2 = expand_var(args[3]);
            int result = strcmp(arg1, arg2) != 0 ? 0 : 1;
            free(arg1);
            free(arg2);
            return result;
        }
        free(arg1);
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        error("execvp");
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    } else {
        error("fork");
    }
    return 1;
}

Token *tokenize(char *input) {
    Token *toks = calloc(MAX_TOKENS, sizeof(Token));
    int i = 0;
    char *p = input;

    while (*p && i < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;

        if (strncmp(p, "if", 2) == 0 && !isalnum(p[2])) { toks[i].type = TOK_IF; p += 2; }
        else if (strncmp(p, "then", 4) == 0 && !isalnum(p[4])) { toks[i].type = TOK_THEN; p += 4; }
        else if (strncmp(p, "elif", 4) == 0 && !isalnum(p[4])) { toks[i].type = TOK_ELIF; p += 4; }
        else if (strncmp(p, "else", 4) == 0 && !isalnum(p[4])) { toks[i].type = TOK_ELSE; p += 4; }
        else if (strncmp(p, "fi", 2) == 0 && !isalnum(p[2])) { toks[i].type = TOK_FI; p += 2; }
        else if (strncmp(p, "case", 4) == 0 && !isalnum(p[4])) { toks[i].type = TOK_CASE; p += 4; }
        else if (strncmp(p, "in", 2) == 0 && !isalnum(p[2])) { toks[i].type = TOK_IN; p += 2; }
        else if (strncmp(p, "esac", 4) == 0 && !isalnum(p[4])) { toks[i].type = TOK_ESAC; p += 4; }
        else if (strncmp(p, "for", 3) == 0 && !isalnum(p[3])) { toks[i].type = TOK_FOR; p += 3; }
        else if (strncmp(p, "while", 5) == 0 && !isalnum(p[5])) { toks[i].type = TOK_WHILE; p += 5; }
        else if (strncmp(p, "do", 2) == 0 && !isalnum(p[2])) { toks[i].type = TOK_DO; p += 2; }
        else if (strncmp(p, "done", 4) == 0 && !isalnum(p[4])) { toks[i].type = TOK_DONE; p += 4; }
        else if (strncmp(p, "function", 8) == 0 && !isalnum(p[8])) { toks[i].type = TOK_FUNCTION; p += 8; }
        else if (*p == '(') { toks[i].type = TOK_LPAREN; p++; }
        else if (*p == ')') { toks[i].type = TOK_RPAREN; p++; }
        else if (*p == '{') { toks[i].type = TOK_LBRACE; p++; }
        else if (*p == '}') { toks[i].type = TOK_RBRACE; p++; }
        else if (*p == '[') { toks[i].type = TOK_LBRACK; p++; }
        else if (*p == ']') { toks[i].type = TOK_RBRACK; p++; }
        else if (strncmp(p, "&&", 2) == 0) { toks[i].type = TOK_AND; p += 2; }
        else if (strncmp(p, "||", 2) == 0) { toks[i].type = TOK_OR; p += 2; }
        else if (strncmp(p, ";;", 2) == 0) { toks[i].type = TOK_SEMISEMI; p += 2; }
        else if (*p == ';') { toks[i].type = TOK_SEMI; p++; }
        else {
            toks[i].type = TOK_WORD;
            char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != ';' && *p != '&' && *p != '|' && *p != ')') p++;
            toks[i].value = strndup(start, p - start);
        }
        i++;
    }
    toks[i].type = TOK_EOF;
    return toks;
}

Token *next_token() {
    return &tokens[token_pos++];
}

void expect(TokenType type) {
    Token *t = next_token();
    if (t->type != type) {
        fprintf(stderr, "Syntax error: expected %d, got %d\n", type, t->type);
        exit(1);
    }
}

int parse_command() {
    Token *t = next_token();
    if (t->type != TOK_WORD) {
        token_pos--;
        return 1;
    }
    char *cmd = expand_var(t->value);
    char *args[MAX_ARGS] = {cmd, NULL};
    int argc = 1;
    while ((t = next_token())->type == TOK_WORD && argc < MAX_ARGS - 1) {
        args[argc++] = expand_var(t->value);
    }
    args[argc] = NULL;
    token_pos--;
    int result = execute(args);
    free(cmd);
    for (int i = 1; i < argc; i++) free(args[i]); /* Free expanded args */
    return result;
}

int parse_case() {
    expect(TOK_CASE);
    Token *t = next_token();
    if (t->type != TOK_WORD) { fprintf(stderr, "Syntax error: case expects word\n"); exit(1); }
    char *value = expand_var(t->value);
    expect(TOK_IN);

    int result = 0;
    int matched = 0;
    while ((t = next_token())->type != TOK_ESAC) {
        if (t->type != TOK_WORD) { fprintf(stderr, "Syntax error: case pattern expected\n"); exit(1); }
        char *pattern = t->value;
        expect(TOK_RPAREN);

        while ((t = next_token())->type != TOK_SEMI && t->type != TOK_SEMISEMI && t->type != TOK_ESAC) {
            token_pos--;
            if (!matched && strcmp(value, pattern) == 0) {
                result = parse_command();
                matched = 1;
            } else {
                parse_command();
            }
        }
        if (matched) {
            while ((t = next_token())->type != TOK_ESAC && t->type != TOK_EOF); /* Skip to esac */
            break;
        }
    }
    free(value);
    return result;
}

int parse_for() {
    expect(TOK_FOR);
    Token *t = next_token();
    if (t->type != TOK_WORD) { fprintf(stderr, "Syntax error: for expects variable\n"); exit(1); }
    char *var = t->value;
    expect(TOK_IN);

    char *words[MAX_ARGS] = {0};
    int word_count = 0;
    while ((t = next_token())->type == TOK_WORD && word_count < MAX_ARGS - 1) {
        words[word_count++] = t->value;
    }
    expect(TOK_DO);

    int result = 0;
    for (int i = 0; i < word_count; i++) {
        set_var(var, words[i]);
        while ((t = next_token())->type != TOK_DONE && t->type != TOK_EOF) {
            token_pos--;
            result = parse_command();
            t = next_token();
            if (t->type == TOK_SEMI || t->type == TOK_SEMISEMI) continue;
            token_pos--;
        }
    }
    if (t->type != TOK_DONE) expect(TOK_DONE);
    return result;
}

int parse_function() {
    expect(TOK_FUNCTION);
    Token *t = next_token();
    if (t->type != TOK_WORD) { fprintf(stderr, "Syntax error: function expects name\n"); exit(1); }
    char *name = t->value;
    expect(TOK_LBRACE);

    char body[SH_MAX_INPUT] = {0};
    int len = 0;
    while ((t = next_token())->type != TOK_RBRACE && t->type != TOK_EOF) {
        if (t->type == TOK_WORD) {
            len += snprintf(body + len, SH_MAX_INPUT - len, "%s ", t->value);
        } else if (t->type == TOK_SEMI || t->type == TOK_SEMISEMI) {
            len += snprintf(body + len, SH_MAX_INPUT - len, "; ");
        }
    }
    set_function(name, body);
    return 0;
}

int parse_statement() {
    Token *t = next_token();
    if (t->type == TOK_EOF) return 0;
    switch (t->type) {
        case TOK_CASE: token_pos--; return parse_case();
        case TOK_FOR: token_pos--; return parse_for();
        case TOK_FUNCTION: token_pos--; return parse_function();
        case TOK_WORD: token_pos--; return parse_command();
        default: fprintf(stderr, "Syntax error: unexpected token %d\n", t->type); exit(1);
    }
}

void parse_and_execute(char *input) {
    tokens = tokenize(input);
    token_pos = 0;
    Token *t;
    while ((t = next_token())->type != TOK_EOF) {
        token_pos--;
        parse_statement();
        t = next_token();
        if (t->type == TOK_SEMI || t->type == TOK_SEMISEMI) continue;
        token_pos--;
    }
    free(tokens);
}

int main() {
    char input[SH_MAX_INPUT];
    char *prompt = "$ ";
    while (1) {
        printf("%s", prompt);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;
        parse_and_execute(input);
        prompt = "$ ";
    }
    return 0;
}
