#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "virtio.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
// System call to send a network packet
uint64 sys_send_packet() {
    uint64 data;
    int len;

    // Extract arguments
    argaddr(0, &data);
    argint(1, &len);

    // Validate arguments
    if (data == 0 || len <= 0) {
        return -1; // Error
    }

    char buffer[PACKET_SIZE];
    if (copyin(myproc()->pagetable, buffer, data, len) < 0) {
        return -1; // Error during copyin
    }

    virtio_send_packet(buffer, len);
    return 0; // Success
}


// System call to receive a network packet
uint64 sys_recv_packet() {
    uint64 data;
    int buflen;

    // Extract arguments
    argaddr(0, &data);
    argint(1, &buflen);

    // Validate arguments
    if (data == 0 || buflen <= 0) {
        return -1; // Error
    }

    char buffer[PACKET_SIZE];
    int received_len;

    // Receive packet and get the received length
    virtio_receive_packet(buffer, buflen, &received_len);

    // Validate received length
    if (received_len <= 0) {
        return -1; // Error
    }

    // Copy received data back to user space
    if (copyout(myproc()->pagetable, data, buffer, received_len) < 0) {
        return -1; // Error during copyout
    }

    return received_len; // Return the length of the received packet
}