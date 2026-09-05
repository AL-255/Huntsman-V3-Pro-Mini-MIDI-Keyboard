#include "keyboard_raw.h"
#include "keyboard_layout.h"
#include <assert.h>
#include <stdio.h>

static keyboard_raw_t s;
static uint16_t raw[61];
static void frame(void) { keyboard_raw_frame(&s, raw, 61, 1, true); }
static bool a(void) { return keyboard_report_get_usage(&s.engine.report, 4); }

static void check_velocity(float actual, int32_t raw_velocity)
{
    const double expected = raw_velocity <= 0 ? 0.0 : raw_velocity >= 4500000 ? 1.0
                            : raw_velocity / 4500000.0;
    const double difference = actual - expected;
    assert(actual >= 0.0f && actual <= 1.0f);
    assert(difference > -0.0000001 && difference < 0.0000001);
}

static void velocity_history_oracle(void)
{
    keyboard_raw_t keys;
    uint16_t history[65][256], values[65];
    bool triggers[65][256] = {{false}}, down[65] = {false};
    uint32_t completed[65] = {0}, random = 42;
    int32_t last[65] = {0};
    keyboard_raw_init(&keys);
    keyboard_raw_enable(&keys, false);
    for (unsigned i = 0; i < 65; ++i) values[i] = 3900;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keyboard_raw_set_all(&keys, 3000, 3300));
    for (unsigned frame = 0; frame < 256; ++frame) {
        for (unsigned i = 0; i < 65; ++i) {
            random = random*1664525u + 1013904223u;
            values[i] = history[i][frame] = frame ? 2800 + random%701 : 3900;
            const bool next = down[i] ? values[i] <= 3300 : values[i] < 3000;
            triggers[i][frame] = next && !down[i];
            down[i] = next;
            if (frame >= 5 && triggers[i][frame-5]) {
                const uint16_t *p = &history[i][frame-4];
                last[i] = 800*(2*(int32_t)p[0]+p[1]-p[3]-2*(int32_t)p[4]);
                ++completed[i];
            }
        }
        keyboard_raw_frame(&keys, values, 65, 3, true);
        for (unsigned i = 0; i < 65; ++i) {
            assert(keys.velocity[i].captures == completed[i]);
            assert(keys.velocity[i].valid == (completed[i] != 0));
            if (completed[i]) check_velocity(keys.velocity[i].value, last[i]);
            unsigned mask = 0;
            for (unsigned age = 0; age < 5 && age <= frame; ++age)
                if (triggers[i][frame-age]) mask |= 1u << age;
            assert(keys.velocity[i].pending == mask);
            assert(keys.velocity[i].ready == !down[i]);
        }
    }
    puts("PASS 16640 randomized per-key frames against full-history fit/trigger oracle");
}

static void velocity_tests(void)
{
    keyboard_raw_t keys;
    uint16_t values[65];
    keyboard_raw_init(&keys);
    keyboard_raw_enable(&keys, false); /* tuning without host key injection */
    for (unsigned i = 0; i < 65; ++i) values[i] = 3900;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    for (unsigned i = 0; i < 65; ++i) values[i] = 3500;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    for (unsigned sample = 1; sample <= 5; ++sample) {
        for (unsigned i = 0; i < 65; ++i) values[i] = 3500 - (i+1)*sample;
        keyboard_raw_frame(&keys, values, 65, 3, true);
        for (unsigned i = 0; i < 65; ++i) {
            assert(keys.velocity[i].captures == (sample == 5));
            if (sample == 5) check_velocity(keys.velocity[i].value, (int32_t)(8000*(i+1)));
        }
    }
    for (unsigned n = 0; n < 10; ++n) keyboard_raw_frame(&keys, values, 65, 3, true);
    for (unsigned i = 0; i < 65; ++i) assert(keys.velocity[i].captures == 1);
    assert(!keys.armed); /* velocity is independent of HID enable */
    values[0] = 3700; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(!keys.velocity[0].ready);
    values[0] = 3701; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].ready && !keys.velocity[1].ready);
    values[0] = 3600; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(!keys.velocity[0].pending);

    /* Three overlapping press windows: release does not truncate the prior
     * five-point fit, and the next key press is never lost or mixed by key. */
    const uint16_t rapid[] = {3500,3800,3490,3810,3480,3400,3390,3380,3370,3360};
    for (unsigned n = 0; n < sizeof(rapid)/sizeof(rapid[0]); ++n) {
        values[0] = rapid[n]; keyboard_raw_frame(&keys, values, 65, 3, true);
        if (n == 5 || n == 7 || n == 9) {
            const uint16_t *p = &rapid[n-4];
            const int32_t expected = 800*(2*(int32_t)p[0]+p[1]-p[3]-2*(int32_t)p[4]);
            check_velocity(keys.velocity[0].value, expected);
            assert(keys.velocity[0].captures == 2+(n-5)/2);
        }
        assert(keys.velocity[1].captures == 1);
    }
    assert(keys.velocity[0].captures == 4 && keys.velocity[0].valid);
    const uint32_t revision = keys.revision;
    assert(!keyboard_raw_set_all(&keys, 3500, 3500));
    assert(keys.revision == revision && keys.velocity[0].valid);
    assert(keyboard_raw_set_all(&keys, 3000, 3300));
    assert(keys.revision == revision+1);
    for (unsigned i = 0; i < 65; ++i) {
        assert(keys.press[i] == 3000 && keys.release[i] == 3300);
        assert(!keys.velocity[i].valid && !keys.velocity[i].pending && !keys.velocity[i].ready);
    }
    for (unsigned i = 0; i < 65; ++i) values[i] = 3900;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    values[0] = 2900; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].pending);
    keyboard_raw_frame(&keys, values, 65, 3, false);
    assert(!keys.velocity[0].pending && !keys.velocity[0].valid);
    for (unsigned i = 0; i < 10; ++i) keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].captures == 4 && !keys.velocity[0].valid);
    puts("PASS 65 simultaneous fits, independent/release arming, overlapping windows, atomic all-key edits, invalid cancellation");
}

