void process_init(int32_t pid) {
	char filepath[MAX_PATH_LENGTH];

	app.id = pid;

	snprintf(filepath, MAX_PATH_LENGTH,
		"%s/%s.main.log",
		config.logs_dir, config.experiment_name);

	char *tmp_file_path = (char *)malloc(strlen(filepath) + 1);
	strcpy(tmp_file_path, filepath);

	app.log_file_path = tmp_file_path;

	if (utils_open_fd(app.log_file_path, "w+", &app.log_fd)) {
		error(NULL, NULL,
			"Unable to create log file '%s'", app.log_file_path);
	}
}


void process_get_stack_boundaries(uint64_t rsp) {
	char cmd[MAX_COMMAND_LENGTH];
	int32_t status;
	err_t err;

	if (utils_open_pipe(&app.read_fd, &app.write_fd)) {
		error(NULL, NULL,
			"Unable to create pipe.");
	}

	unsigned long channels[3] = { STDIN_FD, app.write_fd, STDERR_FD };

	// Generate map
	sprintf(cmd,
		"cat /proc/%lu/maps > %s/%lu",
		app.id, config.procmaps_dir, app.id);

	debug(3, NULL, NULL,
		"Invoking `%s`", cmd);

	err = utils_spawn_process(cmd, true, &status);

	if (err == ERR_CANT_SPAWN_PROC) {
		error(NULL, NULL,
			"Unable to spawn process to execute command '%s'.", cmd);
	}
	else if (err == ERR_CANT_WAIT_PROC) {
		error(NULL, NULL,
			"Unable to wait for termination of command '%s'.", cmd);
	}
	else if (status != 0) {
		error(NULL, NULL,
			"Command '%s' exited with errors.", cmd);
	}

	// Retrieve stack addresses from map
	snprintf(cmd, MAX_COMMAND_LENGTH,
		"cat %s/%lu | grep '\\[stack\\]' | cut -d' ' -f  1",
		config.procmaps_dir, app.id);

	debug(3, NULL, NULL,
		"Invoking `%s`", cmd);

	err = utils_spawn_process(cmd, true, &status, channels);

	if (err == ERR_CANT_SPAWN_PROC) {
		error(NULL, NULL,
			"Unable to spawn process to execute command '%s'.", cmd);
	}
	else if (err == ERR_CANT_WAIT_PROC) {
		error(NULL, NULL,
			"Unable to wait for termination of command '%s'.", cmd);
	}
	else if (status != 0) {
		error(NULL, NULL,
			"Command '%s' exited with errors.", cmd);
	}

	// Load stack information
	size_t count;
	char stack_boundaries[MAX_OUTPUT_LENGTH];

	if (utils_read_fd(app.read_fd, stack_boundaries, MAX_OUTPUT_LENGTH, &count)) {
		error(NULL, NULL,
			"Unable to read %u bytes from pipe",
			MAX_OUTPUT_LENGTH);
	}

	stack_boundaries[strcspn(stack_boundaries, " ")] = '\0';
	stack_boundaries[strcspn(stack_boundaries, "\n")] = '\0';

	char *end_ptr;
	struct rlimit limits;
	getrlimit(RLIMIT_STACK, &limits);

	app.stack_high = strtoul(
		stack_boundaries, &end_ptr, 16);
	app.stack_low_base = strtoul(
		stack_boundaries + strcspn(stack_boundaries, "-") + 1, &end_ptr, 16);
	app.stack_low_main = rsp;
	app.stack_size = limits.rlim_cur;
}


void process_fini() {
	if (utils_close_fd(app.log_fd)) {
		error(NULL, NULL,
			"Unable to close log file '%s'", app.log_file_path);
	}
}


template <typename value_a_t>
value_a_t *find_by_itv(uint64_t low, uint64_t high, itvmap_t<value_a_t> *map) {
	itv_t key = itv_t{
		ITV_MODE_IGNORE,
		std::min(low, high),
		std::max(low, high)
	};

	auto it = map->lower_bound(key);

	if (it == map->end() /*|| low > it->first.high */ || high <= it->first.low) {
		if (it == map->begin()) {
			return NULL;
		}
		else {
			--it;
		}
	}

	if (low >= it->first.high || high <= it->first.low) {
		return NULL;
	}

	if (!itv_contains_itv(&it->second.itv, &key)) {
		return NULL;
	}

	return &it->second;
}


