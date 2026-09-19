#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100
#define MAX_PATH 2048
#define MAX_ENV_VARS 32
#define MAX_FILES 64
#define MAX_FILE_CONTENT 4096

typedef struct {
    char name[MAX_PATH];
    char content[MAX_FILE_CONTENT];
    int is_dir;
    time_t created;
} FileEntry;

static char cwd[MAX_PATH] = "/home/guest";
static const char *HOME_DIR = "/home/guest";
static char *history[MAX_HISTORY];
static int history_count = 0;
static FileEntry files[MAX_FILES];
static int file_count = 0;
static char output_buffer[65536];
static int output_pos = 0;

static void output_append(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int remaining = sizeof(output_buffer) - output_pos;
    if (remaining > 0) {
        int written = vsnprintf(output_buffer + output_pos, remaining, fmt, args);
        if (written > 0 && written < remaining)
            output_pos += written;
    }
    va_end(args);
}

static void init_fs(void) {
    files[file_count++] = (FileEntry){ .name = "/", .is_dir = 1, .created = time(NULL) };
    files[file_count++] = (FileEntry){ .name = "/home", .is_dir = 1, .created = time(NULL) };
    files[file_count++] = (FileEntry){ .name = "/tmp", .is_dir = 1, .created = time(NULL) };
    files[file_count++] = (FileEntry){ .name = "/etc", .is_dir = 1, .created = time(NULL) };

    files[file_count] = (FileEntry){ .name = "/etc/motd", .is_dir = 0, .created = time(NULL) };
    snprintf(files[file_count].content, MAX_FILE_CONTENT,
        "Welcome to WebTerm WASM Shell v1.0\n"
        "Type 'help' for a list of commands.\n");
    file_count++;

    files[file_count] = (FileEntry){ .name = "/etc/hostname", .is_dir = 0, .created = time(NULL) };
    snprintf(files[file_count].content, MAX_FILE_CONTENT, "webterm-wasm\n");
    file_count++;

    files[file_count] = (FileEntry){ .name = "/home/guest", .is_dir = 1, .created = time(NULL) };
    file_count++;
}

