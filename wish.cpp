#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>

static std::vector<std::string> search_paths = {"/bin"};

void print_error() {
    const char msg[] = "An error has occurred\n";
    if (write(STDERR_FILENO, msg, strlen(msg))) {}
}

std::vector<std::string> tokenize(const std::string &str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

std::string find_bin(const std::string &cmd) {
    for (const auto &p : search_paths) {
        std::string full = p + "/" + cmd;
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return "";
}

pid_t run_command(std::string cmd_str) {
    size_t r1 = cmd_str.find('>');
    std::string out_file = "";
    if (r1 != std::string::npos) {
        if (cmd_str.find('>', r1 + 1) != std::string::npos) {
            print_error();
            return 0;
        }
        std::vector<std::string> f_toks = tokenize(cmd_str.substr(r1 + 1));
        if (f_toks.size() != 1) {
            print_error();
            return 0;
        }
        out_file = f_toks[0];
        cmd_str = cmd_str.substr(0, r1);
    }

    std::vector<std::string> args = tokenize(cmd_str);
    if (args.empty()) {
        if (!out_file.empty()) print_error();
        return 0;
    }

    if (args[0] == "exit" || args[0] == "cd" || args[0] == "path") {
        if (!out_file.empty()) { print_error(); return 0; }
        if (args[0] == "exit") {
            if (args.size() != 1) print_error();
            else exit(0);
        } else if (args[0] == "cd") {
            if (args.size() != 2 || chdir(args[1].c_str()) != 0) print_error();
        } else if (args[0] == "path") {
            search_paths.assign(args.begin() + 1, args.end());
        }
        return 0;
    }

    std::string bin = find_bin(args[0]);
    if (bin.empty()) {
        print_error();
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        print_error();
        return 0;
    }
    if (pid == 0) {
        if (!out_file.empty()) {
            int fd = open(out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { print_error(); exit(1); }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        std::vector<char*> c_args;
        for (const auto &a : args) c_args.push_back(const_cast<char*>(a.c_str()));
        c_args.push_back(nullptr);

        execv(bin.c_str(), c_args.data());
        print_error();
        exit(1);
    }
    return pid;
}

void process_line(const std::string &line) {
    std::stringstream ss(line);
    std::string cmd;
    std::vector<pid_t> pids;

    while (std::getline(ss, cmd, '&')) {
        pid_t p = run_command(cmd);
        if (p > 0) pids.push_back(p);
    }

    for (pid_t p : pids) {
        waitpid(p, nullptr, 0);
    }
}

int main(int argc, char *argv[]) {
    std::istream *stream = &std::cin;
    std::ifstream file;
    bool interactive = (argc == 1);

    if (argc == 2) {
        file.open(argv[1]);
        if (!file.is_open()) { print_error(); exit(1); }
        stream = &file;
    } else if (argc > 2) {
        print_error();
        exit(1);
    }

    std::string line;
    while (true) {
        if (interactive) { std::cout << "wish> "; std::cout.flush(); }
        if (!std::getline(*stream, line)) break;
        process_line(line);
    }
    return 0;
}
