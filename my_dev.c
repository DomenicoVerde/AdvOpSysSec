#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/slab.h>   
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>



				/* Module Data */
	
#define SET_HIGH_PRIORITY_FLOW 	_IO('D',1)
#define SET_LOW_PRIORITY_FLOW 	_IO('D',2)
#define SET_READ_ASYNC		_IO('D',3)
#define SET_READ_SYNC		_IO('D',4)
#define SET_WRITE_ASYNC		_IO('D',5)
#define SET_WRITE_SYNC		_IO('D',6)
#define SET_TIMEOUT		_IOW('D',6,long *)
#define EXPORT_SYMTAB
#define AUDIT 			if(1)
#define MODNAME			"MY_DEV"
#define DEVICE_NAME 		"my_dev"
#define MINORS			128
#define DISABLED 		0
#define ENABLED 		1
#define LOW 			0
#define HIGH 			1
#define FLOWS 			2
#define MAX_FLOW_SIZE		64


static int major;			// Major of Driver

struct list_head flow[MINORS][FLOWS];	// High and Low Priority Flows

struct node {				// Element of each Flow
	struct list_head list;
	char * data;
};

struct workqueue_struct * my_workqueue;	// Deferred Work Data Structures
struct work_data {			
	struct work_struct work;
	int minor;
	struct node * temp_node;
};

spinlock_t my_lock[MINORS][FLOWS];	// Lock to Synchronize Read/Write Ops

wait_queue_head_t my_wq[MINORS][FLOWS];	// Queues for Waiting Threads

static int priority[MINORS];		// Default Flow for each Device
static long timeout[MINORS];		// Default Timeout for Synchronous Ops


				/* Module Parameters */