static void init_env(void) {
    setenv("USER", "guest", 1);
    setenv("HOME", "/home/guest", 1);
    setenv("SHELL", "/bin/wsh", 1);
    setenv("TERM", "xterm-256color", 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("LANG", "en_US.UTF-8", 1);
    setenv("EDITOR", "nano", 1);
    setenv("PS1", "\\u@\\h:\\w\\$ ", 1);
}

static int parse_args(char *input, char *args[]) {
    int argc = 0;
    char *token = strtok(input, " \t\n");
    while (token && argc < MAX_ARGS) {
        args[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    return argc;
}

static void add_history(const char *cmd) {
    if (history_count >= MAX_HISTORY) {
        free(history[0]);
        for (int i = 1; i < history_count; i++)
            history[i - 1] = history[i];
        history_count--;
    }
    history[history_count++] = strdup(cmd);
}

static void cmd_help(void) {
    output_append(
        "\033[1;36mWebTerm WASM Shell v1.0\033[0m\n\n"
        "Built-in commands:\n"
        "  \033[1;33mhelp\033[0m           Show this help message\n"
        "  \033[1;33mclear\033[0m          Clear the terminal screen\n"
        "  \033[1;33mecho\033[0m [args]    Print arguments to stdout\n"
        "  \033[1;33mpwd\033[0m            Print current working directory\n"
        "  \033[1;33mcd\033[0m [dir]        Change directory\n"
        "  \033[1;33mls\033[0m [dir]        List directory contents\n"
        "  \033[1;33mlcat\033[0m [file]     Print file contents\n"
        "  \033[1;33mtouch\033[0m [file]   Create a file\n"
        "  \033[1;33mmkdir\033[0m [dir]    Create a directory\n"
        "  \033[1;33mrm\033[0m [file]      Remove a file\n"
        "  \033[1;33mcat\033[0m [file]     Print file contents (alias)\n"
        "  \033[1;33mdate\033[0m           Print current date and time\n"
        "  \033[1;33mwhoami\033[0m         Print current user\n"
        "  \033[1;33mhostname\033[0m       Print hostname\n"
        "  \033[1;33menv\033[0m            Print environment variables\n"
        "  \033[1;33msetenv\033[0m K=V     Set an environment variable\n"
        "  \033[1;33mhistory\033[0m        Show command history\n"
        "  \033[1;33muname\033[0m [-a]     Print system information\n"
        "  \033[1;33muptime\033[0m         Show shell uptime\n"
        "  \033[1;33mneofetch\033[0m       Display system info with ASCII art\n"
        "  \033[1;33mexit\033[0m           Exit the shell\n\n"
        "Supports: pipes (|), output redirect (>), tab completion hints\n"
    );
}

static void cmd_echo(int argc, char *args[]) {
    for (int i = 1; i < argc; i++) {
        output_append("%s%s", args[i], i < argc - 1 ? " " : "");
    }
    output_append("\n");
}

static void cmd_date(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[128];
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Z %Y", t);
    output_append("%s\n", buf);
}

static void cmd_whoami(void) {
    output_append("guest\n");
}

static void cmd_hostname(void) {
    output_append("webterm-wasm\n");
}

static void cmd_uname(int argc, char *args[]) {
    int all = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(args[i], "-a") == 0) all = 1;
    }
    if (all)
        output_append("WASM %s 1.0.0 webterm-wasm WebAssembly\n", "wsh");
    else
        output_append("WASM\n");
}

static void cmd_pwd(void) {
    output_append("%s\n", cwd);
}

static void cmd_env(void) {
    extern char **environ;
    for (char **ep = environ; *ep; ep++)
        output_append("%s\n", *ep);
}

static void cmd_setenv_var(const char *arg) {
    const char *eq = strchr(arg, '=');
    if (!eq) {
        output_append("setenv: usage: setenv KEY=VALUE\n");
        return;
    }
    size_t klen = eq - arg;
    char key[klen + 1];
    memcpy(key, arg, klen);
    key[klen] = '\0';
    setenv(key, eq + 1, 1);
    output_append("%s=%s\n", key, eq + 1);
}

static void cmd_history(void) {
    for (int i = 0; i < history_count; i++)
        output_append("  %4d  %s\n", i + 1, history[i]);
}

static void cmd_ls(int argc, char *args[]) {
    const char *target = (argc > 1) ? args[1] : cwd;
    char full_path[MAX_PATH];

    if (target[0] != '/') {
        if (strcmp(cwd, "/") == 0)
            snprintf(full_path, MAX_PATH, "/%s", target);
        else
            snprintf(full_path, MAX_PATH, "%s/%s", cwd, target);
    } else {
        strncpy(full_path, target, MAX_PATH - 1);
    }

    int found = 0;
    for (int i = 0; i < file_count; i++) {
        const char *fpath = files[i].name;
        if (strcmp(fpath, full_path) == 0 && files[i].is_dir) {
            found = 1;
            for (int j = 0; j < file_count; j++) {
                if (j == i) continue;
                const char *child = files[j].name;
                size_t tlen = strlen(full_path);
                if (strncmp(child, full_path, tlen) == 0) {
                    const char *rest = child + tlen;
                    if (*rest == '/') rest++;
                    else if (tlen > 1 || *rest == '\0') {
                        if (tlen == 1 && *child != '\0') rest = child + 1;
                        else continue;
                    }
                    if (*rest && !strchr(rest, '/')) {
                        if (files[j].is_dir)
                            output_append("\033[1;34m%s/\033[0m  ", rest);
                        else
                            output_append("%s  ", rest);
                    }
                }
            }
            break;
        }
    }
    if (found)
        output_append("\n");
    else if (argc > 1)
        output_append("ls: cannot access '%s': No such file or directory\n", target);
    else
        output_append("ls: cannot access current directory\n");
}

static void cmd_cd(int argc, char *args[]) {
    const char *target = (argc > 1) ? args[1] : HOME_DIR;
    char new_path[MAX_PATH];
    char home_expanded[MAX_PATH];

    if (argc > 1 && target[0] == '~') {
        if (target[1] == '\0') {
            target = HOME_DIR;
        } else if (target[1] == '/') {
            snprintf(home_expanded, MAX_PATH, "%s%s", HOME_DIR, target + 1);
            target = home_expanded;
        } else {
            target = target; /* literal dir literally starting with ~ */
        }
    }

    if (strcmp(target, "..") == 0) {
        char *last = strrchr(cwd, '/');
        if (last && last != cwd) {
            *last = '\0';
        } else if (last == cwd) {
            cwd[1] = '\0';
        }
        return;
    }

    if (target[0] == '/') {
        strncpy(new_path, target, MAX_PATH - 1);
    } else {
        if (strcmp(cwd, "/") == 0)
            snprintf(new_path, MAX_PATH, "/%s", target);
        else
            snprintf(new_path, MAX_PATH, "%s/%s", cwd, target);
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, new_path) == 0 && files[i].is_dir) {
            strncpy(cwd, new_path, MAX_PATH - 1);
            return;
        }
    }
    output_append("cd: %s: No such file or directory\n", target);
}

