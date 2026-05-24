#include <stdint.h>
#include "boot.h"
#include "keyboard.h"
#include "io.h"
#include "console.h"
#include "memory.h"
#include "vfs.h"
#include "initrd.h"
#include "ramfs.h"
#include "ata.h"
#include "serial.h"
#include "idt.h"
#include "timer.h"
#include "tests.h"

static inline char to_lower(char c){ return (c>='A'&&c<='Z')? (char)(c+32): c; }
static int streq(const char* a, const char* b){ while(*a && *b){ if(*a!=*b) return 0; ++a; ++b; } return *a==0 && *b==0; }
static int startswith(const char* s,const char* p){ while(*p){ if(*s++!=*p++) return 0; } return 1; }

static void list_cb(const char* name, int isDir){ console_write(isDir?"[D] ":"[F] "); console_writeln(name); }

static int vfs_rm_recursive(const char* path);
static void do_reboot_realmode(void);

static void path_resolve(char* out, const char* cwd, const char* in){
    if (in && *in=='/') {
        int i=0; while(in[i]){ out[i]=in[i]; i++; } out[i]=0;
    } else {
        int k=0; while(cwd[k]){ out[k]=cwd[k]; k++; }
        if (k==0) out[k++]='/';
        if (in && *in){ if (out[k-1]!='/') out[k++]='/'; int i=0; while(in[i]) out[k++]=in[i++]; }
        out[k]=0;
    }
    char tmp[128]; int ti=0; int i=0;
    if (out[0]!='/'){ tmp[ti++]='/'; }
    int depth=0; int segStart=1;
    tmp[0]='/'; ti=1; depth=0; segStart=1;
    while(out[i]){
        int j=i; while(out[j] && out[j]!='/') j++;
        int segLen = j-i;
        if (segLen==0) { i = j+1; continue; }
        if (segLen==1 && out[i]=='.') {
        } else if (segLen==2 && out[i]=='.' && out[i+1]=='.') {
            if (ti>1){
                if (tmp[ti-1]=='/' && ti>1) ti--;
                while (ti>1 && tmp[ti-1]!='/') ti--;
            }
        } else {
            if (tmp[ti-1] != '/') tmp[ti++]='/';
            for (int k=0;k<segLen && ti<127;k++) tmp[ti++]=out[i+k];
            tmp[ti]=0;
        }
        i = j; if (out[i] == '/') i++;
    }
    if (ti>1 && tmp[ti-1]=='/') ti--; tmp[ti]=0;
    if (ti==0){ tmp[ti++]='/'; tmp[ti]=0; }
    for (int k=0; tmp[k]; ++k) out[k]=tmp[k]; out[ti]=0;
}

static void u64_to_dec(uint64_t v, char* buf){ int n=0; if(v==0){ buf[n++]='0'; buf[n]=0; return; } char tmp[32]; int t=0; while(v){ tmp[t++] = (char)('0'+(v%10)); v/=10; } while(t--) buf[n++]=tmp[t]; buf[n]=0; }
static void u32_to_dec(uint32_t v, char* buf){ u64_to_dec((uint64_t)v, buf); }

static void input_set_line(char* line, int* plen, const char* src){
    while(*plen > 0){ console_putc('\b'); (*plen)--; }
    int i=0; if (src){ while(src[i] && i < 255){ line[i]=src[i]; i++; } }
    line[i]=0; *plen=i; console_write(line);
}

#define HISTORY_MAX 16
static char history_buf[HISTORY_MAX][256];
static int history_head = 0;
static int history_count = 0;
static int history_browse = -1;
static char edit_saved[256];
static int edit_saved_valid = 0;

static const char* history_get_offset(int offset){
    if (offset < 0 || offset >= history_count) return 0;
    int idx = history_head - 1 - offset; if (idx < 0) idx += HISTORY_MAX;
    return history_buf[idx];
}

static int str_eq(const char* a, const char* b){ int i=0; while(a && b && a[i] && b[i]){ if(a[i]!=b[i]) return 0; i++; } return a && b && a[i]==0 && b[i]==0; }
static void str_copy(char* dst, const char* src, int cap){ int i=0; if(cap<=0) return; while(src && src[i] && i<cap-1){ dst[i]=src[i]; i++; } dst[i]=0; }

