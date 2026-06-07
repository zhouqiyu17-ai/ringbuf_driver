/*
 * ringbuf_driver.c
 *
 * 实验二：Linux 内核环形缓冲区字符设备驱动
 *
 * 功能说明：
 * 1. 加载模块后自动创建 /dev/ringbuf 字符设备节点；
 * 2. 用户程序可以通过 open/read/write/close 像访问普通文件一样访问该设备；
 * 3. 驱动内部维护一个 4KB 的环形缓冲区；
 * 4. write() 将用户空间数据写入内核环形缓冲区；
 * 5. read() 从内核环形缓冲区读取数据返回用户空间；
 * 6. 使用 mutex 保护共享缓冲区，避免并发读写造成数据错误。
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define DEVICE_NAME "ringbuf"
#define CLASS_NAME  "ringbuf_class"
#define BUFFER_SIZE 4096

/* 设备号，包含主设备号和次设备号 */
static dev_t ringbuf_dev;

/* Linux 内核中表示字符设备的结构体 */
static struct cdev ringbuf_cdev;

/* 设备类，用于配合 udev 在 /dev 下创建设备节点 */
static struct class *ringbuf_class;

/*
 * 环形缓冲区及其状态变量：
 * ring_buffer：真正存放数据的内核缓冲区；
 * head：读指针，指向下一次 read 要读取的位置；
 * tail：写指针，指向下一次 write 要写入的位置；
 * count：当前缓冲区中已有的有效数据字节数。
 */
static char ring_buffer[BUFFER_SIZE];
static size_t head;
static size_t tail;
static size_t count;

/* 互斥锁，用来保护 ring_buffer/head/tail/count */
static DEFINE_MUTEX(ringbuf_mutex);

/*
 * open 回调函数
 * 当用户程序调用 open("/dev/ringbuf", ...) 时，内核会调用本函数。
 */
static int ringbuf_open(struct inode *inode, struct file *file)
{
	pr_info("ringbuf: device opened\n");
	return 0;
}

/*
 * release 回调函数
 * 当用户程序调用 close(fd) 时，内核会调用本函数。
 */
static int ringbuf_release(struct inode *inode, struct file *file)
{
	pr_info("ringbuf: device closed\n");
	return 0;
}

/*
 * read 回调函数
 *
 * 参数说明：
 * file：内核文件对象；
 * user_buf：用户空间提供的缓冲区地址；
 * len：用户希望读取的最大字节数；
 * offset：文件偏移量，本虚拟设备不使用。
 *
 * 返回值：
 * 大于 0：实际读取的字节数；
 * 等于 0：缓冲区为空，暂时没有数据；
 * 小于 0：发生错误。
 */
static ssize_t ringbuf_read(struct file *file, char __user *user_buf,
			    size_t len, loff_t *offset)
{
	size_t read_bytes = 0;
	char ch;

	if (len == 0)
		return 0;

	/*
	 * 对共享缓冲区加锁。
	 * 因为 read 和 write 可能被不同进程同时调用，如果不加锁，
	 * head、tail、count 可能被同时修改，导致数据错乱。
	 */
	if (mutex_lock_interruptible(&ringbuf_mutex))
		return -ERESTARTSYS;

	/* 简单处理：如果缓冲区为空，直接返回 0。 */
	if (count == 0) {
		mutex_unlock(&ringbuf_mutex);
		pr_info("ringbuf: read 0 bytes, buffer empty\n");
		return 0;
	}

	/*
	 * 逐字节从环形缓冲区读出数据。
	 * 每读一个字节，就移动 head，并减少 count。
	 */
	while (read_bytes < len && count > 0) {
		ch = ring_buffer[head];

		/*
		 * 内核不能直接访问用户空间地址，必须使用 copy_to_user。
		 * 如果复制失败，返回已经成功读取的字节数；如果一个字节都没成功，
		 * 返回 -EFAULT 表示用户空间地址无效。
		 */
		if (copy_to_user(user_buf + read_bytes, &ch, 1)) {
			mutex_unlock(&ringbuf_mutex);
			pr_err("ringbuf: copy_to_user failed\n");
			return read_bytes ? (ssize_t)read_bytes : -EFAULT;
		}

		head = (head + 1) % BUFFER_SIZE;
		count--;
		read_bytes++;
	}

	mutex_unlock(&ringbuf_mutex);

	pr_info("ringbuf: read %zu bytes\n", read_bytes);
	return read_bytes;
}

/*
 * write 回调函数
 *
 * 用户程序调用 write(fd, buf, len) 时，内核会调用本函数。
 * 本函数把用户空间传来的数据写入内核环形缓冲区。
 */
