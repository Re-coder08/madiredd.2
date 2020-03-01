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

/**
 * Global variables that will be used in different function calls
 */
int total_child = 0;
const char *shared_clock_name =
    "/oss-shm-1231234";  // shared clock memory file name
const char *shared_primality_check_name =
    "/oss-shm-42234122";           // shared primality check name
int clock_fd, primality_check_fd;  // shared memory file descriptor
int *clock;                        // shared clock pointer, allocated later
int *primality_checks;  // shared primality checking pointer, allocated later

/**
 * Shorthand function to allocate all shared memories
 * in case of any failure it will terminate the calling process.
 */
static void allocate_shared_memories() {
  /* allocate shared clock memory */
  if ((clock_fd = shm_open(shared_clock_name, O_RDONLY, S_IRUSR | S_IWUSR)) <
      0) {
    perror("could not open clock memory file");
    exit(EXIT_FAILURE);
  }
  if ((clock = (int *)mmap(NULL, sizeof(int) * 2, PROT_READ, MAP_SHARED,
                           clock_fd, 0)) == MAP_FAILED) {
    perror("could not map clock memory");
    exit(EXIT_FAILURE);
  }

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
  if ((primality_checks =
           (int *)mmap(NULL, sizeof(int) * total_child, PROT_READ | PROT_WRITE,
                       MAP_SHARED, primality_check_fd, 0)) == MAP_FAILED) {
    // deallocate clock memory
    munmap(clock, sizeof(int) * 2);
    shm_unlink(shared_clock_name);

    perror("could not map primality check memory");
    exit(EXIT_FAILURE);
  }
}

/**
 * Funtion to deallocate/free up shared memories
 */
static void free_shared_memories() {
  munmap(clock, sizeof(int) * 2);
  munmap(primality_checks, sizeof(int) * total_child);
}

/**
 * Signal handler function.
 * Clears up memories and exit with success.
 */
static void signal_handler(int sig) {
  fprintf(stderr, "signal caught in child. exiting ...\n");
  fflush(stderr);

  // clean up memories
  free_shared_memories();
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

/**
 * Function to check primality of a number.
 * If the number is prime, primality check is set to the number itself,
 * if its not prime, check equals to negative of the number, if function
 * fails to determine primality within 1ms check equals to -1.
 *
 * arguments-
 *  number - number to check primality
 *  child_id - process id and index in the primality_checks array
 *  clock - shared clock pointer
 *  clock_sz - clock memory block size
 *  primality_chekcs - primality check shared memory
 *  check_sz - primality check shared memory block size
 */
void check_primality(int number, int child_id, int *clock, size_t clock_sz,
                     int *primality_checks, size_t check_sz) {
  int sec, nano;
  int cur_sec, cur_nano;
  int sec_diff, nano_diff;

  // store clock at the beginning of execution
  mlock(clock, clock_sz);
  sec = clock[0];
  nano = clock[1];
  munlock(clock, clock_sz);

  // determine whether the number is prime or not
  // iteratively
  int check = number;
  if (number == 1) {
    check = -1;
  }
  for (int i = 2; i <= number / 2; i++) {
    /* check the clock */
    mlock(clock, clock_sz);
    cur_sec = clock[0];
    cur_nano = clock[1];
    munlock(clock, clock_sz);
    if (cur_nano < nano) {
      cur_nano += NANOSECONDS;
      cur_sec--;
    }
    nano_diff = cur_nano - nano;
    sec_diff = cur_sec - sec;
    nano_diff -= MILLISECONDS;
    if (nano_diff < 0 && sec_diff > 0) {
      nano_diff += NANOSECONDS;
      sec_diff--;
    }
    if (nano_diff > 0) {
      // elapsed time is more than 1 ms
      check = -1;
      break;
    }

    // try to determine if its non-prime number
    if (number % i == 0) {
      check = -(number);
      break;
    }
  }

  // update the value in primality check
  mlock(primality_checks, check_sz);
  //   printf("%d is setting check to %d\n", child_id, check);
  primality_checks[child_id] = check;
  munlock(primality_checks, check_sz);
}

int main(int argc, char *argv[]) {
  /* variables */
  int child_id = -1;
  int number = -1;

  /* parse arguments */
  if (argc < 4) {
    printf("not enough arguments");
    exit(EXIT_FAILURE);
  }
  child_id = atoi(argv[1]);
  number = atoi(argv[2]);
  total_child = atoi(argv[3]);

  /* map shared memories */
  allocate_shared_memories();

  /* init signal handlers */
  init_signal_handlers();

  /* run primality check */
  //   printf("calling primality from id = %d\n", child_id);
  check_primality(number, child_id, clock, sizeof(int) * 2, primality_checks,
                  sizeof(int) * total_child);

  /* test infinite loop */
  // while (1) {
  //   sleep(1);
  // }

  /* unmap the allocated memories */
  free_shared_memories();

  return 0;
}