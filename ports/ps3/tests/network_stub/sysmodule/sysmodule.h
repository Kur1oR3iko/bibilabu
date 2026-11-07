#pragma once
#define SYSMODULE_NETCTL 1
#define SYSMODULE_ERR_DUPLICATE 0x80012001u
int sysModuleLoad(int module);
int sysModuleUnload(int module);
