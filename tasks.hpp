/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

// Maximum number of buffer entries per task
#define BUFFER_NUM_ENTRIES      (config.buffer_size * utils_get_os_page_size() / sizeof(buf_entry_t))


// AGGREGATION LEVEL == 2
// --------------------------------------------------------------

bool acc_aggregate_same_addr_and_pc_lvl2(
	const acc_entry_t *val, const acc_entry_t *other,
	uint64_t low, uint64_t high,
	const itvmap_t<acc_entry_t> *map
) {
	bool res = (
		// Merge accesses coming from the same PC if the addresses
		// are contiguous
		(true
			&& low <= high
			// ALLOC/FREE must never merge
			// && val->itv.mode > ITV_MODE_IGNORE
			&& val->itv.mode == other->itv.mode
			&& val->pc == other->pc
		)
		||
		// Merge accesses coming from different PC if the range
		// is completely contained in the existing one
		(true
			&& itv_contains_itv(&other->itv, &val->itv)
			&& val->itv.mode == other->itv.mode
		)
	);
	// img_t *img = image_find_by_addr(val->pc);
	// if (val->pc - img->extent_low == 0xb238) {
	// 	printf("%s %p:%p at %p includes %s %p:%p at %p\n",
	// 		itv_mode_str(other->itv.mode), other->itv.lowptr, other->itv.highptr, (void *)(other->pc - img->extent_low),
	// 		itv_mode_str(val->itv.mode), val->itv.lowptr, val->itv.highptr, (void *)(val->pc - img->extent_low));
	// }
	return res;
}


// AGGREGATION LEVEL == 1
// --------------------------------------------------------------

bool acc_aggregate_same_addr_and_pc_lvl1(
	const acc_entry_t *val, const acc_entry_t *other,
	uint64_t low, uint64_t high,
	const itvmap_t<acc_entry_t> *map
) {
	return (true
		&& low <= high
		// ALLOC/FREE must never merge
		// && val->itv.mode > ITV_MODE_IGNORE
		// Mode comparison is necessary for lint accesses!
		&& val->itv.mode == other->itv.mode
		&& val->pc == other->pc
		// && val->imgid == other->imgid
		// && val->opcode == other->opcode
	);
}


