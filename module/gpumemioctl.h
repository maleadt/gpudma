
#ifndef __GPUDMAIOTCL_H__
#define __GPUDMAIOTCL_H__

//-----------------------------------------------------------------------------

#define GPUMEM_DRIVER_NAME "gpumem"

//-----------------------------------------------------------------------------

#ifdef __linux__
#include <linux/types.h>
#ifndef __KERNEL__
#include <sys/ioctl.h>
#endif
#define GPUMEM_DEVICE_TYPE   'g'
#define GPUMEM_MAKE_IOCTL(c) _IO(GPUMEM_DEVICE_TYPE, (c))
#endif

#define IOCTL_GPUMEM_LOCK   GPUMEM_MAKE_IOCTL(10)
#define IOCTL_GPUMEM_UNLOCK GPUMEM_MAKE_IOCTL(11)
#define IOCTL_GPUMEM_STATE  GPUMEM_MAKE_IOCTL(12)

//-----------------------------------------------------------------------------
// for boundary alignment requirement
#define GPU_BOUND_SHIFT  16
#define GPU_BOUND_SIZE   ((u64)1 << GPU_BOUND_SHIFT)
#define GPU_BOUND_OFFSET (GPU_BOUND_SIZE - 1)
#define GPU_BOUND_MASK   (~GPU_BOUND_OFFSET)

//-----------------------------------------------------------------------------

struct gpudma_lock_t {
    void *handle;       // output: handle to this mapping for subsequent ioctls
    uint64_t addr;      // input: virtual GPU address
    uint64_t size;      // input: size of GPU buffer
    size_t page_count;  // output: number of pages mapped to physical memory
};

//-----------------------------------------------------------------------------

struct gpudma_unlock_t {
    void *handle;       // input: the handle for a mapping
};

//-----------------------------------------------------------------------------

struct gpudma_state_t { // variable-size struct; number of pages should match gpudma_lock_t
    void *handle;       // input: the handle for a mapping
    size_t page_count;  // input & output: number of pages mapped to physical memory
    size_t page_size;   // output: size of each page
    uint64_t pages[1];  // output: list of pages
};

//-----------------------------------------------------------------------------


#endif //_GPUDMAIOTCL_H_
