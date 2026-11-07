#pragma once
#include <stdint.h>
typedef uint64_t u64;
typedef struct {uint64_t st_size;} sysFSStat;
#define SYS_O_RDONLY 0
int sysLv2FsOpen(const char*,int,int*,int,const void*,int);
int sysLv2FsFStat(int,sysFSStat*);
int sysLv2FsRead(int,void*,uint64_t,uint64_t*);
int sysLv2FsClose(int);
