#include <stdint.h>
#include <stddef.h>

extern int printf(const char *fmt, ...);
extern int puts(const char *s);
extern void exit(int status);
extern int fork(void);
extern int execve(const char *path, char *const argv[], char *const envp[]);
extern int waitpid(int pid, int *status, int options);
extern char *gets(char *buf, int size);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, unsigned long n);
extern unsigned long strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern void *memset(void *s, int c, unsigned long n);
extern int64_t read(int fd, void *buf, unsigned long count);
extern int64_t write(int fd, const void *buf, unsigned long count);
extern int uname(void *buf);
extern int getuid(void);
extern int getgid(void);
extern int open(const char *path, int flags);
extern int close(int fd);
extern int readdir(int fd, void *dent);
extern int pipe(int fds[2]);
extern int dup2(int oldfd, int newfd);

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

#define MAX_LINE 256
#define MAX_TOKENS 64
#define MAX_ARGS 16
#define NAME_MAX 64

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

struct dirent {
    uint32_t ino;
    uint32_t type;
    char name[NAME_MAX];
};

#define REDIR_NONE   0
#define REDIR_INPUT  1
#define REDIR_OUTPUT 2
#define REDIR_APPEND 3

struct command {
    char *argv[MAX_ARGS];
    int argc;
    int in_type;
    char *in_file;
    int out_type;
    char *out_file;
};

#define MAX_CMDS 8
struct pipeline {
    struct command cmds[MAX_CMDS];
    int ncmds;
};

static int is_meta(char c) {
    return c == '|' || c == '>' || c == '<';
}

static int tokenize(char *line, char *tokens[]) {
    int count = 0;
    char *p = line;

    while (*p && count < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        tokens[count++] = p;

        if (p[0] == '>' && p[1] == '>') {
            p += 2;
        } else if (is_meta(*p)) {
            p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && !is_meta(*p)) {
                if (p[0] == '>' && p[1] == '>') break;
                p++;
            }
        }

        if (*p && (*p == ' ' || *p == '\t')) {
            *p++ = '\0';
        }
    }

    tokens[count] = 0;
    return count;
}

static int parse_pipeline(char *tokens[], int ntokens, struct pipeline *pl) {
    memset(pl, 0, sizeof(*pl));
    if (ntokens == 0) return 0;

    int cmd_idx = 0;
    int arg_idx = 0;
    struct command *cmd = &pl->cmds[0];

    for (int i = 0; i < ntokens; i++) {
        char *tok = tokens[i];

        if (strcmp(tok, "|") == 0) {
            if (arg_idx == 0) {
                printf("sh: syntax error near '|'\n");
                return -1;
            }
            cmd->argv[arg_idx] = 0;
            cmd->argc = arg_idx;
            cmd_idx++;
            if (cmd_idx >= MAX_CMDS) {
                printf("sh: too many commands in pipeline\n");
                return -1;
            }
            cmd = &pl->cmds[cmd_idx];
            arg_idx = 0;
        } else if (strcmp(tok, "<") == 0) {
            if (i + 1 >= ntokens || !tokens[i + 1] || is_meta(tokens[i + 1][0])) {
                printf("sh: syntax error near '<'\n");
                return -1;
            }
            cmd->in_type = REDIR_INPUT;
            cmd->in_file = tokens[++i];
        } else if (strcmp(tok, ">>") == 0) {
            if (i + 1 >= ntokens || !tokens[i + 1] || is_meta(tokens[i + 1][0])) {
                printf("sh: syntax error near '>>'\n");
                return -1;
            }
            cmd->out_type = REDIR_APPEND;
            cmd->out_file = tokens[++i];
        } else if (strcmp(tok, ">") == 0) {
            if (i + 1 >= ntokens || !tokens[i + 1] || is_meta(tokens[i + 1][0])) {
                printf("sh: syntax error near '>'\n");
                return -1;
            }
            cmd->out_type = REDIR_OUTPUT;
            cmd->out_file = tokens[++i];
        } else {
            if (arg_idx >= MAX_ARGS - 1) {
                printf("sh: too many arguments\n");
                return -1;
            }
            cmd->argv[arg_idx++] = tok;
        }
    }

    if (arg_idx == 0) {
        if (cmd_idx > 0) {
            printf("sh: syntax error at end of pipeline\n");
            return -1;
        }
        return 0;
    }

    cmd->argv[arg_idx] = 0;
    cmd->argc = arg_idx;
    pl->ncmds = cmd_idx + 1;
    return 0;
}

