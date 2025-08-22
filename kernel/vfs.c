#include <stdint.h>
#include "vfs.h"
#include "ramfs.h"

void vfs_init(void) { ramfs_init(); }
int vfs_mount_ramfs(void) { return 0; }
int vfs_mkdir(const char* path){ return ramfs_mkdir(path)?0:-1; }
int vfs_write(const char* path, const char* data, uint32_t len){ return ramfs_write(path,data,len); }
int vfs_read(const char* path, char* out, uint32_t max, uint32_t* outLen){ return ramfs_read(path,out,max,outLen); }
int vfs_rm(const char* path){ return ramfs_rm(path); }
int vfs_ls(const char* path, vfs_list_cb cb){ return ramfs_ls(path, cb); }
int vfs_stat(const char* path, vfs_stat_t* st){ int isd=0; uint32_t sz=0, ch=0; int r=ramfs_stat(path,&isd,&sz,&ch); if(st){ st->exists = (r==0); st->isDir = isd; st->size = sz; st->children = ch; } return r; }