bool filter_raw_access_1(buf_entry_t *bentry, task_data_t *task) {
	// NOTE: This is a bit tricky to understand, but it might be
	// that	via an "oss lint" pragma we specify an arbitrary interval
	// that is so large that it overlaps with other images in memory.
	//
	// Since this is clearly wrong, we must keep these accesses
	// and complain about them. Therefore, an access is filtered
	// if and only if it is *entirely* included in a filtered
	// image or section. This is the semantics of all "*_find_by_itv"
	// functions.
	//
	// Currently we don't need any other version of "*_find_by_itv"
	// which, e.g., returns an interval if there is an intersection.
	// There is, however, a "*_find_by_addr" function which can be
	// used to find exact addresses.

	// Dependency pre-filtering
	// ------------------------
	if (task->argsblock_dep
		&& bentry->eaddr >= task->argsblock_dep->extent_low
		&& bentry->eaddr + bentry->size <= task->argsblock_dep->extent_high) {
		// Skip accesses to the task's own argsblock structure
		debug(4, task->thread, task,
			"IGNORE Access <%p,%p> falls within ARGSBLOCK memory area <%p,%p>",
			bentry->eaddr, bentry->eaddr + bentry->size,
			task->argsblock_dep->extent_low, task->argsblock_dep->extent_high);
		return true;
	}

	// Image filtering
	// ---------------

	img_t *img = image_find_by_id(bentry->imgid);

	if (img->filtered && !img->invalid) {
		// NOTE: This should not happen, but some functions (such as
		// the IFUNC resolvers) are not detected by PIN and hence are
		// not filtered out. An example of this is in libc.so, e.g.,
		// function 'strncmp'.
		// A way to fix this is to filter the accesses coming from
		// these functions a-posteriori, like we are doing here.
		// A better approach would be do to this earlier and avoid
		// storing these accesses...

		debug(4, task->thread, task,
			"IGNORE Access <%p,%p> to FILTERED IMAGE %s <%p,%p>",
			bentry->eaddr, bentry->eaddr + bentry->size,
			basename(img->name),
			img->extent_low, img->extent_high);
		return true;
	}

	// if (img->id != app.main_img->id
	//     && image_has_addr(img, bentry->iptr)
	//     && image_has_itv(img, bentry->eaddr, bentry->eaddr + bentry->size)) {
	// 	return true;
	// }

	// Section filtering
	// -----------------

	sec_t *sec = section_find_by_itv(bentry->eaddr, bentry->eaddr + bentry->size);

	if (sec && !sec->img->invalid) {
		if (sec->type != SEC_TYPE_REALDATA) {
			// Skip accesses to non-data sections
			debug(4, task->thread, task,
				"IGNORE Access <%p,%p> to NON-DATA SECTION %s/%s <%p,%p>",
				bentry->eaddr, bentry->eaddr + bentry->size,
				basename(sec->img->name), sec->name,
				sec->extent_low, sec->extent_high);
			return true;
		}
		else if (sec->mode == SEC_MODE_READ) {
			// Skip accesses to, e.g., consts objects
			debug(4, task->thread, task,
				"IGNORE Access <%p,%p> to READ-ONLY SECTION %s/%s <%p,%p>",
				bentry->eaddr, bentry->eaddr + bentry->size,
				basename(sec->img->name), sec->name,
				sec->extent_low, sec->extent_high);
			return true;
		}
	}

	if (bentry->eaddr >= app.stack_low_main && bentry->eaddr + bentry->size < app.stack_low_base) {
		// Skip accesses to env variables and the process auxiliary vector
		debug(4, task->thread, task,
			"IGNORE Access <%p,%p> falls within PROCESS-STACK base memory area <%p,%p>",
			bentry->eaddr, bentry->eaddr + bentry->size, app.stack_low_base, app.stack_low_main);
		return true;
	}

	// Variable filtering
	// ------------------

	var_t *var = variable_find_by_itv(bentry->eaddr, bentry->eaddr + bentry->size);

	if (var && var->scope == VAR_SCOPE_LOCAL && img->id != app.main_img->id) {
		// Skip accesses to variables in other images that can't be seen from
		// the outside (the fact that they are suggests that such variables
		// are not relevant)
		debug(4, task->thread, task,
			"IGNORE Access <%p,%p> to LOCAL-SCOPED VARIABLE %s/%s <%p,%p>",
			bentry->eaddr, bentry->eaddr + bentry->size,
			basename(var->sec->img->name), var->name,
			var->extent_low, var->extent_high);
		return true;
	}

	return false;
}


bool filter_raw_access_2(buf_entry_t *bentry, task_data_t *task) {
	if (task->id == 0) {
		// Filter out accesses coming from the main task
		return true;
	}

	// Filtering stack access operations
	// ---------------------------------

	if (bentry->mode > ITV_MODE_IGNORE && bentry->eaddr <= bentry->lowstack && bentry->eaddr >= bentry->highstack) {
		// A task is accessing is own stack, this is perfectly fine
		// and we ignore the access
		debug(4, task->thread, task,
			"IGNORE Access <%p,%p> to TASK STACK memory area <%p,%p>",
			bentry->eaddr, bentry->eaddr + bentry->size, bentry->lowstack, bentry->highstack);
		return true;
	}

	// Filtering stack (de)allocation operations
	// -----------------------------------------

	if (bentry->mode <= ITV_MODE_IGNORE && bentry->opcode != XED_ICLASS_LAST) {
		// FIXME: This is a bit of a hack, find a better way
		// ALLOC/FREE accesses to the stack are not interesting for
		// other kind of matches, so are not inserted in the following
		// interval maps...
		return true;
	}

	// NOTE: Accesses to own heap-allocated memory areas are
	// "filtered" using fake heap dependencies. These dependencies
	// are useful in cases where a child task has a dep toward the
	// heap allocated by a parent task. In such cases, the tool
	// must not complain for the fact that the parent task doesn't
	// declare any weak (or strong) dep, since it is the task
	// itself which materialized that memory into the address space.

#if 0
	dym_entry_t dym = dynmem_find_by_itv(bentry->eaddr, bentry->eaddr + bentry->size, &app.dym_map);

	if (dym.itv.mode != ITV_MODE_NONE && dym.task == task) {
		// Skip accesses to own dynamically-allocated memory areas
		// (e.g, malloc performed from within a task)
		debug(4, task->thread, task,
			"IGNORE Access <%p,%p> to OWN DYNMEM AREA <%p,%p>",
			bentry->eaddr, bentry->eaddr + bentry->size,
			dym.itv.lowptr, dym.itv.highptr);
		return true;
	}
#endif

	return false;
}


