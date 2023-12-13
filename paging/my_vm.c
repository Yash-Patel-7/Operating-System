#include "my_vm.h"

// TLB
tlb *tlb_store = NULL;

// physical memory
char *memory = NULL;

// number of physical pages
size_t num_physical_pages = 0;

// number of virtual pages
size_t num_virtual_pages = 0;

// bitmap for physical pages
char *physical_bitmap = NULL;

// size of physical bitmap in bytes
size_t physical_bitmap_size = 0;

// bitmap for virtual pages
char *virtual_bitmap = NULL;

// size of virtual bitmap in bytes
size_t virtual_bitmap_size = 0;

// number of offset bits for a page
size_t offset_bits = 0;

// number of bits in each level
size_t num_bits_per_level = 0;

// number of pages for each level
size_t num_pages_per_level = 0;

// number of bits in last level
size_t num_bits_last_level = 0;

// number of entries in a page
size_t num_entries_per_page = 0;

// create a mutex lock
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// keeps track of number of TLB misses
size_t tlb_misses = 0;

// keeps track of number of TLB lookups
size_t tlb_lookups = 0;

/* 
 * Function 1: EXTRACTING OUTER (TOP-ORDER) BITS
 */
static size_t get_top_bits(size_t value,  size_t num_bits)
{
	//Implement your code here
    // use bitshift to get rid of the lower bits
    size_t result = value >> ((sizeof(size_t) * 8) - num_bits);
    return result;
}


/* 
 * Function 2: SETTING A BIT AT AN INDEX 
 * Function to set a bit at "index" bitmap
 */
static void set_bit_at_index(char *bitmap, size_t index)
{
    //Implement your code here
    size_t bitmap_index = index / 8;
    size_t bit_index = index % 8;
    size_t number = 1 << bit_index;
    bitmap[bitmap_index] = bitmap[bitmap_index] | number;
}

// function that sets a bit at an index to 0
static void set_bit_at_index_zero(char *bitmap, size_t index) {
    //Implement your code here
    size_t bitmap_index = index / 8;
    size_t bit_index = index % 8;
    size_t number = 1 << bit_index;
    number = ~number;
    bitmap[bitmap_index] = bitmap[bitmap_index] & number;
}

/* 
 * Function 3: GETTING A BIT AT AN INDEX 
 * Function to get a bit at "index"
 */
static size_t get_bit_at_index(char *bitmap, size_t index)
{
    //Get to the location in the character bitmap array
    //Implement your code here
    size_t bitmap_index = index / 8;
    size_t bit_index = index % 8;
    size_t number = 1 << bit_index;
    size_t result = bitmap[bitmap_index] & number;
    result = result >> bit_index;
    return result;
}

// function that computes log base 2 of a number
size_t log_2(size_t number) {
    size_t result = 0;
    while (number >>= 1) {
        result++;
    }
    return result;
}

// function that always returns the ceiling of a number 
size_t ceiling(double number) {
    size_t result = (size_t)number;
    if (number - result > 0) {
        result++;
    }
    return result;
}

/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {

    //Allocate physical memory using mmap or malloc; this is the total size of
    //your memory you are simulating
    memory = (char *)malloc(MEMSIZE);

    // initialize the memory to 0
    memset(memory, 0, MEMSIZE);

    //HINT: Also calculate the number of physical and virtual pages and allocate
    //virtual and physical bitmaps and initialize them
    
    // calculate number of physical pages
    num_physical_pages = MEMSIZE / PGSIZE;

    // calculate number of virtual pages
    num_virtual_pages = MAX_MEMSIZE / PGSIZE;

    // calculate number of bytes for the physical bitmap
    physical_bitmap_size = num_physical_pages / 8;

    // calculate number of bytes for the virtual bitmap
    virtual_bitmap_size = num_virtual_pages / 8;

    // allocate physical bitmap
    physical_bitmap = (char *)malloc(physical_bitmap_size);

    // allocate virtual bitmap
    virtual_bitmap = (char *)malloc(virtual_bitmap_size);

    // initialize physical bitmap using memset
    memset(physical_bitmap, 0, physical_bitmap_size);

    // initialize virtual bitmap using memset
    memset(virtual_bitmap, 0, virtual_bitmap_size);

    // calculate number of offset bits
    offset_bits = log_2(PGSIZE);

    // calculate number of bits per level
    num_bits_per_level = ((sizeof(size_t) * 8) - offset_bits) / NUM_LEVELS;

    // calculate number of bits in last level
    num_bits_last_level = (((sizeof(size_t) * 8) - offset_bits) % num_bits_per_level) + num_bits_per_level;

    // compute the number of entries in a page
    num_entries_per_page = PGSIZE / sizeof(size_t);

    // compute number of pages per level
    num_pages_per_level = ceiling((double)(1 << num_bits_last_level) / num_entries_per_page);

    // set the physical bitmap for the page directory to 1
    // iterate over the number of pages in the page directory
    for (size_t i = 0; i < num_pages_per_level; i++) {
        // set the bit at the index to 1
        set_bit_at_index(physical_bitmap, i);
    }

    // if the number of levels is 1, then initalize the memory for the page directory to all 1's
    if (NUM_LEVELS == 1) {
        // initialize the page directory to all 1's
        memset((void*)memory, 255, num_pages_per_level * PGSIZE);
    }

    // initialize the tlb
    tlb_store = (tlb *)malloc(TLB_ENTRIES * sizeof(tlb));

    // initialize the tlb_store to all 1's
    memset((void*)tlb_store, 255, TLB_ENTRIES * sizeof(tlb));
}

