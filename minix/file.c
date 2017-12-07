/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include "minix.h"
#include <linux/buffer_head.h>


// CoW implementation
static int minix_clone_file_range(struct file *src_file, loff_t off,
		struct file *dst_file, loff_t destoff, u64 len) {

	struct inode *src_inode = src_file->f_inode;
	struct inode *dst_inode = dst_file->f_inode;
	struct minix_inode_info *src_minix_inode = minix_i(src_inode);
	struct minix_inode_info *dst_minix_inode = minix_i(dst_inode);
	int i;
	size_t data_zone_index;

	struct super_block *sb = dst_inode->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);

	PRINT_FUNC()
	debug_log("Should clone file %x (inode %x) to file %x (inode %x)\n", src_file, src_file->f_inode, dst_file, dst_file->f_inode);
	//debug_log("Got minix inodes %x and %x\n", src_minix_inode, dst_minix_inode);

	// Direct blocks: assign to target and increase refcount on blocks
	for (i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		if (src_minix_inode->u.i2_data[i] != 0) {
			// Assign the zone number
			dst_minix_inode->u.i2_data[i] = src_minix_inode->u.i2_data[i];
			debug_log("\tSetting block %d to %d\n", i, src_minix_inode->u.i2_data[i]);
			
			// Increase refcount on data block
			data_zone_index = data_zone_index_for_zone_number(sbi, dst_minix_inode->u.i2_data[i]);
			increment_refcount(sbi, data_zone_index);
		}
	}

	// Single indirect blocks: Assign indirect block and increase refcount on indirect block and data blocks
	if (src_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] != 0) {
		// Assign indirect block
		dst_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] = src_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX];

		// Increase refcount on indirect block
		increment_refcounts_on_indirect_block(sb, dst_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX]);
	}

	// Double indirect blocks:
	// - Assign double indirect block in inode
	// - Increase refcount of double indirect block
	// 		- Increase refcount of all single indirect blocks and their data blocks
	if (src_minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX] != 0) {
		struct buffer_head *bh;
		uint32_t *double_indirect_block;
		size_t n_blockrefs_in_block = sb->s_blocksize / sizeof(uint32_t);

		// Assign double indirect block
		dst_minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX] = src_minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX];

		// Increase refcount on double indirect block
		data_zone_index = data_zone_index_for_zone_number(sbi, dst_minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX]);
		increment_refcount(sbi, data_zone_index);

		// Read double indirect block to find single indirect blocks
		bh = sb_bread(sb, dst_minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX]);
		double_indirect_block = (uint32_t*) bh->b_data;

		for (i = 0; i < n_blockrefs_in_block; i++) {
			if (double_indirect_block[i] != 0) {
				increment_refcounts_on_indirect_block(sb, double_indirect_block[i]);
			}
		}
	}

	// Set the same size
	dst_inode->i_size = src_inode->i_size;
	mark_inode_dirty(dst_inode);

	return 0;
}

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the minix filesystem.
 */
const struct file_operations minix_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync,
	.splice_read	= generic_file_splice_read,
	.clone_file_range	= minix_clone_file_range,
};

static int minix_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(inode, attr->ia_size);
		minix_truncate(inode);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

const struct inode_operations minix_file_inode_operations = {
	.setattr	= minix_setattr,
	.getattr	= minix_getattr,
};
