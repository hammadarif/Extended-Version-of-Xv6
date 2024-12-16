#ifndef ARCH_SYS_ARCH_H
#define ARCH_SYS_ARCH_H

#include "types.h"
//#include "defs.h"
#include "cc.h"
#include "spinlock.h"


typedef struct {
    struct spinlock lock; // Spinlock for mutual exclusion
    int locked;           // Mutex state (locked/unlocked)
} sys_mutex_t;

// Semaphore type
typedef struct {
    struct spinlock lock;
    int count; // Semaphore count
} sys_sem_t;

// Mailbox type (for inter-thread communication)
typedef struct {
    void *msgs[10]; // Fixed-size message queue
    int head, tail; // Queue pointers
    sys_sem_t full, empty; // Semaphores for full/empty slots
    struct spinlock lock;
} sys_mbox_t;

// Thread type
typedef struct proc* sys_thread_t;

// Protection type (interrupt disabling)
typedef uint64 sys_prot_t;

// Semaphore functions
err_t sys_sem_new(sys_sem_t *sem, u8_t count);
void sys_sem_free(sys_sem_t *sem);
void sys_sem_signal(sys_sem_t *sem);
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout);

// Mailbox functions
err_t sys_mbox_new(sys_mbox_t *mbox, int size);
void sys_mbox_free(sys_mbox_t *mbox);
void sys_mbox_post(sys_mbox_t *mbox, void *msg);
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg);
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout);

// Thread functions
sys_thread_t sys_thread_new(const char *name, void (*thread)(void *arg), void *arg, int stacksize, int prio);

// Protection functions
sys_prot_t sys_arch_protect(void);
void sys_arch_unprotect(sys_prot_t p);

//pci.c
uint32 pci_config_read32(uint8 bus, uint8 slot, uint8 func, uint8 offset);
int pci_find_device(uint16 vendor_id, uint16 device_id);
void pci_init(void);




#endif /* ARCH_SYS_ARCH_H */
