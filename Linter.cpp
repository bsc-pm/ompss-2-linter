#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <algorithm>
#include <assert.h>
#include <sys/resource.h>

#include "pin.H"

extern "C" {
	#include "xed-interface.h"
}

//#include "lib/interval_tree.hpp"

#include "init.hpp"
#include "utils.hpp"
#include "types.hpp"
#include "prints.hpp"
#include "buffer.hpp"
#include "itvmap.hpp"
#include "process.hpp"
#include "symbols.hpp"
#include "deps.hpp"
#include "tasks.hpp"
#include "threads.hpp"
#include "backend.hpp"


// Register sets that will be read or written by PIN callbacks
// (currently the stack pointer is the only register being used
// for reading its contents)
REGSET r_regset, w_regset;

// Instrumentation states
typedef enum {
	// INSTR_STATE_CLEAN_NANOS6 = 0,
	// INSTR_STATE_CLEAN_FUNC,
	INSTR_STATE_CLEAN = 0,
	INSTR_STATE_INSTR,
	INSTR_STATE_INSTR_WITHSTACK
} ISTATE;

// Instrumentation state register
static REG istate_reg;

// Trace versions
enum {
	TRACE_VERSION_INSTR_WITHSTACK = 0,
	TRACE_VERSION_INSTR,
	TRACE_VERSION_CLEAN
};


// PIN key for accessing TLS in the threads (can't use C-ELF
// native TLS facilities inside PIN)
TLS_KEY tls_key;


/* ------------------------------------------------------ */
/* Command-line Switches                                  */
/* ------------------------------------------------------ */

KNOB<string> KnobOutputDir(
	KNOB_MODE_WRITEONCE, "pintool", "o", ".",
		"specify output directory");

KNOB<string> KnobExperimentName(
	KNOB_MODE_WRITEONCE, "pintool", "exp", "pin",
		"specify experiment name");

KNOB<string> KnobReportLevel(
	KNOB_MODE_WRITEONCE, "pintool", "r", "WRN",
		"minimum report level (DBG < NTC < WRN)");

KNOB<UINT64> KnobDebugLevel(
	KNOB_MODE_WRITEONCE, "pintool", "d", "0",
		"maximum debug level (when debug is enabled as report level)");

// KNOB<UINT64> KnobEvaluationMode(
// 	KNOB_MODE_WRITEONCE, "pintool", "s", "0",
// 		"0 - normal, 1 - don't process traces, 2 - don't log traces");

KNOB<UINT64> KnobRTLInstrumentationMode(
	KNOB_MODE_WRITEONCE, "pintool", "R", "2",
		"0 - none, 1 - instrument, 2 - process");

KNOB<UINT64> KnobISAInstrumentationMode(
	KNOB_MODE_WRITEONCE, "pintool", "I", "2",
		"0 - none, 1 - instrument, 2 - process");

// KNOB<bool> KnobWithoutISAStack(
// 	KNOB_MODE_WRITEONCE, "pintool", "W-ISA-STACK", "false",
// 		"don't instrument stack instructions");

KNOB<bool> KnobWithoutAnalysis(
	KNOB_MODE_WRITEONCE, "pintool", "WA", "false",
		"don't analyze traces");

KNOB<bool> KnobUseImageMapsCache(
	KNOB_MODE_WRITEONCE, "pintool", "c", "false",
		"use cached image maps if possible");

KNOB<bool> KnobUseThreadLogs(
	KNOB_MODE_WRITEONCE, "pintool", "g", "false",
		"use per-thread logs");

KNOB<UINT64> KnobBufferSize(
	KNOB_MODE_WRITEONCE, "pintool", "b", "1024",
		"per-thread buffer size (number of OS pages)");

KNOB<string> KnobEngine(
	KNOB_MODE_WRITEONCE, "pintool", "E", "online",
		"switch engine: 'online' uses interval trees and doesn't write to disk, 'offline' fills a buffer and then dumps accesses to disk");

// Only when the engine is "offline"
KNOB<bool> KnobKeepTraces(
	KNOB_MODE_WRITEONCE, "pintool", "k", "false",
		"keep traces in stable storage after processing");

// Only when the engine is "online"
KNOB<UINT64> KnobAggregationLevel(
	KNOB_MODE_WRITEONCE, "pintool", "a", "0",
		"(0) no aggregation, (1) aggregate on ip, (2) aggregate on ip + addr");

// Only when the engine is "online"
KNOB<bool> KnobEasyOutput(
	KNOB_MODE_WRITEONCE, "pintool", "e", "false",
		"ease the reading of the output produced by the tool (aggregate tasks)");

// Only when the engine is "online"
KNOB<bool> KnobFrequentProcessing(
	KNOB_MODE_WRITEONCE, "pintool", "f", "false",
		"performs the processing of tracing more often");

/* ------------------------------------------------------ */
/* Helper routines                                        */
/* ------------------------------------------------------ */


// PIN version of TLS
#define PIN_GetThread() static_cast<thread_data_t *>(PIN_GetThreadData(tls_key, PIN_ThreadId()))
#define PIN_GetThreadE(id) static_cast<thread_data_t *>(PIN_GetThreadData(tls_key, id))


#define expect_TASK_NOT_NULL(task) \
	expect(task, "Task should not be null.");

#define expect_TASK_FOUND(task, taskid) \
	expect(task, "Unable to find task %u.", taskid);

#define expect_TASK_NOT_IGNORED(task, taskid) \
	expect(task != IGNORED_TASK, "Task %u should not be an ignored task.", taskid);

#define expect_TASK_NON_NULL_PARENT(task, taskid) \
	expect(task || taskid == 0, "Task %u should have a non-null parent.", taskid);


#if 0
// Invoked prior to executing Nanos6 functions to check if it is
// actually necessary to invoke a PIN callback for them
#define INS_InsertIfVersionCall(Instr) \
	INS_InsertIfCall(Instr, IPOINT_BEFORE, \
		(AFUNPTR) OnInstrVersionPredicate, \
		IARG_FAST_ANALYSIS_CALL, \
		IARG_REG_VALUE, istate_reg, \
		IARG_END);

// Invoked prior to executing generic functions to check if it is
// actually necessary to invoke a PIN callback for them
#define INS_InsertIfVersionCallFunc(Instr, Routine) \
	INS_InsertIfCall(Instr, IPOINT_BEFORE, \
		(AFUNPTR) OnInstrVersionPredicateFunc, \
		IARG_FAST_ANALYSIS_CALL, \
		IARG_REG_VALUE, istate_reg, \
		IARG_ADDRINT, RTN_Address(Routine), \
		IARG_END);
#endif


#if 0
ISTATE SwitchVersionRegValueEnter(task_data_t *task, ISTATE InstrState, bool has_ret) {
	tracing_state_t state = task_get_tracing_state(task);

	switch (state) {
		case TRACING_STATE_DEFAULT:
			// nanos6_lint_on_task_start
			return InstrState;

		case TRACING_STATE_ENABLED_NANOS6:
			return INSTR_STATE_CLEAN_NANOS6;

		case TRACING_STATE_SUSPENDED_FIRST_IMPLICIT:
		case TRACING_STATE_SUSPENDED_IMPLICIT:
			if (has_ret == false) {
				return INSTR_STATE_CLEAN_FUNC;
			} else {
				return INSTR_STATE_CLEAN;
			}

		case TRACING_STATE_SUSPENDED_FIRST_EXPLICIT:
		case TRACING_STATE_SUSPENDED_EXPLICIT:
			return INSTR_STATE_CLEAN;

		default:
			error("Unexpected tracing state '%s'",
				tracing_state_str(state));
			return INSTR_STATE_CLEAN;
	}
}


ISTATE SwitchVersionRegValueLeave(task_data_t *task, ISTATE InstrState) {
	tracing_state_t state = task_get_tracing_state(task);

	switch (state) {
		case TRACING_STATE_DEFAULT:
			// nanos6_lint_on_task_start
			return InstrState;

		case TRACING_STATE_ENABLED:
			return INSTR_STATE_INSTR;

		case TRACING_STATE_ENABLED_NANOS6:
			return INSTR_STATE_CLEAN_NANOS6;

		case TRACING_STATE_SUSPENDED_FIRST_IMPLICIT:
		case TRACING_STATE_SUSPENDED_IMPLICIT:
			// if (task->tracing.func_first_addr == addr) {
			// 	return INSTR_STATE_CLEAN;
			// } else {
				return INSTR_STATE_CLEAN;
			// }

		case TRACING_STATE_SUSPENDED_FIRST_EXPLICIT:
		case TRACING_STATE_SUSPENDED_EXPLICIT:
			return INSTR_STATE_CLEAN;

		default:
			error("Unexpected tracing state '%s'",
				tracing_state_str(state));
			return INSTR_STATE_CLEAN;
	}
}
#endif


ISTATE SwitchInstrumentationState(ISTATE InstrState) {
	thread_data_t *thread = PIN_GetThread();

	ISTATE NewInstrState = INSTR_STATE_CLEAN;

	if (task_get_tracing_state(thread->task) == TRACING_STATE_ENABLED) {
		// if (thread->task->children.empty()) {
		if (!thread->task->stack_instr) {
			NewInstrState = INSTR_STATE_INSTR;
		} else {
			NewInstrState = INSTR_STATE_INSTR_WITHSTACK;
		}
	}

	return NewInstrState;
}

/* ------------------------------------------------------ */
/* Analysis routines                                      */
/* ------------------------------------------------------ */


VOID OnMainProgramStart(UINT64 argc, ADDRINT *argv, ADDRINT rsp_main) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	process_get_stack_boundaries(rsp_main);

	debug(2, NULL, NULL,
		"Process stack boundaries are <low_base=%p, low_main=%p, high=%p, max_high=%p>",
		app.stack_low_base, app.stack_low_main, app.stack_high, app.stack_low_base - app.stack_size);
}


/*
	Invoked whenever a new thread is created.
 */
VOID OnThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v) {
	thread_data_t *thread = thread_create(threadid, PIN_GetContextReg(ctxt, REG_STACK_PTR));

	debug(5, thread, NULL,
		"Started thread");

	if (PIN_SetThreadData(tls_key, thread, threadid) == FALSE) {
		error(thread, NULL,
			"PIN_SetThreadData failed");
	}

	thread->idle_task = task_create(thread, -1, "", 0);

	// NOTE: The pin client lock is obtained during the call of
	// this callback, so we don't need to protect the access
	// to the threads list
	app.threads.push_back(thread);
}


/*
	Invoked whenever an existing thread is destroyed.
 */
VOID OnThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v) {
	thread_data_t *thread = static_cast<thread_data_t *>(PIN_GetThreadData(tls_key, threadid));

	debug(5, thread, NULL,
		"Stopped thread with %u/%u saved accesses",
		thread->zeros, thread->calls);

	task_destroy(thread->idle_task);

	// NOTE: The pin client lock is obtained during the call of
	// this callback, so we don't need to protect the access
	// to the threads list
	app.threads.remove(thread);

	thread_destroy(thread);
}


VOID OnSysEnter(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	if (thread->task) {
		// NOTE: We are ignoring memory allocations that don't pass
		// through malloc/free or pragma declarations
		return;
	}

	unsigned int num = PIN_GetSyscallNumber(ctxt, std);
	ADDRINT iptr = PIN_GetContextReg(ctxt, REG_INST_PTR);

	thread->dymreq.iptr = iptr;

	switch (num) {
		case 9:
			// mmap
			thread->dymreq.addr = PIN_GetSyscallArgument(ctxt, std, 0);
			thread->dymreq.size = PIN_GetSyscallArgument(ctxt, std, 1);
			thread->dymreq.type = DYNMEM_REQUEST_MMAP;
			break;

		case 11:
			// munmap
			thread->dymreq.addr = PIN_GetSyscallArgument(ctxt, std, 0);
			thread->dymreq.size = PIN_GetSyscallArgument(ctxt, std, 1);
			thread->dymreq.type = DYNMEM_REQUEST_MUNMAP;
			break;

		case 12:
			// brk
			thread->dymreq.addr = PIN_GetSyscallArgument(ctxt, std, 0);
			thread->dymreq.size = 0;
			thread->dymreq.type = DYNMEM_REQUEST_BRK;
			break;

		default:
			thread->dymreq.type = DYNMEM_REQUEST_NONE;
			break;
	}
}


VOID OnSysExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	if (thread->task) {
		return;
	}

	ADDRINT ret = PIN_GetSyscallReturn(ctxt, std);

	if (ret == (ADDRINT) -1) {
		thread->dymreq.type = DYNMEM_REQUEST_NONE;
		return;
	}

	// img_t *img = image_find_by_addr(thread->dymreq.iptr);

	// if (img->level == IMG_LEVEL_LOW) {
	// 	thread->dymreq.type = DYNMEM_REQUEST_NONE;
	// 	return;
	// }

	// bool res;
	// itvmap_t<dym_entry_t> *dym_map;

	// if (thread_in_task(thread) && task_in_tracing_mode(thread->task)) {
	// 	dym_map = &thread->task->dym_map;
	// }
	// else if (thread_in_task(thread)) {
		// dym_map = &thread->dym_map;
	// }
	// else {
	// 	dym_map = &app.dym_map;
	// }

	uint64_t low, high;

	switch (thread->dymreq.type) {

		case DYNMEM_REQUEST_MMAP:
			// debug(3, NULL, NULL,
			// 	"Detected MMAP allocation <%p, %p> by %s",
			// 	ret, ret + thread->dymreq.size, img->name);

			// res = dynmem_register(
			// 	ret, ret + thread->dymreq.size, DYM_TYPE_MMAP,
			// 	img, thread->task, dym_map);

			// if (thread_in_task(thread) && task_in_tracing_mode(thread->task) && res) {
			// 	task_save_raw_access(thread->task, ITV_MODE_ALLOC,
			// 		XED_ICLASS_LAST, thread->dymreq.iptr, ret, thread->dymreq.size,
			// 		img->id, 0, 0);
			// }
			break;

		case DYNMEM_REQUEST_MUNMAP:
			// debug(3, NULL, NULL,
			// 	"Detected MUNMAP deallocation <%p, %p> by %s",
			// 	thread->dymreq.addr, thread->dymreq.addr + thread->dymreq.size, img->name);

			// res = dynmem_unregister(
			// 	thread->dymreq.addr, thread->dymreq.addr + thread->dymreq.size, DYM_TYPE_MMAP,
			// 	dym_map);

			// if (thread_in_task(thread) && task_in_tracing_mode(thread->task) && res) {
			// 	task_save_raw_access(thread->task, ITV_MODE_FREE,
			// 		XED_ICLASS_LAST, thread->dymreq.iptr, thread->dymreq.addr, thread->dymreq.size,
			// 		img->id, 0, 0);
			// }

			low = thread->dymreq.addr;
			high = low + thread->dymreq.size;

			for (auto &it : app.images) {
				img_t *target_img = it;

				if (target_img->extent_low > high || target_img->extent_high < low) {
					continue;
				}

				//expect(target_img->invalid,
				//	"Trying to de-allocate memory for an image that wasn't unregistered.");

				// debug(4, NULL, NULL,
				// 	"De-allocation <%p, %p> by %s involves image %s",
				// 	(void *)low, (void *)high, img->name, target_img->name);

				// image_unregister(target_img);
			}
			break;

		case DYNMEM_REQUEST_BRK:
			// debug(3, NULL, NULL,
			// 	"Detected BRK allocation <?, %p>",
			// 	thread->dymreq.addr);
			// TODO: registering BRK allocation and de-allocation
			// Intercept first call to detect the current program break
			break;

		case DYNMEM_REQUEST_NONE:
			break;

		default:
			error(thread, NULL,
				"Unexpected memory allocation service.");
	}

	thread->dymreq.type = DYNMEM_REQUEST_NONE;
}


#if 0
BOOL PIN_FAST_ANALYSIS_CALL OnInstrVersionPredicate(ISTATE InstrState) {
	return false
		|| InstrState == INSTR_STATE_CLEAN_NANOS6
		|| InstrState == INSTR_STATE_INSTR
		|| InstrState == INSTR_STATE_INSTR_WITHSTACK
	;
}


BOOL PIN_FAST_ANALYSIS_CALL OnInstrVersionPredicateFunc(ISTATE InstrState, ADDRINT rtn_addr) {
	thread_data_t *thread = PIN_GetThread();

	return false
		|| (thread->task && thread->task->tracing.func_first_addr == rtn_addr)
		|| InstrState == INSTR_STATE_CLEAN_FUNC
		|| InstrState == INSTR_STATE_INSTR
		|| InstrState == INSTR_STATE_INSTR_WITHSTACK
	;
}
#endif


// VOID OnTaskVerified(void) {
// 	thread_data_t *thread = PIN_GetThread();

// 	// The main task cannot be marked as verified, so there must be
// 	// a valid binding to the current thread (i.e., a parent task)
// 	expect_TASK_NOT_NULL(thread->task);

// 	if (task_tracing_is_not_enabled(thread->task)) {
//         return;
// 	}

// 	thread->next_task_verified = true;
// }


