
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include "dynstruct.h"
#include "../../log.h"
#include <stdlib.h>
#include <string.h>

#ifndef FATAL
#define FATAL(fmt, ...) \
  do { \
    LOG_ERROR("[FATAL] " fmt "\n", ##__VA_ARGS__); \
    exit(1); \
  } while(0)
#endif

#define DS_ALIGN(size, alignment) \
  (uint16_t)(((uint16_t)(size) + (uint16_t)(alignment) - 1) & ~((uint16_t)(alignment) - 1))

static const uint16_t g_field_sizes[] = {
  1,
  2,
  4,
  4,
};

void type_define(TypeDesc *td, const char *name,
                 const char **field_names, const uint8_t *field_types,
                 const uint8_t *field_counts, int count) {
  if (!td || !name || !field_names || !field_types || count <= 0) {
    FATAL("[dynstruct] type_define: invalid parameters");
  }
  if (count > 255) {
    FATAL("[dynstruct] type_define '%s': too many fields (%d)", name, count);
  }

  memset(td, 0, sizeof(TypeDesc));
  strncpy(td->name, name, sizeof(td->name) - 1);
  td->name[sizeof(td->name) - 1] = '\0';

  td->field_map = NULL;
  sh_new_strdup(td->field_map);

  uint16_t offset = 0;
  uint16_t max_alignment = 1;

  for (int i = 0; i < count; i++) {
    uint8_t ftype = field_types[i];
    if (ftype >= FIELD_TYPE_COUNT) {
      FATAL("[dynstruct] type_define '%s': field '%s' has invalid type %u",
            name, field_names[i], ftype);
    }

    uint16_t elem_size = g_field_sizes[ftype];
    uint16_t member_alignment = elem_size;

    uint8_t fcount = (field_counts && field_counts[i] > 0) ? field_counts[i] : 1;
    uint16_t member_size = elem_size * fcount;

    offset = DS_ALIGN(offset, member_alignment);

    FieldDesc fd;
    fd.key    = (char *)field_names[i];
    fd.offset = offset;
    fd.size   = member_size;
    fd.type   = ftype;
    fd.count  = fcount;
    shputs(td->field_map, fd);

    offset += member_size;

    if (member_alignment > max_alignment) {
      max_alignment = member_alignment;
    }
  }

  td->stride = DS_ALIGN(offset, max_alignment);
  td->field_count = (uint16_t)count;
  td->alignment = max_alignment;

  LOG_INFO("[dynstruct] Defined type '%s': %d fields, stride=%u, align=%u\n",
           td->name, count, td->stride, td->alignment);
}

uint16_t type_field_offset(TypeDesc *td, const char *name) {
  if (!td || !name) {
    FATAL("[dynstruct] type_field_offset: NULL argument");
  }

  FieldDesc *fd = shgetp_null(td->field_map, name);
  if (!fd) {
    FATAL("[dynstruct] type '%s' has no field '%s'", td->name, name);
  }
  return fd->offset;
}

void *field_ptr(void *row, TypeDesc *td, const char *name) {
  uint16_t off = type_field_offset(td, name);
  return (uint8_t *)row + off;
}

uint8_t field_get_u8(const void *row, TypeDesc *td, const char *name) {
  uint16_t off = type_field_offset(td, name);
  return *(const uint8_t *)((const uint8_t *)row + off);
}

uint16_t field_get_u16(const void *row, TypeDesc *td, const char *name) {
  uint16_t off = type_field_offset(td, name);
  return *(const uint16_t *)((const uint8_t *)row + off);
}

int32_t field_get_i32(const void *row, TypeDesc *td, const char *name) {
  uint16_t off = type_field_offset(td, name);
  return *(const int32_t *)((const uint8_t *)row + off);
}

float field_get_f32(const void *row, TypeDesc *td, const char *name) {
  uint16_t off = type_field_offset(td, name);
  return *(const float *)((const uint8_t *)row + off);
}

void field_set_u8(void *row, TypeDesc *td, const char *name, uint8_t v) {
  uint16_t off = type_field_offset(td, name);
  *(uint8_t *)((uint8_t *)row + off) = v;
}

void field_set_u16(void *row, TypeDesc *td, const char *name, uint16_t v) {
  uint16_t off = type_field_offset(td, name);
  *(uint16_t *)((uint8_t *)row + off) = v;
}

void field_set_i32(void *row, TypeDesc *td, const char *name, int32_t v) {
  uint16_t off = type_field_offset(td, name);
  *(int32_t *)((uint8_t *)row + off) = v;
}

void field_set_f32(void *row, TypeDesc *td, const char *name, float v) {
  uint16_t off = type_field_offset(td, name);
  *(float *)((uint8_t *)row + off) = v;
}

static const char *field_type_name(uint8_t type) {
  switch (type) {
    case FIELD_U8:  return "u8";
    case FIELD_U16: return "u16";
    case FIELD_I32: return "i32";
    case FIELD_F32: return "f32";
    default:        return "???";
  }
}

void type_dump(TypeDesc *td) {
  if (!td) return;
  LOG_INFO("[dynstruct] Type '%s' (stride=%u, align=%u, fields=%u):\n",
           td->name, td->stride, td->alignment, td->field_count);

  ptrdiff_t len = shlen(td->field_map);
  for (ptrdiff_t i = 0; i < len; i++) {
    FieldDesc *fd = &td->field_map[i];
    LOG_INFO("  [%td] '%s' offset=%u size=%u type=%s count=%u\n",
             i, fd->key, fd->offset, fd->size,
             field_type_name(fd->type), fd->count);
  }
}
