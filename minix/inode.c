/*
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Copyright (C) 1996  Gertjan van Wingerde
 *	Minix V2 fs support.
 *
 *  Modified for 680x0 by Andreas Schwab
 *  Updated to filesystem version 3 by Daniel Aragones
 */

#include <linux/module.h>
#include "minix.h"
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/vfs.h>
#include <linux/writeback.h>

static int minix_write_inode(struct inode *inode,
		struct writeback_control *wbc);
static int minix_statfs(struct dentry *dentry, struct kstatfs *buf);
static int minix_remount (struct super_block * sb, int * flags, char * data);

static void minix_evict_inode(struct inode *inode)
{
	PRINT_FUNC();

	truncate_inode_pages_final(&inode->i_data);
	if (!inode->i_nlink) {
		inode->i_size = 0;
		minix_truncate(inode);
	}
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	if (!inode->i_nlink)
		minix_free_inode(inode);
}

static void minix_put_super(struct super_block *sb)
{
	int i;
	struct minix_sb_info *sbi = minix_sb(sb);

	if (!(sb->s_flags & MS_RDONLY)) {
		if (sbi->s_version != MINIX_V3)	 /* s_state is now out from V3 sb */
			sbi->s_ms->s_state = sbi->s_mount_state;
		mark_buffer_dirty(sbi->s_sbh);
	}
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	brelse (sbi->s_sbh);
	kfree(sbi->s_imap);
	sb->s_fs_info = NULL;
	kfree(sbi);
}

static struct kmem_cache * minix_inode_cachep;

static struct inode *minix_alloc_inode(struct super_block *sb)
{
	struct minix_inode_info *ei;

	PRINT_FUNC();