UINT64 OnTaskCreate(UINT64 taskid, VOID *invocation_info, UINT64 flags, ADDRINT rsp, ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();

	// NOTE: At the beginning, i.e. when the main task is about to
	// be created, there no task in the system, so it is allowed
	// to have NULL thread->task value. This is also true when
	// a worker thread is about to execute its first task ever.

	task_data_t *task;
	const char *invocationpoint = taskid == 0 ? "Main task" : *(const char **)invocation_info;

	thread->submit_taskid = taskid;

	if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		// Tasks which are created inside ignored regions are not
		// traced by the tool, but establish a thread->task binding
		// anyway to simplify handling errors in the remaining code.
		// task = task_create_ignored(thread, thread->submit_taskid);
		task = IGNORED_TASK;

		thread->created_task = task;

		task_register(taskid, task);
	}
	else {
		task = task_create(thread, taskid, invocationpoint, flags);
		expect_TASK_NOT_NULL(task);

		thread->created_task = task;

		// The task is globally visible to other threads
		task_register(taskid, task);

		// The default task state is 'TASK_STATE_NONE'
		task_set_state(task, TASK_STATE_CREATED);

		// if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		// 	task_set_tracing_state(task, TRACING_STATE_DISABLED_PERMANENTLY);

		// 	task_add_child(thread->task, task);
		// } else

		if (thread->task) {
			task_add_child(thread->task, task);

			if (task_is_verified(task)) {
				task_set_tracing_state(task, TRACING_STATE_DISABLED_VERIFIED);
			}

			// thread->next_task_verified = true;

			// if (task_tracing_is_not_enabled(task->parent)) {
			// 	// The current task will be living in an ignored region,
			// 	// which means that tracing will never be enabled
			// 	task->tracing.current_state = TRACING_STATE_DISABLED_PERMANENTLY;
			// }

			task_advance_time(thread->task);
		}
	}

	debug(2, thread, thread->task,
		"Task %u '%s' %p created by %d (initial tracing state: %s, final: %d)",
		taskid, invocationpoint, task,
		thread->task ? (int64_t)thread->task->id : -1,
		task == IGNORED_TASK
			? tracing_state_str(TRACING_STATE_DISABLED_PERMANENTLY)
			: tracing_state_str(task_get_tracing_state(task)),
		task == IGNORED_TASK
			? false
			: task_is_final(task));

	// Accesses that occur in between the creation of a task and
	// its submission to Nanos6 are ignored
	// NOTE: This optimization suppresses warnings coming from
	// the case of "implicit accesses" (see testcases/test4)
	return INSTR_STATE_CLEAN;
}


/*
	Invoked whenever a new task is being created to register the
	base address and size of the argsBlock structure for that task.
	Notice that this has two effects:
	1) The newly-created task registers a new artificial dependency,
	   because it will read/write to its own argsBlock structure
	2) The parent task registers, too, a new artificial dependency,
	   because it must prepare the values passed as input to the
	   newly-created task
 */
VOID OnTaskArgsBlockReady(ADDRINT ptr, UINT64 orig_size, UINT64 size) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	// See 'OnTaskCreate' on why thread->task can be NULL here.
	// The first task is created when the thread is not running
	// any task, but in all other cases it is wrong to create
	// a task outside of another task scope
	expect_TASK_NON_NULL_PARENT(thread->task, thread->submit_taskid);

	if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		// The task is an ignored task, nothing to do here
		return;
	}
	else if (thread->submit_taskid == 0) {
		// We are submitting the main task, nothing to do here
		return;
	}

	// task_data_t *task = task_find_by_id(thread->submit_taskid);
	task_data_t *task = thread->created_task;
	expect_TASK_FOUND(task, thread->submit_taskid);
	expect_TASK_NOT_IGNORED(task, thread->submit_taskid);

	if (size == 0) {
		return;
	}

	// The child task will see an artificial dependency because
	// it read-writes to its own argsBlock structure to
	// communicate in both directions with the external world.
	UINT64 ranges[1][3] = { { size, 0, size } };

	task->argsblock_dep = dep_create(
		DEP_NORMAL, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"argsBlock", ptr, ranges, 1);

	// task_register_dep(task, thread, task->argsblock_dep);

	// if (thread->submit_taskid > 0) {
	// 	// The parent task registers as its own dependency the args block
	// 	// of its child task. It creates such an artificial dependency
	// 	// because it must write to the newly-created task's argsBlock
	// 	// structure all the required input values.
	// 	UINT64 ranges[1][3] = { { size, 0, size } };

	// 	thread->task->argsblock_child_dep = dep_create(
	// 		DEP_NORMAL, (dep_mode_t) (0UL | DEP_WRITE | DEP_FAKE),
	// 		"argsBlock_child", ptr, ranges, 1);

	// 	task_register_dep(thread->task, thread, thread->task->argsblock_child_dep);
	// 	task_register_child_dep(thread->task, thread, task->argsblock_dep);
	// }
}


VOID OnTaskDependencyRegistrationCommon(dep_type_t type, dep_mode_t mode,
                                        char const *name, ADDRINT baddr,
                                        UINT64 ranges[][3], UINT64 ndim) {
	thread_data_t *thread = PIN_GetThread();

	// Except for the main task, if we are registering dependencies,
	// then it means that there must be a binding between a thread
	// and a parent task
	expect_TASK_NOT_NULL(thread->task);

	if (thread->task && task_tracing_is_suspended(thread->task)) {
		// If it is a RELEASE dependency and the task is not tracing,
		// then we ignore the dependency registration.
		// If it is a NORMAL dependency and the parent task is not
		// tracing, then we must ignore the dependency registration.
		return;
	}

	// TODO: Why I don't check tracing_is_disabled?

	expect((mode & DEP_FAKE) == 0,
		"Dependency mode should not be FAKE.");

	// A RELEASE dependency is registered for the target task by
	// the task itself while it is executing.
	// A NORMAL dependency is registered for the target task by
	// its parent when the task is about to be submitted.
	task_data_t *task;

	switch (type) {
		case DEP_NORMAL:
			// task = task_find_by_id(thread->submit_taskid);
			task = thread->created_task;
			break;

		case DEP_RELEASE:
			task = thread->task;
			break;

		default:
			task = NULL;
			error(thread, NULL,
				"Unrecognized dependency mode.");
	}

	if (task == IGNORED_TASK) {
		// The submitted task is an ignored task, so we don't have
		// to register anything for it
		return;
	}

	if (type == DEP_RELEASE && config.frequent_processing && config.without_analysis == false) {
		// Empty the buffer and consume traces. This is done to simplify
		// the processing of traces. A 'release' directive splits a
		// trace in different segments, so that everything that happened
		// before it must be processed ignoring the 'release', while
		// everything that will happen after must take it into account.
		// The easiest way to do this without including additional
		// information in the tracefile (e.g., which dependencies are no
		// longer valid) is simply to process the existing trace as
		// soon as a 'release' directive is encountered.
		// task_accessmap_update(task);
		task_flush_raw_accesses(task);
		task_load_raw_accesses(task);
		{
			match_acc_with_child_deps(task);
			match_acc_with_deps(task);
		}
		// task_accessmap_reset(task);
	}

	dep_t *dep = dep_create(type, mode, name, baddr, ranges, ndim);

	task_register_dep(task, thread, dep);

	task_advance_time(task);

	if (type == DEP_RELEASE && config.without_analysis == false) {
		// The 'release' directive now disables some normal deps
		// in the task, if any. If there is no match, it is reported
		match_release_dep_with_normal_deps(dep, task);
	}
	else {
		task_register_child_dep(task->parent, thread, dep);

		if (!task->parent->stack_instr
			&& dep->extent_low <= task->stack_parent_dep->extent_high
			&& dep->extent_high >= task->stack_parent_dep->extent_low) {
			// This dependency is contained within the parent stack,
			// so there could be data races with stack operations in
			// the parent: enable stack instrumentation
			task->parent->stack_instr = true;
		}

		if (task->parent && task->parent->id > 0 && config.without_analysis == false) {
			// If there is a parent task which is NOT the 'main' task,
			// then check if the current dependency is matched by any
			// strong or weak normal dependency in the parent task
			match_child_dep_with_parent_deps(dep, task);
		}
	}

	task_advance_time(thread->task);
}


/*
	Registers a new 1-dimension dependency.
 */
VOID OnTaskDependencyRegistration1(dep_type_t type, dep_mode_t mode,
                                   char const *name, ADDRINT baddr,
                                   UINT64 size1, UINT64 start1, UINT64 end1) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[1][3] = DEP_DIMENSION_ARRAY(1, size, start, end);

	OnTaskDependencyRegistrationCommon(type, mode, name, baddr, ranges, 1);
}


/*
	Registers a new 2-dimension dependency.
 */
VOID OnTaskDependencyRegistration2(dep_type_t type, dep_mode_t mode,
                                   char const *name, ADDRINT baddr,
                                   UINT64 size1, UINT64 start1, UINT64 end1,
                                   UINT64 size2, UINT64 start2, UINT64 end2) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[2][3] = DEP_DIMENSION_ARRAY(2, size, start, end);

	OnTaskDependencyRegistrationCommon(type, mode, name, baddr, ranges, 2);
}


/*
	Registers a new 3-dimension dependency.
 */
VOID OnTaskDependencyRegistration3(dep_type_t type, dep_mode_t mode,
                                   char const *name, ADDRINT baddr,
                                   UINT64 size1, UINT64 start1, UINT64 end1,
                                   UINT64 size2, UINT64 start2, UINT64 end2,
                                   UINT64 size3, UINT64 start3, UINT64 end3) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[3][3] = DEP_DIMENSION_ARRAY(3, size, start, end);

	OnTaskDependencyRegistrationCommon(type, mode, name, baddr, ranges, 3);
}


/*
	Registers a new 4-dimension dependency.
 */
VOID OnTaskDependencyRegistration4(dep_type_t type, dep_mode_t mode,
                                   char const *name, ADDRINT baddr,
                                   UINT64 size1, UINT64 start1, UINT64 end1,
                                   UINT64 size2, UINT64 start2, UINT64 end2,
                                   UINT64 size3, UINT64 start3, UINT64 end3,
                                   UINT64 size4, UINT64 start4, UINT64 end4) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[4][3] = DEP_DIMENSION_ARRAY(4, size, start, end);

	OnTaskDependencyRegistrationCommon(type, mode, name, baddr, ranges, 4);
}


VOID OnTaskSubmitPre(ADDRINT rsp, ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	// if (InstrState == INSTR_STATE_CLEAN) {
	// 	return;
	// }

	thread_data_t *thread = PIN_GetThread();

	// See 'OnTaskCreate' on why thread->task can be NULL here
	expect_TASK_NON_NULL_PARENT(thread->task, thread->submit_taskid);

	if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		return;
	}
	else if (thread->submit_taskid == 0) {
		return;
	}

	// task_data_t *task = task_find_by_id(thread->submit_taskid);
	task_data_t *task = thread->created_task;
	expect_TASK_FOUND(task, thread->submit_taskid);
	expect_TASK_NOT_IGNORED(task, thread->submit_taskid);

	// Register the "stack" dependency
	// -------------------------------

	// It is used to avoid false positives when a dependency in
	// the child task is not covered by any dependency in the
	// parent task, because it belong to the parent own stack
	UINT64 size = thread->task->stackbase - rsp;
	UINT64 ranges[1][3] = { { size, 0, size } };

	thread->task->stack_dep = dep_create(
		DEP_NORMAL, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"stack", rsp, ranges, 1);

	task_register_dep(thread->task, thread, thread->task->stack_dep);

	task_advance_time(thread->task);

	// We create a special dependency representing the parent stack,
	// but we don't register it because accesses to the parent stack
	// that aren't explicitly protected by a user dependency
	// represent a potential data race
	task->stack_parent_dep = dep_create(
		DEP_NORMAL, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"stack_parent", rsp, ranges, 1);

	// FIXME: If the task has to be ignored, we can save computation
	task_set_state(task, TASK_STATE_SUBMITTED);

	// Handle partial task wait if the child task is an if0 task
	// ---------------------------------------------------------

	if (task_is_if0(task) && config.frequent_processing && config.without_analysis == false) {
		// task_accessmap_update(task->parent);
		task_flush_raw_accesses(task->parent);
		task_load_raw_accesses(task->parent);
		{
			match_acc_with_child_deps(task->parent);
			match_acc_with_deps(task->parent);
		}
		// task_accessmap_mayrace_reset(task->parent);
	}


	// Handle verified tasks
	// ---------------------

	// if (thread->next_task_verified) {
	// 	task_data_t *task = task_find_by_id(thread->submit_taskid);
	// 	expect_TASK_FOUND(task, thread->submit_taskid);

	// 	task_set_tracing_state(task, TRACING_STATE_DISABLED_VERIFIED);

	// 	debug(3, thread, task,
	// 		"Task %u %p is considered verified: tracing disabled",
	// 		task->id, task);

	// 	thread->next_task_verified = false;
	// }
}


UINT64 OnTaskSubmitPost(ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	// if (InstrState == INSTR_STATE_CLEAN) {
	// 	return InstrState;
	// }

	thread_data_t *thread = PIN_GetThread();

	// See 'OnTaskCreate' on why thread->task can be NULL here
	expect_TASK_NON_NULL_PARENT(thread->task, thread->submit_taskid);

	if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		return INSTR_STATE_CLEAN;
	}
	else if (thread->submit_taskid == 0) {
		return INSTR_STATE_CLEAN;
	}

	// task_data_t *task = task_find_by_id(thread->submit_taskid);
	task_data_t *task = thread->created_task;
	expect_TASK_FOUND(task, thread->submit_taskid);
	expect_TASK_NOT_IGNORED(task, thread->submit_taskid);

	if (task_is_if0(task)) {
		// If0 task: it means that it acts as a partial sync barrier
		// for all its dependencies

		if (config.without_analysis) {
			// Previous sibling tasks are updated to record the fact that
			// some dependencies are not active anymore.
			match_child_deps_with_sibling_deps(task);
		}

		task_advance_time(thread->task);
	}

	// Unregister ArgsBlockChild dependency
	// ------------------------------------

	// if (thread->task->argsblock_child_dep && config.frequent_processing) {
	// 	// Empty the buffer and consume traces. This is done to simplify
	// 	// the processing of traces. Any subsequent access to the child
	// 	// task's argsBlock structure must be treated as an error, hence
	// 	// the easiest way to do this without having to store additional
	// 	// things in the tracefile is just to consume whatever there
	// 	// is inside it upon the occurrence of this event.
	// 	task_accessmap_update(thread->task);
	// 	{
	// 		match_acc_with_child_deps(task->parent);
	// 		match_acc_with_deps(thread->task);
	// 	}
	// 	task_accessmap_reset(thread->task);
	// 	task_accessmap_mayrace_reset(task->parent);

	// 	task_unregister_dep(thread->task, thread, thread->task->argsblock_child_dep);
	// 	dep_destroy(thread->task->argsblock_child_dep);
	// 	thread->task->argsblock_child_dep = NULL;
	// }


	// Unregister stack dependency
	// ---------------------------

	if (thread->task->stack_dep) {
		task_unregister_dep(thread->task, thread, thread->task->stack_dep);
		dep_destroy(thread->task->stack_dep);
		thread->task->stack_dep = NULL;
	}

	if (config.frequent_processing
		/* || *thread->task->next_children_it == task */
		&& config.without_analysis == false) {
		// We just got out of a race-free zone, so we discard all
		// accesses up to this point because they cannot race with
		// any child task

		// task_accessmap_update(thread->task);
		task_flush_raw_accesses(thread->task);
		task_load_raw_accesses(thread->task);
		{
			match_acc_with_child_deps(thread->task);
			match_acc_with_deps(thread->task);
		}
		// task_accessmap_reset(thread->task);
		// task_accessmap_mayrace_reset(thread->task);
	}

	// // Handle verified tasks
	// // ---------------------

	// if (thread->next_task_verified) {
	// 	task_data_t *task = task_find_by_id(thread->submit_taskid);
	// 	expect_TASK_FOUND(task, thread->submit_taskid);

	// 	task_set_tracing_state(task, TRACING_STATE_DISABLED_VERIFIED);

	// 	debug(3, thread, task,
	// 		"Task %u %p is considered verified: tracing disabled",
	// 		task->id, task);

	// 	thread->next_task_verified = false;
	// }

	//printf("CREATE InstrState for task %u is %u\n", thread->task->id, SwitchInstrumentationState(InstrState));
	return SwitchInstrumentationState(InstrState);
}


/*
	Invoked whenever a new task is about to execute. The respective
	task meta-data structure was already allocated when the task
	was created by its parent, so it must only be retrieved.
 */
VOID OnTaskExecutionPre(UINT64 taskid) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	// See 'OnTaskCreate' on why thread->task can be NULL here
	// Additionally, see OnTaskCreate' on why it is okay not to
	// have a task scope to execute a task (as in the case of an
	// idle worker thread).

	task_data_t *task = task_find_by_id(taskid);

	/* if (task == NULL) {
		// Sometimes we skip OnTaskCreate, so we need to register the
		// task ID immediately prior to execution
		task_register(taskid, IGNORED_TASK);
		task = thread->idle_task;
	}
	else */
	if (task == IGNORED_TASK) {
		task = thread->idle_task;
	}
	else {
		expect_TASK_FOUND(task, taskid);
	}

	debug(3, thread, task,
		"About to execute task %u %p, suspending task %u %p",
		taskid, task,
		thread->task ? thread->task->id : -1, thread->task);

	// From here on, the thread is assigned a new task to execute
	thread_push_task(thread, task);

	task_bind_to_thread(task, thread);
}


/*
	Invoked whenever a task is about to be destroyed. The buffer
	is flushed and all traces from the tracefile are processed.
	The associated task metadata structure is destroyed, too.
 */
VOID OnTaskExecutionPost(UINT64 taskid) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	expect_TASK_NOT_NULL(thread->task);

	// if (task_tracing_is_not_enabled(thread->task)) {
	// 	return;
	// }

#ifndef NDEBUG
	task_data_t *task = thread->task;
#endif

	// From here on, the thread is assigned the previous task, if
	// any, to resume its execution
	thread_pop_task(thread);

	debug(3, thread, task,
		"About to terminate task %u %p, resuming task %u %p",
		taskid, task,
		thread->task ? thread->task->id : -1, thread->task);
}


// VOID OnTaskUnpackDepsPre(ADDRINT rsp, ISTATE InstrState, const char *rtn_name, ADDRINT rtn_addr) {
// 	thread_data_t *thread = PIN_GetThread();

// 	printf("%d Unpacked deps pre\n", thread->id);
// }

