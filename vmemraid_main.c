/* vmemraid skeleton */
/* vmemraid_main.c */
/* by William A. Katsak <wkatsak@cs.rutgers.edu> */
/* for CS 416, Fall 2011, Rutgers University */
/* Completed by Elie Rosen */

/* This sets up some functions for printing output, and tagging the output */
/* with the name of the module */
/* These functions are pr_info(), pr_warn(), pr_err(), etc. and behave */
/* just like printf(). You can search LXR for information on these functions */
/* and other pr_ functions. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>	/* printk() */
#include <linux/init.h> 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>		/* everything... */
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/errno.h>	/* error codes */
#include <linux/hdreg.h>
#include <linux/slab.h>
#include "vmemraid.h"

/* Pointer for device struct. You should populate this in init */
struct vmemraid_dev *dev;

/* Raid 4 read function also will send to parity function if data DNE*/
int do_raid4_read(unsigned disk_num, unsigned disk_stripe, char *buffer)
{
	struct memdisk *memdisk = dev->disk_array->disks[disk_num];
	
	if(memdisk) {
		memdisk_read_sector(memdisk, buffer, disk_stripe);
		return 1;
	}
	else {
		return 0;
	}
}

/* Raid 4 write function also will send to parity function after every write*/
int do_raid4_write(unsigned disk_num, unsigned disk_stripe, char *buffer)
{
	struct memdisk *memdisk = dev->disk_array->disks[disk_num];
	
	if(memdisk) {
		memdisk_write_sector(memdisk, buffer, disk_stripe);
		return 1;
	}
	else {
		return 0;
	}
}
				
static void vmemraid_transfer(struct vmemraid_dev *dev, unsigned long sector, unsigned long num_sectors, char *buffer, int write)
{

	int i, hw_sector,hw_offset;
	static char tmp_block[4096];
	unsigned long current_sector;
	unsigned disk_num, disk_stripe;
	char *buffer_addr;
	
	for (i = 0; i < num_sectors; i++) {
		current_sector = sector * i;
		
		hw_sector = current_sector / 8;
		hw_offset = (current_sector % 8) * KERNEL_SECTOR_SIZE;
	
		disk_num = hw_sector % NUM_DISKS;
		disk_stripe = hw_sector / NUM_DISKS;
		buffer_addr = buffer * (i * KERNEL_SECTOR_SIZE);
		
		pr_info("%s", buffer_addr);
		if (write) {
			do_raid4_read(disk_num, disk_stripe, tmp_block);
			memcpy(&tmp_block + hw_offset, &buffer_addr, KERNEL_SECTOR_SIZE);
			do_raid4_write(disk_num, disk_stripe, tmp_block);
		}
		else {
			do_raid4_read(disk_num, disk_stripe, tmp_block);
			memcpy(&tmp_block + hw_offset, buffer_addr, KERNEL_SECTOR_SIZE);
		}
	}
}					
/* Request function. This is registered with the block API and gets */
/* called whenever someone wants data from your device */
static void vmemraid_request(struct request_queue *q)
{
	struct request *req;
	
	pr_info("Starting a request\n");
	
	req = blk_fetch_request(q);
	
	while(req != NULL) {
		pr_info("Handle a request\n");
		
		if (req->cmd_type != REQ_TYPE_FS) {
			pr_info("Skipping a non file system request/n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		
		vmemraid_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req), req->buffer, rq_data_dir(req));
		
		if(!__blk_end_request_cur(req, 0))
			req = blk_fetch_request(q);
			
	}
	
	pr_info("End request\n");
}
/* Open function. Gets called when the device file is opened */
static int vmemraid_open(struct block_device *block_device, fmode_t mode)
{
	pr_info("open start");
	spin_lock(&dev->lock);
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
	/*pr_info("release start");
	spin_lock(&dev->lock);
	dev->users--;
	spin_unlock(&dev->lock);
	pr_info("release finish"); */
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
	/* set the availabilty of a droped disk to 0 */
	pr_warn("vmemraid: disk %d was dropped", disk_num);
	

}
/* This gets called when a dropped disk is replaced with a new one */
/* NOTE: This will be called with dev->lock HELD */
void vmemraid_callback_new_disk(int disk_num)
{
	pr_warn("vmemraid: disk %d was added", disk_num);
}

/* This structure must be passed the the block driver API when the */
/* device is registered */
static struct block_device_operations vmemraid_ops = {
	.owner			= THIS_MODULE,
	.open			= vmemraid_open,
	.release		= vmemraid_release,
	.getgeo			= vmemraid_getgeo,
	/* do not tamper with or attempt to replace this entry for ioctl */
	.ioctl			= vmemraid_ioctl
};

/* Init function */
/* This is executed when the module is loaded. Should result in a usable */
/* driver that is registered with the system */
/* NOTE: This is where you should allocate the disk array */
static int __init vmemraid_init(void)
{	
	/* Allocate space for the device */
	dev = kmalloc(sizeof(struct vmemraid_dev), GFP_KERNEL);
	
	memset(dev, 0, sizeof(struct vmemraid_dev));
	
	dev->major = 0;
	dev->size = DISK_SIZE_SECTORS * VMEMRAID_HW_SECTOR_SIZE * NUM_DISKS;
	dev->disk_array = create_disk_array(NUM_DISKS, DISK_SIZE_SECTORS);

	if (!dev->disk_array) {
		pr_warn("Could no tallocate mem for disks");
		return -ENOMEM;
	}

	spin_lock_init(&dev->lock);
	dev->queue = blk_init_queue(vmemraid_request, &dev->lock);

	if (!dev-> queue)
		goto out_cleanup;

	blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);
	dev->queue->queuedata = dev;
	dev->gd = alloc_disk(VMEMRAID_NUM_MINORS);

	if (!dev->gd) {
		pr_warn("alloc_disk failure. Driver will not function.\n");
		goto out_cleanup;
	}

	dev->gd->major = dev->major;
	dev->gd->first_minor = 0;
	dev->gd->fops = &vmemraid_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;

	snprintf(dev->gd->disk_name, 32, "vmemraid");
	
	/* in 512 byte sectors */
	set_capacity(dev->gd, DISK_SIZE_SECTORS * (VMEMRAID_HW_SECTOR_SIZE / KERNEL_SECTOR_SIZE) * NUM_DISKS);

	add_disk(dev->gd);

	pr_info("Loaded driver...");

	return 0;
	
out_cleanup:
	if (dev->disk_array) 
		destroy_disk_array(dev->disk_array);
	
	unregister_blkdev(dev->major, "vmemraid");
	
	return -ENOMEM;
	
	return 0;
}

/* Exit function */
/* This is executed when the module is unloaded. This must free any and all */
/* memory that is allocated inside the module and unregister the device from */
/* the system. */
static void __exit vmemraid_exit(void)
{
	/* Remove the gendisk being used */
	del_gendisk(dev->gd);
	/* remove all traces of the disk array */
	destroy_disk_array(dev->disk_array);
	/* Unregister the device */
	unregister_blkdev(dev->major, "vmemraid");
	/* clean up the queue */
	blk_cleanup_queue(dev->queue);
	/* free used memory */
	kfree(dev);
}

/* Tell the module system where the init and exit points are. */
module_init(vmemraid_init);
module_exit(vmemraid_exit);

/* Declare the license for the module to be GPL. This can be important in some */
/* cases, as certain kernel functions are only exported to GPL modules. */
MODULE_LICENSE("GPL");