	ei = kmem_cache_alloc(minix_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void minix_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(minix_inode_cachep, minix_i(inode));
}

void minix_destroy_inode(struct inode *inode)
{
	PRINT_FUNC();

	call_rcu(&inode->i_rcu, minix_i_callback);
}

static void init_once(void *foo)
{
	struct minix_inode_info *ei = (struct minix_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	minix_inode_cachep = kmem_cache_create("minix_inode_cache",
					     sizeof(struct minix_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (minix_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(minix_inode_cachep);
}

static const struct super_operations minix_sops = {
	.alloc_inode	= minix_alloc_inode,
	.destroy_inode	= minix_destroy_inode,
	.write_inode	= minix_write_inode,
	.evict_inode	= minix_evict_inode,
	.put_super	= minix_put_super,
	.statfs		= minix_statfs,
	.remount_fs	= minix_remount,
};

static int minix_remount (struct super_block * sb, int * flags, char * data)
{
	struct minix_sb_info * sbi = minix_sb(sb);
	struct minix_super_block * ms;

	sync_filesystem(sb);
	ms = sbi->s_ms;
	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY) {
		if (ms->s_state & MINIX_VALID_FS ||
		    !(sbi->s_mount_state & MINIX_VALID_FS))
			return 0;
		/* Mounting a rw partition read-only. */
		if (sbi->s_version != MINIX_V3)
			ms->s_state = sbi->s_mount_state;
		mark_buffer_dirty(sbi->s_sbh);
	} else {
	  	/* Mount a partition which is read-only, read-write. */
		if (sbi->s_version != MINIX_V3) {
			sbi->s_mount_state = ms->s_state;
			ms->s_state &= ~MINIX_VALID_FS;
		} else {
			sbi->s_mount_state = MINIX_VALID_FS;
		}
		mark_buffer_dirty(sbi->s_sbh);

		if (!(sbi->s_mount_state & MINIX_VALID_FS))
			debug_log("MINIX-fs warning: remounting unchecked fs, "
				"running fsck is recommended\n");
		else if ((sbi->s_mount_state & MINIX_ERROR_FS))
			debug_log("MINIX-fs warning: remounting fs with errors, "
				"running fsck is recommended\n");
	}
	return 0;
}

static int minix_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct buffer_head **map;
	struct minix_super_block *ms;
	struct minix3_super_block *m3s = NULL;
	unsigned long i, block;
	struct inode *root_inode;
	struct minix_sb_info *sbi;
	int ret = -EINVAL;

	debug_log("Loading super block\n");

	sbi = kzalloc(sizeof(struct minix_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;
	
	BUILD_BUG_ON(32 != sizeof (struct minix_inode));
	BUILD_BUG_ON(64 != sizeof(struct minix2_inode));

	if (!sb_set_blocksize(s, BLOCK_SIZE)) {
		debug_log("- bad_hblock\n");
		goto out_bad_hblock;
	}

	if (!(bh = sb_bread(s, 1))) {
		debug_log("- bad_sb\n");
		goto out_bad_sb;
	}

	ms = (struct minix_super_block *) bh->b_data;
	sbi->s_ms = ms;
	sbi->s_sbh = bh;
	sbi->s_mount_state = ms->s_state;
	sbi->s_ninodes = ms->s_ninodes;
	sbi->s_nzones = ms->s_nzones;
	sbi->s_imap_blocks = ms->s_imap_blocks;
	sbi->s_zmap_blocks = ms->s_zmap_blocks;
	sbi->s_firstdatazone = ms->s_firstdatazone;
	sbi->s_log_zone_size = ms->s_log_zone_size;
	sbi->s_max_size = ms->s_max_size;
	s->s_magic = ms->s_magic;
	if (s->s_magic == MINIX_SUPER_MAGIC) {
		sbi->s_version = MINIX_V1;
		sbi->s_dirsize = 16;
		sbi->s_namelen = 14;
		s->s_max_links = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX_SUPER_MAGIC2) {
		sbi->s_version = MINIX_V1;
		sbi->s_dirsize = 32;
		sbi->s_namelen = 30;
		s->s_max_links = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC) {
		sbi->s_version = MINIX_V2;
		sbi->s_nzones = ms->s_zones;
		sbi->s_dirsize = 16;
		sbi->s_namelen = 14;
		s->s_max_links = MINIX2_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC2) {
		sbi->s_version = MINIX_V2;
		sbi->s_nzones = ms->s_zones;
		sbi->s_dirsize = 32;
		sbi->s_namelen = 30;
		s->s_max_links = MINIX2_LINK_MAX;
	} else if ( *(__u16 *)(bh->b_data + 24) == MINIX3_SUPER_MAGIC) {
		debug_log("- Minix is version 3\n");
		m3s = (struct minix3_super_block *) bh->b_data;
		s->s_magic = m3s->s_magic;
		debug_log("- magic is %ld\n", s->s_magic);
		sbi->s_imap_blocks = m3s->s_imap_blocks;
		debug_log("- imap_blocks is %ld\n", sbi->s_imap_blocks);
		sbi->s_zmap_blocks = m3s->s_zmap_blocks;
		debug_log("- zmap_blocks is %ld\n", sbi->s_zmap_blocks);
		sbi->s_firstdatazone = m3s->s_firstdatazone;
		debug_log("- firstdatazone is %ld\n", sbi->s_firstdatazone);
		sbi->s_log_zone_size = m3s->s_log_zone_size;
		debug_log("- log_size is %ld\n", sbi->s_log_zone_size);
		sbi->s_max_size = m3s->s_max_size;
		debug_log("- max_size is %ld\n", sbi->s_max_size);
		sbi->s_ninodes = m3s->s_ninodes;
		debug_log("- ninodes is %ld\n", sbi->s_ninodes);
		sbi->s_nzones = m3s->s_zones;
		debug_log("- zones is %ld\n", sbi->s_nzones);
		sbi->s_dirsize = 64;
		sbi->s_namelen = 60;
		sbi->s_version = MINIX_V3;
		sbi->s_mount_state = MINIX_VALID_FS;
		sbi->s_inodes_blocks = m3s->s_inodes_blocks;
		sbi->s_refcount_table_blocks = m3s->s_refcount_table_blocks;
		sbi->s_snapshots_start_block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks + sbi->s_inodes_blocks + sbi->s_refcount_table_blocks;
		debug_log("- blocksize is %d\n", m3s->s_blocksize);
		sb_set_blocksize(s, m3s->s_blocksize);
		s->s_max_links = MINIX2_LINK_MAX;
	} else {
		debug_log("- no_fs\n");
		goto out_no_fs;
	}

	/*
	 * Allocate the buffer map to keep the superblock small.
	 */
	if (sbi->s_imap_blocks == 0 || sbi->s_zmap_blocks == 0)
		goto out_illegal_sb;
	i = (sbi->s_imap_blocks + sbi->s_zmap_blocks) * sizeof(bh);
	map = kzalloc(i, GFP_KERNEL);
	if (!map)
		goto out_no_map;
	sbi->s_imap = &map[0];
	sbi->s_zmap = &map[sbi->s_imap_blocks];

	block=2;
	for (i=0 ; i < sbi->s_imap_blocks ; i++) {
		if (!(sbi->s_imap[i]=sb_bread(s, block)))
			goto out_no_bitmap;
		block++;
	}
	for (i=0 ; i < sbi->s_zmap_blocks ; i++) {
		if (!(sbi->s_zmap[i]=sb_bread(s, block)))
			goto out_no_bitmap;
		block++;
	}

	minix_set_bit(0,sbi->s_imap[0]->b_data);
	minix_set_bit(0,sbi->s_zmap[0]->b_data);

	/* Apparently minix can create filesystems that allocate more blocks for
	 * the bitmaps than needed.  We simply ignore that, but verify it didn't
	 * create one with not enough blocks and bail out if so.
	 */
	block = minix_blocks_needed(sbi->s_ninodes, s->s_blocksize);
	if (sbi->s_imap_blocks < block) {
		printk("MINIX-fs: file system does not have enough "
				"imap blocks allocated.  Refusing to mount.\n");
		goto out_no_bitmap;
	}

	block = minix_blocks_needed(
			(sbi->s_nzones - sbi->s_firstdatazone + 1),
			s->s_blocksize);
	if (sbi->s_zmap_blocks < block) {
		printk("MINIX-fs: file system does not have enough "
				"zmap blocks allocated.  Refusing to mount.\n");
		goto out_no_bitmap;
	}

	/*
	 * Allocate and read the refcount table
	 * This is very similar to reading the inode and zone map,
	 * except we have to skip over the inodes that come before
	 * the refcount table on disk
	 */
	if (sbi->s_refcount_table_blocks == 0) {
		goto out_illegal_sb;
	}

	// Allocate memory
	i = sbi->s_refcount_table_blocks * sizeof(bh);
	map = kzalloc(i, GFP_KERNEL);
	if (!map)
		goto out_no_map;

	// Set up buffer_head
	sbi->s_refcount_table = &map[0];

	// Read content
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks + sbi->s_inodes_blocks;
	for (i=0 ; i < sbi->s_refcount_table_blocks ; i++) {
		if (!(sbi->s_refcount_table[i]=sb_bread(s, block)))
			goto out_no_bitmap;
		block++;
	}

	// Minix clears the 0 bit on zone and inode map, so we also clear the refcount
	*((uint32_t*)sbi->s_refcount_table[0]->b_data) = 0;

	/* set up enough so that it can read an inode */
	s->s_op = &minix_sops;
	root_inode = minix_iget(s, MINIX_ROOT_INO);
	if (IS_ERR(root_inode)) {
		ret = PTR_ERR(root_inode);
		goto out_no_root;
	}

	ret = -ENOMEM;
	s->s_root = d_make_root(root_inode);
	if (!s->s_root)
		goto out_no_root;

	if (!(s->s_flags & MS_RDONLY)) {
		if (sbi->s_version != MINIX_V3) /* s_state is now out from V3 sb */
			ms->s_state &= ~MINIX_VALID_FS;
		mark_buffer_dirty(bh);
	}
	if (!(sbi->s_mount_state & MINIX_VALID_FS))
		printk("MINIX-fs: mounting unchecked file system, "
			"running fsck is recommended\n");
	else if (sbi->s_mount_state & MINIX_ERROR_FS)
		printk("MINIX-fs: mounting file system with errors, "
			"running fsck is recommended\n");

	return 0;

out_no_root:
	if (!silent)
		printk("MINIX-fs: get root inode failed\n");
	goto out_freemap;

out_no_bitmap:
	printk("MINIX-fs: bad superblock or unable to read bitmaps\n");
out_freemap:
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	kfree(sbi->s_imap);
	goto out_release;

out_no_map:
	ret = -ENOMEM;
	if (!silent)
		printk("MINIX-fs: can't allocate map\n");
	goto out_release;

out_illegal_sb:
	if (!silent)
		printk("MINIX-fs: bad superblock\n");
	goto out_release;

out_no_fs:
	if (!silent)
		printk("VFS: Can't find a Minix filesystem V1 | V2 | V3 "
		       "on device %s.\n", s->s_id);
out_release:
	brelse(bh);
	goto out;

out_bad_hblock:
	printk("MINIX-fs: blocksize too small for device\n");
	goto out;

out_bad_sb:
	printk("MINIX-fs: unable to read superblock\n");
out:
	s->s_fs_info = NULL;
	kfree(sbi);
	return ret;
}

static int minix_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (sbi->s_nzones - sbi->s_firstdatazone) << sbi->s_log_zone_size;
	buf->f_bfree = minix_count_free_blocks(sb);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = sbi->s_ninodes;
	buf->f_ffree = minix_count_free_inodes(sb);
	buf->f_namelen = sbi->s_namelen;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static int minix_get_block(struct inode *inode, sector_t block,
		    struct buffer_head *bh_result, int create)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_get_block(inode, block, bh_result, create);
	else
		return V2_minix_get_block(inode, block, bh_result, create);
}

static int minix_writepage(struct page *page, struct writeback_control *wbc)
{
	PRINT_FUNC();
	debug_log("\tpage = %x\n", page);

	return block_write_full_page(page, minix_get_block, wbc);
}

static int minix_readpage(struct file *file, struct page *page)
{
	PRINT_FUNC();
	debug_log("\tinode = %x, page = %x\n", file != 0 ? file->f_inode : 0, page);
	return block_read_full_page(page,minix_get_block);
}

int minix_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, minix_get_block);
}

static void minix_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		minix_truncate(inode);
	}
}

