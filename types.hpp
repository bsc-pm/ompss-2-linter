/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

#include <map>
#include <unordered_map>
#include <vector>
#include <list>


using namespace std;


typedef struct __img_entry img_entry_t;
typedef struct __sec_entry sec_entry_t;
typedef struct __fun_entry fun_entry_t;
typedef struct __dym_entry dym_entry_t;
typedef struct __var_entry var_entry_t;

typedef struct thread_data thread_data_t;
typedef struct task_data task_data_t;

/* ------------------------------------------------------ */
/* Intervals and Interval Maps                            */
/* ------------------------------------------------------ */


typedef struct __ts {
	uint64_t epoch;
	uint64_t step;
} ts;


#define TS_MAX ts{ (uint64_t)-1, (uint64_t)-1 }

#define TS_MIN ts{ (uint64_t)0, (uint64_t)0 }


INLINE
void ts_increment_step(ts *time) {
	time->step += 1;
}

INLINE
void ts_increment_epoch(ts *time) {
	time->step = 0;
	time->epoch += 1;
}


bool operator == (const ts& lhs, const ts& rhs) {
	return lhs.epoch == rhs.epoch && lhs.step == rhs.step;
}

bool operator < (const ts& lhs, const ts& rhs) {
	return lhs.epoch < rhs.epoch
		|| (lhs.epoch == rhs.epoch && lhs.step < rhs.step);
}

bool operator > (const ts& lhs, const ts& rhs) {
	return lhs.epoch > rhs.epoch
		|| (lhs.epoch == rhs.epoch && lhs.step > rhs.step);
}

bool operator <= (const ts& lhs, const ts& rhs) {
	return lhs == rhs || lhs < rhs;
}

bool operator >= (const ts& lhs, const ts& rhs) {
	return lhs == rhs || lhs > rhs;
}


typedef enum __itv_mode {
	ITV_MODE_ERR,
	ITV_MODE_NONE,
	ITV_MODE_ALLOC,
	ITV_MODE_FREE,
	ITV_MODE_IGNORE,
	ITV_MODE_READ,
	ITV_MODE_WRITE
} itv_mode_t;


INLINE
const char *itv_mode_str(itv_mode_t mode) {
	switch (mode) {
		case ITV_MODE_ERR:
			return "ERR   ";
		case ITV_MODE_NONE:
			return "NONE  ";
		case ITV_MODE_ALLOC:
			return "ALLOC ";
		case ITV_MODE_FREE:
			return "FREE  ";
		case ITV_MODE_IGNORE:
			return "IGNORE";
		case ITV_MODE_READ:
			return "READ  ";
		case ITV_MODE_WRITE:
			return "WRITE ";
		default:
			return "";
	}
}


typedef struct __itv_t {
	itv_mode_t         mode;
	union {
		uint64_t         low;
		void            *lowptr;
	};
	union {
		uint64_t         high;
		void            *highptr;
	};
} itv_t;


bool operator == (const itv_t& lhs, const itv_t& rhs) {
	return lhs.mode == rhs.mode && lhs.low == rhs.low && lhs.high == rhs.high;
}


// bool operator != (const itv_t& lhs, const itv_t& rhs) {
// 	return !(lhs == rhs);
// }


bool operator < (const itv_t& lhs, const itv_t& rhs) {
	return lhs.low < rhs.low
			|| (lhs.low == rhs.low && lhs.high < rhs.high)
			|| (lhs.low == rhs.low && lhs.high == rhs.high && lhs.mode < rhs.mode);
}


bool itv_contains_itv(const itv_t *a, const itv_t *b) {
	return a->low <= b->low && a->high >= b->high;
}


typedef struct dep dep_t;


typedef struct __dep_entry {
	itv_t             itv;
	dep_t            *dep;
	ts                start_time;
	ts                end_time;
} dep_entry_t;


bool operator == (const dep_entry_t& lhs, const dep_entry_t& rhs) {
	return lhs.itv == rhs.itv && lhs.dep == rhs.dep
		&& lhs.start_time == rhs.start_time && lhs.end_time == rhs.end_time;
}


