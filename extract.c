#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define START_OFFSET 0x70000
#define FRAME_COUNT 150
#define FRAME_SIZE 64
#define W 16
#define H 16

static int pixel_on(const uint8_t f[FRAME_SIZE], int x, int y)
{
    int group = x / 5;
    int within = x % 5;
    int bit = 4 - within;

    if (group < 0 || group >= 4 || y < 0 || y >= H)
        return 0;

    return (f[group * 16 + y] & (1 << bit)) != 0;
}

static void write_ascii(FILE *out, const uint8_t f[FRAME_SIZE])
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++)
            fputc(pixel_on(f, x, y) ? '#' : '.', out);
        fputc('\n', out);
    }
}

static void write_p4_pbm(FILE *out, const uint8_t f[FRAME_SIZE])
{
    /* Binary PBM: P4, 1 bit per pixel, MSB first, 1 = black/on */
    fprintf(out, "P4\n%d %d\n", W, H);

    for (int y = 0; y < H; y++) {
        uint8_t b0 = 0;
        uint8_t b1 = 0;

        for (int x = 0; x < 8; x++) {
            if (pixel_on(f, x, y))
                b0 |= (uint8_t)(0x80 >> x);
        }

        for (int x = 8; x < 16; x++) {
            if (pixel_on(f, x, y))
                b1 |= (uint8_t)(0x80 >> (x - 8));
        }

        fputc(b0, out);
        fputc(b1, out);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s rom.bin out_prefix\n"
        "  %s rom.bin out_prefix txt\n"
        "  %s rom.bin out_prefix p4\n",
        prog, prog, prog);
}

int main(int argc, char **argv)
{
    int output_p4 = 0;

    if (argc != 3 && argc != 4) {
        usage(argv[0]);
        return 1;
    }

    if (argc == 4) {
        if (strcmp(argv[3], "txt") == 0 || strcmp(argv[3], "ascii") == 0) {
            output_p4 = 0;
        } else if (strcmp(argv[3], "p4") == 0 || strcmp(argv[3], "pbm") == 0) {
            output_p4 = 1;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    FILE *rom = fopen(argv[1], "rb");
    if (!rom) {
        perror("open ROM");
        return 1;
    }

    if (fseek(rom, START_OFFSET, SEEK_SET) != 0) {
        perror("seek");
        fclose(rom);
        return 1;
    }

    for (int i = 0; i < FRAME_COUNT; i++) {
        uint8_t frame[FRAME_SIZE];

        if (fread(frame, 1, FRAME_SIZE, rom) != FRAME_SIZE) {
            fprintf(stderr, "Failed reading frame %d\n", i);
            fclose(rom);
            return 1;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s_%04d.%s", argv[2], i,
                 output_p4 ? "pbm" : "txt");

        FILE *out = fopen(path, output_p4 ? "wb" : "w");
        if (!out) {
            perror("open output");
            fclose(rom);
            return 1;
        }

        if (output_p4)
            write_p4_pbm(out, frame);
        else
            write_ascii(out, frame);

        fclose(out);
    }

    fclose(rom);
    return 0;
}
