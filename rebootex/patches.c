#include <string.h>

#include <systemctrl.h>
#include <systemctrl_se.h>

#include "rebootex.h"

FlashBackupList* flash_files = (FlashBackupList*)(0x08800100);

int redirect_flash = 0;

extern void relocateFlashFile(BootFile* file);

int findArkFlashFile(BootFile* file, const char* path){
    u32 nfiles = *(u32*)(ARK_FLASH);
    ArkFlashFile* cur = (ArkFlashFile*)((size_t)(ARK_FLASH)+4);

    for (int i=0; i<nfiles; i++){
        size_t filesize = (cur->filesize[0]) + (cur->filesize[1]<<8) + (cur->filesize[2]<<16) + (cur->filesize[3]<<24);
        if (strncmp(path, cur->name, cur->namelen) == 0){
            file->buffer = (void*)((size_t)(&(cur->name[0])) + cur->namelen);
            file->size = filesize;
            return 0;
        }
        cur = (ArkFlashFile*)((size_t)(cur)+filesize+cur->namelen+5);
    }
    return -1;
}

int pspemuLfatOpenExtra(BootFile* file){

    int ret = -1;
    char* p = file->name;
    
    if (strcmp(p, "pspbtcnf.bin") == 0){
        flash_files->nfiles = 0;
        switch(reboot_conf->iso_mode) {
            case MODE_MARCH33:
            case MODE_INFERNO:
            case MODE_VSHUMD:
                reboot_conf->iso_mode = MODE_INFERNO;
                ret = findArkFlashFile(file, "psvbtjnf.bin"); // inferno ISO mode
                break;
            default:
                ret = findArkFlashFile(file, "psvbtknf.bin"); // np9660 mode
                break;
        }
        if (ret == 0){
            relocateFlashFile(file);
        }
    }
    else if (strncmp(p, "/kd/ark_", 8) == 0){ // ARK module
        ret = findArkFlashFile(file, p);
        if (ret == 0){
            relocateFlashFile(file);
        }
    }

    strcpy((char*)&(flash_files->bootfile[flash_files->nfiles++]), p);
    return ret;
}