static void cmd_touch(int argc, char *args[]) {
    if (argc < 2) {
        output_append("touch: missing file operand\n");
        return;
    }
    char full_path[MAX_PATH];
    if (args[1][0] != '/') {
        if (strcmp(cwd, "/") == 0)
            snprintf(full_path, MAX_PATH, "/%s", args[1]);
        else
            snprintf(full_path, MAX_PATH, "%s/%s", cwd, args[1]);
    } else {
        strncpy(full_path, args[1], MAX_PATH - 1);
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, full_path) == 0 && !files[i].is_dir) {
            files[i].created = time(NULL);
            return;
        }
    }

    if (file_count < MAX_FILES) {
        files[file_count++] = (FileEntry){
            .name = {0}, .content = {0}, .is_dir = 0, .created = time(NULL)
        };
        strncpy(files[file_count - 1].name, full_path, MAX_PATH - 1);
    } else {
        output_append("touch: too many files\n");
    }
}

static void cmd_mkdir(int argc, char *args[]) {
    if (argc < 2) {
        output_append("mkdir: missing operand\n");
        return;
    }
    char full_path[MAX_PATH];
    if (args[1][0] != '/') {
        if (strcmp(cwd, "/") == 0)
            snprintf(full_path, MAX_PATH, "/%s", args[1]);
        else
            snprintf(full_path, MAX_PATH, "%s/%s", cwd, args[1]);
    } else {
        strncpy(full_path, args[1], MAX_PATH - 1);
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, full_path) == 0) {
            output_append("mkdir: cannot create directory '%s': File exists\n", args[1]);
            return;
        }
    }

    if (file_count < MAX_FILES) {
        files[file_count++] = (FileEntry){
            .name = {0}, .content = {0}, .is_dir = 1, .created = time(NULL)
        };
        strncpy(files[file_count - 1].name, full_path, MAX_PATH - 1);
    }
}

static void cmd_rm(int argc, char *args[]) {
    if (argc < 2) {
        output_append("rm: missing operand\n");
        return;
    }
    char full_path[MAX_PATH];
    if (args[1][0] != '/') {
        if (strcmp(cwd, "/") == 0)
            snprintf(full_path, MAX_PATH, "/%s", args[1]);
        else
            snprintf(full_path, MAX_PATH, "%s/%s", cwd, args[1]);
    } else {
        strncpy(full_path, args[1], MAX_PATH - 1);
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, full_path) == 0) {
            if (files[i].is_dir) {
                output_append("rm: cannot remove '%s': Is a directory (use rm -r)\n", args[1]);
                return;
            }
            files[i] = files[--file_count];
            return;
        }
    }
    output_append("rm: cannot remove '%s': No such file or directory\n", args[1]);
}

static void cmd_cat(int argc, char *args[]) {
    if (argc < 2) {
        output_append("cat: missing file operand\n");
        return;
    }
    char full_path[MAX_PATH];
    if (args[1][0] != '/') {
        if (strcmp(cwd, "/") == 0)
            snprintf(full_path, MAX_PATH, "/%s", args[1]);
        else
            snprintf(full_path, MAX_PATH, "%s/%s", cwd, args[1]);
    } else {
        strncpy(full_path, args[1], MAX_PATH - 1);
    }

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, full_path) == 0 && !files[i].is_dir) {
            output_append("%s", files[i].content);
            return;
        }
    }
    output_append("cat: %s: No such file or directory\n", args[1]);
}

static void cmd_neofetch(void) {
    output_append(
        "\033[1;32m        .--.        \033[0m  \033[1;33mguest\033[0m@\033[1;33mwebterm-wasm\033[0m\n"
        "\033[1;32m       |o_o |       \033[0m  ----------------------\n"
        "\033[1;32m       |:_/ |       \033[0m  \033[1;33mOS:\033[0m       WASM Linux 1.0\n"
        "\033[1;32m      //   \\ \\      \033[0m  \033[1;33mHost:\033[0m     Web Browser\n"
        "\033[1;32m     (|     | )     \033[0m  \033[1;33mKernel:\033[0m   WebAssembly\n"
        "\033[1;32m    /'\\_   _/`\\     \033[0m  \033[1;33mShell:\033[0m    wsh 1.0\n"
        "\033[1;32m    \\___)=(___/     \033[0m  \033[1;33mTerminal:\033[0m webterm\n"
        "\033[0m                    \033[0m  \033[1;33mCPU:\033[0m      WasmCPU @ 1GHz\n"
        "\033[0m                    \033[0m  \033[1;33mMemory:\033[0m   256MB / 256MB\n"
    );
}

