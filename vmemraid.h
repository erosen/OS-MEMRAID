/* vmemraid header */
/* vmemraid.h */
/* by William A. Katsak <wkatsak@cs.rutgers.edu> */
/* for CS 416, Fall 2011, Rutgers University */

/* None of these parameters should be changed */

/* Your device should pretend to have 512 byte sectors, but of course */
/* the "hardware" has 4096 byte sectors */
#define KERNEL_SECTOR_SIZE 512
#define VMEMRAID_HW_SECTOR_SIZE 4096

/* The number of minors represents how many partitions the driver supports */
#define VMEMRAID_NUM_MINORS 16

/* Structure that represents a memdisk. The actual definition */
/* is purposely concealed. */
struct memdisk;

/* Structure that represents a disk array. Contains a list of "memdisks" */
struct disk_array {
	struct memdisk **disks;
	unsigned num_disks;
	unsigned disk_size_sectors;
};

/* Device structure. You may add additional items to this */
struct vmemraid_dev {
	int major;
	unsigned size;
	struct disk_array *disk_array;
	short users;
	spinlock_t lock;

	struct request_queue *queue;
	struct gendisk *gd;
};

/* Functions to create the disk array and read and write from the memdisks */
struct disk_array *create_disk_array(unsigned num_disks, unsigned disk_size_sectors);
void destroy_disk_array(struct disk_array *disk_array);
int memdisk_read_sector(struct memdisk *memdisk, char *buffer, unsigned sector_num);
int memdisk_write_sector(struct memdisk *memdisk, char *buffer, unsigned sector_num);
int memdisk_num_sectors(struct memdisk *memdisk);

/* ioctl() implementation */
/* This is done in the binary portion, so you shouldn't mess with this */
int vmemraid_ioctl(struct block_device *block_device, fmode_t mode,
		unsigned int cmd, unsigned long arg);

/* Callbacks that get called when a disk is dropped or added */
void vmemraid_callback_drop_disk(int disk_num);
void vmemraid_callback_new_disk(int disk_num);

