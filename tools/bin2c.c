#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_symbol_name(const char *input, char *output, size_t out_len)
{
    size_t j = 0;

    for (size_t i = 0; input[i] != '\0' && j + 1 < out_len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isalnum(c)) {
            output[j++] = (char)c;
        } else {
            output[j++] = '_';
        }
    }

    if (j == 0 && out_len > 1) {
        output[j++] = '_';
    }

    output[j] = '\0';
}

static int write_c_header(FILE *out, const char *symbol, FILE *in)
{
    if (fseek(in, 0, SEEK_END) != 0) {
        return -1;
    }

    long size = ftell(in);
    if (size < 0) {
        return -1;
    }

    if (fseek(in, 0, SEEK_SET) != 0) {
        return -1;
    }

    fprintf(out, "unsigned char %s[] = {", symbol);

    for (long i = 0; i < size; i++) {
        int ch = fgetc(in);
        if (ch == EOF) {
            return -1;
        }

        if (i % 12 == 0) {
            fprintf(out, "\n  ");
        }

        fprintf(out, "0x%02x", (unsigned char)ch);
        if (i + 1 < size) {
            fprintf(out, ", ");
        }
    }

    if (size > 0) {
        fprintf(out, "\n");
    }

    fprintf(out, "};\n");
    fprintf(out, "unsigned int %s_len = %ld;\n", symbol, size);

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input> <output>\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    char symbol[256];
    make_symbol_name(input_path, symbol, sizeof(symbol));

    FILE *in = fopen(input_path, "rb");
    if (!in) {
        perror("open input");
        return 1;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        perror("open output");
        fclose(in);
        return 1;
    }

    int result = write_c_header(out, symbol, in);

    fclose(in);
    fclose(out);

    if (result != 0) {
        fprintf(stderr, "bin2c: failed to write header\n");
        return 1;
    }

    return 0;
}
