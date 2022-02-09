#include <stdlib.h>     /* atoi */
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>

#include <stdio.h>
#include <stdint.h>
#include <linux/limits.h>
#include <linux/mman.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include <pci.h>
#include <fcntl.h>
#include <linux/vfio.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>

#include <math.h>

#include <internal.h>

#include "nhi.h"

extern int errno;
int device, fp_vfio;
u64 config_offset, bar_offset;

/*
 * This function finds
 * NHI controller in the system
 * represented as
 * <domain>:<bus>:<device>.<function>
 */
int find_nhi_dev(char* nhi_dev)
{
	struct pci_access* pacc;
    	struct pci_dev* dev;
    	char namebuf[1024], * name, * search_name;

    	pacc = pci_alloc();		/* Get the pci_access structure */
    	/* Set all options you want -- here we stick with the defaults */
    	pci_init(pacc);		/* Initialize the PCI library */
    	pci_scan_bus(pacc);		/* We want to get the list of devices */
    	for (dev = pacc->devices; dev; dev = dev->next) {
        	pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);	/* Fill in header info we need */
	        name = pci_lookup_name(pacc, namebuf, sizeof(namebuf), PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);
        	search_name = strstr(name, "NHI");
        	if (search_name != NULL) {
            		sprintf(nhi_dev, "%04x:%02x:%02x.%d", dev->domain, dev->bus, dev->dev, dev->func);

            		pci_cleanup(pacc);		/* Close everything */
            		return 0;
        	}

    	}
    	pci_cleanup(pacc);		/* Close everything */
    	return -1;
}

int find_nhi_iommu_grp(char* nhi_dev, int* iommu_grp_no)
{
	char namebuf[1024], *iommu_group;
    	char iommu_path[1024];

    	snprintf(iommu_path, sizeof(iommu_path),
        	"/sys/bus/pci/devices/%s/iommu_group", nhi_dev);
    	memset(namebuf, 0, 1024);
    	readlink(iommu_path, namebuf, 1024);
    	iommu_group = strrchr(namebuf, '/');
    	if (iommu_group != NULL) {
        	*iommu_grp_no = atoi(iommu_group + 1);
        	return 0;
    	}

    	return -1;
}

/*
 * This function unbinds native driver
 * of NHI and binds VFIO driver to NHI
 */
int nhi_bind_vfio(char* nhi_dev)
{
	const char* kdrv_name = "vfio-pci";
    	char path[1024];
    	int fd = -1;
	int ret;
	FILE *file;

	/* specifying path to override driver of NHI device */
    	snprintf(path, sizeof(path),
        	"/sys/bus/pci/devices/%s/driver_override", nhi_dev);

    	fd = open(path, O_WRONLY);
	file = fdopen(fd, "w");

    	if ((fd < 0) || (file==NULL)) {
        	printf("Cannot open %s: %s\n",
            		path, "failed");
        	goto err;
	}
	/* override the driver for NHI by vfio-pci */
	ret = fwrite(kdrv_name, sizeof(kdrv_name), 1, file);

    	if (ret < 0) {
        	printf("Error: bind failed - Cannot write "
            		"driver %s to device %s\n", kdrv_name, nhi_dev);
        	goto err;
	}
	close(fd);
	fclose(file);

	/* specifying path to unbind native driver */
    	snprintf(path, sizeof(path),
        	"/sys/bus/pci/devices/%s/driver/unbind", nhi_dev);

	fd = open(path, O_WRONLY);
	file = fdopen(fd, "w");

	if ((fd < 0) || (file==NULL)) {
                printf("Cannot open %s: %s\n",
                        path, "failed");
                //goto err;
        }

	/* unbinding native driver */
	ret = fwrite(nhi_dev, sizeof(nhi_dev), 1, file);

        if (ret < 0) {
                printf("Error: unbind failed - Cannot unbind pci driver - %s\n", path);
                //goto err;
        }
	close(fd);
	fclose(file);

	/* specifying path to probe */
	snprintf(path, sizeof(path),
        	"/sys/bus/pci/drivers_probe");

	fd = open(path, O_WRONLY);
	file = fdopen(fd, "w");

	if ((fd < 0) || (file==NULL)) {
                printf("Cannot open %s: %s\n",
                        path, "failed");
                goto err;
        }

	/* probing */
	ret = fwrite(nhi_dev, sizeof(nhi_dev), 1, file);
	if (ret < 0) {
                printf("Error: PCI probe failed\n");
                goto err;
        }
        close(fd);
	fclose(file);

    	return 0;

	err:
    		close(fd);
    		return -1;
}

struct tb_nhi *nhi_vfio_map_mem(char* nhi_dev, int iommu_grp)
{
  	int i, ret, group;
    	char iommu_path[1024];

    	struct vfio_group_status group_status = {
        	.argsz = sizeof(group_status)
    	};

    	struct vfio_device_info device_info = {
        	.argsz = sizeof(device_info)
    	};

    	struct vfio_region_info region_info = {
    		.argsz = sizeof(region_info)
    	};

	struct vfio_irq_info irq_info = {
		.argsz = sizeof(irq_info)
	};

	struct vfio_iommu_type1_info iommu_info = {
		.argsz = sizeof(iommu_info)
	};

    	fp_vfio = open("/dev/vfio/vfio", O_RDWR);

    	if (fp_vfio < 0) {
        	printf("Failed to open /dev/vfio/vfio, %d (%s)\n",
            		fp_vfio, strerror(errno));
        	return NULL;
    	}

    	snprintf(iommu_path, sizeof(iommu_path), "/dev/vfio/%d", iommu_grp);

