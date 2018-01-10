/*
 *  linux/fs/minix/bitmap.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Modified for 680x0 by Hamish Macdonald
 * Fixed for 680x0 by Andreas Schwab
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */

#include "minix.h"
#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include <linux/sched.h>

static DEFINE_SPINLOCK(bitmap_lock);

/**
 * Get the refcount of a particular data block
 */
inline uint32_t get_refcount(struct minix_sb_info *sbi, size_t data_block_index) {
	// This could be easier by simply casting sbi->s_refcount_table[0] to uint32_t*
	// And indexing it with data_block_index, but that's kind of dirty
	// since we'd be reading over the actual size of the buffer_head.
	// So we do the same procedure as in set_refcount...

	// Get index of block and index within block
	size_t block_size = sbi->s_refcount_table[0]->b_size;
	uint32_t refcounts_per_block = block_size / sizeof(uint32_t);
	size_t block_index = data_block_index / refcounts_per_block;
	size_t entry_index = data_block_index % refcounts_per_block;

	// Get refcounter
	struct buffer_head *refcount_table_block = sbi->s_refcount_table[block_index];
	uint32_t *refcount_table_section = (uint32_t*)refcount_table_block->b_data;
	return refcount_table_section[entry_index]; 
}

/**
 * Set the refcount of a particular data block
 */
inline void set_refcount(struct minix_sb_info *sbi, size_t data_block_index, uint32_t value) {
	// Get index of block and index within block
	size_t block_size = sbi->s_refcount_table[0]->b_size;
	uint32_t refcounts_per_block = block_size / sizeof(uint32_t);
	size_t block_index = data_block_index / refcounts_per_block;
	size_t entry_index = data_block_index % refcounts_per_block;

	// Set refcounter
	struct buffer_head *refcount_table_block = sbi->s_refcount_table[block_index];
	uint32_t *refcount_table_section = (uint32_t*)refcount_table_block->b_data;
	refcount_table_section[entry_index] = value;
	mark_buffer_dirty(refcount_table_block);

	debug_log("Set refcount of data block %d to %d\n", data_block_index, refcount_table_section[entry_index]);
}

/**
 * Increments the refcount of a particular data block
 * Returns the new refcount
 */
inline uint32_t increment_refcount(struct minix_sb_info *sbi, size_t data_block_index) {
	uint32_t refcount = get_refcount(sbi, data_block_index);
	if (refcount+1 != 0) {
		set_refcount(sbi, data_block_index, refcount+1);
	} else {
		debug_log("ERROR: Overflow in refcount for data block %d\n", data_block_index);
	}
	return get_refcount(sbi, data_block_index);
}

/**
 * Decrements the refcount of a particular data block
 * Returns the new refcount
 */
inline uint32_t decrement_refcount(struct minix_sb_info *sbi, size_t data_block_index) {
	uint32_t refcount = get_refcount(sbi, data_block_index);
	if (refcount != 0) {
		set_refcount(sbi, data_block_index, refcount-1);
	} else {
		debug_log("ERROR: Underflow in refcount for data block %d\n", data_block_index);
	}
	return get_refcount(sbi, data_block_index);
}

/**
 * Transforms a zone number into a data zone index
 */
inline uint32_t data_zone_index_for_zone_number(struct minix_sb_info *sbi, size_t zone_number) {
	return zone_number - sbi->s_firstdatazone + 1;
}

/*
 * Increments the refcounts of an indirect block and all referenced data blocks
 */
void increment_refcounts_on_indirect_block(struct super_block *sb, uint32_t physical_block_number) {
	size_t data_zone_index;
	uint32_t *indirect_block;
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t n_blockrefs_in_block = sb->s_blocksize / sizeof(uint32_t);
	struct buffer_head *bh;
	size_t i;

	// Increase refcount on indirect block
	data_zone_index = data_zone_index_for_zone_number(sbi, physical_block_number);
	increment_refcount(sbi, data_zone_index);

	// Increase refcount on all data blocks
	bh = sb_bread(sb, physical_block_number);
	indirect_block = (uint32_t*) bh->b_data;

	for (i = 0; i < n_blockrefs_in_block; i++) {
		if (indirect_block[i] != 0) {
			// Increase refcount on data block
			data_zone_index = data_zone_index_for_zone_number(sbi, indirect_block[i]);
			increment_refcount(sbi, data_zone_index);
		}
	}
}

