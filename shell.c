#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>

#define ARG_MAX 128
#define CMD_MAX 32
#define MAXJOBS 128
#define UNDEF 0 
#define FG 1    
#define BG 2    
#define ST 3  
typedef void handler_t(int);
using namespace std;


struct command {
    int argc;
    int bg;
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

struct job_t {
    pid_t pid;
    int jid;
    int state;
    string cmd;
};

class CMDCache {
private:
    unordered_map<string, commands*> cmds_map;
    commands *chead, *ctail;
    int capacity = 100;
    commands *cur;
public:
    CMDCache(int capacity) {
        this->capacity = capacity;
        chead = new commands();
        ctail = new commands();
        chead->next = ctail;
        chead->pre = ctail;
        ctail->next = chead;
        ctail->pre = chead;
        cur = chead;
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

    void listhistory() {
        commands *p = chead->next;
        while (p != ctail) {
            cout << p->key << endl;
            p = p->next; 
        }
    }

    void historyup() {
        if (cmds_map.size() == 0) return;
        cur = cur->next;
        while (cur == chead || cur == ctail) cur = cur->next;
        fputs("$>", stdout);
        fputs(cur->key.c_str(), stdout);
        fputs("\n", stdout);
        return;
    }

    void historydown() {
        if (cmds_map.size() == 0) return;
        cur = cur->pre;
        while (cur == chead || cur == ctail) cur = cur->pre;
        fputs("$>", stdout);
        fputs(cur->key.c_str(), stdout);
        fputs("\n", stdout);
        return;
    }
};
CMDCache cmd_cache(100);

class JOBCtrl {
private:
    int idx = 1;
    struct job_t jobs[MAXJOBS];
public:
    JOBCtrl () {
        for (int i = 0; i < MAXJOBS; i++) {
            clearjob(&jobs[i]);
        }
    }

    int addjob(pid_t pid, int state, string cmd) {
        for (int i = 0; i < MAXJOBS; ++i) {
            if (!jobs[i].pid) {
                jobs[i].pid = pid;
                jobs[i].state = state;
                jobs[i].jid = idx++;
                jobs[i].cmd = cmd;
                return 1;
            }
        }
        fprintf(stderr, "error: Tried to create too many jobs\n");
        return 0;
    }

    int findfgpid() {
        for (int i = 0; i < MAXJOBS; ++i) {
            if (jobs[i].state == FG) {
                return jobs[i].pid;
            }
        }
        return 0;
    }

    void clearjob(struct job_t* job) {
        job->pid = 0;
        job->jid = 0;
        job->state = UNDEF;
        job->cmd = "";
    }

    int deletejob(pid_t pid) {
        if (pid < 1)
            return 0;
        for (int i = 0; i < MAXJOBS; i++) {
            if (jobs[i].pid == pid) {
                clearjob(&jobs[i]);
                idx = maxjid() + 1;
                return 1;
            }
        }
        return 0;
    }

    int maxjid() {
        int ret = 0;
        for (int i = 0; i < MAXJOBS; ++i) {
            if (ret < jobs[i].jid) {
                ret = jobs[i].jid;
            }
        }
        return ret;
    }