int devices_state[MINORS];
module_param_array(devices_state, int, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(devices_state, "An array describing the state of the	\
				devices (Enabled/Disabled).");
				
int low_flows_size[MINORS];
module_param_array(low_flows_size, int, NULL, S_IRUGO);
MODULE_PARM_DESC(low_flows_size, "A Read-Only array describing # of bytes	\
				present in the low priority flow of	 	\
				each device.");
				
int high_flows_size[MINORS];
module_param_array(high_flows_size, int, NULL, S_IRUGO);
MODULE_PARM_DESC(high_flows_size, "A Read-Only array describing # of bytes 	\
				present in the high priority flow of 		\
				each device.");
				
int waiting_threads_low[MINORS];
module_param_array(waiting_threads_low, int, NULL, S_IRUGO);
MODULE_PARM_DESC(waiting_threads_low, "A Read-Only array describing # of 	\
				threads waiting for low priority flow of 	\
				each device.");
				
int waiting_threads_high[MINORS];
module_param_array(waiting_threads_high, int, NULL, S_IRUGO);
MODULE_PARM_DESC(waiting_threads_high, "A Read-Only array describing # of 	\
				threads waiting for high priority flow of 	\
				each device.");
	
		
		
			/* Device Driver File Operations */				
void my_write_handler(struct work_struct * work) {
/* Do the deferred write work. In other words, write bytes to the low priority queue.
*/
	struct work_data *work_data;
	struct node *temp_node;
	int minor;
	size_t len;
	work_data = container_of(work, struct work_data, work);
	temp_node = work_data->temp_node;
	minor = work_data->minor;
	len = strlen(temp_node->data);

	__sync_fetch_and_add(&waiting_threads_low[minor], 1);

	RETRY:
	if (__sync_add_and_fetch(&low_flows_size[minor], len) > MAX_FLOW_SIZE) {
		// If there is not enough space, wait
		__sync_sub_and_fetch(&low_flows_size[minor], len);
		wait_event_idle_exclusive(my_wq[minor][LOW], low_flows_size[minor] + len <= MAX_FLOW_SIZE);
		goto RETRY;
	}

	spin_lock(&my_lock[minor][LOW]);
		list_add_tail(&temp_node->list, &flow[minor][LOW]);
	spin_unlock(&my_lock[minor][LOW]);
			
	__sync_fetch_and_sub(&waiting_threads_low[minor], 1);
	wake_up(&my_wq[minor][LOW]);
	
	AUDIT { printk(KERN_INFO "[%s][%d][LOW]: Written %s.\n", MODNAME,  minor, (char *) temp_node->data); }
}

static int mydev_open(struct inode *, struct file *);
static int mydev_close(struct inode *, struct file *);
static ssize_t mydev_write(struct file *, const char __user *, size_t, loff_t *);
static ssize_t mydev_read(struct file *, char *, size_t, loff_t *);
static long mydev_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations fops = {
	.owner     	= THIS_MODULE,
	.write 		= mydev_write,
	.read 		= mydev_read,
	.open 		= mydev_open,
	.unlocked_ioctl = mydev_ioctl,
	.release 	= mydev_close
};



static int mydev_open(struct inode *inode, struct file *file) {
/* Open the Device only if it is enabled via the corresponding entry in the
 * the devices_state array parameter. 
 */
	int minor = iminor(inode);
	if (devices_state[minor] == ENABLED) {
		AUDIT { printk(KERN_INFO "[%s][%d]: Device Opened.\n", MODNAME, minor); }
		return 0;
	} else {
		AUDIT { printk(KERN_WARNING "[%s][%d]: Cannot Open Disabled Device.\n", MODNAME, minor); }
		return -EACCES;
	}
}



static int mydev_close(struct inode *inode, struct file *file) {
/* Close the Device (Dummy Operation).
 */
	AUDIT { printk(KERN_INFO "[%s][%d]: Device Closed.\n", MODNAME, iminor(inode)); }
	return 0;
}



static ssize_t mydev_write(struct file *filp, const char __user *buff, size_t len, loff_t *off) {
/* Write bytes to the corresponding queue and return # bytes written. 
 * Low Priority Queue implements an asynchronous write operation, meanwhile 
 * High Priority Queue implements a synchronous write operation. 
 */	        
	int minor = iminor(filp->f_path.dentry->d_inode);
	struct node *temp_node = NULL;
	long jiffies = timeout[minor];
	
	// Allocate memory for the segment
	temp_node = kmalloc(sizeof(struct node), GFP_KERNEL);
	temp_node->data = kmalloc(len * sizeof(char), GFP_KERNEL);
	if (temp_node == NULL || temp_node->data == NULL) {
		AUDIT { printk(KERN_WARNING "[%s][%d]: No Space Left on Device.\n", MODNAME, minor); }
		return -ENOSPC;
	}
	
	INIT_LIST_HEAD(&temp_node->list);
	
	// Copy data from user space
	if (copy_from_user(temp_node->data, buff, len) != 0) {
		AUDIT { printk(KERN_WARNING "[%s][%d]: Failed to copy write value from user-space.\n", MODNAME, minor); }
		kfree(temp_node);
		return -EFAULT;
	}
	
	// Select right queue and add the segment
	if (priority[minor] == LOW) {
		// Asynchronous Write Op (Top Half)
		struct work_data * my_work;
		my_work = kmalloc(sizeof(struct work_data), GFP_KERNEL);
		if (my_work == NULL) {
			AUDIT { printk(KERN_WARNING "[%s][%d]: No Space Left on Device.\n", MODNAME, minor); }
			return -ENOSPC;
		}
		INIT_WORK(&my_work->work, my_write_handler);
		my_work->temp_node = temp_node;
		my_work->minor = minor;
		
		queue_work(my_workqueue, &my_work->work);
		AUDIT { printk(KERN_INFO "[%s][%d][LOW]: Write Deferred - %s.\n", MODNAME,  minor, (char *) temp_node->data); }		
		
		return len;
		
	} else if (priority[minor] == HIGH) {
		// Synchronous Write Op	
		__sync_fetch_and_add(&waiting_threads_high[minor], 1);
		
		RETRY:
		if (__sync_add_and_fetch(&high_flows_size[minor], len) > MAX_FLOW_SIZE) {
			// If there is enough space write, otherwhise wait
			__sync_sub_and_fetch(&high_flows_size[minor], len);
			jiffies = wait_event_idle_exclusive_timeout(my_wq[minor][HIGH], 
								high_flows_size[minor] + len <= MAX_FLOW_SIZE, jiffies);
			if (jiffies > 0) {
				// If condition changed and there is some time, retry 
				goto RETRY;	
			} else {
				__sync_fetch_and_sub(&waiting_threads_high[minor], 1);
				kfree(temp_node);
				AUDIT { printk(KERN_INFO "[%s][%d][HIGH]: Write - Timeout Expired.\n", MODNAME, minor);}
				return 0;
			}
		}	

		spin_lock(&my_lock[minor][HIGH]);
			list_add_tail(&temp_node->list, &flow[minor][HIGH]);
		spin_unlock(&my_lock[minor][HIGH]);	
				
		__sync_fetch_and_sub(&waiting_threads_high[minor], 1);			
		wake_up(&my_wq[minor][HIGH]);
		
		AUDIT { printk(KERN_INFO "[%s][%d][HIGH]: Written %s.\n", MODNAME, minor, (char *) temp_node->data); }
		
		return len;
		
	} else {
		AUDIT { printk(KERN_WARNING "[%s][%d]: Default Flow is in a Bad State. Update it.\n", MODNAME, minor); }
		kfree(temp_node);
   		return -EBADFD;
   	}
}



static ssize_t mydev_read(struct file *filp, char __user *buff, size_t len, loff_t *off) {
/* Read synchronously one segment of the selected queue and returns the number
 * of bytes read (including '/n'). After this operation, data will disappear
 * from the selected queue.
 */
	int minor = iminor(filp->f_path.dentry->d_inode);
	struct node *temp_node;
	
	// Select the right queue and remove the segment (Synchronously)
	if (priority[minor] == LOW) {
		__sync_fetch_and_add(&waiting_threads_low[minor], 1);
		if (wait_event_idle_exclusive_timeout(my_wq[minor][LOW], low_flows_size[minor] > 0, timeout[minor]) == 0) {
			// If timeout expires read nothing
			AUDIT { printk(KERN_INFO "[%s][%d][LOW]: Read - Timeout Expired.\n", MODNAME, minor);}
			__sync_fetch_and_sub(&waiting_threads_low[minor], 1);
			return 0;
		}
			
		spin_lock(&my_lock[minor][LOW]);
    			temp_node = list_first_entry(&flow[minor][LOW], struct node, list);    			
    			list_del(&temp_node->list);
    		spin_unlock(&my_lock[minor][LOW]);	
    		
    		__sync_fetch_and_sub(&low_flows_size[minor], strlen(temp_node->data));
    		__sync_fetch_and_sub(&waiting_threads_low[minor], 1);
		wake_up_all(&my_wq[minor][LOW]); 
    		
		AUDIT { printk(KERN_INFO "[%s][%d][LOW]: Read %s.\n", MODNAME, minor, (char *) temp_node->data); }
	
	} else if (priority[minor] == HIGH) {
		__sync_fetch_and_add(&waiting_threads_high[minor], 1);
		if (wait_event_idle_exclusive_timeout(my_wq[minor][HIGH], high_flows_size[minor] > 0, timeout[minor]) == 0) {
			// If timeout expires read nothing
			AUDIT { printk(KERN_INFO "[%s][%d][HIGH]: Read - Timeout Expired.\n", MODNAME, minor);}
			__sync_fetch_and_sub(&waiting_threads_high[minor], 1);
			return 0;
		}  
		
		spin_lock(&my_lock[minor][HIGH]);
    			temp_node = list_first_entry(&flow[minor][HIGH], struct node, list);    			
    			list_del(&temp_node->list);
    		spin_unlock(&my_lock[minor][HIGH]);
    		    		
    		__sync_fetch_and_sub(&high_flows_size[minor], strlen(temp_node->data));
    		__sync_fetch_and_sub(&waiting_threads_high[minor], 1);
   		wake_up_all(&my_wq[minor][HIGH]); 
   		
    		AUDIT { printk(KERN_INFO "[%s][%d][HIGH]: Read %s.\n", MODNAME, minor, (char *) temp_node->data); }

   	} else {
   		AUDIT { printk(KERN_WARNING "[%s][%d]: Default Flow is in a Bad State. Update it.\n", MODNAME, minor); }
   		return -EBADFD;	
	}
	
	// Copy data to user space
	len = strlen(temp_node->data);
	if (copy_to_user(buff, temp_node->data, len) != 0) {
		AUDIT { printk(KERN_WARNING "[%s][%d]: Failed to copy read value from user-space.\n", MODNAME, minor); }
		kfree(temp_node);
		return -EFAULT;
	}
	
	kfree(temp_node);
	return len;
}

static long mydev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
/* Manage already open I/O Sessions. Using this operation, user can set
 * the Flow where wants to write in and setup the execution mode of read/write
 * operation (Synchronous or Asynchronous). In the first case, it is possible
 * to set the timeout used in synchronous operations.
 */
	int minor = iminor(filp->f_path.dentry->d_inode);
	
	switch (cmd) {
		case SET_HIGH_PRIORITY_FLOW:
			AUDIT { printk(KERN_INFO "[%s][%d]: Set High Priority Flow.\n", MODNAME, minor); }
			priority[minor] = HIGH;
			break;
		
		case SET_LOW_PRIORITY_FLOW:
			AUDIT { printk(KERN_INFO "[%s][%d]: Set Low Priority Flow.\n", MODNAME, minor); }
			priority[minor] = LOW;
			break;
		
		case SET_READ_ASYNC:
			AUDIT { printk(KERN_WARNING "[%s][%d]: Asynchronous Read not implemented.\n", MODNAME, minor); }
			return -ENOSYS;
		
		case SET_READ_SYNC:
			AUDIT { printk(KERN_INFO "[%s][%d]: Synchronous Read Set.\n", MODNAME, minor); }
			break;
		
		case SET_WRITE_ASYNC:
			if (priority[minor] == HIGH) {
				AUDIT { printk(KERN_WARNING "[%s][%d][HIGH]: Asynchronous Write not implemented.\n", MODNAME, minor); }
				return -ENOSYS;
			}
			AUDIT { printk(KERN_INFO "[%s][%d][LOW]: Asynchronous Write() Set.\n", MODNAME, minor); }
			break;
		
		case SET_WRITE_SYNC:
			if (priority[minor] == LOW) {
				AUDIT { printk(KERN_WARNING "[%s][%d][LOW]: Synchronous Write() not implemented.\n", MODNAME, minor); }
				return -ENOSYS;
			}
			AUDIT { printk(KERN_INFO "[%s][%d][HIGH]: Synchronous Write() set.\n", MODNAME, minor); }
			break;
		
		case SET_TIMEOUT:
			long t = 0;
			if (copy_from_user(&t, (long *) arg, sizeof(long)) != 0) {
				AUDIT { printk(KERN_WARNING "[%s][%d]: Failed to copy timeout value from user-space.\n", MODNAME, minor); }
				return -EFAULT;
			}
			
			t *= HZ;
			timeout[minor] = t;
			AUDIT { printk(KERN_INFO "[%s][%d]: New Timeout Set: %ld.\n", MODNAME, minor, t); }
			break;
		
		default:
			AUDIT { printk(KERN_WARNING "[%s][%d]: Invalid I/O Ctl CMD=%u.\n", MODNAME, minor, cmd); }
			return -EINVAL;
	}
	return 0;  
}


				/* Module Operations */ 

