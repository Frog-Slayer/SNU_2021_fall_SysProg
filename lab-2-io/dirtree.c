//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                    Fall 2021
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <ParkYeongSeo>
/// @studid <2016-13006>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>

#define MAX_DIR 64            ///< maximum number of directories supported

/// @brief output control flags
#define F_TREE      0x1       ///< enable tree view
#define F_SUMMARY   0x2       ///< enable summary
#define F_VERBOSE   0x4       ///< turn on verbose mode

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};


/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
void panic(const char *msg)
{
  if (msg) fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}


/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *getNext(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}

/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sorty by name
  return strcmp(e1->d_name, e2->d_name);
}

/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void processDir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  DIR* od = opendir(dn);
  struct dirent *dir;
  struct dirent *arr = NULL; ///<an array to store all directories
  unsigned int n = 0; ///<# of dir entries
  int aspret; ///<variable to store a retval of asprintf. it does nothing but is set to avoid warning messages.

  //
  //Handling errors that could occur when processing a directory.
  //Print them inplace of the entries of that directory.
  //
  if (od == NULL) {
    char *errp;
    aspret = asprintf(&errp, "%s%s", pstr, flags & F_TREE? "`-" : "  ");
    aspret = aspret; ///does nothing but is set to avoid warning messages.
    printf("%s", errp);
    switch (errno){
        case(EACCES): printf("Permission denied"); break;
        case(EMFILE): printf("The per-process limit on the number of open file descriptors has been reached."); break;
        case(ENFILE): printf("The system-wide limit on the total number of open files has been reached."); break;
        case(ENOENT): printf("Directory does not exist, or name is an empty string."); break;
        case(ENOMEM): printf("Insufficient memory to complete the operation."); break;
    }
    printf("\n");
    free(errp);
    return;
  }
  
  //
  //read every dir entry and store in arr
  //for we don't know the maximum number of a directory, it will be reallocated continuously;
  //
  while ((dir = getNext(od))!= NULL){
    struct dirent *tmp = (struct dirent *)realloc(arr, (n+1)*sizeof(struct dirent));
    
    if (tmp != NULL) {
        arr = tmp;
        arr[n++] = *dir;
    }
  }
  closedir(od);

  qsort(arr, n, sizeof(struct dirent), dirent_compare); ///<the list of dir should be sorted

  //
  //traverse dir tree and print every required information.
  //
  for (int i = 0; i <n ;i++){
      char *pathWithPrefix;
      char *path;
      aspret = asprintf(&pathWithPrefix, "%s%s%s", 
                        pstr, flags & F_TREE? i == n - 1? 
                        "`-" : "|-" : "  ", arr[i].d_name);

      aspret = asprintf(&path, "%s%c%s", dn, '/', arr[i].d_name);

      if (flags & F_VERBOSE){
        if (strlen(pathWithPrefix) > 54) {
         printf("%.51s...  ", pathWithPrefix);
        }
        else printf("%-54s  ", pathWithPrefix);
        
        struct stat statbuf;
        struct passwd *pwu;
        struct group *grg;

        //
        //Handling errors that could occur when retrieving the meta data of a file
        //Print the error message inplace of the file's meta data.
        //
        if (lstat(path, &statbuf) == -1){
          switch(errno){
            case (EACCES): printf("Search permission is denied\n"); break;
            case (EFAULT): printf("Bad address\n"); break;
            case (ELOOP) : printf("Toom many symbolic links encountere\n"); break;
            case (ENAMETOOLONG): printf("pathname is too long\n"); break;
            case (ENOENT) : printf("A component of pathname does not exist\n"); break;
            case (ENOTDIR) : printf("A component of the prefix of pathname is not a directory\n"); break;
            case (ENOMEM): printf("Out of Memory\n"); break;
            case (EOVERFLOW) : printf("pathname refers to a file whose structural member cannot be represented\n"); break;
          }
          continue;
        }

        //
        //print the files' meta data
        //
        printf("%8s:%-8s  ", (pwu = getpwuid(statbuf.st_uid))->pw_name, (grg = getgrgid(statbuf.st_gid))->gr_name);
        printf("%10d  %8d  %c",(int)statbuf.st_size, (int)statbuf.st_blocks,
                              arr[i].d_type == DT_REG? ' ':
                              arr[i].d_type == DT_DIR? 'd':
                              arr[i].d_type == DT_LNK? 'l':
                              arr[i].d_type == DT_CHR? 'c':
                              arr[i].d_type == DT_FIFO? 'f':
                              arr[i].d_type == DT_SOCK? 's':
                              arr[i].d_type == DT_BLK? 'b':
                              '\0');
        stats->size += (int)statbuf.st_size;
        stats->blocks += (int)statbuf.st_blocks;
      }
      else printf("%s", pathWithPrefix);
      printf("\n");

      free(pathWithPrefix);

      switch (arr[i].d_type){
      case (DT_DIR):    stats->dirs++; 
                        char *prefix;
                        aspret= asprintf(&prefix, "%s%s", pstr, flags& F_TREE? i == n-1? "  " :  "| " : "  "); 
                        processDir(path, prefix, stats, flags); ///<access sub-directory recursively to make a tree
                        free(prefix);
                        break;
      case (DT_FIFO):    stats->fifos++;
                         break;
      case(DT_REG):    stats->files++;
                       break;
      case(DT_LNK):    stats->links++;
                       break;
      case(DT_SOCK):    stats->socks++;
                        break;
    }
      free(path);
      aspret = aspret;
  }

  free(arr);
}


