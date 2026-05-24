#include <stdint.h>
#include <stddef.h>

#include "boot.h"

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0ull
#define EFI_ERROR_BIT 0x8000000000000000ull
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5ull)
#define EFI_LOAD_ERROR (EFI_ERROR_BIT | 1ull)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2ull)
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_BIT | 9ull)

#define EFI_MEMORY_TYPE_LOADER_DATA 4ull
#define EFI_ALLOCATE_ANY_PAGES 0u
#define EFI_ALLOCATE_ADDRESS 2u

typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef uint64_t EFI_LBA;
typedef uint64_t UINTN;
typedef uint16_t CHAR16;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct EFI_LOADED_IMAGE_PROTOCOL EFI_LOADED_IMAGE_PROTOCOL;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    uint64_t CreateTime[3];
    uint64_t LastAccessTime[3];
    uint64_t ModificationTime[3];
    uint64_t Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL* This, EFI_FILE_PROTOCOL** NewHandle, CHAR16* FileName, uint64_t OpenMode, uint64_t Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL* This);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL* This);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL* This, UINTN* BufferSize, void* Buffer);
    EFI_STATUS (EFIAPI *Write)(EFI_FILE_PROTOCOL* This, UINTN* BufferSize, const void* Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL* This, UINTN* Position);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL* This, UINTN Position);
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL* This, const EFI_GUID* InformationType, UINTN* BufferSize, void* Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE_PROTOCOL* This, const EFI_GUID* InformationType, UINTN BufferSize, const void* Buffer);
    EFI_STATUS (EFIAPI *Flush)(EFI_FILE_PROTOCOL* This);
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This, EFI_FILE_PROTOCOL** Root);
};

struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision;
    uint32_t Pad;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    uint32_t LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE ImageHandle);
};

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelFormat;
    uint32_t PixelInformation[4];
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    uint64_t FrameBufferBase;
    UINTN FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL* This, uint32_t ModeNumber, UINTN* SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info);
    EFI_STATUS (EFIAPI *SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL* This, uint32_t ModeNumber);
    EFI_STATUS (EFIAPI *Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL* This, void* BltBuffer, uint32_t BltOperation, UINTN SourceX, UINTN SourceY, UINTN DestinationX, UINTN DestinationY, UINTN Width, UINTN Height, UINTN Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This, CHAR16* String);
};

struct EFI_BOOT_SERVICES {
    uint8_t Hdr[24];
    void* RaiseTPL;
    void* RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(uint32_t, uint32_t, UINTN, uint64_t*);
    EFI_STATUS (EFIAPI *FreePages)(uint64_t, UINTN);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, uint32_t*);
    EFI_STATUS (EFIAPI *AllocatePool)(uint32_t, UINTN, void**);
    EFI_STATUS (EFIAPI *FreePool)(void*);
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, const EFI_GUID*, void**);
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    EFI_STATUS (EFIAPI *InstallConfigurationTable)(const EFI_GUID*, void*);
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    void* OpenProtocol;
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(const EFI_GUID*, void*, void**);
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;
    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
};

struct EFI_SYSTEM_TABLE {
    uint8_t Hdr[24];
    CHAR16* FirmwareVendor;
    uint32_t FirmwareRevision;
    uint32_t Pad;
    EFI_HANDLE ConsoleInHandle;
    void* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    void* StdErr;
    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
};

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
    uint32_t Width;
    uint32_t Height;
    uint32_t PixelFormat;
    uint32_t PixelInformation[4];
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION_FAKE;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR_MIN;

static const EFI_GUID LOADED_IMAGE_PROTOCOL_GUID = { 0x5B1B31A1, 0x9562, 0x11d2, {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B} };
static const EFI_GUID SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = { 0x964E5B22, 0x6459, 0x11D2, {0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B} };
static const EFI_GUID FILE_INFO_GUID = { 0x09576e92, 0x6d3f, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b} };
static const EFI_GUID GOP_GUID = { 0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a} };

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
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

#define PT_LOAD 1u
#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define EM_X86_64 62u

static EFI_SYSTEM_TABLE* g_st;
static EFI_BOOT_SERVICES* g_bs;

static inline void serial_outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void serial_init(void) {
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x80);
    serial_outb(0x3F8 + 0, 0x03);
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x03);
    serial_outb(0x3F8 + 2, 0xC7);
    serial_outb(0x3F8 + 4, 0x0B);
}