// VOID OnTaskUnpackDepsPost(ISTATE InstrState) {
// 	thread_data_t *thread = PIN_GetThread();

// 	printf("%d Unpacked deps post\n", thread->id);
// }


UINT64 OnTaskConcreteExecutionPre(ADDRINT rsp, ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();

	// The binding has been set in 'OnTaskExecutionPre', therefore
	// thread->task cannot be NULL
	expect_TASK_NOT_NULL(thread->task);

	if (task_get_state(thread->task) == TASK_STATE_NONE) {
		// We are executing an ignored task
		return INSTR_STATE_CLEAN;
	}

	expect(task_get_tracing_state(thread->task) < TRACING_STATE_ENABLED,
		"Unexpected tracing state '%s' for task %u.",
		tracing_state_str(task_get_tracing_state(thread->task)),
		thread->task->id);

	debug(3, thread, thread->task,
		"Executing task %u %p (%s)",
		thread->task->id, thread->task, tracing_state_str(task_get_tracing_state(thread->task)));

	if (task_tracing_is_not_enabled(thread->task)) {
		return INSTR_STATE_CLEAN;
	}

	task_set_state(thread->task, TASK_STATE_STARTED);

	// FIXME: avoid setting the stackbase directly
	thread->task->stackbase = rsp;

	// If the tracing state is disabled for other reasons, then
	// clearly we don't want tracing to be enabled for this task
	if (task_get_tracing_state(thread->task) == TRACING_STATE_DEFAULT) {
		task_enable_tracing(thread->task, thread);

		debug(3, thread, thread->task,
			"Tracing enabled, tracefile '%s'",
			thread->task->tracename);
	}

	// return SwitchVersionRegValue(thread->task);
	return SwitchInstrumentationState(InstrState);
}


UINT64 OnTaskConcreteExecutionPost(ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	if (task_get_state(thread->task) == TASK_STATE_NONE) {
		// We are executing an ignored task
		return INSTR_STATE_CLEAN;
	}

	expect(
		task_get_tracing_state(thread->task) == TRACING_STATE_ENABLED ||
		task_tracing_is_disabled(thread->task),
		"Unexpected tracing state '%s' for task %u.",
			tracing_state_str(task_get_tracing_state(thread->task)),
			thread->task->id
	);

	// if (thread->task && task_tracing_is_not_enabled(thread->task)) {
	// 	return INSTR_STATE_CLEAN;
	// }

	debug(3, thread, thread->task,
		"Terminating task %u %p (%s)",
		thread->task->id, thread->task, tracing_state_str(task_get_tracing_state(thread->task)));

	task_set_state(thread->task, TASK_STATE_TERMINATED);

	// If tracing was enabled for this task, we have some things
	// left to do before terminating the task itself
	if (task_get_tracing_state(thread->task) == TRACING_STATE_ENABLED) {

		// When the task terminates, we flush all the accesses
		// which remained in the buffer, regardless of whether we
		// perform the analysis. This is done to achieve fairness
		// in experiments with analysis disabled.
		task_flush_raw_accesses(thread->task);
		task_load_raw_accesses(thread->task);

		if (config.without_analysis == false) {
			// Process the remaining accesses
			// ------------------------------

			{
				match_acc_with_child_deps(thread->task);
				match_acc_with_deps(thread->task);

				check_unmatched_normal_strong_deps(thread->task);
				check_unmatched_normal_weak_deps(thread->task);

				close_concurrent_region(thread->task);
			}
			// task_accessmap_reset(thread->task);
			// task_accessmap_mayrace_reset(thread->task);

			// Report all mismatches for this task
			// -----------------------------------

			task_report_matches(thread->task);
		}
	}

	else if (task_get_tracing_state(thread->task) == TRACING_STATE_DISABLED_VERIFIED) {
		if (config.without_analysis == false) {
			// Report all mismatches for this task
			// -----------------------------------
			// NOTE: We will only report matches that relate to the
			// parent task, i.e., right now only if the parent is missing
			// a (weak) dependency

			task_report_matches(thread->task);
		}
	}

	if (task_get_tracing_state(thread->task) == TRACING_STATE_ENABLED) {
		// Disable tracing
		// ---------------

		task_disable_tracing(thread->task);

		debug(3, thread, thread->task,
			"Tracing disabled, tracefile '%s'",
			thread->task->tracename);
	}

	// return INSTR_STATE_CLEAN_NANOS6;
	return INSTR_STATE_CLEAN;
}


VOID OnTaskFinalClauseEvaluation(UINT64 is_final) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		return;
	}

	if (is_final > 0 && thread->task->id > 0 && config.without_analysis == false) {
		debug(3, thread, thread->task,
			"Task is final, promoting weak deps to strong deps");

		// It's a final task: all weak dependencies must be promoted
		// to strong dependencies!
		promote_weak_deps_to_strong(thread->task);
	}
}


VOID OnTaskWaitEnter(UINT64 taskid, const CHAR *invocationSource, UINT64 taskid_if0) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	thread->taskwait_task_id = taskid_if0;

	if (thread->task && task_tracing_is_not_enabled(thread->task)) {
		return;
	}

	if (thread->taskwait_task_id != -1UL) {
		// A partial taskwait implemented as an if0 task, return
		return;
	}

	// if (thread->task && task_tracing_is_suspended(thread->task)) {
	// 	return;
	// }

	if (config.without_analysis == false) {
		if (config.frequent_processing) {
			// Upon a taskwait, accesses that may potentially race with
			// child tasks are evaluated and potential data races are
			// reported. After this action, a new race-free zone begins.
			// task_accessmap_update(thread->task);
			task_flush_raw_accesses(thread->task);
			task_load_raw_accesses(thread->task);
			{
				match_acc_with_child_deps(thread->task);
				match_acc_with_deps(thread->task);
			}
			// task_accessmap_mayrace_reset(thread->task);
		}

		close_concurrent_region(thread->task);
	}

	thread->task->race_free_zone = true;
	thread->task->stack_instr = false;

	task_advance_time(thread->task);
}


UINT64 OnTaskWaitExit(UINT64 taskid, ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	if (thread->taskwait_task_id != -1UL) {
		// A partial taskwait implemented as an if0 task, return
		return SwitchInstrumentationState(InstrState);
	}

	if (taskid == 0 && config.without_analysis == false) {
		// If it is the main task and there is a taskwait,
		// we process all traces up to this point
		// (this is especially useful for benchmarks...)
		task_flush_raw_accesses(thread->task);
		task_load_raw_accesses(thread->task);
		{
			match_acc_with_child_deps(thread->task);
			match_acc_with_deps(thread->task);

			check_unmatched_normal_strong_deps(thread->task);
			check_unmatched_normal_weak_deps(thread->task);

			close_concurrent_region(thread->task);
		}
	}

	thread->task->race_free_zone = true;
	thread->task->stack_instr = false;

	return SwitchInstrumentationState(InstrState);
}


VOID OnIgnoredRegionAccessRegistrationCommon(dep_mode_t depmode, ADDRINT retip, ADDRINT baddr,
                                             UINT64 ranges[][3], UINT64 ndim) {
	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	if (task_get_tracing_state(thread->task) != TRACING_STATE_SUSPENDED_FIRST_EXPLICIT) {
		// If we are in a nested ignore region we are NOT interested
		// in simulating accesses
		return;
	}

	// This is a trick, we create a dependency to exploit the logic
	// which generates fragment, but then we delete it because we
	// convert it into memory accesses to be saved into the buffer
	//
	// FIXME: "DEP_FAKE" needed to avoid triggering the execution
	// of some checks in dep_register... DEP_FAKE should become a
	// dep_type_t value?
	dep_t *lint_access_dep = dep_create(
		DEP_NORMAL, (dep_mode_t)(depmode | DEP_FAKE), "lint_access", baddr, ranges, ndim);

	char dep_string[MAX_MSG_LENGTH];
	dep_str(lint_access_dep, dep_string, MAX_MSG_LENGTH, thread, thread->task);

	debug(2, thread, thread->task,
		"Registered access %s",
		dep_string);

#ifndef NDEBUG
	// FIXME: Hide implementation of dep ranges
	for (auto &i : lint_access_dep->itvmap) {
		dep_entry_t *value = &i.second;

		debug(3, thread, thread->task,
			"Registered expected range %s [%lu:%lu] <%p:%p> (%lu, %lu)",
			itv_mode_str(value->itv.mode),
			value->itv.low - lint_access_dep->extent_low,
			value->itv.high - lint_access_dep->extent_low,
			value->itv.lowptr, value->itv.highptr,
			value->start_time.epoch, value->start_time.step
		);
	}
#endif

	img_t *img = image_find_by_addr(retip);

	for (auto &it : lint_access_dep->itvmap) {
		const itv_t *itv = &it.first;

		if (itv->mode == ITV_MODE_READ) {
			task_save_raw_access(thread->task, ITV_MODE_READ,
				XED_ICLASS_MOV, retip, itv->low, itv->high - itv->low, img->id, 0, 0);
		}
		else if (itv->mode == ITV_MODE_WRITE) {
			task_save_raw_access(thread->task, ITV_MODE_WRITE,
				XED_ICLASS_MOV, retip, itv->low, itv->high - itv->low, img->id, 0, 0);
		}
		else {
			// All other fragments type are irrelevant
		}
	}

	dep_destroy(lint_access_dep);
}


/*
	Registers a new 1-dimension access.
 */
VOID OnIgnoredRegionAccessRegistration1(dep_mode_t mode, ADDRINT retip, ADDRINT baddr,
                                        UINT64 size1, UINT64 start1, UINT64 end1) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[1][3] = DEP_DIMENSION_ARRAY(1, size, start, end);

	OnIgnoredRegionAccessRegistrationCommon(mode, retip, baddr, ranges, 1);
}


/*
	Registers a new 2-dimension access.
 */
VOID OnIgnoredRegionAccessRegistration2(dep_mode_t mode, ADDRINT retip, ADDRINT baddr,
                                        UINT64 size1, UINT64 start1, UINT64 end1,
                                        UINT64 size2, UINT64 start2, UINT64 end2) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[2][3] = DEP_DIMENSION_ARRAY(2, size, start, end);

	OnIgnoredRegionAccessRegistrationCommon(mode, retip, baddr, ranges, 2);
}


/*
	Registers a new 3-dimension access.
 */
VOID OnIgnoredRegionAccessRegistration3(dep_mode_t mode, ADDRINT retip, ADDRINT baddr,
                                        UINT64 size1, UINT64 start1, UINT64 end1,
                                        UINT64 size2, UINT64 start2, UINT64 end2,
                                        UINT64 size3, UINT64 start3, UINT64 end3) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[3][3] = DEP_DIMENSION_ARRAY(3, size, start, end);

	OnIgnoredRegionAccessRegistrationCommon(mode, retip, baddr, ranges, 3);
}


/*
	Registers a new 4-dimension access.
 */
VOID OnIgnoredRegionAccessRegistration4(dep_mode_t mode, ADDRINT retip, ADDRINT baddr,
                                        UINT64 size1, UINT64 start1, UINT64 end1,
                                        UINT64 size2, UINT64 start2, UINT64 end2,
                                        UINT64 size3, UINT64 start3, UINT64 end3,
                                        UINT64 size4, UINT64 start4, UINT64 end4) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	UINT64 ranges[4][3] = DEP_DIMENSION_ARRAY(4, size, start, end);

	OnIgnoredRegionAccessRegistrationCommon(mode, retip, baddr, ranges, 4);
}


UINT64 OnIgnoredRegionBegin(ADDRINT retip, const char *rtn_name, const char *img_name, ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	ISTATE NewInstrState;

	if (task_tracing_is_disabled(thread->task)) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else if (task_get_state(thread->task) == TASK_STATE_NONE) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else {
		// An ignored region is treated like a suspended tracing region,
		// with the addition that we need to count how deep we are in
		// an ignored region because we are only interested in what
		// happens in the outermost of such regions.
		bool state_change = task_enter_ignore_region(thread->task, retip, 0);

		// printf("%u BEGIN %s %s %u\n", thread->id, rtn_name, img_name, state_change);

		// NewInstrState = SwitchVersionRegValueEnter(thread->task, InstrState, 0);
		NewInstrState = INSTR_STATE_CLEAN;

		if (state_change) {
			debug(4, thread, thread->task,
				"Call to ignored region '%s' in '%s': new tracing state is %s, instrumentation state %d -> %d",
				rtn_name, img_name,
				tracing_state_str(task_get_tracing_state(thread->task)),
				InstrState, NewInstrState);
		}
	}

	//printf("BEGIN NEW InstrState is %u\n", NewInstrState);
	return NewInstrState;
}


UINT64 OnIgnoredRegionEnd(const char *rtn_name, const char *img_name, ISTATE InstrState) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	ISTATE NewInstrState;

	if (task_tracing_is_disabled(thread->task)) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else if (task_get_state(thread->task) == TASK_STATE_NONE) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else {
		// An ignored region is treated like a suspended tracing region,
		// with the addition that we need to count how deep we are in
		// an ignored region because we are only interested in what
		// happens in the outermost of such regions.
		bool state_change = task_leave_ignore_region(thread->task, 0);

		// printf("%u %p END %s %s %u\n", thread->id, thread->task, rtn_name, img_name, state_change);

		// NewInstrState = SwitchVersionRegValueLeave(thread->task, InstrState);
		NewInstrState = SwitchInstrumentationState(InstrState);

		if (state_change) {
			debug(4, thread, thread->task,
				"Return from ignored region '%s' in '%s': new tracing state is %s, instrumentation state %d -> %d",
				rtn_name, img_name,
				tracing_state_str(task_get_tracing_state(thread->task)),
				InstrState, NewInstrState);
		}
	}

	//printf("END NEW InstrState is %u\n", NewInstrState);
	return NewInstrState;
}


#if 0
UINT64 OnIgnoredFunctionRegionPre(ADDRINT retip, UINT64 addr, const char *rtn_name, const char *img_name,
	ISTATE InstrState /*, BOOL has_ret*/) {
	thread_data_t *thread = PIN_GetThread();

	ISTATE NewInstrState;

	printf("%u %p BEGIN FUNC %s %s ISTATE: %d\n", thread->id, thread->task, rtn_name, img_name, InstrState);

	if (thread->task == NULL) {
		NewInstrState = InstrState;
	}
	else if (task_tracing_is_disabled(thread->task)) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else if (task_get_state(thread->task) == TASK_STATE_NONE) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else {
		// printf("%u %p BEGIN FUNC %s %s ISTATE: %d\n", thread->id, thread->task, rtn_name, img_name, InstrState);
		bool state_change = task_enter_ignore_region(thread->task, retip, addr);

		// NewInstrState = SwitchVersionRegValueEnter(thread->task, InstrState, has_ret);
		NewInstrState = INSTR_STATE_CLEAN;

		if (state_change) {
			debug(4, thread, thread->task,
				"Call to '%s' in '%s': new tracing state is %s, instrumentation state %d -> %d",
				rtn_name, img_name,
				tracing_state_str(task_get_tracing_state(thread->task)),
				InstrState, NewInstrState);
		}
	}

	return NewInstrState;
}


UINT64 OnIgnoredFunctionRegionPost(UINT64 addr, const char *rtn_name, const char *img_name,
	ISTATE InstrState, BOOL has_ret) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return INSTR_STATE_CLEAN;
	}

	thread_data_t *thread = PIN_GetThread();

	ISTATE NewInstrState;

	printf("%u %p END FUNC %s %s ISTATE: %d\n", thread->id, thread->task, rtn_name, img_name, InstrState);

	if (thread->task == NULL) {
		NewInstrState = InstrState;
	}
	else if (task_tracing_is_disabled(thread->task)) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else if (task_get_state(thread->task) == TASK_STATE_NONE) {
		NewInstrState = INSTR_STATE_CLEAN;
	}
	else {
		// printf("%u %p END FUNC %s %s ISTATE: %d\n", thread->id, thread->task, rtn_name, img_name, InstrState);
		bool state_change = task_leave_ignore_region(thread->task, addr);

		NewInstrState = SwitchInstrumentationState(InstrState);

		// NewInstrState = SwitchVersionRegValueLeave(thread->task, InstrState);

		if (state_change) {
			debug(4, thread, thread->task,
				"Return from '%s' in '%s': new tracing state is %s, instrumentation state %d -> %d",
				rtn_name, img_name,
				tracing_state_str(task_get_tracing_state(thread->task)),
				InstrState, NewInstrState);
		}
	}

	return NewInstrState;
}
#endif


/*
	Invoked whenever there is a call to a known 'malloc' function.
	The size of the current allocation is stored temporarily for
	further use.
 */
VOID OnIgnoredRegionAllocFunction(ADDRINT ptr, UINT64 size, ADDRINT retip) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	if (task_get_tracing_state(thread->task) != TRACING_STATE_SUSPENDED_FIRST_EXPLICIT) {
		return;
	}
	else if (ptr == 0 || size == 0) {
		return;
	}

	// Register a new artificial dependency for this allocation
	// function, cause there might be read/write accesses to it
	UINT64 ranges[1][3] = { { size, 0, size } };

	dep_t *alloc_dep = dep_create(
		DEP_NORMAL, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"alloc", ptr, ranges, 1);

	task_register_dep(thread->task, thread, alloc_dep);

	task_advance_time(thread->task);

	img_t *img = image_find_by_addr(retip);

	task_save_raw_access(thread->task, ITV_MODE_ALLOC,
		XED_ICLASS_LAST, retip, ptr, size, img->id, 0, 0);

	malloc_register(
		ptr, size);
}


