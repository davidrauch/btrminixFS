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

void cow_dir(struct inode *inode) {
	struct minix_inode_info *minix_inode = minix_i(inode);
	struct minix_sb_info *sbi = minix_sb(inode->i_sb);
	size_t i;
	unsigned long npages;
	struct page *page = NULL;
	bool had_change = false;

	// Directly referenced
	//debug_log("== %d, %d, %d, %d ==", pos, len, first_inode_block_index, last_inode_block_index);
	//debug_log("Current_inode_block_indes is %d", current_inode_block_index);
	for(i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		// Check if block is not yet allocated
		// All following blocks will also be unallocated
		if(minix_inode->u.i2_data[i] == 0) {
			break;
		}

		// CoW block if needed
		cow_block(sbi, inode, &minix_inode->u.i2_data[i], true);
		had_change = true;
	}

	// Single indirect
	//debug_log("Current_inode_block_indes is %d (%d, %d)", current_inode_block_index, last_inode_block_index, n_blockrefs_in_inode + n_blockrefs_in_block);
	if(minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] != 0) {
		// CoW indirect block if needed
		cow_indirect_block(inode, &minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX], &i, true);
		had_change = true;
	}

	// Double indirect
	//debug_log("Current_inode_block_indes is %d", current_inode_block_index);
	if(minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX] != 0) {

		// CoW indirect block if needed
		cow_double_indirect_block(inode, &minix_inode->u.i2_data[DOUBLE_INDIRECT_BLOCK_INDEX], &i, true);
		had_change = true;
	}

	if (had_change) {
		mark_inode_dirty(inode);
		write_inode_now(inode, 1);

		// Remove all pages of the directory entry from the cache
		npages = dir_pages(inode);
		for(i = 0; i < npages; i++) {
			page = dir_get_page(inode, i);
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

	cow_dir(old_dir);
	cow_dir(new_dir);

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
