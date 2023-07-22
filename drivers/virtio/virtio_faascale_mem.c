//
// Created by kingdo on 23-7-20.
//
#include <linux/virtio.h>
#include <linux/virtio_faascale_mem.h>
#include <linux/swap.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/balloon_compaction.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/magic.h>
#include <linux/pseudo_fs.h>
#include <linux/page_reporting.h>

#define VIRTIO_FAASCALE_MEM_ARRAY_BLOCKS_MAX 128


struct virtio_faascale_mem *global_vfs __read_mostly;

enum virtio_faascale_mem_vq {
	VIRTIO_FAASCALE_MEM_VQ_POPULATE,
	VIRTIO_FAASCALE_MEM_VQ_DEPOPULATE,
	VIRTIO_FAASCALE_MEM_VQ_STATS,
	VIRTIO_FAASCALE_MEM_VQ_MAX
};

enum virtio_faascale_mem_config_read {
	VIRTIO_BALLOON_CONFIG_READ_CMD_ID = 0,
};

struct virtio_faascale_mem_block{
	__virtio32 start_pfn;
	__virtio32 size;
};

struct virtio_faascale_mem {
	struct virtio_device *vdev;
	struct virtqueue *populate_vq, *depopulate_vq, *stats_vq;

	/* The balloon servicing is delegated to a freezable workqueue. */
	struct work_struct update_faascale_mem_stats_work;

	/* Prevent updating balloon when it is being canceled. */
	spinlock_t stop_update_lock;
	bool stop_update;
	/* Bitmap to indicate if reading the related config fields are needed */
	unsigned long config_read_bitmap;

	/* Synchronize access/update to this struct virtio_balloon elements */
	struct mutex faascale_mem_lock;

	struct virtio_faascale_mem_block faascale_mem_blocks[VIRTIO_FAASCALE_MEM_ARRAY_BLOCKS_MAX];
	unsigned int num_faascale_mem_blocks;

	/* Waiting for host to ack the pages we released. */
	wait_queue_head_t acked;

	/* Memory statistics */
	struct virtio_faascale_mem_stat stats[VIRTIO_FAASCALE_MEM_S_NR];

};

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_FAASCALE_MEM, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void balloon_ack(struct virtqueue *vq)
{
	struct virtio_faascale_mem *vfm = vq->vdev->priv;

	wake_up(&vfm->acked);
}

static void tell_host(struct virtio_faascale_mem *vfm, struct virtqueue *vq)
{
	struct scatterlist sg;
	unsigned int len;

	sg_init_one(&sg, vfm->faascale_mem_blocks, sizeof(vfm->faascale_mem_blocks[0]) * vfm->num_faascale_mem_blocks);

	/* We should always be able to add one buffer to an empty queue. */
	virtqueue_add_outbuf(vq, &sg, 1, vfm, GFP_KERNEL);
	virtqueue_kick(vq);

	/* When host has read buffer, this completes via balloon_ack */
	wait_event(vfm->acked, virtqueue_get_buf(vq, &len));
}

/// un/populate faascale block 之后，修改 balloon config
static void update_faascale_block(struct virtio_faascale_mem *vfm)
{
	//	u32 actual = vfm->num_pages;
	//
	//	/* Legacy balloon config space is LE, unlike all other devices. */
	//	virtio_cwrite_le(vfm->vdev, struct virtio_balloon_config, actual,
	//			 &actual);
}


