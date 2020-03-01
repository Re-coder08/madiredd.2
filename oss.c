#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* pid_t fork(); */
/* int wait(int *); */
/* int ftruncate(int, size_t); */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
/* int shm_open(const char* name, int oflag, mode_t mode); */
/* int shm_unlink(const char *name); */
/* void *mmap(void *, size_t, int prot, int flags, int fd, off_t); */
/* int munmap(void *, size_t); */
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
/* int sigaction(int, const struct sigaction *, struct sigaction*); */
#include <signal.h>

#define NANOSECONDS 1000000000
#define MILLISECONDS 1000000
#define CLOCK_TICK 10000000
#define MAX_CHILD_PROCESS 20

/*
  global vairables to share data easily among different function calls
  including signal handler
*/
int total_child = 4;
int max_child = 2;
int start_value = 3;
int increment = 5;
char *log_filepath = "output.log";
const char *shared_clock_name =
    "/oss-shm-1231234";  // shared clock memory file name
const char *shared_primality_check_name =
    "/oss-shm-42234122";  // shared primality check name
const char *shared_child_pids_name = "/oss-shm-239373";
int clock_fd, primality_check_fd,
    child_pids_fd;      // shared memory file descriptor
int *clock;             // shared clock pointer, allocated later
int *primality_checks;  // shared primality checking pointer, allocated later
int *child_pids;        // shared memory to store forked child pids
FILE *output_fd = NULL;

/**
 * Prints a help menu describing the different options
 * and their default value.
 */
void print_help(int argc, char *argv[]) {
  printf("usage: %s OPTIONS\n", argv[0]);
  printf("\n");
  printf("OPTIONS - \n");
  printf("\n");
  printf("\t-n x maximum total of child processes to create (default 4)\n");
  printf(
      "\t-s x number of children should be allowed to exist at the same time "
      "(default 2)\n");
  printf("\t-b x start of the sequence of numbers (default 3)\n");
  printf("\t-i x increment between numbers (default 5)\n");
  printf("\t-o l log filepath (default \"output.log\"\n");
  printf("\t-h print this help menu and exit\n");
  printf("\n");
}

/**
 * Shorthand function to allocate all shared memories
 * in case of any failure it will terminate the calling process.
 */
static void allocate_shared_memories() {
  /* allocate shared clock memory */
  if ((clock_fd = shm_open(shared_clock_name, O_RDWR | O_CREAT,
                           S_IRUSR | S_IWUSR)) < 0) {
    perror("could not open clock memory file");
    exit(EXIT_FAILURE);
  }
  if (ftruncate(clock_fd, sizeof(int) * 2) < 0) {
    perror("could not allocate clock memory");
    exit(EXIT_FAILURE);
  }
  if ((clock = (int *)mmap(NULL, sizeof(int) * 2, PROT_READ | PROT_WRITE,
                           MAP_SHARED, clock_fd, 0)) == MAP_FAILED) {
    perror("could not map clock memory");
    exit(EXIT_FAILURE);
  }
  // initialize
  clock[0] = 0;  // seconds
  clock[1] = 0;  // nanoseconds

  /* allocate shared primality check memory */
  if ((primality_check_fd = shm_open(shared_primality_check_name,
                                     O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) <
      0) {
    // deallocate clock memory
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);

    perror("could not open shared primality check file");
    exit(EXIT_FAILURE);
  }
  if (ftruncate(primality_check_fd, sizeof(int) * total_child) < 0) {
    // deallocate clock memory
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);

    perror("could not allocate primality check memory");
    exit(EXIT_FAILURE);
  }
  if ((primality_checks =
           (int *)mmap(NULL, sizeof(int) * total_child, PROT_READ | PROT_WRITE,
                       MAP_SHARED, primality_check_fd, 0)) == MAP_FAILED) {
    // deallocate clock memory
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);

    perror("could not map primality check memory");
    exit(EXIT_FAILURE);
  }
  // initialize
  for (int i = 0; i < total_child; i++) {
    primality_checks[i] = 0;
  }
  /* allocate child pid array */
  if ((child_pids_fd = shm_open(shared_child_pids_name, O_RDWR | O_CREAT,
                                S_IRUSR | S_IWUSR)) < 0) {
    // deallocate previously loaded memories
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);
    munmap(primality_checks, sizeof(int) * 2);
    shm_unlink(shared_primality_check_name);

    perror("could not open shared child pids file");
    exit(EXIT_FAILURE);
  }
  if (ftruncate(child_pids_fd, sizeof(int) * total_child) < 0) {
    // deallocate previously loaded memories
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);
    munmap(primality_checks, sizeof(int) * 2);
    shm_unlink(shared_primality_check_name);

    perror("could not allocate child pid array");
    exit(EXIT_FAILURE);
  }
  if ((child_pids =
           (int *)mmap(NULL, sizeof(int) * total_child, PROT_READ | PROT_WRITE,
                       MAP_SHARED, child_pids_fd, 0)) == MAP_FAILED) {
    // deallocate previously loaded memories
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);
    munmap(primality_checks, sizeof(int) * 2);
    shm_unlink(shared_primality_check_name);

    perror("could not map child pid array");
    exit(EXIT_FAILURE);
  }
  // initialize
  for (int i = 0; i < total_child; i++) {
    child_pids[i] = 0;
  }
}

