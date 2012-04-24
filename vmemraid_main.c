/* vmemraid skeleton */
/* vmemraid_main.c */
/* by William A. Katsak <wkatsak@cs.rutgers.edu> */
/* for CS 416, Fall 2011, Rutgers University */

/* This sets up some functions for printing output, and tagging the output */
/* with the name of the module */
/* These functions are pr_info(), pr_warn(), pr_err(), etc. and behave */
/* just like printf(). You can search LXR for information on these functions */
/* and other pr_ functions. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include "vmemraid.h"
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>

/* Pointer for device struct. You should populate this in init */
struct vmemraid_dev *dev;
static int major_num = 0;

/* builds the parirty for a specified sector for a specified disk and puts it into the buffer */
void build_parity(char *buffer, unsigned disk_num, unsigned disk_row)
{
	int i,j;
	static char buffer_block[VMEMRAID_HW_SECTOR_SIZE];
	
	for(i = 0; i < NUM_DISKS; i++) {
		if(!disk_num) {
			memdisk_read_sector(dev->disk_array->disks[i], buffer_block, disk_row);
		
			for(j = 0; j < VMEMRAID_HW_SECTOR_SIZE; j++) {
				buffer[j] = buffer[j] ^ buffer_block[j];
			}
		}
	
	}
}

/* reads data by either taking from what exists or rebuilds it from parity */
int do_raid4_read(unsigned disk_num, unsigned disk_row, char *buffer)
{
	int i;
	struct memdisk *memdisk = dev->disk_array->disks[disk_num];
	
	if(memdisk) {
		/* this operation is quick because the data already exists */
		pr_info("everythings looking good, reading data that exists\n");
		memdisk_read_sector(memdisk, buffer, disk_row);
		return 1;
	}
	else {	
		/* rebuild the data we want from parity */
		pr_info("oh no, it appears disk_num %d is missing so the data has to be rebuilt, no worries\n", disk_num);
		
		for(i = 0; i < VMEMRAID_HW_SECTOR_SIZE; i++) /* just making sure that data is not pre-written */
				buffer[i] = 0;
				
		build_parity(buffer, disk_num, disk_row);
		return 0;
	}
}

