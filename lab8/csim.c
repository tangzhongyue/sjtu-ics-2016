/* 515030910312 唐仲乐*/
#include <stdio.h>
#include <assert.h>
#include "cachelab.h"
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
/* s : Number of set index bits 
 * S : Number of sets 
 * E : Number of lines per set
 * b : Number of block bits
 * hit_count : Number of hits
 * miss_count : Number of misses
 * eviction_count : Number of evictions
 * verbose : Verbose flag
 * *trace_file : Name of the valgrind trace to replay
 */  
static int s = 0;
static int S = 0;
static int E = 0;
static int b = 0;
static int hit_count = 0;
static int miss_count = 0;
static int eviction_count = 0;
static char verbose = 0;
static char *trace_file;
/*
 * Line - Include the index of least_recently used line(the larger the number is, the less recently the line was used),
 * valid flag, tag index 
 */
typedef struct {
    int LRU;
    int valid;
    int tag;
} Line;
/*
 * Set - Include a pointer to a Line array
 */
typedef struct {
    Line *lines;
} Set;
/*
 * Cache - Include a pointer to a Set array
 */
typedef struct {
    Set *sets;
} Cache;
/*
 * init_Cache - Initialize a simulated cache
 */
void init_Cache(Cache *new_cache){
    int set_index = 0;
    int line_index = 0;
    new_cache->sets = (Set *) malloc(sizeof(Set) * S);
    for(set_index = 0; set_index < S; set_index ++){
        new_cache->sets[set_index].lines = (Line *) malloc(sizeof(Line) * E);
        for(line_index = 0; line_index < E; line_index ++){
            new_cache->sets[set_index].lines[line_index].LRU = 0;
            new_cache->sets[set_index].lines[line_index].valid = 0;
            new_cache->sets[set_index].lines[line_index].tag = 0;
        }
    }
}
/*
 * free_Cache - Free a simulated cache
 */
void free_Cache(Cache *used_cache){
    int set_index = 0;
    for(set_index = 0; set_index < S; set_index ++){
        if(used_cache->sets[set_index].lines != NULL)
            free(used_cache->sets[set_index].lines);
    }
    if(used_cache->sets != NULL){
        free(used_cache->sets);
    }
}
/*
 * get_tag - Get tag index from an address
 */
int get_tag(unsigned long int addr){
    return addr >> (s + b);
}
/*
 * get_set - Get set index from an address
 */
int get_set(unsigned long int addr){
    return (addr >> b) & ((1 << s) - 1);
}
/*
 * update_LRU - Update the LRU index
 */
void update_LRU(Cache csim, int set_index, int MRU_line_index){
    int line_index;
    csim.sets[set_index].lines[MRU_line_index].LRU = -1;
    for(line_index = 0; line_index < E; line_index ++){
        csim.sets[set_index].lines[line_index].LRU += 1;
    }
}
/*
 * find_max_LRU - Find the index of the least-recently used line in the set
 */
int find_max_LRU(Cache csim, int set_index){
    int max_line_index = 0, max_LRU = -1, line_index;
    for(line_index = 0; line_index < E; line_index ++){
        if(csim.sets[set_index].lines[line_index].LRU > max_LRU){
            max_LRU = csim.sets[set_index].lines[line_index].LRU;
            max_line_index = line_index;
        }
    }
    return max_line_index;
} 
/*
 * load_or_store - Simulate the operation of data load or store
 */
void load_or_store(Cache csim, unsigned long int addr, int size){
    int set_index = get_set(addr);
    int tag_index = get_tag(addr);
    int line_index;
    for(line_index = 0; line_index < E; line_index ++){
        if(csim.sets[set_index].lines[line_index].valid == 1 && 
            csim.sets[set_index].lines[line_index].tag == tag_index){
            if(verbose) printf("hit ");
            hit_count += 1;
            update_LRU(csim, set_index, line_index);
            return;
        }
    }
    if(verbose) printf("miss ");
    miss_count += 1;
    for(line_index = 0; line_index < E; line_index ++){
        if(csim.sets[set_index].lines[line_index].valid == 0){
            csim.sets[set_index].lines[line_index].valid = 1;
            csim.sets[set_index].lines[line_index].tag = tag_index;
            update_LRU(csim, set_index, line_index);
            return;
        }
    }
    if(verbose) printf("eviction ");
    eviction_count += 1;
    int evict_line_index = find_max_LRU(csim, set_index);
    csim.sets[set_index].lines[evict_line_index].tag = tag_index;
    update_LRU(csim, set_index, evict_line_index);
    return;
}
/*
 * modify - Simulate the operation of data modify
 */
void modify(Cache csim, unsigned long int addr, int size){
    load_or_store(csim, addr, size);
    load_or_store(csim, addr, size);
}
/*
 * usage - Print usage info
 */
void usage(char *argv[]){
	printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
	printf("Options:\n");
	printf("  -h         Print this help message.\n");
	printf("  -v         Optional verbose flag.\n");
	printf("  -s <num>   Number of set index bits.\n");
	printf("  -E <num>   Number of lines per set.\n");
	printf("  -b <num>   Number of block offset bits.\n");
	printf("  -t <file>  Trace file.\n");
	printf("Examples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
}
int main(int argc, char* argv[])
{
	char c, operation;
    int size;
    unsigned long int addr;
    /* Get arguments */
    while ((c = getopt(argc,argv,"hvs:E:b:t:")) != -1) {
        switch(c) {
        case 'v':
            verbose = 1;
            break;
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
        	E = atoi(optarg);
        	break;
        case 'b':
        	b = atoi(optarg);
        	break;
        case 't':
            trace_file = optarg;
        	break;
        case 'h':
            usage(argv);
            exit(0);
            break;
        default:
            usage(argv);
            exit(1);
        }
    }
    /* Handle unexpected commandline input */
    if(argc == 1 || s == 0 || E == 0 || b == 0) {
        printf("%s: Missing required command line argument\n",argv[0]);
        usage(argv);
        exit(1);
    }
    /* Simulate a cache */
    S = 1 << s;
    Cache csim;
    init_Cache(&csim);  
    FILE* in_fp = fopen(trace_file, "r");
    assert(in_fp);
    while(fscanf(in_fp, "%c %lx,%d", &operation, &addr, &size) != -1){
        /* If "I", ignore it
         * If "L" or "S", access the cache once
         * If "M", access the cache twice
         */
        switch(operation) {
            case 'I':
                break;
            case 'L':
                if(verbose) printf("L %lx,%d ",addr,size);
                load_or_store(csim, addr, size);
                if(verbose) printf("\n");
                break;
            case 'S':
                if(verbose) printf("S %lx,%d ",addr,size);
                load_or_store(csim, addr, size);
                if(verbose) printf("\n");
                break;
            case 'M':
                if(verbose) printf("M %lx,%d ",addr,size);
                modify(csim, addr, size);
                if(verbose) printf("\n");
                break;
            default:
                break;
        }
    }
    printSummary(hit_count, miss_count, eviction_count);
    free_Cache(&csim);
    return 0;
}