/**
 * Funtion to deallocate/free up shared memories
 */
static void free_shared_memories() {
  munmap(clock, sizeof(int) * 2);
  shm_unlink(shared_clock_name);
  munmap(primality_checks, sizeof(int) * total_child);
  shm_unlink(shared_primality_check_name);
  munmap(child_pids, sizeof(int) * total_child);
  shm_unlink(shared_child_pids_name);
}

/**
 * Signal handler function.
 * Clears up memories and exit with success.
 */
static void signal_handler(int sig) {
  fprintf(stderr, "signal caught. exiting ...\n");
  fflush(stderr);

  // kill all running child process
  mlock(child_pids, sizeof(int) * total_child);
  for (int i = 0; i < total_child; i++) {
    if (child_pids[i] > 0) {
      fprintf(stderr, "sending kill signal to %d\n", child_pids[i]);
      fflush(stderr);
      kill((pid_t)child_pids[i], SIGKILL);
    }
  }
  munlock(child_pids, sizeof(int) * total_child);

  sleep(1);
  // clean up memories
  free_shared_memories();
  if (output_fd != NULL) {
    fflush(output_fd);
    fclose(output_fd);
  }
  exit(EXIT_SUCCESS);
}

/**
 * Initializes signal handlers
 */
void init_signal_handlers() {
  struct sigaction sig_action;
  sig_action.sa_handler = signal_handler;
  sig_action.sa_flags = 0;

  sigaction(SIGINT, &sig_action, NULL);
  sigaction(SIGTERM, &sig_action, NULL);
  sigaction(SIGKILL, &sig_action, NULL);
}

