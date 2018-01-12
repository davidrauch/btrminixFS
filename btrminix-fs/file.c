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

long ioctl_funcs(struct file *filp, unsigned int cmd, unsigned long arg) {
	long ret = 0;
	struct super_block *sb = filp->f_inode->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	char __user *snapshot_name_userspace;
	char __user *snapshot_struct_userspace;
	char snapshot_name[SNAPSHOT_NAME_LENGTH];
	struct snapshot_slot snapshot_struct;
	int snapshot_slot;
	int __user *slot_info;
	int slots_taken = 0;
	char names[sbi->s_snapshots_slots * SNAPSHOT_NAME_LENGTH];

	switch(cmd) {
		case IOCTL_BTRMINIX_CREATE_SNAPSHOT:
			snapshot_name_userspace = (char __user*) arg;
			copy_from_user(snapshot_name, snapshot_name_userspace, SNAPSHOT_NAME_LENGTH);
			ret = create_snapshot(sb, snapshot_name);
			break;
		case IOCTL_BTRMINIX_ROLLBACK_SNAPSHOT:
			snapshot_name_userspace = (char __user*) arg;
			copy_from_user(snapshot_name, snapshot_name_userspace, SNAPSHOT_NAME_LENGTH);
			ret = rollback_snapshot(sb, snapshot_name);
			break;
		case IOCTL_BTRMINIX_REMOVE_SNAPSHOT:
			snapshot_name_userspace = (char __user*) arg;
			copy_from_user(snapshot_name, snapshot_name_userspace, SNAPSHOT_NAME_LENGTH);
			ret = remove_snapshot(sb, snapshot_name);
			break;
		case IOCTL_BTRMINIX_SLOT_OF_SNAPSHOT:
			snapshot_struct_userspace = (char __user*) arg;
			copy_from_user(&snapshot_struct, snapshot_struct_userspace, sizeof(snapshot_struct));
			copy_from_user(snapshot_name, snapshot_struct.name, SNAPSHOT_NAME_LENGTH);
			snapshot_slot = slot_of_snapshot(sb, snapshot_name);
			if(snapshot_slot >= 0) {
				copy_to_user(snapshot_struct.slot, &snapshot_slot, sizeof(int));
				ret = 0;
			} else {
				ret = snapshot_slot;
			}
			break;
		case IOCTL_BTRMINIX_LIST_SNAPSHOTS:
			ret = list_snapshots(sb, names);
			copy_to_user((char __user*) arg, names, sbi->s_snapshots_slots * SNAPSHOT_NAME_LENGTH);
			break;
		case IOCTL_BTRMINIX_SNAPSHOT_SLOTS:
			slot_info = (int __user*) arg;
			slots_taken = count_snapshots(sb);
			copy_to_user(slot_info, &sbi->s_snapshots_slots, sizeof(int));
			copy_to_user(slot_info+1, &slots_taken, sizeof(int));
			ret = 0;
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