    	group = open(iommu_path, O_RDWR);
    	if (group < 0) {
        	printf("Failed to open %s, %d (%s)\n",
            		iommu_path, group, strerror(errno));
        	return NULL;
    	}

    	ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
    	if (ret) {
        	printf("ioctl(VFIO_GROUP_GET_STATUS) failed\n");
        	return NULL;
    	}

    	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        	printf("Group not viable, are all devices attached to vfio?\n");
        	return -1;
    	}

    	printf("pre-SET_CONTAINER:\n");
    	printf("VFIO_CHECK_EXTENSION VFIO_TYPE1_IOMMU: %sPresent\n",
        	ioctl(fp_vfio, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) ?
       		"" : "Not ");

    	ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &fp_vfio);
    	if (ret) {
        	printf("Failed to set group fp_vfio\n");
        	return NULL;
    	}

    	printf("post-SET_CONTAINER:\n");
    	printf("VFIO_CHECK_EXTENSION VFIO_TYPE1_IOMMU: %sPresent\n",
        	ioctl(fp_vfio, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) ?
        	"" : "Not ");

    	ret = ioctl(fp_vfio, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    	if (ret) {
        	printf("Failed to set IOMMU\n");
        	return NULL;
    	}

    	device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, nhi_dev);
    	if (device < 0) {
        	printf("Failed to get device %s\n", nhi_dev);
        	return NULL;
    	}

    	if (ioctl(device, VFIO_DEVICE_GET_INFO, &device_info)) {
        	printf("Failed to get device info\n");
        	return NULL;
    	}

    	printf("Device supports %d regions, %d irqs\n",
        	device_info.num_regions, device_info.num_irqs);

    	region_info.index = 0;

    	if (ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &region_info)) {
        	printf("Failed to get info for region 0\n");
    	}

   	printf("Info for region 0\nsize 0x%lx, offset 0x%lx, flags 0x%x\n", (unsigned long)region_info.size, (unsigned long)region_info.offset, region_info.flags);
	bar_offset = region_info.offset;

	struct tb_nhi *nhi = malloc(sizeof(struct tb_nhi));
    	if (region_info.flags & VFIO_REGION_INFO_FLAG_MMAP) {
        	nhi->iobase = mmap(NULL, (size_t)region_info.size, PROT_READ | PROT_WRITE, MAP_SHARED, device, (off_t)region_info.offset);

        	if (nhi->iobase == MAP_FAILED) {
            		printf("mmap failed\n");
			return NULL;
        	}
    	}

	region_info.index = 7;

	if(ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &region_info))  {
		printf("Failed to get info for region 7\n");
	}

	printf("Info for region 7\nsize 0x%lx, offset 0x%lx, flags 0x%x\n", (unsigned long)region_info.size, (unsigned long)region_info.offset, region_info.flags);
	config_offset = region_info.offset;

	irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;
	if(ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info))
		printf("Failed to get info for MSIX IRQ\n");

	printf("Info for MSIX IRQ\nflags 0x%x, count 0x%lx\n", irq_info.flags, (unsigned long)irq_info.count);

	irq_info.index = VFIO_PCI_MSI_IRQ_INDEX;
        if(ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info))
                printf("Failed to get info for MSI IRQ\n");

        printf("Info for MSI IRQ\nflags 0x%x, count 0x%lx\n", irq_info.flags, (unsigned long)irq_info.count);

	irq_info.index = VFIO_PCI_INTX_IRQ_INDEX;
        if(ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info))
                printf("Failed to get info for normal IRQ\n");

        printf("Info for normal IRQ\nflags 0x%x, count 0x%lx\n", irq_info.flags, (unsigned long)irq_info.count);

	if(ioctl(fp_vfio, VFIO_IOMMU_GET_INFO, &iommu_info))
		printf("Failed to get info for IOMMU\n");

	printf("Info for IOMMU\nflags 0x%x, page_sizes 0x%lx\n", iommu_info.flags, iommu_info.iova_pgsizes);

    	return nhi;
}

void read_word(void *buff, u64 offset) {
	ssize_t ret = pread(device, buff, 4, bar_offset + offset);
	if(ret==-1)
                printf("reading from bar space offset %ld failed\n", offset);
}

void read_64word(void *buff, u64 offset) {
	ssize_t ret = pread(device, buff, 8, bar_offset + offset);
        if(ret==-1)
                printf("reading from bar space offset %ld failed\n", offset);
}

void write_word(void *buff, u64 offset) {
	ssize_t ret = pwrite(device, buff, 4, bar_offset + offset);
	if(ret==-1)
		printf("writing to bar space offset %ld failed\n", offset);
}

void write_64word(void *buff, u64 offset) {
	ssize_t ret = pwrite(device, buff, 8, bar_offset + offset);
        if(ret==-1)
                printf("writing to bar space offset %ld failed\n", offset);
}

void read_config_byte(void *buff, u64 offset) {
	ssize_t ret = pread(device, buff, 1, config_offset + offset);
	if(ret==-1)
		printf("reading from config space offset %ld failed\n", offset);
}

void write_config_byte(struct tb_nhi *nhi, u8 val, u64 offset) {
        char path[1024];
        snprintf(path, sizeof(path), "setpci -s %s %x.B=%x", nhi->nhi_dev, offset, val);
        if(system(path) < 0)
                printf("writing to config space offset %ld failed\n", offset);
}

void read_config_dword(void *buff, u64 offset) {
	ssize_t ret = pread(device, buff, 4, config_offset + offset);
	if(ret==-1)
		printf("reading from config space offset %ld failed\n", offset);
}

void write_config_dword(struct tb_nhi *nhi, u32 val, u64 offset) {
	char path[1024];
	snprintf(path, sizeof(path), "setpci -s %s %x.L=%x", nhi->nhi_dev, offset, val);
	if(system(path) < 0)
		printf("writing to config space offset %ld failed\n", offset);
}

