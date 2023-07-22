//
// Created by kingdo on 23-7-20.
//

#ifndef _LINUX_VIRTIO_FAASCALE_MEM_H
#define _LINUX_VIRTIO_FAASCALE_MEM_H

#include <linux/types.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

/* The feature bitmap for virtio balloon */
#define VIRTIO_FAASCALE_MEM_F_MUST_TELL_HOST	0 /* Tell before reclaiming pages */
#define VIRTIO_FAASCALE_MEM_F_STATS_VQ	1 /* Memory Stats virtqueue */
#define VIRTIO_FAASCALE_MEM_F_PAGE_POISON	2 /* Guest is using page poisoning */

struct virtio_faascale_mem_config {
	/* Number of pages host wants Guest to give up. */
	__le32 num_pages;

	/* Number of pages we've actually got in balloon. */
	__le32 actual;

	/* Stores PAGE_POISON if page poisoning is in use */
	__le32 poison_val;
};

#define VIRTIO_FAASCALE_MEM_S_SWAP_IN  0   /* Amount of memory swapped in */
#define VIRTIO_FAASCALE_MEM_S_SWAP_OUT 1   /* Amount of memory swapped out */
#define VIRTIO_FAASCALE_MEM_S_MAJFLT   2   /* Number of major faults */
#define VIRTIO_FAASCALE_MEM_S_MINFLT   3   /* Number of minor faults */
#define VIRTIO_FAASCALE_MEM_S_MEMFREE  4   /* Total amount of free memory */
#define VIRTIO_FAASCALE_MEM_S_MEMTOT   5   /* Total amount of memory */
#define VIRTIO_FAASCALE_MEM_S_AVAIL    6   /* Available memory as in /proc */
#define VIRTIO_FAASCALE_MEM_S_CACHES   7   /* Disk caches */
#define VIRTIO_FAASCALE_MEM_S_HTLB_PGALLOC  8  /* Hugetlb page allocations */
#define VIRTIO_FAASCALE_MEM_S_HTLB_PGFAIL   9  /* Hugetlb page allocation failures */
#define VIRTIO_FAASCALE_MEM_S_NR       10

#define VIRTIO_FAASCALE_MEM_S_NAMES_WITH_PREFIX(VIRTIO_FAASCALE_MEM_S_NAMES_prefix) { \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "swap-in", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "swap-out", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "major-faults", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "minor-faults", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "free-memory", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "total-memory", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "available-memory", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "disk-caches", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "hugetlb-allocations", \
	VIRTIO_FAASCALE_MEM_S_NAMES_prefix "hugetlb-failures" \
}

#define VIRTIO_FAASCALE_MEM_S_NAMES VIRTIO_FAASCALE_MEM_S_NAMES_WITH_PREFIX("")

/*
 * Memory statistics structure.
 * Driver fills an array of these structures and passes to device.
 *
 * NOTE: fields are laid out in a way that would make compiler add padding
 * between and after fields, so we have to use compiler-specific attributes to
 * pack it, to disable this padding. This also often causes compiler to
 * generate suboptimal code.
 *
 * We maintain this statistics structure format for backwards compatibility,
 * but don't follow this example.
 *
 * If implementing a similar structure, do something like the below instead:
 *     struct virtio_faascale_mem_stat {
 *         __virtio16 tag;
 *         __u8 reserved[6];
 *         __virtio64 val;
 *     };
 *
 * In other words, add explicit reserved fields to align field and
 * structure boundaries at field size, avoiding compiler padding
 * without the packed attribute.
 */
struct virtio_faascale_mem_stat {
	__virtio16 tag;
	__virtio64 val;
} __attribute__((packed));

#endif //_LINUX_VIRTIO_FAASCALE_MEM_H
