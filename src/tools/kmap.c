/*
 * kmap.c - Display a listing of the kernel memory mappings
 *
 * Copyright (c) 2014 Samuel Groß
 * Copyright (c) 2016-2017 Siguza
 */

#include <limits.h>             // UINT_MAX
#include <stdbool.h>            // bool, true, false
#include <stdio.h>              // printf, fprintf, stderr

#include <mach/kern_return.h>   // KERN_SUCCESS, kern_return_t
#include <mach/mach_types.h>    // task_t
#include <mach/message.h>       // mach_msg_type_number_t
#include <mach/vm_inherit.h>    // VM_INHERIT_*
#include <mach/vm_map.h>        // vm_region_recurse_64
#include <mach/vm_prot.h>       // VM_PROT_READ, VM_PROT_WRITE, VM_PROT_EXECUTE
#include <mach/vm_region.h>     // VM_REGION_SUBMAP_INFO_COUNT_64, vm_region_info_t, vm_region_submap_info_data_64_t
#include <mach/vm_types.h>      // vm_address_t, vm_size_t

#include "arch.h"               // ADDR
#include "debug.h"              // slow, verbose
#include "libkern.h"            // get_kernel_task

#define VM_KERN_MEMORY_NONE             0
#define VM_KERN_MEMORY_OSFMK            1
#define VM_KERN_MEMORY_BSD              2
#define VM_KERN_MEMORY_BSD              2
#define VM_KERN_MEMORY_IOKIT            3
#define VM_KERN_MEMORY_LIBKERN          4
#define VM_KERN_MEMORY_OSKEXT           5
#define VM_KERN_MEMORY_KEXT             6
#define VM_KERN_MEMORY_IPC              7
#define VM_KERN_MEMORY_STACK            8
#define VM_KERN_MEMORY_CPU              9
#define VM_KERN_MEMORY_PMAP             10
#define VM_KERN_MEMORY_PTE              11
#define VM_KERN_MEMORY_ZONE             12
#define VM_KERN_MEMORY_KALLOC           13
#define VM_KERN_MEMORY_COMPRESSOR       14
#define VM_KERN_MEMORY_COMPRESSED_DATA  15
#define VM_KERN_MEMORY_PHANTOM_CACHE    16
#define VM_KERN_MEMORY_WAITQ            17
#define VM_KERN_MEMORY_DIAG             18
#define VM_KERN_MEMORY_LOG              19
#define VM_KERN_MEMORY_FILE             20
#define VM_KERN_MEMORY_MBUF             21
#define VM_KERN_MEMORY_UBC              22
#define VM_KERN_MEMORY_SECURITY         23
#define VM_KERN_MEMORY_MLOCK            24
#define VM_KERN_MEMORY_REASON           25
#define VM_KERN_MEMORY_SKYWALK          26
#define VM_KERN_MEMORY_LTABLE           27
#define VM_KERN_MEMORY_HV               28
#define VM_KERN_MEMORY_RETIRED          29

