#include <stdint.h>

#define AUDIO_BASE 0xFF203040
#define UI_UPDATE_SAMPLES 512

void band_visualizer_init(void);
void band_visualizer_update_display(const int* samples);
void vga_ui_init(void);
void vga_ui_draw(void);
int vga_ui_can_draw(void);

void iir_init(void);
void iir_poll_ps2(void);
int iir_process_left(int sample);
int iir_process_right(int sample);
void iir_visualize_sample(int sample);
void iir_apply_held_controls(int processed_samples);

volatile int* audio_ptr = (int*)AUDIO_BASE;

int ui_sample_accum = 0;

void service_audio_io(void) {
  int processed_samples = 0;

  while (1) {
    int fifospace = *(audio_ptr + 1);
    int write_space_left = (fifospace >> 24) & 0xFF;
    int write_space_right = (fifospace >> 16) & 0xFF;
    int read_avail_left = (fifospace >> 8) & 0xFF;
    int read_avail_right = fifospace & 0xFF;

    if (read_avail_left == 0 || read_avail_right == 0) {
      break;
    }
    if (write_space_left == 0 || write_space_right == 0) {
      break;
    }

    {
      int left_sample = *(audio_ptr + 2);
      int right_sample = *(audio_ptr + 3);
      int left_out = iir_process_left(left_sample);
      int right_out = iir_process_right(right_sample);
      int mono_out = (left_out + right_out) / 2;

      *(audio_ptr + 2) = left_out;
      *(audio_ptr + 3) = right_out;

      iir_visualize_sample(mono_out);
    }

    processed_samples++;
  }

  ui_sample_accum += processed_samples;
  iir_apply_held_controls(processed_samples);
}

int main(void) {
  iir_init();
  band_visualizer_init();
  vga_ui_init();

  while (1) {
    iir_poll_ps2();
    service_audio_io();

    if (ui_sample_accum >= UI_UPDATE_SAMPLES) {
      if (vga_ui_can_draw()) {
        ui_sample_accum -= UI_UPDATE_SAMPLES;
        band_visualizer_update_display(0);
        vga_ui_draw();
      }
    }
  }

  return 0;
}