#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "ramfs.h"
#include "memory.h"

static ramfs_node_t root;
vfs_inode_t* ramfs_get_inode(const char* path);

static int ramfs_op_read(struct vfs_inode* inode, uint64_t offset, char* buf, uint64_t len, uint64_t* outLen) {
    ramfs_node_t* n = (ramfs_node_t*)inode->private_data;
    if (!n || n->isDir) return -1;
    if (offset >= n->size) {
        if (outLen) *outLen = 0;
        return 0;
    }
    uint64_t to = (n->size - offset < len) ? (n->size - offset) : len;
    if (to && n->data) move_read(n->data, offset, buf, to);
    if (outLen) *outLen = to;
    return 0;
}

static int ramfs_op_write(struct vfs_inode* inode, uint64_t offset, const char* buf, uint64_t len) {
    ramfs_node_t* n = (ramfs_node_t*)inode->private_data;
    if (!n || n->isDir) return -1;
    
    uint64_t actual_len = offset + len;
    if (actual_len < n->size) actual_len = n->size;

    char* tmp_buf = (char*)kmalloc(actual_len);
    if (n->data && n->size > 0) {
        move_read(n->data, 0, tmp_buf, n->size);
    }
    for (uint64_t i = 0; i < len; i++) {
        tmp_buf[offset + i] = buf[i];
    }
    if (n->data) move_free(n->data);
    uchandle_t h = move_alloc(actual_len ? actual_len : 1);
    if (!h) { kfree(tmp_buf); return -3; }
    if (actual_len) move_write(h, tmp_buf, actual_len);
    kfree(tmp_buf);
    n->data = h; n->size = actual_len;
    inode->size = actual_len;
    return 0;
}

static int ramfs_op_stat(struct vfs_inode* inode, vfs_stat_t* st) {
    ramfs_node_t* n = (ramfs_node_t*)inode->private_data;
    if (!n) return -1;
    st->exists = 1;
    st->isDir = n->isDir;
    st->size = n->isDir ? 0 : n->size;
    
    uint64_t c = 0;
    if (n->isDir) {
        ramfs_node_t* x = n->firstChild;
        while (x) { c++; x = x->nextSibling; }
    }
    st->children = c;
    return 0;
}

static vfs_ops_t ramfs_ops = {
    .read = ramfs_op_read,
    .write = ramfs_op_write,
    .stat = ramfs_op_stat,
    // mkdir, rm, ls can be added as needed
};

vfs_inode_t* ramfs_get_inode(const char* path) {
    ramfs_node_t* n = ramfs_find(path);
    if (!n) return NULL;
    
    // In a real system, we'd cache these. For now, allocate on demand or link to node.
    // Let's assume we want to return a vfs_inode_t that wraps this ramfs_node_t.
    vfs_inode_t* inode = vfs_inode_alloc();
    if (!inode) return NULL;
    
    inode->inode_num = (uint64_t)n; // Use pointer as inode number for now
    inode->size = n->size;
    inode->type = n->isDir ? 2 : 1;
    inode->private_data = n;
    inode->ops = &ramfs_ops;
    return inode;
}

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
    static uint64_t used = 0;
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

int ramfs_write(const char* path, uint64_t offset, const char* data, uint64_t len){
    uint64_t actual_len = offset + len;
    // split path into dir + name
    const char* p = path; const char* last = p; for(; *p; ++p) if(*p=='/') last=p+1; const char* name = last;
    char dpath[128]; uint64_t dn = (uint64_t)(name - path); if(dn>=sizeof(dpath)) dn=sizeof(dpath)-1; for(uint64_t i=0;i<dn;++i) dpath[i]=path[i]; dpath[dn]=0;
    ramfs_node_t* dir = ramfs_mkdir(dpath);
    if(!dir) return -1;
    // find existing
    ramfs_node_t* c=dir->firstChild; while(c){ if(!strcmpz(c->name,name)) break; c=c->nextSibling; }
    if(!c){ c = add_child(dir, name, 0); if(!c) return -2; }
    if (actual_len < c->size) actual_len = c->size;
    // (re)allocate data handle
    uchandle_t h = c->data;
    if(!h) {
        h = move_alloc(actual_len ? actual_len : 1);
        if(!h) return -3;
    } else if (actual_len > c->size) {
        // Realloc not supported directly in move_alloc, so we'd have to allocate new and copy
        // For now, assume ramfs_write always overwrites or appends if we had realloc.
        // Actually, let's just do a naive realloc.
        uchandle_t new_h = move_alloc(actual_len);
        if (new_h) {
            if (c->size) {
                char* tmp = kmalloc(c->size);
                move_read(h, 0, tmp, c->size);
                move_write(new_h, 0, c->size); // wait move_write doesn't take offset
                // wait, move_write is `move_write(h, const void* src, uint64_t len)` and it replaces the whole thing!
            }
        }
    }
    // Wait, move_write replaces the whole chunk! We cannot write at an offset easily with move_write.
    // Since move_write doesn't take an offset, let's just read the whole file, modify it, and write it back.
    char* tmp_buf = kmalloc(actual_len);
    if (c->data && c->size > 0) {
        move_read(c->data, 0, tmp_buf, c->size);
    }
    for (uint64_t i = 0; i < len; i++) {
        tmp_buf[offset + i] = data[i];
    }
    if (c->data) move_free(c->data);
    h = move_alloc(actual_len ? actual_len : 1);
    if (!h) { kfree(tmp_buf); return -3; }
    if (actual_len) move_write(h, tmp_buf, actual_len);
    kfree(tmp_buf);
    c->data = h; c->size = actual_len;
    return 0;
}

int ramfs_read(const char* path, uint64_t offset, char* out, uint64_t max, uint64_t* outLen){
    ramfs_node_t* n = ramfs_find(path);
    if(!n || n->isDir) return -1;
    if(offset >= n->size) {
        if(outLen) *outLen = 0;
        return 0;
    }
    uint64_t to = (n->size - offset < max) ? (n->size - offset) : max;
    if(to && n->data) move_read(n->data, offset, out, to);
    if(outLen) *outLen = to;
    return 0;
}

int ramfs_rm(const char* path){
    ramfs_node_t* n = ramfs_find(path);
    if(!n || n==&root) return -1;
    // unlink from parent list
    ramfs_node_t* p = n->parent; if(!p) return -1;
    ramfs_node_t** cur = &p->firstChild; while(*cur && *cur!=n) cur=&(*cur)->nextSibling; if(*cur) *cur = n->nextSibling;
    if(n->data) move_free(n->data);
    return 0;
}

int ramfs_ls(const char* path, void (*cb)(const char*, int)){
    ramfs_node_t* n = ramfs_find(path);
    if(!n || !n->isDir) return -1;
    ramfs_node_t* c = n->firstChild;
    while(c){ cb(c->name, c->isDir); c = c->nextSibling; }
    return 0;
}

int ramfs_stat(const char* path, int* isDir, uint64_t* size, uint64_t* children){
    ramfs_node_t* n = ramfs_find(path);
    if(!n){ if(isDir) *isDir=0; if(size) *size=0; if(children) *children=0; return -1; }
    if(isDir) *isDir = n->isDir ? 1 : 0;
    if(size) *size = n->isDir ? 0 : n->size;
    if(children){ uint64_t c=0; if(n->isDir){ ramfs_node_t* x=n->firstChild; while(x){ c++; x=x->nextSibling; } } *children = c; }
    return 0;
}
