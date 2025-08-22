#include <stdint.h>
#include "keyboard.h"
#include "io.h"
#include "console.h"
#include "memory.h"
#include "vfs.h"
#include "initrd.h"
#include "ramfs.h"
#include "ata.h"
#include "render.h"
#include "gui.h"

#ifndef DISK_SECTORS
#define DISK_SECTORS 2880
#endif
#ifndef DISK_SECTOR_SIZE
#define DISK_SECTOR_SIZE 512
#endif
#ifndef PT_LBA_START
#define PT_LBA_START 0
#endif
#ifndef PT_LBA_COUNT
#define PT_LBA_COUNT 0
#endif

static inline char to_lower(char c){ return (c>='A'&&c<='Z')? (char)(c+32): c; }
static int streq(const char* a, const char* b){ while(*a && *b){ if(*a!=*b) return 0; ++a; ++b; } return *a==0 && *b==0; }
static int startswith(const char* s,const char* p){ while(*p){ if(*s++!=*p++) return 0; } return 1; }

static void list_cb(const char* name, int isDir){ console_write(isDir?"[D] ":"[F] "); console_writeln(name); }

// Forward declaration for recursive remove
static int vfs_rm_recursive(const char* path);

// Simple path resolver relative to CWD. Supports '.' and '..'.
static void path_resolve(char* out, const char* cwd, const char* in){
    // if absolute
    if (in && *in=='/') {
        int i=0; while(in[i]){ out[i]=in[i]; i++; } out[i]=0;
    } else {
        // start with cwd
        int k=0; while(cwd[k]){ out[k]=cwd[k]; k++; }
        if (k==0) out[k++]='/';
        if (in && *in){ if (out[k-1]!='/') out[k++]='/'; int i=0; while(in[i]) out[k++]=in[i++]; }
        out[k]=0;
    }
    // normalize: process segments, handling '.' and '..'
    char tmp[128]; int ti=0; int i=0; // skip leading '/'
    if (out[0]!='/'){ tmp[ti++]='/'; }
    int depth=0; int segStart=1;
    tmp[0]='/'; ti=1; depth=0; segStart=1;
    while(out[i]){
        // copy until next '/'
        int j=i; while(out[j] && out[j]!='/') j++;
        int segLen = j-i;
        if (segLen==0) { i = j+1; continue; }
        // check segment content
        if (segLen==1 && out[i]=='.') {
            // skip '.'
        } else if (segLen==2 && out[i]=='.' && out[i+1]=='.') {
            // pop last segment
            if (ti>1){
                // remove trailing '/'
                if (tmp[ti-1]=='/' && ti>1) ti--;
                // backtrack to previous '/'
                while (ti>1 && tmp[ti-1]!='/') ti--;
            }
        } else {
            if (tmp[ti-1] != '/') tmp[ti++]='/';
            for (int k=0;k<segLen && ti<127;k++) tmp[ti++]=out[i+k];
            tmp[ti]=0;
        }
        i = j;
        if (out[i] == '/') i++;
    }
    if (ti>1 && tmp[ti-1]=='/') ti--; tmp[ti]=0;
    // ensure not empty
    if (ti==0){ tmp[ti++]='/'; tmp[ti]=0; }
    // copy back
    for (int k=0; tmp[k]; ++k) out[k]=tmp[k]; out[ti]=0;
}

static void u32_to_dec(uint32_t v, char* buf){ int n=0; if(v==0){ buf[n++]='0'; buf[n]=0; return; } char tmp[16]; int t=0; while(v){ tmp[t++] = (char)('0'+(v%10)); v/=10; } while(t--) buf[n++]=tmp[t]; buf[n]=0; }

