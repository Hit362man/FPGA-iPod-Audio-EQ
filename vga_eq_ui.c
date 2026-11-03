#include <stdint.h>

#include "address_map.h"

#define VGA_WIDTH 320
#define VGA_HEIGHT 240
#define CHAR_COLUMNS 80
#define CHAR_ROWS 60

#define NUM_BANDS 16
#define GAIN_ONE 256
#define GAIN_MIN 0
#define GAIN_MAX 1024

#define GRAPH_X0 10
#define GRAPH_X1 309
#define GRAPH_Y0 18
#define GRAPH_Y1 112

#define CONTROL_X0 10
#define CONTROL_X1 309
#define CONTROL_Y0 128
#define CONTROL_Y1 214

#define SLIDER_TOP 140
#define SLIDER_BOTTOM 194
#define SLIDER_KNOB_HALF_WIDTH 2

#define PIXEL_STRIDE_HALFWORDS 512
#define FRONT_PIXEL_BUFFER_BASE FPGA_PIXEL_BUF_BASE
#define BACK_PIXEL_BUFFER_BASE (SDRAM_BASE + 0x02000000)

#define COLOR_BG 0x0841
#define COLOR_GRID 0x18C3
#define COLOR_AXIS 0x4228
#define COLOR_BAR 0x7BEF
#define COLOR_SELECTED 0xFFE0
#define COLOR_NORMAL 0x07FF
#define COLOR_KNOB 0xF81F
#define COLOR_PANEL 0x1062

extern int band_gains[NUM_BANDS];
extern int selected_band;

const uint8_t* band_visualizer_get_display_levels(void);
int band_visualizer_get_bar_count(void);
int band_visualizer_get_frequency_for_bar(int bar_index);

volatile int* pixel_ctrl_ptr = (volatile int*)PIXEL_BUF_CTRL_BASE;
volatile uint16_t* pixel_buffer = (volatile uint16_t*)FRONT_PIXEL_BUFFER_BASE;
volatile char* char_buffer = (volatile char*)FPGA_CHAR_BASE;

int last_levels[NUM_BANDS];
int last_band_gains[NUM_BANDS];
int last_selected_band = -1;
int info_selected_band = -1;
int info_effective_gain_percent = -1;

const char band_labels[NUM_BANDS] = {'Z', 'X', 'C', 'V', 'B', 'N', 'A', 'S',
                                     'D', 'F', 'G', 'H', 'J', 'K', 'L', ';'};

const char* band_freq_text[NUM_BANDS] = {
    "20",  "40",  "60",  "90",  "130", "190", "270", "370",
    "490", "640", "820", "1k1", "1k3", "1k7", "2k1", "3k0"};

