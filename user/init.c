// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/virtio.h"

#define S_IFCHR 0020000  // Character device file

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

//Creating /dev
/*
  if (mkdir("/dev") < 0) {
      printf("init: /dev directory already exists or failed to create\n");
  } else {
      printf("init: /dev directory created successfully\n");
  }
  */




  if(open("console", O_RDWR) < 0){
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

/*
  if (mknod("/dev/net", S_IFCHR, DEV_NET) < 0) {
        printf("init: failed to create device node\n");
    } else {
        printf("init: device node created successfully\n");
    }
*/
  // Create network device node
  /*for (int i = 0; i < MAX_NET_DEVICES; i++) {
        char dev_name[16];
        char num[4];

        // Build the name "/dev/netX"
        strcpy(dev_name, "/dev/net");
        num[0] = '0' + (i % 10);  // Convert i to a single digit
        num[1] = '\0';
        strcat(dev_name, num);

        if (mknod(dev_name, S_IFCHR, DEV_NET + i) < 0) {
            printf("init: failed to create %s device node\n", dev_name);
        } else {
            printf("init: %s device node created successfully\n", dev_name);
        }
    }
*/

  
  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
