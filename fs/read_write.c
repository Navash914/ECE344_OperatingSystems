#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"

#define MAX_BLOCK_NR NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS + NR_INDIRECT_BLOCKS * NR_INDIRECT_BLOCKS
#define MAX_FILE_SIZE (long) (MAX_BLOCK_NR) * (long) (BLOCK_SIZE)

/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */
static int
testfs_read_block(struct inode *in, int log_block_nr, char *block)
{
	if (log_block_nr >= MAX_BLOCK_NR)
		return -EFBIG;
	int phy_block_nr = 0;

	assert(log_block_nr >= 0);
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		phy_block_nr = (int)in->in.i_block_nr[log_block_nr];
	} else {
		log_block_nr -= NR_DIRECT_BLOCKS;

		if (log_block_nr >= NR_INDIRECT_BLOCKS) {
			log_block_nr -= NR_INDIRECT_BLOCKS;
			int id_block_nr = log_block_nr / NR_INDIRECT_BLOCKS;
			if (id_block_nr <= NR_INDIRECT_BLOCKS) {
				log_block_nr = log_block_nr % NR_INDIRECT_BLOCKS;
				if (in->in.i_dindirect > 0) {
					read_blocks(in->sb, block, in->in.i_dindirect, 1);
					int id_block = ((int *)block)[id_block_nr];
					if (id_block > 0) {
						read_blocks(in->sb, block, id_block, 1);
						phy_block_nr = ((int *)block)[log_block_nr];
					}
				}
			}
		} else if (in->in.i_indirect > 0) {
			read_blocks(in->sb, block, in->in.i_indirect, 1);
			phy_block_nr = ((int *)block)[log_block_nr];
		}
	}
	if (phy_block_nr > 0) {
		read_blocks(in->sb, block, phy_block_nr, 1);
	} else {
		/* we support sparse files by zeroing out a block that is not
		 * allocated on disk. */
		bzero(block, BLOCK_SIZE);
	}
	return phy_block_nr;
}

int
testfs_read_data(struct inode *in, char *buf, off_t start, size_t size)
{
	if (start >= MAX_FILE_SIZE)
		return -EFBIG;
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // logical block number in the file
	long block_ix = start % BLOCK_SIZE; //  index or offset in the block
	int ret;
	size_t pos = 0;

	assert(buf);
	if (start + (off_t) size > in->in.i_size)
		size = in->in.i_size - start;

	while (size > 0) {
		if ((ret = testfs_read_block(in, block_nr, block)) < 0)
			return ret;
		size_t size_to_read = MIN((size_t) (BLOCK_SIZE - block_ix), size);
		memcpy(buf+pos, block + block_ix, size_to_read);
		pos += size_to_read;
		size -= size_to_read;
		block_nr++;
		block_ix = 0;
	}

	/* return the number of bytes read or any error */
	return pos;
}

/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int
testfs_allocate_block(struct inode *in, int log_block_nr, char *block)
{
	if (log_block_nr >= MAX_BLOCK_NR)
		return -EFBIG;
	int phy_block_nr;
	char indirect[BLOCK_SIZE], dindirect[BLOCK_SIZE];
	int indirect_allocated = 0, dindirect_allocated = 0;

	assert(log_block_nr >= 0);
	phy_block_nr = testfs_read_block(in, log_block_nr, block);

	/* phy_block_nr > 0: block exists, so we don't need to allocate it, 
	   phy_block_nr < 0: some error */
	if (phy_block_nr != 0)
		return phy_block_nr;

	/* allocate a direct block */
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		assert(in->in.i_block_nr[log_block_nr] == 0);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr >= 0) {
			in->in.i_block_nr[log_block_nr] = phy_block_nr;
		}
		return phy_block_nr;
	}

	log_block_nr -= NR_DIRECT_BLOCKS;
	if (log_block_nr >= NR_INDIRECT_BLOCKS) {
		log_block_nr -= NR_INDIRECT_BLOCKS;
		int id_block_nr = log_block_nr / NR_INDIRECT_BLOCKS;
		log_block_nr = log_block_nr % NR_INDIRECT_BLOCKS;

		if (in->in.i_dindirect == 0) {		/* allocate a double indirect block */
			bzero(dindirect, BLOCK_SIZE);
			phy_block_nr = testfs_alloc_block_for_inode(in);
			if (phy_block_nr < 0)
				return phy_block_nr;
			dindirect_allocated = 1;
			in->in.i_dindirect = phy_block_nr;
		} else {	/* read indirect block */
			read_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
		}

		/* allocate indirect block */
		int id_block = ((int *)dindirect)[id_block_nr];
		if (id_block == 0) {
			bzero(indirect, BLOCK_SIZE);
			phy_block_nr = testfs_alloc_block_for_inode(in);
			if (phy_block_nr < 0) {
				if (dindirect_allocated) {
					/* there was an error while allocating the indirect block, 
					* free the dindirect block that was previously allocated */
					testfs_free_block_from_inode(in, in->in.i_dindirect);
					in->in.i_dindirect = 0;
				}
				return phy_block_nr;
			}
			indirect_allocated = 1;
			id_block = phy_block_nr;
			((int *)dindirect)[id_block_nr] = id_block;
		} else {
			read_blocks(in->sb, indirect, id_block, 1);
		}

		/* allocate direct block */
		assert(((int *)indirect)[log_block_nr] == 0);	
		phy_block_nr = testfs_alloc_block_for_inode(in);

		if (phy_block_nr >= 0) {
			/* update indirect block */
			((int *)indirect)[log_block_nr] = phy_block_nr;
			write_blocks(in->sb, indirect, id_block, 1);
			if (indirect_allocated)
				write_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
		} else if (indirect_allocated) {
			testfs_free_block_from_inode(in, id_block);
			((int *)dindirect)[id_block_nr] = 0;

			if (dindirect_allocated) {
				testfs_free_block_from_inode(in, in->in.i_dindirect);
				in->in.i_dindirect = 0;
			}
		}

		return phy_block_nr;
	}
	
	if (in->in.i_indirect == 0) {	/* allocate an indirect block */
		bzero(indirect, BLOCK_SIZE);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr < 0)
			return phy_block_nr;
		indirect_allocated = 1;
		in->in.i_indirect = phy_block_nr;
	} else {	/* read indirect block */
		read_blocks(in->sb, indirect, in->in.i_indirect, 1);
	}

	/* allocate direct block */
	assert(((int *)indirect)[log_block_nr] == 0);	
	phy_block_nr = testfs_alloc_block_for_inode(in);

	if (phy_block_nr >= 0) {
		/* update indirect block */
		((int *)indirect)[log_block_nr] = phy_block_nr;
		write_blocks(in->sb, indirect, in->in.i_indirect, 1);
	} else if (indirect_allocated) {
		/* there was an error while allocating the direct block, 
		 * free the indirect block that was previously allocated */
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}
	return phy_block_nr;
}

