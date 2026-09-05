#include "keyboard_console.h"

#include <string.h>
#include "keyboard_layout.h"

static char *append(char *out, const char *text)
{
    while (*text) *out++ = *text++;
    return out;
}

static char *number(char *out, uint32_t value)
{
    char digits[10];
    unsigned n = 0u;
    do { digits[n++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    while (n) *out++ = digits[--n];
    return out;
}

static void status(keyboard_console_t *console)
{
    const keyboard_config_t *s = &console->test.config;
    char text[192];
    char *out = text;
    const char *labels[] = {"TEST profile=", " fn=", " mode=", " act=", " rapid=",
                            " enabled=", " dirty=", " saved=", ",", " revision="};
    const uint32_t values[] = {s->profile, s->fn, s->mode, s->actuation, s->rapid,
                              s->rapid_enabled, s->dirty, s->saved_actuation,
                              s->saved_rapid, s->revision};
    for (unsigned i = 0; i < 10u; ++i) out = number(append(out, labels[i]), values[i]);
    /* At most 180 bytes even if every value spans ten digits. No printf
     * allocator, floating-point formatter or newlib syscall dependency. */
    out = append(out, "\r\n");
    *out = '\0';
    console->output(text);
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void execute(keyboard_console_t *console)
{
    const char *line = console->line;
    if (console->command && console->command(line)) return;
    if (!strcmp(line, "help"))
    {
        console->output("help | status | test reset | test profile 1/2/3 | test key XX down/up | test status | test report\r\n"
                        "TEST events are isolated: no host keystrokes, flash writes, GPIO, or resets.\r\n");
    }
    else if (!strcmp(line, "status"))
        console->output("APP keyboard diagnostics; optical=off; host keys=neutral; settings=RAM-only\r\n");
    else if (!strcmp(line, "test reset"))
    {
        keyboard_engine_init(&console->test, console->test.config.profile);
        status(console);
    }
    else if (!strcmp(line, "test status")) status(console);
    else if (strlen(line) == 14u && !memcmp(line, "test profile ", 13u) &&
             line[13] >= '1' && line[13] <= '3')
    {
        keyboard_engine_init(&console->test, (uint8_t)(line[13] - '0'));
        status(console);
    }
    else if (strlen(line) >= 14u && !memcmp(line, "test key ", 9u) &&
             hex(line[9]) >= 0 && hex(line[10]) >= 0 && line[11] == ' ' &&
             (!strcmp(line + 12, "down") || !strcmp(line + 12, "up")))
    {
        const uint8_t key = (uint8_t)(hex(line[9]) * 16 + hex(line[10]));
        if (keyboard_action(console->test.config.profile, key, 0u) == NULL)
            console->output("ERR unknown production key ID\r\n");
        else
        {
            (void)keyboard_engine_event(&console->test, key, line[12] == 'd');
            status(console);
        }
    }
    else if (!strcmp(line, "test report"))
    {
        const uint8_t *report = (const uint8_t *)&console->test.report;
        static const char digits[] = "0123456789abcdef";
        char text[48] = "TEST report=";
        for (unsigned i = 0; i < sizeof(console->test.report); ++i)
        {
            text[12u + i * 2u] = digits[report[i] >> 4u];
            text[13u + i * 2u] = digits[report[i] & 15u];
        }
        memcpy(text + 44u, "\r\n", 3u);
        console->output(text);
    }
    else console->output("ERR command; use help\r\n");
}

void keyboard_console_init(keyboard_console_t *console, keyboard_console_output_t output)
{
    memset(console, 0, sizeof(*console));
    console->output = output;
    keyboard_engine_init(&console->test, 1u);
}

void keyboard_console_overflow(keyboard_console_t *console)
{
    console->length = 0u;
    console->discard = true;
}

void keyboard_console_feed(keyboard_console_t *console, uint8_t byte)
{
    if (byte == '\r' || byte == '\n')
    {
        if (console->discard) console->output("ERR line discarded\r\n");
        else if (console->length)
        {
            console->line[console->length] = '\0';
            execute(console);
        }
        console->length = 0u;
        console->discard = false;
    }
    else if (!console->discard)
    {
        if (byte < 32u || byte > 126u || console->length >= sizeof(console->line) - 1u)
            keyboard_console_overflow(console);
        else console->line[console->length++] = (char)byte;
    }
}
