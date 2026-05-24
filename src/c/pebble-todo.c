// Pebble Time 2 task app — single-file sketch for CloudPebble
// Features: scrollable list, mark done / delete via ActionMenu, add via dictation,
// persistent across launches, settings (auto-delete done tasks after 1 min).

#include <pebble.h>

// ---------- Constants ----------

#define MAX_TASKS            32
#define MAX_TASK_LEN         60
#define AUTO_DELETE_DELAY_S  60
#define AUTO_DELETE_CHECK_MS (10 * 1000)

// Persist keys
#define KEY_COUNT     0
#define KEY_TASK_BASE 1                           // task i lives at KEY_TASK_BASE + i
#define KEY_SETTINGS  (KEY_TASK_BASE + MAX_TASKS) // = 33

// Action codes used as action_data in the ActionMenu (cast to void*)
#define ACTION_TOGGLE_DONE 1
#define ACTION_DELETE      2

// ---------- Types ----------

typedef struct {
  char     text[MAX_TASK_LEN];
  bool     done;
  uint32_t done_at; // epoch seconds when marked done; 0 = no pending auto-delete
} Task;

typedef struct {
  bool auto_delete; // default: false
} Settings;

// ---------- Data ----------

static Task     s_tasks[MAX_TASKS];
static uint8_t  s_task_count = 0;
static Settings s_settings;

// Index of the task whose ActionMenu is currently open.
static int s_action_target = -1;

// ---------- UI handles ----------

static Window    *s_main_window;
static MenuLayer *s_menu_layer;

static DictationSession *s_dictation;

static ActionMenu      *s_action_menu;
static ActionMenuLevel *s_action_root;

static Window    *s_settings_window;
static MenuLayer *s_settings_menu_layer;

static AppTimer *s_auto_delete_timer;

// ---------- Forward declarations ----------

static void schedule_auto_delete_timer(void);

// ---------- Persistence ----------

static void load_settings(void) {
  s_settings.auto_delete = false;
  if (persist_exists(KEY_SETTINGS)) {
    persist_read_data(KEY_SETTINGS, &s_settings, sizeof(Settings));
  }
}

static void save_settings(void) {
  persist_write_data(KEY_SETTINGS, &s_settings, sizeof(Settings));
}

static void load_tasks(void) {
  s_task_count = 0;
  if (!persist_exists(KEY_COUNT)) return;

  int32_t count = persist_read_int(KEY_COUNT);
  if (count < 0) count = 0;
  if (count > MAX_TASKS) count = MAX_TASKS;

  for (int i = 0; i < count; i++) {
    if (persist_exists(KEY_TASK_BASE + i)) {
      persist_read_data(KEY_TASK_BASE + i, &s_tasks[i], sizeof(Task));
      s_tasks[i].text[MAX_TASK_LEN - 1] = '\0';
      s_task_count++;
    }
  }
}

static void save_tasks(void) {
  persist_write_int(KEY_COUNT, s_task_count);
  for (int i = 0; i < s_task_count; i++) {
    persist_write_data(KEY_TASK_BASE + i, &s_tasks[i], sizeof(Task));
  }
  for (int i = s_task_count; i < MAX_TASKS; i++) {
    if (persist_exists(KEY_TASK_BASE + i)) {
      persist_delete(KEY_TASK_BASE + i);
    }
  }
}

// ---------- Task operations ----------

static void task_add(const char *text) {
  if (s_task_count >= MAX_TASKS) return;
  strncpy(s_tasks[s_task_count].text, text, MAX_TASK_LEN - 1);
  s_tasks[s_task_count].text[MAX_TASK_LEN - 1] = '\0';
  s_tasks[s_task_count].done    = false;
  s_tasks[s_task_count].done_at = 0;
  s_task_count++;
}

static void task_delete(int idx) {
  if (idx < 0 || idx >= s_task_count) return;
  for (int i = idx; i < s_task_count - 1; i++) {
    s_tasks[i] = s_tasks[i + 1];
  }
  s_task_count--;
}

static void task_toggle_done(int idx) {
  if (idx < 0 || idx >= s_task_count) return;
  s_tasks[idx].done = !s_tasks[idx].done;
  if (s_tasks[idx].done && s_settings.auto_delete) {
    s_tasks[idx].done_at = (uint32_t)time(NULL);
    schedule_auto_delete_timer();
  } else {
    s_tasks[idx].done_at = 0;
  }
}