VOID OnIgnoredRegionDeallocFunction(ADDRINT ptr, ADDRINT retip) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();
	expect_TASK_NOT_NULL(thread->task);

	if (task_get_tracing_state(thread->task) != TRACING_STATE_SUSPENDED_FIRST_EXPLICIT) {
		return;
	}
	else if (ptr == 0) {
		return;
	}

	UINT64 size = malloc_get_size(ptr);

	if (size == 0) {
		return;
	}

	img_t *img = image_find_by_addr(retip);

	task_save_raw_access(thread->task, ITV_MODE_FREE,
		XED_ICLASS_LAST, retip, ptr, size, img->id, 0, 0);

	malloc_unregister(ptr);

	if (config.frequent_processing
		&& config.without_analysis == false) {
		// NOTE: This could be done at the end of an ignored region
		// task_accessmap_update(thread->task);
		task_flush_raw_accesses(thread->task);
		task_load_raw_accesses(thread->task);
		{
			match_acc_with_child_deps(thread->task);
			match_acc_with_deps(thread->task);
		}
		// task_accessmap_reset(thread->task);
	}

	// Register a new artificial release dependency for this deallocation
	// function because further read/write accesses to it are illegal
	UINT64 ranges[1][3] = { { size, 0, size } };

	dep_t *release_alloc = dep_create(
		DEP_RELEASE, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"release_alloc", ptr, ranges, 1);

	task_register_dep(thread->task, thread, release_alloc);

	task_advance_time(thread->task);
}


/*
	Invoked whenever there is a call to a known 'malloc' function.
	The size of the current allocation is stored temporarily for
	further use.
 */
VOID OnMallocPre(ADDRINT func_addr, ADDRINT func_retip, UINT64 size) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	if (thread->task == NULL) {
		return;
	}
	else if (task_get_tracing_state(thread->task) != TRACING_STATE_ENABLED) {
		return;
	}
	else if (size == 0) {
		return;
	}

	img_t *img = image_find_by_addr(func_retip);

	expect(img,
		"Image should not be null.");

	if (img->filtered) {
		return;
	}

	// Only store the current allocation if this malloc happens to
	// be invoked from within task code, when tracing is enabled
	thread->task->malloc_call.size = size;
	thread->task->malloc_call.retip = func_retip;
	thread->task->malloc_call.img = image_find_by_addr(func_retip);
}


/*
	Invoked whenever there is a known 'malloc' function is returning.
	The returned address is saved, together with the size of the
	allocation, as an artificial read/write dependency. This is
	done because a dynamic memory allocation from within task code
	is treated to be local and so should not generate warnings.
 */
VOID OnMallocPost(ADDRINT ptr) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	if (thread->task == NULL) {
		return;
	}
	else if (thread->task->malloc_call.size <= 0 || ptr == 0) {
		return;
	}

	// Register a new artificial dependency for this malloc
	UINT64 ranges[1][3] = { {
		thread->task->malloc_call.size, 0, thread->task->malloc_call.size
	} };

	dep_t *malloc_dep = dep_create(
		DEP_NORMAL, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"malloc", ptr, ranges, 1);

	task_register_dep(thread->task, thread, malloc_dep);

	task_advance_time(thread->task);

#if 0
	// To support the case of a task which allocates and another
	// which frees dynamic memory, the easiest way is to maintain
	// a global map. This is because such two tasks can execute on
	// two different threads, meaning that not a task-level map,
	// nor a thread-level one are sufficient.
	// FIXME: Is there a better way to handle this with no locks?
	itvmap_t<dym_entry_t> *dym_map = &app.dym_map;

	bool res = dynmem_register(
		ptr, ptr + thread->task->malloc_call.size, DYM_TYPE_MALLOC,
		image_find_by_addr(thread->task->malloc_call.retip), thread->task, dym_map);

	if (!res) {
		error("Unable to register dynamic memory area %p + %lu.", ptr, thread->task->malloc_call.size);
	}
#endif

	task_save_raw_access(thread->task, ITV_MODE_ALLOC,
		XED_ICLASS_LAST, thread->task->malloc_call.retip, ptr, thread->task->malloc_call.size,
		thread->task->malloc_call.img->id, 0, 0);

	malloc_register(
		ptr, thread->task->malloc_call.size);

	thread->task->malloc_call.size = 0;
}


VOID OnFreePre(ADDRINT func_addr, ADDRINT func_retip, ADDRINT ptr) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	if (thread->task == NULL) {
		return;
	}
	else if (task_get_tracing_state(thread->task) != TRACING_STATE_ENABLED) {
		return;
	}
	else if (ptr == 0) {
		return;
	}

	img_t *img = image_find_by_addr(func_retip);

	expect(img,
		"Image should not be null.");

	if (img->filtered) {
		return;
	}

	UINT64 size = malloc_get_size(ptr);

	if (size == 0) {
		return;
	}

#if 0
	itvmap_t<dym_entry_t> *dym_map = &app.dym_map;

	bool res = dynmem_unregister(
		ptr, ptr + size, DYM_TYPE_MALLOC, dym_map);

	if (!res) {
		error("Unable to unregister dynamic memory area %p + %lu.", ptr, size);
	}
#endif

	// It's important to register AND process the access before
	// registering the release dependency, so as to avoid false
	// positives when the next batch of accesses are processed
	// img_t *img = image_find_by_addr(func_retip);

	if (config.frequent_processing
		&& config.without_analysis == false) {
		// task_accessmap_update(thread->task);
		task_flush_raw_accesses(thread->task);
		task_load_raw_accesses(thread->task);
		{
			match_acc_with_child_deps(thread->task);
			match_acc_with_deps(thread->task);
		}
		// task_accessmap_reset(thread->task);
	}

	task_save_raw_access(thread->task, ITV_MODE_FREE,
		XED_ICLASS_LAST, func_retip, ptr, size, img->id, 0, 0);

	malloc_unregister(ptr);


	// Register a new artificial release dependency for this free
	UINT64 ranges[1][3] = { { size, 0, size } };

	dep_t *release_malloc = dep_create(
		DEP_RELEASE, (dep_mode_t) (0UL | DEP_READ | DEP_WRITE | DEP_FAKE),
		"release_malloc", ptr, ranges, 1);

	task_register_dep(thread->task, thread, release_malloc);

	task_advance_time(thread->task);
}


VOID OnFreePost() {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}
}


VOID OnTaskDestroy(UINT64 taskid) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL callback function should not be called.");

	if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS) {
		return;
	}

#ifndef NDEBUG
	thread_data_t *thread = PIN_GetThread();
#endif

	// NOTE: When we get here, the binding between a thread and a
	// task doesn't exist anymore because this callback is invoked
	// when a task and all its children have already completed.
	// For this reason, thread->task can be NULL because we already
	// passed through 'OnTaskExecutionPost'.

	task_data_t *task = task_find_by_id(taskid);

	if (task == NULL) {
		return;
	}

	expect_TASK_FOUND(task, taskid);

	if (task == IGNORED_TASK) {
		// We are executing an ignored task
		return;
	}

	// We cannot destroy the task because the parent may need to
	// retrieve some information about it later, so we only mark
	// it as destroyable and that's it.

	task_set_state(task, TASK_STATE_ZOMBIE);

	for (std::list<task_data_t *>::iterator it = task->children.begin();
		it != task->children.end(); ++it) {

		task_data_t *child = *it;

		if (task_get_state(child) != TASK_STATE_ZOMBIE) {
			continue;
		}

		debug(2, thread, thread->task,
			"Task %u '%s' %p destroyed by parent %u '%s' %p",
			child->id, child->invocationpoint, child,
			task->id, task->invocationpoint, task);

		for (dep_t *dep : child->normal_deps) {
			dep_destroy(dep);
		}

		for (dep_t *dep : child->release_deps) {
			dep_destroy(dep);
		}

		task_destroy(child);
	}

	// if (task->id == 0) {
	// 	debug(0, NULL, NULL,
	// 		"Process %u terminated", PIN_GetPid());

	// 	if (app.has_reports == false) {
	// 		success("No problems encountered!");
	// 	}
	// }
}


/*
	Invoked prior to any read/write memory access to check whether
	it is necessary to trace it. It is important to make this call
	as fast as possible, since it is very frequent.
 */
ADDRINT
PIN_FAST_ANALYSIS_CALL
OnMemoryOperandPredicate(ADDRINT ip, UINT32 tid, ADDRINT trace_version, ISTATE InstrState) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	thread_data_t *thread = PIN_GetThreadE(tid);
	expect_TASK_NOT_NULL(thread->task);

	// if (task_get_tracing_state(thread->task) == TRACING_STATE_DISABLED_VERIFIED) {
	// 	expect(InstrState == INSTR_STATE_CLEAN,
	// 		"The instrumentation state shouldn't be != CLEAN in task %u if tracing is disabled",
	// 		thread->task->id);
	// 	expect(trace_version == TRACE_VERSION_CLEAN,
	// 		"The trace version shouldn't be != CLEAN in task %u if tracing is disabled",
	// 		thread->task->id);
	// }

	expect(trace_version == TRACE_VERSION_INSTR || trace_version == TRACE_VERSION_INSTR_WITHSTACK,
		"Code shouldn't be instrumented in task %u if instrumentation is disabled");

	// thread->calls++;

	return (true
		// We are interested in saving accesses for evaluation purposes
		// && config.evaluation_mode < config.EVALUATE_INSTR_ISA
		// The instrumentation state of this thread allows us to instrument this access
		&& (InstrState == INSTR_STATE_INSTR || InstrState == INSTR_STATE_INSTR_WITHSTACK)
		// // We are inside a task
		// && thread->task
		// The tracing state of the task allows us to instrument this access
		&& task_get_tracing_state(thread->task) == TRACING_STATE_ENABLED
	);
}


/*
	Invoked whenever a read memory access is performed.
	This function performs some expensive but required operations:
	- Checks whether the access is within the current stack frame
	  and, if so, discards the access (we are not interested in
	  local accesses)
	- Flushes the buffer if it is full
	- Stores a new entry into the buffer
 */
VOID
PIN_FAST_ANALYSIS_CALL
OnReadMemoryAccess(ADDRINT rsp, UINT64 opcode, ADDRINT ip, ADDRINT addr, UINT64 span, UINT64 imgid) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	if (config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	ADDRINT lowstack  = thread->task->stackbase;
	// ADDRINT highstack = PIN_GetContextReg(ctxt, REG_STACK_PTR) - 128;
	ADDRINT highstack = rsp - 128;

	task_save_raw_access(thread->task, ITV_MODE_READ,
		opcode, ip, addr, span, imgid, lowstack, highstack);
}


/*
	Invoked whenever a write memory access is performed.
	This function performs some expensive but required operations:
	- Checks whether the access is within the current stack frame
	  and, if so, discards the access (we are not interested in
	  local accesses)
	- Flushes the buffer if it is full
	- Stores a new entry into the buffer
 */
VOID
PIN_FAST_ANALYSIS_CALL
OnWriteMemoryAccess(ADDRINT rsp, UINT64 opcode, ADDRINT ip, ADDRINT addr, UINT64 span, UINT64 imgid) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	if (config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		return;
	}

	thread_data_t *thread = PIN_GetThread();

	ADDRINT lowstack  = thread->task->stackbase;
	// ADDRINT highstack = PIN_GetContextReg(ctxt, REG_STACK_PTR) - 128;
	ADDRINT highstack = rsp - 128;

	task_save_raw_access(thread->task, ITV_MODE_WRITE,
		opcode, ip, addr, span, imgid, lowstack, highstack);
}


INLINE
VOID OnStackAllocFree_Generic(UINT64 opcode, itv_mode_t mode,ADDRINT ip,
                              ADDRINT addr, UINT64 span, UINT64 imgid) {
	thread_data_t *thread = PIN_GetThread();

	task_save_raw_access(thread->task, mode, opcode, ip, addr, span, imgid, 0, 0);
}


VOID
PIN_FAST_ANALYSIS_CALL
OnStackAlloc_Rel(UINT64 opcode, ADDRINT ip, ADDRINT addr, UINT64 span, BOOL is_signed, UINT64 imgid) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	if (config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		return;
	}

	OnStackAllocFree_Generic(opcode, ITV_MODE_ALLOC, ip, addr - span, span, imgid);
}


VOID
PIN_FAST_ANALYSIS_CALL
OnStackFree_Rel(UINT64 opcode, ADDRINT ip, ADDRINT addr, UINT64 span, BOOL is_signed, UINT64 imgid) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	if (config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		return;
	}

	OnStackAllocFree_Generic(opcode, ITV_MODE_FREE, ip, addr, span, imgid);
}


VOID
PIN_FAST_ANALYSIS_CALL
OnStackAlloc_Abs(UINT64 opcode, ADDRINT ip, ADDRINT addr, ADDRINT value, UINT64 imgid) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	if (config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		return;
	}

	OnStackAllocFree_Generic(opcode, ITV_MODE_ALLOC, ip, value, addr - value, imgid);
}


VOID
PIN_FAST_ANALYSIS_CALL
OnStackFree_Abs(UINT64 opcode, ADDRINT ip, ADDRINT addr, ADDRINT value, UINT64 imgid) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA callback function should not be called.");

	if (config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		return;
	}

	OnStackAllocFree_Generic(opcode, ITV_MODE_FREE, ip, addr, value - addr, imgid);
}


/* ------------------------------------------------------ */
/* Instrumentation routines                               */
/* ------------------------------------------------------ */


VOID InstrumentStackAllocationInstruction_Delta(INS &Instr, img_t *img, BOOL is_alloc,
                                                UINT64 span, BOOL is_signed, ADDRINT version) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA instrumentation function should not be called.");

	// expect(config.without_isa_stack == false,
	// 	"This ISA instrumentation function should not be called.");

	INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
		(AFUNPTR) OnMemoryOperandPredicate,
		IARG_FAST_ANALYSIS_CALL,
		IARG_ADDRINT, INS_Address(Instr),
		IARG_THREAD_ID,
		IARG_ADDRINT, version,
		IARG_REG_VALUE, istate_reg,
		IARG_END);
	INS_InsertThenPredicatedCall(
		Instr, IPOINT_BEFORE,
		is_alloc ? (AFUNPTR)OnStackAlloc_Rel : (AFUNPTR)OnStackFree_Rel,
		IARG_FAST_ANALYSIS_CALL,
		IARG_UINT64, INS_Opcode(Instr),
		IARG_ADDRINT, INS_Address(Instr),
		IARG_REG_VALUE, REG_STACK_PTR,
		IARG_UINT64, span,
		IARG_BOOL, is_signed,
		IARG_UINT64, img->id,
		IARG_END);
}


VOID InstrumentStackAllocationInstruction_Reg(INS &Instr, img_t *img, BOOL is_alloc, REG reg, ADDRINT version) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA instrumentation function should not be called.");

	// expect(config.without_isa_stack == false,
	// 	"This ISA instrumentation function should not be called.");

	INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
		(AFUNPTR) OnMemoryOperandPredicate,
		IARG_FAST_ANALYSIS_CALL,
		IARG_ADDRINT, INS_Address(Instr),
		IARG_THREAD_ID,
		IARG_ADDRINT, version,
		IARG_REG_VALUE, istate_reg,
		IARG_END);
	INS_InsertThenPredicatedCall(
		Instr, IPOINT_BEFORE,
		is_alloc ? (AFUNPTR)OnStackAlloc_Abs : (AFUNPTR)OnStackFree_Abs,
		IARG_FAST_ANALYSIS_CALL,
		IARG_UINT64, INS_Opcode(Instr),
		IARG_ADDRINT, INS_Address(Instr),
		IARG_REG_VALUE, REG_STACK_PTR,
		IARG_REG_VALUE, reg,
		IARG_UINT64, img->id,
		IARG_END);
}


BOOL InstrumentStackInstruction(TRACE &Trace, INS &Instr, img_t *img) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA instrumentation function should not be called.");

	// expect(config.without_isa_stack == false,
	// 	"This ISA instrumentation function should not be called.");

	BOOL is_alloc;
	REG reg;
	UINT64 span;
	BOOL is_signed;

	ADDRINT version = TRACE_Version(Trace);

	// Stack (de-)allocation operations
	// --------------------------------

	is_alloc = false;
	reg = REG_INVALID();
	span = 0;
	is_signed = false;

	if (INS_Opcode(Instr) == XED_ICLASS_PUSH) {
		return true;
		// span = INS_MemoryWriteSize(Instr);
		// is_alloc = true;
	}
	else if (INS_Opcode(Instr) == XED_ICLASS_POP) {
		span = INS_MemoryReadSize(Instr);
	}
	else if (INS_IsCall(Instr)) {
		return true;
		// span = INS_MemoryWriteSize(Instr);
		// is_alloc = true;
	}
	else if (INS_IsRet(Instr)) {
		return true;
		// span = INS_MemoryReadSize(Instr);
	}
	else if (INS_Opcode(Instr) == XED_ICLASS_SUB && INS_OperandReg(Instr, 2) == REG_STACK_PTR) {
		return true;
		// span = INS_OperandImmediate(Instr, 1);
		// is_signed = xed_decoded_inst_get_immediate_is_signed(INS_XedDec(Instr));
		// is_alloc = true;
	}
	else if (INS_Opcode(Instr) == XED_ICLASS_ADD && INS_OperandReg(Instr, 2) == REG_STACK_PTR) {
		span = INS_OperandImmediate(Instr, 1);
		is_signed = xed_decoded_inst_get_immediate_is_signed(INS_XedDec(Instr));
	}
	else if (INS_Opcode(Instr) == XED_ICLASS_LEAVE) {
		reg = REG_GBP;
	}
	else if (INS_IsMov(Instr) && INS_OperandReg(Instr, 0) == REG_STACK_PTR) {
		// TODO: Could be a longjmp instruction...
		// error("Unsupported MOV instruction at %s:%p.",
		// 	img->name, INS_Address(Instr) - img->extent_low);
	}

	if (span > 0) {
		InstrumentStackAllocationInstruction_Delta(Instr, img, is_alloc, span, is_signed, version);
		return true;
	}
	else if (reg != REG_INVALID()) {
		InstrumentStackAllocationInstruction_Reg(Instr, img, is_alloc, reg, version);
		return true;
	}

	return false;
}