typedef struct __acc_entry {
	itv_t              itv;
	uint64_t           pc;
	int32_t            imgid;
	uint64_t           opcode;
	ts                 time;
} acc_entry_t;


bool operator == (const acc_entry_t& lhs, const acc_entry_t& rhs) {
	return lhs.itv   == rhs.itv   && lhs.pc     == rhs.pc
	    && lhs.imgid == rhs.imgid && lhs.opcode == rhs.opcode
	    && lhs.time  == rhs.time
	;
}


/*
	Dependency match outcome: the tool can verify matches in terms
	of region, section and access mode. A matched access mode means
	that something that was expected to be read/written is indeed
	only read/written. A matched section means that a given array
	section is matched (no accesses outside of it). A matched region
	means that a given array is effectively accessed, but possibly
	outside of the declared section.
 */
typedef enum __mtc_type {
	MATCH_TYPE_INVALID,

	// These are expected matches and are only reported to the user
	// when INFO messages are requested
	MATCH_TYPE_DEBUG = config.REPORT_DEBUG,
		// OK: Found an access toward a normal-strong dependency
		MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP,
		// OK: Found a read access toward a write normal-strong dependency
		//     that was already written
		MATCH_TYPE_YES_READ_ACC_WRITE_NORMAL_STRONG_DEP,

	// These are low-priority unexpected matches that are likely
	// not to cause any damage to the application and will only
	// reported when NOTICE messages are requested
	MATCH_TYPE_NOTICE = config.REPORT_NOTICE,
		// KO: Found a release-strong dependency that doesn't match
		//     with any previously-declared
		MATCH_TYPE_NO_RELEASE_STRONG_DEP_NORMAL_STRONG_DEP,
		// KO: No access found for the current normal-strong dependency
		MATCH_TYPE_NO_NORMAL_STRONG_DEP_ACC,
		// KO: No normal dependency found in child task that matches
		//     with the current normal-weak dependency
		MATCH_TYPE_NO_NORMAL_DEP_CHILD_NORMAL_WEAK_DEP_PARENT,

	// These are high-priority unexpected matches that may raise
	// correctness problems in the application and will always be
	// reported to the user
	MATCH_TYPE_WARNING = config.REPORT_WARNING,
		// KO: No access found for the current normal-strong 'out' dependency
		MATCH_TYPE_NO_NORMAL_STRONG_OUT_DEP_ACC,
		// KO: Found an access toward a release-strong dependency
		MATCH_TYPE_YES_ACC_RELEASE_STRONG_DEP,
		// KO: Found an access toward a normal-weak dependency
		MATCH_TYPE_YES_ACC_NORMAL_WEAK_DEP,
		// KO: Found an access toward a normal-strong dependency, but
		//     the access mode is wrong
		MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP_MODE,
		// KO: Found an access toward a normal-strong dependency, but
		//     toward a wrong array section
		MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP_SECTION,
		// KO: No normal-strong dependency found for the current access
		MATCH_TYPE_NO_ACC_NORMAL_STRONG_DEP,
		// KO: Found an access toward a normal dependency in a child
		//     task which may potentially cause a data race
		MATCH_TYPE_YES_ACC_NORMAL_DEP_CHILD,
		// KO: No normal dependency found in parent task that matches
		//     with the current normal dependency
		MATCH_TYPE_NO_NORMAL_DEP_PARENT_NORMAL_DEP_CHILD
} mtc_type_t;


template<typename entry_a_t, typename entry_b_t>
struct mtc_entry_t {
	itv_t              itv;
	mtc_type_t         type;
	entry_a_t          entry_a;
	entry_b_t          entry_b;
	task_data_t       *task;
};

using mtc_ad_entry_t = mtc_entry_t<acc_entry_t, dep_entry_t>;

using mtc_dd_entry_t = mtc_entry_t<dep_entry_t, dep_entry_t>;

using mtc_adep_entry_t = mtc_entry_t<acc_entry_t, dep_t *>;

using mtc_ddep_entry_t = mtc_entry_t<dep_entry_t, dep_t *>;


