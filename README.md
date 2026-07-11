# edu_pbuf pseudo device driver

`edu_pbuf` is an educational Linux kernel pseudo character device driver. It
creates `/dev/edu_pbuf` and stores the latest byte sequence written by user
space in a kernel buffer.

The driver demonstrates:

- dynamic kernel module loading with `insmod`, `lsmod` and `rmmod`;
- in-tree driver integration with `Kconfig` and `Makefile`;
- character device registration with `alloc_chrdev_region()` and `cdev`;
- automatic `/dev/edu_pbuf` creation with `class_create()` and
  `device_create()`;
- user/kernel data transfer with `copy_from_user()` and `copy_to_user()`;
- runtime configuration with `ioctl()`.

## Files

```text
drivers/lkcamp/Kconfig
drivers/lkcamp/Makefile
drivers/lkcamp/edu_pbuf.c
include/uapi/linux/edu_pbuf.h
tools/testing/selftests/edu_pbuf/Makefile
tools/testing/selftests/edu_pbuf/edu_pbuf_test.c
kernel-integration.patch
```

This repository intentionally does not include the project `docs/` directory.

## Applying to a Linux kernel tree

From the root of a compatible Linux kernel source tree:

```sh
cp -r /path/to/edu-pbuf-device-driver/drivers/lkcamp drivers/
cp /path/to/edu-pbuf-device-driver/include/uapi/linux/edu_pbuf.h include/uapi/linux/
mkdir -p tools/testing/selftests/edu_pbuf
cp /path/to/edu-pbuf-device-driver/tools/testing/selftests/edu_pbuf/* \
    tools/testing/selftests/edu_pbuf/
git apply /path/to/edu-pbuf-device-driver/kernel-integration.patch
```

## Building

```sh
./scripts/config -m EDU_PBUF
make olddefconfig
make -j"$(nproc)" drivers/lkcamp/edu_pbuf.ko
make -C tools/testing/selftests/edu_pbuf
```

## Running

Use a VM or system running the same kernel version/configuration used to build
the module. If `vermagic` does not match `uname -r`, `insmod` can fail.

```sh
sudo insmod drivers/lkcamp/edu_pbuf.ko capacity=4096
lsmod | grep edu_pbuf
ls -l /dev/edu_pbuf
dmesg -T | tail -n 20
```

Basic read/write:

```sh
printf 'hello from user space\n' | sudo tee /dev/edu_pbuf
sudo cat /dev/edu_pbuf
```

`ioctl()` test:

```sh
sudo tools/testing/selftests/edu_pbuf/edu_pbuf_test /dev/edu_pbuf
```

Unload:

```sh
sudo rmmod edu_pbuf
dmesg -T | tail -n 20
```
