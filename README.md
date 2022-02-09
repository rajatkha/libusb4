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
Total paths: 12

As you can see, total paths are printed (no of hop IDs). This value is present in bits [10:0] of host interface capabilities register