/*
	Processes a memory-accessing instruction and instruments it.
	A few filters are implemented:
	- If the access is computed relative to the stack frame pointer,
	  it is discarded because it will be a local access
	- RET, PUSH, POP instructions are discarded, together with all
	  those instructions that implicitly change the stack pointer
	- If the access is computed relative to the FS segment register,
	  it is discarded because it only serves to compute the location
	  of a TLS variable (which will be accessed anyway in another
	  instruction)
 */
VOID InstrumentMemoryInstruction(TRACE &Trace, INS &Instr, img_t *img) {
	expect(config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT,
		"This ISA instrumentation function should not be called.");

	UINT64 /* anyOperands, */memOperands;

	ADDRINT version = TRACE_Version(Trace);

	// Filter out some instructions that are not interesting
	// -----------------------------------------------------

	if (INS_MemoryBaseReg(Instr) == REG_GBP /*|| INS_MemoryBaseReg(Instr) == REG_STACK_PTR */) {
		// If the instruction references something on the private
		// stack frame it is probably not interesting
		// return;
	}
	else if (INS_IsPrefetch(Instr)) {
		// PREFETCH instructions for now are ignored, cause they
		// might access memory outside the dependency regions
		return;
	}
	else if (INS_SegmentPrefix(Instr) && INS_SegmentRegPrefix(Instr) == REG_SEG_FS) {
		// We filter out thread-private data located in the TLS area
		// TODO: Check that we are not discarding important accesses!
		return;
	}
	// TODO: Hardware transactions

#if 0
	anyOperands = INS_OperandCount(Instr);

	// Iterate over each memory operand of the instruction.
	for (UINT64 anyOp = 0; anyOp < anyOperands; anyOp++) {
		if (INS_IsStackWrite(Instr) && INS_OperandIsImplicit(Instr, anyOp)) {
			if (INS_Opcode(Instr) == XED_ICLASS_LEAVE) {
				printf("ciao3\n");
			}
			refSize = INS_MemoryWriteSize(Instr);
			// It is a stack writing instruction and the memory
			// operand is implicit, hence it is a PUSH instruction
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR)OnStackAllocation,
				IARG_PARTIAL_CONTEXT,
				&r_regset, &w_regset,
				IARG_UINT64, INS_Opcode(Instr),
				IARG_ADDRINT, INS_Address(Instr),
				IARG_UINT64, refSize,
				IARG_BOOL, 0,
				IARG_UINT64, img->id,
				IARG_END);
			return;
		}
		else if (INS_IsStackRead(Instr) && INS_OperandIsImplicit(Instr, anyOp)) {
			if (INS_Opcode(Instr) == XED_ICLASS_LEAVE) {
				printf("ciao2\n");
			}
			refSize = INS_MemoryReadSize(Instr);
			// It is a stack reading instruction and the memory
			// operand is implicit, hence it is a POP instruction
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR)OnStackFree,
				IARG_PARTIAL_CONTEXT,
				&r_regset, &w_regset,
				IARG_UINT64, INS_Opcode(Instr),
				IARG_ADDRINT, INS_Address(Instr),
				IARG_UINT64, refSize,
				IARG_BOOL, 0,
				IARG_UINT64, img->id,
				IARG_END);
			return;
		}
		else if (INS_Opcode(Instr) == XED_ICLASS_SUB && INS_OperandIsImmediate(Instr, anyOp)
			&& INS_OperandReg(Instr, anyOp - 1) == REG_STACK_PTR) {
			// It is a stack allocation instruction
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR)OnStackAllocation,
				IARG_PARTIAL_CONTEXT,
				&r_regset, &w_regset,
				IARG_UINT64, INS_Opcode(Instr),
				IARG_ADDRINT, INS_Address(Instr),
				IARG_UINT64, INS_OperandImmediate(Instr, anyOp),
				IARG_BOOL, xed_decoded_inst_get_immediate_is_signed(INS_XedDec(Instr)),
				IARG_UINT64, img->id,
				IARG_END);
			return;
		}
		else if (INS_Opcode(Instr) == XED_ICLASS_ADD && INS_OperandIsImmediate(Instr, anyOp)
			&& INS_OperandReg(Instr, anyOp - 1) == REG_STACK_PTR) {
			// It is a stack de-allocation instruction
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR)OnStackFree,
				IARG_PARTIAL_CONTEXT,
				&r_regset, &w_regset,
				IARG_UINT64, INS_Opcode(Instr),
				IARG_ADDRINT, INS_Address(Instr),
				IARG_UINT64, INS_OperandImmediate(Instr, anyOp),
				IARG_BOOL, xed_decoded_inst_get_immediate_is_signed(INS_XedDec(Instr)),
				IARG_UINT64, img->id,
				IARG_END);
			return;
		}
		else if (INS_Opcode(Instr) == XED_ICLASS_LEAVE) {
			printf("ciao\n");
			// It is a stack de-allocation instruction
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR)OnStackFreeImplicit,
				IARG_PARTIAL_CONTEXT,
				&r_regset, &w_regset,
				IARG_UINT64, INS_Opcode(Instr),
				IARG_ADDRINT, INS_Address(Instr),
				IARG_REG_VALUE, REG_STACK_PTR,
				IARG_REG_VALUE, REG_GBP,
				IARG_UINT64, img->id,
				IARG_END);
			return;
		}
		// else if (INS_OperandIsSegmentReg(Instr, anyOp)) {
		// 	printf("%p\n", (void *)INS_Address(Instr));
		// 	return;
		// }
	}
#endif

	// Instruments memory accesses using a predicated call,
	// i.e. the instrumentation is called iff the memory
	// access will actually be performed.
	// NOTE: On the IA-32 and Intel(R) 64 architectures
	// conditional moves and REP prefixed instructions appear
	// as predicated instructions in Pin.
	memOperands = INS_MemoryOperandCount(Instr);

	// Iterate over each memory operand of the instruction.
	for (UINT64 memOp = 0; memOp < memOperands; memOp++) {
		UINT64 refSize = INS_MemoryOperandSize(Instr, memOp);

		// if (INS_OperandMemorySegmentReg(Instr, memOp) != REG_INVALID()) {
		// 	// TODO: Make it compatible with IA32
		// 	// %fs segment register is used for TLS data, which we
		// 	// assume to be private (it is also used as a canary tag)
		// 	printf("%p\n", (void *)INS_Address(Instr));
		// 	return;
		// }

		if (INS_MemoryOperandIsRead(Instr, memOp)) {
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_FAST_ANALYSIS_CALL,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_THREAD_ID,
				IARG_ADDRINT, version,
				IARG_REG_VALUE, istate_reg,
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR) OnReadMemoryAccess,
				IARG_FAST_ANALYSIS_CALL,
				// IARG_PARTIAL_CONTEXT,
				// &r_regset, &w_regset,
				IARG_REG_VALUE, REG_STACK_PTR,
				IARG_UINT64, INS_Opcode(Instr),
				// IARG_INST_PTR,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_MEMORYOP_EA, memOp,
				IARG_UINT64, refSize,
				IARG_UINT64, img->id,
				IARG_END);
		}

		// Note that in some architectures a single memory
		// operand can be both read and written (for instance
		// incl (%eax) on IA-32) In that case we instrument
		// it once for read and once for write.
		if (INS_MemoryOperandIsWritten(Instr, memOp)) {
			INS_InsertIfPredicatedCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnMemoryOperandPredicate,
				IARG_FAST_ANALYSIS_CALL,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_THREAD_ID,
				IARG_ADDRINT, version,
				IARG_REG_VALUE, istate_reg,
				IARG_END);
			INS_InsertThenPredicatedCall(
				Instr, IPOINT_BEFORE,
				(AFUNPTR) OnWriteMemoryAccess,
				IARG_FAST_ANALYSIS_CALL,
				// IARG_PARTIAL_CONTEXT,
				// &r_regset, &w_regset,
				IARG_REG_VALUE, REG_STACK_PTR,
				IARG_UINT64, INS_Opcode(Instr),
				// IARG_INST_PTR,
				IARG_ADDRINT, INS_Address(Instr),
				IARG_MEMORYOP_EA, memOp,
				IARG_UINT64, refSize,
				IARG_UINT64, img->id,
				IARG_END);
		}
	}
}


VOID InstrumentMallocFunction(RTN &Routine, IMG &Image) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	RTN_InsertCall(Routine, IPOINT_BEFORE,
		(AFUNPTR) OnMallocPre,
		IARG_CALL_ORDER, CALL_ORDER_FIRST,
		IARG_PTR, RTN_Address(Routine),
		IARG_RETURN_IP,
		IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		IARG_END);
	RTN_InsertCall(Routine, IPOINT_AFTER,
		(AFUNPTR) OnMallocPost,
		IARG_CALL_ORDER, CALL_ORDER_LAST,
		IARG_FUNCRET_EXITPOINT_VALUE,
		IARG_END);

	debug(2, NULL, NULL,
		"Instrumented malloc function '%s' in img %s",
		RTN_Name(Routine).c_str(), IMG_Name(Image).c_str());
}


VOID InstrumentFreeFunction(RTN &Routine, IMG &Image) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	RTN_InsertCall(Routine, IPOINT_BEFORE,
		(AFUNPTR) OnFreePre,
		IARG_CALL_ORDER, CALL_ORDER_FIRST,
		IARG_PTR, RTN_Address(Routine),
		IARG_RETURN_IP,
		IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		IARG_END);
	RTN_InsertCall(Routine, IPOINT_AFTER,
		(AFUNPTR) OnFreePost,
		IARG_CALL_ORDER, CALL_ORDER_LAST,
		IARG_FUNCRET_EXITPOINT_VALUE,
		IARG_END);

	debug(2, NULL, NULL,
		"Instrumented free function '%s' in img %s",
		RTN_Name(Routine).c_str(), IMG_Name(Image).c_str());
}


VOID InstrumentIgnoreRegionBeginFunction(RTN &Routine, IMG &Image) {
	RTN_InsertCall(Routine, IPOINT_BEFORE,
		(AFUNPTR) OnIgnoredRegionBegin,
		// NOTE: CALL_ORDER_FIRST makes sure that the callback
		// is called before OnIgnoredFunctionPre, which is
		// exactly what we want
		IARG_CALL_ORDER, CALL_ORDER_FIRST,
		IARG_RETURN_IP,
		IARG_PTR, RTN_Name(Routine).c_str(),
		IARG_PTR, IMG_Name(Image).c_str(),
		IARG_REG_VALUE, istate_reg,
		IARG_RETURN_REGS, istate_reg,
		IARG_END);

	debug(2, NULL, NULL,
		"Instrumented ignored region begin function '%s' in img %s",
		RTN_Name(Routine).c_str(), IMG_Name(Image).c_str());
}


VOID InstrumentIgnoreRegionEndFunction(RTN &Routine, IMG &Image) {
	RTN_InsertCall(Routine, IPOINT_AFTER,
		(AFUNPTR) OnIgnoredRegionEnd,
		// NOTE: CALL_ORDER_LAST makes sure that the callback
		// is called after OnIgnoredFunctionPost, which is
		// exactly what we want
		IARG_CALL_ORDER, CALL_ORDER_LAST,
		// IARG_RETURN_IP,
		IARG_PTR, RTN_Name(Routine).c_str(),
		IARG_PTR, IMG_Name(Image).c_str(),
		IARG_REG_VALUE, istate_reg,
		IARG_RETURN_REGS, istate_reg,
		IARG_END);

	debug(2, NULL, NULL,
		"Instrumented ignored region end function '%s' in img %s",
		RTN_Name(Routine).c_str(), IMG_Name(Image).c_str());
}


/*
	Invoked whenever a 'nanos6_register_region_*' call is made to
	intercept new dependencies.
 */
VOID InstrumentIgnoredRegionAccessFunction(RTN &Routine) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	string rtn_name = RTN_Name(Routine);
	dep_mode_t depmode;
	size_t rpos, wpos, pos;
	UINT64 ndim;

	depmode = DEP_NULL;
	rpos = wpos = pos = 0;

	if ((rpos = rtn_name.find("read")) != string::npos) {
		depmode = (dep_mode_t) (depmode | DEP_READ);
		rpos += string("read").size();
	}
	if ((wpos = rtn_name.find("write")) != string::npos) {
		depmode = (dep_mode_t) (depmode | DEP_WRITE);
		wpos += string("write").size();
		pos = wpos;
	} else {
		pos = rpos;
	}

	ndim = rtn_name[pos + 1] - '0';

	debug(2, NULL, NULL,
		"Instrument ignore region access function '%s' (%s%s, %u dimension(s))",
		RTN_Name(Routine).c_str(), depmode & DEP_READ ? "read" : "", depmode & DEP_WRITE ? "write" : "", ndim);

	switch (ndim) {
		case 1:
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnIgnoredRegionAccessRegistration1,
			IARG_UINT64, depmode,             // in, out, inout
			IARG_RETURN_IP,                   // Return address (used as access address)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the access
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Ending byte position in the 1st dimension (excluded)
			IARG_END);
			break;

		case 2:
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnIgnoredRegionAccessRegistration2,
			IARG_UINT64, depmode,             // in, out, inout
			IARG_RETURN_IP,                   // Return address (used as access address)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the access
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4, // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5, // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6, // Ending byte position in the 2nd dimension (excluded)
			IARG_END);
			break;

		case 3:
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnIgnoredRegionAccessRegistration3,
			IARG_UINT64, depmode,             // in, out, inout
			IARG_RETURN_IP,                   // Return address (used as access address)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the access
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4, // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5, // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6, // Ending byte position in the 2nd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7, // Size of the 3rd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8, // Starting byte position in the 3rd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9, // Ending byte position in the 3rd dimension (excluded)
			IARG_END);
			break;

		case 4:
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnIgnoredRegionAccessRegistration4,
			IARG_UINT64, depmode,             // in, out, inout
			IARG_RETURN_IP,                   // Return address (used as access address)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the access
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2,  // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3,  // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4,  // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5,  // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6,  // Ending byte position in the 2nd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7,  // Size of the 3rd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8,  // Starting byte position in the 3rd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9,  // Ending byte position in the 3rd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 10, // Size of the 4th dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 11, // Starting byte position in the 4th dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 12, // Ending byte position in the 4th dimension (excluded)
			IARG_END);
			break;

		case 5:
		case 6:
		case 7:
		case 8:
			// error("Unsupported number of access dimensions %u", ndim);
			break;

		default:
			error(NULL, NULL,
				"Unexpected number of access dimensions %u", ndim);
	}
}


/*
	Invoked whenever a 'nanos6_register_region_*' call is made to
	intercept new dependencies.
 */
VOID InstrumentNormalDepRegistrationFunction(RTN &Routine) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	string rtn_name = RTN_Name(Routine);
	dep_mode_t depmode;
	dep_type_t deptype;
	UINT64 ndim;

	if (rtn_name.find("reduction") != string::npos) {
		return;
	}

	depmode = DEP_NULL;
	deptype = DEP_NORMAL;

	if (rtn_name.find("concurrent") != string::npos) {
		depmode = DEP_CONCURRENT;
	}
	else if (rtn_name.find("commutative") != string::npos) {
		depmode = DEP_COMMUTATIVE;
	}
	else {
		if (rtn_name.find("weak") != string::npos) {
			depmode = (dep_mode_t) (depmode | DEP_WEAK);
			// deptype = DEP_WEAK;
		}
		if (rtn_name.find("read") != string::npos) {
			depmode = (dep_mode_t) (depmode | DEP_READ);
		}
		if (rtn_name.find("write") != string::npos) {
			depmode = (dep_mode_t) (depmode | DEP_WRITE);
		}
	}

	auto pos = rtn_name.find("depinfo") + string("depinfo").size();
	ndim = rtn_name[pos] - '0';

	debug(2, NULL, NULL,
		"Instrument dependency registration function '%s' (%s%s, %u dimension(s))",
		RTN_Name(Routine).c_str(), depmode & DEP_READ ? "read" : "", depmode & DEP_WRITE ? "write" : "", ndim);

	// INS InstrHead = RTN_InsHeadOnly(Routine);

	switch (ndim) {
		case 1:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration1,
			IARG_UINT64, deptype,             // NORMAL
			IARG_UINT64, depmode,             // in, out, inout, concurrent, commutative
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Stringified contents of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6, // Ending byte position in the 1st dimension (excluded)
			IARG_END);
			break;

		case 2:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration2,
			IARG_UINT64, deptype,             // NORMAL
			IARG_UINT64, depmode,             // in, out, inout, concurrent, commutative
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Stringified contents of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6, // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7, // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8, // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9, // Ending byte position in the 2nd dimension (excluded)
			IARG_END);
			break;

		case 3:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration3,
			IARG_UINT64, deptype,              // NORMAL
			IARG_UINT64, depmode,              // in, out, inout, concurrent, commutative
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2,  // Stringified contents of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3,  // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4,  // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5,  // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6,  // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7,  // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8,  // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9,  // Ending byte position in the 2nd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 10, // Size of the 3rd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 11, // Starting byte position in the 3rd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 12, // Ending byte position in the 3rd dimension (excluded)
			IARG_END);
			break;

		case 4:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration4,
			IARG_UINT64, deptype,              // NORMAL
			IARG_UINT64, depmode,              // in, out, inout, concurrent, commutative
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2,  // Stringified contents of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3,  // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4,  // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5,  // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6,  // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7,  // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8,  // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9,  // Ending byte position in the 2nd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 10, // Size of the 3rd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 11, // Starting byte position in the 3rd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 12, // Ending byte position in the 3rd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 13, // Size of the 4th dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 14, // Starting byte position in the 4th dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 15, // Ending byte position in the 4th dimension (excluded)
			IARG_END);
			break;

		case 5:
		case 6:
		case 7:
		case 8:
			// error("Unsupported number of dependency dimensions %u", ndim);
			break;

		default:
			error(NULL, NULL,
				"Unexpected number of dependency dimensions %u", ndim);
	}
}


