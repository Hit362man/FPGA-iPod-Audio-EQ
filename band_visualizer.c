#include <stdint.h>

#define VISUALIZER_BARS 16

uint8_t display_levels[VISUALIZER_BARS];

int band_centers_hz[VISUALIZER_BARS] = {20,   40,   60,   90,  130, 190,
                                        270,  370,  490,  640, 820, 1060,
                                        1340, 1700, 2150, 3000};

unsigned int band_energy_sum[VISUALIZER_BARS];
int band_energy_count = 0;
unsigned int display_peak = 1;

int abs_int(int value) {
  if (value < 0) {
    if (value == (-2147483647 - 1)) {
      return 2147483647;
    }
    return -value;
  }
  return value;
}

void band_visualizer_init(void) {
  int i;

  for (i = 0; i < VISUALIZER_BARS; i++) {
    display_levels[i] = 0;
    band_energy_sum[i] = 0;
  }

  band_energy_count = 0;
  display_peak = 1;
}

void band_visualizer_add_band_sample(const int* bands) {
  int i;

  for (i = 0; i < VISUALIZER_BARS; i++) {
    band_energy_sum[i] += (unsigned int)abs_int(bands[i]);
  }

  band_energy_count++;
}

void band_visualizer_update_display(const int* samples) {
  int i;
  unsigned int frame_max = 1;

  (void)samples;

  if (band_energy_count == 0) {
    return;
  }

  for (i = 0; i < VISUALIZER_BARS; i++) {
    unsigned int average_energy =
        band_energy_sum[i] / (unsigned int)band_energy_count;

    if (average_energy > frame_max) {
      frame_max = average_energy;
    }
  }

  if (frame_max > display_peak) {
    display_peak = frame_max;
  } else {
    display_peak = (display_peak * 15u + frame_max) / 16u;
  }

  if (display_peak == 0) {
    display_peak = 1;
  }

  for (i = 0; i < VISUALIZER_BARS; i++) {
    unsigned int average_energy =
        band_energy_sum[i] / (unsigned int)band_energy_count;
    unsigned int target_level;

    /*
     * Small leakage between nearby bands is expected in this kind of IIR split.
     * This gate hides tiny bleed on the VGA so the plot matches what you want
     * to see more closely.
     */
    if (average_energy * 10u < frame_max) {
      average_energy = 0;
    }

    target_level = (average_energy * 255u) / display_peak;

    if (target_level > 255u) {
      target_level = 255u;
    }

    display_levels[i] = (uint8_t)((display_levels[i] * 5u + target_level) / 6u);

    band_energy_sum[i] = 0;
  }

  band_energy_count = 0;
}

const uint8_t* band_visualizer_get_display_levels(void) {
  return display_levels;
}

int band_visualizer_get_bar_count(void) { return VISUALIZER_BARS; }

int band_visualizer_get_frequency_for_bar(int bar_index) {
  if (bar_index < 0) {
    bar_index = 0;
  }
  if (bar_index >= VISUALIZER_BARS) {
    bar_index = VISUALIZER_BARS - 1;
  }

  return band_centers_hz[bar_index];
}
