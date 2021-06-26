#include <defs.h>
#include <riscv.h>
#include <syscall.h>
#include <process.h>
#include <defs.h>
#include "memlayout.h"

extern void kernelvec();
extern int devintr();
extern void usertrapret();

extern char trampoline[],usertrap1[],usertrap2[];

void trap_init_vec(){
    // Only 1 LoC
    // 将kernelvec作为内核中断处理基地址写入stvec向量。
    w_stvec((uint64)kernelvec);
}


void exit(int status){
    thread_t *thr = my_thread();
    acquire(&thr->thr_lock);
    thr->xstate = status;
    thr->thread_state = ZOMBIE;
    sched();
}

uint64 kern_sys_exit(){
    thread_t *thr = my_thread();
    exit((int)thr->trapframe->a0);
    return 0;
}

uint64 kern_sys_putc(){
    thread_t *thr = my_thread();
    uart_putc((int)thr->trapframe->a0);
    return 0;
}


uint64 kern_sys_yield(){
    yield();
    return 0;
}

static uint64 (*syscalls[])()={
        [SYS_EXIT] kern_sys_exit,
        [SYS_PUTC] kern_sys_putc,
        [SYS_YIELD] kern_sys_yield,
};


//number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

// 真实的 syscall 处理过程
// 根据你在 user/stdlib.h 中的 syscall 操作在这里对应地寻找目标
void syscall(){
    thread_t *thr = my_thread() ;
    int syscall_num = thr->trapframe->a7;
    if(syscall_num > 0 && syscall_num < NELEM(syscalls) && syscalls[syscall_num]){
        thr->trapframe->a0 = syscalls[syscall_num]();
    } else {
        DEBUG("unknown syscall");
        thr->trapframe->a0 = -1;
    }
}

// 用户态的trap处理函数，内核态请参考 kernel/boot/start.c 中的 kernelvec
//uservec --> usertrap -->  usertrapret -->  userret
//satp supervisor address translation and protection
void usertrap(void) {
    int which_dev = 0;
    if ((r_sstatus() & SSTATUS_SPP) != 0) BUG("usertrap: not from user mode");
    // 由于中断处理过程可能仍然被中断，所以设置中断处理向量为 kernelvec（内核态处理）
    w_stvec((uint64) kernelvec);
    /* 你需要在这个函数中做的事情（仅供参考，你可以有自己的设计，如果不是2阶段中断处理）：
     * 
     * 保存用户态的pc
     * 判断发生trap的类型：syscall、设备中断、时钟中断等
     * 完成处理，进入到trap后半部分处理函数
     */


    thread_t *thr = my_thread();
    thr->trapframe->epc = r_sepc(); //保存用户态的pc

    if (r_scause() == 8) {
        //syscall
        if (thr->killed) BUG("the thread has been killed no syscall!");
        thr->trapframe->epc += 4;
        intr_on();//sstatus = supervisor status
        syscall();
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else {
        BUG_FMT("usertrap(): unexpected scause %p\n", r_scause());
        BUG_FMT("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    }

    // 处理时钟中断：重新调度
    if (which_dev == 2) yield();
    // 进入 trap 后半处理函数
    usertrapret();
}

// Trap 后半处理函数
void usertrapret() {
    /* 你需要在这个函数中做的事情（仅供参考，你可以有自己的设计）：
     * 
     * 关闭中断
     * 重新设置程序的中断处理向量
     * 还原目标寄存器
     * 设置下一次进入内核的一些参数
     * 切换页表
     * 跳转到二进制代码还原现场的部分
     */

    //turn of interrupts until back into user space
    intr_off();

    w_stvec(TRAMPOLINE+usertrap1-trampoline);

    thread_t *thr = my_thread();
    // set up trapframe values that uservec will need when
    // the process next re-enters the kernel.
    thr->trapframe->kernel_satp = r_satp();         // kernel page table
    thr->trapframe->kernel_sp = thr->thr_kstack + PGSIZE; // process's kernel stack
    thr->trapframe->kernel_trap = (uint64)usertrap;
    thr->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(thr->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(thr->proc.pagetable);

    // jump to trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 fn = TRAMPOLINE + (usertrap2 - trampoline);
    ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);

}