/*
	Invoked whenever a 'nanos6_release_*' call is made to
	intercept new release dependencies.
 */
VOID InstrumentReleaseDepRegistrationFunction(RTN &Routine) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	string rtn_name = RTN_Name(Routine);
	dep_mode_t depmode;
	dep_type_t deptype;
	const char *name;
	size_t rpos, wpos, pos;
	UINT64 ndim;

	depmode = DEP_NULL;
	deptype = DEP_RELEASE;
	name = (const char *)"release";
	rpos = wpos = pos = 0;

	if ((pos = rtn_name.find("concurrent")) != string::npos) {
		depmode = DEP_CONCURRENT;
		pos += string("concurrent").size();
	}
	else if ((pos = rtn_name.find("commutative")) != string::npos) {
		depmode = DEP_COMMUTATIVE;
		pos += string("commutative").size();
	}
	else {
		if (rtn_name.find("weak") != string::npos) {
			depmode = (dep_mode_t) (depmode | DEP_WEAK);
			// deptype = DEP_RELEASE_WEAK;
		}
		if ((rpos = rtn_name.find("read")) != string::npos) {
			depmode = (dep_mode_t) (depmode | DEP_READ);
			rpos += string("read").size();
		}
		if ((wpos = rtn_name.find("write")) != string::npos) {
			depmode = (dep_mode_t) (depmode | DEP_WRITE);
			wpos += string("write").size();
			pos = wpos;
		} else {
			pos = rpos;
		}
	}

	ndim = rtn_name[pos + 1] - '0';

	debug(2, NULL, NULL,
		"Instrument dependency release function '%s' (%s%s, %u dimension(s))",
		RTN_Name(Routine).c_str(), depmode & DEP_READ ? "read" : "", depmode & DEP_WRITE ? "write" : "", ndim);

	// INS InstrHead = RTN_InsHeadOnly(Routine);

	switch (ndim) {
		case 1:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration1,
			IARG_UINT64, deptype,             // RELEASE
			IARG_UINT64, depmode,             // in, out, inout, concurrent, commutative
			IARG_PTR, name,                   // Stringified fake prefix of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Ending byte position in the 1st dimension (excluded)
			IARG_END);
			break;

		case 2:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration2,
			IARG_UINT64, deptype,             // RELEASE
			IARG_UINT64, depmode,             // in, out, inout, concurrent, commutative
			IARG_PTR, name,                   // Stringified fake prefix of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4, // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5, // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6, // Ending byte position in the 2nd dimension (excluded)
			IARG_END);
			break;

		case 3:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration3,
			IARG_UINT64, deptype,             // RELEASE
			IARG_UINT64, depmode,             // in, out, inout, concurrent, commutative
			IARG_PTR, name,                   // Stringified fake prefix of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0, // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1, // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2, // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3, // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4, // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5, // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6, // Ending byte position in the 2nd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7, // Size of the 3rd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8, // Starting byte position in the 3rd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9, // Ending byte position in the 3rd dimension (excluded)
			IARG_END);
			break;

		case 4:
			// INS_InsertIfVersionCall(InstrHead);
			// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
			RTN_InsertCall(Routine, IPOINT_BEFORE,
			(AFUNPTR) OnTaskDependencyRegistration4,
			IARG_UINT64, deptype,              // RELEASE
			IARG_UINT64, depmode,              // in, out, inout, concurrent, commutative
			IARG_PTR, name,                    // Stringified fake prefix of the dependency clause
			IARG_FUNCARG_ENTRYPOINT_VALUE, 0,  // Base address of the dependency
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1,  // Size of the 1st dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2,  // Starting byte position in the 1st dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 3,  // Ending byte position in the 1st dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 4,  // Size of the 2nd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 5,  // Starting byte position in the 2nd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6,  // Ending byte position in the 2nd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 7,  // Size of the 3rd dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 8,  // Starting byte position in the 3rd dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 9,  // Ending byte position in the 3rd dimension (excluded)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 10, // Size of the 4th dimension
			IARG_FUNCARG_ENTRYPOINT_VALUE, 11, // Starting byte position in the 4th dimension (included)
			IARG_FUNCARG_ENTRYPOINT_VALUE, 12, // Ending byte position in the 4th dimension (excluded)
			IARG_END);
			break;

		case 5:
		case 6:
		case 7:
		case 8:
			// error("Unsupported number of release dimensions %u", ndim);
			break;

		default:
			error(NULL, NULL,
				"Unexpected number of release dimensions %u", ndim);
	}
}


#if 0
VOID InstrumentFilteredRoutine(RTN &Routine, img_t *img) {
	RTN_InsertCall(Routine, IPOINT_BEFORE,
		(AFUNPTR) OnIgnoredFunctionRegionPre,
		IARG_CALL_ORDER, CALL_ORDER_DEFAULT,
		IARG_RETURN_IP,
		// IARG_PTR, fun,
		IARG_ADDRINT, RTN_Address(Routine),
		IARG_PTR, RTN_Name(Routine).c_str(),
		IARG_PTR, img->name,
		IARG_REG_VALUE, istate_reg,
		// IARG_BOOL, has_ret,
		IARG_RETURN_REGS, istate_reg,
		IARG_END);

	RTN_InsertCall(Routine, IPOINT_AFTER,
		(AFUNPTR) OnIgnoredFunctionRegionPost,
		IARG_CALL_ORDER, CALL_ORDER_DEFAULT,
		// IARG_PTR, fun,
		IARG_ADDRINT, RTN_Address(Routine),
		IARG_PTR, RTN_Name(Routine).c_str(),
		IARG_PTR, img->name,
		IARG_REG_VALUE, istate_reg,
		// IARG_BOOL, has_ret,
		IARG_RETURN_REGS, istate_reg,
		IARG_END);

#if 0
	bool has_ret = false;

	for (INS Instr = RTN_InsHead(Routine); INS_Valid(Instr); Instr = INS_Next(Instr)) {
		if (INS_IsRet(Instr)) {
			has_ret = true;
			break;
		}
	}

	INS Instr = RTN_InsHead(Routine);

	INS_InsertIfVersionCallFunc(Instr, Routine);
	INS_InsertThenCall(Instr, IPOINT_BEFORE,
		(AFUNPTR) OnIgnoredFunctionRegionPre,
		IARG_CALL_ORDER, CALL_ORDER_DEFAULT,
		IARG_RETURN_IP,
		// IARG_PTR, fun,
		IARG_ADDRINT, RTN_Address(Routine),
		IARG_PTR, RTN_Name(Routine).c_str(),
		IARG_PTR, img->name,
		IARG_REG_VALUE, istate_reg,
		IARG_BOOL, has_ret,
		IARG_RETURN_REGS, istate_reg,
		IARG_END);

	Instr = INS_Next(Instr);

	for (; INS_Valid(Instr); Instr = INS_Next(Instr)) {
		if (INS_IsRet(Instr)) {
			INS_InsertIfVersionCallFunc(Instr, Routine);
			INS_InsertThenCall(Instr, IPOINT_BEFORE,
				(AFUNPTR) OnIgnoredFunctionRegionPost,
				IARG_CALL_ORDER, CALL_ORDER_DEFAULT,
				// IARG_PTR, fun,
				IARG_ADDRINT, RTN_Address(Routine),
				IARG_PTR, RTN_Name(Routine).c_str(),
				IARG_PTR, img->name,
				IARG_REG_VALUE, istate_reg,
				IARG_BOOL, has_ret,
				IARG_RETURN_REGS, istate_reg,
				IARG_END);
		}
	}
#endif
}
#endif


VOID InstrumentNanos6LoaderImage(IMG &Image, img_t *img) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	for (SEC Section = IMG_SecHead(Image); SEC_Valid(Section); Section = SEC_Next(Section)) {
		for (RTN Routine = SEC_RtnHead(Section); RTN_Valid(Routine); Routine = RTN_Next(Routine)) {
			RTN_Open(Routine);

			if (RTN_Name(Routine).compare("malloc") == 0) {
				InstrumentMallocFunction(Routine, Image);
			}
			else if (RTN_Name(Routine).compare("free") == 0) {
				InstrumentFreeFunction(Routine, Image);
			}

			// NOTE: The following functions must be instrumented in
			// the loader image, too, when Nanos6 is compiled to use
			// the indirect symbol resolution method.
			// In the 'indirect' method, the loader version is a
			// wrapper for the actual functions to be invoked.
			// In the 'ifunc' method, the loader version is only
			// invoked once in the program; after returning, the
			// actual functions will be invoked.
			// With the 'indirect' method, the mechanism used to update
			// the tracing state of a task assumes that we instrument
			// the loader version.
			else if (RTN_Name(Routine).compare("nanos6_lint_ignore_region_begin") == 0) {
				InstrumentIgnoreRegionBeginFunction(Routine, Image);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_ignore_region_end") == 0) {
				InstrumentIgnoreRegionEndFunction(Routine, Image);
			}

			RTN_Close(Routine);
		}
	}
}


VOID InstrumentNanos6VariantImage(IMG &Image, img_t *img) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	for (SEC Section = IMG_SecHead(Image); SEC_Valid(Section); Section = SEC_Next(Section)) {
		for (RTN Routine = SEC_RtnHead(Section); RTN_Valid(Routine); Routine = RTN_Next(Routine)) {
			RTN_Open(Routine);

			// INS InstrHead = RTN_InsHeadOnly(Routine);
			UINT32 rtn_id = 0;

			if (RTN_Name(Routine).compare("nanos6_lint_on_task_creation") == 0) {
				rtn_id = RTN_Id(Routine);

				// INS_InsertIfVersionCall(InstrHead);
				// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskCreate,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
					IARG_REG_VALUE, REG_STACK_PTR,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_on_task_argsblock_allocation") == 0) {
				rtn_id = RTN_Id(Routine);

				// INS_InsertIfVersionCall(InstrHead);
				// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskArgsBlockReady,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
					IARG_PTR, RTN_Name(Routine).c_str(),
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_submit_task") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskSubmitPre,
					IARG_REG_VALUE, REG_STACK_PTR,
					IARG_REG_VALUE, istate_reg,
					IARG_END);
				RTN_InsertCall(Routine, IPOINT_AFTER,
					(AFUNPTR) OnTaskSubmitPost,
					IARG_CALL_ORDER, CALL_ORDER_LAST,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_on_task_start") == 0) {
				rtn_id = RTN_Id(Routine);

				// INS_InsertIfVersionCall(InstrHead);
				// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskExecutionPre,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_on_task_end") == 0) {
				rtn_id = RTN_Id(Routine);

				// INS_InsertIfVersionCall(InstrHead);
				// INS_InsertThenCall(InstrHead, IPOINT_BEFORE,
				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskExecutionPost,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_on_task_destruction") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_AFTER,
					(AFUNPTR) OnTaskDestroy,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_PTR, RTN_Name(Routine).c_str(),
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_on_taskwait_enter") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskWaitEnter,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_on_taskwait_exit") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskWaitExit,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_in_final") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_AFTER,
					(AFUNPTR) OnTaskFinalClauseEvaluation,
					IARG_FUNCRET_EXITPOINT_VALUE,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_register_alloc") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnIgnoredRegionAllocFunction,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_RETURN_IP,
					IARG_PTR, Routine,
					IARG_END);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_register_free") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnIgnoredRegionDeallocFunction,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
					IARG_RETURN_IP,
					IARG_PTR, Routine,
					IARG_END);
			}

			else if (RTN_Name(Routine).find("nanos6_register_region") != string::npos) {
				InstrumentNormalDepRegistrationFunction(Routine);
			}
			else if (RTN_Name(Routine).find("nanos6_release") != string::npos) {
				InstrumentReleaseDepRegistrationFunction(Routine);
			}
			else if (RTN_Name(Routine).find("nanos6_lint_register_region") != string::npos) {
				InstrumentIgnoredRegionAccessFunction(Routine);
			}

			// NOTE: These functions are also instrumented in their
			// Nanos6 loader version. See 'InstrumentNanos6LoaderImage'
			// for an explanation of this.
			else if (RTN_Name(Routine).compare("nanos6_lint_ignore_region_begin") == 0) {
				InstrumentIgnoreRegionBeginFunction(Routine, Image);
			}
			else if (RTN_Name(Routine).compare("nanos6_lint_ignore_region_end") == 0) {
				InstrumentIgnoreRegionEndFunction(Routine, Image);
			}

			if (rtn_id) {
				debug(2, NULL, NULL,
					"Instrumented %s function %u in %s",
					RTN_Name(Routine).c_str(), rtn_id, IMG_Name(Image).c_str());
			}

			RTN_Close(Routine);
		}
	}
}


VOID InstrumentMainApplicationImage(IMG &Image, img_t *img) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	for (SEC Section = IMG_SecHead(Image); SEC_Valid(Section); Section = SEC_Next(Section)) {
		for (RTN Routine = SEC_RtnHead(Section); RTN_Valid(Routine); Routine = RTN_Next(Routine)) {
			if (!RTN_Valid(Routine)) {
				// Returns FALSE in certain cases when there is no static image
				// of the code available, including dynamically generated code.
				continue;
			}

			RTN_Open(Routine);

			UINT32 rtn_id = 0;

			// Enable and disable tracing when a task begins execution
			if (RTN_Name(Routine).compare("main") == 0) {
				rtn_id = RTN_Id(Routine);

				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskConcreteExecutionPre,
					IARG_REG_VALUE, REG_STACK_PTR,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
				RTN_InsertCall(Routine, IPOINT_AFTER,
					(AFUNPTR) OnTaskConcreteExecutionPost,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
			}
			else if (RTN_Name(Routine).find("ol_task_region") != string::npos) {
				rtn_id = RTN_Id(Routine);

				// FIXME: This is not guaranteed to work with optimization flags
				// Indeed, with -O2 it is already not working...
				RTN_InsertCall(Routine, IPOINT_BEFORE,
					(AFUNPTR) OnTaskConcreteExecutionPre,
					IARG_REG_VALUE, REG_STACK_PTR,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
				RTN_InsertCall(Routine, IPOINT_AFTER,
					(AFUNPTR) OnTaskConcreteExecutionPost,
					IARG_REG_VALUE, istate_reg,
					IARG_RETURN_REGS, istate_reg,
					IARG_END);
			}
			// else if (RTN_Name(Routine).find("nanos6_ol_deps") != string::npos) {
			// 	RTN_InsertCall(Routine, IPOINT_BEFORE,
			// 		(AFUNPTR) OnTaskUnpackDepsPre,
			// 		// IARG_UINT64, 0,
			// 		IARG_REG_VALUE, REG_STACK_PTR,
			// 		IARG_REG_VALUE, istate_reg,
			// 		IARG_ADDRINT, RTN_Name(Routine).c_str(),
			// 		IARG_ADDRINT, RTN_Address(Routine),
			// 		// IARG_RETURN_REGS, istate_reg,
			// 		IARG_END);
			// 	RTN_InsertCall(Routine, IPOINT_AFTER,
			// 		(AFUNPTR) OnTaskUnpackDepsPost,
			// 		// IARG_UINT64, 0,
			// 		IARG_REG_VALUE, istate_reg,
			// 		// IARG_RETURN_REGS, istate_reg,
			// 		IARG_END);
			// }
			// else if (RTN_Name(Routine).find("nanos6_lint_task_verified") != string::npos) {
			// 	// FIXME: TEMPORARY SOLUTION FOR VERIFIED TASKS
			// 	rtn_id = RTN_Id(Routine);

			// 	RTN_InsertCall(Routine, IPOINT_BEFORE,
			// 		(AFUNPTR) OnTaskVerified,
			// 		IARG_END);
			// }

			if (rtn_id) {
				debug(2, NULL, NULL,
					"Instrumented %s function %u in %s",
					RTN_Name(Routine).c_str(), rtn_id, IMG_Name(Image).c_str());
			}

			RTN_Close(Routine);
		}
	}
}


VOID InstrumentStdLibCImage(IMG &Image, img_t *img) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");

	// Register the boundaries of the process stack
	RTN LibcStartMainRoutine = RTN_FindByName(Image, "__libc_start_main");

	if (RTN_Valid(LibcStartMainRoutine)) {
		RTN_Open(LibcStartMainRoutine);

		RTN_InsertCall(LibcStartMainRoutine, IPOINT_BEFORE,
			(AFUNPTR) OnMainProgramStart,
			IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
			IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
			IARG_FUNCARG_ENTRYPOINT_VALUE, 6,
			// IARG_PARTIAL_CONTEXT,
			// &r_regset, &w_regset,
			IARG_END);

		RTN_Close(LibcStartMainRoutine);
	}

	// Register allocation and deallocation of dynamic memory
	RTN MallocRoutine = RTN_FindByName(Image, "malloc");

	if (RTN_Valid(MallocRoutine)) {
		RTN_Open(MallocRoutine);

		InstrumentMallocFunction(MallocRoutine, Image);

		RTN_Close(MallocRoutine);
	}

	// Register allocation and deallocation of dynamic memory
	RTN FreeRoutine = RTN_FindByName(Image, "free");

	if (RTN_Valid(FreeRoutine)) {
		RTN_Open(FreeRoutine);

		InstrumentFreeFunction(FreeRoutine, Image);

		RTN_Close(FreeRoutine);
	}
}


VOID InstrumentStdLibCXXImage(IMG &Image, img_t *img) {
	expect(config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT,
		"This RTL instrumentation function should not be called.");
}


/*
	Process a low image. Invokes analysis routines to enable/disable
	tracing, to register new tasks, to register/unregister argsBlock
	structures when new tasks are created/submitted.
 */
VOID InstrumentLowImage(IMG &Image, img_t *img) {
}