static int builtin_exit(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    exit(0);
    return 0;
}

static int builtin_help(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    puts("iruel shell");
    puts("commands:");
    puts("  exit    - exit the shell");
    puts("  help    - show this help");
    puts("");
    puts("  uname   - print system information");
    puts("  id      - print user/group ids");
    puts("  ls      - list directory entries");
    puts("  cat     - print file contents");
    puts("  echo    - print arguments");
    puts("  pwd     - print working directory");
    return 0;
}

static int builtin_uname(int argc, char *argv[]) {
    struct utsname uts;
    if (uname(&uts) < 0) {
        puts("uname: system call failed");
        return 1;
    }

    int all = 0;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        all = 1;
    }

    if (all) {
        printf("%s %s %s %s %s\n",
               uts.sysname, uts.nodename, uts.release, uts.version, uts.machine);
    } else {
        puts(uts.sysname);
    }

    return 0;
}

static int builtin_id(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    int uid = getuid();
    int gid = getgid();

    const char *username = uid == 0 ? "root" : "user";
    const char *groupname = gid == 0 ? "root" : "users";

    printf("uid=%d(%s) gid=%d(%s)\n", uid, username, gid, groupname);
    return 0;
}

static int builtin_ls(int argc, char *argv[]) {
    const char *path = "/";
    if (argc > 1) {
        path = argv[1];
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot open %s\n", path);
        return 1;
    }

    struct dirent dent;
    while (readdir(fd, &dent) > 0) {
        printf("%s\n", dent.name);
    }

    close(fd);
    return 0;
}

static int builtin_cat(int argc, char *argv[]) {
    char buf[256];

    if (argc == 1) {
        for (;;) {
            int64_t n = read(0, buf, sizeof(buf));
            if (n <= 0) break;
            write(1, buf, (unsigned long)n);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: cannot open %s\n", argv[i]);
            continue;
        }
        for (;;) {
            int64_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            write(1, buf, (unsigned long)n);
        }
        close(fd);
    }

    return 0;
}

static int builtin_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            write(1, " ", 1);
        }
        write(1, argv[i], strlen(argv[i]));
    }
    write(1, "\n", 1);
    return 0;
}

static int builtin_pwd(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    write(1, "/\n", 2);
    return 0;
}

static int is_builtin(const char *name) {
    if (strcmp(name, "exit") == 0) return 1;
    if (strcmp(name, "help") == 0) return 1;
    if (strcmp(name, "uname") == 0) return 1;
    if (strcmp(name, "id") == 0) return 1;
    if (strcmp(name, "ls") == 0) return 1;
    if (strcmp(name, "cat") == 0) return 1;
    if (strcmp(name, "echo") == 0) return 1;
    if (strcmp(name, "pwd") == 0) return 1;
    return 0;
}

static int run_builtin(char *argv[], int argc) {
    if (strcmp(argv[0], "exit") == 0) return builtin_exit(argc, argv);
    if (strcmp(argv[0], "help") == 0) return builtin_help(argc, argv);
    if (strcmp(argv[0], "uname") == 0) return builtin_uname(argc, argv);
    if (strcmp(argv[0], "id") == 0) return builtin_id(argc, argv);
    if (strcmp(argv[0], "ls") == 0) return builtin_ls(argc, argv);
    if (strcmp(argv[0], "cat") == 0) return builtin_cat(argc, argv);
    if (strcmp(argv[0], "echo") == 0) return builtin_echo(argc, argv);
    if (strcmp(argv[0], "pwd") == 0) return builtin_pwd(argc, argv);
    return 1;
}

static void build_path(const char *cmd, char *path) {
    if (cmd[0] == '/') {
        strcpy(path, cmd);
    } else {
        strcpy(path, "/bin/");
        strcpy(path + 5, cmd);
    }
}

static int setup_redir(struct command *cmd) {
    if (cmd->in_type == REDIR_INPUT) {
        int fd = open(cmd->in_file, O_RDONLY);
        if (fd < 0) {
            printf("sh: cannot open %s\n", cmd->in_file);
            return -1;
        }
        dup2(fd, 0);
        close(fd);
    }
    if (cmd->out_type == REDIR_OUTPUT) {
        int fd = open(cmd->out_file, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            printf("sh: cannot create %s\n", cmd->out_file);
            return -1;
        }
        dup2(fd, 1);
        close(fd);
    } else if (cmd->out_type == REDIR_APPEND) {
        int fd = open(cmd->out_file, O_WRONLY | O_CREAT | O_APPEND);
        if (fd < 0) {
            printf("sh: cannot open %s\n", cmd->out_file);
            return -1;
        }
        dup2(fd, 1);
        close(fd);
    }
    return 0;
}

