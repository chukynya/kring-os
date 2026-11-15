// SPDX-License-Identifier: GPL-2.0
/* Kernel module for /proc/timeshare system call telemetry */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define PROC_NAME	"timeshare"
#define EVENT_BUF_LEN	128

/* Protects event_count and last_event across concurrent syscalls */
static DEFINE_MUTEX(timeshare_lock);
static int event_count;
static char last_event[EVENT_BUF_LEN] = "none";

/* Handle read() system calls on /proc/timeshare (e.g. cat /proc/timeshare) */
static int timeshare_proc_show(struct seq_file *m, void *v)
{
	mutex_lock(&timeshare_lock);
	seq_printf(m, "Kernel Module : %s_kmod (ACTIVE)\n", PROC_NAME);
	seq_printf(m, "Events Logged : %d\n", event_count);
	seq_printf(m, "Last Event    : %s\n", last_event);
	mutex_unlock(&timeshare_lock);
	return 0;
}

static int timeshare_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, timeshare_proc_show, NULL);
}

/* Handle write() system calls from user-space Round-Robin scheduler */
static ssize_t timeshare_proc_write(struct file *file, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	char kbuf[EVENT_BUF_LEN];
	size_t len;

	if (count == 0)
		return 0;

	len = (count >= EVENT_BUF_LEN) ? (EVENT_BUF_LEN - 1) : count;
	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;

	kbuf[len] = '\0';
	if (kbuf[len - 1] == '\n')
		kbuf[len - 1] = '\0';

	mutex_lock(&timeshare_lock);
	event_count++;
	snprintf(last_event, sizeof(last_event), "%s", kbuf);
	pr_info("timeshare_kmod: event #%d: %s\n", event_count, last_event);
	mutex_unlock(&timeshare_lock);
	return count;
}

static const struct proc_ops timeshare_proc_ops = {
	.proc_open	= timeshare_proc_open,
	.proc_read	= seq_read,
	.proc_write	= timeshare_proc_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init timeshare_kmod_init(void)
{
	if (!proc_create("timeshare", 0666, NULL, &timeshare_proc_ops))
		return -ENOMEM;

	pr_info("timeshare_kmod: /proc/%s registered\n", PROC_NAME);
	return 0;
}

static void __exit timeshare_kmod_exit(void)
{
	remove_proc_entry(PROC_NAME, NULL);
	pr_info("timeshare_kmod: /proc/%s removed (events=%d)\n",
		PROC_NAME, event_count);
}

module_init(timeshare_kmod_init);
module_exit(timeshare_kmod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Student");
MODULE_DESCRIPTION("Time-Sharing Scheduler /proc System Call Integration");
