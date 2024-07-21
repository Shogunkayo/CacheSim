#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include <string.h>
#include <unistd.h>

#define NUMTHREADS 2
#define BUSCAPACITY 1000

typedef char byte;

enum STATE {
	MODIFIED,
	EXCLUSIVE,
    SHARED,
    INVALID
};

struct cache {
    byte address; // This is the address in memory.
    byte value; // This is the value stored in cached memory.
    enum STATE state; // State for MESI protocol
};

struct decoded_inst {
    int type; // 0 is RD, 1 is WR
    byte address;
    byte value; // Only used for WR 
};

struct bus_transaction {
	int type;
	byte address;
	int issue_id;
};

typedef struct cache cache;
typedef struct decoded_inst decoded;
typedef struct bus_transaction transaction;

const char *files[] = {"inputs/core_1.txt", "inputs/core_2.txt", "inputs/core_3.txt", "inputs/core_4.txt"};
byte *memory;
transaction *shared_bus;
int top;

decoded decode_inst_line(char * buffer) {
    decoded inst;
    char inst_type[2];
    sscanf(buffer, "%s", inst_type);
	if (!strcmp(inst_type, "RD")) {
        inst.type = 0;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    }
	else if (!strcmp(inst_type, "WR")) {
        inst.type = 1;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = val;
    }
    return inst;
}

void print_cachelines(cache * c, int cache_size) {
    for(int i = 0; i < cache_size; i++){
        cache cacheline = *(c+i);
        printf("Address: %d, State: %d, Value: %d\n", cacheline.address, cacheline.state, cacheline.value);
    }
}

void print_bus(int thread_num, int local_top) {
     printf("Shared bus top: %d\tCore: %d\tLocal top: %d\n", top, thread_num+1, local_top);
     for (int i = local_top; i < top % BUSCAPACITY; i++) {
        printf("Type: %d\tAddress: %d\tIssue core: %d\n", shared_bus[i].type, shared_bus[i].address, shared_bus[i].issue_id+1);
     }
     printf("\n");
}

void cpu_loop(int thread_num) {
    const int cache_size = 2;
    cache * c = (cache *) malloc(sizeof(cache) * cache_size);
	// local index into the shared bus
	int local_top = 0;
    // Read Input file
    FILE * inst_file = fopen(files[thread_num], "r");
    char inst_line[20];

    while (fgets(inst_line, sizeof(inst_line), inst_file)){
        sleep((thread_num + 1) * 2); 
        #ifdef DEBUG
            print_bus(thread_num, local_top);
        #endif

		while (local_top < top) {
            transaction snoop_t = shared_bus[local_top % BUSCAPACITY];
			if (snoop_t.issue_id != thread_num) {
				int hash = snoop_t.address % cache_size;
				if (c[hash].address == snoop_t.address) {
                    switch(snoop_t.type) {
                        case 0: 
                                if (c[hash].state == MODIFIED) {
                                    // write back to memory on read miss
                                    #pragma omp critical
                                    {
                                        *(memory + c[hash].address) = c[hash].value;
                                    }
                                    // signal the cache that read the cacheline to read the updated value
                                    transaction t = {3, snoop_t.address, thread_num};
                                    #pragma omp critical
                                    {
                                        shared_bus[(top++ % BUSCAPACITY)] = t;
                                    }
                                }
                                else if (c[hash].state == EXCLUSIVE || c[hash].state == SHARED) {
                                    // signal the cache that read the cacheline that cacheline is SHARED
                                    transaction t = {2, snoop_t.address, thread_num};
                                    #pragma omp critical
                                    {
                                        shared_bus[(top++ % BUSCAPACITY)] = t;
                                    }
                                }
                                c[hash].state = SHARED;
                                break;

                        case 1: c[hash].state = INVALID;
                                break;

                        case 2: c[hash].state = SHARED;
                                break;

                        case 3:
                                // cachline will be in EXCLUSIVE on read miss initially
                                if (c[hash].state == EXCLUSIVE) {
                                    c[hash].value = *(memory + c[hash].address);
                                    c[hash].state = SHARED;
                                }
                                break;
                    }
				}
			}
			local_top++;
		}
        decoded inst = decode_inst_line(inst_line);

        int hash = inst.address%cache_size;
        cache cacheline = *(c+hash);

        // cache does not contain the address
        if (cacheline.address != inst.address) {
            // Flush current cacheline to memory if modified
            if (cacheline.state == MODIFIED) {
                #pragma omp critical
                {
                    *(memory + cacheline.address) = cacheline.value;
                }
            }

            cacheline.address = inst.address;

            // read miss
            if (inst.type == 0) {
                cacheline.value = *(memory + inst.address);
                cacheline.state = EXCLUSIVE;
                // signal the bus that a cache is reading a cacheline
                transaction t = {0, inst.address, thread_num};
                #pragma omp critical
                {
                    shared_bus[(top++ % BUSCAPACITY)] = t;
                }
            }
            // write miss
            else {
                cacheline.value = inst.value;
                cacheline.state = MODIFIED;
                // signal the bus that a cache is reading and writing to a cacheline
                transaction t = {1, inst.address, thread_num};
                #pragma omp critical
                {
                    shared_bus[(top++ % BUSCAPACITY)] = t;
                }
            }
        }
        else {
            // read on invalid state treated as read miss
            // all other reads retain their state so no transaction needed
            if (inst.type == 0 && cacheline.state == INVALID) {
                cacheline.value = *(memory + inst.address);
                cacheline.state = EXCLUSIVE;
                transaction t = {0, inst.address, thread_num};
                #pragma omp critical
                {
                    shared_bus[(top++ % BUSCAPACITY)] = t;
                }
            }
            
            // all writes will result in transition to MODIFIED state
            else if (inst.type == 1) {
                cacheline.value = inst.value;
                cacheline.state = MODIFIED;
                // transaction signal required only if not in MODIFIED state
                if (cacheline.state != MODIFIED) {
                    transaction t = {1, inst.address, thread_num};
                    #pragma omp critical
                    {
                        shared_bus[(top++ % BUSCAPACITY)] = t;
                    }
                }
            }
        }

        *(c+hash) = cacheline;

        switch(inst.type){
            case 0:
                printf("Core %d: Reading from address %d: %d\tState: %d\n", thread_num+1, cacheline.address, cacheline.value, cacheline.state);
                break;
            
            case 1:
                printf("Core %d: Writing to address %d: %d\tState: %d\n", thread_num+1, cacheline.address, cacheline.value, cacheline.state);
                break;
        }
    }
    
    #ifdef DEBUG
        printf("Core %d: Finished operations\n", thread_num+1);
        print_cachelines(c, cache_size);
        printf("\n");
    #endif

	fclose(inst_file);
    free(c);
}

int main(int c, char * argv[]) {
    // Initialize Global memory
    // Let's assume the memory module holds about 24 bytes of data.
    const int memory_size = 24;
    memory = (byte *) malloc(sizeof(byte) * memory_size);

    // initialize the values in memory
    for (int i = 0; i < memory_size; i++) {
        memory[i] = i;
    }

	top  = 0;
	shared_bus = (transaction *) malloc(sizeof(transaction) * BUSCAPACITY);

	#pragma omp parallel num_threads(NUMTHREADS) shared(memory, memory_size, shared_bus, top)
	{
		int id = omp_get_thread_num();
    	cpu_loop(id);
	}

    free(memory);
}