static int parse_u64_dec(const char* s, uint64_t* out){ if(!s||!*s) return -1; uint64_t v=0; for(int i=0; s[i]; ++i){ char c=s[i]; if(c<'0'||c>'9') return -2; uint64_t d=(uint64_t)(c-'0'); uint64_t nv = v*10u + d; if (nv < v) return -3; v = nv; } *out=v; return 0; }
static int parse_u32_dec(const char* s, uint32_t* out){ uint64_t v=0; int r=parse_u64_dec(s,&v); if(r!=0||v>0xFFFFFFFFu) return r ? r : -3; *out=(uint32_t)v; return 0; }
static int parse_hex8(const char* s, uint8_t* out){ if(!s||!*s) return -1; uint32_t v=0; int i=0; for(; s[i] && i<2; ++i){ char c=s[i]; if(c>='0'&&c<='9') v = (v<<4) | (uint32_t)(c-'0'); else { char lc = (c>='A'&&c<='Z')?(c+32):c; if(lc>='a'&&lc<='f') v = (v<<4) | (uint32_t)(10 + lc-'a'); else return -2; } } if(s[i]) return -3; *out=(uint8_t)v; return 0; }

static inline void io_wait_short(void){ for(volatile int i=0;i<10000;++i) __asm__ __volatile__("nop"); }

static void reboot_machine(void){
    __asm__ __volatile__("cli");
    serial_writeln("[sys] reboot: cpu reset");
    console_writeln("rebooting...");
    outb(0x64, 0xFE);
    io_wait_short();
    outb(0xCF9, 0x06);
    io_wait_short();
    for(;;){ __asm__ __volatile__("hlt"); }
}

static void poweroff_machine(void){
    __asm__ __volatile__("cli");
    outw(0xB004, 0x2000);
    io_wait_short();
    outw(0x604, 0x2000);
    io_wait_short();
    outw(0x4004, 0x3400);
    io_wait_short();
    outb(0xF4, 0x00);
    for(;;){ __asm__ __volatile__("hlt"); }
}

#ifdef DISK_MODE_HDD
static void mbr_zero(uint8_t* m){ for(int i=0;i<512;++i) m[i]=0; m[510]=0x55; m[511]=0xAA; }
static void mbr_set_entry(uint8_t* m, int idx, uint8_t boot, uint8_t type, uint32_t lba_start, uint32_t lba_count){ int o=446+idx*16; m[o+0]=boot; m[o+1]=0; m[o+2]=0; m[o+3]=0; m[o+4]=type; m[o+5]=0xFF; m[o+6]=0xFF; m[o+7]=0xFF; m[o+8]=(uint8_t)(lba_start&0xFF); m[o+9]=(uint8_t)((lba_start>>8)&0xFF); m[o+10]=(uint8_t)((lba_start>>16)&0xFF); m[o+11]=(uint8_t)((lba_start>>24)&0xFF); m[o+12]=(uint8_t)(lba_count&0xFF); m[o+13]=(uint8_t)((lba_count>>8)&0xFF); m[o+14]=(uint8_t)((lba_count>>16)&0xFF); m[o+15]=(uint8_t)((lba_count>>24)&0xFF); }
static int mbr_read(uint8_t* m){ return ata_available() ? ata_pio_read28(0,m) : -1; }
static int mbr_write(const uint8_t* m){ return ata_available() ? ata_pio_write28(0,m) : -1; }
static void print_part(int idx, uint8_t boot, uint8_t type, uint64_t s, uint64_t c){ console_write("#"); char nb[4]; u32_to_dec((uint32_t)idx, nb); console_write(nb); console_write(" "); console_write(boot?"* ":"  "); console_write("type=0x"); char hx[3]; const char* hexd="0123456789ABCDEF"; hx[0]=hexd[(type>>4)&0xF]; hx[1]=hexd[type&0xF]; hx[2]=0; console_write(hx); console_write(" start="); char b1[32]; u64_to_dec(s,b1); console_write(b1); console_write(" count="); char b2[32]; u64_to_dec(c,b2); console_writeln(b2); }
#endif

static int g_ls_found = 0;
static char g_ls_first_name[128];
static void rm_first_child_cb(const char* name, int isDir){
    if (g_ls_found) return;
    int i=0; while(name[i] && i < (int)sizeof(g_ls_first_name)-1){ g_ls_first_name[i] = name[i]; i++; }
    g_ls_first_name[i] = 0; g_ls_found = 1;
}

