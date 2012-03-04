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
#include <linux/vmalloc.h>
#include "vmemraid.h"

/* Pointer for device struct. You should populate this in init */
struct vmemraid_dev *dev;
 
/* Request function. This is registered with the block API and gets */
/* called whenever someone wants data from your device */
static void vmemraid_request(struct request_queue *q)
{
	
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
	/* set the availabilty of a droped disk to 0 */
	pr_info("vmemraid: disk %d was dropped", disk_num);
	dev->available[disk_num] = 0;

}
/* This gets called when a dropped disk is replaced with a new one */
/* NOTE: This will be called with dev->lock HELD */
void vmemraid_callback_new_disk(int disk_num)
{
	pr_info("vmemraid: disk %d was added", disk_num);
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
	int i;
	
	/* Allocate space for the device */
	dev = kmalloc(sizeof(struct vmemraid_dev), GFP_KERNEL);
	if (!dev)
		pr_err("vmemraid: Unable to allocate space for device structure");

	/* Create all the memory disks */
	dev->disk_array = create_disk_array(NUM_DISKS, DISK_SIZE_SECTORS * KERNEL_SECTOR_SIZE);	
	
	/* register the device to /dev */
	dev->major = register_blkdev(0, "vmemraid");
	if (dev->major <= 0)
		pr_err("vmemraid: Unable to register device with kernel");
	
	/* mark all the disks as available */
	for (i = 0; i < NUM_DISKS; i++) {
		dev->available[i] = 1;
	}
	
	/* Set total size capacity*/
	dev->size = DISK_SIZE_SECTORS * KERNEL_SECTOR_SIZE * (NUM_DISKS-1);
	
	/* initialize spin lock */
	spin_lock_init(&(dev->lock));
	
	/* create queue for requests */
	dev->queue = blk_init_queue(vmemraid_request, &(dev->lock));
	if (!dev->queue)
		pr_err("vmemraid: Unable to initialize request queue");
	blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);
	
	/* Create the gendisk */
	dev->gd = alloc_disk(VMEMRAID_NUM_MINORS);
	if (!dev->gd)
		pr_err("vmemraid: Unable to allocate gendisk");
	
	/* set all the gendisk info */
	pr_info("make gd");
	dev->gd->major = dev->major;
	dev->gd->first_minor = 0;
	dev->gd->fops = &vmemraid_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev; 
	strcpy(dev->gd->disk_name, "vmemraid");
	set_capacity(dev->gd, dev->size/KERNEL_SECTOR_SIZE);
	
	pr_info("add gd");
	add_disk(dev->gd);
	
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