int scale_faascale_block(struct list_head *block_list, bool pop)
{
	struct virtio_faascale_mem *vfm = global_vfs;
	struct faascale_mem_block *block;
	
	BUG_ON(vfm == NULL);

	if (vfm->stop_update) {
		return -1;
	}

	spin_lock_irq(&vfm->stop_update_lock);
	if (vfm->stop_update) {
		spin_unlock_irq(&vfm->stop_update_lock);
		return -1;
	}

	mutex_lock(&vfm->faascale_mem_lock);

	vfm->num_faascale_mem_blocks = 0;
	list_for_each_entry (block, block_list, list) {
		if(unlikely(vfm->num_faascale_mem_blocks == VIRTIO_FAASCALE_MEM_ARRAY_BLOCKS_MAX)){
			tell_host(vfm, vfm->depopulate_vq);
			vfm->num_faascale_mem_blocks = 0;
		}
		vfm->faascale_mem_blocks[vfm->num_faascale_mem_blocks].size = cpu_to_virtio32(vfm->vdev, block->managed_pages);
		vfm->faascale_mem_blocks[vfm->num_faascale_mem_blocks].start_pfn = cpu_to_virtio32(vfm->vdev, block->block_start_pfn);
		pr_info("KINGDO: %s Block: start_pfn=%u, size=%u\n", pop ? "Populate" : "Depopulate",
			cpu_to_virtio32(vfm->vdev, block->block_start_pfn),
			cpu_to_virtio32(vfm->vdev, block->managed_pages));
		vfm->num_faascale_mem_blocks++;
	}
	
	if(likely(vfm->num_faascale_mem_blocks > 0)){
		tell_host(vfm, pop ? vfm->populate_vq : vfm->depopulate_vq);
		vfm->num_faascale_mem_blocks = 0;
	}

	mutex_unlock(&vfm->faascale_mem_lock);
	spin_unlock_irq(&vfm->stop_update_lock);
	update_faascale_block(vfm);
	return 0;
}

bool block_populate_check(struct list_head *block_list){
	struct faascale_mem_block *block;
	void *addr;
	list_for_each_entry (block, block_list, list) {
		addr = page_to_virt(pfn_to_page(block->block_start_pfn));
		pr_info("KINGDO: Check Block(%lu,%lu,%u): %s\n", block->block_start_pfn, block->managed_pages,
			block->order, (char *)addr);
	}
	return !strcmp(addr, "KINGDO");
}

bool virtio_faascale_mem_is_enable(void){
	return global_vfs !=NULL && !global_vfs->stop_update;
}

static inline void update_stat(struct virtio_faascale_mem *vfm, int idx,
			       u16 tag, u64 val)
{
	BUG_ON(idx >= VIRTIO_FAASCALE_MEM_S_NR);
	vfm->stats[idx].tag = cpu_to_virtio16(vfm->vdev, tag);
	vfm->stats[idx].val = cpu_to_virtio64(vfm->vdev, val);
}

#define pages_to_bytes(x) ((u64)(x) << PAGE_SHIFT)

static unsigned int update_faascale_mem_stats(struct virtio_faascale_mem *vfm)
{
	unsigned long events[NR_VM_EVENT_ITEMS];
	struct sysinfo i;
	unsigned int idx = 0;
	long available;
	unsigned long caches;

	all_vm_events(events);
	si_meminfo(&i);

	available = si_mem_available();
	caches = global_node_page_state(NR_FILE_PAGES);

#ifdef CONFIG_VM_EVENT_COUNTERS
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_SWAP_IN,
		    pages_to_bytes(events[PSWPIN]));
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_SWAP_OUT,
		    pages_to_bytes(events[PSWPOUT]));
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_MAJFLT, events[PGMAJFAULT]);
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_MINFLT, events[PGFAULT]);
#ifdef CONFIG_HUGETLB_PAGE
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_HTLB_PGALLOC,
		    events[HTLB_BUDDY_PGALLOC]);
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_HTLB_PGFAIL,
		    events[HTLB_BUDDY_PGALLOC_FAIL]);
#endif
#endif
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_MEMFREE,
		    pages_to_bytes(i.freeram));
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_MEMTOT,
		    pages_to_bytes(i.totalram));
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_AVAIL,
		    pages_to_bytes(available));
	update_stat(vfm, idx++, VIRTIO_FAASCALE_MEM_S_CACHES,
		    pages_to_bytes(caches));

	return idx;
}

/*
 * While most virtqueues communicate guest-initiated requests to the hypervisor,
 * the stats queue operates in reverse.  The driver initializes the virtqueue
 * with a single buffer.  From that point forward, all conversations consist of
 * a hypervisor request (a call to this function) which directs us to refill
 * the virtqueue with a fresh stats buffer.  Since stats collection can sleep,
 * we delegate the job to a freezable workqueue that will do the actual work via
 * stats_handle_request().
 */
static void stats_request(struct virtqueue *vq)
{
	struct virtio_faascale_mem *vfm = vq->vdev->priv;

	spin_lock(&vfm->stop_update_lock);
	if (!vfm->stop_update)
		queue_work(system_freezable_wq, &vfm->update_faascale_mem_stats_work);
	spin_unlock(&vfm->stop_update_lock);
}

