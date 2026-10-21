/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ENCORE_ELF_OFFSET_H
#define ENCORE_ELF_OFFSET_H
#include <elf.h>
#include <sys/stat.h>

static int elf_span(size_t size, uint64_t offset, uint64_t length) {
 return offset <= size && length <= size - offset;
}

/* Uprobes use file offsets, which need not equal ELF virtual addresses. */
static int queuebuffer_offset(const char *filename, uint64_t *result) {
 int fd = open(filename, O_RDONLY | O_CLOEXEC);
 struct stat st;
 if (fd < 0) return -1;
 if (fstat(fd, &st) || st.st_size < (off_t)sizeof(Elf64_Ehdr)
     || st.st_size > 128 * 1024 * 1024) { close(fd); return -1; }
 size_t size = (size_t)st.st_size;
 const unsigned char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
 close(fd);
 if (data == MAP_FAILED) return -1;
 Elf64_Ehdr eh;
 memcpy(&eh, data, sizeof(eh));
 int found = 0, bad = 0;
 uint64_t resolved = 0;
 if (memcmp(eh.e_ident, ELFMAG, SELFMAG)
     || eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_ident[EI_DATA] != ELFDATA2LSB
     || eh.e_machine != EM_AARCH64 || eh.e_type != ET_DYN
     || eh.e_phentsize != sizeof(Elf64_Phdr) || !eh.e_phnum
     || eh.e_shentsize != sizeof(Elf64_Shdr) || !eh.e_shnum
     || !elf_span(size, eh.e_phoff, (uint64_t)eh.e_phnum * sizeof(Elf64_Phdr))
     || !elf_span(size, eh.e_shoff, (uint64_t)eh.e_shnum * sizeof(Elf64_Shdr))) goto done;
 for (unsigned i = 0; i < eh.e_shnum && !bad; ++i) {
  Elf64_Shdr symbols, strings;
  memcpy(&symbols, data + eh.e_shoff + i * sizeof(symbols), sizeof(symbols));
  if (symbols.sh_type != SHT_DYNSYM && symbols.sh_type != SHT_SYMTAB) continue;
  if (symbols.sh_link >= eh.e_shnum || symbols.sh_entsize != sizeof(Elf64_Sym)
      || symbols.sh_size % sizeof(Elf64_Sym)
      || !elf_span(size, symbols.sh_offset, symbols.sh_size)) { bad = 1; break; }
  memcpy(&strings, data + eh.e_shoff + symbols.sh_link * sizeof(strings), sizeof(strings));
  if (strings.sh_type != SHT_STRTAB || !elf_span(size, strings.sh_offset, strings.sh_size)) { bad = 1; break; }
  for (uint64_t pos = 0; pos < symbols.sh_size; pos += sizeof(Elf64_Sym)) {
   Elf64_Sym symbol;
   memcpy(&symbol, data + symbols.sh_offset + pos, sizeof(symbol));
   if (symbol.st_name >= strings.sh_size || symbol.st_shndx == SHN_UNDEF
       || ELF64_ST_TYPE(symbol.st_info) != STT_FUNC || symbol.st_size < 4
       || !symbol.st_value || (symbol.st_value & 3)) continue;
   const char *name = (const char *)data + strings.sh_offset + symbol.st_name;
   size_t remaining = strings.sh_size - symbol.st_name;
   const char *prefix = "_ZN7android7Surface16hook_queueBuffer";
   if (!memchr(name, 0, remaining) || strncmp(name, prefix, strlen(prefix))) continue;
   if (strstr(name, "_DEPRECATE")) continue;
   int mapped = 0;
   for (unsigned j = 0; j < eh.e_phnum; ++j) {
    Elf64_Phdr ph;
    memcpy(&ph, data + eh.e_phoff + j * sizeof(ph), sizeof(ph));
    if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X) || symbol.st_value < ph.p_vaddr) continue;
    uint64_t delta = symbol.st_value - ph.p_vaddr;
    if (delta > ph.p_filesz || 4 > ph.p_filesz - delta
        || !elf_span(size, ph.p_offset, ph.p_filesz)) continue;
    uint64_t offset = ph.p_offset + delta;
    if (offset & 3) continue;
    if (found && resolved != offset) bad = 1;
    resolved = offset; found = mapped = 1;
   }
   if (!mapped) bad = 1;
  }
 }
done:
 munmap((void *)data, size);
 if (!found || bad) return -1;
 *result = resolved;
 return 0;
}
#endif
