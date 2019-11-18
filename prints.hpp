/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

#include <cstdarg>


#define MAX_MSG_LENGTH  4096

#define STDIN_FD        0UL
#define STDOUT_FD       1UL
#define STDERR_FD       2UL


#define ANSI_COLOR_BLACK   "\x1b[30m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[37m"
#define ANSI_COLOR_RESET   "\x1b[0m"


#define WRN "[" ANSI_COLOR_YELLOW " WRN " ANSI_COLOR_RESET "] "
#define NTC "[" ANSI_COLOR_WHITE  " NTC " ANSI_COLOR_RESET "] "
#define ERR "[" ANSI_COLOR_RED    " ERR " ANSI_COLOR_RESET "] "
#define DBG "[" ANSI_COLOR_CYAN   " DBG " ANSI_COLOR_RESET "] "
#define SCC "[" ANSI_COLOR_GREEN  " SCC " ANSI_COLOR_RESET "] "


// TODO:
// - Improve the signature of debug and warning (avoid passing
//   references to thread and task data)
// - Lock log channel when multiple lines from same task must be
//   generated

INLINE
size_t print_prefix(const char *report_type, const char *margin, char *msg, size_t numchars,
	thread_data_t *thread, task_data_t *task) {

	const char *empty_thread = (const char *)"--";
	const char *empty_task   = (const char *)"------";

	size_t diff_chars = 0;

	diff_chars += snprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		report_type);

	if (thread == NULL && task == NULL) {
		diff_chars += snprintf(msg + numchars + diff_chars, MAX_MSG_LENGTH - numchars + diff_chars,
			"[%06lu] [%s] [%s]%s", app.id, empty_thread, empty_task, margin);
	}
	else if (task == NULL) {
		diff_chars += snprintf(msg + numchars + diff_chars, MAX_MSG_LENGTH - numchars + diff_chars,
			"[%06lu] [%02d] [%s]%s", app.id, thread->id, empty_task, margin);
	}
	else {
		diff_chars += snprintf(msg + numchars + diff_chars, MAX_MSG_LENGTH - numchars + diff_chars,
			"[%06lu] [%02d] [%06d]%s", app.id, thread->id, task->id, margin);
	}

	return diff_chars;
}


#ifndef NDEBUG
#define expect(cond, format, ...) do { \
	if (!(cond)) { \
		__error(NULL, NULL, "{ %s:%d } " format, __FILE__, __LINE__, ##__VA_ARGS__); \
	} \
} while(0)
#else
#define expect(cond, format, ...)
#endif


#define error(thread, task, format, ...) \
	__error(thread, task, "{ %s:%d } " format, __FILE__, __LINE__, ##__VA_ARGS__)


/*
	Prints an error message and exits.
 */
void __error(thread_data_t *thread, task_data_t *task, const char *format, ...) {
	va_list args;
	char msg[MAX_MSG_LENGTH];
	size_t numchars = 0, writtenchars = 0;

	va_start(args, format);

	// numchars += snprintf(msg, MAX_MSG_LENGTH - numchars,
	// 	ERR);

	numchars += print_prefix(ERR, " ", msg, numchars, thread, task);
	numchars += vsnprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		format, args);
	numchars += snprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		"\n");

	utils_write_fd(
		config.use_thread_logs ? (thread ? thread->log_fd : app.log_fd) : STDERR_FD,
		msg, numchars, &writtenchars);

	va_end(args);

	PIN_ExitProcess(1);
}


#ifndef NDEBUG
#define debug(debug_level, thread, task, format, ...) do { \
	if (config.max_debug_level >= debug_level) { \
		__debug(debug_level, thread, task, format, ##__VA_ARGS__); \
	} \
} while(0)
#else
#define debug(debug_level, thread, task, format, ...)
#endif


/*
	Prints a debug message at the requested nesting level and
	passing the current thread and task IDs, if any (or -1 if they
	cannot be retrieved). Debug messages are shown based on the
	value of the TAREADOR_DEBUG environment variable.
 */
void __debug(uint8_t debug_level, thread_data_t *thread, task_data_t *task, const char *format, ...) {
	va_list args;
	char msg[MAX_MSG_LENGTH];
	char margin[32];
	size_t numchars = 0, writtenchars = 0;
	int8_t i = 0;

	// char *debug = getenv("TAREADOR_DEBUG");
	// if (debug == NULL || atoi(debug) < debug_level) {
	// 	return;
	// }

	for (; i < debug_level + 1; ++i) {
		margin[i] = ' ';
	}
	if (i > 1) {
		margin[i++] = '`';
		margin[i++] = '-';
		margin[i++] = ' ';
	}
	margin[i] = '\0';

	va_start(args, format);

	numchars += print_prefix(DBG, margin, msg, numchars, thread, task);
	numchars += vsnprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		format, args);
	numchars += snprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		"\n");

	utils_write_fd(
		config.use_thread_logs ? (thread ? thread->log_fd : app.log_fd) : STDOUT_FD,
		msg, numchars, &writtenchars);

	va_end(args);
}


#define report(report_level, thread, task, format, ...) do { \
	const char *report_type; \
	switch (report_level) { \
		case config.REPORT_NOTICE: \
			report_type = NTC; \
			break; \
		case config.REPORT_WARNING: \
			report_type = WRN; \
			break; \
		default: \
			report_type = ""; \
			error(thread, task, "Invalid report level."); \
	} \
	__report(report_type, thread, task, format, ##__VA_ARGS__); \
} while(0)


#define success(format, ...) do { \
	__report(SCC, NULL, NULL, format, ##__VA_ARGS__); \
} while(0)


/*
	Prints a warning message to report about a mismatch, using the
	provided thread and task data.
 */
void __report(const char *report_type, thread_data_t *thread, task_data_t *task, const char *format, ...) {
	va_list args;
	char msg[MAX_MSG_LENGTH];
	size_t numchars = 0, writtenchars = 0;

	va_start(args, format);

	numchars += print_prefix(report_type, " ", msg, numchars, thread, task);
	numchars += vsnprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		format, args);
	numchars += snprintf(msg + numchars, MAX_MSG_LENGTH - numchars,
		"\n");

	utils_write_fd(
		config.use_thread_logs ? (thread ? thread->log_fd : app.log_fd) : STDOUT_FD,
		msg, numchars, &writtenchars);

	va_end(args);
}
