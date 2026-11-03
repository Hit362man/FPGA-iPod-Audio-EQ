#include <stdint.h>

#include "address_map.h"

#define NUM_SPLITS 15
#define NUM_BANDS 16

#define GAIN_ONE 256
#define GAIN_STEP 64
#define GAIN_MIN 0
#define GAIN_MAX 1024

#define CONTROL_REPEAT_SAMPLES 180

#define ECHO_DELAY_SAMPLES 8192

#define REVERB_DELAY_A 601
#define REVERB_DELAY_B 1151
#define REVERB_DELAY_C 1913

volatile int* ps2_ptr = (int*)PS2_BASE;
volatile int* led_ptr = (int*)LEDR_BASE;

typedef struct {
  int lp1_prev[NUM_SPLITS];
  int lp2_prev[NUM_SPLITS];
} EqState;

typedef struct {
  int delay[ECHO_DELAY_SAMPLES];
  int index;
} EchoState;

typedef struct {
  int delay_a[REVERB_DELAY_A];
  int delay_b[REVERB_DELAY_B];
  int delay_c[REVERB_DELAY_C];
  int index_a;
  int index_b;
  int index_c;
} ReverbState;

void band_visualizer_add_band_sample(const int* bands);

EqState left_eq;
EqState right_eq;
EqState visual_eq;

EchoState left_echo;
EchoState right_echo;

ReverbState left_reverb;
ReverbState right_reverb;

int band_gains[NUM_BANDS];
int selected_band = 0;

int up_held = 0;
int down_held = 0;

int ps2_break_pending = 0;
int ps2_extended_pending = 0;

int control_repeat_accum = 0;
int eq_active = 0;

int echo_enabled = 0;
int reverb_enabled = 0;

/*
Low to high:
Z X C V B N A S D F G H J K L ;

The split points are denser in the low range so bass and low-mid
changes are easier to hear on rap and similar music.
*/
const int split_coeff[NUM_SPLITS] = {6,  10,  15,  21,  30,  42,  57, 73,
                                     91, 112, 133, 156, 177, 198, 217};

int clamp_gain(int value) {
  if (value < GAIN_MIN) {
    return GAIN_MIN;
  }
  if (value > GAIN_MAX) {
    return GAIN_MAX;
  }
  return value;
}

int clamp_int64_to_int(int64_t value) {
  if (value > 2147483647LL) {
    return 2147483647;
  }
  if (value < -2147483648LL) {
    return -2147483648LL;
  }
  return (int)value;
}

void update_eq_active(void) {
  int i;

  eq_active = 0;
  for (i = 0; i < NUM_BANDS; i++) {
    if (band_gains[i] != GAIN_ONE) {
      eq_active = 1;
      return;
    }
  }
}

void update_leds(void) {
  int leds = 0;

  leds |= (selected_band & 0xF);

  if (echo_enabled) {
    leds |= (1 << 4);
  }
  if (reverb_enabled) {
    leds |= (1 << 5);
  }
  if (eq_active) {
    leds |= (1 << 7);
  }
  if (up_held) {
    leds |= (1 << 8);
  }
  if (down_held) {
    leds |= (1 << 9);
  }

  *led_ptr = leds;
}

void reset_eq_state(void) {
  int i;

  for (i = 0; i < NUM_SPLITS; i++) {
    left_eq.lp1_prev[i] = 0;
    left_eq.lp2_prev[i] = 0;

    right_eq.lp1_prev[i] = 0;
    right_eq.lp2_prev[i] = 0;

    visual_eq.lp1_prev[i] = 0;
    visual_eq.lp2_prev[i] = 0;
  }
}

void reset_eq_gains(void) {
  int i;

  for (i = 0; i < NUM_BANDS; i++) {
    band_gains[i] = GAIN_ONE;
  }

  update_eq_active();
}

void reset_echo_state(EchoState* state) {
  int i;

  for (i = 0; i < ECHO_DELAY_SAMPLES; i++) {
    state->delay[i] = 0;
  }

  state->index = 0;
}

void reset_reverb_state(ReverbState* state) {
  int i;

  for (i = 0; i < REVERB_DELAY_A; i++) {
    state->delay_a[i] = 0;
  }
  for (i = 0; i < REVERB_DELAY_B; i++) {
    state->delay_b[i] = 0;
  }
  for (i = 0; i < REVERB_DELAY_C; i++) {
    state->delay_c[i] = 0;
  }

  state->index_a = 0;
  state->index_b = 0;
  state->index_c = 0;
}

