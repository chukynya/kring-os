#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

/*
 * tick_count tracks the number of timer interrupts.
 * volatile sig_atomic_t guarantees atomic reads/writes
 * and prevents the compiler from optimizing out checks.
 */
static volatile sig_atomic_t tick_count = 0;

/*
 * Signal handler for SIGALRM.
 * Keep handlers minimal: increment the counter and let
 * the main loop handle heavy tasks or output.
 */
static void timer_handler(int signum) {
    (void)signum; // Silence unused parameter warning
    tick_count++;
}

int main(void) {
    printf("[Scheduler] Milestone 1: Timer & Signal Foundation\n");
    printf("[Scheduler] Setting up periodic timer (1-second quantum)...\n");

    // 1. Configure the signal handler using sigaction (more reliable than signal())
    struct sigaction sa;
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("Failed to set up SIGALRM handler");
        return EXIT_FAILURE;
    }

    // 2. Configure the interval timer (setitimer)
    // it_value: Time until first expiration
    // it_interval: Period for all subsequent expirations
    struct itimerval timer;
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("Failed to set interval timer");
        return EXIT_FAILURE;
    }

    // 3. Main loop: wait for ticks and observe them
    int last_observed_tick = 0;
    while (last_observed_tick < 5) {
        // pause() suspends the process until any signal is caught
        pause();

        if (tick_count != last_observed_tick) {
            last_observed_tick = tick_count;
            printf("[Tick %d] Timer interrupt received (SIGALRM)\n", last_observed_tick);
        }
    }

    printf("[Scheduler] Completed 5 timer ticks. Foundation verified successfully.\n");
    return EXIT_SUCCESS;
}