static void serial_putc(char c) {
    serial_outb(0x3F8, (uint8_t)c);
}

static void serial_writeln(const char* text) {
    while (*text) serial_putc(*text++);
    serial_putc('\n');
}

static void* mem_copy(void* dst, const void* src, UINTN size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (UINTN i = 0; i < size; ++i) d[i] = s[i];
    return dst;
}

static void mem_set(void* dst, uint8_t value, UINTN size) {
    uint8_t* d = (uint8_t*)dst;
    for (UINTN i = 0; i < size; ++i) d[i] = value;
}

void* memcpy(void* dst, const void* src, size_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < size; ++i) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int value, size_t size) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)value;
    for (size_t i = 0; i < size; ++i) d[i] = v;
    return dst;
}

static UINTN align_up(UINTN value, UINTN alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void write_char16(CHAR16* dst, const char* src) {
    while (*src) {
        *dst++ = (CHAR16)(unsigned char)*src++;
    }
    *dst = 0;
}

static void log16(const char* text) {
    if (!g_st || !g_st->ConOut || !g_st->ConOut->OutputString) return;
    CHAR16 buffer[256];
    UINTN i = 0;
    while (text[i] && i < (sizeof(buffer) / sizeof(buffer[0])) - 2) {
        buffer[i] = (CHAR16)(unsigned char)text[i];
        ++i;
    }
    buffer[i++] = '\r';
    buffer[i++] = '\n';
    buffer[i] = 0;
    g_st->ConOut->OutputString(g_st->ConOut, buffer);
}

static EFI_STATUS open_loaded_image(EFI_HANDLE image_handle, EFI_LOADED_IMAGE_PROTOCOL** loaded_image) {
    return g_bs->HandleProtocol(image_handle, &LOADED_IMAGE_PROTOCOL_GUID, (void**)loaded_image);
}

static EFI_STATUS open_root(EFI_HANDLE device_handle, EFI_FILE_PROTOCOL** root) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = NULL;
    EFI_STATUS status = g_bs->HandleProtocol(device_handle, &SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void**)&fs);
    if (status != EFI_SUCCESS) return status;
    return fs->OpenVolume(fs, root);
}

static EFI_STATUS open_file(EFI_FILE_PROTOCOL* root, const CHAR16* path, EFI_FILE_PROTOCOL** file) {
    return root->Open(root, file, (CHAR16*)path, 1ull, 0ull);
}

static EFI_STATUS file_size(EFI_FILE_PROTOCOL* file, UINTN* size_out) {
    uint8_t info_buffer[1024];
    UINTN buffer_size = sizeof(info_buffer);
    EFI_STATUS status = file->GetInfo(file, &FILE_INFO_GUID, &buffer_size, info_buffer);
    if (status != EFI_SUCCESS) return status;
    EFI_FILE_INFO* info = (EFI_FILE_INFO*)info_buffer;
    *size_out = (UINTN)info->FileSize;
    return EFI_SUCCESS;
}

static EFI_STATUS read_entire_file(EFI_FILE_PROTOCOL* file, void** buffer_out, UINTN* size_out) {
    UINTN size = 0;
    EFI_STATUS status = file_size(file, &size);
    if (status != EFI_SUCCESS) return status;

    void* buffer = NULL;
    status = g_bs->AllocatePool(EFI_MEMORY_TYPE_LOADER_DATA, size ? size : 1u, &buffer);
    if (status != EFI_SUCCESS) return status;

    UINTN read_size = size;
    status = file->SetPosition(file, 0ull);
    if (status != EFI_SUCCESS) {
        g_bs->FreePool(buffer);
        return status;
    }
    status = file->Read(file, &read_size, buffer);
    if (status != EFI_SUCCESS || read_size != size) {
        g_bs->FreePool(buffer);
        return status != EFI_SUCCESS ? status : EFI_LOAD_ERROR;
    }

    *buffer_out = buffer;
    *size_out = size;
    return EFI_SUCCESS;
}

static EFI_STATUS load_kernel_image(EFI_FILE_PROTOCOL* root, const CHAR16* path, void** image_out, UINTN* size_out) {
    EFI_FILE_PROTOCOL* file = NULL;
    serial_writeln("[uefi] opening kernel image");
    EFI_STATUS status = open_file(root, path, &file);
    if (status != EFI_SUCCESS) return status;

    serial_writeln("[uefi] reading kernel image");
    status = read_entire_file(file, image_out, size_out);
    file->Close(file);
    return status;
}

