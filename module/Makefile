KERNEL_VER ?= $(shell uname -r)
MODULES_DIR := /lib/modules/$(KERNEL_VER)
KERNEL_DIR := $(MODULES_DIR)/build

# discover the sources of the NVIDIA driver based on the loaded module
# (just in case there are sources for couple of versions)
nv_version=$(shell /sbin/modinfo -F version -k $(KERNEL_VER) nvidia 2>/dev/null)
nv_sources=$(shell /bin/ls -d /usr/src/nvidia-$(nv_version)/ 2>/dev/null)
ifneq ($(shell test -d "$(nv_sources)" && echo "true" || echo "" ),)
	NVIDIA_SRC_DIR ?= $(shell find "$(nv_sources)" -name "nv-p2p.h"|head -1|xargs dirname || echo "NVIDIA_DRIVER_MISSING")
else
	NVIDIA_SRC_DIR ?= $(shell find /usr/src/nvidia-* -name "nv-p2p.h"|head -1|xargs dirname || echo "NVIDIA_DRIVER_MISSING")
endif
ccflags-y += -I$(NVIDIA_SRC_DIR)

obj-m += gpumem.o
gpumem-y := gpumemproc.o ioctlrw.o gpumemdrv.o

# don't warn about missing NVIDIA symbols; they'll be available
# TODO: create our own Module.symvers,
#       https://github.com/NVIDIA/gds-nvidia-fs/blob/af3f7de96b4e500abf30e3034e5827749953bd68/src/Makefile#L108-L112
KBUILD_MODPOST_WARN=1

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

distclean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.bak .*.cache *.d

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.bak .*.cache *.d *.markers *.symvers *.order
