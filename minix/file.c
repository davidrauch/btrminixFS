/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include "minix.h"
#include <linux/buffer_head.h>
#include "ioctl_basic.h" 

// CoW implementation
static int minix_clone_file_range(struct file *src_file, loff_t off,
		struct file *dst_file, loff_t destoff, u64 len) {

	// Only src_file and dst_file are useful
	// off, destoff and len are always 0 when called from ioctl (e.g. during cp)

	struct inode *src_inode = src_file->f_inode;
	struct inode *dst_inode = dst_file->f_inode;
	struct minix_inode_info *src_minix_inode = minix_i(src_inode);
	struct minix_inode_info *dst_minix_inode = minix_i(dst_inode);
	int i;
	size_t data_zone_index;

	struct super_block *sb = dst_inode->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);

	PRINT_FUNC()
	debug_log("\tShould clone file %x (inode %x) to file %x (inode %x)\n", src_file, src_file->f_inode, dst_file, dst_file->f_inode);

	// Make sure all modifications of the src_file have been written to disk
	// The content of dst_file will be read from disk after cloning,
	// and if there are unsaved changes it will read 'old' data
	// This is synchronous and maybe there is a better way to prevent this problem, but this works for now
	write_inode_now(src_inode, 1);

	// Direct blocks: assign to target and increase refcount on blocks
	for (i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		if (src_minix_inode->u.i2_data[i] != 0) {
			// Assign the zone number
			dst_minix_inode->u.i2_data[i] = src_minix_inode->u.i2_data[i];
			debug_log("\tSetting block %d to %d\n", i, data_zone_index_for_zone_number(sbi, dst_minix_inode->u.i2_data[i]));
			
			// Increase refcount on data block
			data_zone_index = data_zone_index_for_zone_number(sbi, dst_minix_inode->u.i2_data[i]);
			increment_refcount(sbi, data_zone_index);
		}
	}

	// Single indirect blocks: Assign indirect block and increase refcount on indirect block and data blocks
	if (src_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] != 0) {
		// Assign indirect block
		dst_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] = src_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX];
		debug_log("\tSetting block %d to %d\n", i, data_zone_index_for_zone_number(sbi, dst_minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX]));

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

	// Set proper size and truncate all currently cached pages of the destination inode
	// so that the next read will read the new data
	truncate_setsize(dst_inode, src_inode->i_size);
	truncate_inode_pages_range(&dst_inode->i_data, 0, PAGE_ALIGN(dst_inode->i_size));
	mark_inode_dirty(dst_inode);

	return 0;
}

void create_snapshot(struct super_block *sb) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t i, read_block, write_block;
	struct buffer_head *read_bh, *write_bh;

	PRINT_FUNC();

	// Get buffer_head to snapshot
	write_block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks + sbi->s_inodes_blocks + sbi->s_refcount_table_blocks;
	debug_log("\tSnapshot starts at block %ld", write_block);

	// Copy inode bitmap to snapshot
	for(i = 0; i < sbi->s_imap_blocks; i++) {
		read_bh = sbi->s_imap[i];
		write_bh = sb_bread(sb, write_block);

		memcpy(write_bh->b_data, read_bh->b_data, write_bh->b_size);
		mark_buffer_dirty(write_bh);
		sync_dirty_buffer(write_bh);

		write_block++;
	}

	// Copy inodes to snapshot
	read_block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks;
	for(i = 0; i < sbi->s_inodes_blocks; i++) {
		read_bh = sb_bread(sb, read_block);
		write_bh = sb_bread(sb, write_block);

		memcpy(write_bh->b_data, read_bh->b_data, write_bh->b_size);
		mark_buffer_dirty(write_bh);
		sync_dirty_buffer(write_bh);

		read_block++;
		write_block++;
	}

	debug_log("\tCopied blocks until block %ld\n", write_block);

	// Increment all refcounts > 0 (all current content has to be preserved)
	for(i = sbi->s_firstdatazone; i < sbi->s_nzones; i++) {
		read_block = data_zone_index_for_zone_number(sbi, i);
		if(get_refcount(sbi, read_block) > 0) {
			increment_refcount(sbi, read_block);
		}
	}
}

