#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>

static std::vector<std::string> search_paths = {"/bin"};

void print_error() {
    const char error_message[30] = "An error has occurred\n";
    ssize_t bytes_written = write(STDERR_FILENO, error_message, strlen(error_message));
    (void)bytes_written;
}

std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> tokenize(const std::string &str) {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string find_executable(const std::string &cmd) {
    if (cmd.empty()) return "";
    for (const auto &p : search_paths) {
        std::string full_path = p + "/" + cmd;
        if (access(full_path.c_str(), X_OK) == 0) {
            return full_path;
        }
    }
    return "";
}

int handle_builtin(const std::vector<std::string> &args) {
    if (args.empty()) return 1;

    if (args[0] == "exit") {
        if (args.size() != 1) {
            print_error();
            return 1;
        }
        return -1;
    }

    if (args[0] == "cd") {
        if (args.size() != 2) {
            print_error();
        } else {
            if (chdir(args[1].c_str()) != 0) {
                print_error();
            }
        }
        return 1;
    }

    if (args[0] == "path") {
        search_paths.clear();
        for (size_t i = 1; i < args.size(); ++i) {
            search_paths.push_back(args[i]);
        }
        return 1;
    }

    return 0;
}

struct ParsedCommand {
    std::vector<std::string> args;
    std::string output_file;
    bool has_redirection = false;
    bool is_valid = true;
};

ParsedCommand parse_single_command(const std::string &cmd_raw) {
    ParsedCommand cmd;
    std::string trimmed = trim(cmd_raw);
    if (trimmed.empty()) {
        cmd.is_valid = false;
        return cmd;
    }

    size_t first_redir = trimmed.find('>');
    std::string cmd_part = trimmed;

    if (first_redir != std::string::npos) {
        cmd.has_redirection = true;

        if (trimmed.find('>', first_redir + 1) != std::string::npos) {
            print_error();
            cmd.is_valid = false;
            return cmd;
        }

        cmd_part = trim(trimmed.substr(0, first_redir));
        std::string file_part = trim(trimmed.substr(first_redir + 1));

        std::vector<std::string> file_tokens = tokenize(file_part);
        if (file_tokens.size() != 1) {
            print_error();
            cmd.is_valid = false;
            return cmd;
        }
        cmd.output_file = file_tokens[0];
    }

    cmd.args = tokenize(cmd_part);

    if (cmd.args.empty()) {
        if (cmd.has_redirection) {
            print_error();
        }
        cmd.is_valid = false;
        return cmd;
    }

    return cmd;
}

void process_line(const std::string &line) {
    std::string trimmed_line = trim(line);
    if (trimmed_line.empty()) return;

    std::vector<std::string> raw_cmds;
    std::stringstream ss(trimmed_line);
    std::string segment;
    while (std::getline(ss, segment, '&')) {
        raw_cmds.push_back(segment);
    }

    std::vector<ParsedCommand> parsed_cmds;
    for (const auto &raw : raw_cmds) {
        std::string t = trim(raw);
        if (t.empty()) continue;
        ParsedCommand pc = parse_single_command(t);
        if (pc.is_valid) {
            parsed_cmds.push_back(pc);
        }
    }

    if (parsed_cmds.empty()) return;

    if (parsed_cmds.size() == 1) {
        if (parsed_cmds[0].has_redirection && 
           (parsed_cmds[0].args[0] == "exit" || 
            parsed_cmds[0].args[0] == "cd" || 
            parsed_cmds[0].args[0] == "path")) {
            print_error();
            return;
        }

        int status = handle_builtin(parsed_cmds[0].args);
        if (status == -1) {
            exit(0);
        }
        if (status == 1) {
            return;
        }
    }

    std::vector<pid_t> pids;

    for (const auto &cmd : parsed_cmds) {
        if (cmd.args[0] == "exit" || cmd.args[0] == "cd" || cmd.args[0] == "path") {
            if (cmd.has_redirection) {
                print_error();
                continue;
            }
            int b_status = handle_builtin(cmd.args);
            if (b_status == -1) {
                exit(0);
            }
            continue;
        }

        std::string binary = find_executable(cmd.args[0]);
        if (binary.empty()) {
            print_error();
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            print_error();
            continue;
        } else if (pid == 0) {
            if (cmd.has_redirection) {
                int fd = open(cmd.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    print_error();
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            std::vector<char*> c_args;
            for (const auto &arg : cmd.args) {
                c_args.push_back(const_cast<char*>(arg.c_str()));
            }
            c_args.push_back(nullptr);

            execv(binary.c_str(), c_args.data());
            print_error();
            exit(1);
        } else {
            pids.push_back(pid);
        }
    }

    for (pid_t pid : pids) {
        waitpid(pid, nullptr, 0);
    }
}

int main(int argc, char *argv[]) {
    std::istream *input_stream = &std::cin;
    std::ifstream file_stream;
    bool is_interactive = true;

    if (argc == 2) {
        is_interactive = false;
        file_stream.open(argv[1]);
        if (!file_stream.is_open()) {
            print_error();
            exit(1);
        }
        input_stream = &file_stream;
    } else if (argc > 2) {
        print_error();
        exit(1);
    }

    std::string line;
    while (true) {
        if (is_interactive) {
            std::cout << "wish> ";
            std::cout.flush();
        }

        if (!std::getline(*input_stream, line)) {
            break;
        }

        process_line(line);
    }

    return 0;
}
