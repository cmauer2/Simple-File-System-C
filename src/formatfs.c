/*
 * Authors: Carter Mauer & Cole Heausler
 * Course: CSC 4103 - Operating Systems
 * Instructor: Prof. Aisha Ali-Gombe
 * Date: May 6, 2025
 *
 * Prog4 - A simple inode-based filesystem implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "softwaredisk.h"
#include "filesystem.h"

// constants for block numbers
#define INODE_BITMAP_BLOCK 1
#define FIRST_INODE_BLOCK 2
#define LAST_INODE_BLOCK 5
#define FIRST_DIR_ENTRY_BLOCK 6
#define LAST_DIR_ENTRY_BLOCK 69

int main()
{
	char block[SOFTWARE_DISK_BLOCK_SIZE]; // buffer used to zero out disk blocks
	
	// first step: initialize the software disk (clears out any existing content)
	if (!init_software_disk())
	{
		fprintf(stderr, "Failed to initialize the disk\n");
		sd_print_error();
		return 1;
	}
	printf("Software disk is initialized\n");

	// second step: check if the data structs match the expected size
	if (!check_structure_alignment())
	{
		fprintf(stderr, "Structure aligntment failed\n");
		return 1;
	}
	printf("Structure alignment is successful\n");

	// third step: clear inode bitmap to mark them all as free
	memset(block, 0, SOFTWARE_DISK_BLOCK_SIZE);
	if (!write_sd_block(block, INODE_BITMAP_BLOCK))
	{
		fprintf(stderr, "failed to clear inode bitmap\n");
		sd_print_error();
		return 1;
	}

	// fourth step: clear all the inode blocks (blocks that store inode structs)
	for (int i = FIRST_INODE_BLOCK; i <= LAST_INODE_BLOCK; i++)
	{
		if (!write_sd_block(block, i))
		{
			fprintf(stderr, "failed to clear inode block %d\n", i);
			sd_print_error();
			return 1;
		}
	}

	// final step: clear all directory entry blocks (remove all dir contents)
	for (int i = FIRST_DIR_ENTRY_BLOCK; i <= LAST_DIR_ENTRY_BLOCK; i++)
	{
		if (!write_sd_block(block, i))
		{
			fprintf(stderr, "failed to clear directory block %d\n", i);
			sd_print_error();
			return 1;
		}
	}

	// success message
	printf("Filesystem formatted successfully\n");
	return 0;
}