// ---------- Auto-delete ----------

static void auto_delete_tick(void *ctx) {
  s_auto_delete_timer = NULL;
  if (!s_settings.auto_delete) return;

  time_t now     = time(NULL);
  bool   changed = false;

  // Iterate backwards so deleting an index doesn't shift unvisited entries.
  for (int i = s_task_count - 1; i >= 0; i--) {
    if (s_tasks[i].done && s_tasks[i].done_at > 0 &&
        (now - (time_t)s_tasks[i].done_at) >= AUTO_DELETE_DELAY_S) {
      task_delete(i);
      changed = true;
    }
  }

  if (changed) {
    menu_layer_reload_data(s_menu_layer);
  }

  schedule_auto_delete_timer();
}

static void schedule_auto_delete_timer(void) {
  if (!s_settings.auto_delete || s_auto_delete_timer) return;
  for (int i = 0; i < s_task_count; i++) {
    if (s_tasks[i].done && s_tasks[i].done_at > 0) {
      s_auto_delete_timer = app_timer_register(AUTO_DELETE_CHECK_MS, auto_delete_tick, NULL);
      return;
    }
  }
}

static void cancel_auto_delete_timer(void) {
  if (s_auto_delete_timer) {
    app_timer_cancel(s_auto_delete_timer);
    s_auto_delete_timer = NULL;
  }
}

// ---------- ActionMenu (per-task actions) ----------

static void action_perform(ActionMenu *action_menu,
                           const ActionMenuItem *action,
                           void *context) {
  uintptr_t code = (uintptr_t)action_menu_item_get_action_data(action);
  if (s_action_target < 0) return;

  if (code == ACTION_TOGGLE_DONE) {
    task_toggle_done(s_action_target);
  } else if (code == ACTION_DELETE) {
    task_delete(s_action_target);
  }

  menu_layer_reload_data(s_menu_layer);
}

static void action_menu_did_close(ActionMenu *menu,
                                  const ActionMenuItem *performed_action,
                                  void *context) {
  if (s_action_root) {
    action_menu_hierarchy_destroy(s_action_root, NULL, NULL);
    s_action_root = NULL;
  }
  s_action_target = -1;
}

static void open_action_menu_for_task(int idx) {
  s_action_target = idx;
  s_action_root = action_menu_level_create(2);

  const char *toggle_label = s_tasks[idx].done ? "Mark Undone" : "Mark Done";
  action_menu_level_add_action(s_action_root, toggle_label,
                               action_perform, (void *)(uintptr_t)ACTION_TOGGLE_DONE);
  action_menu_level_add_action(s_action_root, "Delete",
                               action_perform, (void *)(uintptr_t)ACTION_DELETE);

  ActionMenuConfig config = {
    .root_level = s_action_root,
    .colors = {
      .background = GColorChromeYellow,
      .foreground = GColorBlack,
    },
    .align     = ActionMenuAlignCenter,
    .did_close = action_menu_did_close,
  };

  s_action_menu = action_menu_open(&config);
}

// ---------- Dictation ----------

static void dictation_cb(DictationSession *session,
                         DictationSessionStatus status,
                         char *transcription,
                         void *context) {
  if (status == DictationSessionStatusSuccess && transcription) {
    task_add(transcription);
    menu_layer_reload_data(s_menu_layer);
    MenuIndex idx = MenuIndex(0, s_task_count);
    menu_layer_set_selected_index(s_menu_layer, idx, MenuRowAlignCenter, true);
  }
}

static void start_dictation(void) {
  if (!s_dictation) {
    s_dictation = dictation_session_create(MAX_TASK_LEN, dictation_cb, NULL);
  }
  if (s_dictation) {
    dictation_session_start(s_dictation);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Dictation unavailable");
  }
}

// ---------- Settings window ----------

static uint16_t settings_get_num_rows(MenuLayer *menu, uint16_t section_index, void *ctx) {
  return 1;
}

static void settings_draw_row(GContext *ctx, const Layer *cell_layer,
                              MenuIndex *cell_index, void *callback_context) {
  const char *subtitle = s_settings.auto_delete ? "On" : "Off";
  menu_cell_basic_draw(ctx, cell_layer, "Auto Delete", subtitle, NULL);
}