int do_raid4_write(unsigned disk_num, unsigned disk_row, char *buffer)
{
	int i;
	struct memdisk *memdisk = dev->disk_array->disks[disk_num];

	if(memdisk) {
		/* if the disk is alive write the new data, then rebuild parity onto the last disk */
		pr_info("disk %d is alive and kicking", disk_num);
		memdisk_write_sector(memdisk, buffer, disk_row);
		
		if(dev->disk_array->disks[NUM_DISKS-1]) { /* Check that last disk exists */
			/* set data in the buffer to 0 because data is already stored in the correct location */
			for(i = 0; i < VMEMRAID_HW_SECTOR_SIZE; i++) 
				buffer[i] = 0;
			
			build_parity(buffer, NUM_DISKS-1, disk_row); /* Create parity for the last disk */
			memdisk_write_sector(dev->disk_array->disks[NUM_DISKS-1], buffer, disk_row);
		}
		else
			pr_warn("parity disk is missing, guess the data can't be stored now");
			
		return 1;
	}
	else {	
		/* if the disk you want is gone, recreate parity on the last disk anyways but don't write*/
		pr_info("disk %d doesn't exist but the data can still be saved through magic", disk_num);
		
		if(dev->disk_array->disks[NUM_DISKS-1]) {/* Check that last disk exists */
			build_parity(buffer, NUM_DISKS-1, disk_row); /* Create parity data */
			memdisk_write_sector(dev->disk_array->disks[NUM_DISKS-1], buffer, disk_row);
		}
		else
			pr_info("Error more than two disks missing all hope is lost!");
		
		return 0;
	}
}
/* control the flow of data and determine if request is for a read or a write */
/* either way the requested sector is always read in first to store the data into the buffer*/
static void vmemraid_transfer(struct vmemraid_dev *dev, unsigned long sector, 
			      unsigned long num_sectors, char *buffer, int write)
{
	int i, hw_sector,hw_offset;
	static char block_buffer[VMEMRAID_HW_SECTOR_SIZE]; /* Holds the 4k block till its ready to be written over */
	unsigned long current_sector;
	unsigned disk_num, disk_row;
	char *buffer_addr;
	
	for(i = 0; i < num_sectors; i++)
	{
		current_sector = sector + i; /* Increments the sector we're looking at */
		hw_sector = current_sector / 8; /* translates the sector to the hard ware sector */
		hw_offset = (current_sector % 8)* KERNEL_SECTOR_SIZE; /* finds the correct offset for the hardware */
		pr_info("sector is %d, offset is %d\n", hw_sector, hw_offset);

		disk_num = hw_sector % (NUM_DISKS-1); /* gets the virtual disk number */
		disk_row = hw_sector / (NUM_DISKS-1); /* gets ths virtual disk sector */
		buffer_addr = buffer + (i * KERNEL_SECTOR_SIZE); /* contains the memory address of the buffer */
		pr_info("disk_num is %d, disk_row is %d\n", disk_num, disk_row);



		do_raid4_read(disk_num, disk_row, block_buffer); /* get the data on the disk in the sector and put it in the buffer */
		if(write) {
			memcpy(block_buffer + hw_offset, buffer_addr, KERNEL_SECTOR_SIZE); /* copy the data into memory */
			do_raid4_write(disk_num, disk_row, block_buffer); /* write the data to the disk to the sector from the buffer */
		}
		else {
			memcpy(buffer_addr, block_buffer + hw_offset, KERNEL_SECTOR_SIZE); /* copy the read data into memory to send 												   back with the request */
		}
	}
}

/* Request function. This is registered with the block API and gets */
/* called whenever someone wants data from your device */
static void vmemraid_request(struct request_queue *q)
{
	struct request *req;
	pr_info("Starting request\n");
	req = blk_fetch_request(q);

	while (req != NULL) {
		pr_info("handling request\n");		
	
		if(req->cmd_type != REQ_TYPE_FS) {
			pr_info("Skip non-cmd request.\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}

		vmemraid_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),req->buffer, rq_data_dir(req));

		if(!__blk_end_request_cur(req,0)) {
			req = blk_fetch_request(q);
		}

		pr_info("End request\n");
	}
}

/* Open function. Gets called when the device file is opened */
static int vmemraid_open(struct block_device *block_device, fmode_t mode)
{
	pr_info("open start"); 
	spin_lock(&dev->lock); /* Lock the drive so only one process can access cruicial data */
	if (!dev->users)
		check_disk_change(block_device->bd_inode->i_bdev);
	dev->users++;
	spin_unlock(&dev->lock);
	pr_info("open finish"); 
	return 0;
}

/* Release function. Gets called when the device file is closed */
static int vmemraid_release(struct gendisk *gd, fmode_t mode)
{
	pr_info("release start");
	spin_lock(&dev->lock);
	dev->users--;
	spin_unlock(&dev->lock);
	pr_info("release finish");
	return 0;
}

/* getgeo function. Provides device "geometry". This should be */
/* the old cylinders:heads:sectors type geometry. As long as you */
/* populate dev->size with the total usable *****bytes***** of your disk */
/* this implementation will work */
int vmemraid_getgeo(struct block_device *block_device, struct hd_geometry *geo)
{
	long size;

	size = dev->size / KERNEL_SECTOR_SIZE;
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;

	return 0;
}

