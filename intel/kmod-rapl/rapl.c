
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/cpu.h>
#include <linux/notifier.h>

#include <asm/msr.h>

MODULE_AUTHOR("Dorian Krause");
MODULE_DESCRIPTION("RAPL MRS interface");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");

static dev_t _rapl_devt;
static struct cdev *_rapl_devs[NR_CPUS];
static struct class *_rapl_class;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)

static loff_t no_seek_end_llseek(struct file *filp, loff_t offset, int whence)
{
	if ((SEEK_SET == whence) || (SEEK_CUR == whence)) {
		return generic_file_llseek_size(filp, offset, whence, OFFSET_MAX, 0);
	}

	return -EINVAL;
}

#endif

/* Probably not the correct version to compare against but good enough to distinguish between
 * the 3.10 kernel in RHEL7 and the 2.6.32 kernel in RHEL6.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)

static struct inode *file_inode(struct file *filp)
{
	return filp->f_mapping->host;
}

#define cpu_notifier_register_begin()	do { } while(0)
#define cpu_notifier_register_done()	do { } while(0)
#define __register_cpu_notifier(X)	register_hotcpu_notifier(X)
#define __unregister_cpu_notifier(X)	unregister_hotcpu_notifier(X)

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
#	define MODE	mode_t
#else
#	define MODE	umode_t
#endif

static int _allow_access_to_msr(u32 msr)
{
	if ((msr == 0x606) ||	/* MSR_RAPL_POWER_UNIT */
	    (msr == 0x611) ||	/* PKG energy status */
	    (msr == 0x619) ||	/* DRAM energy status */
	    (msr == 0x639) ||	/* PP0 energy status */
	    (msr == 0x641)) {	/* PP1 energy status */
		return 1;
	}

	return 0;
}

static ssize_t _rapl_do_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	int err;
	u32 data[2];
	u32 msr = (u32 )*offp;
	int cpu = iminor(file_inode(filp));

	if (8 != count) {
		err = -EINVAL;
		goto out;
	}
	if (unlikely(!_allow_access_to_msr(msr))) {
		err = -EPERM;
		goto out;
	}

	err = rdmsr_safe_on_cpu(cpu, msr, &data[0], &data[1]);
	if (unlikely(err)) {
		goto out;
	}

	if (copy_to_user((u32 __user *)buff, &data, 8)) {
		err = -EFAULT;
		goto out;
	}

	err = 8;	/* number of bytes */
	goto out;

out:
	return err;
}

static int _rapl_do_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations _rapl_fops =
{
	.owner  = THIS_MODULE,
	.llseek = no_seek_end_llseek,
	.read   = _rapl_do_read,
	.open   = _rapl_do_open
};

static int _setup_device(int cpu, struct cdev **cdev)
{
	int err = 0;
	struct device *devp;

	(*cdev) = cdev_alloc();
	if (unlikely(!(*cdev))) {
		pr_err("cdev_alloc() returned NULL");
		err = -ENOMEM;
		goto out;
	}

	(*cdev)->owner = THIS_MODULE;
	(*cdev)->ops   = &_rapl_fops;

	err = cdev_add((*cdev), MKDEV(MAJOR(_rapl_devt), cpu), 1);
	if (unlikely(err)) {
		pr_err("cdev_add() failed");
		goto fail;
	}

	devp = device_create(_rapl_class, NULL, MKDEV(MAJOR(_rapl_devt), cpu), NULL, "rapl%d", cpu);
	if (unlikely(IS_ERR(devp))) {
		pr_err("device_create() failed");
		err = PTR_ERR_OR_ZERO(devp);
		goto fail;
	}

	err = 0;
	goto out;

fail:
	cdev_del((*cdev));
	(*cdev) = NULL;
out:
	return err;
}

static void _teardown_device(int cpu, struct cdev **dev)
{
	if (likely((*dev))) {
		device_destroy(_rapl_class, MKDEV(MAJOR(_rapl_devt), cpu));

		cdev_del((*dev));
		(*dev) = NULL;
	}
}

static char *_rapl_devtde(struct device *dev, MODE *mode)
{
	return kasprintf(GFP_KERNEL, "cpu/%u/rapl", MINOR(dev->devt));
}

static int _rapl_cpu_callback_notifier(struct notifier_block *self,
                                       unsigned long action, void *data)
{
	int err = 0;
	unsigned int cpu = *((unsigned int *)data);

	switch (action)
	{
	case CPU_UP_PREPARE:
		err = _setup_device(cpu, &_rapl_devs[cpu]);
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
		_teardown_device(cpu, &_rapl_devs[cpu]);
		err = 0;
		break;
	}

	return notifier_from_errno(err);
}

struct notifier_block __refdata _rapl_cpu_callback =
{
	.notifier_call = _rapl_cpu_callback_notifier
};

static int __init rapl_init(void)
{
	int err = 0;
	int i;

	memset(_rapl_devs, 0, sizeof(_rapl_devs));

	err = alloc_chrdev_region(&_rapl_devt, 0, NR_CPUS, "rapl");
	if (unlikely(err < 0)) {
		pr_err("alloc_chrdev_region() failed\n");
		err = -EBUSY;
		goto out;
	}

	_rapl_class = class_create(THIS_MODULE, "rapl");
	if (unlikely(IS_ERR(_rapl_class))) {
		pr_err("class_create() failed");
		err = PTR_ERR(_rapl_class);
		goto fail;
	}

	_rapl_class->devnode = _rapl_devtde;

	cpu_notifier_register_begin();
	for_each_online_cpu(i) {
		err = _setup_device(i, &_rapl_devs[i]);
		if (unlikely(err)) {
			goto fail;
		}
	}
	__register_cpu_notifier(&_rapl_cpu_callback);
	cpu_notifier_register_done();

	err = 0;
	goto out;

fail:
	cpu_notifier_register_begin();
	for_each_online_cpu(i) {
		_teardown_device(i, &_rapl_devs[i]);
	}
	__unregister_cpu_notifier(&_rapl_cpu_callback);
	cpu_notifier_register_done();
	if (_rapl_class) {
		class_destroy(_rapl_class);
	}
	unregister_chrdev(MAJOR(_rapl_devt), "rapl");
out:
	return err;
}

static void __exit rapl_exit(void)
{
	int i;

	cpu_notifier_register_begin();
	for_each_online_cpu(i) {
		_teardown_device(i, &_rapl_devs[i]);
	}
	__unregister_cpu_notifier(&_rapl_cpu_callback);
	cpu_notifier_register_done();
	class_destroy(_rapl_class);
	unregister_chrdev(MAJOR(_rapl_devt), "rapl");
}

module_init(rapl_init);
module_exit(rapl_exit);

