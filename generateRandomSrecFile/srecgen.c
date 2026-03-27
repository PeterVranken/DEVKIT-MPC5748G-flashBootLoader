#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MAX_RECORD_DATA 16   // bytes per S3 record (tweak as you like, e.g., 32)

// ---- Utilities ----

static uint8_t checksum_srec(uint8_t count, const uint8_t *addr_be, int addr_len,
                             const uint8_t *data, int len) {
    uint32_t sum = count;
    for (int i = 0; i < addr_len; i++) sum += addr_be[i];
    for (int i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(~sum);
}

// ---- Record writers ----

// S0 header: 2-byte address (usually 0), text payload, 1-byte checksum
static void write_s0(FILE *f, const char *text) {
    // The S0 "header" record contains these fields (all counted in bytes):
    //   mname: char[20]
    //   ver: char[2]
    //   rev: char[2]
    //   description: char[0-36]
    uint8_t len = (uint8_t)strlen(text);
    if (len > 60) len = 60;

    uint8_t count = (uint8_t)(len + 3); // addr(2) + checksum(1)
    uint8_t addr_be[2] = {0x00, 0x00};
    uint8_t csum = checksum_srec(count, addr_be, 2, (const uint8_t*)text, len);

    fprintf(f, "S0%02X0000", count);
    for (int i = 0; i < len; i++) fprintf(f, "%02X", (uint8_t)text[i]);
    fprintf(f, "%02X\n", csum);
}

// S3 data: 4-byte address, data payload, 1-byte checksum
static void write_s3(FILE *f, uint32_t addr, const uint8_t *data, int len) {
    uint8_t count = (uint8_t)(len + 5); // addr(4) + checksum(1)
    uint8_t addr_be[4] = {
        (uint8_t)(addr >> 24),
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr)
    };
    uint8_t csum = checksum_srec(count, addr_be, 4, data, len);

    fprintf(f, "S3%02X%08X", count, addr);
    for (int i = 0; i < len; i++) fprintf(f, "%02X", data[i]);
    fprintf(f, "%02X\n", csum);
}

// S7 termination: 4-byte address (entry point), checksum
static void write_s7(FILE *f, uint32_t entry_addr) {
    uint8_t count = 5; // addr(4) + checksum(1)
    uint8_t addr_be[4] = {
        (uint8_t)(entry_addr >> 24),
        (uint8_t)(entry_addr >> 16),
        (uint8_t)(entry_addr >> 8),
        (uint8_t)(entry_addr)
    };
    uint8_t csum = checksum_srec(count, addr_be, 4, NULL, 0);
    fprintf(f, "S7%02X%08X%02X\n", count, entry_addr, csum);
}

// ---- Main ----

static int parse_hex32(const char *s, uint32_t *out) {
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (errno != 0 || end == s || *end != '\0' || v > 0xFFFFFFFFUL) return 0;
    *out = (uint32_t)v;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 4 || ((argc - 3) % 2) != 0) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s <outfile.s37> <s0_text> <from1> <to1> [<from2> <to2> ...]\n", argv[0]);
        fprintf(stderr, "Notes:\n");
        fprintf(stderr, "  - Addresses are hex (e.g., 1000 2000 or 80000000 80001000)\n");
        fprintf(stderr, "  - Ranges are [from, to) — 'to' is exclusive.\n");
        return 1;
    }

    const char *outfile = argv[1];
    const char *s0text  = argv[2];

    FILE *f = fopen(outfile, "w");
    if (!f) {
        perror("Cannot open output");
        return 1;
    }

    srand((unsigned)time(NULL));

    // Header
    write_s0(f, s0text);

    // Optional: choose entry point as first 'from' (or keep 0)
    uint32_t entry_addr = 0;
    int have_entry = 0;

    // Process address ranges
    for (int i = 3; i < argc; i += 2) {
        uint32_t from, to;
        if (!parse_hex32(argv[i], &from) || !parse_hex32(argv[i+1], &to)) {
            fprintf(stderr, "Invalid hex address in range %s %s\n", argv[i], argv[i+1]);
            fclose(f);
            return 1;
        }
        if (to < from) {
            fprintf(stderr, "Range error: to < from in %s %s\n", argv[i], argv[i+1]);
            fclose(f);
            return 1;
        }
        if (!have_entry) {
            entry_addr = from; // if you prefer 0, remove this block
            have_entry = 1;
        }

        for (uint32_t addr = from; addr < to; ) {
            uint8_t data[MAX_RECORD_DATA];
            uint32_t remain = to - addr;
            int len = (remain > MAX_RECORD_DATA) ? MAX_RECORD_DATA : (int)remain;

            for (int j = 0; j < len; j++) data[j] = (uint8_t)(rand() & 0xFF);

            write_s3(f, addr, data, len);
            addr += (uint32_t)len;
        }
    }

    // Termination
    write_s7(f, entry_addr); // or write_s7(f, 0) to force zero

    fclose(f);
    return 0;
}