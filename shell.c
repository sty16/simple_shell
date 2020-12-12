#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>

#define ARG_MAX 128
#define CMD_MAX 32
using namespace std;


struct command {
    int argc;
    string name;
    string argv[ARG_MAX];
    int fds[2];
};

struct commands {
    string key;
    int cmd_counts;
    struct command* cmds[CMD_MAX];
    commands *pre, *next;
};


string read_input() {
    string input;
    char c;
    while ((c = getchar()) != '\n') {
        if (c == EOF) {
            return "";
        }
        input.push_back(c);
    }
    return input;
}

bool is_blank(string input) {
    for (char c : input) {
        if (!isspace(c)) return false;
    }
    return true;
}


class CMDCache {
private:
    unordered_map<string, commands*> cmds_map;
    commands *chead, *ctail;
    int capacity = 100;
public:
    CMDCache(int capacity) {
        this->capacity = capacity;
        chead = new commands();
        ctail = new commands();
        chead->next = ctail;
        chead->pre = ctail;
        ctail->next = chead;
        ctail->pre = chead;
    }

    commands* get(string input) {
        if (!cmds_map.count(input)) return NULL;
        commands *cur = cmds_map[input];
        cur->pre->next = cur->next;
        cur->next->pre = cur->pre;
        cur->pre = chead;
        cur->next = chead->next;
        chead->next = cur;
        cur->next->pre = cur;
        return cur;
    }

    void put(string key, commands* cmd) {
        if (get(key) != NULL) {
            return;
        } else {
            commands* cur = cmd;
            cmds_map[key] = cmd;
            cur->next = chead->next;
            cur->pre = chead;
            chead->next = cur;
            cur->next->pre = cur;
            if ((int)cmds_map.size() > capacity) {
                commands *tmp = ctail->pre;
                ctail->pre = tmp->pre;
                ctail->pre->next = ctail;
                cmds_map.erase(tmp->key);
                delete tmp;
            }
        }
    }
};

bool check_builtin(struct command* cmd) {
    string cname = cmd->name;
    if (cname == "exit" || cname == "cd" || cname == "") {
        return true;
    }
    return false;
}

