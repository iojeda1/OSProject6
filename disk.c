/*
CSE 30341 Spring 2025 Flash Translation Assignment.
This is the flash translation layer.
You should write all your code here.
*/

#include "disk.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
Structure of the flash translation layer.
Go ahead and add or change things here as needed.
*/

struct disk {
	struct flash_drive *flash_drive;
	int nreads;
	int nwrites;
    int ndisk_blocks; // total disk blocks 
    int npages; // total flash pages
    int nblocks; // total flash blocks 
    int pages; // flash pages in one flash block 
    int *disk_to_flash; 
    int *flash_to_disk; 
    int *state; // free=0, valid=1, stale=2 
    int *erase; // erase count for each flash block
};

/*
Create a new flash translation layer for this flash drive f, and simulated number of blocks
Go ahead and add or change things here as needed.
*/

struct disk * disk_create( struct flash_drive *f, int disk_blocks )
{
	struct disk *d = malloc(sizeof(*d));
	d->flash_drive = f;
	d->nreads = 0;
	d->nwrites = 0;
    d->ndisk_blocks = disk_blocks; 
    d->npages = flash_npages(f); 
    d->pages = flash_npages_per_block(f); 
    d->nblocks =d->npages / d->pages; 
    d->disk_to_flash = malloc(sizeof(int) * d->ndisk_blocks); 
    d->flash_to_disk = malloc(sizeof(int) * d->npages); 
    d->state = malloc(sizeof(int) * d->npages); 
    d->erase = malloc(sizeof(int) * d->nblocks); 
    for (int i = 0; i < d->ndisk_blocks; i++) {
        d->disk_to_flash[i] = -1; 
    }
    for (int i = 0; i < d->npages; i++) {
        d->flash_to_disk[i] = -1; 
        d->state[i] = 0; // all are free at the beginning 
    }
    for (int i = 0; i < d->nblocks; i++) {
        d->erase[i] = 0; // all start with zero wear 
    }
    // DEBUG    
    printf("FTL Initialized:\n");
	printf("  disk blocks: %d\n", d->ndisk_blocks);
	printf("  flash pages: %d\n", d->npages);
	printf("  pages per block: %d\n", d->pages);
	printf("  total blocks: %d\n", d->nblocks);

	return d;
}

void clean(struct disk *d);  // Function declaration

/*
Read a disk block through the flash translation layer.
Go ahead and add or change things here as needed.
*/

int disk_read( struct disk *d, int disk_block, char *data )
{
	printf("disk_read: block %d\n",disk_block);

    int flash_page = d->disk_to_flash[disk_block]; // find flash page mapping to disk block
    if (flash_page == -1) { // check if block is unmapped 
        fprintf(stderr, "Error: block %d is unmapped.\n", disk_block); 
        exit(1); 
    }
	// read data from flash page 
	flash_read(d->flash_drive,flash_page,data);

	d->nreads++;
	return 0;
}

/*
Write a disk block through the flash translation layer.
Go ahead and add or change things here as needed.
*/

int disk_write( struct disk *d, int disk_block, const char *data )
{
	printf("disk_write: block %d\n",disk_block);

    int free = -1; // flag 
    for (int i = 0; i < d->npages; i++) {
        if (d->state[i] == 0) { // found a free page 
            free = i; 
            break; 
        }
    }
 
    if (free == -1) {
        clean(d); 
        for (int i = 0; i < d->npages; i++) {
            if (d->state[i] == 0) {
                free = i; 
                break; 
            }
        }
        if (free == -1) { // debug 
            fprintf(stderr, "disk_write Error: still no free flash pages after cleaning. \n");
            exit(1); 
        }
    }

    int old = d->disk_to_flash[disk_block]; 
    if (old != -1) {
        d->state[old] = 2; // if block not free, mark as stale 
        d->flash_to_disk[old] = -1; 
    }
	// write to new page
	flash_write(d->flash_drive, free, data); 
    d->disk_to_flash[disk_block] = free; 
    d->flash_to_disk[free] = disk_block; 
    d->state[free] = 1; 
	d->nwrites++;
	return 0;
}

/*
Report the total number of operations performed.
You can add more if you like here, but keep the display of reads and writes.
*/

void disk_report( struct disk *d )
{
	printf("\tdisk reads: %d\n",d->nreads);
	printf("\tdisk writes: %d\n",d->nwrites);
}

void clean(struct disk *d) {
    int block = -1;
    int min = 1000000; 
    // find a block with stale pages
    for (int i = 0; i < d->nblocks; i++) {
        int stale = 0; 
        for (int j = 0; j < d->pages; j++) {
            int page = i * d->pages + j;
            if (d->state[page] == 2) {
                stale = 1;
                break;
            }
        }

        if (stale && d->erase[i] < min) {
            block = i; 
            min = d->erase[i]; 
        }
    } 

    // DEBUG 
    printf("CLEANING: Erasing block %d...\n", block);


    struct {
        int disk_block;
        char data[FLASH_PAGE_SIZE];
    } buffer[d->pages];
    int val = 0; 
    // copy valid pages to new location
     for (int i = 0; i < d->pages; i++) {
        int old = block * d->pages + i; 
        if (d->state[old] == 1) {
            int b = d->flash_to_disk[old]; 
            flash_read(d->flash_drive, old, buffer[val].data); 
            buffer[val].disk_block = b; 
            val++; 
            d->state[old] = 2; 
            d->flash_to_disk[old] = -1; 
        }
      }
     flash_erase(d->flash_drive, block); 
     d->erase[block]++; 
    
     // free all pages in the block
     for (int i = 0; i < d->pages; i++) {
        int page = block * d->pages + i; 
        d->state[page] = 0; 
        d->flash_to_disk[page] = -1; 
    }

    // reassign buffer data 
    for (int i = 0; i < val; i++) {
        int new = -1;
        for (int j = 0; j < d->npages; j++) {
            if (d->state[j] == 0) {
                new = j;
                break;
            }
        }
        if (new == -1) { // debug
            fprintf(stderr, "Error: No free pages after erase.\n");
            exit(1);
        }

        flash_write(d->flash_drive, new, buffer[i].data);
        int b = buffer[i].disk_block;
        d->disk_to_flash[b] = new;
        d->flash_to_disk[new] = b;
        d->state[new] = 1;
    }
} 
