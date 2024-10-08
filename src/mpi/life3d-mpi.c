#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

#include "world_gen.h"

#define TAG_PREV 1
#define TAG_NEXT 2

typedef struct args {
  int32_t gen_count;
  int32_t n;
  float density;
  int seed;
} Args;

/* My rank */
int32_t me;

/* Number of processes */
int32_t p;

uint32_t peak_gen[N_SPECIES + 1];

uint64_t max_population_local[N_SPECIES + 1];

uint64_t max_population[N_SPECIES + 1];

uint64_t* population_local;

uint64_t* population_local_old;

uint64_t* population_local_tmp;

uint64_t population[N_SPECIES + 1];

char ***old, ***new, ***tmp;

MPI_Comm comm;

void help() { fprintf(stderr, "\nUsage: life3d gen_count N density seed\n"); }

Args parse_args(int argc, char *argv[]) {
  Args args = {};

  if (argc != 5) {
    printf("Wrong number of arguments (5 required, %d provided)\n", argc);
    help();
    exit(1);
  }

  args.gen_count = atoi(argv[1]);

  if (args.gen_count == 0) {
    printf("Invalid generation count provided: %s\n", argv[1]);
    help();
    exit(1);
  }

  args.n = atoi(argv[2]);

  if (args.n < 3) {
    printf("Invalid N provided: %s\n", argv[2]);
    help();
    exit(1);
  }

  args.density = atof(argv[3]);
  if (args.density < 0 || args.density > 1) {
    printf("Invalid density provided: %s\n", argv[2]);
    help();
    exit(1);
  }

  args.seed = atoi(argv[4]);

  return args;
}

