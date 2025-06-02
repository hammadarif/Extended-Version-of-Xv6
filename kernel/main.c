#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "lwip/timeouts.h"

volatile static int started = 0;

//extern void sys_check_timeouts(void);

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n"); 
    //pci_init();
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    init_netdev();
    //virtio_net_init(); // Initialize VirtIO-Net driver
    lwip_init_network(); //Lwip 
    //virtio_net_enable_interrupt(); // Enable VirtIO-Net interrupts
    sockinit();     // socket table
    userinit();      // first user process
    __sync_synchronize();
    //pci_init();
    started = 1;
    //run_virtio_net_tests();
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts

  }

  scheduler();        
}