static int vfs_rm_recursive(const char* path){
    if (path[0]=='/' && path[1]==0) return -1;
    vfs_stat_t st; if (vfs_stat(path, &st) != 0) return -1;
    if (!st.isDir) return vfs_rm(path);
    for(;;){
        g_ls_found = 0; vfs_ls(path, rm_first_child_cb);
        if (!g_ls_found) break;
        char child[256]; int l=0; while(path[l]){ child[l]=path[l]; l++; }
        if (!(l>0 && child[l-1]=='/')) child[l++]='/';
        int i=0; while(g_ls_first_name[i] && l < (int)sizeof(child)-1){ child[l++] = g_ls_first_name[i++]; }
        child[l]=0;
        int r = vfs_rm_recursive(child); if (r != 0) return r;
    }
    return vfs_rm(path);
}

void kernel_main(const boot_info_t* boot) {
    serial_init();
    serial_writeln("[foxos] serial online");

    /* print boot pointer value for debugging handoff */
    if (boot) {
        char bp[32]; u64_to_dec((uint64_t)(uintptr_t)boot, bp);
        serial_write("[foxos] boot ptr: "); serial_writeln(bp);
    }

    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC) {
        serial_writeln("[foxos] uefi handoff received");
    } else {
        serial_writeln("[foxos] legacy handoff path");
    }

    idt_init();
    serial_writeln("[foxos] idt ready");

    timer_init(100);
    serial_writeln("[foxos] timer online");

    /* Draw a visible framebuffer test pattern before initializing console (helps debug GUI black screen) */
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC && boot->framebuffer.framebuffer_base && boot->framebuffer.framebuffer_size) {
        volatile uint32_t* fb = (volatile uint32_t*)(uintptr_t)boot->framebuffer.framebuffer_base;
        uint32_t stride = boot->framebuffer.pixels_per_scanline ? boot->framebuffer.pixels_per_scanline : boot->framebuffer.width;
        uint32_t w = boot->framebuffer.width ? boot->framebuffer.width : 320;
        uint32_t h = boot->framebuffer.height ? boot->framebuffer.height : 200;
        /* draw a simple checker of white and black  */
        for (uint32_t y = 0; y < (h < 100 ? h : 100); ++y) {
            for (uint32_t x = 0; x < (w < 200 ? w : 200); ++x) {
                uint32_t color = ((x / 8 + y / 8) & 1) ? 0x00FFFFFFu : 0x00000000u;
                fb[y * stride + x] = color;
            }
        }
    }

    console_init(boot);
    console_writeln("foxos console ready");
    serial_writeln("[foxos] console ready");

    mem_init(boot);
    serial_writeln("[foxos] memory init done");

    /* Set up identity paging now that PMM is initialized and reserved regions are known */
    setup_identity_paging();

    /* Run boot self-tests to validate PMM and paging (alloc stress is manual via shell) */
    run_boot_self_tests();

    /* Auto-print arch and PMM info for headless testing */
    {
        char pb[8]; char capb[32];
        u32_to_dec((uint32_t)(sizeof(void*) * 8u), pb);
        u64_to_dec(pmm_total_pages(), capb);
        char freeb[32]; u64_to_dec(pmm_free_pages(), freeb);
        serial_writeln("[auto] arch info:");
        serial_writeln("[auto] x86_64 native");
        serial_write("[auto] pointer bits: "); serial_writeln(pb);
        serial_write("[auto] pointer bytes: "); serial_writeln("8");
        serial_write("[auto] ram pages total: "); serial_writeln(capb);
        serial_write("[auto] ram pages free: "); serial_writeln(freeb);

        if (boot) {
            char fbbuf[64];
            /* print framebuffer info */
            serial_writeln("[auto] framebuffer:");
            u64_to_dec(boot->framebuffer.framebuffer_base, fbbuf); serial_write("[auto] base: "); serial_writeln(fbbuf);
            u64_to_dec(boot->framebuffer.framebuffer_size, fbbuf); serial_write("[auto] size: "); serial_writeln(fbbuf);
            u32_to_dec(boot->framebuffer.width, fbbuf); serial_write("[auto] width: "); serial_writeln(fbbuf);
            u32_to_dec(boot->framebuffer.height, fbbuf); serial_write("[auto] height: "); serial_writeln(fbbuf);
            u32_to_dec(boot->framebuffer.pixels_per_scanline, fbbuf); serial_write("[auto] stride: "); serial_writeln(fbbuf);
            u32_to_dec(boot->framebuffer.pixel_format, fbbuf); serial_write("[auto] pixel_format: "); serial_writeln(fbbuf);
        }
    }

    vfs_init();
    vfs_mount_ramfs();
    initrd_load_into_ramfs();
    console_writeln("vfs: ramfs mounted, initrd loaded");
    serial_writeln("[foxos] vfs/initrd ready");

