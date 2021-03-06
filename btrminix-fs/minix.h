#ifndef FS_MINIX_H
#define FS_MINIX_H

#include <linux/fs.h>
#include <linux/pagemap.h>
#include "minix_fs.h"

#define INODE_VERSION(inode)	minix_sb(inode->i_sb)->s_version
#define MINIX_V1		0x0001		/* original minix fs */
#define MINIX_V2		0x0002		/* minix V2 fs */
#define MINIX_V3		0x0003		/* minix V3 fs */

/*
 * minix fs inode data in memory
 */
struct minix_inode_info {
	union {
		__u16 i1_data[16];
		__u32 i2_data[16];
	} u;
	struct inode vfs_inode;
};

/*
 * minix super-block data in memory
 */
struct minix_sb_info {
	unsigned long s_ninodes;
	unsigned long s_nzones;
	unsigned long s_imap_blocks;
	unsigned long s_zmap_blocks;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	int s_dirsize;
	int s_namelen;
	struct buffer_head ** s_imap;
	struct buffer_head ** s_zmap;
	struct buffer_head * s_sbh;
	struct minix_super_block * s_ms;
	unsigned short s_mount_state;
	unsigned short s_version;
	__u32 s_inodes_blocks;
	__u32 s_refcount_table_blocks;
	struct buffer_head ** s_refcount_table;
	unsigned long s_snapshots_start_block;
	unsigned long s_snapshots_slots;
};

extern struct inode *minix_iget(struct super_block *, unsigned long);
extern struct minix_inode * minix_V1_raw_inode(struct super_block *, ino_t, struct buffer_head **);
extern struct minix2_inode * minix_V2_raw_inode(struct super_block *, ino_t, struct buffer_head **);
extern struct inode * minix_new_inode(const struct inode *, umode_t, int *);
extern void minix_free_inode(struct inode * inode);
extern unsigned long minix_count_free_inodes(struct super_block *sb);
extern int minix_new_block(struct inode * inode);
extern void minix_free_block(struct super_block *sb, unsigned long block);
extern unsigned long minix_count_free_blocks(struct super_block *sb);
extern int minix_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int minix_prepare_chunk(struct page *page, loff_t pos, unsigned len);

extern void V1_minix_truncate(struct inode *);
extern void V2_minix_truncate(struct inode *);
extern void minix_truncate(struct inode *);
extern void minix_set_inode(struct inode *, dev_t);
extern int V1_minix_get_block(struct inode *, long, struct buffer_head *, int);
extern int V2_minix_get_block(struct inode *, long, struct buffer_head *, int);
extern unsigned V1_minix_blocks(loff_t, struct super_block *);
extern unsigned V2_minix_blocks(loff_t, struct super_block *);
extern struct page * dir_get_page(struct inode *dir, unsigned long n);

extern struct minix_dir_entry *minix_find_entry(struct dentry*, struct page**);
extern int minix_add_link(struct dentry*, struct inode*);
extern int minix_delete_entry(struct minix_dir_entry*, struct page*);
extern int minix_make_empty(struct inode*, struct inode*);
extern int minix_empty_dir(struct inode*);
extern void minix_set_link(struct minix_dir_entry*, struct page*, struct inode*);
extern struct minix_dir_entry *minix_dotdot(struct inode*, struct page**);
extern ino_t minix_inode_by_name(struct dentry*);
extern void minix_destroy_inode(struct inode*);

extern inline uint32_t get_refcount(struct minix_sb_info *, size_t);
extern inline void set_refcount(struct minix_sb_info *, size_t, uint32_t);
extern inline uint32_t increment_refcount(struct minix_sb_info *, size_t);
extern inline uint32_t increment_refcount_snapshot_callback(struct super_block *, size_t);
extern inline uint32_t decrement_refcount(struct minix_sb_info *, size_t);
extern inline uint32_t data_zone_index_for_zone_number(struct minix_sb_info *, size_t);
extern void increment_refcounts_on_indirect_block(struct super_block *, uint32_t);

extern inline uint32_t deep_copy_block(struct inode *inode, uint32_t src_block_index);
extern inline void cow_block(struct minix_sb_info *sbi, struct inode *inode, uint32_t *block_index_ptr, bool deep_copy);
extern inline void cow_indirect_block(struct inode *inode, uint32_t *block_index_ptr, size_t *block_counter, bool deep_copy);
extern inline void cow_double_indirect_block(struct inode *inode, uint32_t *block_index_ptr, size_t *block_counter, bool deep_copy);