static void stats_handle_request(struct virtio_faascale_mem *vfm)
{
	struct virtqueue *vq;
	struct scatterlist sg;
	unsigned int len, num_stats;

	num_stats = update_faascale_mem_stats(vfm);

	vq = vfm->stats_vq;
	if (!virtqueue_get_buf(vq, &len))
		return;
	sg_init_one(&sg, vfm->stats, sizeof(vfm->stats[0]) * num_stats);
	virtqueue_add_outbuf(vq, &sg, 1, vfm, GFP_KERNEL);
	virtqueue_kick(vq);
}

static void update_faascale_mem_stats_func(struct work_struct *work)
{
	struct virtio_faascale_mem *vfm;

	vfm = container_of(work, struct virtio_faascale_mem,
			   update_faascale_mem_stats_work);
	stats_handle_request(vfm);
}


static int init_vqs(struct virtio_faascale_mem *vfm)
{
	struct virtqueue *vqs[VIRTIO_FAASCALE_MEM_VQ_MAX];
	vq_callback_t *callbacks[VIRTIO_FAASCALE_MEM_VQ_MAX];
	const char *names[VIRTIO_FAASCALE_MEM_VQ_MAX];
	int err;

	/*
	 * Inflateq and deflateq are used unconditionally. The names[]
	 * will be NULL if the related feature is not enabled, which will
	 * cause no allocation for the corresponding virtqueue in find_vqs.
	 */
	callbacks[VIRTIO_FAASCALE_MEM_VQ_POPULATE] = balloon_ack;
	names[VIRTIO_FAASCALE_MEM_VQ_POPULATE] = "populate";
	callbacks[VIRTIO_FAASCALE_MEM_VQ_DEPOPULATE] = balloon_ack;
	names[VIRTIO_FAASCALE_MEM_VQ_DEPOPULATE] = "depopulate";
	callbacks[VIRTIO_FAASCALE_MEM_VQ_STATS] = NULL;
	names[VIRTIO_FAASCALE_MEM_VQ_STATS] = NULL;

	if (virtio_has_feature(vfm->vdev, VIRTIO_FAASCALE_MEM_F_STATS_VQ)) {
		names[VIRTIO_FAASCALE_MEM_VQ_STATS] = "stats";
		callbacks[VIRTIO_FAASCALE_MEM_VQ_STATS] = stats_request;
	}


	err = vfm->vdev->config->find_vqs(vfm->vdev, VIRTIO_FAASCALE_MEM_VQ_MAX,
					 vqs, callbacks, names, NULL, NULL);
	if (err)
		return err;

	vfm->populate_vq = vqs[VIRTIO_FAASCALE_MEM_VQ_POPULATE];
	vfm->depopulate_vq = vqs[VIRTIO_FAASCALE_MEM_VQ_DEPOPULATE];
	if (virtio_has_feature(vfm->vdev, VIRTIO_FAASCALE_MEM_F_STATS_VQ)) {
		struct scatterlist sg;
		unsigned int num_stats;
		vfm->stats_vq = vqs[VIRTIO_FAASCALE_MEM_VQ_STATS];

		/*
		 * Prime this virtqueue with one buffer so the hypervisor can
		 * use it to signal us later (it can't be broken yet!).
		 */
		num_stats = update_faascale_mem_stats(vfm);

		sg_init_one(&sg, vfm->stats, sizeof(vfm->stats[0]) * num_stats);
		err = virtqueue_add_outbuf(vfm->stats_vq, &sg, 1, vfm,
					   GFP_KERNEL);
		if (err) {
			dev_warn(&vfm->vdev->dev, "%s: add stat_vq failed\n",
				 __func__);
			return err;
		}
		virtqueue_kick(vfm->stats_vq);
	}

	return 0;
}