task_data_t *task_create(thread_data_t *thread, uint32_t id, const char *invocation_info, uint64_t flags) {
	task_data_t *task = new task_data_t;
	expect(task,
		"Unable to create a new task.");

	task->id = id;
	task->invocationpoint = invocation_info;
	task->flags = flags;

	// Thread that crated the task (this is going to change when
	// another thread will be put in charge to execute the task)
	task->thread = thread;
	task->create_thread = thread;

	task->state = TASK_STATE_NONE;

	task->tracename = NULL;
	task->trace = NULL;
	task->nextfpos = 0;
	task->tracefd = -1;

	task->nextbuffentry = 0;
	// task->currentepoch = 0;

	task->time = TS_MIN;
	task->parent_creation_time = TS_MIN;

	// task->normal_deps.reserve(8);
	// task->release_deps.reserve(8);

	task->stack_dep = NULL;
	task->stack_parent_dep = NULL;
	task->argsblock_dep = NULL;
	task->argsblock_child_dep = NULL;

	task->tracing.current_state = TRACING_STATE_DEFAULT;
	task->tracing.current_depth = 0;
	task->tracing.in_nanos_code = false;

	task->stackbase = -1;

	// task->next_children_entry = 0;
	// task->next_children_it = task->children.begin();

	task->parent = NULL;
	task->prev = NULL;

	task->submitted = false;
	task->race_free_zone = true;

	task->malloc_call.size = 0;
	task->malloc_call.retip = 0;
	task->malloc_call.img = NULL;

	return task;
}


void task_register(uint64_t id, task_data_t *task) {
	PIN_GetLock(&app.locks.tasks, 1);
	app.tasks_map[id] = task;
	PIN_ReleaseLock(&app.locks.tasks);

	if (task == IGNORED_TASK) {
		return;
	}

	// PIN_GetLock(&app.locks.itvmap_mtc_acc_dep, 1);
	// if (app.itvmap_mtc_acc_dep.find(task->invocationpoint) == app.itvmap_mtc_acc_dep.end()) {
	// 	app.itvmap_mtc_acc_dep[task->invocationpoint] = itvmap_t<mtc_entry_t<acc_entry_t, dep_t *>>();
	// }
	// PIN_ReleaseLock(&app.locks.itvmap_mtc_acc_dep);

	// PIN_GetLock(&app.locks.itvmap_mtc_dep_dep, 1);
	// if (app.itvmap_mtc_dep_dep.find(task->invocationpoint) == app.itvmap_mtc_dep_dep.end()) {
	// 	app.itvmap_mtc_dep_dep[task->invocationpoint] = itvmap_t<mtc_entry_t<dep_entry_t, dep_t *>>();
	// }
	// PIN_ReleaseLock(&app.locks.itvmap_mtc_dep_dep);
}


INLINE
void task_advance_time(task_data_t *task) {
	ts_increment_epoch(&task->time);
}


INLINE
void task_bind_to_thread(task_data_t *task, thread_data_t *thread) {
	// The task is now bound to the thread which will execute it
	task->thread = thread;
}


/*
	Frees task memory after task destruction.
 */
INLINE
VOID task_destroy(task_data_t *task) {
	task->itvmap_dep_children.clear();

	delete task;
}


/*
	When passed a valid task ID, returns a pointer to its metadata.
 */
INLINE
task_data_t *task_find_by_id(uint64_t id) {
	task_data_t *task;

	task = NULL;

	PIN_GetLock(&app.locks.tasks, 1);
	auto it = app.tasks_map.find(id);

	if (it != app.tasks_map.end()) {
		task = it->second;
	}
	PIN_ReleaseLock(&app.locks.tasks);

	return task;
}


INLINE
void task_set_state(task_data_t *task, task_state_t state) {
	task->state = state;
}


INLINE
task_state_t task_get_state(task_data_t *task) {
	return task->state;
}


INLINE
bool task_is_ignored(task_data_t *task) {
	return task_get_state(task) == TASK_STATE_NONE;
}


INLINE
bool task_is_final(task_data_t *task) {
	return task->flags & (1 << 0);
}


