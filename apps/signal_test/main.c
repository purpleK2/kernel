#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

static bool got_signal = false;

void handle_sigusr1(int signum) {
    printf("Received signal: %d\n", signum);
    got_signal = true;
}

int main() {
    signal(SIGUSR1, handle_sigusr1);
    pid_t parent = getpid();
    pid_t child  = fork();

    if (child < 0) {
        perror("fork");
        return 1;
    } else if (child == 0) {
        printf("Child process: PID=%d, PPID=%d\n", getpid(), getppid());
        kill(parent, SIGUSR1);
        _exit(0);
    }

    while (!got_signal) {
        asm volatile("pause");
    }
    printf("got signal");

    return 0;
}