void free_data_blocks_of_inode(struct super_block *sb, struct minix2_inode *inode) {
	size_t i;
	struct inode fake_inode; // TODO: This isn't very nice...
	fake_inode.i_sb = sb;

	// Free direct data blocks
	for(i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		if(inode->i_zone[i] == 0) {
			break;
		}

		minix_free_block(&fake_inode, inode->i_zone[i]);
	}

	// TODO: Indirect blocks
}

void free_data_blocks_of_inodes(struct super_block *sb, size_t imap_start_block, size_t inodes_start_block) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t imap_block_i, bit, inode_i, inode_block_i, inode_block_offset;
	struct buffer_head *imap_bh, *inode_bh;
	struct minix2_inode *inode;

	PRINT_FUNC();

	for(imap_block_i = 0; imap_block_i < sbi->s_imap_blocks; imap_block_i++) {
		imap_bh = sb_bread(sb, imap_start_block + imap_block_i);

		for(bit = 0; bit < sb->s_blocksize << 3; bit++) {
			inode_i = imap_block_i * sb->s_blocksize_bits + bit;
			if(inode_i >= sbi->s_ninodes) {
				break;
			}

			if(minix_test_bit(bit, imap_bh->b_data)) {
				debug_log("Found inode %ld in use\n", inode_i);

				// Read inode
				inode_block_i = inode_i / (sb->s_blocksize / sizeof(struct minix2_inode));
				inode_block_offset = inode_i % (sb->s_blocksize / sizeof(struct minix2_inode));
				
				debug_log("Inode is in block %ld at offset %ld\n", inode_block_i, inode_block_offset);

				inode_bh = sb_bread(sb, inodes_start_block + inode_block_i);
				inode = ((struct minix2_inode*)inode_bh->b_data) + inode_block_offset;

				free_data_blocks_of_inode(sb, inode);
			}
		}

	}
}

void rollback_snapshot(struct super_block *sb) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t i, read_block, write_block;
	struct buffer_head *read_bh, *write_bh;

	PRINT_FUNC();

	// Remove current content
	free_data_blocks_of_inodes(sb, 2, 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks);

	// Get buffer_head to snapshot
	read_block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks + sbi->s_inodes_blocks + sbi->s_refcount_table_blocks;
	debug_log("\tSnapshot starts at block %ld", read_block);
	read_bh = sb_bread(sb, read_block);

	// Copy inode bitmap from snapshot
	for(i = 0; i < sbi->s_imap_blocks; i++) {
		write_bh = sbi->s_imap[i];
		read_bh = sb_bread(sb, read_block);

		memcpy(write_bh->b_data, read_bh->b_data, write_bh->b_size);
		mark_buffer_dirty(write_bh);
		sync_dirty_buffer(write_bh);

		read_block++;
	}

	// Copy inodes to snapshot
	write_block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks;
	for(i = 0; i < sbi->s_inodes_blocks; i++) {
		write_bh = sb_bread(sb, write_block);
		read_bh = sb_bread(sb, read_block);

		memcpy(write_bh->b_data, read_bh->b_data, write_bh->b_size);
		mark_buffer_dirty(write_bh);
		sync_dirty_buffer(write_bh);

		write_block++;
		read_block++;
	}

	debug_log("\tCopied blocks until block %ld\n", read_block);
}

void remove_snapshot(struct super_block *sb) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t snapshot_start_block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks + sbi->s_inodes_blocks + sbi->s_refcount_table_blocks;

	PRINT_FUNC();

	// Remove snapshot content
	free_data_blocks_of_inodes(sb, snapshot_start_block, snapshot_start_block + sbi->s_imap_blocks);
}

long ioctl_funcs(struct file *filp, unsigned int cmd, unsigned long arg) {
	int ret=0;
	struct super_block *sb = filp->f_inode->i_sb;

	switch(cmd) {
		case IOCTL_ALTMINIX_CREATE_SNAPSHOT:
			create_snapshot(sb);
			break;
		case IOCTL_ALTMINIX_ROLLBACK_SNAPSHOT:
			rollback_snapshot(sb);
			break;
		case IOCTL_ALTMINIX_REMOVE_SNAPSHOT:
			remove_snapshot(sb);
			break;
	} 

	return ret;
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
	.unlocked_ioctl = ioctl_funcs,
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