/*
 * bitmap consists of blocks filled with 16bit words
 * bit set == busy, bit clear == free
 * endianness is a mess, but for counting zero bits it really doesn't matter...
 */
static __u32 count_free(struct buffer_head *map[], unsigned blocksize, __u32 numbits)
{
	__u32 sum = 0;
	unsigned blocks = DIV_ROUND_UP(numbits, blocksize * 8);

	while (blocks--) {
		unsigned words = blocksize / 2;
		__u16 *p = (__u16 *)(*map++)->b_data;
		while (words--)
			sum += 16 - hweight16(*p++);
	}

	// TODO: Maybe we should also count the free zones according to the refcount table
	// and log a warning if they are not the same

	return sum;
}

void minix_free_block(struct inode *inode, unsigned long block)
{
	struct super_block *sb = inode->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	uint32_t refcount_table_index;
	unsigned long bit, zone;

	if (block < sbi->s_firstdatazone || block >= sbi->s_nzones) {
		printk("Trying to free block not in datazone\n");
		return;
	}
	zone = data_zone_index_for_zone_number(sbi, block);
	refcount_table_index = zone;
	bit = zone & ((1<<k) - 1);
	zone >>= k;
	if (zone >= sbi->s_zmap_blocks) {
		printk("minix_free_block: nonexistent bitmap buffer\n");
		return;
	}
	bh = sbi->s_zmap[zone];

	spin_lock(&bitmap_lock);

	// Decrement refcount
	// If refcount is 0, free block in bitmap
	if (decrement_refcount(sbi, refcount_table_index) == 0) {
		debug_log("Freeing data block %d\n", refcount_table_index);
		if (!minix_test_and_clear_bit(bit, bh->b_data))
			printk("minix_free_block (%s:%lu): bit already cleared\n",
			       sb->s_id, block);
	}
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	return;
}

int minix_new_block(struct inode * inode)
{
	struct minix_sb_info *sbi = minix_sb(inode->i_sb);
	int bits_per_zone = 8 * inode->i_sb->s_blocksize;
	int i;

	for (i = 0; i < sbi->s_zmap_blocks; i++) {
		struct buffer_head *bh = sbi->s_zmap[i];
		int j;

		spin_lock(&bitmap_lock);
		j = minix_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone) {
			// Set refcount to 1
			size_t refcount_table_index = i * bits_per_zone + j;
			set_refcount(sbi, refcount_table_index, 1);

			// Set zone used in bitmap
			minix_set_bit(j, bh->b_data);
			spin_unlock(&bitmap_lock);
			mark_buffer_dirty(bh);
			j += i * bits_per_zone + sbi->s_firstdatazone-1;
			if (j < sbi->s_firstdatazone || j >= sbi->s_nzones)
				break;
			return j;
		}
		spin_unlock(&bitmap_lock);
	}
	return 0;
}

unsigned long minix_count_free_blocks(struct super_block *sb)
{
	struct minix_sb_info *sbi = minix_sb(sb);
	u32 bits = sbi->s_nzones - sbi->s_firstdatazone + 1;

	return (count_free(sbi->s_zmap, sb->s_blocksize, bits)
		<< sbi->s_log_zone_size);
}

