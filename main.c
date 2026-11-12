/*
 * main.c - Interactive entry point for the time-sharing system simulator
 *
 * Prompts the user for the number of worker processes and the Round-Robin
 * time quantum (in milliseconds), initializes the process table, runs the
 * signal/timer scheduler, and displays a final summary table.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "scheduler.h"

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

	if (parse_bounded_int(buf, min_val, max_val, out_val) < 0) {
		fprintf(stderr, "Error: Value must be an integer between "
			"%d and %d.\n", min_val, max_val);
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
			fprintf(stderr, "Error: Number of processes must be "
				"between 1 and %d.\n", MAX_PROCS);
			return -1;
		}
		if (parse_bounded_int(argv[2], MIN_QUANTUM_MS, MAX_QUANTUM_MS,
				      quantum_ms) < 0) {
			fprintf(stderr, "Error: Time quantum must be between "
				"%d and %d ms.\n",
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
	printf("   UNIX Time-Sharing System Simulator (Signals & Timers) \n");
	printf("=========================================================\n");

	if (prompt_int("Enter number of worker processes", 1, MAX_PROCS,
		       nr_procs) < 0)
		return -1;

	if (prompt_int("Enter time quantum in milliseconds", MIN_QUANTUM_MS,
		       MAX_QUANTUM_MS, quantum_ms) < 0)
		return -1;

	return 0;
}

int main(int argc, char **argv)
{
	struct scheduler_ctx ctx;
	int nr_procs = 0;
	int quantum_ms = 0;

	if (read_configuration(argc, argv, &nr_procs, &quantum_ms) < 0)
		return EXIT_FAILURE;

	if (scheduler_init(&ctx, nr_procs, quantum_ms) < 0) {
		fprintf(stderr, "Error: Failed to initialize scheduler.\n");
		return EXIT_FAILURE;
	}

	if (scheduler_run(&ctx) < 0) {
		fprintf(stderr, "Error: Scheduler execution failed.\n");
		return EXIT_FAILURE;
	}

	scheduler_print_summary(&ctx);
	return EXIT_SUCCESS;
}
