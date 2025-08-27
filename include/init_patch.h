#ifndef INIT_PATCH_H
#define INIT_PATCH_H

#include <pspsdk.h>

#include <module2.h>

SceModule2* patchLoaderCore(void);
int AdrenalinePatchInit(int (* module_bootstart)(SceSize, void *), void *argp);

#endif