static int run_pipeline(struct pipeline *pl) {
    if (pl->ncmds == 0) return 0;

    if (pl->ncmds == 1) {
        struct command *cmd = &pl->cmds[0];

        if (is_builtin(cmd->argv[0]) &&
            cmd->in_type == REDIR_NONE && cmd->out_type == REDIR_NONE) {
            return run_builtin(cmd->argv, cmd->argc);
        }

        int pid = fork();
        if (pid < 0) {
            puts("sh: fork failed");
            return -1;
        }
        if (pid == 0) {
            if (cmd->in_type != REDIR_NONE || cmd->out_type != REDIR_NONE) {
                if (setup_redir(cmd) < 0) exit(1);
            }
            if (is_builtin(cmd->argv[0])) {
                int ret = run_builtin(cmd->argv, cmd->argc);
                exit(ret);
            }
            char path[MAX_LINE];
            build_path(cmd->argv[0], path);
            execve(path, cmd->argv, 0);
            printf("sh: %s: command not found\n", cmd->argv[0]);
            exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        return status;
    }

    int pids[MAX_CMDS];
    int prev_fd = -1;

    for (int i = 0; i < pl->ncmds; i++) {
        struct command *cmd = &pl->cmds[i];
        int pipefd[2] = {-1, -1};

        if (i < pl->ncmds - 1) {
            if (pipe(pipefd) < 0) {
                puts("sh: pipe failed");
                return -1;
            }
        }

        int pid = fork();
        if (pid < 0) {
            puts("sh: fork failed");
            return -1;
        }

        if (pid == 0) {
            if (prev_fd >= 0) {
                dup2(prev_fd, 0);
                close(prev_fd);
            }
            if (pipefd[1] >= 0) {
                dup2(pipefd[1], 1);
                close(pipefd[0]);
                close(pipefd[1]);
            }
            if (i == 0 && cmd->in_type != REDIR_NONE) {
                int fd = open(cmd->in_file, O_RDONLY);
                if (fd < 0) {
                    printf("sh: cannot open %s\n", cmd->in_file);
                    exit(1);
                }
                dup2(fd, 0);
                close(fd);
            }
            if (i == pl->ncmds - 1 && cmd->out_type != REDIR_NONE) {
                int flags = O_WRONLY | O_CREAT;
                flags |= (cmd->out_type == REDIR_APPEND) ? O_APPEND : O_TRUNC;
                int fd = open(cmd->out_file, flags);
                if (fd < 0) {
                    printf("sh: cannot open %s\n", cmd->out_file);
                    exit(1);
                }
                dup2(fd, 1);
                close(fd);
            }
            if (is_builtin(cmd->argv[0])) {
                int ret = run_builtin(cmd->argv, cmd->argc);
                exit(ret);
            }
            char path[MAX_LINE];
            build_path(cmd->argv[0], path);
            execve(path, cmd->argv, 0);
            printf("sh: %s: command not found\n", cmd->argv[0]);
            exit(127);
        }

        pids[i] = pid;
        if (prev_fd >= 0) close(prev_fd);
        if (pipefd[1] >= 0) close(pipefd[1]);
        prev_fd = pipefd[0];
    }

    int last_status = 0;
    for (int i = 0; i < pl->ncmds; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == pl->ncmds - 1) last_status = status;
    }
    return last_status;
}

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    char line[MAX_LINE];
    char *tokens[MAX_TOKENS];
    struct pipeline pl;

    puts("iruel shell v0.2");
    puts("type 'help' for available commands.\n");

    while (1) {
        write(1, "# ", 2);

        int64_t n = read(0, line, MAX_LINE - 1);
        if (n <= 0) break;
        if (n > 0 && line[n - 1] == '\n') n--;
        line[n] = '\0';

        int ntokens = tokenize(line, tokens);
        if (ntokens == 0) continue;

        if (parse_pipeline(tokens, ntokens, &pl) < 0) continue;
        if (pl.ncmds == 0) continue;

        run_pipeline(&pl);
    }

    puts("exit");
    exit(0);
    return 0;
}