static int virtfaascalemem_probe(struct virtio_device *vdev)
{
	struct virtio_faascale_mem *vfm;
	int err;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	vdev->priv = vfm = kzalloc(sizeof(*vfm), GFP_KERNEL);
	if (!vfm) {
		err = -ENOMEM;
		goto out;
	}

	pr_info("KINGDO: virtfaascalemem_probe\n");
	
	global_vfs = vfm;

	INIT_WORK(&vfm->update_faascale_mem_stats_work, update_faascale_mem_stats_func);
	spin_lock_init(&vfm->stop_update_lock);
	mutex_init(&vfm->faascale_mem_lock);
	init_waitqueue_head(&vfm->acked);
	vfm->vdev = vdev;

	err = init_vqs(vfm);
	if (err)
		goto out_free_vfm;

	if (virtio_has_feature(vdev, VIRTIO_FAASCALE_MEM_F_PAGE_POISON)) {
		/* Start with poison val of 0 representing general init */
		__u32 poison_val = 0;

		/*
		 * Let the hypervisor know that we are expecting a
		 * specific value to be written back in balloon pages.
		 *
		 * If the PAGE_POISON value was larger than a byte we would
		 * need to byte swap poison_val here to guarantee it is
		 * little-endian. However for now it is a single byte so we
		 * can pass it as-is.
		 */
		if (!want_init_on_free())
			memset(&poison_val, PAGE_POISON, sizeof(poison_val));

		virtio_cwrite_le(vfm->vdev, struct virtio_faascale_mem_config,
				 poison_val, &poison_val);
	}

	virtio_device_ready(vdev);

	return 0;

out_free_vfm:
	kfree(vfm);
	global_vfs = NULL;
out:
	return err;
}

static void remove_common(struct virtio_faascale_mem *vfm)
{
	/* Now we reset the device so we can clean up the queues. */
	vfm->vdev->config->reset(vfm->vdev);

	vfm->vdev->config->del_vqs(vfm->vdev);
}

static void virtfaascalemem_remove(struct virtio_device *vdev)
{
	struct virtio_faascale_mem *vfm = vdev->priv;
	
	spin_lock_irq(&vfm->stop_update_lock);
	vfm->stop_update = true;
	spin_unlock_irq(&vfm->stop_update_lock);
	cancel_work_sync(&vfm->update_faascale_mem_stats_work);

	remove_common(vfm);
	kfree(vfm);
}

#ifdef CONFIG_PM_SLEEP
static int virtfaascalemem_freeze(struct virtio_device *vdev)
{
	struct virtio_faascale_mem *vfm = vdev->priv;

	/*
	 * The workqueue is already frozen by the PM core before this
	 * function is called.
	 */
	remove_common(vfm);
	return 0;
}

static int virtfaascalemem_restore(struct virtio_device *vdev)
{
//	struct virtio_faascale_mem *vfm = vdev->priv;
	int ret;

	ret = init_vqs(vdev->priv);
	if (ret)
		return ret;

	virtio_device_ready(vdev);

	return 0;
}
#endif

static void virtfaascalemem_changed(struct virtio_device *vdev)
{
//	struct virtio_faascale_mem *vfm = vdev->priv;
//	unsigned long flags;
}

static int virt_faascale_mem_validate(struct virtio_device *vdev)
{
	/*
	 * Inform the hypervisor that our pages are poisoned or
	 * initialized. If we cannot do that then we should disable
	 * page reporting as it could potentially change the contents
	 * of our free pages.
	 */
	if (!want_init_on_free() &&
	    (IS_ENABLED(CONFIG_PAGE_POISONING_NO_SANITY) ||
	     !page_poisoning_enabled()))
		__virtio_clear_bit(vdev, VIRTIO_FAASCALE_MEM_F_PAGE_POISON);

	__virtio_clear_bit(vdev, VIRTIO_F_ACCESS_PLATFORM);
	return 0;
}

static unsigned int features[] = {
	VIRTIO_FAASCALE_MEM_F_MUST_TELL_HOST,
	VIRTIO_FAASCALE_MEM_F_STATS_VQ,
	VIRTIO_FAASCALE_MEM_F_PAGE_POISON,
};

static struct virtio_driver virtio_faascale_mem_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.validate =	virt_faascale_mem_validate,
	.probe =	virtfaascalemem_probe,
	.remove =	virtfaascalemem_remove,
	.config_changed = virtfaascalemem_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze	=	virtfaascalemem_freeze,
	.restore =	virtfaascalemem_restore,
#endif
};

module_virtio_driver(virtio_faascale_mem_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio faascale mem driver");
MODULE_LICENSE("GPL");