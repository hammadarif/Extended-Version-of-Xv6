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
/*
uint64
sys_netdev_open(void) {
    uint64 fd;
    int mode;

    // Extract arguments
    argaddr(0, &fd);
    argint(1, &mode);


   return netdev_open(fd, mode);
}


// System call to receive a network packet
uint64
sys_netdev_close(void) {
    int fd ;

    // Extract arguments
    argint(0, &fd);
    
    netdev_close(fd);
    return 0; // Return the length of the received packet
}
*/
uint64
sys_netdev_read(void) {
    int fd, n;
    uint64 dst;

    // Extract arguments
    argint(0, &fd);
    argaddr(1, &dst);
    argint(2, &n);

    // Call kernel function
    return netdev_read(fd, dst, n);
}

uint64
sys_netdev_write(void) {
    int fd, n;
    uint64 src;

    // Extract arguments
    argint(0, &fd);
    argaddr(1, &src);
    argint(2, &n);

    // Call kernel function
    return netdev_write(fd, src, n);
}

uint64 sys_symlink(void) {
    char target[MAXPATH], link[MAXPATH];
    begin_op();
    if (argstr(0, target, MAXPATH) < 0 || argstr(1, link, MAXPATH) < 0)
    {
        end_op();
        return -1;
    }
    uint64 ret_va =  create_symlink(target, link);
    end_op();

  return ret_va;
}