#define VM_MEMORY_MALLOC                    1
#define VM_MEMORY_MALLOC_SMALL              2
#define VM_MEMORY_MALLOC_LARGE              3
#define VM_MEMORY_MALLOC_HUGE               4
#define VM_MEMORY_SBRK                      5
#define VM_MEMORY_REALLOC                   6
#define VM_MEMORY_MALLOC_TINY               7
#define VM_MEMORY_MALLOC_LARGE_REUSABLE     8
#define VM_MEMORY_MALLOC_LARGE_REUSED       9
#define VM_MEMORY_ANALYSIS_TOOL             10
#define VM_MEMORY_MALLOC_NANO               11
#define VM_MEMORY_MALLOC_MEDIUM             12
#define VM_MEMORY_MALLOC_PGUARD             13
#define VM_MEMORY_MACH_MSG                  20
#define VM_MEMORY_IOKIT                     21
#define VM_MEMORY_STACK                     30
#define VM_MEMORY_GUARD                     31
#define VM_MEMORY_SHARED_PMAP               32
#define VM_MEMORY_DYLIB                     33
#define VM_MEMORY_OBJC_DISPATCHERS          34
#define VM_MEMORY_UNSHARED_PMAP             35
#define VM_MEMORY_APPKIT                    40
#define VM_MEMORY_FOUNDATION                41
#define VM_MEMORY_COREGRAPHICS              42
#define VM_MEMORY_CORESERVICES              43
#define VM_MEMORY_JAVA                      44
#define VM_MEMORY_COREDATA                  45
#define VM_MEMORY_COREDATA_OBJECTIDS        46
#define VM_MEMORY_ATS                       50
#define VM_MEMORY_LAYERKIT                  51
#define VM_MEMORY_CGIMAGE                   52
#define VM_MEMORY_TCMALLOC                  53
#define VM_MEMORY_COREGRAPHICS_DATA         54
#define VM_MEMORY_COREGRAPHICS_SHARED       55
#define VM_MEMORY_COREGRAPHICS_FRAMEBUFFERS 56
#define VM_MEMORY_COREGRAPHICS_BACKINGSTORES 57
#define VM_MEMORY_COREGRAPHICS_XALLOC       58
#define VM_MEMORY_DYLD                      60
#define VM_MEMORY_DYLD_MALLOC               61
#define VM_MEMORY_SQLITE                    62
#define VM_MEMORY_JAVASCRIPT_CORE           63
#define VM_MEMORY_JAVASCRIPT_JIT_EXECUTABLE_ALLOCATOR 64
#define VM_MEMORY_JAVASCRIPT_JIT_REGISTER_FILE 65
#define VM_MEMORY_GLSL                      66
#define VM_MEMORY_OPENCL                    67
#define VM_MEMORY_COREIMAGE                 68
#define VM_MEMORY_WEBCORE_PURGEABLE_BUFFERS 69
#define VM_MEMORY_IMAGEIO                   70
#define VM_MEMORY_COREPROFILE               71
#define VM_MEMORY_ASSETSD                   72
#define VM_MEMORY_OS_ALLOC_ONCE             73
#define VM_MEMORY_LIBDISPATCH               74
#define VM_MEMORY_ACCELERATE                75
#define VM_MEMORY_COREUI                    76
#define VM_MEMORY_COREUIFILE                77
#define VM_MEMORY_GENEALOGY                 78
#define VM_MEMORY_RAWCAMERA                 79
#define VM_MEMORY_CORPSEINFO                80
#define VM_MEMORY_ASL                       81
#define VM_MEMORY_SWIFT_RUNTIME             82
#define VM_MEMORY_SWIFT_METADATA            83
#define VM_MEMORY_DHMM                      84
#define VM_MEMORY_SCENEKIT                  86
#define VM_MEMORY_SKYWALK                   87
#define VM_MEMORY_IOSURFACE                 88
#define VM_MEMORY_LIBNETWORK                89
#define VM_MEMORY_AUDIO                     90
#define VM_MEMORY_VIDEOBITSTREAM            91
#define VM_MEMORY_CM_XPC                    92
#define VM_MEMORY_CM_RPC                    93
#define VM_MEMORY_CM_MEMORYPOOL             94
#define VM_MEMORY_CM_READCACHE              95
#define VM_MEMORY_CM_CRABS                  96
#define VM_MEMORY_QUICKLOOK_THUMBNAILS      97
#define VM_MEMORY_ACCOUNTS                  98
#define VM_MEMORY_SANITIZER                 99
#define VM_MEMORY_IOACCELERATOR             100
#define VM_MEMORY_CM_REGWARP                101
#define VM_MEMORY_EAR_DECODER               102
#define VM_MEMORY_COREUI_CACHED_IMAGE_DATA  103