int debug(uint32_t n, char ***grid, uint32_t height) {
  for (uint64_t x = 0; x < height+2; x++) {
    fprintf(stderr, "Layer %ld:\n", x);
    for (uint64_t y = 0; y < n; y++) {
      for (uint64_t z = 0; z < n; z++) {
        char value = grid[x][y][z];
        if (value > 0) {
          fprintf(stderr, "%d ", value);
        } else {
          fprintf(stderr, "  ");
        }
      }
      fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
  }

  return EXIT_SUCCESS;
}

/**
 * Computes new inhabitant for cell at position (x, y, z) of grid
 */
char next_inhabitant(int32_t x, int32_t y, int32_t z, int32_t n,
                            char ***grid) {
  // Compute stats for neighbours
  char counts[N_SPECIES + 1];
  char neighbour, current;
  memset(counts, 0, (N_SPECIES + 1) * sizeof(char));

  int32_t left = x - 1;
  int32_t right = x + 1;

  int32_t front = (y - 1 + n) % n;
  int32_t back = (y + 1 + n) % n;

  int32_t up = (z - 1 + n) % n;
  int32_t down = (z + 1 + n) % n;

  counts[grid[left][front][up]]++;
  counts[grid[left][front][z]]++;
  counts[grid[left][front][down]]++;
  counts[grid[left][y][up]]++;
  counts[grid[left][y][z]]++;
  counts[grid[left][y][down]]++;
  counts[grid[left][back][up]]++;
  counts[grid[left][back][z]]++;
  counts[grid[left][back][down]]++;

  counts[grid[x][front][up]]++;
  counts[grid[x][front][z]]++;
  counts[grid[x][front][down]]++;
  counts[grid[x][y][up]]++;
  current = grid[x][y][z];
  counts[grid[x][y][down]]++;
  counts[grid[x][back][up]]++;
  counts[grid[x][back][z]]++;
  counts[grid[x][back][down]]++;

  counts[grid[right][front][up]]++;
  counts[grid[right][front][z]]++;
  counts[grid[right][front][down]]++;
  counts[grid[right][y][up]]++;
  counts[grid[right][y][z]]++;
  counts[grid[right][y][down]]++;
  counts[grid[right][back][up]]++;
  counts[grid[right][back][z]]++;
  counts[grid[right][back][down]]++;

  char most_common = 0;
  char most_common_count = 0;
  char live_count = 0;

  for (char specie = 1; specie <= N_SPECIES; specie++) {
    live_count += counts[specie];

    if (counts[specie] > most_common_count) {
      most_common_count = counts[specie];
      most_common = specie;
    }
  }


  if (current != 0) { // if cell is alive
    return (5 <= live_count && live_count <= 13) ? current : 0;
  } else { // if cell is dead
    return (7 <= live_count && live_count <= 10) ? most_common : 0;
  }
}

void prepare(int n, int start, int end) {
  new = new_grid(n, end + 1 - start);
  population_local = (uint64_t*) malloc(sizeof(uint64_t) * (N_SPECIES + 1));
  population_local_old = (uint64_t*) malloc(sizeof(uint64_t) * (N_SPECIES + 1));
  memset(peak_gen, 0, sizeof(uint32_t) * (N_SPECIES + 1));
  memset(max_population, 0, sizeof(uint64_t) * (N_SPECIES + 1));
  memset(max_population_local, 0, sizeof(uint64_t) * (N_SPECIES + 1));
  memset(population, 0, sizeof(uint64_t) * (N_SPECIES + 1));
  memset(population_local, 0, sizeof(uint64_t) * (N_SPECIES + 1));
}

void finish() {
  if (me == 0) {
    for (uint32_t specie = 1; specie <= N_SPECIES; specie++) {
      printf("%d %ld %d\n", specie, max_population[specie], peak_gen[specie]);
    }
  }
}

/* end is inclusive */
void simulation(int32_t n, int32_t max_gen, char ***grid, uint32_t height) {
  char new_val;
  old = grid;

  MPI_Request requests[4];
  MPI_Status status[4];

  int neighbour_prev = (p < n) ? (me - 1 + p) % p : (me - 1 + n) % n;
  int neighbour_next = (p < n) ? (me + 1) % p : (me + 1) % n;

  // Compute initial stats
  #pragma omp parallel for collapse(3) reduction(+:max_population_local[:N_SPECIES+1])
  for (int32_t x = 1; x <= height; x++) {
    for (int32_t y = 0; y < n; y++) {
      for (int32_t z = 0; z < n; z++) {
        max_population_local[old[x][y][z]]++;
      }
    }
  }

  MPI_Request request_reduce;
  MPI_Status status_reduce;
  MPI_Ireduce(max_population_local, max_population, N_SPECIES+1, MPI_UNSIGNED_LONG, MPI_SUM, 0, comm, &request_reduce);

  #pragma omp parallel
  {
    for (int32_t gen = 0; gen < max_gen; gen++) {
      #pragma omp for collapse(3) reduction(+:population_local[:N_SPECIES+1]) private(new_val)
      for (int32_t x = 1; x <= height; x++) {
        for (int32_t y = 0; y < n; y++) {
          for (int32_t z = 0; z < n; z++) {
            new_val = next_inhabitant(x, y, z, n, old);
            new[x][y][z] = new_val;
            population_local[new_val]++;
          }
        }
      }
      #pragma omp single
      {
        MPI_Isend(&(new[1][0][0]), n*n, MPI_CHAR, neighbour_prev, TAG_PREV, comm, requests);
        MPI_Irecv(&(new[height+1][0][0]), n*n, MPI_CHAR, neighbour_next, TAG_PREV, comm, requests+2);

        MPI_Isend(&(new[height][0][0]), n*n, MPI_CHAR, neighbour_next, TAG_NEXT, comm, requests+1);
        MPI_Irecv(&(new[0][0][0]), n*n, MPI_CHAR, neighbour_prev, TAG_NEXT, comm, requests+3);
        MPI_Wait(&request_reduce, &status_reduce);

        if (me == 0) {
          for (int i = 0; i <= N_SPECIES; i++) {
            if (max_population[i] < population[i]) {
              max_population[i] = population[i];
              peak_gen[i] = gen;
            }
          }
        }

        memset(population_local_old, 0, sizeof(uint64_t) * (N_SPECIES + 1));

        MPI_Waitall(4, requests, status);

        tmp = old;
        old = new;
        new = tmp;

        population_local_tmp = population_local_old;
        population_local_old = population_local;
        population_local = population_local_tmp;

        // No need for barrier, because all must say something for reduce
        MPI_Ireduce(population_local_old, population, N_SPECIES+1, MPI_UNSIGNED_LONG, MPI_SUM, 0, comm, &request_reduce);
      }
    }
  }
  MPI_Wait(&request_reduce, &status_reduce);

  if (me == 0) {
    for (int i = 0; i <= N_SPECIES; i++) {
      if (max_population[i] < population[i]) {
        max_population[i] = population[i];
        peak_gen[i] = max_gen;
      }
    }
  }
}

int main(int argc, char *argv[]) {
  double exec_time;
  Args args = parse_args(argc, argv);

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &p);
  MPI_Comm_rank(MPI_COMM_WORLD, &me);

  // Starting x value
  uint32_t start = (p <= args.n) ? (me * args.n) / p : me;
  uint32_t end = (p <= args.n) ? (((me+1) * args.n) / p) - 1 : me;

  char ***grid = gen_initial_grid(args.n, args.density, args.seed, start, end);
  prepare(args.n, start, end);

  if (p > args.n) { 
    MPI_Group all, group;
    MPI_Comm_group(MPI_COMM_WORLD, &all);

    int* ranks = (int*)malloc(args.n * sizeof(int));
    for (int i = 0; i < args.n; i++) {
      ranks[i] = i;
    }

    MPI_Group_incl(all, args.n, ranks, &group);
    MPI_Comm_create(MPI_COMM_WORLD, group, &comm);
  } else {
    comm = MPI_COMM_WORLD;
  }

  MPI_Barrier(MPI_COMM_WORLD);
    
  exec_time = -MPI_Wtime();

  if (me < args.n) {
    simulation(args.n, args.gen_count, grid, end + 1 - start);
  } 

  exec_time += MPI_Wtime();
  MPI_Barrier(MPI_COMM_WORLD);

  finish();

  MPI_Finalize();

  if (!me) {
    fprintf(stderr, "Took: %.1fs\n", exec_time);
  }

  return EXIT_SUCCESS;
}