struct minix_inode *
minix_V1_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh)
{
	int block;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct minix_inode *p;

	if (!ino || ino > sbi->s_ninodes) {
		printk("Bad inode number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)ino);
		return NULL;
	}
	ino--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 ino / MINIX_INODES_PER_BLOCK;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read inode block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + ino % MINIX_INODES_PER_BLOCK;
}

struct minix2_inode *
minix_V2_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh)
{
	int block;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct minix2_inode *p;
	int minix2_inodes_per_block = sb->s_blocksize / sizeof(struct minix2_inode);

	*bh = NULL;
	if (!ino || ino > sbi->s_ninodes) {
		printk("Bad inode number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)ino);
		return NULL;
	}
	ino--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 ino / minix2_inodes_per_block;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read inode block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + ino % minix2_inodes_per_block;
}

/* Clear the link count and mode of a deleted inode on disk. */

static void minix_clear_inode(struct inode *inode)
{
	struct buffer_head *bh = NULL;

	if (INODE_VERSION(inode) == MINIX_V1) {
		struct minix_inode *raw_inode;
		raw_inode = minix_V1_raw_inode(inode->i_sb, inode->i_ino, &bh);
		if (raw_inode) {
			raw_inode->i_nlinks = 0;
			raw_inode->i_mode = 0;
		}
	} else {
		struct minix2_inode *raw_inode;
		raw_inode = minix_V2_raw_inode(inode->i_sb, inode->i_ino, &bh);
		if (raw_inode) {
			raw_inode->i_nlinks = 0;
			raw_inode->i_mode = 0;
		}
	}
	if (bh) {
		mark_buffer_dirty(bh);
		brelse (bh);
	}
}

void minix_free_inode(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	struct minix_sb_info *sbi = minix_sb(inode->i_sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long ino, bit;

	ino = inode->i_ino;
	debug_log("Removing inode %d", inode->i_ino);
	if (ino < 1 || ino > sbi->s_ninodes) {
		printk("minix_free_inode: inode 0 or nonexistent inode\n");
		return;
	}
	bit = ino & ((1<<k) - 1);
	ino >>= k;
	if (ino >= sbi->s_imap_blocks) {
		printk("minix_free_inode: nonexistent imap in superblock\n");
		return;
	}

	minix_clear_inode(inode);	/* clear on-disk copy */

	bh = sbi->s_imap[ino];
	spin_lock(&bitmap_lock);
	if (!minix_test_and_clear_bit(bit, bh->b_data))
		printk("minix_free_inode: bit %lu already cleared\n", bit);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
}

struct inode *minix_new_inode(const struct inode *dir, umode_t mode, int *error)
{
	struct super_block *sb = dir->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	struct inode *inode = new_inode(sb);
	struct buffer_head * bh;
	int bits_per_zone = 8 * sb->s_blocksize;
	unsigned long j;
	int i;

	if (!inode) {
		*error = -ENOMEM;
		return NULL;
	}
	j = bits_per_zone;
	bh = NULL;
	*error = -ENOSPC;
	spin_lock(&bitmap_lock);
	for (i = 0; i < sbi->s_imap_blocks; i++) {
		bh = sbi->s_imap[i];
		j = minix_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone)
			break;
	}
	if (!bh || j >= bits_per_zone) {
		spin_unlock(&bitmap_lock);
		iput(inode);
		return NULL;
	}
	if (minix_test_and_set_bit(j, bh->b_data)) {	/* shouldn't happen */
		spin_unlock(&bitmap_lock);
		printk("minix_new_inode: bit already set\n");
		iput(inode);
		return NULL;
	}
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	j += i * bits_per_zone;
	if (!j || j > sbi->s_ninodes) {
		iput(inode);
		return NULL;
	}
	inode_init_owner(inode, dir, mode);
	debug_log("Created inode %d", j);
	inode->i_ino = j;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_blocks = 0;
	memset(&minix_i(inode)->u, 0, sizeof(minix_i(inode)->u));
	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	*error = 0;
	return inode;
}

unsigned long minix_count_free_inodes(struct super_block *sb)
{
	struct minix_sb_info *sbi = minix_sb(sb);
	u32 bits = sbi->s_ninodes + 1;

	return count_free(sbi->s_imap, sb->s_blocksize, bits);
}
