#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>

#ifdef DEBUG
#define dprintf(...) fprintf(__VA_ARGS__)
#else
#define dprintf(...) ((void)0)
#endif
#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

void handle_SIGINT(int sig){}
char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
pid_t spawnpid = -5;
int child_status;
pid_t child_pid;
pid_t bg_pid = 0;
int exit_status = 0;
int child_signal = 0;


int main(int argc, char *argv[])
{
  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;
  char *args[MAX_WORDS] = {NULL};
  char *fin;
  char *fout;
  char *foutapp;
  bool is_bg = false; 
  bool curr_is_bg = false;
  bool out_prec = false;
  int end_of_args;
  struct sigaction ignore_action = {0}, old_SIGINT_action = {0}, old_SIGTSTP_action = {0}, custom_SIGINT_action = {0};
  ignore_action.sa_handler = SIG_IGN;
  custom_SIGINT_action.sa_handler = &handle_SIGINT; 
  if (input == stdin){
      sigaction(SIGINT, &ignore_action, &old_SIGINT_action);
      sigaction(SIGTSTP, &ignore_action, &old_SIGTSTP_action);
    }
loop_start:
  for (;;) {
//prompt:;
    /* TODO: Manage background processes */

    /* TODO: prompt */

    char *fin = 0;
    char *fout = 0;
    char *foutapp = 0;
    curr_is_bg = false;

    if (input == stdin){
    sigaction(SIGINT, &custom_SIGINT_action, NULL); 
    }
    print_prompt: 
    if (is_bg){
        child_pid = waitpid(0, &child_status, WNOHANG | WUNTRACED);
        if (child_pid != 0){
            if (child_pid == -1){
                perror("Error in determining status of child!");
            }
            else {
                if (WIFSTOPPED(child_status)) {
                        kill(child_pid, SIGCONT);
                        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) child_pid);
                    }
                else if (WIFEXITED(child_status)){
                    exit_status = WEXITSTATUS(child_status);
                    fprintf(stderr, "Child process %jd done. Exit status %d.\n",  (intmax_t) child_pid, exit_status);
                    exit_status = 0;
                    is_bg = false;

                }
                else {
                    child_signal = WTERMSIG(child_status);
                    fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) child_pid, child_signal);
                    child_signal = 0;
                    is_bg = false;
                }
            }
        }
    }
    if (input == stdin) {
        char* prompt = getenv("PS1");
        if (prompt == NULL){
            if (getuid() == 0){
                prompt = "# ";
            }
            else {
                prompt = "$ ";
            }
        }
    fprintf(stderr, "%s", prompt); 
    }
    errno = 0;
    ssize_t line_len = getline(&line, &n, input);
    if (feof(input)){
        fclose(input);
        exit(0);
    }
    else if (errno == EINTR && line_len == -1){
        fprintf(stderr, "\n");
        clearerr(input);
        goto print_prompt;
        }
    else if (line_len == -1){
        fclose(input);
        err(1, "%s", input_fn);
    }
    if (input == stdin){
        sigaction(SIGINT, &ignore_action, NULL);
    }
    if (line_len == 1) goto loop_start; 

    size_t nwords = wordsplit(line);
    for (size_t i = 0; i < nwords; ++i) {
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
    }
    
    // Now process args, redirections, bg operator
    int i = 0;
    while (i < nwords - 1 && strcmp(words[i],">") != 0 && strcmp(words[i],"<") != 0 && strcmp(words[i],">>") != 0) {
        args[i] = words[i];
        //dprintf(stderr, "args[i]: %s\n", args[i]);
        i++;
    }
    // figure out how to handle this later
    if (i == nwords - 1 && strcmp(words[i], "&") != 0){
        args[i] = words[i];
    }
    while (i < nwords - 1){
        int fd;
        if (strcmp(words[i], "<") == 0) fin = words[i+1];
        else if (strcmp(words[i],">") == 0){
            fout = words[i+1];
            fd = open(fout, O_TRUNC | O_CREAT, 0777);
            close(fd);
            out_prec = true;
        } else if (strcmp(words[i],">>") == 0){
            foutapp = words[i+1];
            fd = open(foutapp, O_APPEND | O_CREAT, 0777);
            close(fd);
            out_prec = false;
        } else dprintf(stderr, "Invalid redirect syntax!");
        i++; 
    }
    //dprintf(stderr, "fin: %s, fout: %s, foutapp: %s\n", fin, fout, foutapp);
    if (strcmp(words[nwords-1], "&") == 0){
        is_bg = true;
        curr_is_bg = true;
    }
    
    if (strcmp(words[0], "cd") != 0 && strcmp(words[0], "exit") != 0){
        spawnpid = fork();
        switch (spawnpid){
            case -1:
                perror("fork() failed!");
                exit(1);
                break;
            case 0:
                sigaction(SIGINT, &old_SIGINT_action, NULL);
                sigaction(SIGTSTP, &old_SIGTSTP_action, NULL);
                dprintf(stderr, "Child process starts here...\n");

                if (fin){
                    close(0);
                    open(fin, O_RDONLY);
                }
                if (fout || foutapp){
                    close(1);
                    if (out_prec){
                        open(fout, O_CREAT|O_WRONLY, 0777);
                    } else {
                        open(foutapp, O_CREAT|O_APPEND|O_WRONLY, 0777);    
                    }
                }
                execvp(args[0], args);
                perror("Child process error!");
                exit(1);
            default:
                if (!curr_is_bg) {
                    child_pid = waitpid(spawnpid, &child_status, WUNTRACED);
                    // Not sure yet about decisions for these exit_status assignments
                    if (WIFSTOPPED(child_status)) {
                        kill(child_pid, SIGCONT);
                        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) child_pid);
                        bg_pid = child_pid;
                    }
                    else if (WIFEXITED(child_status)) {
                        exit_status = WEXITSTATUS(child_status);


                    }
                    else{
                        exit_status = 128 + WTERMSIG(child_status);
                    }
                }
                else {
                    bg_pid = spawnpid;
                }
                break;
            }
    }
    else if (strcmp(words[0], "exit") == 0){
        if (nwords > 1){
            exit_status = atoi(words[1]);
        }
        exit(exit_status);
    } 
    else if (strcmp(words[0], "cd") == 0){
        if (nwords > 1){
            chdir(words[1]);
        } else{
            chdir(getenv("HOME"));
        }
    }

    for (size_t i = 0; i < MAX_WORDS; i++) {
        free(words[i]);
        words[i] = NULL;
        args[i] = NULL;
    }
  }
}

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;
  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!'){
        char *str_bg_pid[10];
        if (bg_pid == 0){
            char *empty = "";
            build_str(empty, NULL);
        }
        else {
            sprintf(str_bg_pid, "%jd", (intmax_t) bg_pid);
            build_str(str_bg_pid, NULL);
        }
    }
    else if (c == '$') { 
        pid_t smallsh_id = getpid();
        char str_pid[10] = {NULL};
        sprintf(str_pid, "%jd", (intmax_t) smallsh_id);
        build_str(str_pid, NULL);
    }
    else if (c == '?') {
      char *str_exit_status[10] = {NULL};
      sprintf(str_exit_status, "%d", exit_status);
      build_str(str_exit_status, NULL);
    }
    else if (c == '{') {
      size_t n = end - start - 2;
       
      char *copy = malloc(n);
      strncpy(copy, start + 2, n - 1);
      copy[n - 1] = '\0';
       
      char *parameter = getenv(copy);
      if (parameter == NULL){
            char *emp = "";
            build_str(emp, NULL);
            free(copy);
            goto cont;
      }
      build_str(parameter, NULL);
      free(copy);
    }
cont:
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
 