    void printjobs() {
        for (int i = 0; i < MAXJOBS; ++i) {
            if (jobs[i].pid != 0) {
                printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
                switch(jobs[i].state) {
                    case BG:
                        printf("Running ");
                        break;
                    case FG:
                        printf("Foreground ");
                        break;
                    case ST:
                        printf("Stopped ");
                        break;
                    default:
                        printf("printjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
                }
                cout << jobs[i].cmd << endl;
            }
        }
    }

    void bgjob(struct command *cmd) {
        if (cmd->argc <= 1) {
            fprintf(stderr, "%s\n", "bg commands require PID/JID.");
		    return;
        }
        int jid = cmd->argv[1][0] == '%' ? stoi(cmd->argv[2]) : stoi(cmd->argv[1]);
        struct job_t *job;
        for (int i = 0; i < MAXJOBS; ++i) {
            if (jobs[i].jid == jid) {
                job = &jobs[i];
                break;
            }
        }
        pid_t pid = job->pid;
        kill(-pid, SIGCONT);
		job->state = BG;
		printf("[%d] (%d) %s\n", job->jid, pid, job->cmd.c_str());
    }

    void fgjob(struct command *cmd) {
        if (cmd->argc <= 1) {
            fprintf(stderr, "%s\n", "fg commands require PID/JID.");
		    return;
        }
        int jid = cmd->argv[1][0] == '%' ? stoi(cmd->argv[2]) : stoi(cmd->argv[1]);
        struct job_t *job;
        for (int i = 0; i < MAXJOBS; ++i) {
            if (jobs[i].jid == jid) {
                job = &jobs[i];
                break;
            }
        }
        pid_t pid = job->pid;
        kill(-pid, SIGCONT);
		job->state = FG;
        // cout << pid << findfgpid() << endl;
        while(pid == findfgpid()) {
            sleep(0);
        }
    }

    struct job_t* getjobpid(pid_t pid) {
        if (pid < 1)
		    return NULL;
        for (int i = 0; i < MAXJOBS; i++)
		    if (jobs[i].pid == pid)
			    return &jobs[i];
        return NULL;
    }

};

JOBCtrl jobctrl;

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

bool check_builtin(struct command* cmd) {
    string cname = cmd->name;
    if (cname == "exit" || cname == "cd" || cname == "history" || cname == "jobs" \
     || cname == "fg" || cname == "bg" || cname == "up" || cname == "down") {
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
    } else if (cname == "jobs") {
        jobctrl.printjobs();
        return EXIT_SUCCESS;
    } else if (cname == "bg") {
        jobctrl.bgjob(cmd);
        return EXIT_SUCCESS;
    } else if (cname == "fg") {
        jobctrl.fgjob(cmd);
        return EXIT_SUCCESS;
    } else if (cname == "history") {
        cmd_cache.listhistory();
        return EXIT_SUCCESS;
    } else if (cname == "up") {
        cmd_cache.historyup();
    } else if (cname == "down") {
        cmd_cache.historydown();
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
    // find if the bg commands
    if (cmd->argv[arg_cnt - 1] == "&") {
        cmd->bg = 1;
        arg_cnt -= 1;
    } else {
        cmd->bg = 0;
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
    sigset_t mask;
    sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	//sigprocmask(SIG_BLOCK, &mask, NULL);
    pid_t pid = fork();
    if (pid == 0) {
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
		setpgid(0, 0);
        int in_fd = cmd->fds[0];
        int out_fd = cmd->fds[1];
        if (in_fd != -1 && in_fd != STDIN_FILENO) {
            dup2(in_fd, STDIN_FILENO);
        }
        if (out_fd != -1 && out_fd != STDOUT_FILENO) {
            dup2(out_fd, STDOUT_FILENO);
        }
        char** argv = new char* [ARG_MAX];
        for (int i = 0; i < cmd->argc; ++i) {
            argv[i] = (char*)cmd->argv[i].data();
        }
        if (pipes != NULL) {
            int pipe_count = cmds->cmd_counts - 1;
            for (int i = 0; i < pipe_count; ++i) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
        }
        execv(cmd->name.c_str(), argv);
        fprintf(stderr, "error: %s\n", strerror(errno));
        if (pipes != NULL) {
            int pipe_count = cmds->cmd_counts - 1;
            for (int i = 0; i < pipe_count; ++i) {
                free(pipes[i]);
            }
        }

        for (int i = 0; i < cmds->cmd_counts; ++i) {
            free(cmds->cmds[i]);
        }
        free(cmds);
        _exit(EXIT_SUCCESS);
    } else {
        int state =  cmd->bg ? BG : FG;
	    jobctrl.addjob(pid, state, cmd->name);
	    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }
    return pid;
}

int exec_commands(struct commands* cmds) {
    int exec_ret = 0;
    if (cmds->cmd_counts == 1) {
        struct command *cmd = cmds->cmds[0];
        if (check_builtin(cmd)) {
            handle_builtin(cmd);
        } else {
            int bg = cmd->bg;
            int pid = 0;
            pid = exec_command(cmds, 0, NULL);
            exec_ret = pid;
            if (!bg) {
                // cout << pid << endl;
                // cout << jobctrl.findfgpid() << endl;
                while(pid == jobctrl.findfgpid()) {
                    // cout << jobctrl.findfgpid();
                    // cout << pid << endl;
                    // sleep(0);
                }
                return exec_ret;
            } else {
                printf("bg command  %d \n", pid);
            }
        }
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

handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) {
        fprintf(stderr, "Signal error");
        exit(1);
    }
    return (old_action.sa_handler);
}

void sigchld_handler(int sig) {
	pid_t pid;
	int status;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if ((pid = waitpid(-1, &status, 0)) > 0) {
		sigprocmask(SIG_BLOCK, &mask, NULL);
        //cout << pid << endl;
		jobctrl.deletejob(pid);
		sigprocmask(SIG_UNBLOCK, &mask, NULL);
	}
    return;
}

void sigtstp_handler(int sig) {
	pid_t pid = 0;
	pid = jobctrl.findfgpid();
    struct job_t *job = jobctrl.getjobpid(pid);
	job->state = ST;
	if (pid <= 0) {
		return;
	}
	if (kill(pid, SIGTSTP) < 0) {
		fprintf(stderr, "error when kill in sigtstp_handler\n");
		return;
	}
	printf("foreground job [%d] has been stopped.\n", job->jid);
    return;
}





int main() {
    int exec_ret;
    Signal(SIGCHLD, sigchld_handler);
    Signal(SIGTSTP, sigtstp_handler);
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
            fflush(stdout);
        }
        if (exec_ret == -1) break;
    }
    cout << "exit" << endl;
    return 0;
}
