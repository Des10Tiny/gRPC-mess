#pragma once

#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

class SubProcess {
    pid_t pid_ = -1;

public:
    explicit SubProcess(const std::string& command) {
        pid_ = fork();
        if (pid_ == 0) {
            std::string shell_cmd = "exec " + command;
            execl("/bin/sh", "sh", "-c", shell_cmd.c_str(), nullptr);
            exit(1);
        } else if (pid_ < 0) {
            throw std::runtime_error("Failed to fork process for: " + command);
        }
    }

    ~SubProcess() {
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }

    SubProcess(const SubProcess&) = delete;
    SubProcess& operator=(const SubProcess&) = delete;
};