// hash function that returns the index in the tlb_store array
size_t tlb_hash(void *vt) {
    return ((size_t)vt >> offset_bits) % TLB_ENTRIES;
}

/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int add_TLB(void *vt, void *pt) {

    /*Part 2 HINT: Add a virtual to physical page translation to the TLB */

    // get the tlb entry
    tlb_store[tlb_hash(vt)] = (tlb){vt, pt};

    return 0;
}

// function that removes a TLB entry given a virtual address
void remove_TLB(void *vt) {
    // index into the tlb_store array
    size_t index = tlb_hash(vt);

    // set the tlb entry to all 1's
    if (tlb_store[index].vt == vt) {
        tlb_store[index] = (tlb){(void*)~((size_t)0), (void*)~((size_t)0)};
    }
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
void* check_TLB(void *vt) {

    /* Part 2: TLB lookup code here */

    // increment the tlb lookups
    tlb_lookups++;

    // get the tlb entry
    tlb entry = tlb_store[tlb_hash(vt)];

    // if the virtual address matches, then return the physical address
    if (entry.vt == vt) {
        return entry.pt;
    }

    // increment the tlb misses
    tlb_misses++;

    return NULL;
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void print_TLB_missrate()
{
    // obtain the lock
    pthread_mutex_lock(&lock);

    double miss_rate = (double)tlb_misses / tlb_lookups;

    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);

    // free the lock
    pthread_mutex_unlock(&lock);
}



/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va) {
    /* Part 1 HINT: Get the Page directory index (1st level) Then get the
    * 2nd-level-page table index using the virtual address.  Using the page
    * directory index and page table index get the physical address.
    *
    * // treat va and pgdir as some memory address value like 01100101
    * 
    * Part 2 HINT: Check the TLB before performing the translation. If
    * translation exists, then you can return physical address from the TLB.
    */

    // check the TLB for a valid translation
    // if check_TLB returns a non-NULL value, then there is a valid translation
    // it returns the physical address which is the physical translation
    void *virtual_trans = (void*)((size_t)va & ~((1 << offset_bits) - 1));
    void *phys_trans = check_TLB(virtual_trans);

    if (phys_trans != NULL) {
        // get the page offset
        size_t page_offset = (size_t)va & ((1 << offset_bits) - 1);

        // get the physical address
        pte_t *physical_address = (pte_t *)((size_t)phys_trans | page_offset);

        return physical_address;
    }

    // iteratively walk through the page directory and get page table entry
    // use the number of bits per level and number of bits in last level based on number of levels
    // to get the page directory index and page table index
    // make sure that once you extract the current level's index, you right shift the virtual address by that many bits
    void *copy_va = va;

    pte_t *physical_address = NULL;

    size_t physical_translation = 0;

    pde_t *next_level = pgdir;

    // iterate through the page directory
    for (size_t i = 0; i < NUM_LEVELS; i++) {
        // if this is the last level, then this is the page table entry index
        // so set physical_translation to the page table entry
        if (i == NUM_LEVELS - 1) {
            // get the page table entry index
            size_t page_table_entry_index = get_top_bits((size_t)copy_va, num_bits_last_level);

            // getting the page table entry
            physical_translation = next_level[page_table_entry_index];

            if (physical_translation == ~((size_t)0)) {
                return NULL;
            }

            break;
        }
        
        // get the page directory index
        size_t page_directory_index = get_top_bits((size_t)copy_va, num_bits_per_level);

        if (next_level[page_directory_index] == 0) {
            return NULL;
        }

        // getting the next level directory
        next_level = (pde_t*)next_level[page_directory_index];
        
        // left shift the virtual address by the number of bits per level
        copy_va = (void *)((size_t)copy_va << num_bits_per_level);
    }

    // add the translation to the TLB
    add_TLB(virtual_trans, (void *)physical_translation);

    // get the page offset
    size_t page_offset = (size_t)va & ((1 << offset_bits) - 1);

    // get the physical address
    physical_address = (pte_t *)(physical_translation | page_offset);

    return physical_address;
}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int page_map(pde_t *pgdir, void *va, void *pa)
{

    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */

    void *copy_va = va;

    size_t pa_physical_translation = (size_t)pa & ~((1 << offset_bits) - 1);

    pte_t *physical_address = NULL;

    size_t physical_translation = 0;

    pde_t *next_level = pgdir;

    size_t page_table_entry_index = 0;

    // iterate through the page directory
    for (size_t i = 0; i < NUM_LEVELS; i++) {
        // if this is the last level, then this is the page table entry index
        // so set physical_translation to the page table entry
        if (i == NUM_LEVELS - 1) {
            // get the page table entry index
            page_table_entry_index = get_top_bits((size_t)copy_va, num_bits_last_level);

            // getting the page table entry
            physical_translation = (size_t)next_level[page_table_entry_index];
            break;
        }
        
        // get the page directory index
        size_t page_directory_index = get_top_bits((size_t)copy_va, num_bits_per_level);

        // if next level is 0, then there is no mapping
        if (next_level[page_directory_index] == 0) {
            // find next available contiguous physical pages for level
            size_t new_level_pg_num = 0;
            if (get_next_contig_avail_physical_pages(num_pages_per_level, &new_level_pg_num) == 1) {
                return -1;
            }

            // set the physical bitmap to 1 for the new level
            for (size_t j = 0; j < num_pages_per_level; j++) {
                set_bit_at_index(physical_bitmap, new_level_pg_num + j);
            }

            // set the page directory entry to the new level
            next_level[page_directory_index] = (pde_t)(memory + (new_level_pg_num * PGSIZE));

            if (i == NUM_LEVELS - 2) {
                // initialize the page table to all 1's
                memset((void*)next_level[page_directory_index], 255, num_pages_per_level * PGSIZE);
            } else {
                // initialize the new level to 0 based on number of pages per level
                memset((void*)next_level[page_directory_index], 0, num_pages_per_level * PGSIZE);
            }
        }

        // getting the next level directory
        next_level = (pde_t*)next_level[page_directory_index];
        
        // left shift the virtual address by the number of bits per level
        copy_va = (void *)((size_t)copy_va << num_bits_per_level);
    }

    // if the physical_translation is all 1's, then there is no mapping
    if (physical_translation == ~((size_t)0)) {
        // set the page table entry to the pa_physical_translation
        next_level[page_table_entry_index] = pa_physical_translation;

        if (check_TLB(va) == NULL) {
            // add the translation to the TLB
            add_TLB(va, (void*)pa_physical_translation);
        }

        return 0;
    }

    return -1;
}


