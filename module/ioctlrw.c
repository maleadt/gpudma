
#include <linux/kernel.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/pagemap.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <asm/io.h>

#include "gpumemdrv.h"
#include "gpumemioctl.h"

//-----------------------------------------------------------------------------

int get_nv_page_size(int val) {
    switch (val) {
    case NVIDIA_P2P_PAGE_SIZE_4KB:
        return 4 * 1024;
    case NVIDIA_P2P_PAGE_SIZE_64KB:
        return 64 * 1024;
    case NVIDIA_P2P_PAGE_SIZE_128KB:
        return 128 * 1024;
    }
    return 0;
}

//--------------------------------------------------------------------

void free_nvp_callback(void *data) {
    int res;
    struct gpumem_t *entry = (struct gpumem_t *)data;
    if (entry) {
        res = nvidia_p2p_free_page_table(entry->page_table);
        if (res == 0) {
            printk(KERN_ERR "%s(): nvidia_p2p_free_page_table() - OK!\n", __FUNCTION__);
            // entry->virt_start = 0ULL;
            // entry->page_table = 0;
        } else {
            printk(KERN_ERR "%s(): Error in nvidia_p2p_free_page_table()\n", __FUNCTION__);
        }
    }
}

//-----------------------------------------------------------------------------

// map a virtual GPU address to physical memory for use with a third-party DMA
int ioctl_mem_lock(struct gpumem *drv, unsigned long arg) {
    int error = 0;
    size_t pin_size = 0ULL;
    struct gpumem_t *entry = 0;
    struct gpudma_lock_t param;

    // read the ioctl argument from userspace
    if (copy_from_user(&param, (void *)arg, sizeof(struct gpudma_lock_t))) {
        printk(KERN_ERR "%s(): Error in copy_from_user()\n", __FUNCTION__);
        error = -EFAULT;
        goto do_exit;
    }

    // allocate kernel memory
    entry = (struct gpumem_t *)kzalloc(sizeof(struct gpumem_t), GFP_KERNEL);
    if (!entry) {
        printk(KERN_ERR "%s(): Error allocate memory to mapping struct\n", __FUNCTION__);
        error = -ENOMEM;
        goto do_exit;
    }

    // alignment as required by the NVIDIA driver
    entry->virt_start = (param.addr & GPU_BOUND_MASK);
    pin_size = param.addr + param.size - entry->virt_start;
    if (!pin_size) {
        printk(KERN_ERR "%s(): Error invalid memory size!\n", __FUNCTION__);
        error = -EINVAL;
        goto do_free_mem;
    }

    // make the virtual memory accessible to other devices
    error = nvidia_p2p_get_pages(0, 0, entry->virt_start, pin_size, &entry->page_table,
                                 free_nvp_callback, entry);
    if (error != 0) {
        printk(KERN_ERR "%s(): Error in nvidia_p2p_get_pages()\n", __FUNCTION__);
        error = -EINVAL;
        goto do_free_mem;
    }
    // write the results back to userspace
    param.handle = entry;
    param.page_count = entry->page_table->entries;
    if (copy_to_user((void *)arg, &param, sizeof(struct gpudma_lock_t))) {
        printk(KERN_ERR "%s(): Error in copy_from_user()\n", __FUNCTION__);
        error = -EFAULT;
        goto do_unlock_pages;
    }

    // keep track of this mapping in the module
    list_add_tail(&entry->list, &drv->table_list);
    printk(KERN_ERR "%s(): Add new entry. handle: %p\n", __FUNCTION__, entry);

    return 0;

do_unlock_pages:
    nvidia_p2p_put_pages(0, 0, entry->virt_start, entry->page_table);
do_free_mem:
    kfree(entry);
do_exit:
    return error;
}

//-----------------------------------------------------------------------------

