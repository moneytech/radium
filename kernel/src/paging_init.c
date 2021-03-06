#include "console.h"
#include "kernel_page.h"
#include "multiboot.h"
#include "paging.h"
#include "string.h"
#include "util.h"

static phys_t
kernel_end;

static phys_t
alloc_zeroed_page()
{
    phys_t page = page_alloc();
    memset32((void*)page, 0, 1024);
    return page;
}

static size_t
register_available_memory_region(multiboot_memory_map_t* region)
{
    size_t pages_registered = 0;

    for(size_t offset = 0; offset + PAGE_SIZE <= region->len; offset += PAGE_SIZE) {
        phys_t addr = region->addr + offset;

        if(addr < kernel_end) {
            // don't put memory before the end of the kernel in the free list
            continue;
        }

        page_free(addr);
        pages_registered++;
    }

    return pages_registered;
}

static size_t
register_available_memory(multiboot_info_t* mb)
{
    multiboot_memory_map_t* mmap = (void*)mb->mmap_addr;
    size_t pages_registered = 0;

    for(size_t i = 0; i < mb->mmap_length / sizeof(multiboot_memory_map_t); i++) {
        if(mmap[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            pages_registered += register_available_memory_region(mmap + i);
        }
    }

    return pages_registered;
}

static void
create_page_tables_for_kernel_space(uint32_t* page_directory)
{
    for(size_t i = 0; i < KERNEL_STACK_END / (4 * 1024 * 1024); i++) {
        page_directory[i] = alloc_zeroed_page() | PE_PRESENT | PE_READ_WRITE;
    }
}

static void
identity_map_kernel(uint32_t* page_directory)
{
    // we start looping from PAGE_SIZE in order to leave the null page unmapped
    // accessing it will cause a page fault.
    for(phys_t addr = PAGE_SIZE; addr <= kernel_end; addr += PAGE_SIZE) {
        size_t page_dir_i = addr / 4096 / 1024;
        size_t page_tab_i = addr / 4096 % 1024;
        uint32_t page_dir_ent = page_directory[page_dir_i];
        if(!page_dir_ent) {
            page_dir_ent = page_directory[page_dir_i] = alloc_zeroed_page() | PE_PRESENT | PE_READ_WRITE;
        }
        uint32_t* page_tab = (uint32_t*)(page_dir_ent & PE_ADDR_MASK);
        page_tab[page_tab_i] = addr | PE_PRESENT | PE_READ_WRITE;
    }
}

static void
recursively_map_page_directory(uint32_t* page_directory)
{
    page_directory[1023] = (phys_t)page_directory | PE_PRESENT | PE_READ_WRITE;
}

void
paging_set_allocatable_start(phys_t addr)
{
    if(addr > kernel_end) {
        kernel_end = round_up(addr, PAGE_SIZE);
    }
}

void
paging_init(multiboot_info_t* mb)
{
    extern int end_of_image;
    paging_set_allocatable_start((phys_t)&end_of_image);

    size_t pages_registered = register_available_memory(mb);
    printf("%d MiB available useful memory.\n", pages_registered * PAGE_SIZE / 1024 / 1024);

    uint32_t* page_directory = (uint32_t*)alloc_zeroed_page();

    create_page_tables_for_kernel_space(page_directory);
    identity_map_kernel(page_directory);
    recursively_map_page_directory(page_directory);

    set_page_directory((phys_t)page_directory);

    kernel_page_init(kernel_end, KERNEL_STACK_BEGIN);
}
