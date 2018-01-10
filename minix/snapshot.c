#include <linux/buffer_head.h>

#include "minix.h"
#include "ioctl_basic.h"


// Frees all blocks associated with an inode (even indirect blocks themselves)
void free_blocks_of_inode(struct super_block *sb, struct minix2_inode *inode) {
	size_t i;
	struct inode fake_inode; // TODO: This isn't very nice...
	fake_inode.i_sb = sb;

	// Free direct data blocks
	for(i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		if(inode->i_zone[i] == 0) {
			break;
		}

		debug_log("\tInode contains data block %d", inode->i_zone[i]);

		minix_free_block(&fake_inode, inode->i_zone[i]);
	}

	// TODO: Indirect blocks
}

// Frees all blocks associated with an inode (even indirect blocks themselves)
void increment_refcount_for_blocks_of_inode(struct super_block *sb, struct minix2_inode *inode) {
	size_t i;
	struct minix_sb_info *sbi = minix_sb(sb);

	// Free direct data blocks
	for(i = 0; i < INDIRECT_BLOCK_INDEX; i++) {
		if(inode->i_zone[i] == 0) {
			break;
		}

		debug_log("\tInode contains data block %d", inode->i_zone[i]);

		increment_refcount(sbi, data_zone_index_for_zone_number(sbi, inode->i_zone[i]));
	}

	// TODO: Indirect blocks
}


// Frees all blocks associated with inodes found at the specified location
void do_for_blocks_of_inodes(struct super_block *sb, size_t imap_start_block, size_t inodes_start_block, void (*callback)(struct super_block*, struct minix2_inode*)) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t imap_block_i, bit, inode_i, inode_block_i, inode_block_offset;
	struct buffer_head *imap_bh, *inode_bh;
	struct minix2_inode *inode;

	PRINT_FUNC();

	for(imap_block_i = 0; imap_block_i < sbi->s_imap_blocks; imap_block_i++) {
		imap_bh = sb_bread(sb, imap_start_block + imap_block_i);

		for(bit = 0; bit < sb->s_blocksize << 3; bit++) {
			inode_i = imap_block_i * sb->s_blocksize_bits + bit;

			// There is no inode 0...
			// That's also why we remove one from inode_1 when we read the inode
			if(inode_i == 0) {
				continue;
			}

			if(inode_i >= sbi->s_ninodes) {
				break;
			}

			if(minix_test_bit(bit, imap_bh->b_data)) {
				debug_log("Found inode %ld in use\n", inode_i);

				// Read inode
				inode_block_i = (inode_i - 1) / (sb->s_blocksize / sizeof(struct minix2_inode));
				inode_block_offset = (inode_i - 1) % (sb->s_blocksize / sizeof(struct minix2_inode));
				
				debug_log("Inode is in block %ld at offset %ld\n", inode_block_i, inode_block_offset);

				inode_bh = sb_bread(sb, inodes_start_block + inode_block_i);
				inode = ((struct minix2_inode*)inode_bh->b_data) + inode_block_offset;

				callback(sb, inode);
			}
		}

	}
}


struct buffer_head *get_bh_to_snapshot_names(struct super_block *sb) {
	return sb_bread(sb, minix_sb(sb)->s_snapshots_start_block);
}


// Gets the ID of the next free snapshot slot, -1 if there is none
int get_free_snapshot_slot(struct super_block *sb) {
	struct buffer_head *snapshot_names_bh = get_bh_to_snapshot_names(sb);
	size_t i;

	// Read snapshot names
	for(i = 0; i < SNAPSHOT_NUM_SLOTS; i++) {
		// Check if the current slot is free
		if(snapshot_names_bh->b_data[i*SNAPSHOT_NAME_LENGTH] == '\0') {
			return i;
		}
	}

	return -1;
}


// Writes a snapshot name to a given slot
void write_snapshot_name(struct super_block *sb, int slot, char *name) {
	struct buffer_head *snapshot_names_bh = get_bh_to_snapshot_names(sb);
	size_t i;

	for(i = 0; i < SNAPSHOT_NAME_LENGTH; i++) {
		snapshot_names_bh->b_data[(slot * SNAPSHOT_NAME_LENGTH) + i] = name[i];
		if(name[i] == '\0') {
			break;
		}
	}

	snapshot_names_bh->b_data[strlen(name)] = '\0';

	mark_buffer_dirty(snapshot_names_bh);
	sync_dirty_buffer(snapshot_names_bh);
}