int
testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size)
{
	if (start >= MAX_FILE_SIZE)
		return -EFBIG;
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // logical block number in the file
	long block_ix = start % BLOCK_SIZE; //  index or offset in the block
	int ret;
	size_t pos = 0;

	while (size > 0) {
		ret = testfs_allocate_block(in, block_nr, block);
		if (ret < 0) {
			if (pos > 0)
				in->in.i_size = MAX(in->in.i_size, start + (off_t) pos);
			return ret;
		}
		size_t size_to_write = MIN((size_t) (BLOCK_SIZE - block_ix), size);
		memcpy(block + block_ix, buf + pos, size_to_write);
		write_blocks(in->sb, block, ret, 1);
		pos += size_to_write;
		size -= size_to_write;
		block_nr++;
		block_ix = 0;
	}

	size = pos;

	if (size > 0) {
		in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
	}
	in->i_flags |= I_FLAGS_DIRTY;

	/* return the number of bytes written or any error */
	return size;
}

int
testfs_free_blocks(struct inode *in)
{
	int i;
	int e_block_nr;

	/* last logical block number */
	e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

	/* remove direct blocks */
	for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
		if (in->in.i_block_nr[i] == 0)
			continue;
		testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
		in->in.i_block_nr[i] = 0;
	}
	e_block_nr -= NR_DIRECT_BLOCKS;

	/* remove indirect blocks */
	if (in->in.i_indirect > 0) {
		char block[BLOCK_SIZE];
		assert(e_block_nr > 0);
		read_blocks(in->sb, block, in->in.i_indirect, 1);
		for (i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
			if (((int *)block)[i] == 0)
				continue;
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}

	e_block_nr -= NR_INDIRECT_BLOCKS;
	/* handle double indirect blocks */
	if (e_block_nr > 0) {
		if (in->in.i_dindirect > 0) {
			char did_block[BLOCK_SIZE], block[BLOCK_SIZE];
			assert(e_block_nr > 0);
			read_blocks(in->sb, did_block, in->in.i_dindirect, 1);
			for (i = 0; i < NR_INDIRECT_BLOCKS && e_block_nr > 0; i++) {
				if (((int *)did_block)[i] == 0) {
					e_block_nr -= NR_INDIRECT_BLOCKS;
					continue;
				}
				read_blocks(in->sb, block, ((int *)did_block)[i], 1);
				for (int j = 0; j < e_block_nr && j < NR_INDIRECT_BLOCKS; j++) {
					if (((int *)block)[j] == 0)
						continue;
					testfs_free_block_from_inode(in, ((int *)block)[j]);
					((int *)block)[j] = 0;
				}
				testfs_free_block_from_inode(in, ((int *)did_block)[i]);
				((int *)did_block)[i] = 0;
				e_block_nr -= NR_INDIRECT_BLOCKS;
			}
			testfs_free_block_from_inode(in, in->in.i_dindirect);
			in->in.i_dindirect = 0;
		}
	}

	assert(e_block_nr <= 0);
	in->in.i_size = 0;
	in->i_flags |= I_FLAGS_DIRTY;
	return 0;
}
