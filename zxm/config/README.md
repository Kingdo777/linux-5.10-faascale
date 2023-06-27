File `microvm-kernel-x86_64-5.10.config` is downloaded form firecracker's recommended [guest kernel configurations](https://github.com/firecracker-microvm/firecracker/blob/main/resources/guest_configs). It is based on the version 5.10. 

More detial about rootfs/kernel can be found at [Creating Custom rootfs and kernel Images](https://github.com/firecracker-microvm/firecracker/blob/main/docs/rootfs-and-kernel-setup.md#creating-custom-rootfs-and-kernel-images).

TO debug the kernel, we should modify it, as my [blog](https://zhuanlan.zhihu.com/p/412604505):
``` bash
./scripts/config \
    -e DEBUG_INFO \
    -e GDB_SCRIPTS \
    -e CONFIG_DEBUG_SECTION_MISMATCH \
    -d CONFIG_RANDOMIZE_BASE
```

Most Importantly, we should enable the flow CONFIG :
```config
CONFIG_PCI
CONFIG_VIRTIO_PCI
CONFIG_VIRTIO_BLK
```

otherwise, we will get the kernel panic:
``virtio-blk VFS: Cannot open root device "vda"``
