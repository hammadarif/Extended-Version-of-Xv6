#include "sys_arch.h"

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    initlock(&sem->lock, "sys_sem");
    sem->count = count;
    return ERR_OK; // Return lwIP's success code
}

void sys_sem_free(sys_sem_t *sem) {
    // No-op in xv6 as memory is statically allocated
}

void sys_sem_signal(sys_sem_t *sem) {
    acquire(&sem->lock);
    sem->count++;
    wakeup(sem);
    release(&sem->lock);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    acquire(&sem->lock);
    while (sem->count == 0) {
        sleep(sem, &sem->lock);
    }
    sem->count--;
    release(&sem->lock);
    return 0; // Return 0 to indicate success
}

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    if (size > 10) return ERR_MEM; // Return error if size exceeds capacity
    initlock(&mbox->lock, "sys_mbox");
    sys_sem_new(&mbox->full, 0);  // Initially empty
    sys_sem_new(&mbox->empty, size); // Initially can accept all messages
    mbox->head = mbox->tail = 0;
    return ERR_OK; // Success
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    sys_arch_sem_wait(&mbox->empty, 0); // Wait for space
    acquire(&mbox->lock);
    mbox->msgs[mbox->tail] = msg;
    mbox->tail = (mbox->tail + 1) % 10;
    release(&mbox->lock);
    sys_sem_signal(&mbox->full); // Signal new message
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    sys_arch_sem_wait(&mbox->full, timeout); // Wait for a message
    acquire(&mbox->lock);
    *msg = mbox->msgs[mbox->head];
    mbox->head = (mbox->head + 1) % 10;
    release(&mbox->lock);
    sys_sem_signal(&mbox->empty); // Signal space available
    return 0; // Success
}
sys_thread_t sys_thread_new(const char *name, void (*thread)(void *arg), void *arg, int stacksize, int prio) {
    struct proc *p = allocproc();
    if (!p) return NULL;

    // Set up the process to run the thread function
    p->name = name;
    p->chan = 0;
    p->state = RUNNABLE;
    p->tf->epc = (uint64)thread;
    p->tf->a0 = (uint64)arg; // Pass argument to the thread function

    return p;
}

sys_prot_t sys_arch_protect(void) {
    uint64 old = r_sstatus();
    intr_off();
    return old;
}

void sys_arch_unprotect(sys_prot_t p) {
    w_sstatus(p);
}