int clamp_value(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

int text_length(const char* text) {
  int len = 0;

  while (text[len] != '\0') {
    len++;
  }

  return len;
}

void select_pixel_buffer(uint32_t base_address) {
  pixel_buffer = (volatile uint16_t*)(uintptr_t)base_address;
}

void fill_rect(int x0, int y0, int x1, int y1, uint16_t color) {
  int x;
  int y;

  if (x0 > x1) {
    int temp = x0;
    x0 = x1;
    x1 = temp;
  }
  if (y0 > y1) {
    int temp = y0;
    y0 = y1;
    y1 = temp;
  }

  x0 = clamp_value(x0, 0, VGA_WIDTH - 1);
  x1 = clamp_value(x1, 0, VGA_WIDTH - 1);
  y0 = clamp_value(y0, 0, VGA_HEIGHT - 1);
  y1 = clamp_value(y1, 0, VGA_HEIGHT - 1);

  for (y = y0; y <= y1; y++) {
    volatile uint16_t* row_ptr = pixel_buffer + y * PIXEL_STRIDE_HALFWORDS;

    for (x = x0; x <= x1; x++) {
      row_ptr[x] = color;
    }
  }
}

void draw_hline(int x0, int x1, int y, uint16_t color) {
  int x;
  volatile uint16_t* row_ptr;

  if (y < 0 || y >= VGA_HEIGHT) {
    return;
  }

  if (x0 > x1) {
    int temp = x0;
    x0 = x1;
    x1 = temp;
  }

  x0 = clamp_value(x0, 0, VGA_WIDTH - 1);
  x1 = clamp_value(x1, 0, VGA_WIDTH - 1);

  row_ptr = pixel_buffer + y * PIXEL_STRIDE_HALFWORDS;
  for (x = x0; x <= x1; x++) {
    row_ptr[x] = color;
  }
}

void draw_vline(int x, int y0, int y1, uint16_t color) {
  int y;

  if (x < 0 || x >= VGA_WIDTH) {
    return;
  }

  if (y0 > y1) {
    int temp = y0;
    y0 = y1;
    y1 = temp;
  }

  y0 = clamp_value(y0, 0, VGA_HEIGHT - 1);
  y1 = clamp_value(y1, 0, VGA_HEIGHT - 1);

  for (y = y0; y <= y1; y++) {
    pixel_buffer[y * PIXEL_STRIDE_HALFWORDS + x] = color;
  }
}

void draw_rect_outline(int x0, int y0, int x1, int y1, uint16_t color) {
  draw_hline(x0, x1, y0, color);
  draw_hline(x0, x1, y1, color);
  draw_vline(x0, y0, y1, color);
  draw_vline(x1, y0, y1, color);
}

void put_char_xy(int col, int row, char c) {
  if (col < 0 || col >= CHAR_COLUMNS || row < 0 || row >= CHAR_ROWS) {
    return;
  }

  char_buffer[(row << 7) + col] = c;
}

void put_text_xy(int col, int row, const char* text) {
  while (*text != '\0') {
    put_char_xy(col, row, *text);
    col++;
    text++;
  }
}

void clear_text_span(int col, int row, int length) {
  int i;

  for (i = 0; i < length; i++) {
    put_char_xy(col + i, row, ' ');
  }
}

void put_small_number(int col, int row, int value) {
  char digits[12];
  int count = 0;
  int i;

  if (value == 0) {
    put_char_xy(col, row, '0');
    return;
  }

  if (value < 0) {
    put_char_xy(col, row, '-');
    col++;
    value = -value;
  }

  while (value > 0 && count < 11) {
    digits[count] = (char)('0' + (value % 10));
    value /= 10;
    count++;
  }

  for (i = count - 1; i >= 0; i--) {
    put_char_xy(col, row, digits[i]);
    col++;
  }
}

int gain_to_slider_y(int gain) {
  int travel = SLIDER_BOTTOM - SLIDER_TOP;
  int y = SLIDER_BOTTOM - ((gain - GAIN_MIN) * travel) / (GAIN_MAX - GAIN_MIN);

  return clamp_value(y, SLIDER_TOP, SLIDER_BOTTOM);
}

int band_x_center(int band_index) {
  return CONTROL_X0 + 12 +
         (band_index * (CONTROL_X1 - CONTROL_X0 - 24)) / (NUM_BANDS - 1);
}

int gain_to_effective_percent(int gain) {
  int64_t effective_q8 = ((int64_t)gain * (int64_t)gain) >> 8;
  return (int)((effective_q8 * 100LL) / GAIN_ONE);
}

void wait_for_vsync_blocking(void) {
  *pixel_ctrl_ptr = 1;

  while ((*(pixel_ctrl_ptr + 3) & 0x01) != 0) {
  }
}

void clear_char_buffer(void) {
  int x;
  int y;

  for (y = 0; y < CHAR_ROWS; y++) {
    for (x = 0; x < CHAR_COLUMNS; x++) {
      char_buffer[(y << 7) + x] = ' ';
    }
  }
}

void draw_band_legend_text(void) {
  int i;

  for (i = 0; i < NUM_BANDS; i++) {
    int base_col = i * 5;
    int freq_len = text_length(band_freq_text[i]);
    int freq_col = base_col + (5 - freq_len) / 2;

    put_char_xy(base_col + 2, 50, band_labels[i]);
    put_text_xy(freq_col, 52, band_freq_text[i]);
  }

  put_text_xy(0, 54, "keys");
  put_text_xy(0, 56, "freq");
}

void draw_static_background(void) {
  int x;
  int y;

  fill_rect(0, 0, VGA_WIDTH - 1, VGA_HEIGHT - 1, COLOR_BG);

  draw_rect_outline(GRAPH_X0, GRAPH_Y0, GRAPH_X1, GRAPH_Y1, COLOR_AXIS);
  draw_rect_outline(CONTROL_X0, CONTROL_Y0, CONTROL_X1, CONTROL_Y1, COLOR_AXIS);

  for (y = GRAPH_Y0 + 18; y < GRAPH_Y1; y += 18) {
    draw_hline(GRAPH_X0 + 1, GRAPH_X1 - 1, y, COLOR_GRID);
  }

  for (x = GRAPH_X0 + 18; x < GRAPH_X1; x += 18) {
    draw_vline(x, GRAPH_Y0 + 1, GRAPH_Y1 - 1, COLOR_GRID);
  }

  put_text_xy(2, 2, "16-BAND EQ");
  put_text_xy(2, 4, "Z X C V B N A S D F G H J K L ;");
  put_text_xy(2, 6, "Arrow keys change selected band");
  put_text_xy(2, 8, "'Bksp' reset  E echo  R reverb");

  draw_band_legend_text();
}

void graph_bar_bounds(int bar_index, int* x0, int* x1) {
  int graph_width = GRAPH_X1 - GRAPH_X0 - 1;

  *x0 = GRAPH_X0 + 1 + (bar_index * graph_width) / NUM_BANDS + 1;
  *x1 = GRAPH_X0 + 1 + ((bar_index + 1) * graph_width) / NUM_BANDS - 3;

  if (*x1 < *x0) {
    *x1 = *x0;
  }
}

void redraw_graph_background_slice(int x0, int x1) {
  int x;
  int y;

  fill_rect(x0, GRAPH_Y0 + 1, x1, GRAPH_Y1 - 1, COLOR_BG);

  for (y = GRAPH_Y0 + 18; y < GRAPH_Y1; y += 18) {
    draw_hline(x0, x1, y, COLOR_GRID);
  }

  for (x = GRAPH_X0 + 18; x < GRAPH_X1; x += 18) {
    if (x >= x0 && x <= x1) {
      draw_vline(x, GRAPH_Y0 + 1, GRAPH_Y1 - 1, COLOR_GRID);
    }
  }
}

void redraw_one_bar(int bar_index, uint8_t level) {
  int x0;
  int x1;
  int graph_height = GRAPH_Y1 - GRAPH_Y0 - 1;
  int bar_height;
  int y0;

  graph_bar_bounds(bar_index, &x0, &x1);
  redraw_graph_background_slice(x0, x1);

  bar_height = (level * graph_height) / 255;
  if (bar_height <= 0) {
    return;
  }

  y0 = GRAPH_Y1 - 1 - bar_height;
  fill_rect(x0, y0, x1, GRAPH_Y1 - 1, COLOR_BAR);
}

void redraw_one_bar_on_all_buffers(int bar_index, uint8_t level) {
  select_pixel_buffer(FRONT_PIXEL_BUFFER_BASE);
  redraw_one_bar(bar_index, level);

  select_pixel_buffer(BACK_PIXEL_BUFFER_BASE);
  redraw_one_bar(bar_index, level);
}

void control_band_bounds(int band_index, int* x0, int* x1) {
  int x_center = band_x_center(band_index);

  *x0 = x_center - 6;
  *x1 = x_center + 6;
}

void redraw_control_background_slice(int x0, int x1) {
  fill_rect(x0, CONTROL_Y0 + 1, x1, CONTROL_Y1 - 1, COLOR_PANEL);
}

void redraw_one_control_band(int band_index) {
  int x_center = band_x_center(band_index);
  int knob_y = gain_to_slider_y(band_gains[band_index]);
  int x0;
  int x1;
  uint16_t rail_color =
      (band_index == selected_band) ? COLOR_SELECTED : COLOR_NORMAL;

  control_band_bounds(band_index, &x0, &x1);
  redraw_control_background_slice(x0, x1);

  draw_vline(x_center, SLIDER_TOP, SLIDER_BOTTOM, rail_color);

  fill_rect(x_center - SLIDER_KNOB_HALF_WIDTH, knob_y - 2,
            x_center + SLIDER_KNOB_HALF_WIDTH, knob_y + 2, COLOR_KNOB);

  if (band_index == selected_band) {
    draw_rect_outline(x_center - 5, SLIDER_TOP - 5, x_center + 5,
                      SLIDER_BOTTOM + 5, COLOR_SELECTED);
  }
}

void redraw_one_control_band_on_all_buffers(int band_index) {
  select_pixel_buffer(FRONT_PIXEL_BUFFER_BASE);
  redraw_one_control_band(band_index);

  select_pixel_buffer(BACK_PIXEL_BUFFER_BASE);
  redraw_one_control_band(band_index);
}

int update_selected_band_info(void) {
  int effective_gain_percent =
      gain_to_effective_percent(band_gains[selected_band]);
  int frequency = band_visualizer_get_frequency_for_bar(selected_band);

  if (selected_band == info_selected_band &&
      effective_gain_percent == info_effective_gain_percent) {
    return 0;
  }

  clear_text_span(2, 12, 44);
  put_text_xy(2, 12, "SEL");
  put_char_xy(6, 12, band_labels[selected_band]);
  put_text_xy(8, 12, "FREQ");
  put_small_number(13, 12, frequency);
  put_text_xy(18, 12, "Hz");
  put_text_xy(22, 12, "EFF");
  put_small_number(26, 12, effective_gain_percent);
  put_text_xy(31, 12, "%");

  info_selected_band = selected_band;
  info_effective_gain_percent = effective_gain_percent;
  return 1;
}

void draw_full_dynamic_on_current_buffer(void) {
  const uint8_t* levels = band_visualizer_get_display_levels();
  int i;

  for (i = 0; i < NUM_BANDS; i++) {
    redraw_one_bar(i, levels[i]);
  }

  fill_rect(CONTROL_X0 + 1, CONTROL_Y0 + 1, CONTROL_X1 - 1, CONTROL_Y1 - 1,
            COLOR_PANEL);

  for (i = 0; i < NUM_BANDS; i++) {
    redraw_one_control_band(i);
  }
}

int vga_ui_can_draw(void) { return 1; }

void vga_ui_init(void) {
  int i;
  const uint8_t* levels = band_visualizer_get_display_levels();

  clear_char_buffer();

  *(pixel_ctrl_ptr + 1) = FRONT_PIXEL_BUFFER_BASE;
  wait_for_vsync_blocking();

  select_pixel_buffer(FRONT_PIXEL_BUFFER_BASE);
  draw_static_background();
  draw_full_dynamic_on_current_buffer();

  *(pixel_ctrl_ptr + 1) = BACK_PIXEL_BUFFER_BASE;
  select_pixel_buffer(BACK_PIXEL_BUFFER_BASE);
  draw_static_background();
  draw_full_dynamic_on_current_buffer();

  for (i = 0; i < NUM_BANDS; i++) {
    last_levels[i] = levels[i];
    last_band_gains[i] = band_gains[i];
  }

  last_selected_band = selected_band;
  info_selected_band = -1;
  info_effective_gain_percent = -1;
  update_selected_band_info();

  select_pixel_buffer(FRONT_PIXEL_BUFFER_BASE);
}

void vga_ui_draw(void) {
  const uint8_t* levels = band_visualizer_get_display_levels();
  int band_needs_redraw[NUM_BANDS];
  int i;

  for (i = 0; i < NUM_BANDS; i++) {
    band_needs_redraw[i] = 0;
  }

  for (i = 0; i < NUM_BANDS; i++) {
    if ((int)levels[i] != last_levels[i]) {
      redraw_one_bar_on_all_buffers(i, levels[i]);
      last_levels[i] = levels[i];
    }
  }

  if (last_selected_band != selected_band) {
    if (last_selected_band >= 0) {
      band_needs_redraw[last_selected_band] = 1;
    }
    band_needs_redraw[selected_band] = 1;
  }

  for (i = 0; i < NUM_BANDS; i++) {
    if (band_gains[i] != last_band_gains[i]) {
      band_needs_redraw[i] = 1;
    }
  }

  for (i = 0; i < NUM_BANDS; i++) {
    if (band_needs_redraw[i]) {
      redraw_one_control_band_on_all_buffers(i);
      last_band_gains[i] = band_gains[i];
    }
  }

  if (last_selected_band != selected_band) {
    last_selected_band = selected_band;
  }

  update_selected_band_info();

  select_pixel_buffer(FRONT_PIXEL_BUFFER_BASE);
}