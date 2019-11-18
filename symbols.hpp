/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

typedef struct symbol_raw {
	unsigned int        id;
	size_t              namelen;
	unsigned int        local;
	unsigned long long  baddr;
	size_t              size;
	size_t              secndx;
	const char         *name;
} symbol_raw_t;


typedef struct section_raw {
	size_t              id;
	size_t              namelen;
	unsigned long long  baddr;
	size_t              size;
	size_t              numsyms;
	const char         *name;
} section_raw_t;

// typedef struct var {
// 	symbol_raw_t        sym;
// 	SEC                &sec;
// } var_t;

// std::vector<var_t> variables;


// class RequestStruct(Structure):
//     _pack_ = 8
//     _fields_ = [
//         ("type", c_uint64),
//         ("arg1", c_uint64),
//         ("arg2", c_uint64)
//     ]


// class ResponseStruct(Structure):
//     _fields_ = [
//         ("type", c_uint64),
//         ("data", c_char * 1024),
//     ]


VOID symbols_generate_map(img_t *img, const char *filename) {
	char cmd[MAX_COMMAND_LENGTH];
	int32_t status;

	snprintf(cmd, MAX_COMMAND_LENGTH,
		"./extractsymbols %s %s > /dev/null",
		img->name, filename);

	debug(3, NULL, NULL,
		"Invoking `%s`", cmd);

	err_t err = utils_spawn_process(cmd, true, &status);

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
}


void symbols_recover_map(img_t *img, const char *filename) {
	long unsigned int map_fd;
	size_t filesize;

	size_t numbytes;
	size_t readbytes;

	// char *sec_name, *sym_name;
	sec_t *sec;
	// char namex[256];

	section_raw_t secr;
	symbol_raw_t symr;

	// size_t count;
	unsigned int i;
	ADDRINT baddr, low, high;

	if (utils_open_fd(filename, "r", &map_fd)) {
		error(NULL, NULL,
			"Unable to open map file '%s'\n",
			filename);
	}

	filesize = 0;

	if (utils_get_size_fd(map_fd, &filesize)) {
		error(NULL, NULL,
			"Unable to walk map file '%s'\n",
			filename);
	}

	if (filesize == 0) {
		error(NULL, NULL,
			"Empty map file '%s'\n",
			filename);
	}

	debug(3, NULL, NULL, "Reading mapfile '%s' (%lu bytes)",
		filename, filesize);

	while (true) {
		numbytes = sizeof(section_raw_t);

		if (utils_read_fd(map_fd, &secr, numbytes, &readbytes)) {
			error(NULL, NULL,
				"Unable to read %u bytes of section metadata from mapfile '%s'",
				sizeof(section_raw_t), filename);
		}

		if (readbytes == 0) {
			break;
		} else {
			assert(readbytes == sizeof(section_raw_t));
		}

		numbytes = secr.namelen + 1;
		char sec_name[numbytes];

		if (utils_read_fd(map_fd, sec_name, numbytes, &readbytes)) {
			error(NULL, NULL,
				"Unable to read %u bytes of section name from mapfile '%s'",
				secr.namelen + 1, filename);
		}

		assert(readbytes == secr.namelen + 1);

		if (secr.baddr > img->extent_low) {
			baddr = secr.baddr;
		} else {
			baddr = img->extent_low + secr.baddr;
		}

		sec = section_find_by_addr(baddr);

		if (sec == NULL) {
			debug(3, NULL, NULL,
				"Unable to find section '%s' from mapfile.",
				sec_name);
			// continue;
		}
		else {
			debug(3, NULL, NULL,
				"Recovered data section %s (id %u) -> %p + %u (%lu symbols)",
				sec_name, secr.id, secr.baddr, secr.size, secr.numsyms);
		}

		for (i = 0; i < secr.numsyms; ++i) {
			numbytes = sizeof(symbol_raw_t);

			if (utils_read_fd(map_fd, &symr, numbytes, &readbytes)) {
				error(NULL, NULL,
					"Unable to read %u bytes of symbol metadata from mapfile '%s'",
					sizeof(symbol_raw_t), filename);
			}

			if (readbytes == 0) {
				break;
			} else {
				assert(readbytes == sizeof(symbol_raw_t));
			}

			numbytes = symr.namelen + 1;
			char sym_name[numbytes];
			// char *sym_name = (char *)malloc(numbytes);

			if (utils_read_fd(map_fd, sym_name, numbytes, &readbytes)) {
				error(NULL, NULL,
					"Unable to read %u bytes of symbol name from mapfile '%s'",
					symr.namelen + 1, filename);
			}

			assert(readbytes == symr.namelen + 1);

			if (sec == NULL) {
				continue;
			}

			low  = baddr + (symr.baddr - secr.baddr);
			high = low + symr.size;

			variable_register(low, high, sym_name, symr.local ? VAR_SCOPE_LOCAL : VAR_SCOPE_GLOBAL, sec);

			debug(4, NULL, NULL,
				"Registered new %s variable %s <%p (%p) + %lu> -> <%p (%p) + %lu>",
				symr.local ? "LOCAL" : "GLOBAL", sym_name,
				symr.baddr, secr.baddr, symr.size,
				low, baddr, symr.size);
		}
	}
}