template<typename entry_a_t, typename entry_b_t>
bool operator == (const mtc_entry_t<entry_a_t, entry_b_t>& lhs,
                  const mtc_entry_t<entry_a_t, entry_b_t>& rhs) {
	return lhs.itv     == rhs.itv     && lhs.type    == rhs.type
	    && lhs.entry_a == rhs.entry_a && lhs.entry_b == rhs.entry_b;
}


INLINE
acc_entry_t itvmap_new_acc(itv_mode_t mode, uint64_t low, uint64_t high,
                           uint64_t pc, int32_t imgid, uint64_t opcode,
                           ts time) {
	acc_entry_t value;

	value.itv.mode = mode;
	value.itv.low = low;
	value.itv.high = high;

	value.pc = pc;
	value.imgid = imgid;
	value.opcode = opcode;

	value.time = time;

	return value;
}


INLINE
dep_entry_t itvmap_new_dep(itv_mode_t mode, uint64_t low, uint64_t high,
                           dep_t *dep) {
	dep_entry_t value;

	value.itv.mode = mode;
	value.itv.low = low;
	value.itv.high = high;

	value.dep = dep;

	return value;
}


template <typename entry_a_t, typename entry_b_t>
INLINE
mtc_entry_t<entry_a_t, entry_b_t> itvmap_new_mtc(uint64_t low, uint64_t high,
                                                 mtc_type_t type, entry_a_t entry_a, entry_b_t entry_b) {
	mtc_entry_t<entry_a_t, entry_b_t> value;

	value.itv.mode = ITV_MODE_IGNORE;
	value.itv.low = low;
	value.itv.high = high;

	value.type = type;
	value.entry_a = entry_a;
	value.entry_b = entry_b;

	return value;
}


template <typename entry_t>
using itvmap_t = std::multimap<itv_t, entry_t>;

template <typename entry_t>
using itvmap_it_t = typename std::multimap<itv_t, entry_t>::iterator;

template <typename entry_t>
using itvmap_pair_t = std::pair<itv_t, entry_t>;


/* ------------------------------------------------------ */
/* Memory Regions                                         */
/* ------------------------------------------------------ */

typedef enum __img_type {
	IMG_TYPE_NONE,
	IMG_TYPE_STT,
	IMG_TYPE_DYN,
	IMG_TYPE_UNK
} img_type_t;


INLINE
const char *img_type_str(img_type_t type) {
	switch (type) {
		case IMG_TYPE_NONE:
			return "  NONE ";
		case IMG_TYPE_STT:
			return "STATIC ";
		case IMG_TYPE_DYN:
			return "DYNAMIC";
		case IMG_TYPE_UNK:
			return "UNKNOWN";
		default:
			return "";
	}
}

// Low images contain runtime code that sits "below" of
// task code
// High images contain helper code that sits "above" of
// (hence, conceptually belongs to) task code
typedef enum __img_level {
	IMG_LEVEL_NONE,
	IMG_LEVEL_LOW,
	IMG_LEVEL_HIGH
} img_level_t;


INLINE
const char *img_level_str(img_level_t level) {
	switch (level) {
		case IMG_LEVEL_NONE:
			return "MAIN";
		case IMG_LEVEL_LOW:
			return "LOW ";
		case IMG_LEVEL_HIGH:
			return "HIGH";
		default:
			return "";
	}
}


typedef struct __img {
	uint64_t               extent_low;
	uint64_t               extent_high;
	uint32_t               id;
	const char            *name;
	img_type_t             type;
	img_level_t            level;
	bool                   filtered;
	bool                   invalid;
	bool                   has_final_vmas;
	uint32_t               next_sec_id;
	uint32_t               next_fun_id;
	itvmap_t<img_entry_t>  itvmap;
	itvmap_t<sec_entry_t>  sec_map;
	// itvmap_t<fun_entry_t>  fun_map;
} img_t;


struct __img_entry {
	itv_t                  itv;
	img_t                 *img;
};


bool operator == (const img_entry_t& lhs, const img_entry_t& rhs) {
	return lhs.itv == rhs.itv && lhs.img == rhs.img;
}