static void settings_select_click(MenuLayer *menu, MenuIndex *cell_index, void *ctx) {
  s_settings.auto_delete = !s_settings.auto_delete;

  if (!s_settings.auto_delete) {
    cancel_auto_delete_timer();
    for (int i = 0; i < s_task_count; i++) {
      s_tasks[i].done_at = 0;
    }
  } else {
    // Arm already-done tasks from this moment.
    uint32_t now = (uint32_t)time(NULL);
    for (int i = 0; i < s_task_count; i++) {
      if (s_tasks[i].done) {
        s_tasks[i].done_at = now;
      }
    }
    schedule_auto_delete_timer();
  }

  menu_layer_reload_data(s_settings_menu_layer);
}

static void settings_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  s_settings_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_settings_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = settings_get_num_rows,
    .draw_row     = settings_draw_row,
    .select_click = settings_select_click,
  });
  menu_layer_set_click_config_onto_window(s_settings_menu_layer, window);

#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_settings_menu_layer, GColorChromeYellow, GColorBlack);
#endif

  layer_add_child(root, menu_layer_get_layer(s_settings_menu_layer));
}

static void settings_window_unload(Window *window) {
  menu_layer_destroy(s_settings_menu_layer);
  save_settings();
  window_destroy(s_settings_window);
  s_settings_window = NULL;
}

static void open_settings_window(void) {
  s_settings_window = window_create();
  window_set_window_handlers(s_settings_window, (WindowHandlers){
    .load   = settings_window_load,
    .unload = settings_window_unload,
  });
  window_stack_push(s_settings_window, true);
}

// ---------- Main MenuLayer callbacks ----------
// Two sections:
//   Section 0: s_task_count + 1 rows — row 0 = "+ Add task", rows 1..N = tasks
//   Section 1: 1 row — Settings

static uint16_t menu_get_num_sections(MenuLayer *menu, void *ctx) {
  return 2;
}

static uint16_t menu_get_num_rows(MenuLayer *menu, uint16_t section_index, void *ctx) {
  return section_index == 0 ? s_task_count + 1 : 1;
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer,
                          MenuIndex *cell_index, void *callback_context) {
  if (cell_index->section == 1) {
    menu_cell_basic_draw(ctx, cell_layer, "Settings", NULL, NULL);
    return;
  }

  if (cell_index->row == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "+ Add task", "Press select", NULL);
    return;
  }

  int task_idx = cell_index->row - 1;
  if (task_idx >= s_task_count) return;

  const char *text        = s_tasks[task_idx].text;
  GRect       bounds      = layer_get_bounds(cell_layer);
  GRect       text_bounds = GRect(bounds.origin.x + 5, bounds.origin.y,
                                  bounds.size.w - 10, bounds.size.h);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  graphics_draw_text(ctx, text, font, text_bounds,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  if (s_tasks[task_idx].done) {
    GSize size = graphics_text_layout_get_content_size(
      text, font, text_bounds,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

    int16_t line_y  = text_bounds.origin.y + (size.h / 2) + 2;
    int16_t line_x1 = text_bounds.origin.x;
    int16_t line_x2 = text_bounds.origin.x + size.w;

    graphics_draw_line(ctx, GPoint(line_x1, line_y),     GPoint(line_x2, line_y));
    graphics_draw_line(ctx, GPoint(line_x1, line_y + 1), GPoint(line_x2, line_y + 1));
  }
}

static void menu_select_click(MenuLayer *menu, MenuIndex *cell_index, void *ctx) {
  if (cell_index->section == 1) {
    open_settings_window();
    return;
  }
  if (cell_index->row == 0) {
    start_dictation();
  } else {
    open_action_menu_for_task(cell_index->row - 1);
  }
}

// ---------- Window lifecycle ----------

static void main_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections,
    .get_num_rows     = menu_get_num_rows,
    .draw_row         = menu_draw_row,
    .select_click     = menu_select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);

#if defined(PBL_COLOR)
  menu_layer_set_highlight_colors(s_menu_layer, GColorChromeYellow, GColorBlack);
#endif

  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void main_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
}

// ---------- App init / deinit ----------

static void init(void) {
  load_settings();
  load_tasks();

  if (s_settings.auto_delete) {
    schedule_auto_delete_timer();
  }

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  save_tasks();
  save_settings();
  cancel_auto_delete_timer();

  if (s_dictation) {
    dictation_session_destroy(s_dictation);
    s_dictation = NULL;
  }
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
