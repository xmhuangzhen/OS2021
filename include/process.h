//
// Created by Wenxin Zheng on 2021/3/5.
//

#ifndef ACMOS_SPR21_PROCESS_H
#define ACMOS_SPR21_PROCESS_H

#include <list.h>
#include <pagetable.h>
#include <lock.h>

typedef enum state { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE, IDLE, USED } process_state_t;
typedef enum file_type{PUTC} file_type_t;


typedef struct context{
    uint64 ra;
    uint64 sp;

    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
}context_t;



typedef struct trapframe{
    uint64 kernel_satp;//kernel page table
    uint64 kernel_sp;
    uint64 kernel_trap;
    uint64 epc;//user program counter
    uint64 kernel_hartid;//kernel tp
    uint64 ra;
    uint64 sp;
    uint64 gp;
    uint64 tp;
    uint64 t0;
    uint64 t1;
    uint64 t2;
    uint64 s0;
    uint64 s1;
    uint64 a0;
    uint64 a1;
    uint64 a2;
    uint64 a3;
    uint64 a4;
    uint64 a5;
    uint64 a6;
    uint64 a7;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
    uint64 t3;
    uint64 t4;
    uint64 t5;
    uint64 t6;
}trapframe_t;

typedef struct process {
    struct list_head thread_list;
    process_state_t process_state;
    // 以下部分请根据自己的需要自行填充

    struct lock proc_lock;

//    int killed;

  //  context_t proc_context;

//    uint64 proc_kstack;               // Virtual address of kernel stack
    pagetable_t pagetable; //user page table
    uint64 pagetable_sz;

    int pid; //process identifier
    int num_thread;
} process_t;

//process_t* myproc(void);

// 状态可以根据自己的需要进行修改
typedef process_state_t thread_state_t;

typedef struct thread {
    struct list_head process_list_thread_node;
    thread_state_t thread_state;
    struct list_head sched_list_thread_node;
    // 以下部分请根据自己的需要自行填充

    process_t proc;

    int tid; // thread id
    uint64 thr_kstack; //kernel stack

    trapframe_t *trapframe;
    context_t thr_context;

    struct lock thr_lock;

    int killed;

    int xstate;                  // Exit status to be returned to parent's wait

    int pos_proc;
} thread_t;

thread_t* my_thread(void);

typedef struct cpu{
//    struct process_t *proc;
//    struct context_t *context;

    thread_t *thr;
    context_t context;
    int noff;//depth of push_off() nesting
    int intena;//Were interrupts enabled before push_off()?
}cpu_t;

cpu_t* mycpu(void);


process_t *alloc_proc(const char* bin, thread_t *thr);
bool load_thread(file_type_t type);
void sched_enqueue(thread_t *target_thread);
thread_t *sched_dequeue();
bool sched_empty();
void sched_start();
void sched_init();
void proc_init();
void trap_init_vec();

void sched();
void yield();
/*
// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

//#define THR_KSTACK(tid) (TRAMPOLINE - ((tid)+1)*2*PGSIZE)
*/
#define NTHREAD 100

#endif  // ACMOS_SPR21_PROCESS_H
