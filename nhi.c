#include <linux/vfio.h>
#include <sys/eventfd.h>
#include <endian.h>
#include <unistd.h>

#include "nhi.h"
#include "nhi_regs.h"

int main(void) {

	char nhi_dev[256];
	int iommu_grp_no, ret;

	ret = find_nhi_dev(nhi_dev);
	if(ret) {
		printf("ERROR: NHI controller can't be found on this system\n");
		goto err;
	}

	ret = find_nhi_iommu_grp(nhi_dev, &iommu_grp_no);
	if(ret) {
		printf("ERROR: can't find iommu group\n");
		goto err;
	}

	ret = nhi_bind_vfio(nhi_dev);
	if(ret) {
		printf("ERROR: can't bind vfio driver to NHI\n");
		goto err;
	}

	struct tb_nhi *nhi  = nhi_vfio_map_mem(nhi_dev, iommu_grp_no);
	if(!nhi) {
		printf("ERROR: can't generate mem map of NHI BAR0\n");
		goto err;
	}

	strcpy(nhi->nhi_dev, nhi_dev);
        printf("PCI Device:%s\n", nhi->nhi_dev);

	nhi->hop_count = *(int*)(nhi->iobase+REG_HOP_COUNT);
	printf("Total paths: %d\n", nhi->hop_count);

	err:
		return -1;
}
