#include "process.h"
#include "lock.h"
#include "pagetable.h"
#include "elf.h"
#include "defs.h"
#include <memlayout.h>
//#include "../memory/answer_pgt.h"


//TO BE FIXED:
//(DONE)1. TRAMPOLINE and TRAPFRAME should be mapped at a fixed address instead of highest address
//(DONE)2. list in thread

extern void swtch(struct context*, struct context*); //swtch in tramp.s


extern const char binary_putc_start;
thread_t *running[NCPU];
struct list_head sched_list[NCPU];
struct lock pidlock, tidlock, schedlock;
process_t proc[NPROC];
thread_t thread[NTHREAD];
int _pid, _tid;
extern char trampoline[];
extern void usertrapret(void);

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

void push_off(void) {
    int old = intr_get();
    intr_off();
    if(mycpu()->noff == 0) mycpu()->intena = old;
    mycpu()->noff += 1;
}

void pop_off(void) {
    cpu_t *c = mycpu();
    if(intr_get()) DEBUG("pop_off - interruptible");
    if(c->noff < 1) DEBUG("pop_off");
    c->noff -= 1;
    if(c->noff == 0 && c->intena) intr_on();
}

cpu_t cpus[NCPU];

cpu_t* mycpu(void){
    int id = cpuid();
    cpu_t *c = &cpus[id];
    return c;
}

/*
process_t* myproc(void){
    push_off();
    cpu_t *cpu = mycpu();
    process_t *p=cpu->proc;
    pop_off();
    return p;
}
*/

thread_t* my_thread(void){
    push_off();
    cpu_t *cpu = mycpu();
    thread_t *thr = cpu->thr;
    pop_off();
    return thr;
}

// 将ELF文件映射到给定页表的地址空间，返回pc的数值
// 关于 ELF 文件，请参考：https://docs.oracle.com/cd/E19683-01/816-1386/chapter6-83432/index.html
static uint64 load_binary(pagetable_t *target_page_table, const char *bin){
	struct elf_file *elf;
    int i;
    uint64 seg_sz, p_vaddr, seg_map_sz;
	elf = elf_parse_file(bin);

	uint64 origin_sz=0;
	/* load each segment in the elf binary */
	for (i = 0; i < elf->header.e_phnum; ++i) {
		if (elf->p_headers[i].p_type == PT_LOAD) {
            // 根据 ELF 文件格式做段映射
            // 从ELF中获得这一段的段大小
            seg_sz = elf->p_headers[i].p_memsz;
            // 对应段的在内存中的虚拟地址
            p_vaddr = elf->p_headers[i].p_vaddr;
            // 对映射大小做页对齐
			seg_map_sz = ROUNDUP(seg_sz + p_vaddr, PGSIZE) - PGROUNDDOWN(p_vaddr);
            // 接下来代码的期望目的：将程序代码映射/复制到对应的内存空间
            // 一种可能的实现如下：
            /* 
             * 在 target_page_table 中分配一块大小
             * 通过 memcpy 将某一段复制进入这一块空间
             * 页表映射修改
             */

            int j,num,num_page;
            for(j = 0,num_page=0;j < seg_map_sz;++num_page,j+=PGSIZE){
                char* mem;
                mem = mm_kalloc();
                if(mem == NULL) DEBUG("loadbinary kalloc fail");

                memset(mem,0,PGSIZE);

                if(seg_sz - j < PGSIZE)
                    num = seg_sz-j;
                else
                    num = PGSIZE;

                memcpy(mem,bin+elf->p_headers[num_page].p_offset+j,num);//MAYBE
                if(pt_map_pages(*target_page_table,p_vaddr+j,(uint64)mem,PGSIZE,PTE_W|PTE_X|PTE_R|PTE_U) != 0){
                    DEBUG("load_binary map pages");
                }
            }
		}
	}
	/* PC: the entry point */
	return elf->header.e_entry;
}

void freewalk(pagetable_t pagetable){
    for(int i = 0;i < 512;++i){
        pte_t pte = pagetable[i];
        if((pte & PTE_V) &&(pte &(PTE_R|PTE_W|PTE_X))==0){
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        }else if(pte&PTE_V){
            DEBUG("freewalk boom");
        }
    }
    mm_kfree((void*)pagetable);
}

