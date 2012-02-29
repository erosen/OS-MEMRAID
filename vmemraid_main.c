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

/* Constants to define the number of disks in the array */
/* and the size in HARDWARE sectors of each disk */
/* You should set these values once, right here, and always */
/* reference them via these constants. */
/* Default is 5 disks, each of size 8192 sectors x 4 KB/sector = 32 MB */
#define NUM_DISKS 5
#define NUM_SECTORS 8192

/* Pointer for device struct. You should populate this in init */
struct vmemraid_dev *dev;


static void vmemraid_transfer(struct vmemraid_dev *dev, sector_t sector,
                unsigned long nsect, char *buffer, int write) {
        unsigned long offset = sector * KERNEL_SECTOR_SIZE;
        unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;
 
        if ((offset + nbytes) > dev->size) {
                printk (KERN_NOTICE "vmemraid: Beyond-end write (%ld %ld)\n", offset, nbytes);
                return;
        }
        if (write)
                memcpy(dev->data + offset, buffer, nbytes);
        else
                memcpy(buffer, dev->data + offset, nbytes);
}
 
/* Request function. This is registered with the block API and gets */
/* called whenever someone wants data from your device */
static void vmemraid_request(struct request_queue *q)
{
	struct request *req;
 
        req = blk_fetch_request(q);
        while (req != NULL) {
                if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
                        printk (KERN_NOTICE "Skip non-CMD request\n");
                        __blk_end_request_all(req, -EIO);
                        continue;
                }
                vmemraid_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),
                                req->buffer, rq_data_dir(req));
                if ( ! __blk_end_request_cur(req, 0) ) {
                        req = blk_fetch_request(q);
                }
        }
 
}

/* Open function. Gets called when the device file is opened */
static int vmemraid_open(struct block_device *block_device, fmode_t mode)
{
	return 0;
}

/* Release function. Gets called when the device file is closed */
static int vmemraid_release(struct gendisk *gd, fmode_t mode)
{
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

}
/* This gets called when a dropped disk is replaced with a new one */
/* NOTE: This will be called with dev->lock HELD */
void vmemraid_callback_new_disk(int disk_num)
{

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
	//dev = vmalloc(sizeof(struct vmemraid_dev));	
	
	/* Set up empty device */
	dev->size = NUM_SECTORS * BLOCK_SIZE;
	spin_lock_init(&(dev->lock));
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL)
		return -ENOMEM; 
	
	/* Get a request queue */
	dev->queue = blk_init_queue(vmemraid_request, &(dev->lock));
	if (dev->queue == NULL)
		goto out;
	blk_queue_logical_block_size(dev->queue, NUM_SECTORS);
	
	/* Get registered. */
	dev->major = register_blkdev(dev->major, "vmemraid");
	if (dev->major <= 0) {
		printk(KERN_WARNING "vmemraid: unable to get major number\n");
		goto out;
	}
	
	/* And the gendisk structure. */
	dev->gd = alloc_disk(VMEMRAID_NUM_MINORS);
	if (!dev->gd)
		goto out_unregister;
		
	dev->gd->major = dev->major;
	dev->gd->first_minor = 0;
	dev->gd->fops = &vmemraid_ops;
	dev->gd->private_data = dev;
	strcpy(dev->gd->disk_name, "vmemraid0");
	set_capacity(dev->gd, NUM_SECTORS);
	dev->gd->queue = dev->queue;
	add_disk(dev->gd);

	return 0;

out_unregister:
	unregister_blkdev(dev->major, "vmemraid");
out:
	//vfree(dev);
	vfree(dev->data);
	return -ENOMEM;
	return 0;
}

/* Exit function */
/* This is executed when the module is unloaded. This must free any and all */
/* memory that is allocated inside the module and unregister the device from */
/* the system. */
static void __exit vmemraid_exit(void)
{
	del_gendisk(dev->gd);
	put_disk(dev->gd);
	unregister_blkdev(dev->major, "vmemraid");
	blk_cleanup_queue(dev->queue);
	vfree(dev->data);
}

/* Tell the module system where the init and exit points are. */
module_init(vmemraid_init);
module_exit(vmemraid_exit);

/* Declare the license for the module to be GPL. This can be important in some */
/* cases, as certain kernel functions are only exported to GPL modules. */
MODULE_LICENSE("GPL");

