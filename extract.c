#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define START_OFFSET 0x70000
#define FRAME_COUNT 150
#define FRAME_SIZE 64

static void write_ascii(FILE *out, const uint8_t f[64])
{
    for (int y = 0; y < 16; y++) {
        for (int group = 0; group < 3; group++) {
            uint8_t v = f[group * 16 + y] & 0x1F;

            for (int bit = 4; bit >= 0; bit--) {
                fputc((v & (1 << bit)) ? '#' : '.', out);
            }
        }

        /* 16th column, not present in the 3x5-byte payload */
        fputc('.', out);
        fputc('\n', out);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s rom.bin out_prefix\n", argv[0]);
        return 1;
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
        snprintf(path, sizeof(path), "%s_%04d.txt", argv[2], i);

        FILE *out = fopen(path, "w");
        if (!out) {
            perror("open output");
            fclose(rom);
            return 1;
        }

        write_ascii(out, frame);
        fclose(out);
    }

    fclose(rom);
    return 0;
}
