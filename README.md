# libusb4
A VFIO program to map host interface memory of USB4 host router present in the system

Please note that this program assumes you have a NHI TBT controller in your linux system. 
Also, you have to clone this git repository for some functions to work. https://github.com/pciutils/pciutils

After cloning the above repo, you need to compile it so run make. 

Now, check your PCI tree and see which ID represents NHI controller. In my system it is 0000:03:00.0

Bind the vfio-pci driver: sudo sh -c "echo 8086 15e8 > /sys/bus/pci/drivers/vfio-pci/new_id"

Compile the code: gcc nhi.c vfio_nhi_enumerate.c -I/home/rajatkha/pciutils/lib -lpci

Run the binary: sudo ./a.out

The following output should be displayed:
pre-SET_CONTAINER:
VFIO_CHECK_EXTENSION VFIO_TYPE1_IOMMU: Present
post-SET_CONTAINER:
VFIO_CHECK_EXTENSION VFIO_TYPE1_IOMMU: Present
Device supports 9 regions, 5 irqs
Info for region 0
size 0x40000, offset 0x0, flags 0x7
Info for region 7
size 0x1000, offset 0x70000000000, flags 0x3
Info for MSIX IRQ
flags 0x9, count 0x10
Info for MSI IRQ
flags 0x9, count 0x1
Info for normal IRQ
flags 0x7, count 0x1
Info for IOMMU
flags 0x3, page_sizes 0xfffffffffffff000
PCI Device:0000:03:00.0
Total paths: 12

As you can see, total paths are printed (no of hop IDs)