/*
	Process a high image. Invokes analysis routines to register both
	ordinary and release dependencies, to register the stack area
	of tasks, to register local 'malloc' invocations.
	Additionally, a high image is processed to retrieve its symbol
	map and use it to lookup symbol names from raw addresses.
 */
VOID InstrumentHighImage(IMG &Image, img_t *img) {
	for (SEC Section = IMG_SecHead(Image); SEC_Valid(Section); Section = SEC_Next(Section)) {
		string sec_name = SEC_Name(Section);

		// TODO: Gestire zone TLS
		if (SEC_Mapped(Section)
		    && sec_name.find(".thread", 0) == string::npos
		    && sec_name.find(".tbss", 0) == string::npos) {
			// Register section
			sec_type_t type;
			sec_mode_t mode;

			type = SEC_TYPE_UNKNOWN;

			switch (SEC_Type(Section)) {
				case SEC_TYPE_UNUSED:
				case SEC_TYPE_OPD:
				case SEC_TYPE_LOOS:
				case SEC_TYPE_USER:
				case SEC_TYPE_PROC:
					// type = SEC_TYPE_UNKNOWN;
					break;
				case SEC_TYPE_REGREL:
				case SEC_TYPE_DYNREL:
				case SEC_TYPE_DYNAMIC:
				case SEC_TYPE_GOT:
				case SEC_TYPE_HASH:
				case SEC_TYPE_LSDA:
				case SEC_TYPE_UNWIND:
				case SEC_TYPE_UNWINDINFO:
				case SEC_TYPE_REGSYM:
				case SEC_TYPE_DYNSYM:
				case SEC_TYPE_DEBUG:
				case SEC_TYPE_SYMSTR:
				case SEC_TYPE_DYNSTR:
				case SEC_TYPE_SECSTR:
				case SEC_TYPE_COMMENT:
					type = SEC_TYPE_METADATA;
					break;
				case SEC_TYPE_PLTOFF:
					type = SEC_TYPE_METACODE;
					break;
				case SEC_TYPE_DATA:
				case SEC_TYPE_STACK:
				case SEC_TYPE_BSS:
					type = SEC_TYPE_REALDATA;
					break;
				case SEC_TYPE_EXEC:
					type = SEC_TYPE_REALCODE;
					break;
				default:
					error(NULL, NULL,
						"Unrecognized section type.");
			}

			type = section_fix_type(sec_name.c_str(), type);
			mode = SEC_MODE_NONE;

			if (SEC_IsReadable(Section)) {
				mode = (sec_mode_t) (mode | SEC_MODE_READ);
			}
			if (SEC_IsWriteable(Section)) {
				mode = (sec_mode_t) (mode | SEC_MODE_WRITE);
			}
			if (SEC_IsExecutable(Section)) {
				mode = (sec_mode_t) (mode | SEC_MODE_EXEC);
			}

			sec_t *sec = section_register(
				SEC_Address(Section),
				SEC_Address(Section) + SEC_Size(Section),
				SEC_Name(Section).c_str(),
				type,
				mode,
				img
			);

			if (!sec) {
				error(NULL, NULL,
					"Unable to register section %s.", sec_name);
			}

			debug(2, NULL, NULL,
				"Registered %s/%s%s%s section %d <%p, %p> '%s'",
				sec_type_str(sec->type),
				(sec->mode & SEC_MODE_READ ) ? "R" : "-",
				(sec->mode & SEC_MODE_WRITE) ? "W" : "-",
				(sec->mode & SEC_MODE_EXEC ) ? "X" : "-",
				sec->id, (void *)sec->extent_low, (void *)sec->extent_high, sec->name);
		}
	}

	debug(2, NULL, NULL,
		"Parsing symbol information");

	char *filename;
	size_t numchars;

	numchars = strlen(basename(img->name)) + strlen(config.imgmaps_dir) + strlen("/.map") + 1;
	filename = (char *)malloc(numchars);
	snprintf(filename, numchars, "%s/%s.map", config.imgmaps_dir, basename(img->name));

	if (utils_check_file(filename) == ERR_NOTA_FILE) {
		error(NULL, NULL,
			"'%s' is not a file, can't read from or write to this path.");
	}

	if (config.use_image_maps_cache == false || (utils_check_file(filename) == ERR_NEXISTS_FILE)) {
		// Spawn a process that dumps a map of sections and symbols
		debug(3, NULL, NULL,
			"Generating symbol map");
		symbols_generate_map(img, filename);
	}
	else {
		debug(3, NULL, NULL,
			"Using symbol map from cache");
	}

	// Recover the map of sections and symbols for later use
	debug(3, NULL, NULL,
		"Recovering symbol map");
	symbols_recover_map(img, filename);

	free(filename);
}


VOID InstrumentFilteredImage(IMG Image, img_t *img) {
	img->filtered = true;

	debug(3, NULL, NULL,
		"Filtering image '%s'",
		img->name);

	return;

	// FIXME: Pin sometimes is not able to catch all symbols
	// and functions, so we must compensate with an external
	// process for this

#if 0
	for (SEC Section = IMG_SecHead(Image); SEC_Valid(Section); Section = SEC_Next(Section)) {
		for (RTN Routine = SEC_RtnHead(Section); RTN_Valid(Routine); Routine = RTN_Next(Routine)) {

			if (!RTN_Valid(Routine)) {
				// Returns FALSE in certain cases when there is no static image
				// of the code available, including dynamically generated code.
				return;
			}

			RTN_Open(Routine);

#if 0
			fun_type_t type;

			SYM Symbol = RTN_Sym(Routine);
			string rtn_name;

			if (!SYM_Valid(Symbol)) {
				rtn_name = RTN_Name(Routine);
			} else {
				rtn_name = PIN_UndecorateSymbolName(SYM_Name(Symbol), UNDECORATION_NAME_ONLY);
			}

			if (SYM_Dynamic(Symbol)) {
				sec_t *sec = section_find_by_addr(RTN_Address(Routine));

				if (sec && sec->type == SEC_TYPE_METACODE) {
					type = FUN_TYPE_EXTERNAL;
				} else {
					type = FUN_TYPE_INTERNAL_D_REAL;
				}
			}
			else if (SYM_GeneratedByPin(Symbol) || RTN_IsArtificial(Routine) || RTN_IsDynamic(Routine)) {
				type = FUN_TYPE_INTERNAL_D_FAKE;
			}
			else if (SYM_IFuncImplementation(Symbol)) {
				type = FUN_TYPE_INTERNAL_I_IMPL;
			}
			else if (SYM_IFuncResolver(Symbol)) {
				type = FUN_TYPE_INTERNAL_I_RESV;
			}
			else {
				type = FUN_TYPE_INTERNAL_D_REAL;
			}

			fun_t *fun = function_register(
				RTN_Address(Routine),
				RTN_Address(Routine) + std::max(RTN_Range(Routine), RTN_Size(Routine)),
				(new string(rtn_name))->c_str(),
				type,
				img
			);

			if (!fun) {
				error("Unable to register function %s.", rtn_name);
			}

			fun->filtered = 1;

			debug(4, -1, -1, "Filtered %s function %d <%p, %p> '%s'",
				fun_type_str(fun->type),
				fun->id, (void *)fun->extent_low, (void *)fun->extent_high, fun->name);

			debug(4, -1, -1, "Filtered function '%s' @ %p",
				RTN_Name(Routine), RTN_Address(Routine));
#endif

			// if (RTN_Name(Routine).compare("dl_runtime_resolve") == 0) {
			// 	// TODO: This is a special function: it has an entry point,
			// 	// but then it calls a function which is in another image,
			// 	// so it never returns... right now we ignore it because
			// 	// there is no easy way to detect this
			// 	continue;
			// }

			if (!RTN_IsArtificial(Routine)) {
				debug(4, NULL, NULL,
					"Filtered function '%s' @ %p",
					RTN_Name(Routine).c_str(), RTN_Address(Routine));
			}

			InstrumentFilteredRoutine(Routine, img);

			// RTN_InsertCall(Routine, IPOINT_BEFORE,
			// 	(AFUNPTR) OnIgnoredFunctionRegionPre,
			// 	IARG_CALL_ORDER, CALL_ORDER_DEFAULT,
			// 	IARG_RETURN_IP,
			// 	// IARG_PTR, fun,
			// 	IARG_ADDRINT, RTN_Address(Routine),
			// 	IARG_PTR, RTN_Name(Routine).c_str(),
			// 	IARG_PTR, img->name,
			// 	IARG_REG_VALUE, istate_reg,
			// 	IARG_RETURN_REGS, istate_reg,
			// 	IARG_END);
			// RTN_InsertCall(Routine, IPOINT_AFTER,
			// 	(AFUNPTR) OnIgnoredFunctionRegionPost,
			// 	IARG_CALL_ORDER, CALL_ORDER_DEFAULT,
			// 	// IARG_PTR, fun,
			// 	IARG_ADDRINT, RTN_Address(Routine),
			// 	IARG_PTR, RTN_Name(Routine).c_str(),
			// 	IARG_PTR, img->name,
			// 	IARG_REG_VALUE, istate_reg,
			// 	IARG_RETURN_REGS, istate_reg,
			// 	IARG_END);

			RTN_Close(Routine);
		}
	}
#endif
}


#if 0
VOID OnTraceExecutionBegin(UINT64 trace_version, ISTATE InstrState, ADDRINT address) {
	thread_data_t *thread = PIN_GetThread();

	bool must_switch = (
		(istate_reg == INSTR_STATE_CLEAN && trace_version == TRACE_VERSION_INSTR)
		||
		(istate_reg == INSTR_STATE_INSTR && trace_version == TRACE_VERSION_CLEAN)
	);

	img_t *img = image_find_by_addr(address);

	if (thread->task) {
		printf("%u %p in %s istate_reg is %s, trace version is %s => %s\n",
			thread->id,
			img ? (void *)(address - img->extent_low) : (void *)address,
			img ? img->name : "null",
			istate_reg == INSTR_STATE_CLEAN ? "clean" : "instr",
			trace_version == TRACE_VERSION_CLEAN ? "clean" : "instr",
			must_switch ? "SWITCH" : "--"
		);
	}
}
#endif


#if 0
VOID SMC(ADDRINT traceStartAddress, ADDRINT traceEndAddress, VOID *v) {
	thread_data_t *thread = PIN_GetThread();

	img_t *img = image_find_by_addr(traceStartAddress);

	expect(img != NULL,
		"Unable to find image for address %p %u", traceStartAddress, thread->id);

	sec_t *sec = section_find_by_addr(traceStartAddress);

	expect(sec != NULL,
		"Unable to find section for address %p %s", traceStartAddress, img->name);

	printf("Detected SMC in %s->%s <%p,%p>\n", img->name, sec->name,
		(void *)traceStartAddress, (void *)traceEndAddress);
}
#endif


/*
	Invoked by PIN whenever a new trace is loaded that is not found
	in the trace cache.
	For each instruction in a high image, process it.
 */
VOID Trace(TRACE Trace, VOID *v) {
	// Static filtering of traces
	// --------------------------
	// Before instrumenting a trace, we first make sure that it
	// belongs to a piece of code that is of interest for us
	// (otherwise we simply return) and that some invariants hold
	// (otherwise we throw a fatal error)

	RTN Routine = TRACE_Rtn(Trace);
	if (!RTN_Valid(Routine)) {
		return;
	}

	SEC Section = RTN_Sec(Routine);
	if (!SEC_Valid(Section)) {
		return;
	}

	IMG Image = SEC_Img(Section);
	if (!IMG_Valid(Image)) {
		return;
	}

	img_t *img = image_find_by_addr(TRACE_Address(Trace));

	expect(img != NULL,
		"Unable to find image for address %p", TRACE_Address(Trace));

	if (img->level == IMG_LEVEL_LOW) {
		return;
	}

	if (img->filtered) {
		return;
	}

	// SYM Symbol = RTN_Sym(Routine);

	// if (/* SYM_IFuncResolver(Symbol) && */ img->filtered) {
	// 	// FIXME: Right now a bit of a hack to avoid instrumenting
	// 	// IFUNC functions that aren't catched inside
	// 	// 'InstrumentFilteredImage' due to PIN limitations
	// 	return;
	// }

	sec_t *sec = section_find_by_addr(TRACE_Address(Trace));

	expect(sec != NULL,
		"Unable to find section for address %p %s", TRACE_Address(Trace), img->name);

	if (sec->type == SEC_TYPE_METACODE) {
		return;
	}

	// Instrumentation versioning
	// --------------------------
	//
	// Two instrumentation versions are maintained:
	// - VERSION 0 (INSTR): memory tracing enabled (default)
	// - VERSION 1 (CLEAN): memory tracing disabled
	//
	// PIN instrumentation versioning works by maintaining a tag
	// for each trace, representing the instrumentation to which
	// that trace belongs; additionally, a scratch register is used
	// to switch programmatically between different instrumentation
	// versions.
	//
	// By default, traces are instrumented. Before executing any
	// concrete instruction of a trace, we instruct PIN to check if
	// there is a discrepancy between the current version tag and
	// the version register. If so, execution is interrupted and
	// resumed at the right instrumentation version for that trace.
	//
	// The scratch register is set at runtime:
	// - Whenever we enter inside a task
	// - Whenever we exit from a task
	// - Whenever we enter/leave an ignored region inside a task
	//
	// The value which is set depends on the tracing level for that
	// task, meaning that there is a version register value change
	// IF AND ONLY IF there is a correspondent tracing state change
	// from ENABLED to !ENABLED, or vice-versa.
	//
	// Notice that VERSION 0 is the default version tag, but the
	// default value for the version register is VERSION 1, hence
	// upon executing the first trace of the program there will be
	// an instrumentation version switch. The reason for such a
	// counter-intuitive setting of defaults is that, according to
	// PIN manual, "there are some situations where Pin will reset
	// the version tag to 0, even if executing a non-0 trace. This
	// may occur after a system call, an exception or other unusual
	// control flow. Versioning is intended to enable lightweight
	// instrumentation."

	// Fetch the current instrumentation version
	ADDRINT version = TRACE_Version(Trace);

	// TRACE_InsertCall(Trace, IPOINT_BEFORE, AFUNPTR(OnTraceExecutionBegin),
	// 	IARG_ADDRINT, version,
	// 	IARG_REG_VALUE, istate_reg,
	// 	IARG_ADDRINT, TRACE_Address(Trace),
	// 	IARG_CALL_ORDER, 1,
	// 	IARG_END);

	INS Instr = BBL_InsHead(TRACE_BblHead(Trace));

	// Switch between instrumentation versions, if required
	switch (version) {
		case TRACE_VERSION_CLEAN:
			INS_InsertVersionCase(Instr,
				istate_reg, INSTR_STATE_INSTR, TRACE_VERSION_INSTR,
				IARG_CALL_ORDER, 2,
				IARG_END);
			INS_InsertVersionCase(Instr,
				istate_reg, INSTR_STATE_INSTR_WITHSTACK, TRACE_VERSION_INSTR_WITHSTACK,
				IARG_CALL_ORDER, 2,
				IARG_END);
			break;

		case TRACE_VERSION_INSTR:
			INS_InsertVersionCase(Instr,
				istate_reg, INSTR_STATE_CLEAN, TRACE_VERSION_CLEAN,
				IARG_CALL_ORDER, 2,
				IARG_END);
			INS_InsertVersionCase(Instr,
				istate_reg, INSTR_STATE_INSTR_WITHSTACK, TRACE_VERSION_INSTR_WITHSTACK,
				IARG_CALL_ORDER, 2,
				IARG_END);

			// Additionally, we instrument the trace to perform a full
			// memory tracing, as required by the tool!
			for (BBL BBlock = TRACE_BblHead(Trace); BBL_Valid(BBlock); BBlock = BBL_Next(BBlock)) {
				for (INS Instr = BBL_InsHead(BBlock); INS_Valid(Instr); Instr = INS_Next(Instr)) {
					InstrumentMemoryInstruction(Trace, Instr, img);
				}
			}
			break;

		case TRACE_VERSION_INSTR_WITHSTACK:
			INS_InsertVersionCase(Instr,
				istate_reg, INSTR_STATE_CLEAN, TRACE_VERSION_CLEAN,
				IARG_CALL_ORDER, 2,
				IARG_END);
			// FIXME: Check if this is possible
			INS_InsertVersionCase(Instr,
				istate_reg, INSTR_STATE_INSTR, TRACE_VERSION_INSTR,
				IARG_CALL_ORDER, 2,
				IARG_END);
			// INS_InsertVersionCase(Instr,
			// 	istate_reg, INSTR_STATE_CLEAN_NANOS6, TRACE_VERSION_CLEAN,
			// 	IARG_CALL_ORDER, 2,
			// 	IARG_END);
			// INS_InsertVersionCase(Instr,
			// 	istate_reg, INSTR_STATE_CLEAN_FUNC, TRACE_VERSION_CLEAN,
			// 	IARG_CALL_ORDER, 2,
			// 	IARG_END);

			// Additionally, we instrument the trace to perform a full
			// memory tracing, as required by the tool!
			for (BBL BBlock = TRACE_BblHead(Trace); BBL_Valid(BBlock); BBlock = BBL_Next(BBlock)) {
				for (INS Instr = BBL_InsHead(BBlock); INS_Valid(Instr); Instr = INS_Next(Instr)) {
					if (/* config.without_isa_stack ||  */
					  InstrumentStackInstruction(Trace, Instr, img) == false) {
						InstrumentMemoryInstruction(Trace, Instr, img);
					}
				}
			}
			break;

		default:
			error(NULL, NULL,
				"Unrecognised instrumentation version.");
	}
}


/*
	Invoked by PIN whenever a new image is loaded into memory.
	Images are processed differently based on whether they are high
	or low images. High images are libraries that contribute to the
	application's business logic; low images are libraries that
	contribute to the runtime logic the application relies on to
	execute.
 */
