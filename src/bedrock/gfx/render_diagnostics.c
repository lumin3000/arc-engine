
#include "render_diagnostics.h"
#include <stdio.h>
#include <string.h>

static RenderDiagState g_diag = {0};
static bool g_initialized = false;

void render_diag_init(void) {
  if (g_initialized)
    return;

  memset(&g_diag, 0, sizeof(g_diag));
  g_diag.min_render_queue = 0;
  g_diag.max_render_queue = 99999;

  g_initialized = true;
  fprintf(stderr, "[RenderDiag] Initialized\n");
}

void render_diag_shutdown(void) {
  if (!g_initialized)
    return;

  memset(&g_diag, 0, sizeof(g_diag));
  g_initialized = false;
}

void render_diag_begin_frame(void) {
  if (!g_initialized)
    render_diag_init();

  g_diag.frame_number++;
}

void render_diag_end_frame(void) {
  if (!g_initialized)
    return;

  if (g_diag.dump_requested || g_diag.continuous_mode) {
    render_diag_dump();
    g_diag.dump_requested = false;
  }
}

void render_diag_record(const RenderDiagEntry *entry) {
  if (!g_initialized || !entry)
    return;

  g_diag.total_objects++;

  if (entry->issue_flags != ISSUE_NONE) {
    g_diag.objects_with_issues++;
  }

  if (g_diag.name_filter[0] != '\0') {
    if (strstr(entry->name, g_diag.name_filter) == NULL) {
      return;
    }
  }

  if (g_diag.issues_only && entry->issue_flags == ISSUE_NONE) {
    return;
  }

  if (entry->render_queue < g_diag.min_render_queue ||
      entry->render_queue > g_diag.max_render_queue) {
    return;
  }

  if (g_diag.entry_count < DIAG_MAX_ENTRIES) {
    memcpy(&g_diag.entries[g_diag.entry_count], entry, sizeof(RenderDiagEntry));
    g_diag.entry_count++;
  }
}

const char *render_diag_issues_to_string(uint8_t issues) {
  static char buffer[256];
  buffer[0] = '\0';

  if (issues == ISSUE_NONE) {
    return "OK";
  }

  if (issues & ISSUE_OFFSCREEN)
    strcat(buffer, "OFFSCREEN ");
  if (issues & ISSUE_BEHIND_CAMERA)
    strcat(buffer, "BEHIND_CAM ");
  if (issues & ISSUE_NO_TEXTURE)
    strcat(buffer, "NO_TEX ");
  if (issues & ISSUE_ZERO_SIZE)
    strcat(buffer, "ZERO_SIZE ");
  if (issues & ISSUE_INVALID_MESH)
    strcat(buffer, "BAD_MESH ");
  if (issues & ISSUE_ZERO_TRIS)
    strcat(buffer, "NO_TRIS ");

  int len = strlen(buffer);
  if (len > 0 && buffer[len - 1] == ' ') {
    buffer[len - 1] = '\0';
  }

  return buffer;
}

void render_diag_dump(void) {
  if (!g_initialized)
    return;

  fprintf(stderr, "\n");
  fprintf(stderr,
          "╔══════════════════════════════════════════════════════════════╗\n");
  fprintf(stderr,
          "║          RENDER DIAGNOSTICS - Frame %-6d                   ║\n",
          g_diag.frame_number);
  fprintf(stderr,
          "╠══════════════════════════════════════════════════════════════╣\n");
  fprintf(stderr,
          "║ Total: %-4d objects | Issues: %-4d | Filtered: %-4d          ║\n",
          g_diag.total_objects, g_diag.objects_with_issues, g_diag.entry_count);

  if (g_diag.name_filter[0] != '\0' || g_diag.issues_only) {
    fprintf(stderr,
            "║ Filter: name=\"%s\" issues_only=%s                    ║\n",
            g_diag.name_filter[0] ? g_diag.name_filter : "*",
            g_diag.issues_only ? "YES" : "NO");
  }

  fprintf(stderr,
          "╚══════════════════════════════════════════════════════════════╝\n");

  if (g_diag.entry_count == 0) {
    fprintf(stderr, "  (no entries to display)\n\n");
    return;
  }

  for (int i = 0; i < g_diag.entry_count; i++) {
    const RenderDiagEntry *e = &g_diag.entries[i];

    const char *marker = (e->issue_flags != ISSUE_NONE) ? "[!]" : "[ ]";

    const char *status =
        (e->issue_flags != ISSUE_NONE)
            ? render_diag_issues_to_string(e->issue_flags)
            : "OK";

    fprintf(stderr, "%s %s#%d @ world(%.1f, %.2f, %.1f) -> NDC(%.2f, %.2f, %.2f)",
            marker, e->name, e->id, e->world_x, e->world_y, e->world_z,
            e->ndc_x, e->ndc_y, e->ndc_z);

    if (!e->in_viewport) {
      fprintf(stderr, " [OUT]");
    }
    fprintf(stderr, "\n");

    fprintf(stderr,
            "    tex=%d tri=%d rq=%d scale=(%.1f,%.1f,%.1f) gpu=%s | %s\n",
            e->texture_id, e->tri_count, e->render_queue, e->scale_x, e->scale_y,
            e->scale_z, e->gpu_valid ? "Y" : "N", status);
  }

  fprintf(stderr, "\n");

  g_diag.entry_count = 0;
  g_diag.total_objects = 0;
  g_diag.objects_with_issues = 0;
}

void render_diag_request_dump(void) { g_diag.dump_requested = true; }

void render_diag_set_continuous(bool enabled) {
  g_diag.continuous_mode = enabled;
  fprintf(stderr, "[RenderDiag] Continuous mode: %s\n",
          enabled ? "ON" : "OFF");
}

void render_diag_set_filter(const char *name_filter) {
  if (name_filter) {
    strncpy(g_diag.name_filter, name_filter, DIAG_MAX_FILTER_LEN - 1);
    g_diag.name_filter[DIAG_MAX_FILTER_LEN - 1] = '\0';
  } else {
    g_diag.name_filter[0] = '\0';
  }
  fprintf(stderr, "[RenderDiag] Name filter: \"%s\"\n", g_diag.name_filter);
}

void render_diag_set_issues_only(bool enabled) {
  g_diag.issues_only = enabled;
  fprintf(stderr, "[RenderDiag] Issues only: %s\n", enabled ? "ON" : "OFF");
}

void render_diag_set_render_queue_range(int min_rq, int max_rq) {
  g_diag.min_render_queue = min_rq;
  g_diag.max_render_queue = max_rq;
  fprintf(stderr, "[RenderDiag] RenderQueue range: [%d, %d]\n", min_rq, max_rq);
}

void render_diag_clear_filters(void) {
  g_diag.name_filter[0] = '\0';
  g_diag.issues_only = false;
  g_diag.min_render_queue = 0;
  g_diag.max_render_queue = 99999;
  fprintf(stderr, "[RenderDiag] Filters cleared\n");
}

bool render_diag_is_enabled(void) { return g_initialized; }

int render_diag_get_entry_count(void) { return g_diag.entry_count; }

const RenderDiagEntry *render_diag_get_entry(int index) {
  if (index < 0 || index >= g_diag.entry_count)
    return NULL;
  return &g_diag.entries[index];
}