static int minix_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;
	size_t i;
	struct inode *inode = file->f_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	struct super_block *sb = inode->i_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t first_inode_block_index;
	size_t last_inode_block_index;

	size_t n_blockrefs_in_inode = NUM_ZONES_IN_INODE - 2;
	size_t n_blockrefs_in_block = sb->s_blocksize / sizeof(uint32_t);

	int new_block;
	uint32_t data_zone_index;

	PRINT_FUNC();
	debug_log("- file: %x\n", file);
	debug_log("  - inode: %x\n", file->f_inode);
	debug_log("- pos: %d\n", pos);
	debug_log("- len: %d\n", len);
	debug_log("- page: %x\n", *pagep);

	
	// We can change the target inode here!
	// - Check which blocks would be written to
	// 		- Only relevant: block that are already referenced
	//		  new blocks will be allocated automatically in block_write_begin()
	// - Check which of these blocks have refcount > 1
	// - For those blocks: Allocate new blocks, change references in inode
	//		- Since the page cache always flushes full pages, we don't have to manually copy the old contents
	//		- This relies on page size >= block size, but other parts of the driver also assume this

	// Check which blocks would be written to
	// TODO: Also handle indirect data blocks
	first_inode_block_index = pos / sb->s_blocksize;
	last_inode_block_index = (pos + len -1) / sb->s_blocksize;

	// TODO: This currently only works for the first 7 blocks (directly referenced)
	for (i = first_inode_block_index; i <= last_inode_block_index; i++) {
		// Directly referenced blocks
		if (i < n_blockrefs_in_inode) {
			// Check if block is not yet allocated
			// All following blocks will also be unallocated
			if(minix_inode->u.i2_data[i] == 0) {
				break;
			}

			// Check refcount of this data block
			// Inode contains physical block number!
			data_zone_index = data_zone_index_for_zone_number(sbi, minix_inode->u.i2_data[i]);
			if(get_refcount(sbi, data_zone_index) > 1) {
				// Assign new block
				new_block = minix_new_block(inode);
				if (new_block != 0) {
					// Decrement refcount on old block
					decrement_refcount(sbi, data_zone_index);

					// Set new block
					minix_inode->u.i2_data[i] = new_block;
				} else {
					debug_log("ERROR: Could not get new block for CoW");
				}
			}
		} else if (i < n_blockrefs_in_inode + n_blockrefs_in_block) {
			// Single indirectly referenced blocks
			size_t indirect_i = i - n_blockrefs_in_inode;
			struct buffer_head *indirect_block_bh;
			uint32_t physical_block_index;
			
			// If we don't reference an indirect block yet, we can just ignore it
			if (minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] == 0) {
				break;
			}

			// Copy the indirect block if needed
			data_zone_index = data_zone_index_for_zone_number(sbi, minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX]);
			if(get_refcount(sbi, data_zone_index) > 1) {
				// Assign new block
				new_block = minix_new_block(inode);
				if (new_block != 0) {
					struct buffer_head *old_indirect_block_bh;
					struct buffer_head *new_indirect_block_bh;

					// Copy content to new indirect block
					// Read block content
					if (!(old_indirect_block_bh = sb_bread(sb, minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX]))) {
						debug_log("ERROR: Could not read old indirect block");
					}

					// Read target block
					if (!(new_indirect_block_bh = sb_bread(sb, new_block))) {
						debug_log("ERROR: Could not read new indirect block");
					}

					// Copy
					memcpy(new_indirect_block_bh->b_data, old_indirect_block_bh->b_data, new_indirect_block_bh->b_size);
					mark_buffer_dirty(new_indirect_block_bh);
					sync_dirty_buffer(new_indirect_block_bh);

					// Decrement refcount on old block
					decrement_refcount(sbi, data_zone_index);

					// Set new block
					minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX] = new_block;

				} else {
					debug_log("ERROR: Could not get new block for CoW");
				}
			}

			// Copy the actual data blocks if needed
			// Read indirect block
			if (!(indirect_block_bh = sb_bread(sb, minix_inode->u.i2_data[INDIRECT_BLOCK_INDEX]))) {
				debug_log("ERROR: Could not read indirect block");
			}

			physical_block_index = ((uint32_t*)indirect_block_bh->b_data)[indirect_i];

			if (physical_block_index == 0) {
				break;
			}

			// Check refcount
			data_zone_index = data_zone_index_for_zone_number(sbi, physical_block_index);
			if(get_refcount(sbi, data_zone_index) > 1) {
				// Assign new block
				new_block = minix_new_block(inode);
				if (new_block == 0) {
					debug_log("ERROR: Could not get new block for CoW");
					break;
				}
				
				// Decrement refcount on old block
				decrement_refcount(sbi, data_zone_index);

				// Set new block
				((uint32_t*)indirect_block_bh->b_data)[indirect_i] = new_block;
				mark_buffer_dirty(indirect_block_bh);
				sync_dirty_buffer(indirect_block_bh);
			}

		} else {
			// Double indirectly referenced blocks
		}
	}


	// block_write_begin will allocate new blocks and rewrite indirect blocks if needed
	// We have to prepare the inode so that all these operations are done on blocks with refcount == 1
	ret = block_write_begin(mapping, pos, len, flags, pagep,
				minix_get_block);
	if (unlikely(ret))
		minix_write_failed(mapping, pos + len);

	return ret;
}

