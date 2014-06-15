#include "console.h"
#include "gdt.h"
#include "kernel_page.h"
#include "paging.h"
#include "panic.h"
#include "string.h"
#include "task.h"
#include "util.h"

static_assert(tss_t_is_0x68_bytes_long, sizeof(tss_t) == 0x68);

static tss_t
tss;

void
task_init()
{
    gdt_set_tss(GDT_TSS, (uint32_t)&tss, sizeof(tss));
    gdt_reload();

    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = KERNEL_STACK_END;

    // pointer to the IO permission bitmap is beyond the end of the segment
    tss.iopb = sizeof(tss);

    __asm__ volatile("ltr ax" :: "a"((uint16_t)GDT_TSS | 3));
}

void
task_new(task_t* task, const char* name)
{
    strlcpy(task->name, name, sizeof(task->name));

    // initialise task page directory
    uint32_t* current_page_directory = (uint32_t*)CURRENT_PAGE_DIRECTORY;
    uint32_t* task_page_directory = kernel_page_alloc_zeroed();

    if(!task_page_directory) {
        panic("could not allocate page for new task's page directory");
    }

    for(size_t i = 0; i < KERNEL_STACK_BEGIN / (4*1024*1024); i++) {
        task_page_directory[i] = current_page_directory[i];
    }

    task->page_directory = task_page_directory;
    task->page_directory_phys = virt_to_phys((virt_t)task_page_directory);
    task->page_directory[1023] = task->page_directory_phys | PE_PRESENT | PE_READ_WRITE;

    // initialize kernel stack page table for new task
    uint32_t* kernel_stack_page_table = kernel_page_alloc_zeroed();
    task->page_directory[KERNEL_STACK_BEGIN / (4*1024*1024)] = virt_to_phys((virt_t)kernel_stack_page_table) | PE_PRESENT | PE_READ_WRITE;

    task->kernel_stack = kernel_page_alloc_zeroed();
    kernel_stack_page_table[1023] = virt_to_phys((virt_t)task->kernel_stack) | PE_PRESENT | PE_READ_WRITE;
}

void
task_destroy(task_t* task)
{
    kernel_page_free(task->kernel_stack);
    kernel_page_free((void*)(task->page_directory[KERNEL_STACK_BEGIN / (4*1024*1024)] & PE_ADDR_MASK));
    kernel_page_free(task->page_directory);
    memset(task, 0, sizeof(*task));
}
