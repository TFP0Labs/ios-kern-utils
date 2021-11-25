/*
 * libkern.c - Everything that touches the kernel.
 *
 * Copyright (c) 2014 Samuel Groß
 * Copyright (c) 2016-2017 Siguza
 */

#include <dlfcn.h>              // RTLD_*, dl*
#include <limits.h>             // UINT_MAX
#include <stdio.h>              // fprintf, snprintf
#include <stdlib.h>             // free, malloc, random, srandom
#include <string.h>             // memmem
#include <time.h>               // time

#include <mach/mach.h>          // Everything mach
#include <mach-o/loader.h>      // MH_EXECUTE
#include <mach-o/nlist.h>       // struct nlist_64
#include <sys/mman.h>           // mmap, munmap, MAP_FAILED
#include <sys/stat.h>           // fstat, struct stat
#include <sys/syscall.h>        // syscall

#include "arch.h"               // TARGET_MACOS, IMAGE_OFFSET, MACH_TYPE, MACH_HEADER_MAGIC, mach_hdr_t
#include "debug.h"              // DEBUG
#include "mach-o.h"             // CMD_ITERATE

#include "libkern.h"

#define MAX_CHUNK_SIZE 0xFFF /* MIG limitation */
#define SYS_MAX                                 530
#define ALIGNTO(addr,align) ((addr+align-1)&~(align-1))
// https://opensource.apple.com/source/xnu/xnu-3789.51.2/osfmk/mach/vm_statistics.h.auto.html
#define VM_KERNEL_LINK_ADDRESS (0xFFFFFFF007004000ULL)
#define VM_KERN_MEMORY_CPU (9)
#ifdef __arm64e__
# define CPU_DATA_RTCLOCK_DATAP_OFF (0x190)
#else
# define CPU_DATA_RTCLOCK_DATAP_OFF (0x198)
#endif

kern_return_t mach_vm_region(vm_map_t map, mach_vm_address_t* address, mach_vm_size_t* size, vm_region_flavor_t flavor, vm_region_info_t info,
    mach_msg_type_number_t* count, mach_port_t* object_name);

// Only support for arm64 iOS11 and later
kern_return_t get_kernel_task(task_t* ptask) 
{
    static bool init = false;
    static task_t tfp0 = MACH_PORT_NULL;
    if (!init) {
        kern_return_t ret = task_for_pid(mach_task_self(), 0, &tfp0);
        if(ret != KERN_SUCCESS) {
            host_get_special_port(mach_host_self(), HOST_LOCAL_NODE, 4, &tfp0);
        }
        if(MACH_PORT_VALID(tfp0)) {
            pid_t pid;
            if(pid_for_task(tfp0, &pid) != KERN_SUCCESS || pid != 0) {
                tfp0 = MACH_PORT_NULL;
            }
        } else {
            tfp0 = MACH_PORT_NULL;
        }
        init = true;
    }
    *ptask = tfp0;
    if (tfp0 == MACH_PORT_NULL) {
        return KERN_FAILURE;
    }
    return KERN_SUCCESS;
}

typedef uint64_t kaddr_t;

