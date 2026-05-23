// Pebble Time 2 task app — single-file sketch for CloudPebble
// Features: scrollable list, mark done / delete via ActionMenu, add via dictation,
// persistent across launches.

#include <pebble.h>

// ---------- Data model ----------

#define MAX_TASKS       32
#define MAX_TASK_LEN    60   // 60 chars + null = 61 bytes per task; fits in 256-byte persist limit

typedef struct {
  char text[MAX_TASK_LEN];
  bool done;
} Task;

static Task     s_tasks[MAX_TASKS];
static uint8_t  s_task_count = 0;

// Persist keys
#define KEY_COUNT       0
#define KEY_TASK_BASE   1   // task i lives at KEY_TASK_BASE + i

// Action codes used as action_data in the ActionMenu (cast to void*)
#define ACTION_TOGGLE_DONE  1
#define ACTION_DELETE       2

// Index of the task whose ActionMenu is currently open. Set just before opening.
static int s_action_target = -1;

// ---------- UI handles ----------

static Window     *s_main_window;
static MenuLayer  *s_menu_layer;

static DictationSession *s_dictation;

static ActionMenu       *s_action_menu;
static ActionMenuLevel  *s_action_root;

// ---------- Persistence ----------

static void load_tasks(void) {
  s_task_count = 0;
  if (!persist_exists(KEY_COUNT)) return;

  int32_t count = persist_read_int(KEY_COUNT);
  if (count < 0) count = 0;
  if (count > MAX_TASKS) count = MAX_TASKS;

  for (int i = 0; i < count; i++) {
    if (persist_exists(KEY_TASK_BASE + i)) {
      persist_read_data(KEY_TASK_BASE + i, &s_tasks[i], sizeof(Task));
      // Defensive: ensure null termination in case of corruption
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
  // Clean up any stale keys past the current count
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
  s_tasks[s_task_count].done = false;
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
  // Tear down the hierarchy we built; labels are string literals so no per-item free.
  if (s_action_root) {
    action_menu_hierarchy_destroy(s_action_root, NULL, NULL);
    s_action_root = NULL;
  }
  s_action_target = -1;
}

static void open_action_menu_for_task(int idx) {
  s_action_target = idx;

  s_action_root = action_menu_level_create(2);

  // Label changes based on current done state
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
    .align = ActionMenuAlignCenter,
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
    // Scroll to the new item at the bottom
    MenuIndex idx = MenuIndex(0, s_task_count); // row index in section 0 (see menu layout below)
    menu_layer_set_selected_index(s_menu_layer, idx, MenuRowAlignCenter, true);
  }
  // On failure: do nothing; the system already showed an error dialog.
}

static void start_dictation(void) {
  if (!s_dictation) {
    s_dictation = dictation_session_create(MAX_TASK_LEN, dictation_cb, NULL);
  }
  if (s_dictation) {
    dictation_session_start(s_dictation);
  } else {
    // Likely the phone isn't connected, or platform has no mic support.
    // In production you'd show a dialog window here.
    APP_LOG(APP_LOG_LEVEL_WARNING, "Dictation unavailable");
  }
}

// ---------- MenuLayer callbacks ----------
// Layout: section 0 has (s_task_count + 1) rows.
//   row 0          = "+ Add task"
//   row 1..N       = tasks (1-indexed in UI, 0-indexed in s_tasks)

static uint16_t menu_get_num_rows(MenuLayer *menu, uint16_t section_index, void *ctx) {
  return s_task_count + 1;  // +1 for the "Add task" row
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer,
                          MenuIndex *cell_index, void *callback_context) {
  if (cell_index->row == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "+ Add task", "Press select", NULL);
    return;
  }

  int task_idx = cell_index->row - 1;
  if (task_idx >= s_task_count) return;

  const char *text = s_tasks[task_idx].text;
  GRect bounds = layer_get_bounds(cell_layer);

  // Inset roughly matches menu_cell_basic_draw's padding
  GRect text_bounds = GRect(bounds.origin.x + 5, bounds.origin.y,
                            bounds.size.w - 10, bounds.size.h);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  // MenuLayer already set the context's text color based on highlight state,
  // so draw_text and draw_line will both use the correct color automatically.
  graphics_draw_text(ctx, text, font, text_bounds,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  if (s_tasks[task_idx].done) {
    // Measure the rendered text so the strike is exactly its width
    GSize size = graphics_text_layout_get_content_size(
      text, font, text_bounds,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

    int16_t line_y  = text_bounds.origin.y + (size.h / 2) + 2;
    int16_t line_x1 = text_bounds.origin.x;
    int16_t line_x2 = text_bounds.origin.x + size.w;

    // Two adjacent 1px lines = a more visible 2px strike
    graphics_draw_line(ctx, GPoint(line_x1, line_y),     GPoint(line_x2, line_y));
    graphics_draw_line(ctx, GPoint(line_x1, line_y + 1), GPoint(line_x2, line_y + 1));
  }
}

static void menu_select_click(MenuLayer *menu, MenuIndex *cell_index, void *ctx) {
  if (cell_index->row == 0) {
    start_dictation();
  } else {
    open_action_menu_for_task(cell_index->row - 1);
  }
}

// ---------- Window lifecycle ----------

static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows    = menu_get_num_rows,
    .draw_row        = menu_draw_row,
    .select_click    = menu_select_click,
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
  load_tasks();

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  save_tasks();

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