static ssize_t ringbuf_write(struct file *file, const char __user *user_buf,
			     size_t len, loff_t *offset)
{
	size_t written = 0;
	char ch;

	if (len == 0)
		return 0;

	if (mutex_lock_interruptible(&ringbuf_mutex))
		return -ERESTARTSYS;

	/*
	 * 逐字节写入环形缓冲区。
	 * 如果缓冲区满了，停止写入，并返回已经写入的字节数。
	 */
	while (written < len && count < BUFFER_SIZE) {
		/*
		 * 内核不能直接读取用户空间地址，必须使用 copy_from_user。
		 * 这里每次从用户空间复制 1 个字节到内核变量 ch。
		 */
		if (copy_from_user(&ch, user_buf + written, 1)) {
			mutex_unlock(&ringbuf_mutex);
			pr_err("ringbuf: copy_from_user failed\n");
			return written ? (ssize_t)written : -EFAULT;
		}

		ring_buffer[tail] = ch;
		tail = (tail + 1) % BUFFER_SIZE;
		count++;
		written++;
	}

	mutex_unlock(&ringbuf_mutex);

	pr_info("ringbuf: write %zu bytes\n", written);
	return written;
}

/*
 * file_operations 是字符设备驱动的核心接口表。
 * 用户空间对 /dev/ringbuf 执行 open/read/write/close 时，
 * 内核会根据这里的函数指针调用对应驱动函数。
 */
static const struct file_operations ringbuf_fops = {
	.owner = THIS_MODULE,
	.open = ringbuf_open,
	.release = ringbuf_release,
	.read = ringbuf_read,
	.write = ringbuf_write,
};

/*
 * 模块初始化函数
 * 执行 sudo insmod ringbuf_driver.ko 时会被调用。
 */
static int __init ringbuf_init(void)
{
	int ret;
	struct device *ringbuf_device;

	/* 初始化环形缓冲区状态 */
	head = 0;
	tail = 0;
	count = 0;

	/* 1. 动态申请设备号，避免手动指定主设备号冲突。 */
	ret = alloc_chrdev_region(&ringbuf_dev, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("ringbuf: failed to allocate device number\n");
		return ret;
	}

	/* 2. 初始化 cdev，并绑定 file_operations。 */
	cdev_init(&ringbuf_cdev, &ringbuf_fops);
	ringbuf_cdev.owner = THIS_MODULE;

	/* 3. 将 cdev 注册到内核。 */
	ret = cdev_add(&ringbuf_cdev, ringbuf_dev, 1);
	if (ret < 0) {
		pr_err("ringbuf: failed to add cdev\n");
		unregister_chrdev_region(ringbuf_dev, 1);
		return ret;
	}

	/*
	 * 4. 创建设备类。
	 * Linux 6.4 及以后版本 class_create 只有一个参数；
	 * 较老版本 class_create 需要 THIS_MODULE 和类名两个参数。
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	ringbuf_class = class_create(CLASS_NAME);
#else
	ringbuf_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(ringbuf_class)) {
		ret = PTR_ERR(ringbuf_class);
		pr_err("ringbuf: failed to create class\n");
		cdev_del(&ringbuf_cdev);
		unregister_chrdev_region(ringbuf_dev, 1);
		return ret;
	}

	/* 5. 创建设备节点，成功后 /dev 下会出现 /dev/ringbuf。 */
	ringbuf_device = device_create(ringbuf_class, NULL, ringbuf_dev, NULL,
				       DEVICE_NAME);
	if (IS_ERR(ringbuf_device)) {
		ret = PTR_ERR(ringbuf_device);
		pr_err("ringbuf: failed to create device\n");
		class_destroy(ringbuf_class);
		cdev_del(&ringbuf_cdev);
		unregister_chrdev_region(ringbuf_dev, 1);
		return ret;
	}

	pr_info("ringbuf: driver loaded, major=%d minor=%d\n",
		MAJOR(ringbuf_dev), MINOR(ringbuf_dev));
	return 0;
}

/*
 * 模块卸载函数
 * 执行 sudo rmmod ringbuf_driver 时会被调用。
 */
static void __exit ringbuf_exit(void)
{
	/* 卸载顺序一般与加载顺序相反，确保资源完整释放。 */
	device_destroy(ringbuf_class, ringbuf_dev);
	class_destroy(ringbuf_class);
	cdev_del(&ringbuf_cdev);
	unregister_chrdev_region(ringbuf_dev, 1);

	pr_info("ringbuf: driver unloaded\n");
}

module_init(ringbuf_init);
module_exit(ringbuf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Lab");
MODULE_DESCRIPTION("A simple Linux character device driver with ring buffer");
MODULE_VERSION("1.0");
