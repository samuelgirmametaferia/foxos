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
    
    /* Load PT_LOAD segments into the canonical user address space (USER_BASE). */
    const uint64_t USER_BASE = 0x8000000000ULL;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint64_t vaddr = phdrs[i].p_vaddr | USER_BASE;
            uint64_t memsz = phdrs[i].p_memsz;
            uint64_t filesz = phdrs[i].p_filesz;
            uint64_t offset = phdrs[i].p_offset;
            
            serial_write("[loader] segment: vaddr="); serial_u64(vaddr);
            serial_write(" memsz="); serial_u64(memsz);
            serial_writeln("");

            uint64_t vaddr_start = vaddr & ~0xFFFULL;
            uint64_t vaddr_end = (vaddr + memsz + 0xFFFULL) & ~0xFFFULL;
            
            for (uint64_t va = vaddr_start; va < vaddr_end; va += PAGE_SIZE) {
                paddr_t frame = pmm_alloc_frame();
                memzero((void*)(uintptr_t)frame, PAGE_SIZE);
                vmm_map(pml4, va, frame, 0x07);
                
                uint64_t page_offset = 0;
                uint64_t file_offset = 0;
                uint64_t read_len = 0;
                
                if (va < vaddr) {
                    page_offset = vaddr - va;
                    file_offset = offset;
                    read_len = (filesz > (PAGE_SIZE - page_offset)) ? (PAGE_SIZE - page_offset) : filesz;
                } else {
                    page_offset = 0;
                    file_offset = offset + (va - vaddr);
                    if (va - vaddr < filesz) {
                        read_len = (filesz - (va - vaddr) > PAGE_SIZE) ? PAGE_SIZE : filesz - (va - vaddr);
                    }
                }
                
                if (read_len > 0) {
                    sys_lseek(fd, file_offset, 0);
                    sys_read(fd, (void*)((uintptr_t)frame + page_offset), read_len);
                }
            }
        }
    }
    
    serial_write("[loader] jumping to entry: "); serial_u64(ehdr.e_entry | USER_BASE); serial_writeln("");
    kfree(phdrs);
    sys_close(fd);
    
    /* Spawn the ELF at the canonical user entry (ORed with USER_BASE). */
    process_spawn_elf(pml4, ehdr.e_entry | USER_BASE);
    
    return 0;
}
