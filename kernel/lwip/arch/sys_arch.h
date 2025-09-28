#ifndef ARCH_SYS_ARCH_H
#define ARCH_SYS_ARCH_H

// -----------------------------------------------------------------------------
// 1) Include only the minimal xv6 headers you truly need for type definitions
//    (e.g., for spinlock, uint, etc.). Avoid including "proc.h" here if possible.
// -----------------------------------------------------------------------------
#include "types.h"       // for uint, etc.
#include "spinlock.h"    // for struct spinlock
#include "sleeplock.h"   // if you truly need sleeplock (not always needed)
#include "cc.h"          // lwIP's "compiler specific" definitions (if you use it)

// If you only need a pointer to 'struct proc', forward-declare it instead:
// (Remove #include "proc.h" unless you *really* need the full definition.)
struct proc;

// -----------------------------------------------------------------------------
// 2) Define the lwIP-specific sys_* types BEFORE including lwIP headers
// -----------------------------------------------------------------------------

// lwIP needs to know the size of your mailbox. If you want a fixed 10:
#ifndef SYS_MBOX_SIZE
#define SYS_MBOX_SIZE 10
#endif

// sys_mutex_t
typedef struct {
    struct spinlock lock; // Use a spinlock for the mutex
    int locked;           // Or remove/rename if you prefer the spinlock alone
} sys_mutex_t;

// sys_sem_t
typedef struct {
    struct spinlock lock;
    int count;
} sys_sem_t;

// sys_mbox_t
typedef struct {
    void     *msgs[SYS_MBOX_SIZE];
    int       head, tail;
    sys_sem_t full;
    sys_sem_t empty;
    struct spinlock lock;
} sys_mbox_t;

// sys_thread_t: on xv6, a thread can be represented by 'struct proc *'
typedef struct proc *sys_thread_t;

// sys_prot_t: used by sys_arch_protect(). Usually an integer or uint64 on RISC-V.
typedef uint64 sys_prot_t;

// -----------------------------------------------------------------------------
// 3) NOW include lwIP headers that reference these types
// -----------------------------------------------------------------------------
#include "lwip/err.h"
#include "lwip/sys.h"

// -----------------------------------------------------------------------------
// 4) Declare any global variables or functions you need
//    (but watch out for multiple definitions of the same thing)
// -----------------------------------------------------------------------------
extern uint ticks;  // from xv6 kernel

// If you need these from xv6, ensure you don't cause redefinition errors:
void initlock(struct spinlock*, char*);
void acquire(struct spinlock*);
void release(struct spinlock*);
void wakeup(void*);
void sleep(void*, struct spinlock*);
long strtol(const char *nptr, char **endptr, int base);
// If you have a "sleep_timeout()" function, declare it here if needed:
int sleep_timeout(void* chan, struct spinlock* lk, uint64 ticks_to_wait);

// -----------------------------------------------------------------------------
// 5) Provide function prototypes for your sys_arch.c implementations
//    (matching lwIP's expected signatures)
// -----------------------------------------------------------------------------

// Semaphores
err_t  sys_sem_new(sys_sem_t *sem, u8_t count);
void   sys_sem_free(sys_sem_t *sem);
void   sys_sem_signal(sys_sem_t *sem);
u32_t  sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout);

// Mailbox
err_t  sys_mbox_new(sys_mbox_t *mbox, int size);
void   sys_mbox_free(sys_mbox_t *mbox);
void   sys_mbox_post(sys_mbox_t *mbox, void *msg);
err_t  sys_mbox_trypost(sys_mbox_t *mbox, void *msg);
u32_t  sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout);

// Threads
sys_thread_t sys_thread_new(const char *name, void (*thread)(void *arg),
                            void *arg, int stacksize, int prio);

// Protection (interrupt disable/restore)
sys_prot_t sys_arch_protect(void);
void       sys_arch_unprotect(sys_prot_t p);

// If you really need PCI or other helper declarations, keep them here:
uint32 pci_config_read32(uint8 bus, uint8 slot, uint8 func, uint8 offset);
int    pci_find_device(uint16 vendor_id, uint16 device_id);
void   pci_init(void);
uint16 lwip_htons(uint16 x);
uint16 lwip_ntohs(uint16 x);

// If you want an allocproc_wrapper() or safestrcpy():
struct proc* allocproc_wrapper(void);
char* safestrcpy(char*, const char*, int);

#endif /* ARCH_SYS_ARCH_H */
