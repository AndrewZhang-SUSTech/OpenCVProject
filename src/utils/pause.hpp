#include <fmt/core.h>
#include <quill/Backend.h>
#include <cstdio>
#include <string_view>

#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;
bool terminal_modified = false;

void reset_terminal_mode() noexcept {
    if (terminal_modified) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        terminal_modified = false;
    }
}

void signal_handler(int signum) noexcept {
    reset_terminal_mode();
    std::_Exit(signum);
}

#endif

void smart_pause() noexcept {
#if defined(_WIN32) || defined(_WIN64)
    DWORD processList[2];
    DWORD count = GetConsoleProcessList(processList, 2);
    if (count != 2) {
        quill::Backend::stop();
        fmt::println("Press any key to continue . . .");
        _getch();
    }
#elif defined(__APPLE__)
    pid_t ppid = getppid();
    char procName[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(ppid, procName, sizeof(procName)) > 0) {
        std::string_view parentPath(procName);
        if (parentPath.find("terminal") != std::string_view::npos ||
            parentPath.find("iTerm2") != std::string_view::npos ||
            parentPath.find("launchd") != std::string_view::npos) {
            signal(SIGINT, signal_handler);
            signal(SIGTERM, signal_handler);

            struct termios new_termios;
            tcgetattr(STDIN_FILENO, &orig_termios);
            new_termios = orig_termios;
            new_termios.c_lflag &= ~(ICANON | ECHO);

            terminal_modified = true;
            tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
            fmt::println("Press any key to continue . . .");
            getchar();
            reset_terminal_mode();
        }
    }
#endif
}
