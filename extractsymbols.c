#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <elf.h>
#include <gelf.h>

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

union data {
	symbol_raw_t      symr;
	section_raw_t     secr;
};

struct node {
	union data    payload;
	struct node  *prev;
	struct node  *next;
};

typedef struct list {
	struct node  *start;
	struct node  *end;
} list_t;


void list_init(list_t *list) {
	list->start = list->end = NULL;
}

struct node *list_append(list_t *list, union data payload) {
	struct node *node = malloc(sizeof(struct node));
	node->prev = node->next = NULL;
	node->payload = payload;

	if (list->end == NULL) {
		list->start = list->end = node;
	} else {
		list->end->next = node;
		node->prev = list->end;
		list->end = node;
	}

	assert( (list->start == NULL && list->end == NULL) ||
	        (list->start != NULL && list->end != NULL) );

	return node;
}

void list_remove(list_t *list, struct node *node) {
	if (node->prev) {
		node->prev->next = node->next;
	} else {
		list->start = node->next;
	}

	if (node->next) {
		node->next->prev = node->prev;
	} else {
		list->end = node->prev;
	}

	assert( (list->start == NULL && list->end == NULL) ||
	        (list->start != NULL && list->end != NULL) );

	free(node);
}


void extract_data(Elf *elf, list_t *sections, list_t *symbols) {
	size_t shstrndx;

	unsigned int i;
	Elf_Scn *scn;
	GElf_Shdr shdr;
	const char *secname;
	struct node *secnode;

	Elf_Data *data;
	size_t count;

	GElf_Sym sym;
	const char *symname;
	struct node *symnode;

	union data payload;

	elf_getshdrstrndx(elf, &shstrndx);

	for (i = 1, scn = NULL; (scn = elf_nextscn(elf, scn)) != NULL; ++i) {
		gelf_getshdr(scn, &shdr);

		secname = elf_strptr(elf, shstrndx, shdr.sh_name);

		// Data sections---saved in a temporary list for convenience
		if (shdr.sh_type == SHT_PROGBITS || shdr.sh_type == SHT_NOBITS) {
			section_raw_t secr = {
				i, strlen(secname), shdr.sh_addr, shdr.sh_size, 0, secname
			};

			payload.secr = secr;

			printf("Section %s (id %u) -> %p + %u\n",
				secname, i, shdr.sh_addr, shdr.sh_size);

			secnode = list_append(sections, payload);
		}

		// Symbol tables---parsed to extract symbol information
		if (shdr.sh_type == SHT_SYMTAB || shdr.sh_type == SHT_DYNSYM) {
			data = elf_getdata(scn, NULL);
			count = shdr.sh_size / shdr.sh_entsize;

			printf("Symbol table %s (count %u)\n",
				secname, count);

			for (unsigned int j = 0; j < count; ++j) {
				gelf_getsym(data, j, &sym);

				symname = elf_strptr(elf, shdr.sh_link, sym.st_name);

				if (GELF_ST_TYPE(sym.st_info) == STT_OBJECT) {
					symbol_raw_t symr = {
						j, strlen(symname), GELF_ST_BIND(sym.st_info) == STB_LOCAL,
							sym.st_value, sym.st_size, sym.st_shndx, symname
					};

					payload.symr = symr;

					printf("\tSymbol %s (id %u) %s -> %p + %u, in section %u\n",
						symname, j,
							GELF_ST_BIND(sym.st_info) == STB_LOCAL ? "LOCAL" : "GLOBAL",
								sym.st_value, sym.st_size, sym.st_shndx);

					symnode = list_append(symbols, payload);
				}
			}
		}
	}

	for (secnode = sections->start; secnode; secnode = secnode->next) {
		section_raw_t *secr = &secnode->payload.secr;

		for (symnode = symbols->start; symnode; symnode = symnode->next) {
			symbol_raw_t *symr = &symnode->payload.symr;

			// if (GELF_ST_TYPE(sym.st_info) == STT_OBJECT) {
				if (symr->secndx == secr->id) {
					secr->numsyms += 1;
				}
			// }
		}
	}
}


void dump_data(FILE *map, list_t *sections, list_t *symbols) {
	struct node *secnode, *symnode, *symnode_prev;

	section_raw_t secr;
	symbol_raw_t symr;

	// char zero = '\0';

	for (secnode = sections->start; secnode; secnode = secnode->next) {
		secr = secnode->payload.secr;

		printf("Section %s (id %u) -> %p + %u (numsyms %lu)\n",
			secr.name, secr.id, secr.baddr, secr.size, secr.numsyms);

		fwrite(&secr, sizeof(section_raw_t), 1, map);
		fwrite(secr.name, secr.namelen + 1, 1, map);
		// fwrite(&zero, 1, 1, map);

		symnode = symbols->start;
		while (symnode) {
			symr = symnode->payload.symr;

			if (symr.secndx == secr.id) {
				printf("\tSymbol %s (id %u) %s -> %p + %u\n",
					symr.name, symr.id, symr.local ? "LOCAL" : "GLOBAL", symr.baddr, symr.size);

				fwrite(&symr, sizeof(symbol_raw_t), 1, map);
				fwrite(symr.name, symr.namelen + 1, 1, map);
				// fwrite(&zero, 1, 1, map);

				symnode_prev = symnode;
				symnode = symnode->next;

				list_remove(symbols, symnode_prev);
			} else {
				symnode = symnode->next;
			}
		}
	}
}

void recover_data(FILE *map) {
	section_raw_t secr;
	symbol_raw_t symr;

	size_t count;
	unsigned int i;

	char name[2048];

	printf("\n");

	while ((count = fread(&secr, sizeof(section_raw_t), 1, map)) == 1) {
		count = fread(&name, secr.namelen + 1, 1, map);
		assert(count == 1);

		printf("Section %s (id %u) -> %p + %u\n",
			name, secr.id, secr.baddr, secr.size);

		for (i = 0; i < secr.numsyms; ++i) {
			count = fread(&symr, sizeof(symbol_raw_t), 1, map);
			assert(count == 1);

			count = fread(&name, symr.namelen + 1, 1, map);
			assert(count == 1);

			printf("\tSymbol %s (id %u) -> %p + %u\n",
				name, symr.id, symr.baddr, symr.size);
		}
	}
}


int main(int argc, char **argv) {
	char *filename, *mapname;

	int fd;
	Elf *elf;
	FILE *map;

	list_t sections, symbols;

	if (argc <= 2) {
		printf("Please provide a valid filename and a valid path for the mapfile.\n");
		exit(-1);
	}

	filename = argv[1];
	mapname = argv[2];

	printf("Writing mapfile '%s'\n", mapname);

	list_init(&sections);
	list_init(&symbols);

	elf_version(EV_CURRENT);

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		printf("Unable to open filename '%s' for reading.\n",
			filename);
		exit(-1);
	}

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf == NULL) {
		printf("Unable to load ELF file '%s'.\n",
			filename);
		exit(-1);
	}

	map = fopen(mapname, "wb");
	if (map == NULL) {
		printf("Unable to open mapfile for writing.\n",
			mapname);
		exit(-1);
	}

	extract_data(elf, &sections, &symbols);

	dump_data(map, &sections, &symbols);

	map = freopen(mapname, "rb", map);
	if (map == NULL) {
		printf("Unable to re-open mapfile for writing.\n",
			mapname);
		exit(-1);
	}

	recover_data(map);

	fclose(map);
	elf_end(elf);
	close(fd);
}