// function that gets the next available physical page number (starting from 0)
size_t get_next_avail_physical_page(size_t *output) {
    // iterate through the physical bitmap to find the next available page
    for (size_t i = 0; i < num_physical_pages; i++) {
        if (get_bit_at_index(physical_bitmap, i) == 0) {
            *output = i;
            return 0;
        }
    }
    return 1;
}

// function that gets the next available physical page number with X number of contiguous available pages
size_t get_next_contig_avail_physical_pages(size_t num_pages, size_t *output) {
    // iterate through the physical bitmap to find the next available page
    for (size_t i = 0; i < num_physical_pages; i++) {
        // if the current page is available
        if (get_bit_at_index(physical_bitmap, i) == 0) {
            // check if the next X pages are available
            size_t j = 0;
            for (j = 0; j < num_pages; j++) {
                // if the next page is not available, then break
                if (get_bit_at_index(physical_bitmap, i + j) == 1) {
                    break;
                }
            }
            // if the next X pages are available, then return the page number of the first page
            if (j == num_pages) {
                *output = i;
                return 0;
            }
        }
    }
    return 1;
}

// function that gets the next available virtual page number with X number of contiuuous available pages
size_t get_next_contig_avail_virtual_pages(size_t num_pages, size_t *output) {
    // iterate through the virtual bitmap to find the next available page
    for (size_t i = 0; i < num_virtual_pages; i++) {
        // if the current page is available
        if (get_bit_at_index(virtual_bitmap, i) == 0) {
            // check if the next X pages are available
            size_t j = 0;
            for (j = 0; j < num_pages; j++) {
                // if the next page is not available, then break
                if (get_bit_at_index(virtual_bitmap, i + j) == 1) {
                    break;
                }
            }
            // if the next X pages are available, then return the page number of the first page
            if (j == num_pages) {
                *output = i;
                return 0;
            }
        }
    }
    return 1;
}