img_t *image_register(uint64_t low, uint64_t high, const char *name, img_type_t type, img_level_t level) {
	char *img_name;
	uint32_t img_name_size = strlen(name) + 1;

	img_name = (char *) malloc(sizeof(char) * img_name_size);
	snprintf(img_name, img_name_size, "%s", name);

	img_t *img = new img_t{
		extent_low : low,
		extent_high : high,
		id : 0,
		name : name,
		type : type,
		level : level,
		filtered : false,
		invalid : false,
		has_final_vmas : false,
		next_sec_id : 0,
		next_fun_id : 0
	};

	img_entry_t img_entry;

	img_entry.itv.mode = ITV_MODE_IGNORE;
	img_entry.itv.low = low;
	img_entry.itv.high = high;

	img_entry.img = img;

	itvmap_insert(img_entry, &img->itvmap);

	PIN_GetLock(&app.locks.images, 1);
	// if (itvmap_insert_disjoint(img_entry, &app.img_map)) {
	// FIXME: Gestire le immagini con frammenti! (es. VDSO - Loader)
	if (itvmap_insert(img_entry, &app.img_map)) {
		app.images.push_back(img);
		img->id = app.next_img_id++;

		PIN_ReleaseLock(&app.locks.images);
		return img;
	}

	PIN_ReleaseLock(&app.locks.images);
	delete img;
	return NULL;
}


void image_unregister(img_t *img) {
	// The image is kept in the ID map, but its fragments are
	// removed from the shared image address map.
	// A flag is raised meaning that the image is 'invalid'

	// FIXME: It should be possible to remove the image descriptor
	// from memory along with all section and variable descriptors,
	// but for some reason this causes a segfault somewhere that
	// must be investigated

	PIN_GetLock(&app.locks.images, 1);

	// Remove all sections from the image
	// for (auto it : img->sec_map) {
	// 	sec_entry_t *sec_entry = &it.second;
	// 	sec_t *sec = sec_entry->sec;

	// 	for (auto jt : sec->var_map) {
	// 		var_entry_t *var_entry = &jt.second;
	// 		var_t *var = var_entry->var;

	// 		free((void *)var->name);
	// 		delete var;
	// 	}

	// 	delete sec;
	// }

	// Remove all fragments from the shared image map
	for (auto it : img->itvmap) {
		img_entry_t *img_entry = &it.second;
		itvmap_remove(*img_entry, &app.img_map);
	}

	// app.images.at(img->id) = NULL;

	img->invalid = true;

	PIN_ReleaseLock(&app.locks.images);

	// delete img;
}


img_t *image_find_by_id(uint64_t id) {
	img_t *img;

	img = NULL;

	PIN_GetLock(&app.locks.images, 1);
	if (id < app.images.size()) {
		img = app.images[id];
	}
	PIN_ReleaseLock(&app.locks.images);

	return img;
}


img_t *image_find_by_itv(uint64_t low, uint64_t high) {
	img_entry_t *value;
	img_t *img;

	PIN_GetLock(&app.locks.images, 1);
	value = find_by_itv(low, high, &app.img_map);
	img = value != NULL ? value->img : NULL;
	PIN_ReleaseLock(&app.locks.images);

	return img;
}


INLINE
img_t *image_find_by_addr(uint64_t addr) {
	return image_find_by_itv(addr, addr + 1);
}


INLINE
bool image_has_itv(img_t *img, uint64_t low, uint64_t high) {
	return img->extent_low <= low && img->extent_high >= high;
}


INLINE
bool image_has_addr(img_t *img, uint64_t addr) {
	return image_has_itv(img, addr, addr);
}


/*
	A low image is an image that never belongs to task code.
	It can be any library that is required to run an application,
	but not its business logic.
	FIXME: Some implementations of Pthread use HTM transactions
	which are not fully supported by Pin (?)
 */
INLINE
bool image_is_low(const char *name) {
	string img_name = name;

	return (
		   img_name.find("libnanos6" , 0) != string::npos // Nanos6
		|| img_name.find("ld-linux"  , 0) != string::npos // Linux loader
		|| img_name.find("libdl"     , 0) != string::npos // Dynamic linking
		|| img_name.find("libpthread", 0) != string::npos // Pthread
		//|| img_name.find("libtmi"    , 0) != string::npos // MPI
		//|| img_name.find("libmpi"    , 0) != string::npos // MPI
		|| img_name.find("[vdso]"    , 0) != string::npos // VDSO
		// || img_name.find("libc"      , 0) != string::npos
	);
}


