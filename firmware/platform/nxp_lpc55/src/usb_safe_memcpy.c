#include <stddef.h>
#include <stdint.h>

typedef uint32_t alias_word_t __attribute__((__may_alias__));

/* Production 0x200004a2: word copies only when BOTH pointers are aligned,
 * then byte copies for the entire tail (also when either pointer is odd).
 * Newlib's prebuilt ARM memcpy uses an odd-address STRH for a 3-byte tail;
 * USB SRAM is Device memory, so -mno-unaligned-access alone cannot fix it.
 * Volatile accesses preserve widths and prevent loop-to-memcpy folding.
 * Unlike the production helper, preserve standard memcpy's return value. */
void *__wrap_memcpy(void *destination, const void *source, size_t length)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    if ((((uintptr_t)out | (uintptr_t)in) & 3u) == 0u)
    {
        while (length >= 4u)
        {
            *(volatile alias_word_t *)out = *(const volatile alias_word_t *)in;
            out += 4u;
            in += 4u;
            length -= 4u;
        }
    }
    while (length-- != 0u)
        *(volatile uint8_t *)out++ = *(const volatile uint8_t *)in++;
    return destination;
}
