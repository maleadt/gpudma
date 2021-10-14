using CUDA

using Base: Filesystem, JL_O_RDWR
using Printf
using Mmap


## ioctl argument structs and helper functions

mutable struct gpudma_lock
    handle::Ptr{Cvoid}
    addr::UInt64
    size::UInt64
    page_count::Csize_t
end
gpudma_lock(addr, size) = gpudma_lock(C_NULL, addr, size, 0)

ioctl_mem_lock(fd, arg::gpudma_lock) =
    ccall(:ioctl, Cint, (Cint, Cint, Ref{gpudma_lock}), fd, IOCTL_GPUMEM_LOCK, arg)

mutable struct gpudma_unlock
    handle::Ptr{Cvoid}
end

ioctl_mem_unlock(fd, arg::gpudma_unlock) =
    ccall(:ioctl, Cint, (Cint, Cint, Ref{gpudma_unlock}), fd, IOCTL_GPUMEM_UNLOCK, arg)

mutable struct gpudma_state{N}
    handle::Ptr{Cvoid}
    page_count::Csize_t
    page_size::Csize_t
    pages::NTuple{N,UInt64}
end
gpudma_state(handle, page_count) =
    gpudma_state{Int(page_count)}(handle, page_count, 0, ntuple(i->0, page_count))

ioctl_mem_state(fd, arg::gpudma_state{N}) where N =
    ccall(:ioctl, Cint, (Cint, Cint, Ref{gpudma_state{N}}), fd, IOCTL_GPUMEM_STATE, arg)


## main application

# TODO: implement IO(), see e.g.
#       https://github.com/vpelletier/python-ioctl-opt/blob/master/ioctl_opt/__init__.py
const IOCTL_GPUMEM_LOCK = 26378
const IOCTL_GPUMEM_UNLOCK = 26379
const IOCTL_GPUMEM_STATE = 26380

const GPUMEM_DRIVER_NAME = "gpumem"

function main()
    # open the driver
    file = Filesystem.open("/dev/$GPUMEM_DRIVER_NAME", JL_O_RDWR, 0)
    file_fd = reinterpret(Cint, fd(file))

    println("Total devices: ", length(devices()))

    println("Select device: $(CUDA.name(device()))")

    println("Compute capability: $(capability(device()))")

    global_mem = CUDA.total_memory()
    println("Global memory: $(Base.format_bytes(global_mem))")
    if global_mem > 4 * 1024 * 1024 * 1024
        println("64-bit Memory Address support")
    end

    # allocate GPU memory
    sz = 2^20
    #data = CuArray{UInt8}(undef, sz)
    # XXX: doesn't seem to work with our asynchronously-allocated memory?
    buf = Mem.alloc(Mem.Device, sz; async=false)
    dptr = convert(CuPtr{UInt8}, buf)
    data = unsafe_wrap(CuArray{UInt8}, dptr, (sz,))

    @printf("Allocate memory address: %p\n", dptr)

    attribute!(dptr, CUDA.POINTER_ATTRIBUTE_SYNC_MEMOPS, Cuint(1))

    println("Going to lock")

    # pin this memory, making it accessible to third-party DMA engines
    lock = gpudma_lock(dptr, sz)
    res = ioctl_mem_lock(file_fd, lock)
    if res != 0
        println("Error in IOCTL_GPUMEM_LOCK")
        @goto do_free_attr
    end

    # fetch the physical page addresses of our memory buffer.
    # typically, a device driver in kernel space would use these addresses,
    # not pass them to userspace.
    println("Getting state. We lock $(lock.page_count) pages")
    state = gpudma_state(lock.handle, lock.page_count)
    res = ioctl_mem_state(file_fd, state)
    if res < 0
        println("Error in IOCTL_GPUMEM_STATE")
        @goto do_unlock
    end

    println("Page count $(state.page_count)")
    println("Page size $(state.page_size)")

    # print the physical addresses that correspond to each virtual page
    count = 0x0A000000
    for i in 1:state.page_count
        @printf("%02d: 0x%lx\n", i, state.pages[i])

        # to emulate a DMA transfer, the gpumem driver supports accessing
        # physical memory using mmap
        va = mmap(file, Vector{UInt8}, (state.page_size,), state.pages[i]; grow=false)
        let va = reinterpret(UInt32, va)
            for j in 1:length(va)
                va[j] = count
                count += 1
            end
        end
        finalize(va)
        @printf("Physical Address 0x%lx -> Virtual Address %p\n", state.pages[i], pointer(va))
    end

    h_odata = reinterpret(UInt32, Array(data))

    expect_data = 0x0A000000
    error_cnt = 0
    for (ii, val) in enumerate(h_odata)
        if val != expect_data
            error_cnt += 1
            if error_cnt < 32
                @printf("%4d 0x%.8X - Error  expect: 0x%.8X\n", ii, val, expect_data)
            end
        elseif ii <= 16
            @printf("%4d 0x%.8X \n", ii, val);
        end
        expect_data += 1
    end
    if error_cnt == 0
        println("Test successful")
    else
        println("Test with error")
    end

    println("Going to unlock")

@label do_unlock
    unlock = gpudma_unlock(lock.handle)
    res = ioctl_mem_unlock(file_fd, unlock)
    if res < 0
        println("Error in IOCTL_GPUMEM_UNLOCK")
    end

@label do_free_attr
    attribute!(dptr, CUDA.POINTER_ATTRIBUTE_SYNC_MEMOPS, Cuint(0))

@label do_free_memory
    Mem.free(buf)

    return


end

isinteractive() || main()