typedef enum __sec_type {
	SEC_TYPE_NONE,

		// .rodata, .data, .bss, .tdata, .tbss
	SEC_TYPE_REALDATA,

		// .text
	SEC_TYPE_REALCODE,

		// .comment, .debug, .dynamic,.dynstr, .dynsym, .eh_frame,
		// .got, .hash, .interp, .line, .note, .rel, .rela,
		// .shstrtab, .strtab, .symtab
	SEC_TYPE_METADATA,

		// .fini, .init, .plt
	SEC_TYPE_METACODE,

	// All the rest
	SEC_TYPE_UNKNOWN
} sec_type_t;


INLINE
const char *sec_type_str(sec_type_t type) {
	switch (type) {
		case SEC_TYPE_NONE:
			return "  NONE  ";
		case SEC_TYPE_REALDATA:
			return "REALDATA";
		case SEC_TYPE_REALCODE:
			return "REALCODE";
		case SEC_TYPE_METADATA:
			return "METADATA";
		case SEC_TYPE_METACODE:
			return "METACODE";
		case SEC_TYPE_UNKNOWN:
			return "UNKNOWN ";
		default:
			return "";
	}
}


typedef enum __sec_mode {
	SEC_MODE_NONE  = 0x0,
	SEC_MODE_READ  = 0x1,
	SEC_MODE_WRITE = 0x2,
	SEC_MODE_EXEC  = 0x4
} sec_mode_t;


typedef struct __sec {
	uint64_t               extent_low;
	uint64_t               extent_high;
	uint32_t               id;
	const char            *name;
	sec_type_t             type;
	sec_mode_t             mode;
	uint32_t               next_var_id;
	img_t                 *img;
	itvmap_t<var_entry_t>  var_map;
} sec_t;


struct __sec_entry {
	itv_t                  itv;
	sec_t                 *sec;
};


bool operator == (const sec_entry_t& lhs, const sec_entry_t& rhs) {
	return lhs.itv == rhs.itv && lhs.sec == rhs.sec;
}


typedef enum __fun_type {
	FUN_TYPE_NONE,
	FUN_TYPE_EXTERNAL,
	FUN_TYPE_INTERNAL_I_RESV,
	FUN_TYPE_INTERNAL_I_IMPL,
	FUN_TYPE_INTERNAL_D_REAL,
	FUN_TYPE_INTERNAL_D_FAKE
} fun_type_t;


INLINE
const char *fun_type_str(fun_type_t type) {
	switch (type) {
		case FUN_TYPE_NONE:
			return "NONE      ";
		case FUN_TYPE_EXTERNAL:
			return "EXT       ";
		case FUN_TYPE_INTERNAL_I_RESV:
			return "INT-I-RESV";
		case FUN_TYPE_INTERNAL_I_IMPL:
			return "INT-I-IMPL";
		case FUN_TYPE_INTERNAL_D_REAL:
			return "INT-D-REAL";
		case FUN_TYPE_INTERNAL_D_FAKE:
			return "INT-D-FAKE ";
		default:
			return "";
	}
}


typedef struct __fun {
	uint64_t               extent_low;
	uint64_t               extent_high;
	uint32_t               id;
	const char            *name;
	fun_type_t             type;
	img_t                 *img;
	bool                   filtered;
} fun_t;


struct __fun_entry {
	itv_t                  itv;
	fun_t                 *fun;
};


typedef enum __dym_type {
	DYM_TYPE_NONE,
	DYM_TYPE_SBRK,
	DYM_TYPE_MMAP,
	DYM_TYPE_MALLOC
} dym_type_t;


struct __dym_entry {
	itv_t                  itv;
	dym_type_t             type;
	img_t                 *img;
	// thread_data_t         *thread;
	task_data_t           *task;
};


bool operator == (const dym_entry_t& lhs, const dym_entry_t& rhs) {
	return lhs.itv == rhs.itv && lhs.type   == rhs.type
	    && lhs.img == rhs.img /*&& lhs.thread == rhs.thread */&& lhs.task == rhs.task;
}