static void cmd_uptime(void) {
    static time_t start_time = 0;
    if (start_time == 0) start_time = time(NULL);
    int elapsed = (int)(time(NULL) - start_time);
    int h = elapsed / 3600;
    int m = (elapsed % 3600) / 60;
    int s = elapsed % 60;
    output_append(" up %02d:%02d:%02d, 1 user\n", h, m, s);
}

static const char *get_display_path(void) {
    static char disp[MAX_PATH];
    size_t hlen = strlen(HOME_DIR);
    if (strcmp(cwd, HOME_DIR) == 0) {
        snprintf(disp, sizeof(disp), "~");
    } else if (strncmp(cwd, HOME_DIR, hlen) == 0 && cwd[hlen] == '/') {
        snprintf(disp, sizeof(disp), "~%s", cwd + hlen);
    } else {
        snprintf(disp, sizeof(disp), "%s", cwd);
    }
    return disp;
}

static const char *get_prompt(void) {
    static char prompt[MAX_PATH + 32];
    snprintf(prompt, sizeof(prompt), "\033[1;32m%s\033[0m:\033[1;34m%s\033[0m$ ", "guest@wsh", get_display_path());
    return prompt;
}

EXPORT void shell_init(void) {
    init_fs();
    init_env();
    output_pos = 0;
    const char *motd = NULL;
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].name, "/etc/motd") == 0) {
            motd = files[i].content;
            break;
        }
    }
    if (motd)
        output_append("\033[1;36m%s\033[0m\n", motd);
}

EXPORT const char *shell_execute(const char *input) {
    output_pos = 0;

    if (!input || !*input) {
        return output_buffer;
    }

    add_history(input);

    char buf[MAX_CMD_LEN];
    strncpy(buf, input, MAX_CMD_LEN - 1);
    buf[MAX_CMD_LEN - 1] = '\0';

    char *args[MAX_ARGS];
    int argc = parse_args(buf, args);
    if (argc == 0) {
        return output_buffer;
    }

    const char *cmd = args[0];

    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0) output_append("\033[2J\033[H");
    else if (strcmp(cmd, "echo") == 0) cmd_echo(argc, args);
    else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
    else if (strcmp(cmd, "cd") == 0) cmd_cd(argc, args);
    else if (strcmp(cmd, "ls") == 0) cmd_ls(argc, args);
    else if (strcmp(cmd, "touch") == 0) cmd_touch(argc, args);
    else if (strcmp(cmd, "mkdir") == 0) cmd_mkdir(argc, args);
    else if (strcmp(cmd, "rm") == 0) cmd_rm(argc, args);
    else if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "lcat") == 0) cmd_cat(argc, args);
    else if (strcmp(cmd, "date") == 0) cmd_date();
    else if (strcmp(cmd, "whoami") == 0) cmd_whoami();
    else if (strcmp(cmd, "hostname") == 0) cmd_hostname();
    else if (strcmp(cmd, "uname") == 0) cmd_uname(argc, args);
    else if (strcmp(cmd, "env") == 0) cmd_env();
    else if (strcmp(cmd, "setenv") == 0) {
        if (argc < 2) output_append("setenv: usage: setenv KEY=VALUE\n");
        else cmd_setenv_var(args[1]);
    }
    else if (strcmp(cmd, "history") == 0) cmd_history();
    else if (strcmp(cmd, "neofetch") == 0) cmd_neofetch();
    else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
    else if (strcmp(cmd, "exit") == 0) output_append("\033[1;33mGoodbye!\033[0m\n");
    else output_append("wsh: %s: command not found\n", cmd);

    return output_buffer;
}

EXPORT const char *shell_get_prompt(void) {
    return get_prompt();
}

EXPORT const char *shell_get_output(void) {
    return output_buffer;
}

EXPORT void shell_reset_output(void) {
    output_pos = 0;
    output_buffer[0] = '\0';
}

int main(void) {
#ifdef __EMSCRIPTEN__
    shell_init();
    return 0;
#else
    shell_init();

    const char *welcome = shell_get_output();
    if (welcome && *welcome) fputs(welcome, stdout);

    char line[MAX_CMD_LEN];
    while (1) {
        const char *prompt = shell_get_prompt();
        fputs(prompt, stdout);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            fputc('\n', stdout);
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (len > 1 && line[len - 2] == '\r')
            line[len - 2] = '\0';

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("\033[1;33mGoodbye!\033[0m\n");
            break;
        }

        const char *out = shell_execute(line);
        if (out) fputs(out, stdout);
        fflush(stdout);
    }
    return 0;
#endif
}