/* This gets called when a disk is dropped from the array */
/* NOTE: This will be called with dev->lock HELD */
void vmemraid_callback_drop_disk(int disk_num)
{ 
	pr_warn("disk %d was dropped", disk_num);
}
/* This gets called when a dropped disk is replaced with a new one */
/* NOTE: This will be called with dev->lock HELD */
void vmemraid_callback_new_disk(int disk_num)
{	
	struct memdisk *memdisk = dev->disk_array->disks[disk_num];
	static char buffer[VMEMRAID_HW_SECTOR_SIZE];
	int disk_row, num_rows;
	
	num_rows = memdisk_num_sectors(memdisk);
	
	pr_warn("attempting to add disk %d", disk_num);
	pr_info("time to rebuild all the data from parity ughh...");
	
	for(disk_row = 0; disk_row < num_rows; disk_row++) {
		build_parity(buffer, disk_num, disk_row);
		memdisk_write_sector(memdisk, buffer, disk_row);
	}
	

	dev->lost_disk_count--;

	pr_info("disk %d was added successfully", disk_num);
	
	
}

/* This structure must be passed the the block driver API when the */
/* device is registered */
static struct block_device_operations vmemraid_ops = {
	.owner			= THIS_MODULE,
	.open			= vmemraid_open,
	.release		= vmemraid_release,
	.getgeo			= vmemraid_getgeo,
	/* do not tamper with or attempt to replace this entry for ioctl */
	.ioctl		= vmemraid_ioctl
};

/* Init function */
/* This is executed when the module is loaded. Should result in a usable */
/* driver that is registered with the system */
/* NOTE: This is where you should allocate the disk array */
static int __init vmemraid_init(void)
{
	int major = register_blkdev(0, "vmemraid");

	if (major <= 0)	{
		pr_warn("Unable to get major number. Driver will not function.\n");
		return -EBUSY;
	}
	
	dev = kmalloc(sizeof(struct vmemraid_dev), GFP_KERNEL);
	memset(dev, 0, sizeof(struct vmemraid_dev));
	dev->major = major;
	dev->size = DISK_SIZE_SECTORS * VMEMRAID_HW_SECTOR_SIZE*(NUM_DISKS-1); 
	dev->disk_array = create_disk_array(NUM_DISKS, DISK_SIZE_SECTORS);

	if(!dev->disk_array)
		pr_warn("Could not allocate memory for disks. Driver will not function.\n");
		
	spin_lock_init(&dev->lock);
	dev->queue = blk_init_queue(vmemraid_request, &dev->lock);

	if(!dev->queue)
		goto out_cleanup;
	
	blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);
	dev->queue->queuedata = dev;

	dev->gd = alloc_disk(VMEMRAID_NUM_MINORS);

	if(!dev->gd) {
		pr_warn("alloc_disk failure. Driver will not function.\n");
		goto out_cleanup;
	}

	dev->gd->major = dev->major;
	dev->gd->first_minor = 0;
	dev->gd->fops = &vmemraid_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;

	snprintf(dev->gd->disk_name, 32, "vmemraid");

	set_capacity(dev->gd, (dev->size/KERNEL_SECTOR_SIZE));

	add_disk(dev->gd);

	pr_info("Disk added successfully!");
	
	return 0;

out_cleanup:
	if(dev->disk_array)
		destroy_disk_array(dev->disk_array);
		
	unregister_blkdev(dev->major, "vmemraid");

	return -ENOMEM;
}

/* Exit function */
/* This is executed when the module is unloaded. This must free any and all */
/* memory that is allocated inside the module and unregister the device from */
/* the system. */
static void __exit vmemraid_exit(void)
{
	if(dev->gd) {
		del_gendisk(dev->gd);
		put_disk(dev->gd);
	}
	
	if(dev->queue)
		blk_cleanup_queue(dev->queue);

	if(dev->disk_array)
		destroy_disk_array(dev->disk_array);
		
	unregister_blkdev(major_num, "vmemraid");
	
	kfree(dev);
}

/* Tell the module system where the init and exit points are. */
module_init(vmemraid_init);
module_exit(vmemraid_exit);

/* Declare the license for the module to be GPL. This can be important in some */
/* cases, as certain kernel functions are only exported to GPL modules. */
MODULE_LICENSE("GPL");