VOID ImageLoad(IMG Image, VOID *v) {
	if (!IMG_Valid(Image)) {
		return;
	}

	string image_name = IMG_Name(Image);

	img_type_t type;
	img_level_t level;

	uint64_t low, high;

	type = IMG_TYPE_UNK;

	switch (IMG_Type(Image)) {
		case IMG_TYPE_STATIC:
		case IMG_TYPE_SHARED:
			type = IMG_TYPE_STT;
			level = IMG_LEVEL_NONE;
			break;
		case IMG_TYPE_SHAREDLIB:
			type = IMG_TYPE_DYN;
			break;
		case IMG_TYPE_RELOCATABLE:
		case IMG_TYPE_DYNAMIC_CODE:
		case IMG_TYPE_API_CREATED:
			// type = IMG_TYPE_UNK;
			break;
		default:
			error(NULL, NULL,
				"Unrecognized image type.");
	}

	if (image_is_low(image_name.c_str())) {
		level = IMG_LEVEL_LOW;
	} else {
		level = IMG_LEVEL_HIGH;
	}

	if (IMG_IsMainExecutable(Image)) {
		// type = IMG_TYPE_STT;
		level = IMG_LEVEL_NONE;
	}

	low = IMG_LowAddress(Image);
	high = IMG_HighAddress(Image);

	for (unsigned int i = 0; i < IMG_NumRegions(Image); ++i) {
		low = std::min(low, IMG_RegionLowAddress(Image, i));
		high = std::max(high, IMG_RegionHighAddress(Image, i));
	}

	// FIXME: Record image regions beside extensive boundaries

	img_t *img = image_register(
		low,
		high,
		IMG_Name(Image).c_str(),
		type,
		level
	);

	if (!img) {
		error(NULL, NULL,
			"Unable to register %s/%s image <%p,%p> '%s'.",
			img_type_str(type), img_level_str(level),
			low, high, image_name);
	}

	debug(1, NULL, NULL,
		"Registered %s/%s image %d <%p,%p> '%s'",
		img_type_str(img->type), img_level_str(img->level),
		img->id, (void *)img->extent_low, (void *)img->extent_high, img->name);

	// NOTE: We cannot filter out all the LOW images because of the
	// linux loader and the 'dl_runtime_resolve' function which
	// never returns (makes an indirect jump to the actual function
	// that needs to be called in another image...).
	// At the same time, we cannot filter out only the libc because
	// of the pthread_mutex_* functions that for some reasons are
	// not correctly instrumented by PIN (I have to investigate).
	// However, notice that conceptually LOW images are filtered by
	// default in a static fashion (see Trace()), while filtered
	// images are HIGH images that for some reason are not
	// interesting from the point of view of tracing. Theoretically,
	// only these images should be filtered, but I must first
	// solve the problem of PIN failing to detect function exit
	// points in some cases (like for pthread_mutex_*).
	// Note that if a LOW image call a HIGH image function, then
	// tracing will be suspended upon invoking this function and
	// resumed upon returning from it. However, since the return
	// address belong to a LOW image, tracing will be implicitly
	// disabled.
	// Also, it makes sense to filter a HIGH image if there are
	// still functions that we wish to instrument in a different
	// way (e.g., 'verified' functions). If we disabled HIGH images
	// in the same way as we do for LOW images, it would not be
	// possible at all to instrument these functions.
	// TODO: Check that the last statement is actually true...
	// PIN has 'RTN_AddInstrumentFunction'!

	// Check if the image has a fixed address in memory
	for (SEC Section = IMG_SecHead(Image); SEC_Valid(Section); Section = SEC_Next(Section)) {
		if (SEC_Mapped(Section)) {
			if (SEC_Address(Section) >= IMG_LowAddress(Image) && img->type == IMG_TYPE_STT) {
				// It means that the image already contains the final VMA
				// that will be used at runtime (there is no randomization)
				img->has_final_vmas = true;
				debug(2, NULL, NULL,
					"Image %s contains final VMAs", img->name);
			}
			break;
		}
	}

	if (img->level == IMG_LEVEL_NONE) {
		app.main_img = img;

		debug(2, NULL, NULL,
			"Detected main application image %s", img->name);

		InstrumentHighImage(Image, img);

		if (config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT) {
			InstrumentMainApplicationImage(Image, img);
		}
	}

	else if (img->level == IMG_LEVEL_LOW) {
		InstrumentLowImage(Image, img);
		InstrumentFilteredImage(Image, img);

		if (image_name.find("libnanos6.so") != string::npos) {
			app.nanos_loader_img = img;

			debug(2, NULL, NULL,
				"Detected nanos loader image %s", img->name);

			if (config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT) {
				InstrumentNanos6LoaderImage(Image, img);
			}
		}
		else if (image_name.find("libnanos6") != string::npos) {
			app.nanos_img = img;

			debug(2, NULL, NULL,
				"Detected nanos variant image %s", img->name);

			if (config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT) {
				InstrumentNanos6VariantImage(Image, img);
			}
		}

		// else if (image_name.find("libtmi") != string::npos || image_name.find("libmpi") != string::npos) {
		// 	debug(2, NULL, NULL,
		// 		"Detected MPI image %s", img->name);

		// 	InstrumentFilteredImage(Image, img);
		// }

		// InstrumentFilteredImage(Image, img);
	}

	else {
		InstrumentHighImage(Image, img);

		if (image_name.find("libstdc++") != string::npos) {
			app.stdlibcxx_img = img;

			debug(2, NULL, NULL,
				"Detected standard C++ library image %s", img->name);

			InstrumentFilteredImage(Image, img);
			// FIXME: This doesn't cover the case of a malloc function
			// coming from other images (e.g., C++ stdlib or special
			// malloc functions)

			if (config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT) {
				InstrumentStdLibCXXImage(Image, img);
			}
		}

		else if (image_name.find("libc") != string::npos) {
			app.stdlibc_img = img;

			debug(2, NULL, NULL,
				"Detected standard C library image %s", img->name);

			InstrumentFilteredImage(Image, img);
			// FIXME: This doesn't cover the case of a malloc function
			// coming from other images (e.g., C++ stdlib or special
			// malloc functions)

			if (config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT) {
				InstrumentStdLibCImage(Image, img);
			}
		}
	}
}


VOID ImageUnload(IMG Image, VOID *v) {
	if (!IMG_Valid(Image)) {
		return;
	}

	string image_name = IMG_Name(Image);

	img_t *img = image_find_by_addr(IMG_LowAddress(Image));

	if (!img) {
		error(NULL, NULL,
			"Unable to unregister image '%s'.", image_name);
	}

#ifndef NDEBUG
	uint32_t img_id = img->id;
	img_type_t img_type = img->type;
	img_level_t img_level = img->level;
#endif

	image_unregister(img);

	debug(1, NULL, NULL,
		"Unregistered %s/%s image %d <%p,%p> '%s'",
		img_type_str(img_type), img_level_str(img_level), img_id,
		(void *)IMG_LowAddress(Image), (void *)IMG_HighAddress(Image), image_name.c_str());
}


BOOL OnAppCrash(THREADID threadid, INT32 sig, CONTEXT *ctxt, BOOL hasHandler,
                const EXCEPTION_INFO *pExceptInfo, VOID *v) {
	error(NULL, NULL,
		"The application has crashed.");

	// for (auto &it : app.tasks_map) {
	// 	task_data_t *task = it.second;
	// }

	return TRUE;
}


VOID OnAppFini(INT32 code, VOID *v) {
	debug(0, NULL, NULL,
		"Process %u terminated with code %u",
		app.id, code);

	if (app.has_reports == false) {
		success("No problems encountered!");
	}

	process_fini();
}


/* ------------------------------------------------------ */
/* Main                                                   */
/* ------------------------------------------------------ */

INT32 Usage() {
	PIN_ERROR("This Pintool prints a trace of memory addresses\n"
	           + KNOB_BASE::StringKnobSummary() + "\n");

	return -1;
}


INT32 main(INT32 argc, CHAR *argv[], CHAR *envp[]) {
	const char *nanos6_variant = getenv("NANOS6");

	if (nanos6_variant == NULL || strcmp(nanos6_variant, "lint") != 0) {
		error(NULL, NULL,
			"NANOS6 envvar must be set to 'lint'");
	}

	// Init PIN
	PIN_InitSymbols();

	if (PIN_Init(argc, argv)) {
		return Usage();
	}

	// Init configuration
	{
		config.output_dir = KnobOutputDir.Value().c_str();
		config.experiment_name = KnobExperimentName.Value().c_str();

		if (utils_check_dir(config.output_dir) == ERR_NEXISTS_DIR &&
			  utils_create_dir(config.output_dir)) {
			error(NULL, NULL,
				"Cannot create output dir '%s'.",
				config.output_dir);
		}

		config.procmaps_dir = utils_str_concat(2, config.output_dir, "/procmaps");
		if (utils_check_dir(config.procmaps_dir) == ERR_NEXISTS_DIR &&
			  utils_create_dir(config.procmaps_dir)) {
			error(NULL, NULL,
				"Cannot create procmaps dir '%s'.",
				config.procmaps_dir);
		}

		config.imgmaps_dir = utils_str_concat(2, config.output_dir, "/imgmaps");
		if (utils_check_dir(config.imgmaps_dir) == ERR_NEXISTS_DIR &&
			  utils_create_dir(config.imgmaps_dir)) {
			error(NULL, NULL,
				"Cannot create imgmaps dir '%s'.",
				config.imgmaps_dir);
		}

		config.traces_dir = utils_str_concat(2, config.output_dir, "/traces");
		if (utils_check_dir(config.traces_dir) == ERR_NEXISTS_DIR &&
			  utils_create_dir(config.traces_dir)) {
			error(NULL, NULL,
				"Cannot create traces dir '%s'.",
				config.traces_dir);
		}

		config.logs_dir = utils_str_concat(2, config.output_dir, "/logs");
		if (utils_check_dir(config.logs_dir) == ERR_NEXISTS_DIR &&
			  utils_create_dir(config.logs_dir)) {
			error(NULL, NULL,
				"Cannot create logs dir '%s'.",
				config.logs_dir);
		}

		if (KnobReportLevel.Value().compare("WRN") == 0) {
			config.min_report_level = config.REPORT_WARNING;
		} else if (KnobReportLevel.Value().compare("NTC") == 0) {
			config.min_report_level = config.REPORT_NOTICE;
		} else if (KnobReportLevel.Value().compare("DBG") == 0) {
			config.min_report_level = config.REPORT_DEBUG;
		} else {
			error(NULL, NULL,
				"Invalid value for the 'r' flag.");
		}

		config.max_debug_level = KnobDebugLevel.Value();

		// switch (KnobEvaluationMode.Value()) {
		// 	case 0:
		// 		config.evaluation_mode = config.EVALUATE_PROCESSING;
		// 		break;

		// 	case 1:
		// 		config.evaluation_mode = config.EVALUATE_LOGGING;
		// 		break;

		// 	case 2:
		// 		config.evaluation_mode = config.EVALUATE_INSTR_ISA;
		// 		break;

		// 	case 3:
		// 		config.evaluation_mode = config.EVALUATE_INSTR_NANOS6;
		// 		break;

		// 	default:
		// 		error(NULL, NULL,
		// 			"Invalid value for the evaluation mode.");
		// }

		config.rtl_instrumentation_mode = static_cast<decltype(config.rtl_instrumentation_mode)>
			(KnobRTLInstrumentationMode.Value());

		if (config.rtl_instrumentation_mode > config.RTL_INSTRUMENTATION_PROCESS) {
			error(NULL, NULL,
				"Invalid value for the RTL instrumentation mode.");
		}

		config.isa_instrumentation_mode = static_cast<decltype(config.isa_instrumentation_mode)>
			(KnobISAInstrumentationMode.Value());

		if (config.isa_instrumentation_mode > config.ISA_INSTRUMENTATION_PROCESS) {
			error(NULL, NULL,
				"Invalid value for the ISA instrumentation mode.");
		}

		if ((unsigned int)config.rtl_instrumentation_mode < (unsigned int)config.isa_instrumentation_mode) {
			error(NULL, NULL,
				"The ISA instrumentation mode cannot be greater than the RTL instrumentation mode.");
		}

		config.without_analysis = KnobWithoutAnalysis.Value();

		if (config.rtl_instrumentation_mode < config.RTL_INSTRUMENTATION_PROCESS
		 || config.isa_instrumentation_mode < config.ISA_INSTRUMENTATION_PROCESS) {
		 	// Implied by the other parameters
			config.without_analysis = true;
		}

		// config.without_isa_stack = KnobWithoutISAStack.Value();

		config.use_image_maps_cache = KnobUseImageMapsCache.Value();
		config.use_thread_logs = KnobUseThreadLogs.Value();

		config.buffer_size = KnobBufferSize.Value();

		if (KnobEngine.Value().compare("online") == 0) {
			config.engine = config.ENGINE_ONLINE;
		} else if (KnobEngine.Value().compare("offline") == 0) {
			config.engine = config.ENGINE_OFFLINE;
		} else {
			error(NULL, NULL,
				"Invalid value for the 'E' flag.");
		}

		config.keep_traces = KnobKeepTraces.Value();

		config.aggregation_level = static_cast<decltype(config.aggregation_level)>
			(KnobAggregationLevel.Value());

		if (config.aggregation_level > config.AGGREGATE_ADDR) {
			error(NULL, NULL,
				"Invalid value for the aggregation level.");
		}

		config.easy_output = KnobEasyOutput.Value();

		config.frequent_processing = KnobFrequentProcessing.Value();
	}

	// Init global PIN variables
	{
		// Setup TLS key
		tls_key = PIN_CreateThreadDataKey(NULL);
		if (tls_key == INVALID_TLS_KEY) {
			error(NULL, NULL,
				"Number of already allocated keys reached the limit");
		}

		// Initialize register sets: we only need the stack pointer
		REGSET_Clear(r_regset);
		REGSET_Clear(w_regset);
		REGSET_Insert(r_regset, REG_STACK_PTR);

		// Initialize version register
		istate_reg = PIN_ClaimToolRegister();
		if (istate_reg == REG_INVALID()) {
			error(NULL, NULL,
				"Unable to allocate a version register");
		}
	}

	// Initialize process
	process_init(PIN_GetPid());

	// Init locks
	PIN_InitLock(&app.locks.tasks);
	PIN_InitLock(&app.locks.images);
	PIN_InitLock(&app.locks.itvmap_mtc_acc_dep);
	PIN_InitLock(&app.locks.itvmap_mtc_dep_dep);
	PIN_InitLock(&app.locks.stdout_channel);
	PIN_InitLock(&app.locks.dynmem);
	PIN_InitLock(&app.locks.malloc);

	debug(0, NULL, NULL,
		"Instrumenting process %u", app.id);

	debug(1, NULL, NULL,
		"Configuration:");
	debug(1, NULL, NULL,
		"\tExperiment dir: %s", config.output_dir);
	debug(1, NULL, NULL,
		"\tExperiment name: %s", config.experiment_name);
	// debug(1, NULL, NULL,
	// 	"\tProcess maps dir: %s", config.procmaps_dir);
	// debug(1, NULL, NULL,
	// 	"\tImage maps dir: %s", config.imgmaps_dir);
	// debug(1, NULL, NULL,
	// 	"\tTraces dir: %s", config.traces_dir);
	// debug(1, NULL, NULL,
	// 	"\tLogs dir: %s", config.logs_dir);
	debug(1, NULL, NULL,
		"\tMin report level: %s", config_report_level_str(config.min_report_level));
	debug(1, NULL, NULL,
		"\tMax debug level: %u", config.max_debug_level);
	// debug(1, NULL, NULL,
	// 	"\tEvaluation mode: %s", config_evaluation_mode_str(config.evaluation_mode));
	debug(1, NULL, NULL,
		"\tRTL Instrumentation mode: %s", config_instrumentation_mode_str(config.rtl_instrumentation_mode));
	debug(1, NULL, NULL,
		"\tISA Instrumentation mode: %s", config_instrumentation_mode_str(config.isa_instrumentation_mode));
	debug(1, NULL, NULL,
		"\tUse image map cache: %s", config.use_image_maps_cache ? "yes" : "no");
	debug(1, NULL, NULL,
		"\tUse thread logs: %s", config.use_thread_logs ? "yes" : "no");
	debug(1, NULL, NULL,
		"\tBuffer size: %u", config.buffer_size);
	debug(1, NULL, NULL,
		"\tEngine type: %s", config_engine_str(config.engine));
	debug(1, NULL, NULL,
		"\tKeep traces: %s", config.keep_traces ? "yes" : "no");
	debug(1, NULL, NULL,
		"\tAggregation level: %u", config.aggregation_level);
	debug(1, NULL, NULL,
		"\tEasy output: %s", config.easy_output ? "yes" : "no");
	debug(1, NULL, NULL,
		"\tFrequent processing: %s", config.frequent_processing ? "yes" : "no");

	// Register callback functions (always executed)
	PIN_AddThreadStartFunction(OnThreadStart, 0);
	PIN_AddThreadFiniFunction(OnThreadFini, 0);

	IMG_AddInstrumentFunction(ImageLoad, 0);
	IMG_AddUnloadFunction(ImageUnload, 0);

	PIN_InterceptSignal(SIGSEGV, OnAppCrash, 0);
	PIN_AddFiniFunction(OnAppFini, 0);

	if (config.rtl_instrumentation_mode >= config.RTL_INSTRUMENTATION_INTERCEPT) {
		PIN_AddSyscallEntryFunction(OnSysEnter, 0);
		PIN_AddSyscallExitFunction(OnSysExit, 0);
	}

	if (config.isa_instrumentation_mode >= config.ISA_INSTRUMENTATION_INTERCEPT) {
		TRACE_AddInstrumentFunction(Trace, 0);
		// TRACE_AddSmcDetectedFunction(SMC, 0);
	}

	// PIN runs in default mode (not 'probe')
	PIN_StartProgram();
	return 0;
}