// clean-up a mapping created with ioctl_mem_lock()
int ioctl_mem_unlock(struct gpumem *drv, unsigned long arg) {
    int error = -EINVAL;
    struct gpumem_t *entry = 0;
    struct gpudma_unlock_t param;
    struct list_head *pos, *n;

    // read the ioctl argument from userspace
    if (copy_from_user(&param, (void *)arg, sizeof(struct gpudma_unlock_t))) {
        printk(KERN_ERR "%s(): Error in copy_from_user()\n", __FUNCTION__);
        error = -EFAULT;
        goto do_exit;
    }

    // find the mapping in our list (safer than just using `param.handle` as-is)
    list_for_each_safe(pos, n, &drv->table_list) {
        entry = list_entry(pos, struct gpumem_t, list);
        if (entry == param.handle) {
            printk(KERN_ERR "%s(): entry = %p\n", __FUNCTION__, entry);

            // unmap the memory
            error =
                nvidia_p2p_put_pages(0, 0, entry->virt_start, entry->page_table);
            if (error != 0) {
                printk(KERN_ERR "%s(): Error in nvidia_p2p_put_pages()\n",
                        __FUNCTION__);
                goto do_exit;
            }
            printk(KERN_ERR "%s(): nvidia_p2p_put_pages() - Ok!\n", __FUNCTION__);

            list_del(pos);
            kfree(entry);
            break;
        } else {
            printk(KERN_ERR "%s(): Skip entry: %p\n", __FUNCTION__, entry);
        }
    }

do_exit:
    return error;
}

//-----------------------------------------------------------------------------

// fetch the actual physical addresses from a mapping
//
// this usually wouldn't be exposed to userspace. instead, the device driver
// being modified to add GPUDirect support would use these addresses to DMA to.
int ioctl_mem_state(struct gpumem *drv, unsigned long arg) {
    int error = 0;
    int size = 0;
    int i = 0;
    struct gpumem_t *entry = 0;
    struct gpudma_state_t header;
    struct gpudma_state_t *param;
    struct list_head *pos, *n;

    // read the ioctl argument from userspace
    if (copy_from_user(&header, (void *)arg, sizeof(struct gpudma_state_t))) {
        printk(KERN_ERR "%s(): Error in copy_from_user()\n", __FUNCTION__);
        error = -EFAULT;
        goto do_exit;
    }

    // find the mapping in our list (safer than just using `param.handle` as-is)
    list_for_each_safe(pos, n, &drv->table_list) {
        entry = list_entry(pos, struct gpumem_t, list);
        if (entry == header.handle) {
            printk(KERN_ERR "%s(): entry = %p\n", __FUNCTION__, entry);

            if (!entry->page_table) {
                printk(KERN_ERR "%s(): Error - memory not pinned!\n", __FUNCTION__);
                return -EINVAL;
            }

            if (entry->page_table->entries != header.page_count) {
                printk(KERN_ERR "%s(): Error - page counters invalid!\n",
                        __FUNCTION__);
                return -EINVAL;
            }

            // allocate kernel memory to store the results in
            // (gpudma_state_t is variable-size, so we can't stack-allocate)
            size =
                (sizeof(uint64_t) * header.page_count) + sizeof(struct gpudma_state_t);
            param = kzalloc(size, GFP_KERNEL);
            if (!param) {
                printk(KERN_ERR "%s(): Error allocate memory!\n", __FUNCTION__);
                return -ENOMEM;
            }

            // write the physical memory of each page in the output buffer
            for (i = 0; i < entry->page_table->entries; i++) {
                struct nvidia_p2p_page *nvp = entry->page_table->pages[i];
                if (nvp) {
                    param->pages[i] = nvp->physical_address;
                    param->page_count++;
                    printk(KERN_ERR "%s(): %02d - 0x%llx\n", __FUNCTION__, i,
                            param->pages[i]);
                }
            }
            printk(KERN_ERR "%s(): page_count = %ld\n", __FUNCTION__,
                    (long int)param->page_count);

            // set and return the rest of the results to userspace
            param->page_size = get_nv_page_size(entry->page_table->page_size);
            param->handle = header.handle;
            if (copy_to_user((void *)arg, param, size)) {
                printk(KERN_DEBUG "%s(): Error in copy_to_user()\n", __FUNCTION__);
                error = -EFAULT;
            }

            kfree(param);
        } else {
            printk(KERN_ERR "%s(): Skip entry: %p\n", __FUNCTION__, entry);
        }
    }

do_exit:
    return error;
}

//-----------------------------------------------------------------------------