int handle_builtin(struct command* cmd) {
    int exec_ret;
    string cname = cmd->name;
    if (cname == "exit") return -1;
    else if (cname == "cd") {
        exec_ret = chdir(cmd->argv[1].c_str());
        if (exec_ret != 0) {
            fprintf(stderr, "error : unable to change dir\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } else if (cname == "") {

    }
    return EXIT_SUCCESS;
}

struct command* parse_command(string input) {
    int arg_cnt = 0;
    int n = input.size();
    int i = 0, j;
    struct command* cmd = new command();
    cmd->fds[0] = STDIN_FILENO;
    cmd->fds[1] = STDOUT_FILENO;
    while (i < n) {
        if (input[i] == ' ') {
            ++i;
            continue;
        } else {
            j = i + 1;
            while (j < n && input[j] != ' ') ++j;
            cmd->argv[arg_cnt++] = input.substr(i, j - i);
            i = j;
        }
    }
    // check redirect 
    for (int i = 0; i < arg_cnt; ++i) {
        if (cmd->argv[i] == ">") {
            int out_fd = open(cmd->argv[i + 1].c_str(), O_RDWR | O_CREAT, 0777);
            cmd->fds[1] = out_fd;
            arg_cnt -= 2;
        }
        if (cmd->argv[i] == "<") {
            int in_fd = open(cmd->argv[i + 1].c_str(), O_RDWR, 0777);
            cmd->fds[0] = in_fd;
            arg_cnt -= 2;
        }
    }
    cmd->argc = arg_cnt;
    cmd->name = cmd->argv[0];   
    return cmd;
}

struct commands* parse_commands(string input) {
    int cmd_cnt = 0;
    int i = 0, j = 0, n = input.size();
    struct commands* cmds = new commands();
    if (cmds == NULL) {
        fprintf(stderr, "error: memory alloc error\n");
        exit(EXIT_FAILURE);
    }
    while (i < n) {
        if (input[i] == '|') {
            ++i;
            continue;
        } else {
            j = i + 1;
            while (j < n && input[j] != '|') ++j;
            cmds->cmds[cmd_cnt++] = parse_command(input.substr(i, j - i));
            i = j;
        }
    }
    cmds->key = input;
    cmds->cmd_counts = cmd_cnt;
    return cmds;
}

int exec_command(struct commands *cmds, int cid, int (*pipes)[2]) {
    struct command* cmd = cmds->cmds[cid];
    if (check_builtin(cmd)) {
        return handle_builtin(cmd);
    }
    pid_t pid = fork();
    if (pid == 0) {
        int in_fd = cmd->fds[0];
        int out_fd = cmd->fds[1];
        if (in_fd != -1 && in_fd != STDIN_FILENO) {
            close(pipes[cid][1]);
            dup2(in_fd, STDIN_FILENO);
        }
        if (out_fd != -1 && out_fd != STDOUT_FILENO) {
            close(pipes[cid][0]);
            dup2(out_fd, STDOUT_FILENO);
        }
        char** argv = new char* [ARG_MAX];
        for (int i = 0; i < cmd->argc; ++i) {
            argv[i] = (char*)cmd->argv[i].data();
        }
        int pipe_count = cmds->cmd_counts - 1;
        for (int i = 0; i < pipe_count; ++i) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        // cout << cmd->argc << endl;
        // if (cmd->name == "/usr/bin/wc") {
        // //     char *buff = new char[2048];
        // //     read(0, buff, 10);
        // //     cout << buff << endl;
        //     char *buff[2048];
        //     do {
        //         len = read(0, buff, 2048);
        //         string input((char *)buff, len);
        //         cout <<  input << endl;
        //         argv[cmd->argc] = (char *)input.c_str();
        //     } while(len);
        //  }
        execv(cmd->name.c_str(), argv);
        fprintf(stderr, "error: %s\n", strerror(errno));

        for (int i = 0; i < pipe_count; ++i) {
            free(pipes[i]);
        }
        for (int i = 0; i < cmds->cmd_counts; ++i) {
            free(cmds->cmds[i]);
        }
        free(cmds);
        // 关闭文件
        //if (in_fd != -1 && in_fd != STDIN_FILENO) close(in_fd);
        //if (out_fd != -1 && out_fd != STDOUT_FILENO) close(out_fd);
        _exit(EXIT_FAILURE);
    }
    return pid;
}

int exec_commands(struct commands* cmds) {
    int exec_ret;
    if (cmds->cmd_counts == 1) {
        exec_ret = exec_command(cmds, 0, NULL);
        wait(NULL);
    } else {
        int pipe_cnt = cmds->cmd_counts - 1;
        int (*pipes)[2] = new int[pipe_cnt][2];
        if (pipes == NULL) {
            fprintf(stderr, "error: memory alloc error\n");
			return 0;
        }
        cmds->cmds[0]->fds[0] = 0;
        for (int i = 0; i < pipe_cnt; ++i) {
            pipe(pipes[i]);
            cmds->cmds[i]->fds[STDOUT_FILENO] = pipes[i][1];
            cmds->cmds[i + 1]->fds[STDIN_FILENO] = pipes[i][0];
        }
        cmds->cmds[pipe_cnt]->fds[STDOUT_FILENO] = STDOUT_FILENO;
        for (int i = 0; i < cmds->cmd_counts; ++i) {
            exec_ret = exec_command(cmds, i, pipes);
        }
        for (int i = 0; i < pipe_cnt; ++i) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        delete pipes;
        for (int i = 0; i < cmds->cmd_counts; ++i) {
            wait(NULL);
        }
    }
    return exec_ret;
}






int main() {
    int exec_ret;
    CMDCache cmd_cache(100);
    while (true) {
        fputs("$>", stdout);
        string input = read_input();
        if (input.size() > 0 && !is_blank(input)) {
            struct commands *cmds = cmd_cache.get(input);
            if (cmds == NULL) {
                cmds = parse_commands(input);
                cmd_cache.put(input, cmds);
            }
            exec_ret = exec_commands(cmds);
        }
        if (exec_ret == -1) break;
    }
    return 0;
}