typedef enum __var_scope {
	VAR_SCOPE_LOCAL,
	VAR_SCOPE_GLOBAL
} var_scope_t;


INLINE
const char *var_scope_str(var_scope_t scope) {
	switch (scope) {
		case VAR_SCOPE_LOCAL:
			return "LOCAL ";
		case VAR_SCOPE_GLOBAL:
			return "GLOBAL";
		default:
			return "";
	}
}


typedef struct __var {
	uint64_t               extent_low;
	uint64_t               extent_high;
	uint32_t               id;
	const char            *name;
	var_scope_t            scope;
	sec_t                 *sec;
} var_t;


struct __var_entry {
	itv_t                  itv;
	var_t                 *var;
};


bool operator == (const var_entry_t& lhs, const var_entry_t& rhs) {
	return lhs.itv == rhs.itv && lhs.var == rhs.var;
}


typedef struct __opaque_obj {
	uint64_t               extent_low;
	uint64_t               extent_high;
	const char            *name;
} opaque_obj_t;


typedef struct __process {
	uint64_t id;

	unsigned long write_fd;
	unsigned long read_fd;

	const char *log_file_path;
	unsigned long log_fd;

	std::vector<img_t *> images;
	uint32_t next_img_id;

	itvmap_t<img_entry_t> img_map;
	// itvmap_t<dym_entry_t> dym_map;

/*
	An unordered map of malloc instances is kept based on the
	address, for fast lookup.
 */
	std::unordered_map<uint64_t, size_t> malloc_map;
/*
	An unordered map of tasks is kept based on the task ID,
	for fast lookup.
 */
	std::unordered_map<uint64_t, task_data_t*> tasks_map;

	std::unordered_map<string, itvmap_t<
		mtc_entry_t<acc_entry_t, dep_t *>>> itvmap_mtc_acc_dep;
	std::unordered_map<string, itvmap_t<
		mtc_entry_t<dep_entry_t, dep_t *>>> itvmap_mtc_dep_dep;

	std::list<thread_data_t *> threads;

	uint64_t stack_low_base;
	uint64_t stack_low_main;
	uint64_t stack_high;
	size_t   stack_size;


	struct {
		PIN_LOCK tasks;
		PIN_LOCK images;
		PIN_LOCK itvmap_mtc_acc_dep;
		PIN_LOCK itvmap_mtc_dep_dep;
		PIN_LOCK stdout_channel;
		PIN_LOCK dynmem;
		PIN_LOCK malloc;
	} locks;

	// Main (application) image
	img_t *main_img;
	// Nanos6 image of the loaded variant
	img_t *nanos_img;
	// Image of the Nanos6 loader
	img_t *nanos_loader_img;
	// Image of the C standard library
	img_t *stdlibc_img;
	// Image of the C++ standard library
	img_t *stdlibcxx_img;
	// Image where the 'malloc' function resides
	// img_t *malloc_img;

	bool has_reports;
} process_t;


process_t app;


/* ------------------------------------------------------ */
/* Mock Functions                                         */
/* ------------------------------------------------------ */

// typedef enum __mock_param_type {
// 	// means skip this parameter
// 	MOCK_PARAM_TYPE_NONE,
// 	// '-1' means 'use the return value', >= 0 means 'use the value of the respective argument'
// 	MOCK_PARAM_TYPE_ARG,
// 	// means 'use the value of the respective argument as hash to retrieve this value'
// 	MOCK_PARAM_TYPE_REF,
// 	// means 'use it as the actual value'
// 	MOCK_PARAM_TYPE_VALUE,
// } mock_param_type_t;


// typedef struct __mock_param {
// 	mock_param_type_t  type;
// 	union {
// 		uint64_t           ref;
// 		int64_t            value;
// 	};
// } mock_param_t;


// typedef struct __mock_entry {
// 	const char  *img_name;
// 	const char  *fun_name;

// 	mock_param_t start;
// 	mock_param_t size;
// 	mock_param_t end;

// 	itv_mode_t   mode;
// } mock_entry_t;


/* ------------------------------------------------------ */
/* Dependency Tracking                                    */
/* ------------------------------------------------------ */