// Parse decimal uint32 from token (null-terminated)
static int parse_u32_dec(const char* s, uint32_t* out){ if(!s||!*s) return -1; uint32_t v=0; for(int i=0; s[i]; ++i){ char c=s[i]; if(c<'0'||c>'9') return -2; uint32_t d=(uint32_t)(c-'0'); uint32_t nv = v*10u + d; if (nv < v) return -3; v = nv; } *out=v; return 0; }
// Parse hex byte from 1-2 hex digits
static int parse_hex8(const char* s, uint8_t* out){ if(!s||!*s) return -1; uint32_t v=0; int i=0; for(; s[i] && i<2; ++i){ char c=s[i]; if(c>='0'&&c<='9') v = (v<<4) | (uint32_t)(c-'0'); else { char lc = (c>='A'&&c<='Z')?(c+32):c; if(lc>='a'&&lc<='f') v = (v<<4) | (uint32_t)(10 + lc-'a'); else return -2; } } if(s[i]) return -3; *out=(uint8_t)v; return 0; }

#ifdef DISK_MODE_HDD
static void mbr_zero(uint8_t* m){ for(int i=0;i<512;++i) m[i]=0; m[510]=0x55; m[511]=0xAA; }
static void mbr_set_entry(uint8_t* m, int idx, uint8_t boot, uint8_t type, uint32_t lba_start, uint32_t lba_count){ int o=446+idx*16; m[o+0]=boot; m[o+1]=0; m[o+2]=0; m[o+3]=0; m[o+4]=type; m[o+5]=0xFF; m[o+6]=0xFF; m[o+7]=0xFF; m[o+8]=(uint8_t)(lba_start&0xFF); m[o+9]=(uint8_t)((lba_start>>8)&0xFF); m[o+10]=(uint8_t)((lba_start>>16)&0xFF); m[o+11]=(uint8_t)((lba_start>>24)&0xFF); m[o+12]=(uint8_t)(lba_count&0xFF); m[o+13]=(uint8_t)((lba_count>>8)&0xFF); m[o+14]=(uint8_t)((lba_count>>16)&0xFF); m[o+15]=(uint8_t)((lba_count>>24)&0xFF); }
static int mbr_read(uint8_t* m){ return ata_pio_read28(0,m); }
static int mbr_write(const uint8_t* m){ return ata_pio_write28(0,m); }
static void print_part(int idx, uint8_t boot, uint8_t type, uint32_t s, uint32_t c){ console_write("#"); char nb[4]; u32_to_dec((uint32_t)idx, nb); console_write(nb); console_write(" "); console_write(boot?"* ":"  "); console_write("type=0x"); char hx[3]; const char* hexd="0123456789ABCDEF"; hx[0]=hexd[(type>>4)&0xF]; hx[1]=hexd[type&0xF]; hx[2]=0; console_write(hx); console_write(" start="); char b1[16]; u32_to_dec(s,b1); console_write(b1); console_write(" count="); char b2[16]; u32_to_dec(c,b2); console_writeln(b2); }
#endif

// Helper state for directory listing during recursive delete
static int g_ls_found = 0;
static char g_ls_first_name[128];
static void rm_first_child_cb(const char* name, int isDir){
    if (g_ls_found) return;
    int i=0; while(name[i] && i < (int)sizeof(g_ls_first_name)-1){ g_ls_first_name[i] = name[i]; i++; }
    g_ls_first_name[i] = 0;
    g_ls_found = 1;
}

// Recursive remove implementation using VFS APIs
static int vfs_rm_recursive(const char* path){
    // Don't allow removing root
    if (path[0]=='/' && path[1]==0) return -1;

    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -1;

    if (!st.isDir){
        return vfs_rm(path);
    }

    // Directory: remove children until empty
    for(;;){
        g_ls_found = 0;
        vfs_ls(path, rm_first_child_cb);
        if (!g_ls_found) break; // empty

        char child[256];
        int l=0; while(path[l]){ child[l]=path[l]; l++; }
        if (!(l>0 && child[l-1]=='/')) child[l++]='/';
        int i=0; while(g_ls_first_name[i] && l < (int)sizeof(child)-1){ child[l++] = g_ls_first_name[i++]; }
        child[l]=0;

        int r = vfs_rm_recursive(child);
        if (r != 0) return r;
    }

    return vfs_rm(path);
}

