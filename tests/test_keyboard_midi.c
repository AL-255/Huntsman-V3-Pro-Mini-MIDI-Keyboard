#include "keyboard_midi.h"
#include "keyboard_layout.h"
#include "travel_lighting.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static keyboard_raw_t raw;
static keyboard_midi_t midi;
static uint16_t values[65], lower[65], upper[65];
static uint8_t log_events[20000][4];
static unsigned logged, frames;
static bool blocked;
static bool send_event(uint8_t cin, uint8_t status, uint8_t note, uint8_t value)
{
    if (blocked) return false;
    assert(logged < 20000 && note < 128 && value < 128 && cin == status >> 4);
    uint8_t *p = log_events[logged++]; p[0]=cin; p[1]=status; p[2]=note; p[3]=value;
    return true;
}
static unsigned sensor(uint8_t usage, uint8_t modifier)
{
    for (unsigned i=0; i<raw.count; ++i) {
        const uint8_t key = keyboard_key_for_sensor(raw.profile,i);
        const keyboard_action_t *a = keyboard_action(raw.profile,key,0);
        if (a && key != KEY_ID_FN && a->type==2 && a->arg0==modifier && a->arg1==usage) return i;
    }
    assert(false); return 0;
}
static unsigned fn_sensor(void)
{
    for (unsigned i=0; i<raw.count; ++i)
        if (keyboard_key_for_sensor(raw.profile,i)==KEY_ID_FN) return i;
    assert(false); return 0;
}
static void step(void)
{
    keyboard_raw_frame(&raw,values,raw.count ? raw.count : 61,raw.profile ? raw.profile : 1,true);
    keyboard_midi_frame(&midi,&raw,lower,upper,frames++/8);
}
static void drain(void)
{
    for (unsigned i=0; i<300; ++i) keyboard_midi_service(&midi,frames/8,send_event);
}
static void init(void)
{
    keyboard_raw_init(&raw); keyboard_midi_init(&midi);
    logged=frames=0; blocked=false;
    for (unsigned i=0;i<65;++i) { values[i]=3900; lower[i]=1000; upper[i]=3900; }
    step(); assert(raw.armed && !midi.mode);
}
static void toggle(void)
{
    const unsigned fn=fn_sensor(), ent=sensor(0x28,0);
    const unsigned mode=midi.mode;
    values[fn]=values[ent]=3500; step();
    assert(midi.mode==(mode^1) && !raw.armed);
    for (unsigned i=0; i<20; ++i) step();
    assert(midi.mode==(mode^1)); /* holding the chord never repeats */
    values[fn]=values[ent]=3900;
    drain(); step(); logged=0;
}
static unsigned events(unsigned status,unsigned note)
{
    unsigned n=0;
    for (unsigned i=0;i<logged;++i) if (log_events[i][1]==status && log_events[i][2]==note) ++n;
    return n;
}
static void press_fit(unsigned i)
{
    values[i]=3500; step();
    for (unsigned j=1;j<=5;++j) { values[i]=3500-j*100; step(); }
}
static void default_mapping(void)
{
    init();
    static const uint8_t expected[][2]={{0x2b,72},{0x14,74},{0x1a,76},{8,77},{0x15,79},
        {0x17,81},{0x1c,83},{0x18,84},{0x0c,86},{0x12,88},{0x13,89},{0x2f,91},
        {0x30,93},{0x31,95},{0x1e,73},{0x1f,75},{0x21,78},{0x22,80},{0x23,82},
        {0x25,85},{0x26,87},{0x2d,90},{0x2e,92},{0x2a,94},
        {4,61},{0x1d,62},{0x16,63},{0x1b,64},{6,65},{9,66},{0x19,67},{0x0a,68},
        {5,69},{0x0b,70},{0x11,71},{0x10,72},{0x0e,73},{0x36,74},{0x0f,75},
        {0x37,76},{0x38,77},{0x34,78}};
    unsigned mapped=0;
    for (unsigned i=0;i<61;++i) mapped += midi.mapping[i]!=255;
    assert(mapped==43);
    for (unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i)
        assert(midi.mapping[sensor(expected[i][0],0)]==expected[i][1]);
    assert(midi.mapping[sensor(0,2)]==60);
    assert(!keyboard_midi_map(&midi,&raw,fn_sensor(),40));
    assert(!keyboard_midi_map(&midi,&raw,sensor(0,1),40));
    assert(!keyboard_midi_map(&midi,&raw,sensor(0,4),40));
    assert(!keyboard_midi_map(&midi,&raw,65,40));
    assert(!keyboard_midi_map(&midi,&raw,0,128));
}
static void velocity_pressure_and_modes(void)
{
    init(); const unsigned tab=sensor(0x2b,0);
    press_fit(tab); drain(); assert(!logged); /* standard keyboard, no MIDI */
    assert(keyboard_report_get_usage(&raw.engine.report,0x2b));
    values[tab]=3900; step(); toggle();
    press_fit(tab); drain(); assert(events(0x90,72)==1);
    assert(log_events[0][3]==23); /* 800000 / 4500000 * 127 rounded */
    for(unsigned i=0;i<80;++i) step();
    drain();
    assert(events(0xa0,72)==1);
    values[tab]=1000; step(); frames+=80; drain();
    assert(log_events[logged-1][1]==0xa0 && log_events[logged-1][3]==127);
    values[tab]=3700; step(); drain(); assert(!events(0x80,72));
    values[tab]=3701; step(); drain(); assert(events(0x80,72)==1);
    press_fit(tab); drain();
    toggle(); assert(!midi.mode && !raw.midi_mode);
    values[tab]=3900; step(); assert(raw.armed);
    uint8_t rgb[LIGHTING_FRAME_SIZE]={0};
    keyboard_midi_lights(&midi,rgb,midi.changed_at);
    const lighting_channels_t *c=&g_lighting_channels[0][tab];
    assert(rgb[c->green]==128 && rgb[c->blue]==0);
}
static void short_taps_and_overlap(void)
{
    init(); toggle(); const unsigned tab=sensor(0x2b,0);
    for(unsigned i=0;i<6;++i) { values[tab]=i%2 ? 3900 : 3500; step(); }
    for(unsigned i=0;i<5;++i) step();
    drain(); assert(events(0x90,72)==3 && events(0x80,72)==3);
    for(unsigned i=0;i<logged;++i) if(log_events[i][1]==0x90) assert(log_events[i][3]>=1);
}
static void shift_and_filtered_strike(void)
{
    init(); const unsigned shift=sensor(0,2);
    values[shift]=3500; step();
    assert(raw.engine.report.modifiers==2);
    values[shift]=3900; step(); toggle();
    values[shift]=3500; step();
    const uint16_t points[]={3400,3300,2300,2200,2100};
    for(unsigned i=0;i<5;++i) {values[shift]=points[i]; step();}
    drain(); assert(events(0x90,60)==1 && log_events[0][3]==23);
    assert(raw.engine.report.modifiers==0);
    values[shift]=3900; step(); drain(); assert(events(0x80,60)==1);
}
static void octave_and_duplicates(void)
{
    init(); toggle(); unsigned tab=sensor(0x2b,0), q=sensor(0x14,0), up=sensor(0,4);
    press_fit(tab); drain();
    values[up]=3500; step(); for(unsigned i=0;i<10;++i) step();
    assert(midi.octave==1); values[up]=3900; step();
    values[tab]=3900; step(); drain(); assert(events(0x80,72)==1 && !events(0x80,84));
    press_fit(tab); drain(); assert(events(0x90,84)==1);
    assert(keyboard_midi_map(&midi,&raw,q,72)); drain(); values[tab]=3900; step(); logged=0;
    press_fit(tab); press_fit(q); drain(); assert(events(0x90,84)==1);
    values[tab]=3900; step(); drain(); assert(!events(0x80,84));
    values[q]=3900; step(); drain(); assert(events(0x80,84)==1);
    midi.octave=-7; logged=0; press_fit(tab); drain(); assert(!logged);
}
static void faults_and_backpressure(void)
{
    init(); toggle(); unsigned tab=sensor(0x2b,0);
    press_fit(tab); blocked=true; drain(); assert(!logged && midi.count==1);
    values[tab]=3900; step(); drain(); assert(midi.count==2);
    blocked=false; drain(); assert(events(0x90,72)==1 && events(0x80,72)==1);
    logged=0; blocked=true;
    for(unsigned i=0;i<70 && !midi.errors;++i) {
        press_fit(tab); values[tab]=3900; step();
    }
    assert(midi.errors==1 && midi.panic && !midi.count && !midi.refs[72]);
    /* Busy MIDI must still allow the mode chord and normal HID recovery. */
    values[tab]=3900; step();
    values[fn_sensor()]=values[sensor(0x28,0)]=3500; step(); assert(!midi.mode);
    blocked=false; drain(); assert(!midi.panic && events(0xb0,120)==1 && events(0xb0,123)==1);
    assert(events(0x80,72)==1);
    for(unsigned i=0;i<65;++i) values[i]=3900;
    step(); toggle(); press_fit(tab); drain();
    keyboard_raw_invalidate(&raw); keyboard_midi_guard(&midi,&raw); drain();
    assert(!midi.refs[72] && !midi.count);
}
static void polyphony(void)
{
    for (unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3 ? 65 : 60+profile;
        step(); drain(); toggle();
        unsigned voices=0;
        bool play[65]={0};
        for (unsigned i=0;i<raw.count;++i) {
            play[i]=midi.role[i]==0;
            if (play[i]) { midi.mapping[i]=i; values[i]=3500; ++voices; }
        }
        step();
        for(unsigned j=1;j<=5;++j) {
            for(unsigned i=0;i<raw.count;++i) if(play[i]) values[i]=3500-j*(100+i);
            step();
        }
        drain();
        unsigned ons=0;
        for(unsigned i=0;i<logged;++i) if(log_events[i][1]==0x90) {
            const unsigned note=log_events[i][2];
            assert(play[note] && log_events[i][3]==(unsigned)((100+note)*8000.0f/4500000.0f*127.0f+0.5f));
            ++ons;
        }
        assert(ons==voices);
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        step(); drain();
        for(unsigned i=0;i<raw.count;++i) if(play[i]) assert(events(0x80,i)==1);
        assert(!midi.errors);
    }
}
static void octave_lights(void)
{
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3 ? 65 : 60+profile;
        step(); drain();
        midi.mode=1; /* isolate overlay from the transient mode-change pulses */
        const unsigned down=sensor(0,1), up=sensor(0,4);
        uint8_t rgb[LIGHTING_FRAME_SIZE];
        for(int shift=-10;shift<=10;++shift) {
            midi.octave=shift;
            const unsigned magnitude=shift<0 ? -shift : shift;
            const unsigned half=60*(11-magnitude);
            for(unsigned phase=0;phase<2;++phase) {
                memset(rgb,7,sizeof(rgb));
                keyboard_midi_lights(&midi,rgb,half*phase);
                for(unsigned i=0;i<raw.count;++i) {
                    const lighting_channels_t *c=&g_lighting_channels[profile-1][i];
                    const unsigned offset=c->controller*192u;
                    if(i==(shift<0 ? down : up) && shift) {
                        assert(rgb[offset+c->red]==(phase ? 0 : 128));
                        assert(rgb[offset+c->green]==(phase ? 0 : 48));
                        assert(rgb[offset+c->blue]==0);
                    } else if(midi.role[i]!=2) { /* Enter retains mode marker */
                        assert(rgb[offset+c->red]==7 && rgb[offset+c->green]==7 && rgb[offset+c->blue]==7);
                    }
                }
            }
        }
        midi.mode=0; midi.octave=-3;
        memset(rgb,7,sizeof(rgb)); keyboard_midi_lights(&midi,rgb,0);
        const lighting_channels_t *c=&g_lighting_channels[profile-1][down];
        assert(rgb[c->controller*192u+c->red]==7); /* keyboard mode: no octave overlay */
        midi.mode=1; midi.changes=1; midi.changed_at=0;
        keyboard_midi_lights(&midi,rgb,0);
        assert(rgb[c->controller*192u+c->red]==0 && rgb[c->controller*192u+c->blue]==128);
    }
    puts("PASS octave LEDs: both signs/all magnitudes/all layouts, zero/keyboard inactive, mode pulse priority");
}
int main(void)
{
    default_mapping(); velocity_pressure_and_modes(); short_taps_and_overlap();
    octave_and_duplicates(); faults_and_backpressure(); polyphony(); shift_and_filtered_strike(); octave_lights();
    printf("MIDI tests passed; controller state %zu bytes\n",sizeof(keyboard_midi_t));
    return 0;
}