void reset_effect_states(void) {
  reset_echo_state(&left_echo);
  reset_echo_state(&right_echo);

  reset_reverb_state(&left_reverb);
  reset_reverb_state(&right_reverb);
}

void reset_all_audio_controls(void) {
  reset_eq_gains();
  reset_eq_state();
  reset_effect_states();

  echo_enabled = 0;
  reverb_enabled = 0;

  up_held = 0;
  down_held = 0;
  control_repeat_accum = 0;
}

int select_band_from_code(uint8_t code) {
  if (code == 0x1A) return 0;  /* Z */
  if (code == 0x22) return 1;  /* X */
  if (code == 0x21) return 2;  /* C */
  if (code == 0x2A) return 3;  /* V */
  if (code == 0x32) return 4;  /* B */
  if (code == 0x31) return 5;  /* N */
  if (code == 0x1C) return 6;  /* A */
  if (code == 0x1B) return 7;  /* S */
  if (code == 0x23) return 8;  /* D */
  if (code == 0x2B) return 9;  /* F */
  if (code == 0x34) return 10; /* G */
  if (code == 0x33) return 11; /* H */
  if (code == 0x3B) return 12; /* J */
  if (code == 0x42) return 13; /* K */
  if (code == 0x4B) return 14; /* L */
  if (code == 0x4C) return 15; /* ; */

  return -1;
}

void handle_extended_code(uint8_t code) {
  if (code == 0x75) { /* Up arrow */
    if (ps2_break_pending) {
      up_held = 0;
    } else {
      up_held = 1;
    }
  } else if (code == 0x72) { /* Down arrow */
    if (ps2_break_pending) {
      down_held = 0;
    } else {
      down_held = 1;
    }
  }

  ps2_extended_pending = 0;
  ps2_break_pending = 0;
}

void handle_normal_code(uint8_t code) {
  int band_index = select_band_from_code(code);

  if (!ps2_break_pending) {
    if (band_index != -1) {
      selected_band = band_index;

      /* Clear stale arrow state when switching bands. */
      up_held = 0;
      down_held = 0;
      control_repeat_accum = 0;
    } else if (code == 0x24) { /* E */
      echo_enabled = !echo_enabled;

      if (echo_enabled) {
        reset_echo_state(&left_echo);
        reset_echo_state(&right_echo);
      }
    } else if (code == 0x2D) { /* R */
      reverb_enabled = !reverb_enabled;

      if (reverb_enabled) {
        reset_reverb_state(&left_reverb);
        reset_reverb_state(&right_reverb);
      }
    } else if (code == 0x66) { /* Backspace */
      reset_all_audio_controls();
    }
  }

  ps2_break_pending = 0;
}

void handle_ps2_byte(uint8_t byte) {
  if (byte == 0xE0) {
    ps2_extended_pending = 1;
    return;
  }

  if (byte == 0xF0) {
    ps2_break_pending = 1;
    return;
  }

  if (ps2_extended_pending) {
    handle_extended_code(byte);
  } else {
    handle_normal_code(byte);
  }

  update_leds();
}

void iir_poll_ps2(void) {
  int data = *ps2_ptr;

  while (data & 0x8000) {
    uint8_t byte = (uint8_t)(data & 0xFF);
    handle_ps2_byte(byte);
    data = *ps2_ptr;
  }
}

void iir_apply_held_controls(int processed_samples) {
  if (!up_held && !down_held) {
    control_repeat_accum = 0;
    return;
  }

  control_repeat_accum += processed_samples;

  while (control_repeat_accum >= CONTROL_REPEAT_SAMPLES) {
    control_repeat_accum -= CONTROL_REPEAT_SAMPLES;

    if (up_held && !down_held) {
      band_gains[selected_band] =
          clamp_gain(band_gains[selected_band] + GAIN_STEP);
    } else if (down_held && !up_held) {
      band_gains[selected_band] =
          clamp_gain(band_gains[selected_band] - GAIN_STEP);
    }

    update_eq_active();
    update_leds();
  }
}