/* Function responsible for allocating pages
and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes) {

    /* 
     * HINT: If the physical memory is not yet initialized, then allocate and initialize.
     */

   /* 
    * HINT: If the page directory is not initialized, then initialize the
    * page directory. Next, using get_next_avail(), check if there are free pages. If
    * free pages are available, set the bitmaps and map a new page. Note, you will 
    * have to mark which physical pages are used. 
    */

    // obtain the lock
    pthread_mutex_lock(&lock);


    if (memory == NULL) {
        set_physical_mem();
    }

    if (num_bytes == 0) {
        // free the lock
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    // calculate the number of pages needed
    size_t num_pages_needed = ceiling((double)num_bytes / PGSIZE);

    // get the next available contiguous virtual page based on number of pages needed
    size_t virtual_page_num = 0;
    if (get_next_contig_avail_virtual_pages(num_pages_needed, &virtual_page_num) == 1) {
        // free the lock
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    // set the bitmap to 1 for the virtual page and physical page based on number of pages needed
    for (size_t i = 0; i < num_pages_needed; i++) {
        set_bit_at_index(virtual_bitmap, virtual_page_num + i);
    }

    // for each page, map the virtual page to the physical page
    for (size_t i = 0; i < num_pages_needed; i++) {
        // get the next available physical page
        size_t physical_page_num = 0;
        if (get_next_avail_physical_page(&physical_page_num) == 1) {
            // free the lock
            pthread_mutex_unlock(&lock);
            return NULL;
        }

        // set the bitmap to 1 for the physical page
        set_bit_at_index(physical_bitmap, physical_page_num);

        // get the physical address by making the non-offset bits to physical_page_num
        void *pa = (void *)((size_t)(physical_page_num) << offset_bits);

        // get the virtual address by making the non-offset bits to virtual_page_num
        void *va = (void *)((size_t)(virtual_page_num + i) << offset_bits);

        // map the virtual page to the physical page
        if (page_map((pde_t*)memory, va, pa) == -1) {
            // free the lock
            pthread_mutex_unlock(&lock);
            return NULL;
        }
    }

    // compute the virtual address of the first page
    void *virtual_address = (void *)((size_t)virtual_page_num << offset_bits);

    // free the lock
    pthread_mutex_unlock(&lock);

    // return the virtual address of the first page
    return virtual_address;
}

/* Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     *
     * Part 2: Also, remove the translation from the TLB
     */

    // obtain the lock
    pthread_mutex_lock(&lock);

    if (size == 0) {
        // free the lock
        pthread_mutex_unlock(&lock);
        return;
    }

    // make the offset bits 0 in va
    va = (void *)((size_t)va & ~((1 << offset_bits) - 1));

    // calculate number of pages based on size (number of bytes)
    size_t num_pages = ceiling((double)size / PGSIZE);

    // extract virtual page number from va, which are the non-offset bits
    size_t virtual_page_num = (size_t)va >> offset_bits;

    // iterate through virtual bitmap and check if the contiguous num_pages are allocated (1)
    for (size_t i = 0; i < num_pages; i++) {
        // if the virtual page is not allocated, then return
        if (get_bit_at_index(virtual_bitmap, virtual_page_num + i) == 0) {
            // free the lock
            pthread_mutex_unlock(&lock);
            return;
        }
    }

    // iterate through virtual bitmap and physical bitmap and set the bits to 0
    for (size_t i = 0; i < num_pages; i++) {
        set_bit_at_index_zero(virtual_bitmap, virtual_page_num + i);

        // compute the virtual address of the page
        void *virtual_address = (void *)((size_t)(virtual_page_num + i) << offset_bits);

        // translate the virtual address to get the physical address
        pte_t *physical_address = translate((pde_t*)memory, virtual_address);

        // get physical page number from physical address, which are the non-offset bits
        size_t physical_page_num = (size_t)physical_address >> offset_bits;

        // set the physical bitmap to 0 for the physical page
        set_bit_at_index_zero(physical_bitmap, physical_page_num);
    }

    // walk the page directory using va to get the page table entry
    for (size_t i = 0; i < num_pages; i++) {
        void* virtual_trans = (void*)((size_t)va + (i * PGSIZE));
        void* copy_va = virtual_trans;
        
        pde_t *next_level = (pde_t*)memory;

        size_t page_table_entry_index = 0;

        // iterate through the page directory
        for (size_t i = 0; i < NUM_LEVELS; i++) {
            // if this is the last level, then this is the page table entry index
            // so set physical_translation to the page table entry
            if (i == NUM_LEVELS - 1) {
                // get the page table entry index
                page_table_entry_index = get_top_bits((size_t)copy_va, num_bits_last_level);
                break;
            }
            
            // get the page directory index
            size_t page_directory_index = get_top_bits((size_t)copy_va, num_bits_per_level);

            // getting the next level directory
            next_level = (pde_t*)next_level[page_directory_index];
            
            // left shift the virtual address by the number of bits per level
            copy_va = (void *)((size_t)copy_va << num_bits_per_level);
        }

        // set the page table entry to all 1's
        next_level[page_table_entry_index] = ~((size_t)0);

        // remove the translation from the TLB
        remove_TLB(virtual_trans);
    }

    // free the lock
    pthread_mutex_unlock(&lock);
}