sec_t *section_register(uint64_t low, uint64_t high, const char *name,
                        sec_type_t type, sec_mode_t mode, img_t *img) {
	char *sec_name;
	uint32_t sec_name_size = strlen(name) + 1;

	sec_name = (char *) malloc(sizeof(char) * sec_name_size);
	snprintf(sec_name, sec_name_size, "%s", name);

	sec_t *sec = new sec_t{
		extent_low : low,
		extent_high : high,
		id : 0,
		name : name,
		type : type,
		mode : mode,
		next_var_id : 0,
		img : img
	};

	sec_entry_t sec_entry;

	sec_entry.itv.mode = ITV_MODE_IGNORE;
	sec_entry.itv.low = low;
	sec_entry.itv.high = high;

	sec_entry.sec = sec;

	if (itvmap_insert_disjoint(sec_entry, &img->sec_map)) {
		sec->id = img->next_sec_id++;
		return sec;
	}

	delete sec;
	return NULL;
}


sec_type_t section_fix_type(const char *name, sec_type_t type) {
	string sec_name = name;

	if (
		   sec_name.find(".altinstructions" , 0) != string::npos
		|| sec_name.find(".comment"         , 0) != string::npos
		|| sec_name.find(".debug"           , 0) != string::npos
		|| sec_name.find(".dynamic"         , 0) != string::npos
		|| sec_name.find(".dynstr"          , 0) != string::npos
		|| sec_name.find(".dynsym"          , 0) != string::npos
		|| sec_name.find(".eh_frame"        , 0) != string::npos
		|| sec_name.find(".got"             , 0) != string::npos
		|| sec_name.find(".hash"            , 0) != string::npos
		|| sec_name.find(".interp"          , 0) != string::npos
		|| sec_name.find(".line"            , 0) != string::npos
		|| sec_name.find(".note"            , 0) != string::npos
		|| sec_name.find(".rel"             , 0) != string::npos
		|| sec_name.find(".shstrtab"        , 0) != string::npos
		|| sec_name.find(".strtab"          , 0) != string::npos
		|| sec_name.find(".symtab"          , 0) != string::npos
		|| sec_name.find(".version"         , 0) != string::npos
	) {
		return SEC_TYPE_METADATA;
	}
	else if (
		   sec_name.find(".altinstr_replacement"    , 0) != string::npos
		|| sec_name.find(".fini"                    , 0) != string::npos
		|| sec_name.find(".init"                    , 0) != string::npos
		|| sec_name.find(".plt"                     , 0) != string::npos
	) {
		return SEC_TYPE_METACODE;
	}

	return type;
}


sec_t *section_find_by_itv(uint64_t low, uint64_t high) {
	img_t *img;

	sec_entry_t *value;
	sec_t *sec;

	img = image_find_by_itv(low, high);

	if (img == NULL) {
		// printf("Image not found for addr <%p,%p>\n", (void *)low, (void *)high);
		return NULL;
	}

	value = find_by_itv(low, high, &img->sec_map);
	sec = value != NULL ? value->sec : NULL;

	return sec;
}


INLINE
sec_t *section_find_by_addr(uint64_t addr) {
	return section_find_by_itv(addr, addr + 1);
}


#if 0
bool dym_already_mapped(const dym_entry_t *val, const dym_entry_t *other,
                        uint64_t low, uint64_t high, const itvmap_t<dym_entry_t> *map) {
	return other->type != DYM_TYPE_MALLOC && val->itv.low  >= other->itv.low
	                                      && val->itv.high <= other->itv.high;
}


bool dynmem_register(uint64_t low, uint64_t high, dym_type_t type,
                     img_t *img, task_data_t *task, itvmap_t<dym_entry_t> *dym_map) {
	dym_entry_t dym_entry;

	dym_entry.itv.mode = ITV_MODE_IGNORE;
	dym_entry.itv.low = low;
	dym_entry.itv.high = high;

	dym_entry.type = type;
	dym_entry.img = img;
	// dym_entry.thread = thread;
	dym_entry.task = task;

	bool res;

	PIN_GetLock(&app.locks.dynmem, 1);
	{
		// if (type == DYM_TYPE_MALLOC) {
		// 	size_t num_already_mapped;

		// 	num_already_mapped = itvmap_process_touching(
		// 		&dym_entry, dym_map,
		// 		dym_already_mapped, itvmap_dummy_true_proc);

		// 	if (num_already_mapped == 1) {
		// 		// There must be exactly one region already mapped
		// 		res = itvmap_insert(dym_entry, dym_map);
		// 	} else {
		// 		res = false;
		// 	}
		// }
		// else {
			res = itvmap_insert_disjoint(dym_entry, dym_map);
		// }
	}
	PIN_ReleaseLock(&app.locks.dynmem);

	return res;
}


