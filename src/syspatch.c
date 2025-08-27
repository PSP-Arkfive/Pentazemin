#include <string.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>
#include <psputilsforkernel.h>
#include <pspinit.h>
#include <pspumd.h>
#include <pspdisplay.h>

#include <ark.h> 
#include <kermit.h>
#include <cfwmacros.h>
#include <rebootconfig.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <systemctrl_private.h>

#include "funcs.h"
#include "adrenaline.h"
#include "adrenaline_compat.h"
#include "io_patch.h"
#include "init_patch.h"
#include "extra_patches.h"

extern SEConfig* se_config;

extern int (*_sctrlHENApplyMemory)(u32);
extern int memoryHandlerVita(u32 p2);

// Previous Module Start Handler
STMOD_HANDLER previous = NULL;

int (* DisplaySetFrameBuf)(void*, int, int, int) = NULL;


void OnSystemStatusIdle() {
    SceAdrenaline *adrenaline = (SceAdrenaline *)ADRENALINE_ADDRESS;

    initAdrenalineInfo();

    // Set fake framebuffer so that cwcheat can be displayed
    if (adrenaline->pops_mode) {
        DisplaySetFrameBuf = (void*)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", 0xA38B3F89);
        DisplaySetFrameBuf((void *)NATIVE_FRAMEBUFFER, PSP_SCREEN_LINE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        memset((void *)NATIVE_FRAMEBUFFER, 0, SCE_PSPEMU_FRAMEBUFFER_SIZE);
    } else {
        SendAdrenalineCmd(ADRENALINE_VITA_CMD_RESUME_POPS);
    }
}

// kermit_peripheral's sub_000007CC clone, called by loadexec + 0x0000299C with a0=8 (was a0=7 for fw <210)
// Returns 0 on success
u64 kermit_flash_load(int cmd)
{
    u8 buf[128];
    u64 resp;
    void *alignedBuf = (void*)ALIGN_64((int)buf + 63);
    sceKernelDcacheInvalidateRange(alignedBuf, 0x40);
    KermitPacket *packet = (KermitPacket *)KERMIT_PACKET((int)alignedBuf);
    u32 argc = 0;
    sceKermitSendRequest661(packet, KERMIT_MODE_PERIPHERAL, cmd, argc, KERMIT_CALLBACK_DISABLE, &resp);
    return resp;
}

int flashLoadPatch(int cmd)
{
    int ret = kermit_flash_load(cmd);
    // Custom handling on loadFlash mode, else nothing
    if ( cmd == KERMIT_CMD_ERROR_EXIT || cmd == KERMIT_CMD_ERROR_EXIT_2 )
    {
        // Wait for flash to load
        sceKernelDelayThread(10000);

        // Load FLASH0.ARK
        char archive[ARK_PATH_SIZE];
        strcpy(archive, ark_config->arkpath);
        strcat(archive, FLASH0_ARK);
        
        int fd = sceIoOpen(archive, PSP_O_RDONLY, 0777);
        sceIoRead(fd, (void*)ARK_FLASH, MAX_FLASH0_SIZE);
        sceIoClose(fd);

        sctrlFlushCache();
    }
    return ret;
}

int patchKermitPeripheral()
{
    // Redirect KERMIT_CMD_ERROR_EXIT loadFlash function
    u32 swaddress = sctrlHENFindFirstJAL(sctrlHENFindFunction("sceKermitPeripheral_Driver", "sceKermitPeripheral_driver", 0x0648E1A3));
    REDIRECT_FUNCTION(swaddress, flashLoadPatch);
    return 0;
}

int (*_sceKernelVolatileMemTryLock)(int unk, void **ptr, int *size);
int sceKernelVolatileMemTryLockPatched(int unk, void **ptr, int *size) {
    int res = 0;

    int i;
    for (i = 0; i < 0x10; i++) {
        res = _sceKernelVolatileMemTryLock(unk, ptr, size);
        if (res >= 0)
        	break;

        sceKernelDelayThread(100);
    }

    return res;
}

static u8 get_pscode_from_region(int region)
{
    u8 code;

    code = region;
    
    if(code < 12) {
        code += 2;
    } else {
        code -= 11;
    }

    if(code == 2) {
        code = 3;
    }

    return code;
}

int (* _sceChkregGetPsCode)(u8 *pscode);
int sceChkregGetPsCodePatched(u8 *pscode) {
    int res = _sceChkregGetPsCode(pscode);

    pscode[0] = 0x01;
    pscode[1] = 0x00;
    pscode[3] = 0x00;
    pscode[4] = 0x01;
    pscode[5] = 0x00;
    pscode[6] = 0x01;
    pscode[7] = 0x00;

    if (se_config->vshregion)
        pscode[2] = get_pscode_from_region(se_config->vshregion);

    return res;
}

int sceUmdRegisterUMDCallBackPatched(int cbid) {
    int k1 = pspSdkSetK1(0);
    int res = sceKernelNotifyCallback(cbid, PSP_UMD_NOT_PRESENT);
    pspSdkSetK1(k1);
    return res;
}

int (* sceMeAudio_driver_C300D466)(int codec, int unk, void *info);
int sceMeAudio_driver_C300D466_Patched(int codec, int unk, void *info) {
    int res = sceMeAudio_driver_C300D466(codec, unk, info);

    if (res < 0 && codec == 0x1002 && unk == 2)
        return 0;

    return res;
}

int memcmp_patched(const void *b1, const void *b2, size_t len) {
    u32 tag = 0x4C9494F0;

    if (memcmp(&tag, b2, len) == 0) {
        static u8 kernel661_keys[0x10] = { 0x76, 0xF2, 0x6C, 0x0A, 0xCA, 0x3A, 0xBA, 0x4E, 0xAC, 0x76, 0xD2, 0x40, 0xF5, 0xC3, 0xBF, 0xF9 };
        memcpy((void *)0xBFC00220, kernel661_keys, sizeof(kernel661_keys));
        return 0;
    }

    return memcmp(b1, b2, len);
}

void PatchMemlmd() {
    SceModule2 *mod = (SceModule2*)sceKernelFindModuleByName("sceMemlmd");
    u32 text_addr = mod->text_addr;

    // Allow 6.61 kernel modules
    MAKE_CALL(text_addr + 0x2C8, memcmp_patched);
    
    sctrlFlushCache();
}

int ReadFile(char *file, void *buf, int size) {
    SceUID fd = sceIoOpen(file, PSP_O_RDONLY, 0);
    if (fd < 0)
        return fd;

    int read = sceIoRead(fd, buf, size);

    sceIoClose(fd);
    return read;
}

int sceResmgrDecryptIndexPatched(void *buf, int size, int *retSize) {
    int k1 = pspSdkSetK1(0);
    *retSize = ReadFile("flash0:/vsh/etc/version.txt", buf, size);
    pspSdkSetK1(k1);
    return 0;
}

int sceKernelSuspendThreadPatched(SceUID thid) {
    SceKernelThreadInfo info;
    info.size = sizeof(SceKernelThreadInfo);
    if (sceKernelReferThreadStatus(thid, &info) == 0) {
        if (strcmp(info.name, "popsmain") == 0) {
        	SendAdrenalineCmd(ADRENALINE_VITA_CMD_PAUSE_POPS);
        }
    }

    return sceKernelSuspendThread(thid);
}

int sceKernelResumeThreadPatched(SceUID thid) {
    SceKernelThreadInfo info;
    info.size = sizeof(SceKernelThreadInfo);
    if (sceKernelReferThreadStatus(thid, &info) == 0) {
        if (strcmp(info.name, "popsmain") == 0) {
        	SendAdrenalineCmd(ADRENALINE_VITA_CMD_RESUME_POPS);
        }
    }

    return sceKernelResumeThread(thid);
}

void patch_GameBoot(SceModule2* mod){
    u32 p1 = 0;
    u32 p2 = 0;
    int patches = 2;
    for (u32 addr=mod->text_addr; addr<mod->text_addr+mod->text_size && patches; addr+=4){
        u32 data = _lw(addr);
        if (data == 0x2C43000D){
            p1 = addr-36;
            patches--;
        }
        else if (data == 0x27BDFF20 && _lw(addr-4) == 0x27BD0040){
            p2 = addr-24;
            patches--;
        }
    }
    _sw(JAL(p1), p2);
    _sw(0x24040002, p2 + 4);
}

int sctrlStartUsb() {
    return SendAdrenalineCmd(ADRENALINE_VITA_CMD_START_USB);
}

int sctrlStopUsb() {
    return SendAdrenalineCmd(ADRENALINE_VITA_CMD_STOP_USB);
}

int sctrlGetUsbState() {
    int state = SendAdrenalineCmd(ADRENALINE_VITA_CMD_GET_USB_STATE);
    if (state & 0x20)
        return 1; // Connected

    return 2; // Not connected
}

void patch_SysconfPlugin(SceModule2* mod){
    u32 text_addr = mod->text_addr;
    // Dummy all vshbridge usbstor functions
    _sw(0x24020001, text_addr + 0xCD78); // sceVshBridge_ED978848 - vshUsbstorMsSetWorkBuf
    _sw(0x00001021, text_addr + 0xCDAC); // sceVshBridge_EE59B2B7
    _sw(0x00001021, text_addr + 0xCF0C); // sceVshBridge_6032E5EE - vshUsbstorMsSetProductInfo
    _sw(0x00001021, text_addr + 0xD218); // sceVshBridge_360752BF - vshUsbstorMsSetVSHInfo

    // Dummy LoadUsbModules, UnloadUsbModules
    MAKE_DUMMY_FUNCTION_RETURN_0(text_addr + 0xCC70);
    MAKE_DUMMY_FUNCTION_RETURN_0(text_addr + 0xD2C4);

    // Redirect USB functions
    REDIRECT_SYSCALL(mod->text_addr + 0xAE9C, sctrlStartUsb);
    REDIRECT_SYSCALL(mod->text_addr + 0xAFF4, sctrlStopUsb);
    REDIRECT_SYSCALL(mod->text_addr + 0xB4A0, sctrlGetUsbState);

    // Ignore wait thread end failure
    _sw(0, text_addr + 0xB264);
}

void patchPopsMan(SceModule2* mod){
    u32 text_addr = mod->text_addr;

    // Use different mode for SceKermitPocs
    _sw(0x2405000E, text_addr + 0x2030);
    _sw(0x2405000E, text_addr + 0x20F0);
    _sw(0x2405000E, text_addr + 0x21A0);

    // Use different pops register location
    _sw(0x3C014BCD, text_addr + 0x11B4);
}

void patchPops(SceModule2* mod){
    // Use different pops register location
    u32 i;
    for (i = 0; i < mod->text_size; i += 4) {
        if ((_lw(mod->text_addr+i) & 0xFFE0FFFF) == 0x3C0049FE) {
        	_sh(0x4BCD, mod->text_addr+i);
        }
    }
}

void exit_game_patched(){
    sctrlSESetBootConfFileIndex(MODE_UMD);
    if (se_config->launcher_mode)
        sctrlArkExitLauncher();
    else
        sctrlKernelExitVSH(NULL);
}

int AdrenalineOnModuleStart(SceModule2 * mod){

    // System fully booted Status
    static int booted = 0;
    
    // Patch Kermit Peripheral Module to load flash0
    if(strcmp(mod->modname, "sceKermitPeripheral_Driver") == 0)
    {
        patchKermitPeripheral();
        goto flush;
    }

    if (strcmp(mod->modname, "sceLowIO_Driver") == 0) {

        // Protect pops memory
        if (sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_POPS) {
        	sceKernelAllocPartitionMemory(6, "", PSP_SMEM_Addr, 0x80000, (void *)0x09F40000);
        	memset((void *)0x49F40000, 0, 0x80000);
        }

        memset((void *)0xABCD0000, 0, 0x1B0);

        PatchLowIODriver2(mod->text_addr);
        goto flush;
    }

    if (strcmp(mod->modname, "sceLoadExec") == 0) {
        PatchLoadExec(mod->text_addr, mod->text_size);
        // fix exitgame
        REDIRECT_FUNCTION(sctrlHENFindFunction(mod->modname, "LoadExecForUser", 0x05572A5F), exit_game_patched);
        REDIRECT_FUNCTION(sctrlHENFindFunction(mod->modname, "LoadExecForUser", 0x2AC9954B), exit_game_patched);
        goto flush;
    }

    if (strcmp(mod->modname, "scePower_Service") == 0) {
        PatchPowerService(mod->text_addr);
        PatchPowerService2(mod->text_addr);
        goto flush;
    }
    
    if (strcmp(mod->modname, "sceChkreg") == 0) {
        MAKE_DUMMY_FUNCTION(sctrlHENFindFunction("sceChkreg", "sceChkreg_driver", 0x54495B19), 1);
        HIJACK_FUNCTION(sctrlHENFindFunction("sceChkreg", "sceChkreg_driver", 0x59F8491D), sceChkregGetPsCodePatched, _sceChkregGetPsCode);
        goto flush;
    }

    if (strcmp(mod->modname, "sceMesgLed") == 0) {
        REDIRECT_FUNCTION(sctrlHENFindFunction("sceMesgLed", "sceResmgr_driver", 0x9DC14891), sceResmgrDecryptIndexPatched);
        goto flush;
    }

    if (strcmp(mod->modname, "sceUmd_driver") == 0) {
        REDIRECT_FUNCTION(mod->text_addr + 0xC80, sceUmdRegisterUMDCallBackPatched);
        goto flush;
    }

    if(strcmp(mod->modname, "sceMeCodecWrapper") == 0) {
        HIJACK_FUNCTION(sctrlHENFindFunction(mod->modname, "sceMeAudio_driver", 0xC300D466), sceMeAudio_driver_C300D466_Patched, sceMeAudio_driver_C300D466);
        goto flush;
    }

    if (strcmp(mod->modname, "sceUtility_Driver") == 0) {
        PatchUtility();
        goto flush;
    }

    if (strcmp(mod->modname, "sceUSBCam_Driver") == 0) {
        patchUsbCam(mod);
        goto flush;
    }

    if (strcmp(mod->modname, "sceImpose_Driver") == 0) {
        PatchImposeDriver(mod->text_addr);
        // configure inferno cache
        se_config->iso_cache_size_kb = 32;
        se_config->iso_cache_num = 32;
        se_config->iso_cache_partition = (se_config->force_high_memory)? 2:11;
        goto flush;
    }

    if(strcmp(mod->modname, "game_plugin_module") == 0) {
        if (se_config->skiplogos == 1 || se_config->skiplogos == 2) {
            patch_GameBoot(mod);
        }
        goto flush;
    }

    if (strcmp(mod->modname, "sysconf_plugin_module") == 0){
        patch_SysconfPlugin(mod);
        goto flush;
    }

    if (strcmp(mod->modname, "sceSAScore") == 0) {
        PatchSasCore();
        goto flush;
    }

    if (strcmp(mod->modname, "scePops_Manager") == 0){
        patchPopsMan(mod);
        goto flush;
    }

    if (strcmp(mod->modname, "pops") == 0){
        patchPops(mod);
        goto flush;
    }

    if (strcmp(mod->modname, "CWCHEATPRX") == 0) {
        if (sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_POPS) {
        	sctrlHookImportByNID(mod, "ThreadManForKernel", 0x9944F31F, sceKernelSuspendThreadPatched);
        	sctrlHookImportByNID(mod, "ThreadManForKernel", 0x75156E8F, sceKernelResumeThreadPatched);
        	goto flush;
        }
    }

    // VLF Module Patches
    if(strcmp(mod->modname, "VLF_Module") == 0)
    {
        // Patch VLF Module
        patchVLF(mod);
        // Exit Handler
        goto flush;
    }
       
    // Boot Complete Action not done yet
    if(booted == 0)
    {
        // Boot is complete
        if(isSystemBooted())
        {
            // Initialize Memory Stick Speedup Cache
            if (se_config->msspeed)
        		msstorCacheInit("ms");

            // patch bug in ePSP volatile mem
            _sceKernelVolatileMemTryLock = (void *)sctrlHENFindFunction("sceSystemMemoryManager", "sceSuspendForUser", 0xA14F40B2);
            sctrlHENPatchSyscall(_sceKernelVolatileMemTryLock, sceKernelVolatileMemTryLockPatched);

        	// Adrenaline patches
        	OnSystemStatusIdle();

            // Boot Complete Action done
            booted = 1;
        }
    }

flush:
    sctrlFlushCache();

    // Forward to previous Handler
    if(previous) return previous(mod);
    return 0;
}

void initAdrenalineSysPatch(){

    // Patch stuff
    patchLoaderCore();
    PatchIoFileMgr();
    PatchMemlmd();

    // initialize Adrenaline Layer
    initAdrenaline();

    // Register Module Start Handler
    previous = sctrlHENSetStartModuleHandler(AdrenalineOnModuleStart);

    // Implement extra memory unlock
    HIJACK_FUNCTION(K_EXTRACT_IMPORT(sctrlHENApplyMemory), memoryHandlerVita, _sctrlHENApplyMemory);
}