static sector_t minix_bmap(struct address_space *mapping, sector_t block)
{
	PRINT_FUNC();
	
	return generic_block_bmap(mapping,block,minix_get_block);
}

static const struct address_space_operations minix_aops = {
	.readpage = minix_readpage,
	.writepage = minix_writepage,
	.write_begin = minix_write_begin,
	.write_end = generic_write_end,
	.bmap = minix_bmap
};

static const struct inode_operations minix_symlink_inode_operations = {
	.get_link	= page_get_link,
	.getattr	= minix_getattr,
};

void minix_set_inode(struct inode *inode, dev_t rdev)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &minix_file_inode_operations;
		inode->i_fop = &minix_file_operations;
		inode->i_mapping->a_ops = &minix_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &minix_dir_inode_operations;
		inode->i_fop = &minix_dir_operations;
		inode->i_mapping->a_ops = &minix_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &minix_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &minix_aops;
	} else
		init_special_inode(inode, inode->i_mode, rdev);
}

/*
 * The minix V1 function to read an inode.
 */
static struct inode *V1_minix_iget(struct inode *inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	raw_inode = minix_V1_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode) {
		iget_failed(inode);
		return ERR_PTR(-EIO);
	}
	inode->i_mode = raw_inode->i_mode;
	i_uid_write(inode, raw_inode->i_uid);
	i_gid_write(inode, raw_inode->i_gid);
	set_nlink(inode, raw_inode->i_nlinks);
	inode->i_size = raw_inode->i_size;
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec = raw_inode->i_time;
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = 0;
	for (i = 0; i < 9; i++)
		minix_inode->u.i1_data[i] = raw_inode->i_zone[i];
	minix_set_inode(inode, old_decode_dev(raw_inode->i_zone[0]));
	brelse(bh);
	unlock_new_inode(inode);
	return inode;
}


