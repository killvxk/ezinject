#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <elf.h>
#include <link.h>

#include "util.h"

struct elfparse_info
{
	char *path;
	int fd;
	void *mapping;
	size_t len;

	ElfW(Ehdr) *ehdr;

	char *strtab;
	ElfW(Sym) *symtab;
	int symtab_entries;

	char *dynstr;
	ElfW(Sym) *dynsym;
	int dynsym_entries;
};

static void elfparse_parse(struct elfparse_info *hndl);

void *elfparse_createhandle(const char *procpath)
{
	struct elfparse_info *hndl = malloc(sizeof(struct elfparse_info));
	if(!hndl) return 0;
	memset(hndl, 0, sizeof(struct elfparse_info));
	hndl->path = strdup(procpath);
	if(!hndl->path)
		goto free_mem;
	hndl->fd = open(hndl->path, O_RDONLY);
	if(hndl->fd < 0)
		goto free_path;
	hndl->len = lseek(hndl->fd, 0, SEEK_END);
	lseek(hndl->fd, 0, SEEK_SET);
	hndl->mapping = mmap(0, hndl->len, PROT_READ, MAP_SHARED, hndl->fd, 0);
	if(hndl->mapping == MAP_FAILED)
		goto free_path;
	elfparse_parse(hndl);
	return hndl;
free_path:
	free(hndl->path);
free_mem:
	free(hndl);
	return 0;
}

static void elfparse_parse(struct elfparse_info *hndl)
{
	ElfW(Ehdr) *ehdr = hndl->mapping;
	hndl->ehdr = ehdr;
	DBG("e_ident=%s", ehdr->e_ident);
	DBG("e_phoff=%zu", ehdr->e_phoff);
	DBG("e_shoff=%zu", ehdr->e_shoff);
	DBG("e_shentsize=%u", ehdr->e_shentsize);
	DBG("e_shnum=%u", ehdr->e_shnum);

	ElfW(Shdr) *shdr = hndl->mapping + ehdr->e_shoff;
	char *strtab = hndl->mapping + shdr[ehdr->e_shstrndx].sh_offset;
	for(int i = 0; i < ehdr->e_shnum; ++i)
	{
		ElfW(Shdr) *cur_shdr = &shdr[i];
		char *name = &strtab[cur_shdr->sh_name];
		if(!strcmp(name, ".symtab"))
		{
			hndl->symtab = hndl->mapping + cur_shdr->sh_offset;
			hndl->symtab_entries = cur_shdr->sh_size / cur_shdr->sh_entsize;
			DBG("Found symbol table (%u entries): %p", hndl->symtab_entries, hndl->symtab);
		}
		else if(!strcmp(name, ".strtab"))
		{
			hndl->strtab = hndl->mapping + cur_shdr->sh_offset;
			DBG("Found string table: %p", hndl->strtab);
		}
		else if(!strcmp(name, ".dynsym"))
		{
			hndl->dynsym = hndl->mapping + cur_shdr->sh_offset;
			hndl->dynsym_entries = cur_shdr->sh_size / cur_shdr->sh_entsize;
			DBG("Found dynsym (%u entries): %p", hndl->dynsym_entries, hndl->dynsym);
		}
		else if(!strcmp(name, ".dynstr"))
		{
			hndl->dynstr = hndl->mapping + cur_shdr->sh_offset;
			DBG("Found dynstr: %p", hndl->dynstr);
		}
	}
}

bool elfparse_needs_reloc(void *handle)
{
	struct elfparse_info *hndl = (struct elfparse_info *)handle;
	return hndl->ehdr->e_type != ET_EXEC;
}

static char *elfparse_findfunction(char *strtab, ElfW(Sym) *symtab, int symtab_entries, const char *funcname)
{
	for(int i = 0; i < symtab_entries; ++i)
	{
		char *curname = &strtab[symtab[i].st_name];
		if(!strcmp(curname, funcname))
			return (char *)symtab[i].st_value;
	}
	return 0;
}

char *elfparse_getfuncaddr(void *handle, const char *funcname)
{
	struct elfparse_info *hndl = (struct elfparse_info*)handle;
	char *fn = elfparse_findfunction(hndl->strtab, hndl->symtab, hndl->symtab_entries, funcname);
	if(fn)
		goto ret;
	DBG("Function %s not found in symtab, trying dynsym", funcname);
	fn = elfparse_findfunction(hndl->dynstr, hndl->dynsym, hndl->dynsym_entries, funcname);
	if(fn)
		goto ret;
	WARN("Function %s not found in symtab or dynsym", funcname);
	return 0;
ret:
	if(hndl->ehdr->e_machine == EM_ARM) /* apply fix for Thumb functions */
		fn = (char *)((uintptr_t)fn & ~1);
	return fn;
}

void elfparse_destroyhandle(void *handle)
{
	struct elfparse_info *hndl = (struct elfparse_info*)handle;
	free(hndl->path);
	munmap(hndl->mapping, hndl->len);
	close(hndl->fd);
	free(hndl);
}
