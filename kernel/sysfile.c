//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "socket.h"
#include "memlayout.h"


extern unsigned long r_mtime(void);
// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
fdalloc_for_proc(struct file *f, struct proc *p)
{
  int fd;

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}
static struct inode*
create(char *path, short type, short major, short minor)
{
    struct inode *ip, *dp;
    char name[DIRSIZ];
    //printf("[CREATE] Start\n");

    // Find the parent directory of the file being created
    if ((dp = nameiparent(path, name)) == 0) {
        //printf("[CREATE] Failed to find parent directory\n");
        return 0;
    }

    ilock(dp);
    //printf("[CREATE] Before Directory Lookup\n");

    // Look up the file in the directory, but do not resolve symlinks
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        ilock(ip);
        //printf("[CREATE] File already exists\n");

        // Allow re-use for regular files or symlinks
        if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE || ip->type == T_SYMLINK)) {
            iunlockput(dp);
            return ip;  // Return the existing inode
        }

        // File exists but is incompatible
        iunlockput(ip);
        iunlockput(dp);
        return 0;
    }

    // Allocate a new inode
    if ((ip = ialloc(dp->dev, type)) == 0) {
        //printf("[CREATE] Failed to allocate inode\n");
        iunlockput(dp);
        return 0;
    }

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    // Handle directories: add "." and ".." entries
    if (type == T_DIR) {
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) {
            //printf("[CREATE] Failed to create . or .. entries\n");
            goto fail;
        }
    }

    // Link the new file to its parent directory
    if (dirlink(dp, name, ip->inum) < 0) {
        //printf("[CREATE] Failed to link new file to parent directory\n");
        goto fail;
    }

    // Update parent directory for directories
    if (type == T_DIR) {
        dp->nlink++;
        iupdate(dp);
    }
    //printf("[CREATE] Before lock release %p \n", dp);
    iunlockput(dp);
    //printf("[CREATE] File created successfully\n");
    return ip;

fail:
    ip->nlink = 0;
    iupdate(ip);
    iunlockput(ip);
    iunlockput(dp);
    return 0;
}
/*
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];
  printf("[CREATE] Start\n");
  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);
  printf("[CREATE] Before Directory Lookup\n");
  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE || ip->type == T_SYMLINK))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}
*/
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}
uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
uint64
create_symlink(const char *target, const char *link) {
    struct inode *ip;
    //printf("[CREATESYMLINK] Start: Target=%s, Link=%s\n", target, link);

    // Create the inode for the symlink
    ip = create((char *)link, T_SYMLINK, 0, 0);
    //printf("[CREATESYMLINK] Returned value of create: %p\n", ip);
    if (ip == 0) {
        //printf("[CREATESYMLINK] Failed to create inode for link: %s\n", link);
        return -1;
    }
    //printf("[CREATESYMLINK] Before Lock %p \n",ip);
    //ilock(ip);
    //printf("[CREATESYMLINK] After Lock\n");
    //printf("[CREATESYMLINK] Link inode locked successfully\n");

    // Check if the target path fits in the inode
    if (strlen(target) + 1 > MAXFILE) {  // Assuming MAXFILE is the max inode size
        //printf("[CREATESYMLINK] Target path too long: %s\n", target);
        //iunlockput(ip);
        return -1;
    }

    // Write the target path into the symlink inode
    if (writei(ip, 0, (uint64)target, 0, strlen(target) + 1) < strlen(target) + 1) {
        //printf("[CREATESYMLINK] Failed to write target path to inode\n");
        ip->nlink = 0;  // Mark inode as unused
        iupdate(ip);
        //iunlockput(ip);
        return -1;
    }

    // Update and release the inode
    iupdate(ip);
    iunlockput(ip);

    //printf("[CREATESYMLINK] Symlink created successfully: Target=%s, Link=%s\n", target, link);
    return 0;
}