// Snapshots
long create_snapshot(struct super_block *sb, char *name);
long rollback_snapshot(struct super_block *sb, char *name);
long remove_snapshot(struct super_block *sb, char *name);
long slot_of_snapshot(struct super_block *sb, char *name);
long list_snapshots(struct super_block *sb, char* names);
size_t count_snapshots(struct super_block *sb);

extern const struct inode_operations minix_file_inode_operations;
extern const struct inode_operations minix_dir_inode_operations;
extern const struct file_operations minix_file_operations;
extern const struct file_operations minix_dir_operations;

static inline struct minix_sb_info *minix_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct minix_inode_info *minix_i(struct inode *inode)
{
	return container_of(inode, struct minix_inode_info, vfs_inode);
}

static inline unsigned minix_blocks_needed(unsigned bits, unsigned blocksize)
{
	return DIV_ROUND_UP(bits, blocksize * 8);
}

// For debugging
#define PRINT_TO_KERNEL_LOG	0

#if PRINT_TO_KERNEL_LOG == 1
	#define debug_log(...) _debug_log(__VA_ARGS__)
#else
	#define debug_log(...)
#endif

static inline void _debug_log(const char* format, ...) {
	// Declarations
	va_list arg;
	const char *prefix = "BTRMINIX: ";
	char result[strlen(prefix)+strlen(format)]; // +1 for the null-terminator

	// BTRMINIX prefix
    strcpy(result, prefix);
    strcat(result, format);

    if(!PRINT_TO_KERNEL_LOG) {
		return;
	}
	// Forward to printk()
	va_start (arg, format);
	vprintk ((const char*)result, arg);
	va_end (arg);
}

#define PRINT_FUNC() debug_log("%s called", __func__);

// Byte order specific stuff
#if defined(CONFIG_MINIX_FS_NATIVE_ENDIAN) && \
	defined(CONFIG_MINIX_FS_BIG_ENDIAN_16BIT_INDEXED)

#error Minix file system byte order broken

#elif defined(CONFIG_MINIX_FS_NATIVE_ENDIAN)

/*
 * big-endian 32 or 64 bit indexed bitmaps on big-endian system or
 * little-endian bitmaps on little-endian system
 */

#define minix_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr), (unsigned long *)(addr))
#define minix_set_bit(nr, addr)		\
	__set_bit((nr), (unsigned long *)(addr))
#define minix_test_and_clear_bit(nr, addr) \
	__test_and_clear_bit((nr), (unsigned long *)(addr))
#define minix_test_bit(nr, addr)		\
	test_bit((nr), (unsigned long *)(addr))
#define minix_find_first_zero_bit(addr, size) \
	find_first_zero_bit((unsigned long *)(addr), (size))

#elif defined(CONFIG_MINIX_FS_BIG_ENDIAN_16BIT_INDEXED)

/*
 * big-endian 16bit indexed bitmaps
 */

static inline int minix_find_first_zero_bit(const void *vaddr, unsigned size)
{
	const unsigned short *p = vaddr, *addr = vaddr;
	unsigned short num;

	if (!size)
		return 0;

	size >>= 4;
	while (*p++ == 0xffff) {
		if (--size == 0)
			return (p - addr) << 4;
	}

	num = *--p;
	return ((p - addr) << 4) + ffz(num);
}

#define minix_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr) ^ 16, (unsigned long *)(addr))
#define minix_set_bit(nr, addr)	\
	__set_bit((nr) ^ 16, (unsigned long *)(addr))
#define minix_test_and_clear_bit(nr, addr)	\
	__test_and_clear_bit((nr) ^ 16, (unsigned long *)(addr))

static inline int minix_test_bit(int nr, const void *vaddr)
{
	const unsigned short *p = vaddr;
	return (p[nr >> 4] & (1U << (nr & 15))) != 0;
}

#else

/*
 * little-endian bitmaps
 */

#define minix_test_and_set_bit	__test_and_set_bit_le
#define minix_set_bit		__set_bit_le
#define minix_test_and_clear_bit	__test_and_clear_bit_le
#define minix_test_bit	test_bit_le
#define minix_find_first_zero_bit	find_first_zero_bit_le

#endif

#endif /* FS_MINIX_H */