/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
int put_value(void *va, void *val, int size) {

    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */
    /*return -1 if put_value failed and 0 if put is successfull*/

    // obtain the lock
    pthread_mutex_lock(&lock);

    void *copy_va = va;

    while (true) {
        // translate the virtual address to get the physical address
        // we can only write until the end of the current page

        // compute remaining number of bytes until the end of the current page
        size_t remaining_size = PGSIZE - ((size_t)copy_va & ((1 << offset_bits) - 1));

        // get the smaller of either total size remaining or remaining size till end of current page
        size_t size_to_write = size < remaining_size ? size : remaining_size;

        pte_t *physical_address = translate((pde_t*)memory, copy_va);

        if (physical_address == NULL) {
            // free the lock
            pthread_mutex_unlock(&lock);
            printf("Cannot access memory\n");
            exit(1);
        }

        // extract the physical_translation part and offset part
        size_t physical_translation = (size_t)physical_address & ~((1 << offset_bits) - 1);
        size_t page_offset = (size_t)physical_address & ((1 << offset_bits) - 1);

        // compute the real memory address
        void *real_memory_address = (void*)(memory + ((physical_translation) * PGSIZE) + page_offset);

        // put the val into the real memory address
        memcpy(real_memory_address, val, size_to_write);

        // increment the copy_va by the size_to_write
        copy_va = (void *)((size_t)copy_va + size_to_write);

        // increment the val by the size_to_write
        val = (void *)((size_t)val + size_to_write);

        // decrement the size by the size_to_write
        size -= size_to_write;

        // if the size is 0, then we are done
        if (size == 0) {
            break;
        }
    }

    // free the lock
    pthread_mutex_unlock(&lock);

    return 0;
}


/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {

    /* HINT: put the values pointed to by "va" inside the physical memory at given
    * "val" address. Assume you can access "val" directly by derefencing them.
    */

    // obtain the lock
    pthread_mutex_lock(&lock);

    void *copy_va = va;

    while (true) {
        // translate the virtual address to get the physical address
        // we can only read until the end of the current page

        // compute remaining number of bytes until the end of the current page
        size_t remaining_size = PGSIZE - ((size_t)copy_va & ((1 << offset_bits) - 1));

        // get the smaller of either total size remaining or remaining size till end of current page
        size_t size_to_read = size < remaining_size ? size : remaining_size;

        pte_t *physical_address = translate((pde_t*)memory, copy_va);

        if (physical_address == NULL) {
            // free the lock
            pthread_mutex_unlock(&lock);
            printf("Cannot access memory\n");
            exit(1);
        }

        // extract the physical_translation part and offset part
        size_t physical_translation = (size_t)physical_address & ~((1 << offset_bits) - 1);
        size_t page_offset = (size_t)physical_address & ((1 << offset_bits) - 1);

        // compute the real memory address
        void *real_memory_address = (void*)(memory + ((physical_translation) * PGSIZE) + page_offset);

        // get the val from the real memory address
        memcpy(val, real_memory_address, size_to_read);

        // increment the copy_va by the size_to_read
        copy_va = (void *)((size_t)copy_va + size_to_read);

        // increment the val by the size_to_read
        val = (void *)((size_t)val + size_to_read);

        // decrement the size by the size_to_read
        size -= size_to_read;

        // if the size is 0, then we are done
        if (size == 0) {
            break;
        }
    }

    // free the lock
    pthread_mutex_unlock(&lock);
}


/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));
                // printf("Values at the index: %d, %d, %d, %d, %d\n", 
                //     a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}


