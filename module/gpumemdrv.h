

#ifndef GPUMEM_H
#define GPUMEM_H

//-----------------------------------------------------------------------------

#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/semaphore.h>

#include "nv-p2p.h"

//-----------------------------------------------------------------------------

struct gpumem_t {
    struct list_head list;          // needed for gpumem_t to be part of table_list
    u64 virt_start;                 // start page address of the virtual memory
    nvidia_p2p_page_table_t *page_table;
};

//-----------------------------------------------------------------------------

struct gpumem {
    struct proc_dir_entry *proc;
    struct list_head table_list;    // list of gpumem_t entries
};

//-----------------------------------------------------------------------------

int get_nv_page_size(int val);

//-----------------------------------------------------------------------------

#endif