int init_module(void) {
/* Load the Kernel Module, Registering the Device Driver and
 * Initializing the Device Files.
 */	
	int i = 0;
	
	major = __register_chrdev(0, 0, MINORS, DEVICE_NAME, &fops);
	if (major < 0) {
		printk(KERN_ERR "[%s]: Failed to Register Device Driver.\n", MODNAME);
		return major;
	}

	printk(KERN_INFO "[%s]: Device Driver Registered with Major %d.\n", MODNAME, major);
	
	// Init Devices & Data Structures
	my_workqueue = create_workqueue("my_workqueue");
	while (i < MINORS) {
		INIT_LIST_HEAD(&flow[i][LOW]);
		INIT_LIST_HEAD(&flow[i][HIGH]);
		
		spin_lock_init(&my_lock[i][LOW]);
		spin_lock_init(&my_lock[i][HIGH]);
		
		init_waitqueue_head(&my_wq[i][LOW]);
		init_waitqueue_head(&my_wq[i][HIGH]);
		
		// Init Parameters
		devices_state[i] = ENABLED;
		
		low_flows_size[i] 	= 0;
		high_flows_size[i] 	= 0;		
		waiting_threads_low[i] 	= 0;		
		waiting_threads_high[i] = 0;
		
		timeout[i] = 4 * HZ;
		priority[i] = LOW;
		i++;
	}
	
	return 0;
}

void cleanup_module(void) {
/* Unregister the Device Driver	(Clean-up).
 */
	flush_workqueue(my_workqueue);
	destroy_workqueue(my_workqueue);
	unregister_chrdev(major, DEVICE_NAME);
	printk(KERN_INFO "[%s]: Device Driver with Major %d Unregistered Successfully.\n", MODNAME, major);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Domenico Verde");
MODULE_DESCRIPTION("SOA Academic Project 21/22. A Multi-Flow Device File");
MODULE_VERSION("3.0");