bool dym_correspond_and_same_type(const dym_entry_t *val, const dym_entry_t *other,
                                uint64_t low, uint64_t high, const itvmap_t<dym_entry_t> *map) {
	return val->itv == other->itv && val->type == other->type;
}


bool dym_intersect_and_same_type(const dym_entry_t *val, const dym_entry_t *other,
                             uint64_t low, uint64_t high, const itvmap_t<dym_entry_t> *map) {
	return low < high && val->type == other->type;
}


bool dynmem_unregister(uint64_t low, uint64_t high, dym_type_t type, itvmap_t<dym_entry_t> *dym_map) {
	dym_entry_t dym_entry;

	dym_entry.itv.mode = ITV_MODE_IGNORE;
	dym_entry.itv.low = low;
	dym_entry.itv.high = high;

	dym_entry.type = type;

	bool res;

	PIN_GetLock(&app.locks.dynmem, 1);
	{
		// if (type == DYM_TYPE_MALLOC) {
		// 	res = itvmap_remove_conditional(
		// 		dym_entry, dym_map, dym_correspond_and_same_type);
		// }
		// else {
			res = itvmap_remove_fragmented(
				dym_entry, dym_map, dym_intersect_and_same_type);
		// }
	}
	PIN_ReleaseLock(&app.locks.dynmem);

	return res;
}


dym_entry_t dynmem_find_by_itv(uint64_t low, uint64_t high, itvmap_t<dym_entry_t> *dym_map) {
	dym_entry_t *value;
	dym_entry_t res;

	PIN_GetLock(&app.locks.dynmem, 1);
	{
		value = find_by_itv(low, high, dym_map);

		if (value == NULL) {
			res.itv.mode = ITV_MODE_NONE;
			res.itv.low = 0;
			res.itv.high = 0;

			res.type = DYM_TYPE_NONE;
			res.img = NULL;
			// res.thread = NULL;
			res.task = NULL;
		}
		else {
			res = *value;
		}
	}
	PIN_ReleaseLock(&app.locks.dynmem);

	return res;
}


INLINE
dym_entry_t dynmem_find_by_addr(uint64_t addr, itvmap_t<dym_entry_t> *dym_map) {
	return dynmem_find_by_itv(addr, addr + 1, dym_map);
}
#endif


bool malloc_register(uint64_t addr, size_t size) {
	bool res;

	PIN_GetLock(&app.locks.malloc, 1);
	{
		res = app.malloc_map.insert(std::pair<uint64_t, size_t>(addr, size)).second;
	}
	PIN_ReleaseLock(&app.locks.malloc);

	return res;
}


size_t malloc_get_size(uint64_t addr) {
	size_t size;

	PIN_GetLock(&app.locks.malloc, 1);
	{
		auto it = app.malloc_map.find(addr);

		if (it != app.malloc_map.end()) {
			size = it->second;
		} else {
			size = 0;
		}
	}
	PIN_ReleaseLock(&app.locks.malloc);

	return size;
}


bool malloc_unregister(uint64_t addr) {
	bool res;

	PIN_GetLock(&app.locks.malloc, 1);
	{
		res = app.malloc_map.erase(addr);
	}
	PIN_ReleaseLock(&app.locks.malloc);

	return res;
}


#if 0
fun_t *function_register(uint64_t low, uint64_t high, const char *name,
                         fun_type_t type, img_t *img) {
	fun_t *fun = new fun_t{
		extent_low : low,
		extent_high : high,
		id : 0,
		name : name,
		type : type,
		img : img,
		filtered : 0
	};

	fun_entry_t fun_entry;

	fun_entry.itv.mode = ITV_MODE_IGNORE;
	fun_entry.itv.low = low;
	fun_entry.itv.high = high;

	fun_entry.fun = fun;

	if (itvmap_insert(fun_entry, &img->fun_map)) {
		fun->id = img->next_fun_id++;
		return fun;
	}

	delete fun;
	return NULL;
}


