#include "efi.h"

void efi_puts(efi_system_table_t *st, const CHAR16 *s)
{
    st->con_out->output_string(st->con_out, (CHAR16 *)s);
}

void efi_print_hex64(efi_system_table_t *st, uint64_t value)
{
    CHAR16 buf[] = {
        '0', 'x',
        '0', '0', '0', '0', '0', '0', '0', '0',
        '0', '0', '0', '0', '0', '0', '0', '0',
        0
    };

    for (int i = 0; i < 16; i++) {
        unsigned int shift = (15 - i) * 4;
        unsigned int digit = (value >> shift) & 0xf;
        buf[2 + i] = (digit < 10) ? (CHAR16)('0' + digit)
                                  : (CHAR16)('a' + digit - 10);
    }

    efi_puts(st, buf);
}

void efi_print_u64(efi_system_table_t *st, uint64_t value)
{
    CHAR16 buf[21];
    int pos = 20;

    buf[pos] = 0;
    if (value == 0) {
        buf[--pos] = '0';
    } else {
        while (value != 0 && pos > 0) {
            buf[--pos] = (CHAR16)('0' + (value % 10));
            value /= 10;
        }
    }

    efi_puts(st, &buf[pos]);
}
