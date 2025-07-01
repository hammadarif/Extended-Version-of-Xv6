// sys_arch.c (without using tickslock)
#include "sys_arch.h"
#include "riscv.h"
#include "defs.h"         // For sleep_timeout(), wakeup(), etc.
//#include "trapframe.h"    // For trapframe definition
#include "spinlock.h"     // If still needed for other locks (but not ticks)
#include "string.h"       // For safestrcpy if needed
#include "proc.h"

// If not already defined in lwIP or elsewhere, define:
#ifndef SYS_ARCH_TIMEOUT
#define SYS_ARCH_TIMEOUT ((u32_t)0xffffffffUL)
#endif

// Make sure we have a consistent mailbox size
#ifndef SYS_MBOX_SIZE
#define SYS_MBOX_SIZE 10
#endif

// -----------------------------------------------------------------------------
// Global 'ticks' from xv6
// We do NOT lock around this read. We simply declare it as extern.
extern uint ticks;  

// -----------------------------------------------------------------------------
// Semaphores
// -----------------------------------------------------------------------------
#if 0
err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
    initlock(&sem->lock, "sys_sem");
    sem->count = count;
    return ERR_OK; // lwIP success
}

void
sys_sem_free(sys_sem_t *sem)
{
    // No-op in xv6 if semaphores are statically allocated
}

void
sys_sem_signal(sys_sem_t *sem)
{
    acquire(&sem->lock);
    sem->count++;
    wakeup(sem);
    release(&sem->lock);
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
    // Capture the starting time (in ticks)
    // No lock around ticks here
    uint start_ticks = ticks;  
    u32_t elapsed = 0;

    acquire(&sem->lock);
    while (sem->count == 0) {
        if (timeout_ms != 0) {
            // Convert the timeout from ms to ticks (assume 10ms per tick)
            uint64 wait_ticks = (timeout_ms + 9) / 10; // round up

            // sleep_timeout() is assumed to return -1 on timeout
            if (sleep_timeout(sem, &sem->lock, wait_ticks) == -1) {
                // Timed out
                release(&sem->lock);
                return SYS_ARCH_TIMEOUT; // 0xFFFFFFFF
            }
        } else {
            // No timeout => block indefinitely
            sleep(sem, &sem->lock);
        }
    }

    // We have the semaphore
    sem->count--;

    // Calculate elapsed time in ms without locking ticks
    {
        uint now_ticks = ticks;  // read current ticks
        elapsed = (now_ticks - start_ticks) * 10;  // each tick = 10 ms
    }

    release(&sem->lock);
    return elapsed; 
}

// -----------------------------------------------------------------------------
// Mailbox
// -----------------------------------------------------------------------------

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
    // Ensure size doesn't exceed our fixed capacity
    if (size > SYS_MBOX_SIZE)
        return ERR_MEM; 

    initlock(&mbox->lock, "sys_mbox");
    // 'full' tracks how many slots are filled
    // 'empty' tracks how many slots are free
    sys_sem_new(&mbox->full, 0);
    sys_sem_new(&mbox->empty, size);

    mbox->head = 0;
    mbox->tail = 0;
    return ERR_OK;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    // Wait indefinitely for an empty slot
    sys_arch_sem_wait(&mbox->empty, 0);

    acquire(&mbox->lock);
    mbox->msgs[mbox->tail] = msg;
    mbox->tail = (mbox->tail + 1) % SYS_MBOX_SIZE;
    release(&mbox->lock);

    // Signal that one more slot is now full
    sys_sem_signal(&mbox->full);
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
    // 1) Wait on 'full' with a timeout
    u32_t wait_time = sys_arch_sem_wait(&mbox->full, timeout_ms);
    if (wait_time == SYS_ARCH_TIMEOUT) {
        // Timed out before we got a message
        if (msg) {
            *msg = NULL; 
        }
        return SYS_ARCH_TIMEOUT;
    }

    // 2) We have a message
    acquire(&mbox->lock);
    if (msg) {
        *msg = mbox->msgs[mbox->head];
    }
    mbox->head = (mbox->head + 1) % SYS_MBOX_SIZE;
    release(&mbox->lock);

    // 3) Signal that one slot is now free
    sys_sem_signal(&mbox->empty);

    // 4) Return how long we waited (in ms)
    return wait_time;
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    acquire(&mbox->lock);

    // Check if the queue is full
    if (((mbox->tail + 1) % SYS_MBOX_SIZE) == mbox->head) {
        // Full mailbox
        release(&mbox->lock);
        return ERR_MEM; 
    }

    // Post the message
    mbox->msgs[mbox->tail] = msg;
    mbox->tail = (mbox->tail + 1) % SYS_MBOX_SIZE;

    release(&mbox->lock);
    sys_sem_signal(&mbox->full);
    return ERR_OK;
}