// Max number of supported array dimensions
#define MAX_ARRAY_DIM          (INT32)(8)

/*
	Dependency type: is it a 'depend' clause or a 'release' clause?
 */
typedef enum dep_type {
	DEP_NORMAL,               // Normal dependency
	DEP_RELEASE,              // Release dependency
	NUM_DEP_TYPES
} dep_type_t;

/*
	Dependency mode: describes the direction and other properties
	of a particular dependency. It is implemented as a incremental
	bitmask: 'inout' implied both 'in' and 'out', 'commutative' and
	'concurrent' imply 'inout'. 'weak' can be used with both 'in'
	and 'out'.
 */
typedef enum dep_mode {
	DEP_NULL        = 0x00,
	DEP_READ        = 0x01,   // 'in'
	DEP_WRITE       = 0x02,   // 'out'
	DEP_READWRITE   = 0x03,   // 'inout'
	DEP_COMMUTATIVE = 0x07,   // 'commutative'
	DEP_CONCURRENT  = 0x0F,   // 'concurrent'
	DEP_WEAK        = 0x10,   // 'weak'
	DEP_FAKE        = 0x20    // 'fake' (used for things like argsBlock, malloc, etc.)
} dep_mode_t;


INLINE
const char *dep_mode_str(dep_mode_t mode) {
	if ((mode & DEP_CONCURRENT) == DEP_CONCURRENT) {
		return "concurrent ";
	}
	else if ((mode & DEP_COMMUTATIVE) == DEP_COMMUTATIVE) {
		return "commutative";
	}
	else if ((mode & DEP_READ) && (mode & DEP_WRITE)) {
		return "   inout   ";
	}
	else if (mode & DEP_READ) {
		return "   in      ";
	}
	else if (mode & DEP_WRITE) {
		return "     out   ";
	}
	else {
		return "    ???    ";
	}
}


INLINE
const char *dep_type_str(dep_type_t type, dep_mode_t mode) {
	if (type == DEP_NORMAL) {
		if (mode & DEP_WEAK) {
			return "normal   weak ";
		}
		else {
			return "normal  strong";
		}
	}
	else if (type == DEP_RELEASE) {
		if (mode & DEP_WEAK) {
			return "release  weak ";
		}
		else {
			return "release strong";
		}
	}
	else {
		if (mode & DEP_WEAK) {
			return "???????  weak ";
		}
		else {
			return "??????? strong";
		}
	}
}


/*
	Dependency information for a single dimension.
 */
typedef struct dep_dim {
	UINT64             size;                  // Size of the dimension
	UINT64             start;                 // Starting byte position in the dimension (included)
	UINT64             end;                   // Ending byte position in the dimension (excluded)
} dep_dim_t;


/*
	Dependency information (not depending on the dimension)
 */
struct dep {
	uint32_t           id;
	char const        *name;                  // Stringified contents of the dependency clause
	task_data_t       *task;
	// char const        *task_invocationpoint;

	dep_type_t         type;                  // Type of dependency (e.g., release)
	dep_mode_t         mode;                  // Read or write

	UINT32             ndim;                  // Number of associated dimensions (e.g., for array sections)
	dep_dim_t  *adim;                  // Array of dimension-related information

	uint64_t extent_low, extent_high;

	// A map of all addresses that are expected to be touched
	// via this dependency
	// interval_map<ADDRINT, access_t> accessmap;
	itvmap_t<dep_entry_t> itvmap;

	// Unmatched 'release' are deps that release memory regions
	// never registered before
	// Unmatched 'normal weak' are weak deps that are not matched
	// by any normal child dep
	// Unmatched 'normal strong' are strong deps that are not
	// matched by any access
	itvmap_t<dep_entry_t> itvmap_unmatched;

	// Unreleased 'normal strong' deps are strong deps that are
	// still valid at this point of execution (i.e., no release
	// dep released it)
	// Unreleased 'normal weak' deps are weak deps that are
	// still valid at this point of execution (i.e., no release
	// dep released it)
	// There is no unreleased 'release' deps...
	itvmap_t<dep_entry_t> itvmap_unreleased;

