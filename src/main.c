#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "./recdir.h"
#include "./sha256.h"
#define STB_DS_IMPLEMENTATION
#include "./stb_ds.h"

#define CHUNK_SIZE 4096
#define REF_COUNT 100000
#define OUTPUT_FILE "output.txt"

/*256-bit hash value*/
typedef struct {
    BYTE bytes[32];
} Hash;

typedef struct {
    Hash key;
    u_int64_t ref;
} Record;

static Record *db = NULL;

static u_int64_t total_chunks = 0;

char hex_digit(unsigned int digit)
{
    digit = digit % 0x10;
    if (digit <= 9) return digit + '0';
    if (10 <= digit && digit <= 15) return digit - 10 + 'a';
    assert(0 && "unreachable");
}

void hash_as_cstr(Hash hash, char output[32*2 + 1])
{
    for (size_t i = 0; i < 32; ++i) {
        output[i*2 + 0] = hex_digit(hash.bytes[i] / 0x10);
        output[i*2 + 1] = hex_digit(hash.bytes[i]);
    }
    output[32*2] = '\0';
}

void hash_of_file(const char *file_path)
{
    Hash hash;

    FILE *f = fopen(file_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Could not open file %s: %s\n",
                file_path, strerror(errno));
        exit(1);
    }

    /*Read 4KB from file to buffer.*/
    BYTE buffer[CHUNK_SIZE];
    size_t buffer_size = fread(buffer, 1, sizeof(buffer), f);

    while (buffer_size > 0) {
        total_chunks += 1;
        /*Calculate sha256 hash value of buffer.*/
        SHA256_CTX ctx;
        memset(&ctx, 0, sizeof(ctx));
        sha256_init(&ctx);
        sha256_update(&ctx, buffer, buffer_size);
        sha256_final(&ctx, hash.bytes);
         /*Add the sha256 hash value to db*/
        ptrdiff_t index = hmgeti(db, hash);
        if (index < 0) {
            Record record;
            record.key = hash;
            record.ref = 1;
            hmputs(db, record);
        } else {
            db[index].ref += 1;
        }
        buffer_size = fread(buffer, 1, sizeof(buffer), f);
    }

    if (ferror(f)) {
        // fprintf(stderr, "Could not read from file %s: %s\n",
        //         file_path, strerror(errno));
        // exit(1);
        printf("Could not read from file %s.\n",
                file_path);
    }
    fclose(f);
}


int main(int argc, char **argv)
{
    RECDIR *recdir = NULL;
    errno = 0;

    switch (argc)
    {
    case 1:
        printf("No arguments given, using current directory.\n");
        recdir = recdir_open(".");
        break;
    case 2:
        recdir = recdir_open(argv[1]);
        break;
    default:
        fprintf(stderr, "Usage: %s [path]\n", argv[0]);
        exit(1);
    }

    assert(recdir != NULL);

    struct dirent *ent = recdir_read(recdir);

    while (ent) {
        char *path = join_path(recdir_top(recdir)->path, ent->d_name);
        hash_of_file(path);
        ent = recdir_read(recdir);
    }

    if (errno != 0) {
        fprintf(stderr,
                "ERROR: could not read the directory: %s\n",
                recdir_top(recdir)->path);
        exit(1);
    }

    recdir_close(recdir);

    u_int64_t refs[REF_COUNT] = {0};
    printf("db size: %lu\n", hmlen(db));

    for (ptrdiff_t i = 0; i < hmlen(db); ++i) {
        if (db[i].ref < REF_COUNT) {
            refs[db[i].ref] += 1;
        }
        else {
            printf("Ref count %lu is too large (> %d)\n", db[i].ref, REF_COUNT);
            exit(1);
        }
    }

    FILE *f = fopen(OUTPUT_FILE, "w");
    if (f == NULL) {
        fprintf(stderr, "Could not open file %s: %s\n",
                OUTPUT_FILE, strerror(errno));
        exit(1);
    }


    for(ptrdiff_t i = 0; i < REF_COUNT; ++i) {
        if(refs[i] > 0) {
            fprintf(f, "%lu \t%lu\n", i, refs[i]);
        }
    }

    fprintf(f, "\nTotal chunks: \t%lu\n", total_chunks);
    fprintf(f, "Duplication Ratio: \t%lu\n", 1 - (refs[1]/total_chunks));
    
    fclose(f);
    return 0;
}
