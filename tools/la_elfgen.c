#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/kernel/loongarch/early_boot.h"

/* Host-side generator for the first LoongArch boot stub; no LA toolchain needed. */
#define EM_LOONGARCH 258

#define ET_EXEC 2
#define EV_CURRENT 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define PT_LOAD 1
#define PF_X 1
#define PF_R 4

#define MAX_CODE_WORDS 1024

static void die_errno(const char *what, const char *path)
{
    fprintf(stderr, "%s %s: %s\n", what, path, strerror(errno));
    exit(1);
}

static void write_all(FILE *out, const void *buf, size_t len, const char *path)
{
    if (fwrite(buf, 1, len, out) != len) {
        die_errno("write", path);
    }
}

static void emit_u16(FILE *out, uint16_t value, const char *path)
{
    uint8_t buf[2] = {
        (uint8_t)(value),
        (uint8_t)(value >> 8),
    };
    write_all(out, buf, sizeof(buf), path);
}

static void emit_u32(FILE *out, uint32_t value, const char *path)
{
    uint8_t buf[4] = {
        (uint8_t)(value),
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };
    write_all(out, buf, sizeof(buf), path);
}

static void emit_u64(FILE *out, uint64_t value, const char *path)
{
    uint8_t buf[8] = {
        (uint8_t)(value),
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 32),
        (uint8_t)(value >> 40),
        (uint8_t)(value >> 48),
        (uint8_t)(value >> 56),
    };
    write_all(out, buf, sizeof(buf), path);
}

static uint32_t la_lu12i_w(unsigned rd, uint32_t imm20)
{
    return 0x14000000U | ((imm20 & 0xfffffU) << 5) | (rd & 0x1fU);
}

static uint32_t la_ori(unsigned rd, unsigned rj, uint32_t imm12)
{
    return 0x03800000U | ((imm12 & 0xfffU) << 10) |
           ((rj & 0x1fU) << 5) | (rd & 0x1fU);
}

static uint32_t la_st_b(unsigned rd, unsigned rj, uint32_t imm12)
{
    return 0x29000000U | ((imm12 & 0xfffU) << 10) |
           ((rj & 0x1fU) << 5) | (rd & 0x1fU);
}

static void append_insn(uint32_t *code, size_t *words, uint32_t insn)
{
    if (*words >= MAX_CODE_WORDS) {
        fprintf(stderr, "LoongArch boot stub is too large\n");
        exit(1);
    }
    code[*words] = insn;
    *words += 1;
}

static size_t build_boot_stub(uint32_t *code)
{
    static const char msg[] = LA_EARLY_BOOT_LOG;
    size_t words = 0;
    const unsigned uart = 4;
    const unsigned ch = 5;

    append_insn(code, &words, la_lu12i_w(uart, LA_UART_BASE >> 12));
    append_insn(code, &words, la_ori(uart, uart, LA_UART_BASE & 0xfffU));

    for (size_t i = 0; i < sizeof(msg) - 1; i++) {
        append_insn(code, &words, la_ori(ch, 0, (uint8_t)msg[i]));
        append_insn(code, &words, la_st_b(ch, uart, 0));
    }

    append_insn(code, &words, 0x50000000U);
    return words;
}

static void write_padding(FILE *out, uint64_t from, uint64_t to, const char *path)
{
    uint8_t zeros[128] = {0};

    while (from < to) {
        uint64_t remain = to - from;
        size_t chunk = remain < sizeof(zeros) ? (size_t)remain : sizeof(zeros);
        write_all(out, zeros, chunk, path);
        from += chunk;
    }
}

static void write_elf_header(FILE *out, const char *path, uint64_t code_size)
{
    const uint8_t ident[16] = {
        0x7f, 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB, EV_CURRENT,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    write_all(out, ident, sizeof(ident), path);
    emit_u16(out, ET_EXEC, path);
    emit_u16(out, EM_LOONGARCH, path);
    emit_u32(out, EV_CURRENT, path);
    emit_u64(out, LA_KERNEL_ENTRY, path);
    emit_u64(out, 64, path);
    emit_u64(out, 0, path);
    emit_u32(out, 0, path);
    emit_u16(out, 64, path);
    emit_u16(out, 56, path);
    emit_u16(out, 1, path);
    emit_u16(out, 0, path);
    emit_u16(out, 0, path);
    emit_u16(out, 0, path);

    emit_u32(out, PT_LOAD, path);
    emit_u32(out, PF_R | PF_X, path);
    emit_u64(out, LA_TEXT_OFFSET, path);
    emit_u64(out, LA_KERNEL_ENTRY, path);
    emit_u64(out, LA_KERNEL_ENTRY, path);
    emit_u64(out, code_size, path);
    emit_u64(out, code_size, path);
    emit_u64(out, 0x1000, path);
}

int main(int argc, char **argv)
{
    uint32_t code[MAX_CODE_WORDS];
    size_t words;
    FILE *out;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT\n", argv[0]);
        return 1;
    }

    words = build_boot_stub(code);
    out = fopen(argv[1], "wb");
    if (out == NULL) {
        die_errno("open", argv[1]);
    }

    write_elf_header(out, argv[1], words * sizeof(code[0]));
    write_padding(out, 64 + 56, LA_TEXT_OFFSET, argv[1]);
    for (size_t i = 0; i < words; i++) {
        emit_u32(out, code[i], argv[1]);
    }

    if (fclose(out) != 0) {
        die_errno("close", argv[1]);
    }

    return 0;
}
