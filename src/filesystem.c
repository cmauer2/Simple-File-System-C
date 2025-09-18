/*
 * Authors: Carter Mauer & Cole Heausler
 * Course: CSC 4103 - Operating Systems
 * Instructor: Prof. Aisha Ali-Gombe
 * Date: May 6, 2025
 *
 * Prog 4 - A simple inode-based filesystem implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "filesystem.h"
#include "softwaredisk.h"

// constants for system limits and block ranges
#define INODE_BITMAP_BLOCK 1
#define FIRST_INODE_BLOCK 2
#define LAST_INODE_BLOCK 5
#define FIRST_DIR_ENTRY_BLOCK 6
#define LAST_DIR_ENTRY_BLOCK 69
#define FIRST_DATA_BLOCK 70
#define MAX_FILES 512
#define MAX_FILENAME_SIZE 507
#define INODES_PER_BLOCK 128
#define DIR_ENTRIES_PER_BLOCK 8
#define NUM_DIRECT_INODE_BLOCKS 13
#define NUM_SINGLE_INDIRECT_BLOCKS (SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint16_t))

// struct for directory entry
typedef struct {
    char filename[MAX_FILENAME_SIZE + 1];
    uint16_t inode_num;
    uint8_t used;
} DirectoryEntry;

// struct for an inode
typedef struct {
    uint16_t direct[NUM_DIRECT_INODE_BLOCKS];
    uint16_t indirect;
    uint32_t size;
} Inode;

// internal struct for keeping file state while open
struct FileInternals {
    int inode_num;
    FileMode mode;
    uint32_t pos;
};

FSError fserror = FS_NONE;

// function prototypes
static int alloc_inode();
static int alloc_data_block();
static void free_inode(int inode_num);
static void free_data_block(int block);
static int save_inode(int inode_num, Inode* node);
static int load_inode(int inode_num, Inode* node);
static int find_file_entry(char *name, DirectoryEntry *entry_out, int *block_out, int *index_out);
static int find_free_dir_entry(int *block_out, int *index_out);
static int get_data_block_for_offset(Inode *inode, int offset, int allocate);

// makes sure struct fits in the block
int check_structure_alignment()
{
    return sizeof(Inode) <= 64 && sizeof(DirectoryEntry) <= 512;
}

// allocates a free bit in bitmap block and returns its index
static int alloc_bitmap_block(int blocknum, int max_bits)
{
    uint8_t buf[SOFTWARE_DISK_BLOCK_SIZE];
    if (!read_sd_block(buf, blocknum)) return -1;
    for (int i = 0; i < max_bits; i++)
    {
        int byte = i / 8;
        int bit = i % 8;
        if (!(buf[byte] & (1 << bit)))
       	{
            buf[byte] |= (1 << bit);
            if (!write_sd_block(buf, blocknum)) return -1;
            return i;
        }
    }
    return -1;
}

// allocate a free inode
static int alloc_inode()
{
    int num = alloc_bitmap_block(INODE_BITMAP_BLOCK, MAX_FILES);
    if (num == -1) fserror = FS_OUT_OF_SPACE;
    return num;
}

// allocate a free data block
static int alloc_data_block()
{
    uint8_t bitmap[SOFTWARE_DISK_BLOCK_SIZE];

    if (!read_sd_block(bitmap, 0))
    {
	    fserror = FS_IO_ERROR;
	    return -1;
    }

    unsigned long total = software_disk_size();
    for (unsigned long block_num = FIRST_DATA_BLOCK; block_num < total; block_num++)
    {
	    int byte_index = block_num / 8;
	    int bit_index = block_num % 8;
	    if (!(bitmap[byte_index] & (1 << bit_index)))
	    {
		    bitmap[byte_index] |= (1 << bit_index);
		    if (!write_sd_block(bitmap, 0))
		    {
			    fserror = FS_IO_ERROR;
			    return -1;
		    }
		    return block_num;
	    }
    }
    fserror = FS_OUT_OF_SPACE;
    return -1;
}

// finds the block number that stores the inode
static int inode_block_for(int inode_num)
{
    return FIRST_INODE_BLOCK + inode_num / INODES_PER_BLOCK;
}

// gets inode index in its block
static int inode_offset_in_block(int inode_num)
{
    return inode_num % INODES_PER_BLOCK;
}

// saves inode to disk
static int save_inode(int inode_num, Inode* node)
{
    Inode inodes[INODES_PER_BLOCK];
    int block = inode_block_for(inode_num);
    if (!read_sd_block(inodes, block)) return 0;
    inodes[inode_offset_in_block(inode_num)] = *node;
    return write_sd_block(inodes, block);
}

// loads inode from disk
static int load_inode(int inode_num, Inode* node)
{
    Inode inodes[INODES_PER_BLOCK];
    int block = inode_block_for(inode_num);
    if (!read_sd_block(inodes, block)) return 0;
    *node = inodes[inode_offset_in_block(inode_num)];
    return 1;
}

// find directory entry by name 
static int find_file_entry(char *name, DirectoryEntry *entry_out, int *block_out, int *index_out)
{
    DirectoryEntry entries[DIR_ENTRIES_PER_BLOCK];
    for (int b = FIRST_DIR_ENTRY_BLOCK; b <= LAST_DIR_ENTRY_BLOCK; b++)
    {
        if (!read_sd_block(entries, b)) return 0;
        for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
       	{
            if (entries[i].used && strcmp(entries[i].filename, name) == 0)
	    {
                if (entry_out) *entry_out = entries[i];
                if (block_out) *block_out = b;
                if (index_out) *index_out = i;
                return 1;
            }
        }
    }
    return 0;
}

// finds free slot for new directory entry
static int find_free_dir_entry(int *block_out, int *index_out)
{
    DirectoryEntry entries[DIR_ENTRIES_PER_BLOCK];
    for (int b = FIRST_DIR_ENTRY_BLOCK; b <= LAST_DIR_ENTRY_BLOCK; b++)
    {
        if (!read_sd_block(entries, b)) return 0;
        for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
       	{
            if (!entries[i].used)
	    {
                *block_out = b;
                *index_out = i;
                return 1;
            }
        }
    }
    return 0;
}

// creates new file and adds to directory
File create_file(char *name)
{
    if (!name || name[0] == '\0')
    {
	    fserror = FS_ILLEGAL_FILENAME;
	    return NULL;
    }
    if (find_file_entry(name, NULL, NULL, NULL))
    {
	    fserror = FS_FILE_ALREADY_EXISTS;
	    return NULL;
    }
    int inode_num = alloc_inode();
    if (inode_num == -1) return NULL;

    Inode node = {0};
    if (!save_inode(inode_num, &node)) return NULL;

    int block, index;
    if (!find_free_dir_entry(&block, &index))
    {
	    fserror = FS_OUT_OF_SPACE;
	    return NULL;
    }
    DirectoryEntry entries[DIR_ENTRIES_PER_BLOCK];
    if (!read_sd_block(entries, block)) return NULL;

    strcpy(entries[index].filename, name);
    entries[index].inode_num = inode_num;
    entries[index].used = 1;
    if (!write_sd_block(entries, block)) return NULL;

    struct FileInternals *f = malloc(sizeof(struct FileInternals));
    f->inode_num = inode_num;
    f->mode = READ_WRITE;
    f->pos = 0;
    fserror = FS_NONE;
    return f;
}

// opens file from directory in their specified mode
File open_file(char *name, FileMode mode)
{
    DirectoryEntry entry;
    if (!find_file_entry(name, &entry, NULL, NULL))
    {
	    fserror = FS_FILE_NOT_FOUND;
	    return NULL;
    }

    struct FileInternals *f = malloc(sizeof(struct FileInternals));
    f->inode_num = entry.inode_num;
    f->mode = mode;
    f->pos = 0;
    fserror = FS_NONE;
    return f;
}

// closes an open file (free memory)
void close_file(File file)
{
    if (!file)
    {
	    fserror = FS_FILE_NOT_OPEN;
	    return;
    }
    free(file);
    fserror = FS_NONE;
}

// gets current size of file
unsigned long file_length(File file)
{
    Inode node;
    if (!file || !load_inode(file->inode_num, &node))
    {
	    fserror = FS_FILE_NOT_OPEN;
	    return 0;
    }
    return node.size;
}

// sets current position of file
int seek_file(File file, unsigned long bytepos)
{
    Inode node;
    if (!file || !load_inode(file->inode_num, &node))
    {
	    fserror = FS_FILE_NOT_OPEN;
	    return 0;
    }
    file->pos = bytepos;
    if (file->pos > node.size) node.size = file->pos; // grows the file if needed
    return save_inode(file->inode_num, &node);
}

// reads bytes from file into buffer
unsigned long read_file(File file, void *buf, unsigned long numbytes)
{
    if (!file)
    {
	    fserror = FS_FILE_NOT_OPEN;
	    return 0;
    }
    if (file->mode == READ_WRITE || file->mode == READ_ONLY)
    {
        Inode node;
        if (!load_inode(file->inode_num, &node)) return 0;

        uint8_t *dst = buf;
        unsigned long bytes_read = 0;
        while (bytes_read < numbytes && file->pos < node.size)
       	{
            int block_offset = file->pos / SOFTWARE_DISK_BLOCK_SIZE;
            int inner_offset = file->pos % SOFTWARE_DISK_BLOCK_SIZE;
            int block = get_data_block_for_offset(&node, block_offset, 0);
            if (block == -1) break;
            uint8_t tmp[SOFTWARE_DISK_BLOCK_SIZE];
            read_sd_block(tmp, block);
            int to_copy = SOFTWARE_DISK_BLOCK_SIZE - inner_offset;
            if (to_copy > (node.size - file->pos)) to_copy = node.size - file->pos;
            if (to_copy > (numbytes - bytes_read)) to_copy = numbytes - bytes_read;
            memcpy(dst + bytes_read, tmp + inner_offset, to_copy);
            bytes_read += to_copy;
            file->pos += to_copy;
        }
        return bytes_read;
    } else
    {
        fserror = FS_FILE_READ_ONLY;
        return 0;
    }
}

// writes bytes from buffer into file
unsigned long write_file(File file, void *buf, unsigned long numbytes)
{
    if (!file || file->mode != READ_WRITE)
    {
	    fserror = FS_FILE_READ_ONLY;
	    return 0;
    }
    Inode node;
    if (!load_inode(file->inode_num, &node)) return 0;

    uint8_t *src = buf;
    unsigned long bytes_written = 0;
    while (bytes_written < numbytes)
    {
        int block_offset = file->pos / SOFTWARE_DISK_BLOCK_SIZE;
        int inner_offset = file->pos % SOFTWARE_DISK_BLOCK_SIZE;
        int block = get_data_block_for_offset(&node, block_offset, 1);
        if (block == -1) break;
        uint8_t tmp[SOFTWARE_DISK_BLOCK_SIZE];
        read_sd_block(tmp, block);
        int to_copy = SOFTWARE_DISK_BLOCK_SIZE - inner_offset;
        if (to_copy > (numbytes - bytes_written)) to_copy = numbytes - bytes_written;
        memcpy(tmp + inner_offset, src + bytes_written, to_copy);
        write_sd_block(tmp, block);
        bytes_written += to_copy;
        file->pos += to_copy;
    }
    if (file->pos > node.size) node.size = file->pos;
    save_inode(file->inode_num, &node);
    return bytes_written;
}

// deletes file by name and frees up its resources
int delete_file(char *name)
{
    DirectoryEntry entry;
    int block, index;
    if (!find_file_entry(name, &entry, &block, &index))
    {
	    fserror = FS_FILE_NOT_FOUND;
	    return 0;
    }
    Inode node;
    if (!load_inode(entry.inode_num, &node))
    {
	    fserror = FS_IO_ERROR;
	    return 0;
    }
    for (int i = 0; i < NUM_DIRECT_INODE_BLOCKS; i++)
    {
	    if (node.direct[i] != 0)
	    {
		    free_data_block(node.direct[i]);
	    }
    }
    if (node.indirect != 0)
    {
	    uint16_t pointers[NUM_SINGLE_INDIRECT_BLOCKS];
	    read_sd_block(pointers, node.indirect);
	    for (int i = 0; i < NUM_SINGLE_INDIRECT_BLOCKS; i++)
	    {
		    if (pointers[i] != 0)
		    {
			    free_data_block(pointers[i]);
		    }
	    }
	    free_data_block(node.indirect);
    }
    DirectoryEntry entries[DIR_ENTRIES_PER_BLOCK];
    read_sd_block(entries, block);
    entries[index].used = 0;
    write_sd_block(entries, block);
    free_inode(entry.inode_num);
    fserror = FS_NONE;
    return 1;
}

int file_exists(char *name)
{
    fserror = FS_NONE;
    return find_file_entry(name, NULL, NULL, NULL);
}

// prints out the current error code
void fs_print_error(void)
{
    const char* msgs[] =
    {
        "FileSystem: No error.",
        "FileSystem: Out of space.",
        "FileSystem: File not open.",
        "FileSystem: File already open.",
        "FileSystem: File not found.",
        "FileSystem: File is read-only.",
        "FileSystem: File already exists.",
        "FileSystem: Exceeds max file size.",
        "FileSystem: Illegal filename.",
        "FileSystem: I/O error."
    };
    printf("%s\n", msgs[fserror]);
}

// frees a previously allocated inode
static void free_inode(int inode_num)
{
    uint8_t bitmap[SOFTWARE_DISK_BLOCK_SIZE];
    if (!read_sd_block(bitmap, INODE_BITMAP_BLOCK))
    {
        fserror = FS_IO_ERROR;
        return;
    }
    int byte = inode_num / 8;
    int bit = inode_num % 8;
    bitmap[byte] &= ~(1 << bit);
    write_sd_block(bitmap, INODE_BITMAP_BLOCK);
}

// frees a previously allocated data block
static void free_data_block(int block)
{
    uint8_t bitmap[SOFTWARE_DISK_BLOCK_SIZE];
    if (!read_sd_block(bitmap, 0))
    {
        fserror = FS_IO_ERROR;
        return;
    }
    int byte = block / 8;
    int bit = block % 8;
    bitmap[byte] &= ~(1 << bit);
    write_sd_block(bitmap, 0);
}

// resolves file offset into a data block and allocates if needed
static int get_data_block_for_offset(Inode *inode, int offset, int allocate) {
    if (offset < NUM_DIRECT_INODE_BLOCKS)
    {
        if (inode->direct[offset] == 0 && allocate)
       	{
            int b = alloc_data_block();
            if (b == -1) return -1;
            inode->direct[offset] = b;
        }
        return inode->direct[offset] == 0 ? -1 : inode->direct[offset];
    } else
    {
        // indirect block logic
        if (inode->indirect == 0)
       	{
            if (!allocate) return -1;
            int b = alloc_data_block();
            if (b == -1) return -1;
            inode->indirect = b;
            uint16_t empty[NUM_SINGLE_INDIRECT_BLOCKS] = {0};
            write_sd_block(empty, b);
        }

        uint16_t pointers[NUM_SINGLE_INDIRECT_BLOCKS];
        read_sd_block(pointers, inode->indirect);

        int index = offset - NUM_DIRECT_INODE_BLOCKS;
        if (index >= NUM_SINGLE_INDIRECT_BLOCKS)
       	{
            fserror = FS_EXCEEDS_MAX_FILE_SIZE;
            return -1;
        }

        if (pointers[index] == 0 && allocate)
       	{
            int b = alloc_data_block();
            if (b == -1) return -1;
            pointers[index] = b;
            write_sd_block(pointers, inode->indirect);
        }
        return pointers[index] == 0 ? -1 : pointers[index];
    }
}