/*
 * Unsets the write bits of a mode and returns the new mode
 */
static uint16_t minix_mode_without_write_bits(uint16_t mode) {
	const uint16_t mask = 0xff6d; // All bits set except the write bits
	return mode & mask;
}


/*
 * The minix V2 function to read an inode.
 */
static struct inode *V2_minix_iget(struct inode *inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	PRINT_FUNC();

	raw_inode = minix_V2_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode) {
		iget_failed(inode);
		return ERR_PTR(-EIO);
	}

	// Read real mode from new location
	inode->i_mode = raw_inode->i_real_mode;

	i_uid_write(inode, raw_inode->i_uid);
	i_gid_write(inode, raw_inode->i_gid);
	set_nlink(inode, raw_inode->i_nlinks);
	inode->i_size = raw_inode->i_size;
	inode->i_mtime.tv_sec = raw_inode->i_mtime;
	inode->i_atime.tv_sec = raw_inode->i_atime;
	inode->i_ctime.tv_sec = raw_inode->i_ctime;
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = 0;
	for (i = 0; i < NUM_ZONES_IN_INODE; i++)
		minix_inode->u.i2_data[i] = raw_inode->i_zone[i];
	minix_set_inode(inode, old_decode_dev(raw_inode->i_zone[0]));
	brelse(bh);
	unlock_new_inode(inode);
	return inode;
}

