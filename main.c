// SPDX-License-Identifier: GPL-2.0
/*
 * User-space controller for the Kernel Round-Robin Time-Sharing Scheduler
 *
 * Prompts the user (or parses CLI arguments) for the number of kernel worker
 * threads and the Round-Robin time quantum in milliseconds, writes the
 * configuration to /proc/timeshare, and streams the live kernel execution
 * trace until all kernel worker threads complete.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "scheduler.h"

#define READ_BUF_SIZE	65536
#define POLL_INTERVAL_NS 20000000L

/*
 * parse_bounded_int - Parse a strict integer string within [min_val, max_val]
 * @str:     Input string to parse
 * @min_val: Minimum allowed value (inclusive)
 * @max_val: Maximum allowed value (inclusive)
 * @out_val: Output pointer for parsed integer
 *
 * Return: 0 on valid input within bounds, -1 on error.
 */
static int parse_bounded_int(const char *str, int min_val, int max_val,
			     int *out_val)
{
	char *endptr = NULL;
	long val;

	if (!str || !out_val)
		return -1;

	while (isspace((unsigned char)*str))
		str++;

	if (*str == '\0')
		return -1;

	errno = 0;
	val = strtol(str, &endptr, 10);
	if (errno != 0 || endptr == str)
		return -1;

	while (isspace((unsigned char)*endptr))
		endptr++;

	if (*endptr != '\0')
		return -1;

	if (val < min_val || val > max_val)
		return -1;

	*out_val = (int)val;
	return 0;
}

/*
 * prompt_int - Read and validate an integer parameter from stdin
 * @label:   Prompt message shown to the user
 * @min_val: Minimum allowed value
 * @max_val: Maximum allowed value
 * @out_val: Output pointer for validated integer
 *
 * Return: 0 on success, -1 on EOF or invalid input.
 */
static int prompt_int(const char *label, int min_val, int max_val, int *out_val)
{
	char buf[128];

	printf("%s [%d-%d]: ", label, min_val, max_val);
	fflush(stdout);

	if (!fgets(buf, sizeof(buf), stdin)) {
		fprintf(stderr, "\nError: Failed to read input from stdin.\n");
		return -1;
	}

	if (!strchr(buf, '\n') && !feof(stdin)) {
		int ch;

		while ((ch = getchar()) != '\n' && ch != EOF)
			;
		fprintf(stderr, "Error: Input line is too long.\n");
		return -1;
	}

	if (parse_bounded_int(buf, min_val, max_val, out_val) < 0) {
		fprintf(stderr, "Error: Enter an integer in [%d, %d].\n",
			min_val, max_val);
		return -1;
	}

	return 0;
}

/*
 * read_configuration - Obtain simulation parameters via CLI args or stdin
 * @argc:       Argument count from main()
 * @argv:       Argument vector from main()
 * @nr_procs:   Output pointer for number of processes
 * @quantum_ms: Output pointer for time quantum in milliseconds
 *
 * Return: 0 on success, -1 on invalid input.
 */
static int read_configuration(int argc, char **argv, int *nr_procs,
			      int *quantum_ms)
{
	if (argc == 3) {
		if (parse_bounded_int(argv[1], 1, MAX_PROCS, nr_procs) < 0) {
			fprintf(stderr, "Error: Processes must be 1..%d.\n",
				MAX_PROCS);
			return -1;
		}
		if (parse_bounded_int(argv[2], MIN_QUANTUM_MS, MAX_QUANTUM_MS,
				      quantum_ms) < 0) {
			fprintf(stderr, "Error: Quantum must be %d..%d ms.\n",
				MIN_QUANTUM_MS, MAX_QUANTUM_MS);
			return -1;
		}
		return 0;
	}

	if (argc != 1) {
		fprintf(stderr, "Usage: %s [num_processes time_quantum_ms]\n",
			argv[0]);
		return -1;
	}

	printf("=========================================================\n");
	printf("   Linux Kernel Time-Sharing Scheduler (hrtimer & kthread)\n");
	printf("=========================================================\n");

	if (prompt_int("Enter number of worker processes", 1, MAX_PROCS,
		       nr_procs) < 0)
		return -1;

	if (prompt_int("Enter time quantum in milliseconds", MIN_QUANTUM_MS,
		       MAX_QUANTUM_MS, quantum_ms) < 0)
		return -1;

	return 0;
}

/*
 * trigger_kernel_sim - Write configuration to /proc/timeshare
 * @nr_procs:   Number of worker kthreads
 * @quantum_ms: Time quantum in milliseconds
 *
 * Return: 0 on success, -1 on error.
 */
static int trigger_kernel_sim(int nr_procs, int quantum_ms)
{
	char cmd[64];
	ssize_t written;
	int len;
	int fd;

	fd = open(TIMESHARE_PROC_PATH, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr,
			"Error: Cannot open %s (%s).\n"
			"Hint: Load the kernel module first with:\n"
			"  sudo insmod timeshare_kmod.ko\n"
			"or run inside the QEMU VM with:\n"
			"  make vm-qemu\n",
			TIMESHARE_PROC_PATH, strerror(errno));
		return -1;
	}

	len = snprintf(cmd, sizeof(cmd), "%d %d\n", nr_procs, quantum_ms);
	written = write(fd, cmd, (size_t)len);
	if (written < 0) {
		perror("write(/proc/timeshare)");
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

/*
 * stream_kernel_output - Poll /proc/timeshare and stream new trace lines
 *
 * Return: 0 when [KERNEL_STATUS: COMPLETED] is observed, -1 on error.
 */
static int stream_kernel_output(void)
{
	struct timespec delay;
	size_t printed_len = 0;
	char *buf;

	buf = malloc(READ_BUF_SIZE);
	if (!buf) {
		perror("malloc");
		return -1;
	}

	delay.tv_sec = 0;
	delay.tv_nsec = POLL_INTERVAL_NS;

	for (;;) {
		char *status_marker;
		size_t content_len;
		ssize_t total = 0;
		ssize_t n;
		int fd;

		fd = open(TIMESHARE_PROC_PATH, O_RDONLY);
		if (fd < 0) {
			perror("open(/proc/timeshare)");
			free(buf);
			return -1;
		}

		while ((n = read(fd, buf + total,
				 READ_BUF_SIZE - 1 - (size_t)total)) > 0) {
			total += n;
			if (total >= READ_BUF_SIZE - 1)
				break;
		}
		close(fd);

		if (n < 0) {
			perror("read(/proc/timeshare)");
			free(buf);
			return -1;
		}

		buf[total] = '\0';
		status_marker = strstr(buf, "\n[KERNEL_STATUS:");
		if (!status_marker)
			status_marker = strstr(buf, "[KERNEL_STATUS:");

		content_len = status_marker ?
			      (size_t)(status_marker - buf) : (size_t)total;

		if (content_len > printed_len) {
			fwrite(buf + printed_len, 1,
			       content_len - printed_len, stdout);
			fflush(stdout);
			printed_len = content_len;
		}

		if (status_marker &&
		    strstr(status_marker, "[KERNEL_STATUS: COMPLETED]"))
			break;

		nanosleep(&delay, NULL);
	}

	free(buf);
	return 0;
}

int main(int argc, char **argv)
{
	int nr_procs = 0;
	int quantum_ms = 0;

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (read_configuration(argc, argv, &nr_procs, &quantum_ms) < 0)
		return EXIT_FAILURE;

	if (trigger_kernel_sim(nr_procs, quantum_ms) < 0)
		return EXIT_FAILURE;

	if (stream_kernel_output() < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