/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-t] [-s] [-v] [-h] [path...]\n"
                  "Gather information about directory trees. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -t        print the directory tree (default if no other option specified)\n"
                  " -s        print summary of directories (total number of files, total file size, etc)\n"
                  " -v        print detailed information for each file. Turns on tree view.\n"
                  " -h        print this help\n"
                  " path...   list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DIR);

  exit(EXIT_FAILURE);
}


/// @brief program entry point
int main(int argc, char *argv[])
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary tstat, dstat;
  unsigned int flags = 0;

  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if      (!strcmp(argv[i], "-t")) flags |= F_TREE;
      else if (!strcmp(argv[i], "-s")) flags |= F_SUMMARY;
      else if (!strcmp(argv[i], "-v")) flags |= F_VERBOSE;
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    } else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      } else {
        printf("Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;


  //
  // process each directory
  //
  // TODO

  //set total stat to 0
  memset(&tstat, 0, sizeof(struct summary));

  //enumerate every arguments
  for (int i = 0; i < ndir; i++) {
    //set directory stat to 0
    memset(&dstat, 0, sizeof(struct summary));
    
    //
    //Print Header
    //
    if (flags & F_SUMMARY) {
      if (flags & F_VERBOSE) printf("Name                                                        User:Group           Size    Blocks Type \n");
      else  printf("Name\n");
      printf("----------------------------------------------------------------------------------------------------\n");
    }
  
    printf("%s\n", directories[i]);
    processDir(directories[i], "",  &dstat, flags);

    //
    //Print Footer
    //
    if (flags & F_SUMMARY) {
      printf("----------------------------------------------------------------------------------------------------\n");
      char *summstr;
      int ret = asprintf(&summstr, "%u %s, %u %s, %u %s, %u %s, and %u %s",
                                    dstat.files, dstat.files != 1? "files" : "file",
                                    dstat.dirs, dstat.dirs != 1? "directories" :  "directory",
                                    dstat.links, dstat.links != 1? "links" : "link",
                                    dstat.fifos, dstat.fifos != 1? "pipes" :  "pipe",
                                    dstat.socks, dstat.socks != 1? "sockets" :  "socket");
      ret = ret;
      if (flags & F_VERBOSE) {
          printf("%-68s   %14llu %9llu\n", summstr, dstat.size, dstat.blocks);
      }
      else printf("%s\n", summstr);
      free(summstr);
    }
    printf("\n");

    //
    //Add every member of dstat to total stat
    //
    tstat.blocks += dstat.blocks;
    tstat.dirs += dstat.dirs;
    tstat.fifos += dstat.fifos;
    tstat.links += dstat.links;
    tstat.size += dstat.size;
    tstat.socks += dstat.socks;
    tstat.files += dstat.files; 
  }
  

  //
  // print grand total
  //
  if ((flags & F_SUMMARY) && (ndir > 1)) {
    printf("Analyzed %d directories:\n"
           "  total # of files:        %16d\n"
           "  total # of directories:  %16d\n"
           "  total # of links:        %16d\n"
           "  total # of pipes:        %16d\n"
           "  total # of socksets:     %16d\n",
           ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks);

    if (flags & F_VERBOSE) {
      printf("  total file size:         %16llu\n"
             "  total # of blocks:       %16llu\n",
             tstat.size, tstat.blocks);
    }

  }
  //
  // that's all, folks
  //
  return EXIT_SUCCESS;
}