int
sys_mbox_valid(sys_mbox_t *mbox)
{
    return (mbox != NULL);
}

// -----------------------------------------------------------------------------
// Mutex
// -----------------------------------------------------------------------------

err_t
sys_mutex_new(sys_mutex_t *mutex)
{
    initlock(&mutex->lock, "sys_mutex");
    return ERR_OK;
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
    acquire(&mutex->lock);
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
    release(&mutex->lock);
}

// -----------------------------------------------------------------------------
// Thread Creation
// -----------------------------------------------------------------------------

sys_thread_t
sys_thread_new(const char *name,
               void (*thread)(void *arg),
               void *arg,
               int stacksize,
               int prio)
{
    // NOTE: Adjust as needed if you want a kernel-level or user-level thread.
    struct proc *p = allocproc_wrapper();
    if (!p)
        return NULL;

    safestrcpy(p->name, name, sizeof(p->name));
    p->chan  = 0;
    p->state = RUNNABLE;

    // For user-level threads, trapframe->epc is user PC
    // For kernel-level threads, you may need a kernel trampoline approach
    p->trapframe->epc = (uint64)thread;
    p->trapframe->a0  = (uint64)arg;

    // stacksize, prio are unused in vanilla xv6
    // lwIP passes them, but we ignore them here
    return p;
}

// -----------------------------------------------------------------------------
// Interrupt Protection
// -----------------------------------------------------------------------------
#endif // 0
sys_prot_t
sys_arch_protect(void)
{
    uint64 old = r_sstatus();
    intr_off(); // globally disable interrupts
    return old;
}

void
sys_arch_unprotect(sys_prot_t p)
{
    w_sstatus(p); // restore previous sstatus
}

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

u32_t
sys_now(void)
{
    // Return a millisecond timestamp without using tickslock.
    // If 'ticks' increments at 100Hz, each increment = 10 ms
    // The read below is unlocked.
    return (u32_t)(ticks * 10);
}

/*
// Byte-order functions for lwIP
uint16_t
lwip_htons(uint16_t x)
{
    return ((x & 0x00ffU) << 8) | ((x & 0xff00U) >> 8);
}

uint16_t
lwip_ntohs(uint16_t x)
{
    return lwip_htons(x);
}
*/


int
sleep_timeout(void *chan, struct spinlock *lk, uint64 wait_ticks)
{
  // Record the time (in ticks) at which we started sleeping.
  uint start = ticks;

  // This will release 'lk' and block the current process until
  // another thread calls 'wakeup(chan)' or the process is scheduled again.
  sleep(chan, lk);

  // After waking up (for any reason), lock 'lk' has been re-acquired by xv6.
  // Check how long we've been asleep:
  if ((ticks - start) >= wait_ticks) {
    // More than 'wait_ticks' ticks have passed.
    // We consider that a timeout.
    return -1; 
  }

  // Otherwise, we woke up (probably because someone called wakeup(chan))
  // *before* our desired wait time elapsed.
  return 0;
}

void sys_init(void) {
    // No special init needed for xv6 port
}

//redefination of strol 
long
strtol(const char *nptr, char **endptr, int base)
{
    long result = 0;
    int sign = 1;

    /* 1) Skip leading whitespace */
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' ||
           *nptr == '\r' || *nptr == '\f' || *nptr == '\v') {
        nptr++;
    }

    /* 2) Check for optional sign */
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    /* If base is zero or 10, assume decimal by default. 
       For a minimal approach, we just default to decimal. */
    if (base == 0) {
        base = 10;
    }

    /* 3) Parse digits until we hit a non-digit or the end of string */
    while (*nptr) {
        int digit;

        /* Convert character to its digit value */
        if (*nptr >= '0' && *nptr <= '9') {
            digit = *nptr - '0';
        } else {
            /* Non-digit character, stop. */
            break;
        }

        /* If digit is invalid for the given base, stop. */
        if (digit >= base) {
            break;
        }

        /* Accumulate into result */
        result = result * base + digit;
        nptr++;
    }

    /* 4) If endptr is not NULL, set it to the point where we stopped */
    if (endptr) {
        *endptr = (char *)nptr;
    }

    return sign * result;
}