int main(int argc, char *argv[]) {
  /* variables */
  int running_children = 0;
  int cur_number = start_value;
  int child_status;                                 // wait call argument
  char **child_args;                                // child exec arguments
  int *primes, *non_primes, *failures;              // used for result logging
  int prime_count, non_prime_count, failure_count;  // used for result logging
  int cur_sec, cur_nano;

  /* parse arguments passed to the program */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0) {
      // print help and exit
      print_help(argc, argv);
      exit(EXIT_SUCCESS);
    } else if (strcmp(argv[i], "-n") == 0) {
      total_child = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-s") == 0) {
      max_child = atoi(argv[++i]);
      // limit maximum number of child process at a time
      max_child =
          (max_child > MAX_CHILD_PROCESS) ? MAX_CHILD_PROCESS : max_child;
    } else if (strcmp(argv[i], "-b") == 0) {
      start_value = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-i") == 0) {
      increment = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-o") == 0) {
      log_filepath = argv[++i];
    }
  }

  /* print runtime configuration */
  printf("Running oss under the following configuration -\n");
  printf("total children = %d\n", total_child);
  printf("maximum simultaneous children = %d\n", max_child);
  printf("start value = %d\n", start_value);
  printf("increment between numbers = %d\n", increment);
  printf("output file = %s\n", log_filepath);
  printf("\n");

  /* allocate shared memories */
  allocate_shared_memories();

  /* allocate child argument strings */
  child_args = (char **)malloc(sizeof(char *) * 5);
  for (int i = 0; i < 4; i++) {
    child_args[i] = (char *)malloc(sizeof(char) * 10);
    child_args[i][0] = '\0';
  }
  sprintf(child_args[0], "./user");
  child_args[4] = NULL;
  printf("successfully allocated shared memories\n");

  /* open log file */
  if ((output_fd = fopen(log_filepath, "a")) == NULL) {
    perror("could not open output file");

    // free up allocated memories
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);
    munmap(primality_checks, sizeof(int) * total_child);
    shm_unlink(shared_primality_check_name);
    exit(EXIT_FAILURE);
  }

  /* signal handler registration */
  init_signal_handlers();

  /* main loop */
  cur_number = start_value;
  sprintf(child_args[3], "%d", total_child);
  for (int child_id = 0; child_id < total_child; child_id++) {
    /* increment clock */
    mlock(clock, sizeof(int) * 2);
    clock[1] += CLOCK_TICK;
    if (clock[1] >= NANOSECONDS) {
      clock[0]++;
      clock[1] -= NANOSECONDS;
    }
    cur_sec = clock[0];
    cur_nano = clock[1];
    munlock(clock, sizeof(int) * 2);

    /* fork a new child process */
    if (fork() == 0) {
      /* check primality by executing user (child) program and exit */
      mlock(child_pids, sizeof(int) * total_child);
      child_pids[child_id] = (int)getpid();  // store pid for later use
      munlock(child_pids, sizeof(int) * total_child);

      // set command line arguments for child
      sprintf(child_args[1], "%d", child_id);
      sprintf(child_args[2], "%d", cur_number);

      // exec child program name user
      execv("./user", child_args);
      exit(EXIT_SUCCESS);
    } else {
      /* this is parent process code */
      fprintf(output_fd,
              "forked new child. child id = %d. number = %d. launch time = %d "
              "sec %d nanosec.\n",
              child_id, cur_number, cur_sec, cur_nano);
      running_children++;
      cur_number += increment;
      if (running_children >= max_child) {
        // wait for a child to exit before we can start or fork another one
        pid_t child_pid = wait(&child_status);
        // search child id using the pid of it
        int id = 0;
        mlock(child_pids, sizeof(int) * total_child);
        for (; id < total_child; id++) {
          if (child_pids[id] == (int)child_pid) {
            break;
          }
        }
        munlock(child_pids, sizeof(int) * total_child);
        fprintf(output_fd,
                "child with id = %d is done working. timestamp = %d sec %d "
                "nanosec.\n",
                id, cur_sec, cur_nano);
        running_children--;
      }
    }
  }

  /* wait for all children to finish up */
  pid_t child_pid;
  while ((child_pid = wait(&child_status)) > 0) {
    /* increment clock */
    mlock(clock, sizeof(int) * 2);
    clock[1] += CLOCK_TICK;
    if (clock[1] >= NANOSECONDS) {
      clock[0]++;
      clock[1] -= NANOSECONDS;
    }
    cur_sec = clock[0];
    cur_nano = clock[1];
    munlock(clock, sizeof(int) * 2);

    // search child id using the pid of it
    int id = 0;
    mlock(child_pids, sizeof(int) * total_child);
    for (; id < total_child; id++) {
      if (child_pids[id] == (int)child_pid) {
        break;
      }
    }
    munlock(child_pids, sizeof(int) * total_child);

    /* log child finish event */
    fprintf(output_fd,
            "child with id = %d is done working. timestamp = %d sec %d "
            "nanosec.\n",
            id, cur_sec, cur_nano);
  }

  /* collect and print result in steps */
  prime_count = 0;
  non_prime_count = 0;
  failure_count = 0;
  primes = (int *)malloc(sizeof(int) * total_child);
  non_primes = (int *)malloc(sizeof(int) * total_child);
  failures = (int *)malloc(sizeof(int) * total_child);
  // initialize
  for (int i = 0; i < total_child; i++) {
    primes[i] = 0;
    non_primes[i] = 0;
    failures[i] = 0;
  }
  // collect result
  cur_number = start_value;
  for (int i = 0; i < total_child; i++) {
    if (primality_checks[i] == cur_number) {
      // it is prime number
      primes[prime_count++] = cur_number;
    } else if (primality_checks[i] == -(cur_number)) {
      // non prime number
      non_primes[non_prime_count++] = cur_number;
    } else if (primality_checks[i] == -1) {
      failures[failure_count++] = cur_number;
    }
    cur_number += increment;
  }
  // print or log result
  if (prime_count > 0) {
    printf("total %d primes found. they are -\n", prime_count);
    fprintf(output_fd, "total %d primes found. they are -\n", prime_count);
    for (int i = 0; i < prime_count; i++) {
      printf("%d\n", primes[i]);
      fprintf(output_fd, "%d\n", primes[i]);
    }
  }
  if (non_prime_count > 0) {
    printf("total %d non primes found. they are -\n", non_prime_count);
    fprintf(output_fd, "total %d non primes found. they are -\n",
            non_prime_count);
    for (int i = 0; i < non_prime_count; i++) {
      printf("-%d\n", non_primes[i]);
      fprintf(output_fd, "-%d\n", non_primes[i]);
    }
  }
  if (failure_count > 0) {
    printf(
        "total %d numbers primality could not be determined due to lack of "
        "time. they are "
        "-\n",
        failure_count);
    fprintf(output_fd,
            "total %d numbers primality could not be determined due to lack of "
            "time. they "
            "are -\n",
            failure_count);
    for (int i = 0; i < failure_count; i++) {
      printf("%d\n", failures[i]);
      fprintf(output_fd, "%d\n", failures[i]);
    }
  }
  // free up memories
  free(primes);
  free(non_primes);
  free(failures);

  /* close output file */
  fclose(output_fd);

  /* deallocate/free up shared memory
    unmap the allocated memories and then unlink shared memory file
  */
  printf("deallocating shared memories\n");
  free_shared_memories();
  for (int i = 0; i < 5; i++) {
    free(child_args[i]);
  }
  free(child_args);
  return 0;
}
