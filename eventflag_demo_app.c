#include <furi.h>
#include <furi/core/string.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <gui/gui.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#define TAG "flag_demo"

typedef enum {
  EventGetTypeClear,
  EventGetTypeWait,
} EventGetType;

typedef enum {
  TestFlagLeft = 0x01,
  TestFlagRight = 0x02,
  TestFlagDummy = 0x04,
  TestFlagAll = 0x07,
  TestFlagReserved = 0xffffffff,
} TestFlag;

#define MY_LOG_MAX_SIZE 100

typedef struct {
  char *buffer;
  _Atomic uint32_t cursor;
} ModelStr;
typedef struct {
  _Atomic bool use_periodic_dummy_set;
  _Atomic EventGetType event_get_type;
  _Atomic uint32_t errors_timeout;
  _Atomic uint32_t errors_empty;
  _Atomic uint32_t errors_unknown;
  _Atomic uint32_t event_count;

  ModelStr logs_irq;
  ModelStr logs_evt;
} Model;

FuriEventLoop *event_loop = NULL;
FuriEventFlag *event_flag = NULL;
ViewPort *view_port = NULL;
FuriString *view_port_str = NULL;
Model *model = NULL;

const char *name_of_event_get_type(EventGetType type);
const char *to_yes_no(bool b);
static void draw_callback(Canvas *canvas, void *context);
void set_led_color(uint32_t color);
void add_char_concurrent(ModelStr *str, char c);

void handle_irq_button_left(void *context) {
  UNUSED(context);
  if (furi_hal_gpio_read(&gpio_button_left)) {
    // for some reason irq is also on rising edge
    return;
  }

  add_char_concurrent(&model->logs_irq, 'L');
  furi_event_flag_set(event_flag, TestFlagLeft);
}

void handle_irq_button_right(void *context) {
  UNUSED(context);
  if (furi_hal_gpio_read(&gpio_button_right)) {
    // for some reason irq is also on rising edge
    return;
  }
  add_char_concurrent(&model->logs_irq, 'R');
  furi_event_flag_set(event_flag, TestFlagRight);
}

void handle_event_button(FuriEventLoopObject *object, void *context) {
  UNUSED(object);
  UNUSED(context);

  uint32_t events;

  atomic_fetch_add(&model->event_count, 1);

  switch (model->event_get_type) {
  case EventGetTypeWait:
    events = furi_event_flag_wait(event_flag, TestFlagAll, FuriFlagWaitAny, 0);
    break;
  case EventGetTypeClear:
  default:
    events = furi_event_flag_clear(event_flag, TestFlagAll);
    break;
  }

  if (events & FuriFlagError) {
    set_led_color(0xff0000);
    if (events == FuriFlagErrorTimeout) {
      atomic_fetch_add(&model->errors_timeout, 1);
    } else {
      atomic_fetch_add(&model->errors_unknown, 1);
    }
    return;
  }
  if (events == 0) {
    atomic_fetch_add(&model->errors_empty, 1);
    return;
  }
  if (events & TestFlagLeft) {
    add_char_concurrent(&model->logs_evt, 'L');
  }
  if (events & TestFlagRight) {
    add_char_concurrent(&model->logs_evt, 'R');
  }
}

static void draw_callback(Canvas *canvas, void *context) {
  UNUSED(context);
  canvas_set_font(canvas, FontPrimary);

  canvas_draw_str(canvas, 3, 8, "EventFlag IRQ Demo");

  int line = 0;
  canvas_draw_str(canvas, 3, 22 + 10 * line, "IRQ:");
  canvas_draw_str(canvas, 30, 22 + 10 * line, model->logs_irq.buffer);
  line++;

  canvas_draw_str(canvas, 3, 22 + 10 * line, "EVT:");
  canvas_draw_str(canvas, 30, 22 + 10 * line, model->logs_evt.buffer);
  line++;

  furi_string_printf(view_port_str, "unset method: %s",
                     name_of_event_get_type(model->event_get_type));
  canvas_draw_str(canvas, 3, 22 + 10 * line,
                  furi_string_get_cstr(view_port_str));
  line++;

  furi_string_printf(view_port_str, "dummy evt timer: %s",
                     to_yes_no(model->use_periodic_dummy_set));
  canvas_draw_str(canvas, 3, 22 + 10 * line,
                  furi_string_get_cstr(view_port_str));
  line++;

  furi_string_printf(view_port_str, "err: %ld %ld %ld evt cnt: %ld",
                     model->errors_timeout, model->errors_empty,
                     model->errors_unknown, model->event_count);
  canvas_draw_str(canvas, 3, 22 + 10 * line,
                  furi_string_get_cstr(view_port_str));
  line++;
}