static const char* kern_tag(uint32_t tag)
{
    switch(tag)
    {
        case   0: return "none/?";
        case   1: return "osfmk/malloc";
        case   2: return "bsd/malloc";
        case   3: return "iokit/malloc";
        case   4: return "libkern/malloc";
        case   5: return "oskext/sbrk";
        case   6: return "kext/realloc";
        case   7: return "ipc/malloc";
        case   8: return "stack/malloc";
        case   9: return "cpu/malloc";
        case  10: return "pmap/analysis";
        case  11: return "pte/malloc";
        case  12: return "zone/malloc";
        case  13: return "kalloc/malloc";
        case  14: return "compressor/?";
        case  15: return "compressed_data/?";
        case  16: return "phantom/?";
        case  17: return "waitq/?";
        case  18: return "diag/?";
        case  19: return "log/?";
        case  20: return "file/mach_msg";
        case  21: return "mbuf/iokit";
        case  22: return "ubc/?";
        case  23: return "security/?";
        case  24: return "mlock/?";
        case  25: return "reason/?";
        case  26: return "skywalk/?";
        case  27: return "ltable/?";
        case  28: return "hv/?";
        case  29: return "retired/?";
        case  30: return "?/stack";
        case  31: return "?/guard";
        case  32: return "?/shared_pmap";
        case  33: return "?/dylib";
        case  34: return "?/objc";
        case  35: return "?/unshared_pmap";
        case  40: return "?/appkit";
        case  41: return "?/foundation";
        case  42: return "?/coregraphics";
        case  43: return "?/coreservices";
        case  44: return "?/java"; 
        case  45: return "?/coredata";
        case  46: return "?/coredata";
        case  50: return "?/ats";
        case  51: return "?/layerkit";
        case  52: return "?/cgimage";
        case  53: return "?/tcmalloc";
        case  54: return "?/coregraphics";
        case  55: return "?/coregraphics";
        case  56: return "?/coregraphics";
        case  57: return "?/coregraphics";
        case  58: return "?/coregraphics";
        case  60: return "?/dyld";
        case  61: return "?/dyld_malloc";
        case  62: return "?/sqlite";
        case  63: return "?/javascript";
        case  64: return "?/javascript";
        case  65: return "?/javascript";
        case  66: return "?/glsl";
        case  67: return "?/opencl";
        case  68: return "?/coreimage";
        case  69: return "?/webcore";
        case  70: return "?/imageio";
        case  71: return "?/coreprofile";
        case  72: return "?/assetsd";
        case  73: return "?/os_alloc_once";
        case  74: return "?/libdispatch";
        case  75: return "?/accelerate";
        case  76: return "?/coreui";
        case  77: return "?/coreuifile";
        case  78: return "?/genealogy";
        case  79: return "?/rawcamera";
        case  80: return "?/corpseinfo";
        case  81: return "?/asl";
        case  82: return "?/swift";
        case  83: return "?/swift";
        case  84: return "?/dhmm";
        case  86: return "?/scenekit";
        case  87: return "?/skywalk";
        case  88: return "?/iosurface";
        case  89: return "?/libnetwork";
        case  90: return "?/audio";
        case  91: return "?/videobitstream";
        case  92: return "?/cm_xpc";
        case  93: return "?/cm_xpc";
        case  94: return "?/cm_memorypool";
        case  95: return "?/cm_readcache";
        case  96: return "?/cm_crabs";
        case  97: return "?/quicklook";
        case  98: return "?/accounts";
        case  99: return "?/sanitizer";
        case 100: return "?/ioaccelerator";
        case 101: return "?/cm_regwarp";
        case 102: return "?/ear_decoder";
        case 103: return "?/coreui";
    }
    if (tag >= 230 && tag < 240) {
        return "?/rosetta";
    }
    if (tag >= 249 && tag < 256) {
        return "?/application";
    }
    return 0;
}

static const char* share_mode(char mode)
{
    switch(mode)
    {
        case SM_COW:                return "cow";
        case SM_PRIVATE:            return "prv";
        case SM_EMPTY:              return "nul";
        case SM_SHARED:             return "shm";
        case SM_TRUESHARED:         return "tru";
        case SM_PRIVATE_ALIASED:    return "p/a";
        case SM_SHARED_ALIASED:     return "s/a";
        case SM_LARGE_PAGE:         return "big";
    }
    return "???";
}

static const char* inheritance(vm_inherit_t inh)
{
    switch(inh)
    {
        case VM_INHERIT_SHARE:          return "sh";
        case VM_INHERIT_COPY:           return "cp";
        case VM_INHERIT_NONE:           return "--";
        case VM_INHERIT_DONATE_COPY:    return "dn";
    }
    return "??";
}

static void print_usage(const char *self)
{
    fprintf(stderr, "Usage: %s [-h] [-v [-d]] [-e]\n"
                    "    -d  Debug mode (sleep between function calls, gives\n"
                    "        sshd time to deliver output before kernel panic)\n"
                    "    -e  Extended output (print all information available)\n"
                    "    -g  Show gaps between regions\n"
                    "    -h  Print this help\n"
                    "    -v  Verbose (debug output)\n"
                    , self);
}