INLINE
bool task_is_if0(task_data_t *task) {
	return task->flags & (1 << 1);
}


INLINE
bool task_is_verified(task_data_t *task) {
	return task->flags & (1 << 5);
}


INLINE
void task_add_child(task_data_t *task, task_data_t *child) {
	task->children.push_back(child);

	// A new task has been created, so we are potentially in a new
	// data-race region
	task->race_free_zone = false;

	child->parent = task;
	child->parent_creation_time = task->time;
}


INLINE
bool task_enter_ignore_region(task_data_t *task, uint64_t func_retip, uint64_t func_addr) {
	bool state_change = false;

	switch (task->tracing.current_state) {

		case TRACING_STATE_DEFAULT:
		case TRACING_STATE_DISABLED_VERIFIED:
		case TRACING_STATE_DISABLED_PERMANENTLY:
			// Nothing to see here, move along, move along...
			break;

		case TRACING_STATE_ENABLED:
			state_change = true;

			if (image_find_by_addr(func_addr) == app.nanos_img
			 || image_find_by_addr(func_addr) == app.nanos_loader_img) {
				task->tracing.in_nanos_code = true;
			} else {
				task->tracing.in_nanos_code = false;
			}

			if (task->tracing.in_nanos_code) {
				task->tracing.func_first_addr = func_addr;
				task->tracing.func_last_retip = func_retip;
				task->tracing.current_state = TRACING_STATE_ENABLED_NANOS6;
				break;
			}

			if (func_addr) {
				// Implicit ignored region is a function, so there are
				// no explicit nanos6_lint_ignore_region_* markers.
				task->tracing.current_state = TRACING_STATE_SUSPENDED_FIRST_IMPLICIT;
			}
			else {
				// Explicit nanos6_lint_ignore_region_* markers are used.
				task->tracing.current_state = TRACING_STATE_SUSPENDED_FIRST_EXPLICIT;
			}

			task->tracing.current_depth += 1;

			// The first event determines which part in the state
			// graph will be taken, until we get backed to ENABLED.
			// To track enter/leave event we need to save the function
			// address and its return address.
			task->tracing.func_first_addr = func_addr;
			task->tracing.func_last_retip = func_retip;
			break;

		case TRACING_STATE_ENABLED_NANOS6:
			if (task->tracing.func_last_retip == func_retip) {
				// We jumped to another function without making a call,
				// so we update the function address to track this fact.
				// Note that there is no change of state nor we perform
				// a depth increment.
				task->tracing.func_first_addr = func_addr;
			}
			break;

		case TRACING_STATE_SUSPENDED_FIRST_IMPLICIT:
		case TRACING_STATE_SUSPENDED_IMPLICIT:
			if (func_addr == 0) {
				// If we are in the implicit suspension branch, we keep
				// counting only the invocations to the function that
				// made us enter this implicit branch, without mixing it
				// with the explicit nested ignore region that can be
				// encountered along the way.
				break;
			}

			if (task->tracing.func_first_addr == func_addr) {
				// We are invoking the same function more than once...
				// (e.g., a recursive function), so we change state and
				// keep track of how many times that function has been
				// invoked while tracing is suspended.
				if (task->tracing.current_state == TRACING_STATE_SUSPENDED_FIRST_IMPLICIT) {
					state_change = true;
				}

				task->tracing.current_depth += 1;
				task->tracing.current_state = TRACING_STATE_SUSPENDED_IMPLICIT;
			}
			else if (task->tracing.func_last_retip == func_retip) {
				// We jumped to another function without making a call,
				// so we update the function address to track this fact.
				// Note that there is no change of state nor we perform
				// a depth increment.
				task->tracing.func_first_addr = func_addr;
			}
			break;

		case TRACING_STATE_SUSPENDED_FIRST_EXPLICIT:
		case TRACING_STATE_SUSPENDED_EXPLICIT:
			if (func_addr > 0) {
				// If we get into an ignored region explicitly, we will
				// only track explicit ignore regions. This is indeed
				// easier, because explicit markers make the depth value
				// more accurate and we don't want to mix it with the
				// number of invocations to an implicitly ignored
				// function.
				break;
			}

			if (func_retip > 0 && task->tracing.func_last_retip == func_retip) {
				// We jumped to another function without making a call,
				// so we update the function address to track this fact.
				// Note that there is no change of state nor we perform
				// a depth increment.
				// task->tracing.func_first_addr = func_addr;
				break;
			}

			if (task->tracing.current_state == TRACING_STATE_SUSPENDED_FIRST_EXPLICIT) {
				state_change = true;
			}

			task->tracing.current_depth += 1;
			task->tracing.current_state = TRACING_STATE_SUSPENDED_EXPLICIT;
			// task->tracing.func_last_retip = func_retip;
			break;

		default:
			error(task->thread, task,
				"Unexpected tracing state %d (%s)",
				task->tracing.current_state, tracing_state_str(task->tracing.current_state));
	}

	return state_change;
}


