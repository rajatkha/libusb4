#include "types.h"

int find_nhi_dev(char* nhi_dev);
int find_nhi_iommu_grp(char* nhi_dev, int* iommu_grp_no);
int nhi_bind_vfio(char* nhi_dev);
struct tb_nhi *nhi_vfio_map_mem(char* nhi_dev, int iommu_grp);

struct tb_nhi {
        void *iobase;
        struct tb_ctl *ctl;
        bool going_away;
        u32 hop_count;
        struct tb_ring **tx;
        struct tb_ring **rx;
	int irq;
	int vfio_event_fd[16];
	char nhi_dev[256];
	int safe_mode;
};

void read_config_dword(void *buff, u64 offset);
void write_config_dword(struct tb_nhi *nhi, u32 val, u64 offset);
void read_word(void *buff, u64 offset);
void write_word(void *buff, u64 offset);
void read_64word(void *buff, u64 offset);
void write_64word(void *buff, u64 offset);