#ifdef DISK_MODE_HDD
    ata_init();
    serial_writeln("[foxos] ata init done");
#endif

    keyboard_init();
    serial_writeln("[foxos] keyboard ready");

    char cwd[128]; cwd[0] = '/'; cwd[1] = 0;
    char line[256]; int len = 0;
    history_head = 0; history_count = 0; history_browse = -1; edit_saved_valid = 0; edit_saved[0]=0;

    console_write("foxos> ");

    serial_writeln("foxos> ");
    idt_enable_interrupts();

    for (;;) {
        int ch = keyboard_getchar();
        if (ch == -1) { __asm__ __volatile__("hlt"); continue; }

        if (ch == KBD_KEY_UP) {
            if (history_count == 0) continue;
            if (history_browse == -1){ str_copy(edit_saved, line, sizeof(edit_saved)); edit_saved_valid = 1; history_browse = 0; }
            else if (history_browse < history_count - 1) history_browse++;
            const char* src = history_get_offset(history_browse);
            if (src) input_set_line(line, &len, src);
            continue;
        } else if (ch == KBD_KEY_DOWN) {
            if (history_browse == -1) continue;
            if (history_browse == 0){ history_browse = -1; if (edit_saved_valid) input_set_line(line, &len, edit_saved); else input_set_line(line, &len, ""); }
            else { history_browse--; const char* src = history_get_offset(history_browse); if (src) input_set_line(line, &len, src); }
            continue;
        }

        if (ch == '\n') {
            console_putc('\n'); line[len] = '\0';
            serial_write("\n[cmd] "); serial_writeln(line);
            if (len > 0) {
                int last_idx = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
                if (!(history_count > 0 && str_eq(history_buf[last_idx], line))) {
                    str_copy(history_buf[history_head], line, sizeof(history_buf[history_head]));
                    history_head = (history_head + 1) % HISTORY_MAX;
                    if (history_count < HISTORY_MAX) history_count++;
                }
            }
            history_browse = -1; edit_saved_valid = 0; edit_saved[0]=0;
            int i=0; while(line[i] && line[i]!=' '){ line[i]=to_lower(line[i]); i++; }

            if (streq(line, "clear")) {
                console_clear();
            } else if (streq(line, "help")) {
                console_writeln("commands:");
                console_writeln("  help                 - show this help");
                console_writeln("  arch                 - prove 64-bit native mode");
                console_writeln("  uptime               - show system uptime");
                console_writeln("  sleep <ms>           - wait for N milliseconds");
                console_writeln("  ls [path]            - list directory");
                console_writeln("  pwd                  - print working dir");
                console_writeln("  cd <dir>             - change directory");
                console_writeln("  cat <path>           - print file");
                console_writeln("  echo TEXT > PATH     - write file");
                console_writeln("  mkdir <dir>          - create directory");
                console_writeln("  rm <path>            - remove file");
                console_writeln("  stat <path>          - show file/dir info");
                console_writeln("  runtests             - run boot self-tests + alloc stress");
                console_writeln("  selftest             - run boot self-tests only");
                console_writeln("  allocstress          - run allocation stress test only");
                console_writeln("  defrag               - attempt to defragment UC allocations to contiguous backing");
                console_writeln("  pmm                  - show PMM stats and UC handles");
            } else if (streq(line, "arch")) {
                char pb[8], capb[16];
                u32_to_dec((uint32_t)(sizeof(void*) * 8u), pb);
                u64_to_dec(pmm_total_pages(), capb);
                console_write("arch: x86_64 native\n");
                console_write("pointer bits: "); console_writeln(pb);
                console_write("pointer bytes: "); console_writeln("8");
                console_write("ram pages: "); console_writeln(capb);
                console_writeln("smp: not enabled yet");
                serial_writeln("[arch] x86_64 native");
                serial_write("[arch] pointer bits: "); serial_writeln(pb);
                serial_writeln("[arch] pointer bytes: 8");
                serial_write("[arch] ram pages: "); serial_writeln(capb);
                serial_writeln("[arch] smp: not enabled yet");
            } else if (streq(line, "uptime")) {
                uint64_t t = timer_get_ticks();
                char tb[32], sb[32], mb[32];
                u64_to_dec(t, tb); u64_to_dec(t/100, sb); u64_to_dec((t%100)*10, mb);
                console_write("ticks: "); console_write(tb);
                console_write(" uptime: "); console_write(sb); console_write(".");
                if (t%100 < 10) console_write("0");
                console_write(mb); console_writeln("s");
                serial_write("ticks: "); serial_writeln(tb);
            } else if (startswith(line, "sleep ")) {
                uint64_t ms = 0; if (parse_u64_dec(line+6, &ms)==0){ timer_sleep(ms); console_writeln("woke up"); serial_writeln("woke up"); }
            } else if (streq(line, "ls")) {
                vfs_ls(cwd, list_cb);
            } else if (startswith(line, "ls ")) {
                char path[128]; path_resolve(path, cwd, line+3); vfs_ls(path, list_cb);
            } else if (streq(line, "pwd")) {
                console_writeln(cwd);
            } else if (startswith(line, "cd ")) {
                char path[128]; path_resolve(path, cwd, line+3); vfs_stat_t st; if (vfs_stat(path,&st)==0 && st.isDir){ str_copy(cwd, path, sizeof(cwd)); console_writeln("ok"); } else { console_writeln("cd: no such dir"); }
            } else if (startswith(line, "cat ")) {
                char path[128]; path_resolve(path, cwd, line+4); char buf[256]; uint64_t out=0; if (vfs_read(path, buf, sizeof(buf)-1, &out)==0){ uint64_t idx = (out < sizeof(buf)) ? out : (uint64_t)(sizeof(buf)-1); buf[idx]=0; console_writeln(buf);} else { console_writeln("cat: not found"); }
            } else if (startswith(line, "echo ")) {
                char* p = line+5; char* gt = p; while(*gt && *gt!='>') gt++;
                if (*gt=='>') {
                    *gt = 0; char path_in[128]; char* path = gt+1; while(*path==' ') path++;
                    path_resolve(path_in, cwd, path); uint64_t l=0; while(p[l]) l++; if (vfs_write(path_in,p,l)==0) console_writeln("ok"); else console_writeln("write failed");
                }
            } else if (startswith(line, "mkdir ")) {
                char path[128]; path_resolve(path, cwd, line+6); if (vfs_mkdir(path)==0) console_writeln("ok"); else console_writeln("mkdir failed");
            } else if (startswith(line, "rm ")) {
                char path[128]; path_resolve(path, cwd, line+3); if (vfs_rm(path)==0) console_writeln("ok"); else console_writeln("rm failed");
            } else if (startswith(line, "stat ")) {
                char path[128]; path_resolve(path, cwd, line+5); vfs_stat_t st; if (vfs_stat(path,&st)==0){
                    console_write("type: "); console_writeln(st.isDir?"dir":"file");
                    if(!st.isDir){ console_write("size: "); char buf[32]; u64_to_dec(st.size, buf); console_writeln(buf);} else { console_write("children: "); char buf[32]; u64_to_dec(st.children, buf); console_writeln(buf);} }
                else { console_writeln("stat: not found"); }
            console_writeln("  runtests             - run boot self-tests + alloc stress");
            console_writeln("  selftest             - run boot self-tests only");
            console_writeln("  allocstress          - run allocation stress test only");
            } else if (streq(line, "runtests")) {
                serial_writeln("[cmd] runtests");
                run_boot_self_tests(); run_alloc_stress(); console_writeln("runtests done");
            } else if (streq(line, "selftest")) {
                serial_writeln("[cmd] selftest");
                run_boot_self_tests(); console_writeln("selftest done");
            } else if (streq(line, "allocstress")) {
                serial_writeln("[cmd] allocstress");
                run_alloc_stress(); console_writeln("allocstress done");
            } else if (streq(line, "defrag")) {
                serial_writeln("[cmd] defrag");
                int moved = uc_defrag_all(); char mb[32]; u64_to_dec((uint64_t)moved, mb);
                console_write("defrag moved: "); console_writeln(mb);
            } else if (streq(line, "pmm")) {
                serial_writeln("[cmd] pmm");
                pmm_dump_stats();
            } else if (streq(line, "reboot")) {
                reboot_machine();
            } else if (streq(line, "shutdown")) {
                poweroff_machine();
            }
            len = 0; console_write("foxos> "); serial_write("foxos> ");        } else if (ch == '\b') {
            if (len > 0) { len--; console_putc('\b'); }
        } else if (ch >= 32 && ch <= 126) {
            if (history_browse != -1){ history_browse = -1; edit_saved_valid = 0; }
            if (len < (int)sizeof(line)-1) { line[len++] = (char)ch; console_putc((char)ch); }
        }
    }
}