	itvmap_t<dep_entry_t> itvmap_active;
};


/* ------------------------------------------------------ */
/* Fast Buffering                                         */
/* ------------------------------------------------------ */

// Number of OS pages per thread-buffer
// #define NUM_BUF_PAGES 512


/*
	Structure of a buffer entry.
 */
typedef struct buf_entry {
	itv_mode_t  mode;
	UINT32   opcode;           // Instruction opcode
	ADDRINT  iptr;             // Instruction address
	ADDRINT  eaddr;            // Data effective address
	UINT32   size;             // Number of bytes accessed
	UINT32   imgid;            // Image ID (e.g., main image, libc, etc)
	ADDRINT  lowstack;
	ADDRINT  highstack;
	bool     race_free_zone;
	ts       time;
} buf_entry_t;


typedef struct raw_access {
	uint32_t    opcode;           // Instruction opcode
	uint64_t    iptr;             // Instruction address

	uint64_t    eaddr;            // Data effective address
	uint32_t    size;             // Number of bytes accessed

	itv_mode_t  mode;
	uint32_t    imgid;            // Image ID (e.g., main image, libc, etc)

	uint64_t    lowstack;
	uint64_t    highstack;

	bool        race_free_zone;
	ts          time;
} raw_access_t;





/* ------------------------------------------------------ */
/* Dynamic Memory Areas                                   */
/* ------------------------------------------------------ */

// Type of dynamic-memory request, if any
typedef enum dm_req {
	DYNMEM_REQUEST_NONE,
	DYNMEM_REQUEST_BRK,
	DYNMEM_REQUEST_MMAP,
	DYNMEM_REQUEST_MUNMAP,
	DYNMEM_REQUEST_NUM
} dm_req_t;


/* ------------------------------------------------------ */
/* Task-Local Data                                        */
/* ------------------------------------------------------ */

// #define TRACING_DISABLED  (UINT64)(0xFFFFFFFFFFFFFFFF)
// #define TRACING_ENABLED   (UINT64)(0x7FFFFFFFFFFFFFFF)


typedef enum __task_state {
	TASK_STATE_NONE,
	TASK_STATE_CREATED,
	TASK_STATE_SUBMITTED,
	TASK_STATE_STARTED,
	TASK_STATE_TERMINATED,
	TASK_STATE_ZOMBIE
} task_state_t;


typedef enum __tracing_state {
	TRACING_STATE_DISABLED_VERIFIED,
	TRACING_STATE_DISABLED_PERMANENTLY,
	TRACING_STATE_DEFAULT,
	TRACING_STATE_ENABLED,
	TRACING_STATE_ENABLED_NANOS6,
	TRACING_STATE_SUSPENDED_FIRST_IMPLICIT,
	TRACING_STATE_SUSPENDED_IMPLICIT,
	TRACING_STATE_SUSPENDED_FIRST_EXPLICIT,
	TRACING_STATE_SUSPENDED_EXPLICIT
} tracing_state_t;


INLINE
const char *tracing_state_str(tracing_state_t state) {
	switch (state) {
		case TRACING_STATE_DISABLED_VERIFIED:
			return "DISABLED_VERIFIED";

		case TRACING_STATE_DISABLED_PERMANENTLY:
			return "DISABLED_PERMANENTLY";

		case TRACING_STATE_DEFAULT:
			return "DEFAULT";

		case TRACING_STATE_ENABLED:
			return "ENABLED";

		case TRACING_STATE_ENABLED_NANOS6:
			return "ENABLED_NANOS6";

		case TRACING_STATE_SUSPENDED_FIRST_IMPLICIT:
			return "SUSPENDED_FIRST_IMPLICIT";

		case TRACING_STATE_SUSPENDED_IMPLICIT:
			return "SUSPENDED_IMPLICIT";

		case TRACING_STATE_SUSPENDED_FIRST_EXPLICIT:
			return "SUSPENDED_FIRST_EXPLICIT";

		case TRACING_STATE_SUSPENDED_EXPLICIT:
			return "SUSPENDED_EXPLICIT";

		default:
			return "";
	}
}


