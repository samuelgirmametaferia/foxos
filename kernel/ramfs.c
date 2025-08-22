#include <stdint.h>
#include <stddef.h>
#include "ramfs.h"
#include "memory.h"

static ramfs_node_t root;

static int strncmpn(const char* a, const char* b, size_t n){ for(size_t i=0;i<n;++i){ if(a[i]!=b[i]||!a[i]||!b[i]) return (unsigned char)a[i]-(unsigned char)b[i]; } return 0; }
static int strcmpz(const char* a,const char* b){ while(*a&&*b){ if(*a!=*b) return (unsigned char)*a-(unsigned char)*b; ++a;++b;} return (unsigned char)*a-(unsigned char)*b; }
static size_t strlenz(const char* s){ size_t n=0; while(s[n])++n; return n; }
static void strncpyz(char* d,const char* s,size_t m){ size_t i=0; for(; i<m-1 && s[i]; ++i) d[i]=s[i]; d[i]=0; }

void ramfs_init(void){
    root.name[0] = '/'; root.name[1]=0;
    root.isDir = 1; root.parent = NULL; root.firstChild = NULL; root.nextSibling = NULL; root.data = 0; root.size = 0;
}

ramfs_node_t* ramfs_root(void){ return &root; }

static ramfs_node_t* add_child(ramfs_node_t* dir, const char* name, int isDir){
    static ramfs_node_t nodes[256];
    static uint32_t used = 0;
    if (used >= 256) return NULL;
    ramfs_node_t* n = &nodes[used++];
    strncpyz(n->name, name, sizeof(n->name));
    n->isDir = isDir; n->parent = dir; n->firstChild = NULL; n->nextSibling = dir->firstChild; dir->firstChild = n; n->data = 0; n->size=0;
    return n;
}

static const char* skip_sep(const char* p){ while(*p=='/') ++p; return p; }
static const char* next_sep(const char* p){ while(*p && *p!='/') ++p; return p; }

static ramfs_node_t* ensure_dir_path(const char* path){
    const char* p = skip_sep(path);
    ramfs_node_t* cur = &root;
    while(*p){
        const char* q = next_sep(p);
        size_t len = (size_t)(q-p);
        if(len==0) break;
        // last segment stops if end
        int last = (*q==0);
        // find in children
        ramfs_node_t* c = cur->firstChild;
        ramfs_node_t* found = NULL;
        while(c){ if(!strncmpn(c->name,p,len) && c->name[len]==0){ found=c; break;} c=c->nextSibling; }
        if(!found){ // create dir
            char name[32]; size_t m = len<31?len:31; for(size_t i=0;i<m;++i) name[i]=p[i]; name[m]=0;
            found = add_child(cur, name, 1);
            if(!found) return NULL;
        }
        if(!found->isDir && !last) return NULL;
        cur = found; p = skip_sep(q);
    }
    return cur;
}

ramfs_node_t* ramfs_find(const char* path){
    const char* p = skip_sep(path);
    ramfs_node_t* cur = &root;
    while(*p){
        const char* q = next_sep(p);
        size_t len = (size_t)(q-p);
        if(len==0) break;
        ramfs_node_t* c = cur->firstChild; ramfs_node_t* found=NULL;
        while(c){ if(!strncmpn(c->name,p,len) && c->name[len]==0){ found=c; break;} c=c->nextSibling; }
        if(!found) return NULL;
        cur = found; p = skip_sep(q);
    }
    return cur;
}

ramfs_node_t* ramfs_mkdir(const char* path){ return ensure_dir_path(path); }

int ramfs_write(const char* path, const char* data, uint32_t len){
    // split path into dir + name
    const char* p = path; const char* last = p; for(; *p; ++p) if(*p=='/') last=p+1; const char* name = last;
    char dpath[128]; uint32_t dn = (uint32_t)(name - path); if(dn>=sizeof(dpath)) dn=sizeof(dpath)-1; for(uint32_t i=0;i<dn;++i) dpath[i]=path[i]; dpath[dn]=0;
    ramfs_node_t* dir = ramfs_mkdir(dpath);
    if(!dir) return -1;
    // find existing
    ramfs_node_t* c=dir->firstChild; while(c){ if(!strcmpz(c->name,name)) break; c=c->nextSibling; }
    if(!c){ c = add_child(dir, name, 0); if(!c) return -2; }
    // (re)allocate data handle
    if(c->data) uc_free(c->data);
    uchandle_t h = uc_alloc(len ? len : 1);
    if(!h) return -3;
    if(len){ uc_write(h, data, len); }
    c->data = h; c->size = len;
    return 0;
}

int ramfs_read(const char* path, char* out, uint32_t max, uint32_t* outLen){
    ramfs_node_t* n = ramfs_find(path);
    if(!n || n->isDir) return -1;
    uint32_t to = (n->size < max) ? n->size : max;
    if(to && n->data) uc_read(n->data, 0, out, to);
    if(outLen) *outLen = to;
    return 0;
}

int ramfs_rm(const char* path){
    ramfs_node_t* n = ramfs_find(path);
    if(!n || n==&root) return -1;
    // unlink from parent list
    ramfs_node_t* p = n->parent; if(!p) return -1;
    ramfs_node_t** cur = &p->firstChild; while(*cur && *cur!=n) cur=&(*cur)->nextSibling; if(*cur) *cur = n->nextSibling;
    if(n->data) uc_free(n->data);
    return 0;
}

int ramfs_ls(const char* path, void (*cb)(const char*, int)){
    ramfs_node_t* n = ramfs_find(path);
    if(!n || !n->isDir) return -1;
    ramfs_node_t* c = n->firstChild;
    while(c){ cb(c->name, c->isDir); c = c->nextSibling; }
    return 0;
}

int ramfs_stat(const char* path, int* isDir, uint32_t* size, uint32_t* children){
    ramfs_node_t* n = ramfs_find(path);
    if(!n){ if(isDir) *isDir=0; if(size) *size=0; if(children) *children=0; return -1; }
    if(isDir) *isDir = n->isDir ? 1 : 0;
    if(size) *size = n->isDir ? 0 : n->size;
    if(children){ uint32_t c=0; if(n->isDir){ ramfs_node_t* x=n->firstChild; while(x){ c++; x=x->nextSibling; } } *children = c; }
    return 0;
}