void kernel_main() {
    console_init();
    console_set_color(0x0F, 0x00);
    console_writeln("foxos console ready");

    mem_init();

    vfs_init();
    vfs_mount_ramfs();
    initrd_load_into_ramfs();
    console_writeln("vfs: ramfs mounted, initrd loaded");

#ifdef DISK_MODE_HDD
    ata_init();
#endif

    // Initialize GUI (disabled by default to avoid black screen). Define ENABLE_GUI to re-enable.
#ifdef ENABLE_GUI
    gui_init();
#else
    // gui_init();
#endif

    keyboard_init();

    char cwd[128]; cwd[0] = '/'; cwd[1] = 0;
    char line[256]; int len = 0;
    console_write("foxos> ");

    for (;;) {
        int ch = keyboard_getchar();
        if (ch < 0) { for (int i=0;i<1000;++i) io_wait(); continue; }
        if (ch == '\n') {
            console_putc('\n');
            line[len] = '\0';
            // lowercase command keyword only
            int i=0; while(line[i] && line[i]!=' '){ line[i]=to_lower(line[i]); i++; }

            if (streq(line, "clear")) {
                console_clear();
            } else if (streq(line, "help")) {
                console_writeln("commands:");
                console_writeln("  help                 - show this help");
                console_writeln("  clear                - clear screen");
                console_writeln("  ls [path]            - list directory");
                console_writeln("  pwd                  - print working dir");
                console_writeln("  cd <dir>             - change directory");
                console_writeln("  cd ..                - go up one level");
                console_writeln("  cat <path>           - print file");
                console_writeln("  touch <path>         - create empty file");
                console_writeln("  cp <src> <dst>       - copy file (<=1024B)");
                console_writeln("  mv <src> <dst>       - move/rename file");
                console_writeln("  echo TEXT > PATH     - write file");
                console_writeln("  echo TEXT >> PATH    - append to file");
                console_writeln("  mkdir <dir>          - create directory");
                console_writeln("  rm <path>            - remove file");
                console_writeln("  rmdir <path>         - remove directory");
                console_writeln("  rm -r <path>         - recursive remove");
                console_writeln("  stat <path>          - show file/dir info");
                console_writeln("  render <file>        - render tiny SAM file (HTML-like)");
#ifdef DISK_MODE_FLOPPY
                console_writeln("  disk                 - show floppy info");
#else
                console_writeln("  disk info            - show disk mode/size/sectors");
                console_writeln("  disk list            - list MBR partitions");
                console_writeln("  disk mkpt N          - create N primary partitions");
                console_writeln("  disk mkpart i s c t  - set entry i=start,count,typeHex");
                console_writeln("  disk clear           - zero the MBR (keep 0x55AA)");
#endif
            } else if (streq(line, "ls")) {
                vfs_ls(cwd, list_cb);
            } else if (startswith(line, "ls ")) {
                char path[128]; path_resolve(path, cwd, line+3); vfs_ls(path, list_cb);
            } else if (streq(line, "pwd")) {
                console_writeln(cwd);
            } else if (streq(line, "cd ..")) {
                // go to parent
                int L=0; while(cwd[L]) L++; if (L>1){ // remove trailing segment
                    if (cwd[L-1]=='/' && L>1) L--; // remove trailing /
                    while(L>1 && cwd[L-1]!='/') L--; if (L>1 && cwd[L-1]=='/') L--; if (L==0) { cwd[0]='/'; cwd[1]=0; } else { cwd[L+1]=0; cwd[L+1]=0; }
                    // rebuild properly
                    if (L<=1){ cwd[0]='/'; cwd[1]=0; } else { cwd[L+1]=0; }
                }
                console_writeln("ok");
            } else if (startswith(line, "cd ")) {
                char path[128]; path_resolve(path, cwd, line+3); vfs_stat_t st; if (vfs_stat(path,&st)==0 && st.isDir){ int j=0; while(path[j]){ cwd[j]=path[j]; j++; } cwd[j]=0; console_writeln("ok"); } else { console_writeln("cd: no such dir"); }
            } else if (startswith(line, "cat ")) {
                char path[128]; path_resolve(path, cwd, line+4); char buf[256]; uint32_t out=0; if (vfs_read(path, buf, sizeof(buf)-1, &out)==0){ buf[out]=0; console_writeln(buf);} else { console_writeln("cat: not found"); }
            } else if (startswith(line, "touch ")) {
                char path[128]; path_resolve(path, cwd, line+6); uint32_t out=0; if (vfs_read(path, 0, 0, &out)==0) { console_writeln("ok"); } else { if (vfs_write(path, "", 0)==0) console_writeln("ok"); else console_writeln("touch failed"); }
            } else if (startswith(line, "cp ")) {
                // cp <src> <dst> (files only, up to 1024 bytes)
                char* p = line+3; while(*p==' ') p++; char* src = p; while(*p && *p!=' ') p++; if(!*p){ console_writeln("usage: cp <src> <dst>"); }
                else { *p=0; char* dst = p+1; char sp[128], dp[128]; path_resolve(sp,cwd,src); path_resolve(dp,cwd,dst); char buf[1024]; uint32_t n=0; if (vfs_read(sp, buf, sizeof(buf), &n)==0){ if (vfs_write(dp, buf, n)==0) console_writeln("ok"); else console_writeln("cp: write failed"); } else console_writeln("cp: read failed"); }
            } else if (startswith(line, "mv ")) {
                // mv <src> <dst> (files only, up to 1024 bytes)
                char* p = line+3; while(*p==' ') p++; char* src = p; while(*p && *p!=' ') p++; if(!*p){ console_writeln("usage: mv <src> <dst>"); }
                else { *p=0; char* dst = p+1; char sp[128], dp[128]; path_resolve(sp,cwd,src); path_resolve(dp,cwd,dst); char buf[1024]; uint32_t n=0; if (vfs_read(sp, buf, sizeof(buf), &n)==0){ if (vfs_write(dp, buf, n)==0){ if (vfs_rm(sp)==0) console_writeln("ok"); else console_writeln("mv: remove src failed"); } else console_writeln("mv: write failed"); } else console_writeln("mv: read failed"); }
            } else if (startswith(line, "echo ")) {
                // echo text > /path  OR  echo text >> /path (append)
                char* p = line+5; // after space
                char* gt = p; while(*gt && *gt!='>') gt++;
                if (*gt=='>') {
                    if (gt[1]=='>' && gt[2]==' ') {
                        *gt = 0; // terminate text
                        char path_in[128]; char* path = gt+3; path_resolve(path_in, cwd, path);
                        // append: read existing (up to 1024), then write concatenation
                        char buf[1024]; uint32_t n=0; if (vfs_read(path_in, buf, sizeof(buf), &n)!=0) n=0; // if not exist, treat as empty
                        uint32_t tlen=0; while(p[tlen]) tlen++;
                        if (n + tlen > sizeof(buf)) { console_writeln("append too large"); }
                        else { for (uint32_t k=0;k<tlen;++k) buf[n+k]=p[k]; if (vfs_write(path_in, buf, n+tlen)==0) console_writeln("ok"); else console_writeln("write failed"); }
                    } else if (gt[1]==' ') {
                        *gt = 0; char path_in[128]; char* path = gt+2; path_resolve(path_in, cwd, path); uint32_t l=0; while(p[l]) l++; if (vfs_write(path_in,p,l)==0) console_writeln("ok"); else console_writeln("write failed");
                    } else {
                        console_writeln("usage: echo TEXT > /path | echo TEXT >> /path");
                    }
                } else {
                    console_writeln(p);
                }
            } else if (startswith(line, "mkdir ")) {
                char path[128]; path_resolve(path, cwd, line+6); if (vfs_mkdir(path)==0) console_writeln("ok"); else console_writeln("mkdir failed");
            } else if (startswith(line, "rm -r ")) {
                char path[128]; path_resolve(path, cwd, line+6); if (vfs_rm_recursive(path)==0) console_writeln("ok"); else console_writeln("rm -r failed");
            } else if (startswith(line, "rm ")) {
                char path[128]; path_resolve(path, cwd, line+3); if (vfs_rm(path)==0) console_writeln("ok"); else console_writeln("rm failed");
            } else if (startswith(line, "rmdir ")) {
                char path[128]; path_resolve(path, cwd, line+6); if (vfs_rm(path)==0) console_writeln("ok"); else console_writeln("rmdir failed");
            } else if (startswith(line, "stat ")) {
                char path[128]; path_resolve(path, cwd, line+5); vfs_stat_t st; if (vfs_stat(path,&st)==0){
                    console_write("type: "); console_writeln(st.isDir?"dir":"file");
                    if(!st.isDir){ console_write("size: "); char buf[16]; u32_to_dec(st.size, buf); console_writeln(buf);} else { console_write("children: "); char buf[16]; u32_to_dec(st.children, buf); console_writeln(buf);} }
                else { console_writeln("stat: not found"); }
            } else if (streq(line, "disk")) {
#ifdef DISK_MODE_FLOPPY
                console_writeln("disk: superfloppy 1.44MB, 512-byte sectors, 2880 sectors, no partition table");
#else
#ifdef DISK_SIZE_MB
                console_write("disk: hdd ");
                char num[16]; int n=0; uint32_t mb=DISK_SIZE_MB; char tmp[16]; if(mb==0){ num[n++]='0'; } else { int t=0; while(mb){ tmp[t++]=(char)('0'+(mb%10)); mb/=10; } while(t--) num[n++]=tmp[t]; } num[n]=0;
                console_write(num); console_writeln("MB, 512-byte sectors");
#else
                console_writeln("disk: hdd, 512-byte sectors");
#endif
#endif
            } else if (startswith(line, "disk ")) {
                char* p = line+5; while(*p==' ') p++;
#ifdef DISK_MODE_FLOPPY
                console_writeln("not supported in floppy mode");
#else
                if (streq(p, "info")) {
                    console_writeln("mode: hdd");
#ifdef DISK_SIZE_MB
                    console_write("size: "); char b[16]; u32_to_dec(DISK_SIZE_MB, b); console_write(b); console_writeln(" MB");
#endif
                    console_write("sectors: "); char b2[16]; u32_to_dec(DISK_SECTORS, b2); console_writeln(b2);
                } else if (streq(p, "list")) {
                    uint8_t m[512]; if (ata_available() && mbr_read(m)==0){ if (m[510]!=0x55||m[511]!=0xAA){ console_writeln("no MBR signature"); } else { for(int i=0;i<4;++i){ int o=446+i*16; uint8_t boot=m[o+0]; uint8_t type=m[o+4]; uint32_t s = (uint32_t)m[o+8] | ((uint32_t)m[o+9]<<8) | ((uint32_t)m[o+10]<<16) | ((uint32_t)m[o+11]<<24); uint32_t c = (uint32_t)m[o+12] | ((uint32_t)m[o+13]<<8) | ((uint32_t)m[o+14]<<16) | ((uint32_t)m[o+15]<<24); if(type!=0){ print_part(i,boot,type,s,c);} } } } else console_writeln("ata: no drive");
                } else if (startswith(p, "mkpt ")) {
                    char* q = p+5; while(*q==' ') q++; char* t1=q; while(*q && *q!=' ') q++; if(*q){ *q=0; q++; }
                    uint32_t n=0; if(parse_u32_dec(t1,&n)!=0 || n==0 || n>4){ console_writeln("usage: disk mkpt N (1..4)"); }
                    else if(!ata_available()){ console_writeln("ata: no drive"); }
                    else {
                        uint8_t m[512]; mbr_zero(m);
                        uint32_t base = (PT_LBA_START?PT_LBA_START:2048);
                        if (DISK_SECTORS<=base){ console_writeln("disk too small"); }
                        else {
                            uint32_t avail = DISK_SECTORS - base;
                            uint32_t each = avail / n; if(each==0){ console_writeln("too many partitions"); }
                            else {
                                for(uint32_t i=0;i<n && i<4;i++){
                                    uint32_t start = base + i*each;
                                    uint32_t count = (i==n-1)? (avail - i*each) : each;
                                    mbr_set_entry(m,(int)i, (i==0)?0x80:0x00, 0x83, start, count);
                                }
                                if (mbr_write(m)==0) console_writeln("ok"); else console_writeln("write failed");
                            }
                        }
                    }
                } else if (startswith(p, "mkpart ")) {
                    // disk mkpart i start count typeHex
                    char* q = p+7; while(*q==' ') q++;
                    // token 1: index
                    char* t1=q; while(*q && *q!=' ') q++; if(*q){ *q=0; q++; }
                    while(*q==' ') q++;
                    // token 2: start
                    char* t2=q; while(*q && *q!=' ') q++; if(*q){ *q=0; q++; }
                    while(*q==' ') q++;
                    // token 3: count
                    char* t3=q; while(*q && *q!=' ') q++; if(*q){ *q=0; q++; }
                    while(*q==' ') q++;
                    // token 4: type hex (e.g., 83)
                    char* t4=q; while(*q && *q!=' ') q++; *q=0;
                    uint32_t idx=0,start=0,count=0; uint8_t type=0;
                    if(parse_u32_dec(t1,&idx)!=0 || idx>3 || parse_u32_dec(t2,&start)!=0 || parse_u32_dec(t3,&count)!=0 || parse_hex8(t4,&type)!=0){ console_writeln("usage: disk mkpart i start count typeHex"); }
                    else if(!ata_available()){ console_writeln("ata: no drive"); }
                    else {
                        uint8_t m[512]; if(mbr_read(m)!=0){ console_writeln("read failed"); }
                        else { if (m[510]!=0x55||m[511]!=0xAA) mbr_zero(m); mbr_set_entry(m,(int)idx,(idx==0)?0x80:0x00,type,start,count); if(mbr_write(m)==0) console_writeln("ok"); else console_writeln("write failed"); }
                    }
                } else if (streq(p, "clear")) {
                    if(!ata_available()){ console_writeln("ata: no drive"); }
                    else { uint8_t m[512]; mbr_zero(m); if(mbr_write(m)==0) console_writeln("ok"); else console_writeln("write failed"); }
                } else {
                    console_writeln("usage: disk info|list|mkpt N|mkpart i start count type|clear");
                }
#endif
            } else if (startswith(line, "render ")) {
                char path[128]; path_resolve(path, cwd, line+7);
                if (render_file(path)==0) console_writeln("render ok"); else console_writeln("render failed");
            } else if (streq(line, "gui demo")) {
#ifdef ENABLE_GUI
                if (!g_fb.present){ console_writeln("no framebuffer"); }
                else {
                    GuiWindow w; gui_window_init(&w, 40, 30, 320, 200, "foxos GUI");
                    gui_window_draw(&w);
                    gui_window_fill_text(&w, "Hello GUI!\nThis is a stub per-pixel window.");
                    console_writeln("gui ok");
                }
#else
                console_writeln("gui disabled");
#endif
            }
            len = 0;
            console_write("foxos> ");
        } else if (ch == '\b') {
            if (len > 0) { len--; console_putc('\b'); }
        } else if (ch >= 32 && ch <= 126) {
            if (len < (int)sizeof(line)-1) { line[len++] = (char)ch; console_putc((char)ch); }
        }
    }
}
