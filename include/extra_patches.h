#ifndef EXTRA_PATCHES_H
#define EXTRA_PATCHES_H

#include <pspsdk.h>

extern u64 kermit_flash_load(int cmd);
extern void patchUsbCam(SceModule2*);
extern void patchVLF(SceModule2*);
extern void PatchImposeDriver(u32 text_addr);
extern void PatchUtility();
extern void PatchPowerService(u32 text_addr);
extern void PatchLoadExec(u32 text_addr, u32 text_size);

#endif