/*
 * The global function to read an inode.
 */
struct inode *minix_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_iget(inode);
	else
		return V2_minix_iget(inode);
}

/*
 * The minix V1 function to synchronize an inode.
 */
static struct buffer_head * V1_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	raw_inode = minix_V1_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode)
		return NULL;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = fs_high2lowuid(i_uid_read(inode));
	raw_inode->i_gid = fs_high2lowgid(i_gid_read(inode));
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_time = inode->i_mtime.tv_sec;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = old_encode_dev(inode->i_rdev);
	else for (i = 0; i < 9; i++)
		raw_inode->i_zone[i] = minix_inode->u.i1_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

/*
 * The minix V2 function to synchronize an inode.
 */
static struct buffer_head * V2_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	struct minix_inode_info *minix_inode = minix_i(inode);
	int i;

	PRINT_FUNC()
	//debug_log("\tWorking on inode %x\n", inode);

	raw_inode = minix_V2_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode)
		return NULL;

	// Write the nonwrite version of the mode as legacy mode
	raw_inode->i_mode = minix_mode_without_write_bits(inode->i_mode);
	// Write the real mode
	raw_inode->i_real_mode = inode->i_mode;

	raw_inode->i_uid = fs_high2lowuid(i_uid_read(inode));
	raw_inode->i_gid = fs_high2lowgid(i_gid_read(inode));
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_mtime = inode->i_mtime.tv_sec;
	raw_inode->i_atime = inode->i_atime.tv_sec;
	raw_inode->i_ctime = inode->i_ctime.tv_sec;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_zone[0] = old_encode_dev(inode->i_rdev);
	else for (i = 0; i < NUM_ZONES_IN_INODE; i++) {
		//debug_log("\tWriting block %d to %x\n", i, minix_inode->u.i2_data[i]);
		raw_inode->i_zone[i] = minix_inode->u.i2_data[i];
	}
	mark_buffer_dirty(bh);
	return bh;
}

static int minix_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err = 0;
	struct buffer_head *bh;

	PRINT_FUNC();
	debug_log("\tFor inode %x", inode);

	if (INODE_VERSION(inode) == MINIX_V1)
		bh = V1_minix_update_inode(inode);
	else
		bh = V2_minix_update_inode(inode);
	if (!bh)
		return -EIO;
	if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing minix inode [%s:%08lx]\n",
				inode->i_sb->s_id, inode->i_ino);
			err = -EIO;
		}
	}
	brelse (bh);
	return err;
}

int minix_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int flags)
{
	struct super_block *sb = path->dentry->d_sb;
	struct inode *inode = d_inode(path->dentry);

	generic_fillattr(inode, stat);
	if (INODE_VERSION(inode) == MINIX_V1)
		stat->blocks = (BLOCK_SIZE / 512) * V1_minix_blocks(stat->size, sb);
	else
		stat->blocks = (sb->s_blocksize / 512) * V2_minix_blocks(stat->size, sb);
	stat->blksize = sb->s_blocksize;
	return 0;
}

/*
 * The function that is called for file truncation.
 */
void minix_truncate(struct inode * inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;
	if (INODE_VERSION(inode) == MINIX_V1)
		V1_minix_truncate(inode);
	else
		V2_minix_truncate(inode);
}

static struct dentry *minix_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, minix_fill_super);
}

static struct file_system_type minix_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "altminix",
	.mount		= minix_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("altminix");

static int __init init_minix_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&minix_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_minix_fs(void)
{
        unregister_filesystem(&minix_fs_type);
	destroy_inodecache();
}

module_init(init_minix_fs)
module_exit(exit_minix_fs)
MODULE_LICENSE("GPL");