static void velocity_clamp_tests(void)
{
    const uint16_t points[][5] = {
        {2000,2100,2200,2300,2400}, /* negative */
        {3000,3000,3000,3000,3000}, /* zero */
        {3000,3000,3000,2999,3000}, /* smallest positive fit */
        {3000,3000,3000,1876,750},  /* 4499200 */
        {3000,3000,3000,1875,750},  /* exactly 4500000 */
        {3000,3000,3000,1874,750},  /* 4500800 */
        {3500,3500,3000,1000,1}     /* well above maximum */
    };
    for (unsigned k = 0; k < sizeof(points)/sizeof(points[0]); ++k) {
        keyboard_raw_init(&s);
        for (unsigned i = 0; i < 61; ++i) raw[i] = 3900;
        frame(); raw[32] = 3500; frame();
        for (unsigned i = 0; i < 5; ++i) { raw[32] = points[k][i]; frame(); }
        const uint16_t *p = points[k];
        const int32_t expected = 800*(2*(int32_t)p[0]+p[1]-p[3]-2*(int32_t)p[4]);
        assert(s.velocity[32].valid && s.velocity[32].captures == 1);
        check_velocity(s.velocity[32].value, expected);
        if (k < 2) assert(s.velocity[32].value == 0.0f);
        if (k >= 4) assert(s.velocity[32].value == 1.0f);
    }
    puts("PASS normalized float: negative/zero, small positive, below/at/above 4500000");
}

int main(void)
{
    velocity_tests();
    velocity_history_oracle();
    velocity_clamp_tests();
    keyboard_raw_init(&s);
    for (unsigned i = 0; i < 61; ++i) raw[i] = 3900;
    assert(keyboard_key_for_sensor(1, 32) == 0x1f);
    raw[32] = 3500; frame();
    assert(!s.armed && !a()); /* held at startup */
    raw[32] = 3700; frame(); assert(!s.armed);
    raw[32] = 3701; frame(); assert(s.armed && !a());
    raw[32] = 3600; frame(); assert(!a());
    raw[32] = 3599; frame(); assert(a());
    for (unsigned i = 0; i < 100; ++i) {
        raw[32] = i % 2 ? 3600 : 3700; frame(); assert(a());
    }
    raw[32] = 3701; frame(); assert(!a());
    raw[32] = 3650; frame(); assert(!a());

    assert(!keyboard_raw_set(&s, 61, 3000, 3100));
    assert(!keyboard_raw_set(&s, 32, 0, 3100));
    assert(!keyboard_raw_set(&s, 32, 3100, 3100));
    assert(!keyboard_raw_set(&s, 32, 3200, 3100));
    assert(!keyboard_raw_set(&s, 32, 3000, 4097));
    assert(!keyboard_raw_set(&s, 32, 3000, 4096));
    assert(s.revision == 0 && s.armed);
    assert(keyboard_raw_set(&s, 32, 3000, 3200));
    assert(s.revision == 1 && !s.armed && !a());
    frame(); assert(s.armed);
    raw[32] = 2999; frame(); assert(a());
    raw[33] = 3500; frame(); assert(s.down[33] && a());
    raw[32] = 3201; frame(); assert(!a() && s.down[33]);
    raw[33] = 3900; frame();

    /* Every ordinary key/modifier produces its mapped bit, together (NKRO). */
    for (unsigned i = 0; i < 61; ++i) {
        const keyboard_action_t *action = keyboard_action(1, keyboard_key_for_sensor(1, i), 0);
        if (action && action->type == 2) raw[i] = 2000;
    }
    frame();
    for (unsigned i = 0; i < 61; ++i) {
        const keyboard_action_t *action = keyboard_action(1, keyboard_key_for_sensor(1, i), 0);
        if (action && action->type == 2) {
            assert((s.engine.report.modifiers & action->arg0) == action->arg0);
            if (action->arg1 >= 4 && action->arg1 <= 0x73)
                assert(keyboard_report_get_usage(&s.engine.report, action->arg1));
        }
    }
    keyboard_raw_enable(&s, false); frame(); assert(!s.armed && !a());
    keyboard_raw_enable(&s, true); frame(); assert(!s.armed);
    for (unsigned i = 0; i < 61; ++i) raw[i] = 3900;
    frame(); assert(s.armed);
    raw[32] = 2000; frame(); assert(a());
    keyboard_raw_frame(&s, raw, 61, 1, false); assert(!a() && !s.valid && !s.armed);
    frame(); assert(!s.armed);
    raw[32] = 3900; frame(); assert(s.armed);
    raw[0] = 4097; frame(); assert(!s.armed && !s.valid);
    raw[0] = 0; frame(); assert(!s.armed && !s.valid);
    puts("PASS raw Schmitt boundaries, startup/invalid/enable/config guards, per-key settings and NKRO");
    return 0;
}
