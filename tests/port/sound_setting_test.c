/* Checks the sound driver's command that sets one of the voices' settings by
 * hand ($FD, game/sound/driver.c) against the native mixer: each side's
 * volume and each byte of the pitch keeping the rest, the envelope's two
 * halves, both kinds of gain, the mix's volumes and keys, the switches, the
 * echo's buffer and one tap of its filter, a setting that takes nothing, and
 * the voice taken from the setting's number. Exits non-zero on failure.
 *
 *   sound_setting_test */
#include <stdio.h>
#include <string.h>

#include "platform/native/mixer.h"
#include "sound/driver.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static Mixer mixer;
static SoundDriver driver;

static void set(uint8_t setting, uint8_t value)
{
    sound_run_command(&driver, (SoundCommand){ 0xFD, setting, value, 0 });
}

int main(void)
{
    mixer_init(&mixer);
    driver.voices = mixer_voices(&mixer);
    MixerVoice *v3 = &mixer.voice[3];

    au_voice_volume(driver.voices, 3, 10, 20);
    set(0x30, 0x90);
    CHECK(v3->volume[0] == -0x70 && v3->volume[1] == 20, "left volume %d %d", v3->volume[0], v3->volume[1]);
    set(0x31, 0x05);
    CHECK(v3->volume[0] == -0x70 && v3->volume[1] == 5, "right volume %d %d", v3->volume[0], v3->volume[1]);

    au_voice_pitch(driver.voices, 3, 0x1234);
    set(0x32, 0xAB);
    CHECK(v3->pitch == 0x12AB, "pitch low %04X", v3->pitch);
    set(0x33, 0x05);
    CHECK(v3->pitch == 0x05AB, "pitch high %04X", v3->pitch);

    set(0x34, 0x2A);
    CHECK(v3->sample == 0x2A, "sample %02X", v3->sample);

    au_voice_envelope(driver.voices, 3, (AuEnvelope){ false, 1, 2, 3, 4 });
    set(0x35, 0xDE);
    CHECK(v3->adsr == 1 && v3->decay == 5 && v3->attack == 14 && v3->sustain_level == 3 && v3->sustain_rate == 4,
          "envelope rates %d %d %d %d %d", v3->adsr, v3->decay, v3->attack, v3->sustain_level, v3->sustain_rate);
    set(0x36, 0xB7);
    CHECK(v3->adsr == 1 && v3->decay == 5 && v3->attack == 14 && v3->sustain_level == 5 && v3->sustain_rate == 0x17,
          "envelope sustain %d %d", v3->sustain_level, v3->sustain_rate);

    set(0x37, 0x45);
    CHECK(v3->gain_mode == AU_GAIN_DIRECT && v3->gain_value == 0x45, "direct gain %d %02X", v3->gain_mode, v3->gain_value);
    set(0x37, 0xC9);
    CHECK(v3->gain_mode == AU_GAIN_INCREASE_LINEAR && v3->gain_value == 0x09, "sliding gain %d %02X", v3->gain_mode,
          v3->gain_value);

    /* the voice is the number's high bits less the top one */
    set(0xB4, 0x11);
    CHECK(v3->sample == 0x11, "voice from a high number %02X", v3->sample);

    au_master_volume(driver.voices, 1, 2);
    set(0x1C, 0x7F);
    CHECK(mixer.master[0] == 1 && mixer.master[1] == 0x7F, "master %d %d", mixer.master[0], mixer.master[1]);
    au_echo_volume(driver.voices, 3, 4);
    set(0x2C, 0x40);
    CHECK(mixer.echo_volume[0] == 0x40 && mixer.echo_volume[1] == 4, "echo volume %d %d", mixer.echo_volume[0],
          mixer.echo_volume[1]);
    set(0x4C, 0x81);
    CHECK(mixer.keys_asked == 0x81, "keys on %02X", mixer.keys_asked);
    set(0x5C, 0x02);
    CHECK(mixer.keys_released == 0x02, "keys off %02X", mixer.keys_released);

    set(0x6C, 0xA7);
    CHECK(mixer.resetting == 1 && mixer.silenced == 0 && mixer.echo_writes == 0 && mixer.noise_clock == 7,
          "switches %d %d %d %d", mixer.resetting, mixer.silenced, mixer.echo_writes, mixer.noise_clock);
    set(0x6C, 0x40);
    CHECK(mixer.resetting == 0 && mixer.silenced == 1 && mixer.echo_writes == 1 && mixer.noise_clock == 0,
          "switches again %d %d %d %d", mixer.resetting, mixer.silenced, mixer.echo_writes, mixer.noise_clock);

    set(0x0D, 0x90);
    set(0x2D, 0x0E);
    set(0x3D, 0x30);
    set(0x4D, 0xC0);
    CHECK(mixer.echo_feedback == -0x70 && mixer.modulated_voices == 0x0E && mixer.noise_voices == 0x30 &&
          mixer.echo_voices == 0xC0, "sends %d %02X %02X %02X", mixer.echo_feedback, mixer.modulated_voices,
          mixer.noise_voices, mixer.echo_voices);

    au_echo_delay(driver.voices, 2);
    uint8_t page = mixer.echo_page;
    set(0x7D, 0x05);
    CHECK(mixer.echo_delay == 5 && mixer.echo_page == page, "delay alone %d %02X", mixer.echo_delay, mixer.echo_page);
    set(0x6D, 0xC0);
    CHECK(mixer.echo_delay == 5 && mixer.echo_page == 0xC0, "page alone %d %02X", mixer.echo_delay, mixer.echo_page);

    const int8_t taps[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    au_echo_filter(driver.voices, taps);
    set(0x5F, 0xF0);
    CHECK(mixer.taps[5] == -0x10 && mixer.taps[4] == 5 && mixer.taps[6] == 7, "tap %d", mixer.taps[5]);

    /* what the chip only reports, and the directory's place, take nothing */
    static Mixer before;
    before = mixer;
    set(0x38, 0x55);
    set(0x5D, 0x55);
    set(0x7C, 0x55);
    set(0x1E, 0x55);
    CHECK(!memcmp(&before, &mixer, sizeof mixer), "a setting that takes nothing changed the mixer");

    if (failures)
        return 1;
    printf("sound_setting_test: ok\n");
    return 0;
}
