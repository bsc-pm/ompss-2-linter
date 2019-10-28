INLINE
thread_data_t *thread_create(uint32_t id, uint64_t stackbase) {
	thread_data_t *thread = new thread_data_t;

	thread->id = id;
	thread->task = NULL;
	thread->stackbase = stackbase;
	thread->calls = thread->zeros = 0;
	thread->next_task_verified = false;

	thread->dymreq.type = DYNMEM_REQUEST_NONE;

	if (utils_open_pipe(&thread->read_fd, &thread->write_fd)) {
		error(thread, NULL,
			"Unable to create pipe.");
	}

	char filepath[MAX_PATH_LENGTH];
	snprintf(filepath, MAX_PATH_LENGTH,
		"%s/%s.%u.log",
		config.logs_dir, config.experiment_name, thread->id);

	char *tmp_file_path = (char *)malloc(strlen(filepath) + 1);
	strcpy(tmp_file_path, filepath);

	thread->log_file_path = tmp_file_path;

	if (utils_open_fd(thread->log_file_path, "w+", &thread->log_fd)) {
		error(thread, NULL,
			"Unable to create log file '%s'", thread->log_file_path);
	}

	return thread;
}


INLINE
void thread_destroy(thread_data_t *thread) {
	if (utils_close_fd(thread->read_fd)) {
		error(thread, NULL,
			"Unable to close read end of pipe.");
	}

	if (utils_close_fd(thread->write_fd)) {
		error(thread, NULL,
			"Unable to close read end of pipe.");
	}

	if (utils_close_fd(thread->log_fd)) {
		error(thread, NULL,
			"Unable to close log file '%s'", thread->log_file_path);
	}

	delete thread;
}


INLINE
void thread_push_task(thread_data_t *thread, task_data_t *task) {
	// if (thread->task) {
		// If we were executing another task (a parent task)
		// we temporarily store its meta-data.
		// We need this check because when the 'main' task is
		// executed for the first time, the current thread doesn't
		// have a binding to a task yet.
		task->prev = thread->task;
	// }

	// The thread is now executing another task
	thread->task = task;
}


INLINE
void thread_pop_task(thread_data_t *thread) {
	// Restore any parent task that was suspended to execute the
	// current task
	thread->task = thread->task->prev;
}


INLINE
void thread_write_log(const char *format, ...) {
	
}
