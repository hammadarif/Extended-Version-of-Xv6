#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device 
#define T_SYMLINK 4   //for symbolic aka soft link
//#define T_DEV     4   // NetDevice

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};