fun_t *function_find_by_itv(uint64_t low, uint64_t high) {
	img_t *img;

	img = image_find_by_itv(low, high);

	if (img == NULL) {
		return NULL;
	}

	auto it = img->fun_map.lower_bound(itv_t{
		ITV_MODE_IGNORE,
		std::min(low, high),
		std::max(low, high)
	});

	if (it == img->fun_map.begin()) {
		return NULL;
	}

	it--;

	return it->second.fun;
}


INLINE
fun_t *function_find_by_addr(uint64_t addr) {
	return function_find_by_itv(addr, addr + 1);
}
#endif


var_t *variable_register(uint64_t low, uint64_t high, const char *name,
                         var_scope_t scope, sec_t *sec) {
	char *var_name;
	uint32_t var_name_size = strlen(name) + 1;

	var_name = (char *) malloc(sizeof(char) * var_name_size);
	snprintf(var_name, var_name_size, "%s", name);

	var_t *var = new var_t{
		extent_low : low,
		extent_high : high,
		id : 0,
		name : var_name,
		scope : scope,
		sec : sec
	};

	var_entry_t var_entry;

	var_entry.itv.mode = ITV_MODE_IGNORE;
	var_entry.itv.low = low;
	var_entry.itv.high = high;

	var_entry.var = var;

	if (itvmap_insert(var_entry, &sec->var_map)) {
		var->id = sec->next_var_id++;
		return var;
	}

	delete var;
	return NULL;
}


var_t *variable_find_by_itv(uint64_t low, uint64_t high) {
	sec_t *sec;

	var_entry_t *value;
	var_t *var;

	sec = section_find_by_itv(low, high);

	if (sec == NULL) {
		return NULL;
	}

	value = find_by_itv(low, high, &sec->var_map);
	var = value != NULL ? value->var : NULL;

	return var;
}


INLINE
var_t *variable_find_by_addr(uint64_t addr) {
	return variable_find_by_itv(addr, addr + 1);
}


#if 0
	procstackbase = (((PIN_GetContextReg(ctxt, REG_STACK_PTR)) >> 12) << 12) + (1ULL << 13);
	procstackbase = (((rsp) >> 12) << 12) + (1ULL << 12);

	// Walk the environment variables list
	ADDRINT *envp = &argv[argc + 1];
	UINT64 envc;
	for (envc = 0; envp[envc] != 0; ++envc);

	// Walk the auxiliary vector
	ADDRINT *auxv = &envp[envc + 1];
	UINT64 auxc;
	for (auxc = 0; auxv[auxc] != 0; ++auxc);

	procstackbase = (ADDRINT) &envp[envc + 1];
	ADDRINT procstackbase2 = (ADDRINT) &auxv[auxc + 1];
	ADDRINT procstackbase3 = rsp;
	// procstackbase = procstackbase & (~(1ULL << 12) - 1);
	procstackbase = (((procstackbase + (1ULL << 12)) >> 12) << 12);
	procstackbase2 = (((procstackbase2 + (1ULL << 12)) >> 12) << 12);
	procstackbase3 = (((procstackbase3 + (1ULL << 12)) >> 12) << 12);

	struct rlimit limits;
	getrlimit(RLIMIT_STACK, &limits);

	procstacksize = limits.rlim_cur;

	debug(2, -1, -1, "PROCESS STACK REGION is <%p,%p,%p>",
		procstackbase, procstackbase2, procstackbase3);
#endif


#if 0
INLINE
bool isNanosAPI(img_t *img, const char *api) {
	string rtn_name = api;

	return (
		   (app.nanos_img
		    && img->id == app.nanos_img->id && (
			     rtn_name.compare("nanos6_block_current_task") == 0
			  || rtn_name.compare("nanos6_unblock_task")       == 0
			  || rtn_name.compare("nanos6_in_final")           == 0
			  || rtn_name.compare("nanos6_spawn_function")     == 0
			  || rtn_name.compare("nanos6_create_task")        == 0
			  || rtn_name.compare("nanos6_submit_task")        == 0
			  || rtn_name.compare("nanos6_taskwait")           == 0
			  || rtn_name.compare("nanos6_user_lock")          == 0
			  || rtn_name.compare("nanos6_user_unlock")        == 0
			  || rtn_name.find("nanos6_release_")              == 0
		))
		|| (app.nanos_loader_img
		    && img->id == app.nanos_loader_img->id && (
			     rtn_name.find("_nanos")                      == 0
			  || rtn_name.find("nanos6")                      == 0
		))
	);
}
#endif
