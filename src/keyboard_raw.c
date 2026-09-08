#include "keyboard_raw.h"
#include "keyboard_layout.h"
#include "keyboard_menu.h"
#include <string.h>

void keyboard_raw_invalidate(keyboard_raw_t *s)
{
    s->armed = s->valid = false;
    memset(s->down, 0, sizeof(s->down));
    for (unsigned i = 0; i < RAW_KEY_COUNT; ++i) {
        keyboard_velocity_t *v = &s->velocity[i];
        v->ready = v->valid = false;
        v->pending = v->write = 0u;
        v->value = 0;
        /* Preserve the completion counter across configuration/faults. Five
         * NEW valid samples must arrive before any new result can complete. */
    }
    const keyboard_config_t saved=s->engine.config;
    keyboard_engine_init(&s->engine, s->profile);
    if (saved.profile==s->profile && saved.saved_actuation>=1u && saved.saved_actuation<=10u) {
        s->engine.config.saved_actuation=s->engine.config.actuation=saved.saved_actuation;
        s->engine.config.saved_rapid=s->engine.config.rapid=saved.saved_rapid;
        s->engine.config.rapid_enabled=saved.rapid_enabled;
        s->engine.config.locked=saved.locked;
        s->engine.config.revision=saved.revision;
    }
}

void keyboard_raw_init(keyboard_raw_t *s)
{
    memset(s, 0, sizeof(*s));
    for (unsigned i = 0; i < RAW_KEY_COUNT; ++i) {
        s->press[i] = RAW_DEFAULT_PRESS;
        s->release[i] = RAW_DEFAULT_RELEASE;
    }
    s->enabled = true;
    keyboard_raw_invalidate(s);
}

void keyboard_raw_enable(keyboard_raw_t *s, bool enabled)
{
    s->enabled = enabled;
    keyboard_raw_invalidate(s);
}

bool keyboard_raw_set(keyboard_raw_t *s, unsigned index, unsigned press, unsigned release)
{
    /* 4096 is the largest valid sample: a release threshold of 4096 could
     * never be exceeded and would prevent neutral arming forever. */
    if (index >= s->count || !press || press >= release || release >= 4096u) return false;
    s->press[index] = (uint16_t)press;
    s->release[index] = (uint16_t)release;
    ++s->revision;
    /* Config edits must not create an unrequested down edge or leave a stuck
     * host key. Require a fresh neutral frame before reporting again. */
    keyboard_raw_invalidate(s);
    return true;
}

bool keyboard_raw_set_all(keyboard_raw_t *s, unsigned press, unsigned release)
{
    if (!s->count || !press || press >= release || release >= 4096u) return false;
    for (unsigned i = 0; i < s->count; ++i) {
        s->press[i] = (uint16_t)press;
        s->release[i] = (uint16_t)release;
    }
    ++s->revision;
    keyboard_raw_invalidate(s); /* one atomic main-loop configuration change */
    return true;
}

static void velocity_frame(keyboard_velocity_t *v, uint16_t raw, bool trigger, bool released)
{
    v->window[v->write] = raw;
    v->write = (v->write + 1u) % 5u;
    if (v->pending & 16u) {
        /* The triggering sample is excluded. At trigger+5 the rolling window
         * contains exactly samples +1..+5, oldest at the next write slot.
         * Four signed intervals: decreasing ADC means positive velocity.
         * Discard one furthest from the median (earliest wins ties), then
         * average the other three. Keep fractions until float normalization. */
        const unsigned w = v->write;
        int32_t delta[4], sorted[4], sum = 0;
        for (unsigned i = 0; i < 4; ++i) {
            delta[i] = (int32_t)v->window[(w+i)%5u] - v->window[(w+i+1u)%5u];
            sorted[i] = delta[i]; sum += delta[i];
        }
        for (unsigned i = 1; i < 4; ++i) {
            const int32_t item = sorted[i];
            unsigned j = i;
            while (j && sorted[j-1] > item) { sorted[j] = sorted[j-1]; --j; }
            sorted[j] = item;
        }
        const int32_t twice_median = sorted[1] + sorted[2];
        unsigned outlier = 0;
        int32_t largest = -1;
        for (unsigned i = 0; i < 4; ++i) {
            int32_t distance = 2 * delta[i] - twice_median;
            if (distance < 0) distance = -distance;
            if (distance > largest) { largest = distance; outlier = i; }
        }
        const float raw_velocity = (float)(sum - delta[outlier]) * (8000.0f / 3.0f);
        v->value = raw_velocity <= 0 ? 0.0f : raw_velocity >= 4500000 ? 1.0f
                   : raw_velocity / 4500000.0f;
        ++v->captures;
        v->valid = true;
    }
    v->pending = (v->pending << 1u) & 31u;
    if (released) v->ready = true;
    if (trigger && v->ready) {
        v->pending |= 1u;
        v->ready = false;
    }
    /* Release can rearm before an earlier fit completes. The five-bit delay
     * line retains EVERY pending trigger; overlapping windows do not cancel
     * each other. At most one trigger per key per scan, so it cannot overflow.
     * This runs while host HID is disabled too, for safe GUI tuning. */
}

void keyboard_raw_frame(keyboard_raw_t *s, const uint16_t *raw, uint8_t count,
                        uint8_t profile, bool valid)
{
    if (profile < 1u || profile > 3u || count != (profile == 3u ? 65u : 60u + profile)) {
        keyboard_raw_invalidate(s); return;
    }
    if (s->profile != profile || s->count != count) {
        s->profile = profile; s->count = count;
        keyboard_raw_invalidate(s);
    }
    bool neutral = true;
    for (unsigned i = 0; i < count; ++i) {
        s->raw[i] = raw[i];
        if (!raw[i] || raw[i] > 4096u) valid = false;
        if (raw[i] <= s->release[i]) neutral = false;
    }
    if (!valid) { keyboard_raw_invalidate(s); return; }
    s->valid = true;
    if (!s->armed && s->enabled && neutral) {
        keyboard_engine_release_all(&s->engine);
        memset(s->down, 0, sizeof(s->down));
        s->armed = true;
    }
    bool changed[RAW_KEY_COUNT]={false};
    unsigned fn=RAW_KEY_COUNT;
    for (unsigned i = 0; i < count; ++i) {
        const bool next = s->down[i] ? raw[i] <= s->release[i] : raw[i] < s->press[i];
        velocity_frame(&s->velocity[i], raw[i], next && !s->down[i], raw[i] > s->release[i]);
        if (next == s->down[i]) continue;
        s->down[i] = next;
        changed[i]=true;
        if (keyboard_key_for_sensor(profile,i)==KEY_ID_FN) fn=i;
    }
    if (s->armed && !s->midi_mode) {
        bool (*event)(keyboard_engine_t *,uint8_t,bool)=s->menu_managed ?
            keyboard_application_event : keyboard_engine_event;
        /* Resolve simultaneous chords independently of ASIC sensor order. */
        if (fn<count) (void)event(&s->engine,KEY_ID_FN,s->down[fn]);
        for (unsigned i=0; i<count; ++i)
            if (changed[i] && i!=fn && !(s->menu_managed && !s->engine.config.mode &&
                s->engine.config.fn && keyboard_menu_control(profile,keyboard_key_for_sensor(profile,i))))
                (void)event(&s->engine,keyboard_key_for_sensor(profile,i),s->down[i]);
    }
}
