#include <stdint.h>
#include "initrd.h"
#include "vfs.h"

// Weak defaults if not provided by the build system
__attribute__((weak)) const unsigned char initrd_blob[] = "file:/etc/motd=Welcome to foxos\nfile:/init/hello.txt=Hello initrd\n";
__attribute__((weak)) const unsigned int initrd_size = sizeof("file:/etc/motd=Welcome to foxos\nfile:/init/hello.txt=Hello initrd\n")-1;

static int starts_with(const char* s, const char* p){ while(*p){ if(*s++!=*p++) return 0; } return 1; }

void initrd_load_into_ramfs(void){
    // simple line parser: file:<path>=<data> per line
    const char* p = (const char*)initrd_blob; const char* end = p + initrd_size;
    while(p < end){
        const char* nl = p; while(nl<end && *nl!='\n') ++nl;
        if (starts_with(p, "file:")) {
            const char* path = p+5; const char* eq = path; while(eq<nl && *eq!='=') ++eq;
            if (eq<nl) {
                char spath[128]; unsigned i=0; while(path<eq && i<sizeof(spath)-1) spath[i++]=*path++; spath[i]=0;
                const char* data = eq+1; uint32_t len = (uint32_t)(nl - data);
                vfs_write(spath, data, len);
            }
        }
        p = nl + 1;
    }
    // Demo SAM file (same content as our tiny HTML subset)
    vfs_write("/examples/demo.sam",
        "<h1 style=\"color: lightcyan; background: 1\">Welcome to foxos</h1>\n"
        "<p style=\"color: lightgreen\">This is a tiny HTML renderer demo in the console.</p>\n"
        "<p>Plain <span style=\"color: red\">red</span> and <span style=\"color: 14\">yellow</span> text.</p>\n"
        "<br>\n"
        "<script>\nconsole.log('hello from js!')\nalert('alerts look like normal lines')\n</script>\n",
        0 /* length 0 means compute from string in our vfs */);
}