void proc_free_pagetable(pagetable_t pagetable, uint64 sz) {
    pt_unmap_addrs(pagetable, TRAMPOLINE);
    pt_unmap_addrs(pagetable, TRAPFRAME);
    if (sz > 0) {
        //free the addr
        uint64 npages = PGROUNDUP(sz)/PGSIZE;
        for(uint64 va = 0; va < npages*PGSIZE; va+=PGSIZE){
            pt_unmap_addrs(pagetable,va);
        }
    }
    freewalk(pagetable);
}


void free_thread(thread_t *thr){
    if(thr->trapframe != NULL){
        mm_kfree(thr->trapframe);
    }
    thr->proc.num_thread--;
    thr->thread_state = UNUSED;
}


void free_process(process_t *p){
    if(p->pagetable != NULL){
        proc_free_pagetable(p->pagetable,p->pagetable_sz);
    }
    //TODO
    if(p->num_thread == 0) p->process_state = UNUSED;
}

//user page table
pagetable_t proc_create_pagetable(process_t *p, thread_t *thr) {
    DEBUG("proc_create_pagetable-1\n");
    pagetable_t pagetable;
    if ((pagetable = mm_kalloc()) == NULL) return NULL;

    DEBUG("proc_create_pagetable-2\n");
    memset(pagetable, 0, PGSIZE);
    p->pagetable_sz = PGSIZE;

    DEBUG("proc_create_pagetable-3\n");
    if (pt_map_pages(p->pagetable, TRAMPOLINE, (uint64) trampoline, PGSIZE, PTE_R | PTE_X) < 0) {
        freewalk(p->pagetable);
        return 0;
    }

    DEBUG("proc_create_pagetable-4\n");
    if (pt_map_pages(p->pagetable, TRAPFRAME-(thr->pos_proc)*PGSIZE, (uint64) (thr->trapframe), PGSIZE, PTE_R | PTE_W) < 0) {
        pt_unmap_addrs(p->pagetable, TRAMPOLINE);
        freewalk(p->pagetable);
        return 0;
    }

    return pagetable;
}

/* 分配一个进程，需要至少完成以下目标：
 * 
 * 分配一个主线程
 * 创建一张进程页表
 * 分配pid、tid
 * 设置初始化线程上下文
 * 设置初始化线程返回地址寄存器ra，栈寄存器sp
 * 
 * 这个函数传入参数为一个二进制的代码和一个线程指针(具体传入规则可以自己修改)
 * 此外程序首次进入用户态之前，应该设置好trap处理向量为usertrap（或者你自定义的）
 */
process_t *alloc_proc(const char* bin, thread_t *thr){
    DEBUG("alloc_proc-1\n");
    process_t *p;
    for(p=proc;p<&proc[NPROC];++p){
        acquire(&p->proc_lock);
        if(p->process_state==UNUSED){
            goto success;
        } else {
            release(&p->proc_lock);
        }
    }
    return 0;

success:
    p->process_state = USED;
    DEBUG("alloc_proc-2\n");

    int thr_flag = 0;
    for(thr=thread;thr<&thread[NTHREAD];++thr){
        acquire(&thr->thr_lock);
        if(thr->thread_state == UNUSED){
            thr_flag = 1;
            break;
        } else {
            release(&thr->thr_lock);
        }
    }
    if(!thr_flag) return 0;

    DEBUG("alloc_proc-3\n");
    thr->pos_proc = p->num_thread;
    p->num_thread++;
    thr->thread_state = USED;
    thr->proc = *p;

    // allocate a main thread and a trapframe page
    if((thr->trapframe=(trapframe_t*)mm_kalloc())==NULL){
        free_thread(thr);
        release(&thr->thr_lock);
        free_process(p);
        release(&p->proc_lock);
        return 0;
    }

    DEBUG("alloc_proc-4\n");

    //create a process page table
    if((p->pagetable = proc_create_pagetable(p,thr)) == NULL){
        free_thread(thr);
        release(&thr->thr_lock);
        free_process(p);
        release(&p->proc_lock);
        return 0;
    }
    DEBUG("alloc_proc-5\n");

    load_binary(&p->pagetable,bin);
    DEBUG("alloc_proc-6\n");

    //allocate pid
    acquire(&pidlock);
    p->pid = _pid;
    _pid = _pid+1;
    release(&pidlock);

    //allocate tid
    acquire(&tidlock);
    thr->tid = _tid;
    _tid = _tid+1;
    release(&tidlock);

    thr->thr_kstack = KSTACK(thr->tid);
    //initialize thead context
    memset(&thr->thr_context,0,sizeof(thr->thr_context));

    //initialized thread ra,sp
    thr->thr_context.ra = (uint64)usertrapret;//MAYBE
    thr->thr_context.sp = thr->thr_kstack + PGSIZE;

    //trap vector --> usertrap
    trap_init_vec();

    return p;
}

