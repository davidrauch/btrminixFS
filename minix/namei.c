/*
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include "minix.h"
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/highmem.h>
#include <linux/swap.h>

void cow_dir(struct inode *dir) {
	struct minix_inode_info *minix_inode = minix_i(dir);
	struct minix_sb_info *sbi = minix_sb(dir->i_sb);
	size_t i, data_zone_index, new_block;
	struct buffer_head *read_bh, *write_bh;
	unsigned long npages;
	struct page *page = NULL;
	bool had_change = false;

	// Clone the inodes pages if needed
	// Direct blocks: assign to target and increase refcount on blocks
	for (i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		if(minix_inode->u.i2_data[i] == 0) {
			break;
		}

		// Check refcount of this data block
		// Inode contains physical block number!
		data_zone_index = data_zone_index_for_zone_number(sbi, minix_inode->u.i2_data[i]);
		if(get_refcount(sbi, data_zone_index) > 1) {
			// Assign new block
			new_block = minix_new_block(dir);
			if (new_block != 0) {
				// Copy content
				read_bh = sb_bread(dir->i_sb, minix_inode->u.i2_data[i]);
				write_bh = sb_bread(dir->i_sb, new_block);
				memcpy(write_bh->b_data, read_bh->b_data, write_bh->b_size);
				mark_buffer_dirty(write_bh);
				sync_dirty_buffer(write_bh);
				brelse(read_bh);
				brelse(write_bh);

				// Decrement refcount on old block
				decrement_refcount(sbi, data_zone_index);

				// Set new block
				minix_inode->u.i2_data[i] = new_block;

				had_change = true;
			} else {
				debug_log("ERROR: Could not get new block for CoW");
			}
		}
	}

	if (had_change) {
		mark_inode_dirty(dir);
		write_inode_now(dir, 1);

		// Remove all pages of the directory entry from the cache
		npages = dir_pages(dir);
		for(i = 0; i < npages; i++) {
			page = dir_get_page(dir, i);
			lock_page(page);
			delete_from_page_cache(page);
			unlock_page(page);
		}
	}
}

static int add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = minix_add_link(dentry, inode);

	PRINT_FUNC();

	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static struct dentry *minix_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode = NULL;
	ino_t ino;

	PRINT_FUNC();

	if (dentry->d_name.len > minix_sb(dir->i_sb)->s_namelen)
		return ERR_PTR(-ENAMETOOLONG);

	ino = minix_inode_by_name(dentry);
	if (ino) {
		inode = minix_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	d_add(dentry, inode);
	return NULL;
}

static int minix_mknod(struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	int error;
	struct inode *inode;
	
	PRINT_FUNC();

	cow_dir(dir);

	if (!old_valid_dev(rdev))
		return -EINVAL;

	inode = minix_new_inode(dir, mode, &error);

	if (inode) {
		minix_set_inode(inode, rdev);
		mark_inode_dirty(inode);
		error = add_nondir(dentry, inode);
	}
	return error;
}

static int minix_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int error;
	struct inode *inode;

	PRINT_FUNC();
	cow_dir(dir);
	
	inode = minix_new_inode(dir, mode, &error);
	
	if (inode) {
		minix_set_inode(inode, 0);
		mark_inode_dirty(inode);
		d_tmpfile(dentry, inode);
	}
	return error;
}

static int minix_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	PRINT_FUNC();

	cow_dir(dir);

	return minix_mknod(dir, dentry, mode, 0);
}

static int minix_symlink(struct inode * dir, struct dentry *dentry,
	  const char * symname)
{
	int err = -ENAMETOOLONG;
	int i = strlen(symname)+1;
	struct inode * inode;
	
	PRINT_FUNC();

	cow_dir(dir);

	if (i > dir->i_sb->s_blocksize)
		goto out;

	inode = minix_new_inode(dir, S_IFLNK | 0777, &err);
	if (!inode)
		goto out;

	minix_set_inode(inode, 0);
	err = page_symlink(inode, symname, i);
	if (err)
		goto out_fail;

	err = add_nondir(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	iput(inode);
	goto out;
}

static int minix_link(struct dentry * old_dentry, struct inode * dir,
	struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	
	PRINT_FUNC();

	cow_dir(inode);

	inode->i_ctime = current_time(inode);
	inode_inc_link_count(inode);
	ihold(inode);
	return add_nondir(dentry, inode);
}

static int minix_mkdir(struct inode * dir, struct dentry *dentry, umode_t mode)
{
	struct inode * inode;
	int err;
	
	PRINT_FUNC();

	cow_dir(dir);

	inode_inc_link_count(dir);

	inode = minix_new_inode(dir, S_IFDIR | mode, &err);
	if (!inode)
		goto out_dir;

	minix_set_inode(inode, 0);

	inode_inc_link_count(inode);

	err = minix_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = minix_add_link(dentry, inode);
	if (err)
		goto out_fail;

	d_instantiate(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	iput(inode);
out_dir:
	inode_dec_link_count(dir);
	goto out;
}

static int minix_unlink(struct inode * dir, struct dentry *dentry)
{
	int err = -ENOENT;
	struct inode * inode = d_inode(dentry);
	struct page * page;
	struct minix_dir_entry * de;
	
	PRINT_FUNC();

	// CoW
	cow_dir(dir);

	de = minix_find_entry(dentry, &page);
	if (!de)
		goto end_unlink;

	err = minix_delete_entry(de, page);
	if (err)
		goto end_unlink;

	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
end_unlink:
	return err;
}

static int minix_rmdir(struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	int err = -ENOTEMPTY;

	PRINT_FUNC();

	cow_dir(dir);

	if (minix_empty_dir(inode)) {
		err = minix_unlink(dir, dentry);
		if (!err) {
			inode_dec_link_count(dir);
			inode_dec_link_count(inode);
		}
	}
	return err;
}

static int minix_rename(struct inode * old_dir, struct dentry *old_dentry,
			struct inode * new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct inode * old_inode = d_inode(old_dentry);
	struct inode * new_inode = d_inode(new_dentry);
	struct page * dir_page = NULL;
	struct minix_dir_entry * dir_de = NULL;
	struct page * old_page;
	struct minix_dir_entry * old_de;
	int err = -ENOENT;

	PRINT_FUNC();

	cow_dir(old_inode);
	cow_dir(new_inode);

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	old_de = minix_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = minix_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page * new_page;
		struct minix_dir_entry * new_de;

		err = -ENOTEMPTY;
		if (dir_de && !minix_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = minix_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		minix_set_link(new_de, new_page, old_inode);
		new_inode->i_ctime = current_time(new_inode);
		if (dir_de)
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
	} else {
		err = minix_add_link(new_dentry, old_inode);
		if (err)
			goto out_dir;
		if (dir_de)
			inode_inc_link_count(new_dir);
	}

	minix_delete_entry(old_de, old_page);
	mark_inode_dirty(old_inode);

	if (dir_de) {
		minix_set_link(dir_de, dir_page, new_dir);
		inode_dec_link_count(old_dir);
	}
	return 0;

out_dir:
	if (dir_de) {
		kunmap(dir_page);
		put_page(dir_page);
	}
out_old:
	kunmap(old_page);
	put_page(old_page);
out:
	return err;
}

/*
 * directories can handle most operations...
 */
const struct inode_operations minix_dir_inode_operations = {
	.create		= minix_create,
	.lookup		= minix_lookup,
	.link		= minix_link,
	.unlink		= minix_unlink,
	.symlink	= minix_symlink,
	.mkdir		= minix_mkdir,
	.rmdir		= minix_rmdir,
	.mknod		= minix_mknod,
	.rename		= minix_rename,
	.getattr	= minix_getattr,
	.tmpfile	= minix_tmpfile,
};