/*
	Task metadata.
 */
struct task_data {
	uint32_t              id;                     // Task ID
	thread_data_t        *thread;
	thread_data_t        *create_thread;
	task_state_t          state;
	const char           *invocationpoint;            // Task invocation point in the source code
	UINT64                flags;

	char                 *tracename;             // Name of the tracefile
	FILE                 *trace;                      // Tracefile stream
	long int              nextfpos;                   // Current position in the stream
	long unsigned int     tracefd;

	// buf_entry_t           buff[BUFFER_NUM_ENTRIES];   // Trace buffer
	buf_entry_t          *buff;
	UINT32                nextbuffentry;              // Current position in the buffer
	// UINT32                currentepoch;

	ts                    time;
	ts                    parent_creation_time;

	// std::list<dep_t> normal_deps;
	std::list<dep_t *> normal_deps;
	// dep_t            normal_deps[DEPS_MAX_ENTRIES];     // Dependencies for this task
	// UINT32                next_normal_depsentry;              // Current number of registered dependencies

	// std::list<dep_t> release_deps;
	std::list<dep_t *> release_deps;
	// dep_t            release_deps[DEPS_MAX_ENTRIES];  // Release dependencies for this task
	// UINT32                next_release_depsentry;           // Current number of registered released dependencies

	std::list<dep_t *> child_deps;

	dep_t           *stack_dep;
	dep_t           *stack_parent_dep;
	dep_t           *argsblock_dep;
	dep_t           *argsblock_child_dep;

	struct {
		int64_t             current_depth;
		tracing_state_t     current_state;

		bool                in_nanos_code;
		uint64_t            func_first_addr;
		uint64_t            func_last_addr;
		uint64_t            func_last_retip;
	} tracing;

	ADDRINT          stackbase;
	bool             stack_instr;

	// itvmap_t<dym_entry_t> dym_map; // Map of dynamic memory

	std::list<struct task_data *> children;
	// UINT32                          next_children_entry;
	// std::list<struct task_data *>::iterator   next_children_it;

	struct task_data   *parent;
	struct task_data   *prev;

	bool                  submitted;
	bool                  race_free_zone;

	struct {
		UINT32                size;
		ADDRINT               retip;
		img_t                *img;
	} malloc_call;

	// A map of all the addresses that have been touched by this
	// task during its execution
	// itvmap_t<acc_entry_t> itvmap_unmatched;
	// itvmap_t<acc_entry_t> itvmap_mayrace;
	itvmap_t<acc_entry_t> itvmap_common;
	itvmap_t<acc_entry_t> itvmap_sametask_only;
	itvmap_t<acc_entry_t> itvmap_childtask_only;

	itvmap_t<dep_entry_t> itvmap_dep_children;

	itvmap_t<mtc_entry_t
		<acc_entry_t, dep_t *>>    itvmap_mtc_acc_dep;
	itvmap_t<mtc_entry_t
		<dep_entry_t, dep_t *>>    itvmap_mtc_dep_dep;
};


#define IGNORED_TASK (task_data_t *)(-1)


/* ------------------------------------------------------ */
/* Thread-Local Data                                      */
/* ------------------------------------------------------ */

/*
	Thread metadata.
 */
struct thread_data {
	UINT32           id;        // Thread ID
	task_data_t     *task;            // Pointer to the current task

	UINT64           submit_taskid;   // ID of the task being created
	UINT64           taskwait_task_id;   // ID of the task being created
	task_data_t     *created_task;            // Pointer to the current task

	UINT32           calls;           // Number of accesses being intercepted
	UINT32           zeros;           // Number of accesses being intercepted when outside of task code

	ADDRINT          stackbase;

	bool             next_task_verified;

	task_data_t     *idle_task;

	// itvmap_t<dym_entry_t> dym_map; // Map of dynamic memory

	NATIVE_FD        read_fd, write_fd;

	char            *log_file_path;
	NATIVE_FD        log_fd;

	struct {
		dm_req_t         type;
		ADDRINT          addr;
		size_t           size;
		ADDRINT          iptr;
	} dymreq;
};
