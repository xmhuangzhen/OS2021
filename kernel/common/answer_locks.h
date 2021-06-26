
//
// Created by Wenxin Zheng on 2021/4/21.
//

#ifndef ACMOS_SPR21_ANSWER_LOCKS_H
#define ACMOS_SPR21_ANSWER_LOCKS_H


int lock_init(struct lock *lock){
    /* Your code here */
    sync_lock_test_and_set(&lock->locked,0);
    if(nlock >= MAXLOCKS) BUG("Max lock count reached.");
    locks[nlock++] = lock;
    return 0;
}

void acquire(struct lock *lock){
    /* Your code here */
    if(!holding_lock(lock)){
        while(sync_lock_test_and_set(&lock->locked,1) != 0);
        sync_synchronize();
        lock->cpuid = cpuid();
    }
}

// Try to acquire the lock once
// Return 0 if succeed, -1 if failed.
int try_acquire(struct lock *lock){
    /* Your code here */
    if(!holding_lock(lock)){
        if(sync_lock_test_and_set(&lock->locked,1) == 0) {
            sync_synchronize();
            lock->cpuid = cpuid();
            return 0;
        }
    }
    return -1;
}

void release(struct lock* lock){
    /* Your code here */
    if(holding_lock(lock)){
        sync_lock_test_and_set(&lock->locked ,0);
        sync_synchronize();
        lock->cpuid = -1;
        sync_lock_release(&lock->locked);
    }
}

int is_locked(struct lock* lock){
    return lock->locked;
}

// private for spin lock
int holding_lock(struct lock* lock){
    /* Your code here */
    if(is_locked(lock) && cpuid() == lock->cpuid)
        return 1;
    return 0;
}

#endif  // ACMOS_SPR21_ANSWER_LOCKS_H
