/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

// Maximum number of buffer entries per task
#define BUFFER_NUM_ENTRIES      (config.buffer_size * utils_get_os_page_size() / sizeof(buf_entry_t))


INLINE
buf_entry_t *buffer_create(void) {
	buf_entry_t *buffer = (buf_entry_t *) calloc(sizeof(buf_entry_t), BUFFER_NUM_ENTRIES);

	return buffer;
}


INLINE
void buffer_destroy(buf_entry_t *buffer) {
	free(buffer);
}

bool filter_raw_access_1(buf_entry_t *bentry, task_data_t *task);
/*
	Flush the current task buffer to disk, into the tracefile.
	The buffer can then be used to store new entries.
 */
void buffer_flush(task_data_t *task) {
	size_t numentries = std::min((UINT32) BUFFER_NUM_ENTRIES, task->nextbuffentry);

	debug(5, task->thread, task,
		"Current buffer size: %u / %u / %u, trace %p",
			task->nextbuffentry, numentries, BUFFER_NUM_ENTRIES, task->trace);

	if (numentries == 0) {
		return;
	}

	for (unsigned int i = 0; i < numentries; ++i) {
		buf_entry_t bentry = task->buff[i];

		if (filter_raw_access_1(&bentry, task)) {
			continue;
		}

		size_t count = sizeof(buf_entry_t);

		if (OS_WriteFD(task->tracefd, &bentry, &count)
			.generic_err == OS_RETURN_CODE_FILE_WRITE_FAILED) {
			error(NULL, NULL,
				"Unable to write %u bytes from tracefile '%s'",
				sizeof(buf_entry_t), task->tracename);
		}

		if (count == 0) {
			break;
		}

		debug(6, task->thread, task,
			"Dumping %s access at %p to %p + %u (%s)",
			itv_mode_str(bentry.mode), (VOID *)bentry.iptr, (VOID *)bentry.eaddr, bentry.size);
	}

	// if (fwrite(task->buff, sizeof(buf_entry_t), numentries, task->trace) == 0) {
	// 	error("Unable to write %u entries to tracefile '%s'",
	// 		numentries, task->tracename);
	// }

	// size_t count = sizeof(buf_entry_t) * numentries;

	// if (OS_WriteFD(task->tracefd, task->buff, &count)
	// 	.generic_err == OS_RETURN_CODE_FILE_WRITE_FAILED) {
	// 	error("Unable to write %u entries to tracefile '%s'",
	// 		numentries, task->tracename);
	// }

	task->nextbuffentry = 0;

	ts_increment_epoch(&task->time);
}


void buffer_store(task_data_t *task, itv_mode_t mode, uint32_t opcode, uint64_t ip,
                  uint64_t addr, uint32_t span, uint32_t imgid,
                  uint64_t lowstack, uint64_t highstack) {
	if (task->nextbuffentry == BUFFER_NUM_ENTRIES) {
		buffer_flush(task);
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