const char *name_of_event_get_type(EventGetType type) {
  switch (type) {
  case EventGetTypeClear:
    return "clear";
  case EventGetTypeWait:
    return "wait";
  default:
    return "unknown";
  }
}

const char *to_yes_no(bool b) { return b ? "yes" : "no"; }

void add_char_concurrent(ModelStr *str, char c) {
  uint32_t old = atomic_fetch_add(&str->cursor, 1);
  if (old >= MY_LOG_MAX_SIZE) {
    str->cursor = MY_LOG_MAX_SIZE;
    return;
  }
  str->buffer[old] = c;
}

void loop_iteration(void *context) {
  UNUSED(context);
  if (!furi_hal_gpio_read(&gpio_button_back)) {
    furi_event_loop_stop(event_loop);
  }
  if (!furi_hal_gpio_read(&gpio_button_up)) {
    model->event_get_type =
        (model->event_get_type == EventGetTypeClear ? EventGetTypeWait
                                                    : EventGetTypeClear);
    furi_delay_ms(300);
  }
  if (!furi_hal_gpio_read(&gpio_button_down)) {
    model->use_periodic_dummy_set = !model->use_periodic_dummy_set;
    furi_delay_ms(300);
  }

  view_port_update(view_port);

  if (model->use_periodic_dummy_set) {
    furi_event_flag_set(event_flag, TestFlagDummy);
  }
}

void set_led_color(uint32_t color) {
  furi_hal_light_set(LightRed, (color >> 16) & 0xff);
  furi_hal_light_set(LightGreen, (color >> 8) & 0xff);
  furi_hal_light_set(LightBlue, (color >> 0) & 0xff);
}

void modelstr_init(ModelStr *str) {
  str->buffer = malloc(MY_LOG_MAX_SIZE + 1);
  memset(str->buffer, 0, MY_LOG_MAX_SIZE + 1);
  str->cursor = 0;
}

void modelstr_deinit(ModelStr *str) { free(str->buffer); }

void model_init() {
  model = malloc(sizeof(Model));

  model->errors_timeout = 0;
  model->errors_empty = 0;
  model->errors_unknown = 0;
  model->event_count = 0;
  model->use_periodic_dummy_set = false;
  model->event_get_type = EventGetTypeWait;

  modelstr_init(&model->logs_irq);
  modelstr_init(&model->logs_evt);
}

void model_deinit() {
  modelstr_deinit(&model->logs_irq);
  modelstr_deinit(&model->logs_evt);
  free(model);
  model = NULL;
}

int eventflag_demo_app(void *p) {
  UNUSED(p);

  model_init();

  Gui *gui = furi_record_open(RECORD_GUI);
  view_port_str = furi_string_alloc();
  view_port = view_port_alloc();
  view_port_draw_callback_set(view_port, draw_callback, NULL);
  gui_add_view_port(gui, view_port, GuiLayerFullscreen);

  set_led_color(0);
  furi_hal_gpio_init(&gpio_button_left, GpioModeInterruptFall, GpioPullUp,
                     GpioSpeedVeryHigh);
  furi_hal_gpio_init(&gpio_button_right, GpioModeInterruptFall, GpioPullUp,
                     GpioSpeedVeryHigh);
  event_loop = furi_event_loop_alloc();
  event_flag = furi_event_flag_alloc();
  furi_event_loop_subscribe_event_flag(
      event_loop, event_flag, FuriEventLoopEventIn, handle_event_button, NULL);

  furi_hal_gpio_remove_int_callback(&gpio_button_left);
  furi_hal_gpio_add_int_callback(&gpio_button_left, handle_irq_button_left,
                                 NULL);
  furi_hal_gpio_remove_int_callback(&gpio_button_right);
  furi_hal_gpio_add_int_callback(&gpio_button_right, handle_irq_button_right,
                                 NULL);

  FuriEventLoopTimer *timer = furi_event_loop_timer_alloc(
      event_loop, loop_iteration, FuriEventLoopTimerTypePeriodic, NULL);
  furi_event_loop_timer_start(timer, 100);
  furi_event_loop_run(event_loop);

  furi_event_loop_timer_free(timer);
  furi_hal_gpio_remove_int_callback(&gpio_button_left);
  furi_hal_gpio_remove_int_callback(&gpio_button_right);
  furi_event_loop_unsubscribe(event_loop, event_flag);
  furi_event_flag_free(event_flag);
  furi_event_loop_free(event_loop);
  gui_remove_view_port(gui, view_port);
  furi_string_free(view_port_str);
  model_deinit();
  set_led_color(0);
  return 0;
}