static void print_range(task_t kernel_task, bool extended, bool gaps, unsigned int level, vm_address_t min, vm_address_t max)
{
    vm_region_submap_info_data_64_t info;
    vm_address_t last_addr = min;
    vm_size_t size, last_size;
    mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
    unsigned int depth;
    size_t displaysize, last_displaysize;
    char scale, last_scale;
    char curA, curR, curW, curX, maxA, maxR, maxW, maxX;

    for(vm_address_t addr = min; 1; addr += size)
    {
        // get next memory region
        depth = level;
        if(vm_region_recurse_64(kernel_task, &addr, &size, &depth, (vm_region_info_t)&info, &info_count) != KERN_SUCCESS)
        {
            break;
        }
        if(addr >= max)
        {
            addr = max;
        }

        if(gaps)
        {
            if(last_addr != 0)
            {
                last_size = addr - last_addr;
                if(last_size > 0)
                {
                    last_scale = 'K';
                    last_displaysize = last_size / 1024;
                    if(last_displaysize > 4096)
                    {
                        last_scale = 'M';
                        last_displaysize /= 1024;
                        if(last_displaysize > 4096)
                        {
                            last_scale = 'G';
                            last_displaysize /= 1024;
                        }
                    }
                    printf("%*s [%4zu%c]\n"
                           , (int)(4 * sizeof(void*) + 1 + (extended ? 4 : 0)), ""
                           , last_displaysize, last_scale
                    );
                }
            }
            last_addr = addr + size;
        }

        if(addr >= max)
        {
            break;
        }

        // size
        scale = 'K';
        displaysize = size / 1024;
        if(displaysize > 4096)
        {
            scale = 'M';
            displaysize /= 1024;
            if(displaysize > 4096)
            {
                scale = 'G';
                displaysize /= 1024;
            }
        }

        // protection
        curA = (info.protection) & ~(VM_PROT_ALL) ? '+' : '-';
        curR = (info.protection) & VM_PROT_READ ? 'r' : '-';
        curW = (info.protection) & VM_PROT_WRITE ? 'w' : '-';
        curX = (info.protection) & VM_PROT_EXECUTE ? 'x' : '-';
        maxA = (info.max_protection) & ~(VM_PROT_ALL) ? '+' : '-';
        maxR = (info.max_protection) & VM_PROT_READ ? 'r' : '-';
        maxW = (info.max_protection) & VM_PROT_WRITE ? 'w' : '-';
        maxX = (info.max_protection) & VM_PROT_EXECUTE ? 'x' : '-';

        if(extended)
        {
            if (kern_tag(info.user_tag) != 0) {
                printf("%*s" ADDR "-" ADDR "%*s" " [%4zu%c] %c%c%c%c/%c%c%c%c [%s %s %s] %016llx [%u %u %hu %hhu %hu] %08x/%08x:<%10u> %u,%u {%10u,%10u} %s\n"
                    , 4 * level, "", addr, addr+size, 4 * (1 - level), ""
                    , displaysize, scale
                    , curA, curR, curW, curX
                    , maxA, maxR, maxW, maxX
                    , info.is_submap ? "map" : depth > 0 ? "sub" : "mem", share_mode(info.share_mode), inheritance(info.inheritance), info.offset
                    , info.behavior, info.pages_reusable, info.user_wired_count, info.external_pager, info.shadow_depth // these should all be 0
                    , info.user_tag, info.object_id, info.ref_count
                    , info.pages_swapped_out, info.pages_shared_now_private, info.pages_resident, info.pages_dirtied
                    , kern_tag(info.user_tag)
                );
            } else {
                printf("%*s" ADDR "-" ADDR "%*s" " [%4zu%c] %c%c%c%c/%c%c%c%c [%s %s %s] %016llx [%u %u %hu %hhu %hu] %08x/%08x:<%10u> %u,%u {%10u,%10u} %d\n"
                    , 4 * level, "", addr, addr+size, 4 * (1 - level), ""
                    , displaysize, scale
                    , curA, curR, curW, curX
                    , maxA, maxR, maxW, maxX
                    , info.is_submap ? "map" : depth > 0 ? "sub" : "mem", share_mode(info.share_mode), inheritance(info.inheritance), info.offset
                    , info.behavior, info.pages_reusable, info.user_wired_count, info.external_pager, info.shadow_depth // these should all be 0
                    , info.user_tag, info.object_id, info.ref_count
                    , info.pages_swapped_out, info.pages_shared_now_private, info.pages_resident, info.pages_dirtied
                    , info.user_tag
                );
            }
        }
        else
        {
            printf(ADDR "-" ADDR " [%4zu%c] %c%c%c/%c%c%c\n"
                   , addr, addr + size, displaysize, scale
                   , curR, curW, curX, maxR, maxW, maxX);
        }

        if(info.is_submap)
        {
            print_range(kernel_task, extended, gaps, level + 1, addr, addr + size);
        }
    }
}

int main(int argc, const char **argv)
{
    bool extended = false,
         gaps     = false;

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        if(strcmp(argv[i], "-d") == 0)
        {
            slow = true;
        }
        else if(strcmp(argv[i], "-v") == 0)
        {
            verbose = true;
        }
        else if(strcmp(argv[i], "-e") == 0)
        {
            extended = true;
        }
        else if(strcmp(argv[i], "-g") == 0)
        {
            gaps = true;
        }
        else
        {
            fprintf(stderr, "[!] Unrecognized option: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }

    task_t kernel_task;
    KERNEL_TASK_OR_GTFO(kernel_task);

    print_range(kernel_task, extended, gaps, 0, 0, ~0);

    return 0;
}