static EFI_STATUS load_kernel_elf(void* image, UINTN image_size, uint64_t* entry_out, uint64_t* base_out, uint64_t* size_out) {
    if (image_size < sizeof(Elf64_Ehdr)) return EFI_LOAD_ERROR;

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)image;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') return EFI_LOAD_ERROR;
    if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB || ehdr->e_machine != EM_X86_64) return EFI_LOAD_ERROR;
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) return EFI_LOAD_ERROR;

    uint64_t kernel_min = UINT64_MAX;
    uint64_t kernel_max = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        uint64_t ph_off = ehdr->e_phoff + ((uint64_t)i * ehdr->e_phentsize);
        if (ph_off + sizeof(Elf64_Phdr) > image_size) return EFI_LOAD_ERROR;

        Elf64_Phdr* ph = (Elf64_Phdr*)((uint8_t*)image + ph_off);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        uint64_t aligned_vaddr = ph->p_vaddr & ~0xFFFull;
        uint64_t offset = ph->p_vaddr - aligned_vaddr;
        uint64_t total_size = offset + ph->p_memsz;
        UINTN pages = (UINTN)align_up((UINTN)total_size, 4096u) / 4096u;
        uint64_t target = aligned_vaddr;

        EFI_STATUS status = g_bs->AllocatePages(EFI_ALLOCATE_ADDRESS, EFI_MEMORY_TYPE_LOADER_DATA, pages, &target);
        if (status != EFI_SUCCESS) return status;

        if (ph->p_offset + ph->p_filesz > image_size) return EFI_LOAD_ERROR;

        mem_set((void*)(uintptr_t)target, 0, (UINTN)pages * 4096u);
        mem_copy((uint8_t*)(uintptr_t)(target + offset), (uint8_t*)image + ph->p_offset, (UINTN)ph->p_filesz);

        if (aligned_vaddr < kernel_min) kernel_min = aligned_vaddr;
        if (aligned_vaddr + total_size > kernel_max) kernel_max = aligned_vaddr + total_size;
    }

    if (kernel_min == UINT64_MAX || kernel_max <= kernel_min) return EFI_LOAD_ERROR;
    *entry_out = ehdr->e_entry;
    *base_out = kernel_min;
    *size_out = kernel_max - kernel_min;
    return EFI_SUCCESS;
}