INLINE
bool task_leave_ignore_region(task_data_t *task, uint64_t func_addr) {
	bool state_change = false;

	switch (task->tracing.current_state) {

		// case TRACING_STATE_ENABLED:
			// This can happen if we pass through an ignore region
			// and we get to the EXPLICIT branch before the IMPLICIT
			// one can activate... in this case we must do nothing
			// break;

		case TRACING_STATE_DEFAULT:
		case TRACING_STATE_DISABLED_VERIFIED:
		case TRACING_STATE_DISABLED_PERMANENTLY:
			// Nothing to see here, move along, move along...
			break;

		case TRACING_STATE_ENABLED_NANOS6:
			if (task->tracing.func_first_addr == func_addr) {
				task->tracing.current_state = TRACING_STATE_ENABLED;
				task->tracing.in_nanos_code = false;
				state_change = true;
			}
			break;

		case TRACING_STATE_SUSPENDED_FIRST_IMPLICIT:
		case TRACING_STATE_SUSPENDED_IMPLICIT:
			if (func_addr == 0) {
				// For an explanation of this, see 'task_enter_ignore_region'
				break;
			}

			if (task->tracing.func_first_addr == func_addr) {
				task->tracing.current_depth -= 1;

				if (task->tracing.current_depth == 1) {
					expect(task->tracing.current_state != TRACING_STATE_SUSPENDED_FIRST_IMPLICIT,
						"Current state should not be FIRST_IMPLICIT.");

					task->tracing.current_state = TRACING_STATE_SUSPENDED_FIRST_IMPLICIT;
					state_change = true;
				}
				else if (task->tracing.current_depth == 0) {
					expect(task->tracing.current_state != TRACING_STATE_SUSPENDED_IMPLICIT,
						"Current state should not be IMPLICIT.");

					task->tracing.current_state = TRACING_STATE_ENABLED;
					state_change = true;
				}
			}
			break;

		case TRACING_STATE_SUSPENDED_FIRST_EXPLICIT:
		case TRACING_STATE_SUSPENDED_EXPLICIT:
			if (func_addr > 0) {
				// For an explanation of this, see 'task_enter_ignore_region'
				break;
			}

			task->tracing.current_depth -= 1;

			if (task->tracing.current_depth == 1) {
				task->tracing.current_state = TRACING_STATE_SUSPENDED_FIRST_EXPLICIT;
				state_change = true;
			}
			else if (task->tracing.current_depth == 0) {
				task->tracing.current_state = TRACING_STATE_ENABLED;
				state_change = true;
			}
			break;

		default:
			error(task->thread, task,
				"Unexpected tracing state %d (%s) in thread %u %p.",
				task->tracing.current_state, tracing_state_str(task->tracing.current_state));
	}

	return state_change;
}


#if 0
INLINE
bool task_has_tracing_state(task_data_t *task, tracing_state_t state, uint64_t func_retip) {
	switch (task->tracing.current_state) {

		case TRACING_STATE_SUSPENDED_FIRST_IMPLICIT:
			// It is an ignored function and we are actually inside
			// it in this moment (using the return address is more
			// robust cause it won't change in the presence of function
			// aliases)
			// NOTE: Using function addresses might not function if
			// there are aliases (e.g., __libc_malloc vs. int_malloc
			// in libc, but possibly other libraries too)
			return (task->tracing.func_last_retip == func_retip);

		default:
			return (task->tracing.current_state == state);

	}
}
#endif