// Gets the slot of a given snapshot name
int get_slot_of_snapshot_name(struct super_block *sb, char *name) {
	struct buffer_head *snapshot_names_bh = get_bh_to_snapshot_names(sb);
	size_t i;

	if(strlen(name) == 0) {
		return -1;
	}

	// Read snapshot names
	for(i = 0; i < SNAPSHOT_NUM_SLOTS; i++) {
		// Check if the current slot is free
		if(strcmp(snapshot_names_bh->b_data + (i * SNAPSHOT_NAME_LENGTH), name) == 0) {
			debug_log("Found snapshot %s in slot %d", name, i);
			return i;
		}
	}

	return -1;
}


// Gets snapshot name in a given slot
char* get_snapshot_name_of_slot(struct super_block *sb, size_t slot) {
	struct buffer_head *snapshot_names_bh = get_bh_to_snapshot_names(sb);
	return snapshot_names_bh->b_data + (slot * SNAPSHOT_NAME_LENGTH);
}


size_t get_block_for_snapshot_slot(struct super_block *sb, int slot) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t snapshot_size = sbi->s_imap_blocks + sbi->s_inodes_blocks;
	return sbi->s_snapshots_start_block + SNAPSHOT_BLOCKS_FOR_NAMES + slot * snapshot_size;
}


// Creates a new snapshot
long create_snapshot(struct super_block *sb, char *name) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t i, read_block, write_block;
	struct buffer_head *read_bh, *write_bh;
	int slot;
	
	PRINT_FUNC();

	// Get free slot
	slot = get_free_snapshot_slot(sb);
	if(slot == -1) {
		debug_log("\tNo free slots for snapshot\n");
		return IOCTL_ERROR_NO_FREE_SNAPSHOTS;
	}

	// Check if name is free
	if(get_slot_of_snapshot_name(sb, name) != -1) {
		debug_log("\tName already exists\n");
		return IOCTL_ERROR_SNAPSHOT_EXISTS;
	}

	// Get buffer_head to snapshot slot
	write_block = get_block_for_snapshot_slot(sb, slot);
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

	// Increment refcount of currently referenced data blocks
	do_for_blocks_of_inodes(sb, 2, 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks, increment_refcount_for_blocks_of_inode);

	// Write snapshot name to table
	debug_log("\tPutting snapshot %s in slot %d\n", name, slot);
	write_snapshot_name(sb, slot, name);

	debug_log("\tPut snapshot %s in slot %d\n", name, slot);

	return 0;
}


// Rolls back to a given snapshot
long rollback_snapshot(struct super_block *sb, char *name) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t i, read_block, write_block;
	struct buffer_head *read_bh, *write_bh;
	int slot;

	PRINT_FUNC();

	// Find snapshot slot
	debug_log("\tShould rollback to snapshot %s\n", name);
	slot = get_slot_of_snapshot_name(sb, name);
	if(slot == -1) {
		debug_log("\tSnapshot does not exist\n");
		return IOCTL_ERROR_SNAPSHOT_DOES_NOT_EXIST;
	}

	// Remove current content
	do_for_blocks_of_inodes(sb, 2, 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks, free_blocks_of_inode);

	// Get buffer_head to snapshot
	read_block = get_block_for_snapshot_slot(sb, slot);
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

	// Increase refcount for current content
	do_for_blocks_of_inodes(sb, 2, 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks, increment_refcount_for_blocks_of_inode);

	return 0;
}


// Removes a given snapshot
long remove_snapshot(struct super_block *sb, char *name) {
	struct minix_sb_info *sbi = minix_sb(sb);
	size_t snapshot_block;
	int slot;

	PRINT_FUNC();

	// Find snapshot slot
	slot = get_slot_of_snapshot_name(sb, name);
	if(slot == -1) {
		debug_log("\tSnapshot does not exist\n");
		return IOCTL_ERROR_SNAPSHOT_DOES_NOT_EXIST;
	}
	snapshot_block = get_block_for_snapshot_slot(sb, slot);

	// Remove snapshot content
	do_for_blocks_of_inodes(sb, snapshot_block, snapshot_block + sbi->s_imap_blocks, free_blocks_of_inode);

	// Remove snapshot name
	write_snapshot_name(sb, slot, "");

	return 0;
}

long slot_of_snapshot(struct super_block *sb, char *name) {
	int slot;

	PRINT_FUNC();

	// Find snapshot slot
	slot = get_slot_of_snapshot_name(sb, name);
	if(slot == -1) {
		debug_log("\tSnapshot does not exist\n");
		return IOCTL_ERROR_SNAPSHOT_DOES_NOT_EXIST;
	}

	return slot;
}

long list_snapshots(struct super_block *sb, char *names) {
	size_t i;
	char *name;

	for(i = 0; i < SNAPSHOT_NUM_SLOTS; i++) {
		name = get_snapshot_name_of_slot(sb, i);
		strncpy(names + i * SNAPSHOT_NAME_LENGTH, name, SNAPSHOT_NAME_LENGTH);
	}

	return 0;
}