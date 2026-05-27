#include "loader.h"
#include "../fs/vfs.h"
#include "memory.h"
#include "process.h"
#include "serial.h"

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define PT_LOAD 1

int sys_exec(const char* path, char* const argv[], char* const envp[]) {
    (void)argv; (void)envp;
    
    int fd = sys_open(path, 0, 0);
    if (fd < 0) return -1;
    
    Elf64_Ehdr ehdr;
    if (sys_read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        sys_close(fd);
        return -1;
    }
    
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || 
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        sys_close(fd);
        return -1;
    }
    
    uint64_t pml4 = process_create_pml4();
    
    Elf64_Phdr* phdrs = (Elf64_Phdr*)kmalloc(ehdr.e_phnum * sizeof(Elf64_Phdr));
    sys_lseek(fd, ehdr.e_phoff, 0);
    sys_read(fd, phdrs, ehdr.e_phnum * sizeof(Elf64_Phdr));
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint64_t vaddr = phdrs[i].p_vaddr;
            uint64_t memsz = phdrs[i].p_memsz;
            uint64_t filesz = phdrs[i].p_filesz;
            uint64_t offset = phdrs[i].p_offset;
            
            for (uint64_t off = 0; off < memsz; off += PAGE_SIZE) {
                paddr_t frame = pmm_alloc_frame();
                memzero((void*)(uintptr_t)frame, PAGE_SIZE);
                vmm_map(pml4, vaddr + off, frame, 0x07);
                
                if (off < filesz) {
                    uint64_t to_read = (filesz - off > PAGE_SIZE) ? PAGE_SIZE : filesz - off;
                    sys_lseek(fd, offset + off, 0);
                    sys_read(fd, (void*)(uintptr_t)frame, to_read);
                }
            }
        }
    }
    
    kfree(phdrs);
    sys_close(fd);
    
    process_spawn_elf(pml4, ehdr.e_entry);
    
    return 0;
}
