/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

#define INLINE inline __attribute__((always_inline))
//#define NDEBUG

#define NANOS6_LOADER_IMG  ((const char *)("libnanos6.so.0"))
#define NANOS6_LINT_IMG    ((const char *)("libnanos6-lint.so.0"))


#define MAX_COMMAND_LENGTH   2048
#define MAX_PATH_LENGTH      2048
#define MAX_OUTPUT_LENGTH    4096


struct {
	// Core options
	const char *output_dir;
	const char *experiment_name;

	enum {
		REPORT_DEBUG   = 100,
		REPORT_NOTICE  = 200,
		REPORT_WARNING = 300
	} min_report_level;

	unsigned int max_debug_level;

	// enum {
	// 	EVALUATE_PROCESSING = 0,
	// 	EVALUATE_LOGGING,
	// 	EVALUATE_INSTR_ISA,
	// 	EVALUATE_INSTR_NANOS6
	// } evaluation_mode;

	enum {
		RTL_INSTRUMENTATION_NONE = 0,
		RTL_INSTRUMENTATION_INTERCEPT,
		RTL_INSTRUMENTATION_PROCESS
	} rtl_instrumentation_mode;

	enum {
		ISA_INSTRUMENTATION_NONE = 0,
		ISA_INSTRUMENTATION_INTERCEPT,
		ISA_INSTRUMENTATION_PROCESS
	} isa_instrumentation_mode;

	// bool without_isa_stack;
	bool without_analysis;

	bool use_image_maps_cache;
	bool use_thread_logs;

	unsigned int buffer_size;

	enum {
		ENGINE_OFFLINE = 0,
		ENGINE_ONLINE,
	} engine;

	// Off-line engine options
	bool keep_traces;

	// On-line engine options
	enum {
		AGGREGATE_NONE = 0,
		AGGREGATE_PC,
		AGGREGATE_ADDR
		// AGGREGATE_TASK
	} aggregation_level;

	bool easy_output;

	bool frequent_processing;

	// Derived options
	const char *procmaps_dir;
	const char *imgmaps_dir;
	const char *traces_dir;
	const char *logs_dir;
} config;


INLINE
const char *config_engine_str(unsigned int engine) {
	switch (engine) {
		case config.ENGINE_ONLINE:
			return "ONLINE";
		break;
		case config.ENGINE_OFFLINE:
			return "OFFLINE";
		break;
		break;
		default:
			return "";
	}
}


INLINE
const char *config_report_level_str(unsigned int report_level) {
	switch (report_level) {
		case config.REPORT_DEBUG:
			return "DEBUG";
		break;
		case config.REPORT_NOTICE:
			return "NOTICE";
		break;
		case config.REPORT_WARNING:
			return "WARNING";
		break;
		default:
			return "";
	}
}


INLINE
const char *config_instrumentation_mode_str(unsigned int instrumentation_mode) {
	switch (instrumentation_mode) {
		case config.ISA_INSTRUMENTATION_NONE:
			return "NONE";
		break;
		case config.ISA_INSTRUMENTATION_INTERCEPT:
			return "INTERCEPT";
		break;
		case config.ISA_INSTRUMENTATION_PROCESS:
			return "PROCESS";
		break;
		default:
			return "";
	}
}
