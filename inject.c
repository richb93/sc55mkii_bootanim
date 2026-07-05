/*
 * inject_sc55_boot.c
 *
 * Patch SC-55mkII / hidden SC-155mkII boot display frames into a control ROM.
 *
 * Build:
 *   cc -O2 -Wall -Wextra -o inject_sc55_boot inject_sc55_boot.c
 *
 * Usage:
 *   ./inject_sc55_boot -55  input.rom output.rom frames_dir
 *   ./inject_sc55_boot -155 input.rom output.rom frames_dir
 *
 * With looping:
 *   ./inject_sc55_boot -55 --loop input.rom output.rom frames_dir
 *
 * Input frames:
 *   frame_0000.txt / .pbm etc, sorted alphabetically.
 *
 * ASCII format:
 *   16 lines of 16 chars
 *   # = on
 *   . = off
 *
 * PBM format:
 *   Plain P1 PBM only.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>

#define FRAME_COUNT 75
#define FRAME_SIZE 64
#define W 16
#define H 16

#define OFFSET_SC55  0x070000
#define OFFSET_SC155 0x0712C0

typedef struct {
    char *path;
} FrameFile;

static int cmp_framefile(const void *a, const void *b)
{
    const FrameFile *fa = (const FrameFile *)a;
    const FrameFile *fb = (const FrameFile *)b;
    return strcmp(fa->path, fb->path);
}

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name);
    size_t e = strlen(ext);
    if (n < e) return 0;
    return strcasecmp(name + n - e, ext) == 0;
}

static char *join_path(const char *dir, const char *name)
{
    size_t len = strlen(dir) + strlen(name) + 2;
    char *out = malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s/%s", dir, name);
    return out;
}

static int load_frame_list(const char *dir, FrameFile **files_out, int *count_out)
{
    DIR *d = opendir(dir);
    if (!d) {
        perror("opendir");
        return -1;
    }

    FrameFile *files = NULL;
    int count = 0;
    int cap = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;

        if (name[0] == '.')
            continue;

        if (!has_ext(name, ".txt") && !has_ext(name, ".pbm"))
            continue;

        if (count >= cap) {
            cap = cap ? cap * 2 : 32;
            FrameFile *tmp = realloc(files, sizeof(FrameFile) * cap);
            if (!tmp) {
                closedir(d);
                return -1;
            }
            files = tmp;
        }

        files[count].path = join_path(dir, name);
        if (!files[count].path) {
            closedir(d);
            return -1;
        }

        count++;
    }

    closedir(d);

    qsort(files, count, sizeof(FrameFile), cmp_framefile);

    *files_out = files;
    *count_out = count;
    return 0;
}

static int read_ascii_frame(const char *path, int px[H][W])
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror(path);
        return -1;
    }

    char line[256];

    for (int y = 0; y < H; y++) {
        if (!fgets(line, sizeof(line), fp)) {
            fprintf(stderr, "%s: expected 16 lines\n", path);
            fclose(fp);
            return -1;
        }

        int x = 0;
        for (int i = 0; line[i] && line[i] != '\n' && line[i] != '\r'; i++) {
            if (line[i] == '#' || line[i] == '1') {
                if (x < W) px[y][x++] = 1;
            } else if (line[i] == '.' || line[i] == '0') {
                if (x < W) px[y][x++] = 0;
            }
        }

        if (x != W) {
            fprintf(stderr, "%s: line %d has %d pixels, expected 16\n",
                    path, y + 1, x);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int pbm_next_token(FILE *fp, char *tok, size_t tok_size)
{
    int c;

    do {
        c = fgetc(fp);
        if (c == '#') {
            while (c != EOF && c != '\n')
                c = fgetc(fp);
        }
    } while (c != EOF && isspace(c));

    if (c == EOF)
        return 0;

    size_t i = 0;
    while (c != EOF && !isspace(c)) {
        if (i + 1 < tok_size)
            tok[i++] = (char)c;
        c = fgetc(fp);
    }

    tok[i] = '\0';
    return 1;
}

static int read_pbm_frame(const char *path, int px[H][W])
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror(path);
        return -1;
    }

    char tok[64];

    if (!pbm_next_token(fp, tok, sizeof(tok)) || strcmp(tok, "P1") != 0) {
        fprintf(stderr, "%s: only plain P1 PBM is supported\n", path);
        fclose(fp);
        return -1;
    }

    if (!pbm_next_token(fp, tok, sizeof(tok))) {
        fclose(fp);
        return -1;
    }
    int w = atoi(tok);

    if (!pbm_next_token(fp, tok, sizeof(tok))) {
        fclose(fp);
        return -1;
    }
    int h = atoi(tok);

    if (w != W || h != H) {
        fprintf(stderr, "%s: PBM must be 16x16, got %dx%d\n", path, w, h);
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!pbm_next_token(fp, tok, sizeof(tok))) {
                fprintf(stderr, "%s: not enough PBM pixel data\n", path);
                fclose(fp);
                return -1;
            }

            if (strcmp(tok, "1") == 0)
                px[y][x] = 1;
            else if (strcmp(tok, "0") == 0)
                px[y][x] = 0;
            else {
                fprintf(stderr, "%s: invalid PBM pixel '%s'\n", path, tok);
                fclose(fp);
                return -1;
            }
        }
    }

    fclose(fp);
    return 0;
}

static int read_frame_file(const char *path, int px[H][W])
{
    if (has_ext(path, ".pbm"))
        return read_pbm_frame(path, px);

    return read_ascii_frame(path, px);
}

static void blank_payload(uint8_t out[FRAME_SIZE])
{
    memset(out, 0, FRAME_SIZE);
}

static void pixels_to_payload(const int px[H][W], uint8_t out[FRAME_SIZE], const char *name)
{
    (void)name; /* column 16 is now supported */

    blank_payload(out);

    for (int y = 0; y < H; y++) {
        for (int group = 0; group < 4; group++) {
            uint8_t v = 0;

            for (int bit = 4; bit >= 0; bit--) {
                int x = group * 5 + (4 - bit);

                /* The fourth group contains column 16 plus off-screen bits. */
                if (x >= W)
                    continue;

                if (px[y][x])
                    v |= (uint8_t)(1 << bit);
            }

            out[group * 16 + y] = v;
        }
    }
}