/*
uint64
create_symlink(const char *target, const char *link) {
    struct inode *ip;
    printf("[CREATESYMLINK] Start: Target=%s, Link=%s\n", target, link);

    // Create the inode for the symlink
    ip = create((char *)link, T_SYMLINK, 0, 0);
    if (ip == 0) {
        printf("[CREATEFUNC] Failed to create inode for link: %s\n", link);
        return -1;
    }

    ilock(ip);
    printf("[CREATEFUNC] Link inode created successfully\n");

    // Write the target path into the symlink inode
    if (writei(ip, 0, (uint64)(target), 0, strlen(target) + 1) < 0) {
        printf("[CREATEFUNC] Failed to write target to link inode\n");
        iunlockput(ip);
        return -1;
    }

    // Update and release the inode
    iupdate(ip);
    iunlockput(ip);

    printf("[CREATEFUNC] Symlink created successfully: Target=%s, Link=%s\n", target, link);
    return 0;
}*/
uint64
sys_socket(void)
{
  int domain, type, protocol;
  if(argint(0, &domain) < 0 || argint(1, &type) < 0 || argint(2, &protocol) < 0)
    return -1;
  return sockalloc(domain, type, protocol, 0, 0);
}

uint64
sys_connect(void)
{
  int sockfd;
  uint64 user_addr;
  struct sockaddr addr;
  int addrlen;
  
  if (argint(0, &sockfd) < 0 || argaddr(1, &user_addr) < 0 || argint(2, &addrlen) < 0)
    return -1;

  // copy struct sockaddr from user space to kernel space
  if (copyin(myproc()->pagetable, (char*)&addr, user_addr, sizeof(addr)) < 0)
    return -1;

  return sockconnect(sockfd, &addr, addrlen);
}

uint64
sys_bind(void)
{
  int sockfd;
  uint64 user_addr;
  struct sockaddr addr;
  int addrlen;
  if (argint(0, &sockfd) < 0 || argaddr(1, &user_addr) < 0 || argint(2, &addrlen) < 0)
    return -1;

  // copy struct sockaddr from user space to kernel space
  if (copyin(myproc()->pagetable, (char*)&addr, user_addr, sizeof(addr)) < 0)
    return -1;
  
  return sockbind(sockfd,  &addr, addrlen);
}

uint64
sys_listen(void)
{
  int sockfd, backlog;
  if(argint(0, &sockfd) < 0 || argint(1, &backlog) < 0)
    return -1;
  return socklisten(sockfd, backlog);
}

uint64
sys_accept(void)
{
  int sockfd;
  uint64 user_addr;
  uint64 user_addrlen;
  
  if(argint(0, &sockfd) < 0 || argaddr(1, &user_addr) < 0 || argaddr(2, &user_addrlen) < 0)
    return -1;
  
  struct sockaddr addr;
  int addrlen;
  int new_sockfd;

  if ((new_sockfd = sockaccept(sockfd, &addr, &addrlen)) < 0)
    return -1;
  
  pagetable_t pagetable = myproc()->pagetable;
  if (copyout(pagetable, user_addr, (char*)&addr, sizeof(addr)) < 0 || 
      copyout(pagetable, user_addrlen, (char*)&addrlen, sizeof(addrlen)) < 0)
    return -1;
  
  return new_sockfd;
}
/*
uint64
sys_gethostbyname(void)
{
  char name[MAX_DOMAIN_NAME];
  uint64 addr;

  if(argstr(0, name, MAX_DOMAIN_NAME) < 0 || argaddr(1, &addr) < 0)
    return -1;

  struct sockaddr res;

  // copyin sockaddr from user space
  if(copyin(myproc()->pagetable, (char*)&res, addr, sizeof(res)) < 0)
    return -1;

  int rc = sockgethostbyname(name, &res);

  // copyout sockaddr to user space
  if (rc == 0)
    if (copyout(myproc()->pagetable, addr, (char*)&res, sizeof(res)) < 0)
      return -1;

  return rc;
}
*/
uint64
sys_inetaddress(void)
{
  char char_addr[MAX_ADDRESS_LENGTH];
  uint64 addr;

  if(argstr(0, char_addr, MAX_ADDRESS_LENGTH) < 0 || argaddr(1, &addr) < 0)
    return -1;

  struct sockaddr res;

  // copyin sockaddr from user space
  if(copyin(myproc()->pagetable, (char*)&res, addr, sizeof(res)) < 0)
    return -1;

  int rc = sockinetaddress(char_addr, &res);

  // copyout sockaddr to user space
  if (rc == 0)
    if (copyout(myproc()->pagetable, addr, (char*)&res, sizeof(res)) < 0)
      return -1;


  return rc;
}
/*
uint32
sys_now(void)
{
  
  return r_mtime() / 10000;
}
*/
