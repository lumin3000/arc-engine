
#ifndef RENDER_DIAGNOSTICS_H
#define RENDER_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#define DIAG_MAX_ENTRIES 1024
#define DIAG_MAX_NAME_LEN 32
#define DIAG_MAX_FILTER_LEN 64

#define ISSUE_NONE 0x00
#define ISSUE_OFFSCREEN 0x01
#define ISSUE_BEHIND_CAMERA 0x02
#define ISSUE_NO_TEXTURE 0x04
#define ISSUE_ZERO_SIZE 0x08
#define ISSUE_INVALID_MESH 0x10
#define ISSUE_ZERO_TRIS 0x20

typedef struct {
  int id;
  char name[DIAG_MAX_NAME_LEN];

  float world_x, world_y, world_z;
  float ndc_x, ndc_y, ndc_z;
  bool in_viewport;

  float altitude;
  int render_queue;
  int texture_id;
  int tri_count;
  bool gpu_valid;

  float scale_x, scale_y, scale_z;

  uint8_t issue_flags;
} RenderDiagEntry;

typedef struct {

  RenderDiagEntry entries[DIAG_MAX_ENTRIES];
  int entry_count;

  int frame_number;

  char name_filter[DIAG_MAX_FILTER_LEN];
  bool issues_only;
  int min_render_queue;
  int max_render_queue;

  bool continuous_mode;
  bool dump_requested;

  int total_objects;
  int objects_with_issues;
} RenderDiagState;

void render_diag_init(void);
void render_diag_shutdown(void);

void render_diag_begin_frame(void);
void render_diag_end_frame(void);

void render_diag_record(const RenderDiagEntry *entry);

void render_diag_dump(void);
void render_diag_request_dump(void);
void render_diag_set_continuous(bool enabled);

void render_diag_set_filter(const char *name_filter);
void render_diag_set_issues_only(bool enabled);
void render_diag_set_render_queue_range(int min_rq, int max_rq);
void render_diag_clear_filters(void);

bool render_diag_is_enabled(void);
int render_diag_get_entry_count(void);
const RenderDiagEntry *render_diag_get_entry(int index);

const char *render_diag_issues_to_string(uint8_t issues);

#endif