void split_sample_into_bands(int sample, EqState* state, int bands[NUM_BANDS]) {
  int lowpasses[NUM_SPLITS];
  int i;

  for (i = 0; i < NUM_SPLITS; i++) {
    int diff1 = sample - state->lp1_prev[i];
    state->lp1_prev[i] =
        state->lp1_prev[i] + (int)(((int64_t)diff1 * split_coeff[i]) >> 8);

    int diff2 = state->lp1_prev[i] - state->lp2_prev[i];
    state->lp2_prev[i] =
        state->lp2_prev[i] + (int)(((int64_t)diff2 * split_coeff[i]) >> 8);

    lowpasses[i] = state->lp2_prev[i];
  }

  bands[0] = lowpasses[0];

  for (i = 1; i < NUM_SPLITS; i++) {
    bands[i] = lowpasses[i] - lowpasses[i - 1];
  }

  bands[NUM_BANDS - 1] = sample - lowpasses[NUM_SPLITS - 1];
}

int gain_to_effective_q8(int gain) {
  /*
  Nonlinear gain curve:
  - 0 stays 0, so bottom fully mutes the band
  - values above unity become much more noticeable
  - max user gain 4x becomes max effective gain 16x
  */
  return (int)(((int64_t)gain * (int64_t)gain) >> 8);
}

int combine_bands_with_gain(const int bands[NUM_BANDS]) {
  int i;
  int64_t output = 0;

  for (i = 0; i < NUM_BANDS; i++) {
    int effective_gain = gain_to_effective_q8(band_gains[i]);
    output += (int64_t)effective_gain * (int64_t)bands[i];
  }

  output = output >> 8;

  return clamp_int64_to_int(output);
}

int apply_echo(int sample, EchoState* state) {
  int delayed = state->delay[state->index];
  int64_t output = (int64_t)sample + (((int64_t)delayed * 3) >> 2);
  int64_t stored = (int64_t)sample + (((int64_t)delayed * 3) >> 3);

  state->delay[state->index] = clamp_int64_to_int(stored);
  state->index++;

  if (state->index >= ECHO_DELAY_SAMPLES) {
    state->index = 0;
  }

  return clamp_int64_to_int(output);
}

int apply_reverb(int sample, ReverbState* state) {
  int delayed_a = state->delay_a[state->index_a];
  int delayed_b = state->delay_b[state->index_b];
  int delayed_c = state->delay_c[state->index_c];

  int64_t output = (int64_t)sample + (((int64_t)delayed_a * 3) >> 3) +
                   ((int64_t)delayed_b >> 2) + ((int64_t)delayed_c >> 3);

  state->delay_a[state->index_a] =
      clamp_int64_to_int((int64_t)sample + ((int64_t)delayed_a >> 2));
  state->delay_b[state->index_b] =
      clamp_int64_to_int((int64_t)sample + ((int64_t)delayed_b >> 3));
  state->delay_c[state->index_c] =
      clamp_int64_to_int((int64_t)sample + ((int64_t)delayed_c >> 3));

  state->index_a++;
  state->index_b++;
  state->index_c++;

  if (state->index_a >= REVERB_DELAY_A) {
    state->index_a = 0;
  }
  if (state->index_b >= REVERB_DELAY_B) {
    state->index_b = 0;
  }
  if (state->index_c >= REVERB_DELAY_C) {
    state->index_c = 0;
  }

  return clamp_int64_to_int(output);
}

int apply_channel_effects(int sample, EchoState* echo_state,
                          ReverbState* reverb_state) {
  if (echo_enabled) {
    sample = apply_echo(sample, echo_state);
  }

  if (reverb_enabled) {
    sample = apply_reverb(sample, reverb_state);
  }

  return sample;
}

int iir_process_left(int sample) {
  int bands[NUM_BANDS];
  int output;

  split_sample_into_bands(sample, &left_eq, bands);

  if (!eq_active) {
    output = sample;
  } else {
    output = combine_bands_with_gain(bands);
  }

  return apply_channel_effects(output, &left_echo, &left_reverb);
}

int iir_process_right(int sample) {
  int bands[NUM_BANDS];
  int output;

  split_sample_into_bands(sample, &right_eq, bands);

  if (!eq_active) {
    output = sample;
  } else {
    output = combine_bands_with_gain(bands);
  }

  return apply_channel_effects(output, &right_echo, &right_reverb);
}

void iir_visualize_sample(int sample) {
  int bands[NUM_BANDS];

  split_sample_into_bands(sample, &visual_eq, bands);
  band_visualizer_add_band_sample(bands);
}

void iir_init(void) {
  reset_eq_state();
  reset_eq_gains();
  reset_effect_states();

  selected_band = 0;
  up_held = 0;
  down_held = 0;
  ps2_break_pending = 0;
  ps2_extended_pending = 0;
  control_repeat_accum = 0;

  echo_enabled = 0;
  reverb_enabled = 0;

  update_leds();
}