INLINE
void task_enable_tracing(task_data_t *task, thread_data_t *thread /*, uint64_t stackbase */) {
	if (config.engine == config.ENGINE_OFFLINE) {
		char filepath[MAX_PATH_LENGTH];

		snprintf(filepath, MAX_PATH_LENGTH,
			"%s/%s.%u.trace",
			config.traces_dir, config.experiment_name, task->id);

		char *tmp_file_path = (char *)malloc(strlen(filepath) + 1);
		strcpy(tmp_file_path, filepath);

		task->tracename = tmp_file_path;

		if (utils_open_fd(task->tracename, "w+", &task->tracefd)) {
			error(thread, task,
				"Unable to create tracefile '%s'", task->tracename);
		}
	}

	task->buff = (buf_entry_t *) malloc(sizeof(buf_entry_t) * BUFFER_NUM_ENTRIES);
	expect(task->buff != NULL,
		"Unable to allocate buffer for task %u.", task->id);

	task->tracing.current_state = TRACING_STATE_ENABLED;
	// task->stackbase = stackbase;
}


INLINE
void task_disable_tracing(task_data_t *task) {
	if (config.engine == config.ENGINE_OFFLINE) {
		if (utils_close_fd(task->tracefd)) {
			error(task->thread, task,
				"Unable to close tracefile '%s'", task->tracename);
		}

		if (utils_delete_file(task->tracename)) {
			error(task->thread, task,
				"Unable to delete tracefile '%s'", task->tracename);
		}
	}

	free(task->buff);

	task->tracing.current_state = TRACING_STATE_DEFAULT;
}


INLINE
bool task_tracing_is_suspended(task_data_t *task) {
	return task->tracing.current_state > TRACING_STATE_ENABLED_NANOS6;
}


INLINE
bool task_tracing_is_disabled(task_data_t *task) {
	return task->tracing.current_state < TRACING_STATE_DEFAULT;
}


INLINE
bool task_tracing_is_not_enabled(task_data_t *task) {
	return task_tracing_is_suspended(task) || task_tracing_is_disabled(task);
}


INLINE
void task_set_tracing_state(task_data_t *task, tracing_state_t state) {
	task->tracing.current_state = state;
}


INLINE
tracing_state_t task_get_tracing_state(task_data_t *task) {
	return task->tracing.current_state;
}


void task_register_dep(task_data_t *task, thread_data_t *thread, dep_t *dep) {
	std::list<dep_t *> *list;

	switch (dep->type) {
		case DEP_NORMAL:
			list = &task->normal_deps;
			break;

		case DEP_RELEASE:
			list = &task->release_deps;
			break;

		default:
			error(thread, task,
				"Unsupported dependency type");
	}

	list->push_back(dep);

	dep->id = list->size();
	dep->task = task;

	// dep_print(task->thread, task, dep);
	// dep_print_ranges(task->thread, task, dep);

	char dep_string[MAX_MSG_LENGTH];
	dep_str(dep, dep_string, MAX_MSG_LENGTH, task->thread, task);

	debug(2, thread, task,
		"Registered dependency %s",
		dep_string);

	// FIXME: Hide implementation of dep ranges
	for (auto &i : dep->itvmap) {
		dep_entry_t *value = &i.second;

		value->start_time = task->time;
		value->end_time = TS_MAX;

		debug(3, thread, task,
			"Registered expected range %s [%lu:%lu] <%p:%p> (%lu, %lu)",
			itv_mode_str(value->itv.mode),
			value->itv.low - dep->extent_low,
			value->itv.high - dep->extent_low,
			value->itv.lowptr, value->itv.highptr,
			value->start_time.epoch, value->start_time.step
		);
	}
}


bool task_unregister_dep(task_data_t *task, thread_data_t *thread, dep_t *dep) {
	std::list<dep_t *> *list;

	dep->task = NULL;

	switch (dep->type) {
		case DEP_NORMAL:
			list = &task->normal_deps;
			break;

		case DEP_RELEASE:
			list = &task->release_deps;
			break;

		default:
			error(thread, task,
				"Unsupported dependency type");
	}

	for (auto it = list->begin(); it != list->end(); ++it) {
		dep_t *other = *it;

		if (other == dep) {
			char dep_string[MAX_MSG_LENGTH];
			dep_str(dep, dep_string, MAX_MSG_LENGTH, task->thread, task);

			debug(2, thread, task,
				"Unregistered dependency %s",
				dep_string);

			list->erase(it);
			return true;
		}
	}

	return false;
}


