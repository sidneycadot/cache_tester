
////////////////////
// cache_tester.c //
////////////////////

#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

struct worker_info {
    pthread_t thread;
    pthread_barrier_t * barrier;
    unsigned worker_id;
    unsigned num_workers;
    uint8_t * mem;
    unsigned memsize;
    unsigned flush_size;
    unsigned num_repeats;
    unsigned check_interval;
};

void * worker(void *arg)
{
    struct worker_info * wi = arg;
    int result;
    volatile unsigned memvol;
 
    //printf("[%u] worker reporting for duty.\n", wi->worker_id);

    char * flushmem = malloc(wi->flush_size);
    assert(flushmem != NULL);

    // Initialize our thread memory
    for (unsigned i = 0; i < wi->memsize; ++i)
    {
        uint8_t value;

        value = rand() % 256;
        value = (value - value % wi->num_workers) + wi->worker_id;

        wi->mem[i] = value;
    }

    // We have just woken up ...

    for (unsigned outer_rep = 0; outer_rep < wi->num_repeats; ++outer_rep)
    {
        // Manipulate our local memory.

        unsigned rep = rand() % 100;

        for (unsigned r = 0; r < rep; ++r)
        {
            unsigned sz = 1 + rand() % 100;
            uint8_t * a = &wi->mem[rand() % (wi->memsize - sz)];
            uint8_t * b = &wi->mem[rand() % (wi->memsize - sz)];
            uint8_t tmp[sz];
            memcpy(tmp, a, sz);
            memcpy(a, b, sz);
            memcpy(b, tmp, sz);
        }

        // Prepare wait.
        unsigned wait = rand() % 16;

        // Synchronize with other threads.
        result = pthread_barrier_wait(wi->barrier);
        assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);

        // Wait a bit of time.
        while (wait--)
        {
            memvol = wait;
        }

        // Flush our cache. This overlaps quite closely with the cache flushes of the other worker threads,
        // with a bit of randomness added by the preceding 'wait'.
        // Hopefully, this will sometimes recreate a situation where the cache handling goes wrong.
        memset(flushmem, rand() % 256, wi->flush_size);

        // Synchronize with other threads for reporting.
        result = pthread_barrier_wait(wi->barrier);
        assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);

        if (outer_rep % wi->check_interval == 0)
        {
            //printf("[%u] checking (outer_rep=%u) ...\n", wi->worker_id, outer_rep);

            // Check all our bytes
            unsigned err = 0;
            for (unsigned i = 0; i < wi->memsize; ++i)
            {
                if (wi->mem[i] % wi->num_workers != wi->worker_id)
                {
                    err = 1;
                }
            }

            if (err != 0)
            {
                fprintf(stderr, "[%u] error detected, yikes!!!!\n", wi->worker_id);
                assert(0);
            }
        }
    }

    unsigned checksum = memvol;

    for (unsigned i = 0; i < wi->flush_size; ++i)
    {
        checksum += flushmem[i];
    }

    for (unsigned i = 0; i < wi->memsize; ++i)
    {
        checksum += wi->mem[i];
    }

    free(flushmem);

    printf("[%u] worker done (anti-optimization checksum: 0x%x).\n", wi->worker_id, checksum);

    return NULL;
}

void test_cache(unsigned num_workers, unsigned memsize, unsigned num_repeats)
{
    int result;
    unsigned arena_size = memsize * num_workers;
    pthread_barrier_t barrier;

    char * arena = malloc(arena_size);
    assert(arena != NULL);

    char * arena_base = arena; // Perhaps lift this to a multiple of memsize in physical memory?

    result = pthread_barrier_init(&barrier, NULL, num_workers);
    assert(result == 0);

    struct worker_info wi[num_workers];

    for (unsigned i = 0; i < num_workers; ++i)
    {
        wi[i].worker_id = i;
        wi[i].num_workers = num_workers;
        wi[i].barrier = &barrier;
        wi[i].mem = (uint8_t *)&arena_base[memsize * i];
        wi[i].memsize = memsize;
        wi[i].flush_size = 1048576;
        wi[i].num_repeats = num_repeats;
        wi[i].check_interval = 5000;
        result = pthread_create(&wi[i].thread, NULL, worker, &wi[i]);
        assert(result == 0);
    }

    for (unsigned i = 0; i < num_workers; ++i)
    {
        result = pthread_join(wi[i].thread, NULL);
        assert(result == 0);
    }

    result = pthread_barrier_destroy(&barrier);
    assert(result == 0);

    free(arena);
}

double gettime(void)
{
    struct timeval tv;
    int result = gettimeofday(&tv, NULL);
    assert(result == 0);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

int main(void)
{
    unsigned num_workers = 4;

    for (unsigned num_repeats = 1; num_repeats <= 1000000000; num_repeats = num_repeats * (5 - (num_repeats % 9) % 2) / 2)
    {
        const unsigned memsize_array[] = {256, 512, 1024, 2048, 4096, 8192, 16384};

        for (unsigned memsize_index = 0; memsize_index < sizeof(memsize_array) / sizeof(*memsize_array); ++memsize_index)
        {
            unsigned memsize = memsize_array[memsize_index];
            printf("num_repeats: %u size: %u\n", num_repeats, memsize);
            double t0 = gettime();
            test_cache(num_workers, memsize, num_repeats);
            double t1 = gettime();
            printf("done. duration: %.3f seconds.\n\n", (t1 - t0));
        }
    }
    return 0;
}