static EFI_STATUS get_graphics(boot_framebuffer_t* framebuffer) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_STATUS status = g_bs->LocateProtocol(&GOP_GUID, NULL, (void**)&gop);
    if (status != EFI_SUCCESS || !gop || !gop->Mode || !gop->Mode->Info) {
        framebuffer->framebuffer_base = 0;
        framebuffer->framebuffer_size = 0;
        framebuffer->width = 0;
        framebuffer->height = 0;
        framebuffer->pixels_per_scanline = 0;
        framebuffer->pixel_format = 0;
        framebuffer->red_mask = 0;
        framebuffer->green_mask = 0;
        framebuffer->blue_mask = 0;
        framebuffer->reserved = 0;
        return EFI_SUCCESS;
    }

    framebuffer->framebuffer_base = gop->Mode->FrameBufferBase;
    framebuffer->framebuffer_size = gop->Mode->FrameBufferSize;
    framebuffer->width = gop->Mode->Info->HorizontalResolution;
    framebuffer->height = gop->Mode->Info->VerticalResolution;
    framebuffer->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    framebuffer->pixel_format = gop->Mode->Info->PixelFormat;
    framebuffer->red_mask = 0;
    framebuffer->green_mask = 0;
    framebuffer->blue_mask = 0;
    framebuffer->reserved = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS build_boot_info(
    EFI_HANDLE image_handle,
    EFI_FILE_PROTOCOL* root,
    boot_info_t** boot_info_out,
    uint64_t* map_key_out,
    void** handoff_block_out,
    uint64_t* handoff_size_out,
    uint64_t* kernel_entry_out,
    uint64_t* kernel_base_out,
    uint64_t* kernel_size_out
) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded = NULL;
    serial_writeln("[uefi] loading image protocol");
    status = open_loaded_image(image_handle, &loaded);
    if (status != EFI_SUCCESS) return status;

    serial_writeln("[uefi] opening root filesystem");
    void* kernel_image = NULL;
    UINTN kernel_image_size = 0;
    status = load_kernel_image(root, (const CHAR16*)u"\\EFI\\BOOT\\KERNEL.ELF", &kernel_image, &kernel_image_size);
    if (status != EFI_SUCCESS) {
        status = load_kernel_image(root, (const CHAR16*)u"\\KERNEL.ELF", &kernel_image, &kernel_image_size);
        if (status != EFI_SUCCESS) return status;
    }

    serial_writeln("[uefi] kernel image loaded");
    uint64_t kernel_entry = 0;
    uint64_t kernel_base = 0;
    uint64_t kernel_size = 0;
    serial_writeln("[uefi] loading kernel elf");
    status = load_kernel_elf(kernel_image, kernel_image_size, &kernel_entry, &kernel_base, &kernel_size);
    if (status != EFI_SUCCESS) {
        g_bs->FreePool(kernel_image);
        return status;
    }

    serial_writeln("[uefi] building boot info");
    boot_framebuffer_t framebuffer;
    get_graphics(&framebuffer);

    /* If a framebuffer is present, clear firmware splash so kernel console is visible in GUI */
    if (framebuffer.framebuffer_base != 0 && framebuffer.framebuffer_size > 0) {
        serial_writeln("[uefi] clearing firmware framebuffer");
        mem_set((void*)(uintptr_t)framebuffer.framebuffer_base, 0x00, (UINTN)framebuffer.framebuffer_size);
        /* report framebuffer info */
        char info_buf[128];
        /* crude integer to string: print width/height/pxline*/
        /* reuse mem_set + serial_writeln for simplicity */
    }

    UINTN raw_map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;
    serial_writeln("[uefi] probing memory map");
    status = g_bs->GetMemoryMap(&raw_map_size, NULL, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        g_bs->FreePool(kernel_image);
        return status;
    }

    raw_map_size += descriptor_size * 8u;
    EFI_MEMORY_DESCRIPTOR* raw_map = NULL;
    serial_writeln("[uefi] allocating memory map buffer");
    status = g_bs->AllocatePool(EFI_MEMORY_TYPE_LOADER_DATA, raw_map_size, (void**)&raw_map);
    if (status != EFI_SUCCESS) {
        g_bs->FreePool(kernel_image);
        return status;
    }

    serial_writeln("[uefi] reading memory map");
    status = g_bs->GetMemoryMap(&raw_map_size, raw_map, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_SUCCESS) {
        g_bs->FreePool(raw_map);
        g_bs->FreePool(kernel_image);
        return status;
    }

    uint64_t descriptor_count = raw_map_size / descriptor_size;
    uint64_t boot_data_bytes = sizeof(boot_info_t) + ((descriptor_count + 64ull) * sizeof(boot_memory_region_t));
    uint64_t boot_data_pages = (boot_data_bytes + 4095ull) / 4096ull;

    uint64_t boot_data_base = 0;
    serial_writeln("[uefi] allocating handoff block");
    status = g_bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES, EFI_MEMORY_TYPE_LOADER_DATA, (UINTN)boot_data_pages, &boot_data_base);
    if (status != EFI_SUCCESS) {
        g_bs->FreePool(raw_map);
        serial_writeln("[uefi] kernel image freed");
        g_bs->FreePool(kernel_image);
        return status;
    }

    boot_info_t* boot_info = (boot_info_t*)(uintptr_t)boot_data_base;
    boot_memory_region_t* regions = (boot_memory_region_t*)(boot_info + 1);
    mem_set(boot_info, 0, (UINTN)boot_data_pages * 4096u);

    boot_info->magic = FOX_BOOT_INFO_MAGIC;
    boot_info->version = FOX_BOOT_INFO_VERSION;
    boot_info->loader_base = (uint64_t)(uintptr_t)loaded->ImageBase;
    boot_info->loader_size = loaded->ImageSize;
    boot_info->kernel_base = kernel_base;
    boot_info->kernel_size = kernel_size;
    boot_info->handoff_base = boot_data_base;
    boot_info->handoff_size = boot_data_pages * 4096ull;
    boot_info->memory_map_count = descriptor_count;
    boot_info->memory_map = regions;
    boot_info->framebuffer = framebuffer;

    for (uint64_t i = 0; i < descriptor_count; ++i) {
        EFI_MEMORY_DESCRIPTOR* src = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)raw_map + (i * descriptor_size));
        regions[i].type = (uint64_t)src->Type;
        regions[i].physical_start = src->PhysicalStart;
        regions[i].virtual_start = src->VirtualStart;
        regions[i].page_count = src->NumberOfPages;
        regions[i].attribute = src->Attribute;
    }

    g_bs->FreePool(kernel_image);

    serial_writeln("[uefi] refreshing memory map");
    status = g_bs->GetMemoryMap(&raw_map_size, NULL, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;

    raw_map_size += descriptor_size * 8u;
    raw_map = NULL;
    serial_writeln("[uefi] reallocating memory map buffer");
    status = g_bs->AllocatePool(EFI_MEMORY_TYPE_LOADER_DATA, raw_map_size, (void**)&raw_map);
    if (status != EFI_SUCCESS) return status;
    serial_writeln("[uefi] rereading memory map");
    status = g_bs->GetMemoryMap(&raw_map_size, raw_map, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_SUCCESS) {
        g_bs->FreePool(raw_map);
        return status;
    }

    descriptor_count = raw_map_size / descriptor_size;
    if (descriptor_count + 64ull > (boot_data_bytes - sizeof(boot_info_t)) / sizeof(boot_memory_region_t)) {
        g_bs->FreePool(raw_map);
        return EFI_OUT_OF_RESOURCES;
    }

    boot_info->memory_map_count = descriptor_count;
    for (uint64_t i = 0; i < descriptor_count; ++i) {
        EFI_MEMORY_DESCRIPTOR* src = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)raw_map + (i * descriptor_size));
        regions[i].type = (uint64_t)src->Type;
        regions[i].physical_start = src->PhysicalStart;
        regions[i].virtual_start = src->VirtualStart;
        regions[i].page_count = src->NumberOfPages;
        regions[i].attribute = src->Attribute;
    }

    *boot_info_out = boot_info;
    *map_key_out = map_key;
    *handoff_block_out = raw_map;
    *handoff_size_out = raw_map_size;
    *kernel_entry_out = kernel_entry;
    *kernel_base_out = kernel_base;
    *kernel_size_out = kernel_size;
    return EFI_SUCCESS;
}