vm_address_t get_kernel_base(void)
{
    task_t tfp0;
    kern_return_t ret = get_kernel_task(&tfp0);
    if(ret != KERN_SUCCESS)
    {
        return 0;
    }
    mach_msg_type_number_t cnt = TASK_DYLD_INFO_COUNT;
    vm_region_extended_info_data_t extended_info;
    task_dyld_info_data_t dyld_info;
    kaddr_t addr, rtclock_datap;
    struct mach_header_64 mh64;
    mach_port_t obj_nm;
    mach_vm_size_t sz;
    if(task_info(tfp0, TASK_DYLD_INFO, (task_info_t)&dyld_info, &cnt) == KERN_SUCCESS && dyld_info.all_image_info_size != 0) {
        kaddr_t kslide = dyld_info.all_image_info_size;
        return VM_KERNEL_LINK_ADDRESS + kslide;
    }
    cnt = VM_REGION_EXTENDED_INFO_COUNT;
    for(addr = 0; mach_vm_region(tfp0, &addr, &sz, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&extended_info, &cnt, &obj_nm) == KERN_SUCCESS; addr += sz) {
        mach_port_deallocate(mach_task_self(), obj_nm);
        if(extended_info.user_tag == VM_KERN_MEMORY_CPU && extended_info.protection == VM_PROT_DEFAULT) {
            if(kernel_read(addr + CPU_DATA_RTCLOCK_DATAP_OFF, sizeof(rtclock_datap), &rtclock_datap) != KERN_SUCCESS) {
                break;
            }
            rtclock_datap = trunc_page_kernel(rtclock_datap);
            do {
                if(rtclock_datap <= VM_KERNEL_LINK_ADDRESS) {
                    return 0;
                }
                rtclock_datap -= vm_kernel_page_size;
                if(kernel_read(rtclock_datap, sizeof(mh64), &mh64) != KERN_SUCCESS) {
                    return 0;
                }
            } while(mh64.magic != MH_MAGIC_64 || mh64.cputype != CPU_TYPE_ARM64 || mh64.filetype != MH_EXECUTE);
            return rtclock_datap;
        }
    }
    return 0;
}

vm_size_t kernel_read(vm_address_t addr, vm_size_t size, void *buf)
{
    DEBUG("Reading kernel bytes " ADDR "-" ADDR, addr, addr + size);
    kern_return_t ret;
    task_t kernel_task;
    vm_size_t remainder = size,
              bytes_read = 0;

    ret = get_kernel_task(&kernel_task);
    if(ret != KERN_SUCCESS)
    {
        return -1;
    }

    // The vm_* APIs are part of the mach_vm subsystem, which is a MIG thing
    // and therefore has a hard limit of 0x1000 bytes that it accepts. Due to
    // this, we have to do both reading and writing in chunks smaller than that.
    for(vm_address_t end = addr + size; addr < end; remainder -= size)
    {
        size = remainder > MAX_CHUNK_SIZE ? MAX_CHUNK_SIZE : remainder;
        ret = vm_read_overwrite(kernel_task, addr, size, (vm_address_t)&((char*)buf)[bytes_read], &size);
        if(ret != KERN_SUCCESS || size == 0)
        {
            DEBUG("vm_read error: %s", mach_error_string(ret));
            break;
        }
        bytes_read += size;
        addr += size;
    }

    return bytes_read;
}

vm_size_t kernel_write(vm_address_t addr, vm_size_t size, void *buf)
{
    DEBUG("Writing to kernel at " ADDR "-" ADDR, addr, addr + size);
    kern_return_t ret;
    task_t kernel_task;
    vm_size_t remainder = size,
              bytes_written = 0;

    ret = get_kernel_task(&kernel_task);
    if(ret != KERN_SUCCESS)
    {
        return -1;
    }

    for(vm_address_t end = addr + size; addr < end; remainder -= size)
    {
        size = remainder > MAX_CHUNK_SIZE ? MAX_CHUNK_SIZE : remainder;
        ret = vm_write(kernel_task, addr, (vm_offset_t)&((char*)buf)[bytes_written], size);
        if(ret != KERN_SUCCESS)
        {
            DEBUG("vm_write error: %s", mach_error_string(ret));
            break;
        }
        bytes_written += size;
        addr += size;
    }

    return bytes_written;
}

vm_address_t kernel_find(vm_address_t addr, vm_size_t len, void *buf, size_t size)
{
    vm_address_t ret = 0;
    unsigned char* b = malloc(len);
    if(b)
    {
        // TODO reading in chunks would probably be better
        if(kernel_read(addr, len, b))
        {
            void *ptr = memmem(b, len, buf, size);
            if(ptr)
            {
                ret = addr + ((char*)ptr - (char*)b);
            }
        }
        free(b);
    }
    return ret;
}