void task_register_child_dep(task_data_t *task, thread_data_t *thread, dep_t *dep) {
	char dep_string[MAX_MSG_LENGTH];
	dep_str(dep, dep_string, MAX_MSG_LENGTH, task->thread, task);

	std::list<dep_t *> *list = &task->child_deps;

	list->push_back(dep);

	for (auto &i : dep->itvmap) {
		const dep_entry_t *value = &i.second;

		dep_entry_t new_value = *value;
		new_value.start_time = task->time;
		new_value.end_time = TS_MAX;

		// NOTE: It currently registers dependencies to the same object
		// from different tasks... maybe this is not what we want
		if (itvmap_insert(new_value, &task->itvmap_dep_children) == false) {
			error(thread, task,
				"Error while inserting child dependency.");
		}
	}

	debug(2, thread, task,
		"Registered child dependency %s",
		dep_string);
}


void task_update_accessmap(task_data_t *task, buf_entry_t *bentry, itvmap_t<acc_entry_t> *map) {
	if (config.aggregation_level == config.AGGREGATE_ADDR) {
		itvmap_insert_aggregated(
			itvmap_new_acc(
				bentry->mode,
				bentry->eaddr,
				bentry->eaddr + bentry->size,
				bentry->iptr,
				bentry->imgid,
				bentry->opcode,
				bentry->time
			),
			map,
			acc_aggregate_same_addr_and_pc_lvl2
		);
	}
	else if (config.aggregation_level == config.AGGREGATE_PC) {
		itvmap_insert_aggregated(
			itvmap_new_acc(
				bentry->mode,
				bentry->eaddr,
				bentry->eaddr + bentry->size,
				bentry->iptr,
				bentry->imgid,
				bentry->opcode,
				bentry->time
			),
			map,
			acc_aggregate_same_addr_and_pc_lvl1
		);
	}
	else {
		itvmap_insert(
			itvmap_new_acc(
				bentry->mode,
				bentry->eaddr,
				bentry->eaddr + bentry->size,
				bentry->iptr,
				bentry->imgid,
				bentry->opcode,
				bentry->time
			),
			map
		);
	}
}


void task_flush_raw_accesses_online(task_data_t *task, size_t numentries) {
	for (unsigned int i = 0; i < numentries; ++i) {
		buf_entry_t bentry = task->buff[i];

		if (filter_raw_access_1(&bentry, task)) {
			continue;
		}

		bool filtered = filter_raw_access_2(&bentry, task);

		debug(6, task->thread, task,
			"Dumping %s access at %p to %p + %u filtered: %d; rfz: %d",
			itv_mode_str(bentry.mode), (VOID *)bentry.iptr, (VOID *)bentry.eaddr, bentry.size,
			filtered, bentry.race_free_zone);

		if (bentry.race_free_zone == false && filtered == false) {
			task_update_accessmap(task, &bentry, &task->itvmap_common);
		}
		else if (bentry.race_free_zone == false && filtered) {
			task_update_accessmap(task, &bentry, &task->itvmap_childtask_only);
		}
		else if (bentry.race_free_zone && filtered == false) {
			task_update_accessmap(task, &bentry, &task->itvmap_sametask_only);
		}
	}
}


void task_flush_raw_accesses_offline(task_data_t *task, size_t numentries) {
	size_t count;

	if (utils_write_fd(task->tracefd, task->buff, numentries * sizeof(buf_entry_t), &count)) {
		error(task->thread, task,
			"Unable to write bytes to tracefile '%s'",
			task->tracename);
	}

	if (count != numentries * sizeof(buf_entry_t)) {
		error(task->thread, task,
			"Unable to write %u bytes to tracefile '%s' (%u bytes written)",
			numentries * sizeof(buf_entry_t), task->tracename, count);
	}

	// for (unsigned int i = 0; i < numentries; ++i) {
	// 	buf_entry_t bentry = task->buff[i];

	// 	if (filter_raw_access_1(&bentry, task)) {
	// 		continue;
	// 	}

	// 	bool filtered = filter_raw_access_2(&bentry, task);

	// 	debug(6, task->thread, task,
	// 		"Dumping %s access at %p to %p + %u filtered: %d; rfz: %d",
	// 		itv_mode_str(bentry.mode), (VOID *)bentry.iptr, (VOID *)bentry.eaddr, bentry.size,
	// 		filtered, bentry.race_free_zone);

	// 	size_t count;

	// 	if (utils_write_fd(task->tracefd, &bentry, sizeof(buf_entry_t), &count)) {
	// 		error(task->thread, task,
	// 			"Unable to write bytes from tracefile '%s'",
	// 			task->tracename);
	// 	}

	// 	if (count == 0) {
	// 		break;
	// 	}
	// }
}