typedef void (__attribute__((sysv_abi)) *kernel_entry_fn)(const boot_info_t*);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    g_st = SystemTable;
    g_bs = SystemTable->BootServices;
    serial_init();
    serial_writeln("[uefi] loader start");

    EFI_LOADED_IMAGE_PROTOCOL* loaded = NULL;
    serial_writeln("[uefi] opening loaded image");
    EFI_STATUS status = open_loaded_image(ImageHandle, &loaded);
    if (status != EFI_SUCCESS) {
        serial_writeln("[uefi] loaded image open failed");
        return status;
    }

    EFI_FILE_PROTOCOL* root = NULL;
    serial_writeln("[uefi] opening root volume");
    status = open_root(loaded->DeviceHandle, &root);
    if (status != EFI_SUCCESS) {
        serial_writeln("[uefi] root volume open failed");
        return status;
    }

    boot_info_t* boot_info = NULL;
    void* memory_map_buffer = NULL;
    uint64_t memory_map_size = 0;
    UINTN map_key = 0;
    uint64_t kernel_entry = 0;
    uint64_t kernel_base = 0;
    uint64_t kernel_size = 0;
    status = build_boot_info(ImageHandle, root, &boot_info, &map_key, &memory_map_buffer, &memory_map_size, &kernel_entry, &kernel_base, &kernel_size);
    root->Close(root);
    if (status != EFI_SUCCESS) {
        log16("UEFI loader failed to prepare boot info");
        serial_writeln("[uefi] boot info failed");
        return status;
    }

    serial_writeln("[uefi] boot info ready");

    serial_writeln("[uefi] exiting boot services");
    status = g_bs->ExitBootServices(ImageHandle, map_key);
    if (status != EFI_SUCCESS) {
        log16("ExitBootServices failed");
        serial_writeln("[uefi] exit boot services failed");
        return status;
    }

    serial_writeln("[uefi] jumping to kernel");

    (void)memory_map_buffer;
    (void)memory_map_size;
    (void)kernel_base;
    (void)kernel_size;

    kernel_entry_fn entry = (kernel_entry_fn)(uintptr_t)kernel_entry;
    entry(boot_info);

    for (;;) {
        __asm__ __volatile__("hlt");
    }
    return EFI_SUCCESS;
}