static int copy_file_to_memory(const char *path, uint8_t **data_out, size_t *size_out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror(path);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return -1;
    }

    rewind(fp);

    uint8_t *data = malloc((size_t)len);
    if (!data) {
        fclose(fp);
        return -1;
    }

    if (fread(data, 1, (size_t)len, fp) != (size_t)len) {
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    *data_out = data;
    *size_out = (size_t)len;
    return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror(path);
        return -1;
    }

    if (fwrite(data, 1, size, fp) != size) {
        perror("fwrite");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s -55  [--loop] input.rom output.rom frames_dir\n"
        "  %s -155 [--loop] input.rom output.rom frames_dir\n",
        argv0, argv0);
}

int main(int argc, char **argv)
{
    int model = 0;
    int loop = 0;
    int arg = 1;

    if (argc < 5) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[arg], "-55") == 0) {
        model = 55;
    } else if (strcmp(argv[arg], "-155") == 0) {
        model = 155;
    } else {
        usage(argv[0]);
        return 1;
    }
    arg++;

    if (arg < argc && strcmp(argv[arg], "--loop") == 0) {
        loop = 1;
        arg++;
    }

    if (argc - arg != 3) {
        usage(argv[0]);
        return 1;
    }

    const char *in_rom = argv[arg++];
    const char *out_rom = argv[arg++];
    const char *frames_dir = argv[arg++];

    size_t patch_offset = (model == 55) ? OFFSET_SC55 : OFFSET_SC155;

    uint8_t *rom = NULL;
    size_t rom_size = 0;

    if (copy_file_to_memory(in_rom, &rom, &rom_size) != 0)
        return 1;

    if (patch_offset + FRAME_COUNT * FRAME_SIZE > rom_size) {
        fprintf(stderr, "ROM is too small for patch offset 0x%lX\n",
                (unsigned long)patch_offset);
        free(rom);
        return 1;
    }

    FrameFile *files = NULL;
    int input_count = 0;

    if (load_frame_list(frames_dir, &files, &input_count) != 0) {
        fprintf(stderr, "Failed to read frames directory\n");
        free(rom);
        return 1;
    }

    if (input_count == 0) {
        fprintf(stderr, "No .txt or .pbm frames found in %s\n", frames_dir);
        free(rom);
        return 1;
    }

    if (input_count > FRAME_COUNT) {
        fprintf(stderr,
                "Warning: %d input frames found; only the first 75 will be used\n",
                input_count);
        input_count = FRAME_COUNT;
    }

    if (input_count < FRAME_COUNT && !loop) {
        fprintf(stderr,
                "Warning: only %d frames supplied; remaining %d frames will be blank\n",
                input_count, FRAME_COUNT - input_count);
    }

    if (input_count < FRAME_COUNT && loop) {
        int loops = FRAME_COUNT / input_count;
        int used = loops * input_count;
        int blank = FRAME_COUNT - used;

        fprintf(stderr,
                "Loop mode: %d input frames x %d full loops = %d frames; %d blank frames\n",
                input_count, loops, used, blank);
    }

    for (int out_i = 0; out_i < FRAME_COUNT; out_i++) {
        int source_i = -1;

        if (loop) {
            int loops = FRAME_COUNT / input_count;
            int used = loops * input_count;

            if (out_i < used)
                source_i = out_i % input_count;
        } else {
            if (out_i < input_count)
                source_i = out_i;
        }

        uint8_t payload[FRAME_SIZE];

        if (source_i < 0) {
            blank_payload(payload);
        } else {
            int px[H][W];

            if (read_frame_file(files[source_i].path, px) != 0) {
                free(rom);
                return 1;
            }

            pixels_to_payload(px, payload, files[source_i].path);
        }

        memcpy(rom + patch_offset + (size_t)out_i * FRAME_SIZE,
               payload,
               FRAME_SIZE);
    }

    if (write_file(out_rom, rom, rom_size) != 0) {
        free(rom);
        return 1;
    }

    printf("Patched %s graphics at 0x%06lX in %s\n",
           model == 55 ? "SC-55mkII" : "SC-155mkII",
           (unsigned long)patch_offset,
           out_rom);

    for (int i = 0; i < input_count; i++)
        free(files[i].path);
    free(files);
    free(rom);

    return 0;
}