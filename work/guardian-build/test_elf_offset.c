/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "elf-offset.h"

static unsigned char fixture[2048];
static void reset(void) {
 memset(fixture, 0, sizeof(fixture));
 Elf64_Ehdr eh = {0};
 memcpy(eh.e_ident, ELFMAG, SELFMAG);
 eh.e_ident[EI_CLASS] = ELFCLASS64; eh.e_ident[EI_DATA] = ELFDATA2LSB;
 eh.e_type = ET_DYN; eh.e_machine = EM_AARCH64;
 eh.e_phoff = 64; eh.e_phentsize = sizeof(Elf64_Phdr); eh.e_phnum = 1;
 eh.e_shoff = 128; eh.e_shentsize = sizeof(Elf64_Shdr); eh.e_shnum = 3;
 memcpy(fixture, &eh, sizeof(eh));
 Elf64_Phdr ph = {.p_type=PT_LOAD, .p_flags=PF_R|PF_X,
  .p_offset=1024, .p_vaddr=0x10000, .p_filesz=512, .p_memsz=1024};
 memcpy(fixture + 64, &ph, sizeof(ph));
 Elf64_Shdr symbols = {.sh_type=SHT_DYNSYM, .sh_offset=512,
  .sh_size=2*sizeof(Elf64_Sym), .sh_link=2, .sh_entsize=sizeof(Elf64_Sym)};
 memcpy(fixture + 128 + sizeof(Elf64_Shdr), &symbols, sizeof(symbols));
 const char names[] = "\0_ZN7android7Surface16hook_queueBufferEP13ANativeWindowP19ANativeWindowBufferi";
 Elf64_Shdr strings = {.sh_type=SHT_STRTAB, .sh_offset=768, .sh_size=sizeof(names)};
 memcpy(fixture + 128 + 2*sizeof(Elf64_Shdr), &strings, sizeof(strings));
 memcpy(fixture + 768, names, sizeof(names));
 Elf64_Sym symbol = {.st_name=1, .st_info=ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
  .st_shndx=1, .st_value=0x10040, .st_size=16};
 memcpy(fixture + 512 + sizeof(Elf64_Sym), &symbol, sizeof(symbol));
}
static int check(const char *name, int accepted, size_t size) {
 char path[] = "/data/local/tmp/encore-elf-test-XXXXXX";
 int fd = mkstemp(path);
 if (fd < 0) return 1;
 int written = write(fd, fixture, size) == (ssize_t)size;
 close(fd);
 uint64_t offset = 0;
 int result = queuebuffer_offset(path, &offset);
 unlink(path);
 int ok = written && (accepted ? result == 0 && offset == 1088 : result != 0);
 printf("%s %s\n", ok ? "PASS" : "FAIL", name);
 return !ok;
}
int main(void) {
 int errors=0;
 reset(); errors+=check("virtual address translated to file offset",1,sizeof(fixture));
 reset(); ((Elf64_Shdr *)(fixture+128))[1].sh_type=SHT_SYMTAB;
 errors+=check("symbol table fallback",1,sizeof(fixture));
 reset(); ((Elf64_Ehdr *)fixture)->e_machine=EM_X86_64;
 errors+=check("wrong architecture",0,sizeof(fixture));
 reset(); ((Elf64_Phdr *)(fixture+64))->p_flags=PF_R;
 errors+=check("non executable target",0,sizeof(fixture));
 reset(); ((Elf64_Sym *)(fixture+512))[1].st_shndx=SHN_UNDEF;
 errors+=check("undefined symbol",0,sizeof(fixture));
 reset(); ((Elf64_Sym *)(fixture+512))[1].st_value=0x10041;
 errors+=check("unaligned target",0,sizeof(fixture));
 reset(); ((Elf64_Sym *)(fixture+512))[1].st_value=0x10200;
 errors+=check("memory only target",0,sizeof(fixture));
 reset(); ((Elf64_Shdr *)(fixture+128))[1].sh_offset=UINT64_MAX;
 errors+=check("invalid section bounds",0,sizeof(fixture));
 reset(); memset(fixture+768,'A',80);
 errors+=check("unterminated symbol",0,sizeof(fixture));
 reset(); ((Elf64_Shdr *)(fixture+128))[1].sh_size=3*sizeof(Elf64_Sym);
 ((Elf64_Sym *)(fixture+512))[2]=((Elf64_Sym *)(fixture+512))[1];
 ((Elf64_Sym *)(fixture+512))[2].st_value+=4;
 errors+=check("ambiguous entry",0,sizeof(fixture));
 reset(); errors+=check("truncated header",0,20);
 printf("ELF_OFFSET_TESTS %s failures=%d\n",errors?"FAIL":"PASS",errors);
 return errors?1:0;
}