void task_flush_raw_accesses(task_data_t *task) {
	size_t numentries = std::min((UINT32) BUFFER_NUM_ENTRIES, task->nextbuffentry);

	if (numentries == 0) {
		return;
	}

	debug(5, task->thread, task,
		"Current buffer size: %u / %u / %u, trace %p",
			task->nextbuffentry, numentries, BUFFER_NUM_ENTRIES, task->trace);

	if (config.engine == config.ENGINE_ONLINE) {
		task_flush_raw_accesses_online(task, numentries);
	} else {
		task_flush_raw_accesses_offline(task, numentries);
	}

	task->nextbuffentry = 0;

	ts_increment_epoch(&task->time);
}


void task_load_raw_accesses_online(task_data_t *task) {
	return;
}


void task_load_raw_accesses_offline(task_data_t *task) {
	uint64_t new_offset;

	if (utils_walk_fd(task->tracefd, "b", 0, &new_offset)) {
		error(task->thread, task,
			"Unable to rewind the tracefile '%s'",
			task->tracename);
	}

	while (true) {
		size_t count, numentries;
		buf_entry_t buff[BUFFER_NUM_ENTRIES];

		if (utils_read_fd(task->tracefd, &buff, sizeof(buf_entry_t) * BUFFER_NUM_ENTRIES, &count)) {
			error(task->thread, task,
				"Unable to read bytes from the tracefile '%s'",
				task->tracename);
		}

		if (count % sizeof(buf_entry_t) != 0) {
			error(task->thread, task,
				"Read an unexpected number of bytes from the tracefile '%s'",
				task->tracename);
		}
		else if (count == 0) {
			break;
		}

		numentries = count / sizeof(buf_entry_t);

		for (unsigned int i = 0; i < numentries; ++i) {
			buf_entry_t bentry = buff[i];

			if (filter_raw_access_1(&bentry, task)) {
				continue;
			}

			bool filtered = filter_raw_access_2(&bentry, task);

			debug(6, task->thread, task,
				"Dumping %s access at %p to %p + %u filtered: %d; rfz: %d",
				itv_mode_str(bentry.mode), (VOID *)bentry.iptr, (VOID *)bentry.eaddr, bentry.size,
				filtered, bentry.race_free_zone);

			if (bentry.race_free_zone == false && filtered == false) {
				task_update_accessmap(task, &bentry, &task->itvmap_common);
			}
			else if (bentry.race_free_zone == false && filtered) {
				task_update_accessmap(task, &bentry, &task->itvmap_childtask_only);
			}
			else if (bentry.race_free_zone && filtered == false) {
				task_update_accessmap(task, &bentry, &task->itvmap_sametask_only);
			}
		}
	}

	if (config.keep_traces == false) {
		if (utils_resize_fd(task->tracefd, 0)) {
			error(task->thread, task,
				"Unable to truncate tracefile '%s'",
				task->tracename);
		}
	}
}


void task_load_raw_accesses(task_data_t *task) {
	if (config.engine == config.ENGINE_ONLINE) {
		task_load_raw_accesses_online(task);
	} else {
		task_load_raw_accesses_offline(task);
	}
}


void task_save_raw_access(
	task_data_t *task, itv_mode_t mode, uint32_t opcode, uint64_t ip,
	uint64_t addr, uint32_t span, uint32_t imgid,
	uint64_t lowstack, uint64_t highstack
) {
	if (task->nextbuffentry == BUFFER_NUM_ENTRIES) {
		task_flush_raw_accesses(task);
	}

	task->buff[task->nextbuffentry++] = buf_entry_t{
		.mode = mode,
		.opcode = opcode,
		.iptr = ip,
		.eaddr = addr,
		.size = span,
		.imgid = imgid,
		.lowstack = lowstack,
		.highstack = highstack,
		.race_free_zone = task->race_free_zone,
		.time = task->time
	};

	ts_increment_step(&task->time);

	task->thread->zeros++;
}