bool load_thread(file_type_t type){
    if(type == PUTC){
        thread_t *t = NULL;
        DEBUG("load_thread -1\n");
        process_t *p = alloc_proc(&binary_putc_start, t);
        DEBUG("load_thread-2\n");
        if(!t) return false;
        sched_enqueue(t);
    } else {
        BUG("Not supported");
        return false;
    }
    return true;
}


// sched_enqueue和sched_dequeue的主要任务是加入一个任务到队列中和删除一个任务
// 这两个函数的展示了如何使用list.h中可的函数（加入、删除、判断空、取元素）
// 具体可以参考：Stackoverflow上的回答
// https://stackoverflow.com/questions/15832301/understanding-container-of-macro-in-the-linux-kernel
void sched_enqueue(thread_t *target_thread){
    if(target_thread->thread_state == RUNNING) BUG("Running Thread cannot be scheduled.");
    list_add(&target_thread->sched_list_thread_node, &(sched_list[cpuid()]));
}

thread_t *sched_dequeue(){
    if(list_empty(&(sched_list[cpuid()]))) BUG("Scheduler List is empty");
    thread_t *head = container_of(&(sched_list[cpuid()]), thread_t, sched_list_thread_node);
    list_del(&head->sched_list_thread_node);
    return head;
}

bool sched_empty(){
    return list_empty(&(sched_list[cpuid()]));
}

// 开始运行某个特定的函数
void thread_run(thread_t *target){
    //TODO
    acquire(&target->thr_lock);
    if(target->thread_state == RUNNABLE){
        target->thread_state = RUNNING;
        cpu_t *c = mycpu();
        c->thr = target;
        swtch(&c->context,&target->thr_context);
        c->thr = 0;
    }
    release(&target->thr_lock);
}

// sched_start函数启动调度，按照调度的队列开始运行。
void sched_start(){
    while(1){
        if(sched_empty()) BUG("Scheduler list empty, no app loaded");
        thread_t *next = sched_dequeue();
        thread_run(next);
    }
}

void sched_init(){
    // 初始化调度队列锁
    lock_init(&schedlock);
    // 初始化队列头
    init_list_head(&(sched_list[cpuid()]));
}


void proc_init(){
    // 初始化pid、tid锁
    lock_init(&pidlock);
    lock_init(&tidlock);
    // 接下来代码期望的目的：映射第一个用户线程并且插入调度队列
    _pid = 1; _tid = 1;
    process_t *p;
    for(p=proc;p<&proc[NPROC];++p){
        lock_init(&p->proc_lock);
        p->num_thread = 0;
    }
    thread_t *thr;
    for(thr=thread;thr<&thread[NTHREAD];++thr){
        lock_init(&thr->thr_lock);
        thr->thr_kstack = KSTACK((int)(thr-thread));
        thr->pos_proc = -1;
    }
    DEBUG("PROC_init -1\n");
    //TODO
    if(!load_thread(PUTC)) BUG("Load failed");
    DEBUG("proc_init -2 \n");
}

void sched(void){
    thread_t *thr = my_thread();
    if(!holding_lock(&thr->thr_lock)) BUG("sched p->thr_lock");
    if(mycpu()->noff != 1) BUG("sched cpu noff");
    if(thr->thread_state == RUNNING) BUG("sched thread state is running");
    if(intr_get()) BUG("sched interrupt");
    int intena = mycpu()->intena;
    swtch(&thr->thr_context,&mycpu()->context);////////TODO
    mycpu()->intena = intena;
}

void yield(void){
    thread_t *thr = my_thread();
    if(thr->thread_state != ZOMBIE) {
        acquire(&thr->thr_lock);
        thr->thread_state = RUNNABLE;
        sched();
        sched_enqueue(thr);
        release(&thr->thr_lock);
    }
}
