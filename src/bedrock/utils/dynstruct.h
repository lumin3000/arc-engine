
#ifndef DYNSTRUCT_H
#define DYNSTRUCT_H

#include <stdint.h>

enum {
  FIELD_U8  = 0,
  FIELD_U16 = 1,
  FIELD_I32 = 2,
  FIELD_F32 = 3,
};

#define FIELD_TYPE_COUNT 4

typedef struct {
  char    *key;
  uint16_t offset;
  uint16_t size;
  uint8_t  type;
  uint8_t  count;
} FieldDesc;

typedef struct {
  char       name[32];
  FieldDesc *field_map;
  uint16_t   stride;
  uint16_t   field_count;
  uint16_t   alignment;
} TypeDesc;

void type_define(TypeDesc *td, const char *name,
                 const char **field_names, const uint8_t *field_types,
                 const uint8_t *field_counts, int count);

uint16_t type_field_offset(TypeDesc *td, const char *name);

void *field_ptr(void *row, TypeDesc *td, const char *name);

uint8_t  field_get_u8 (const void *row, TypeDesc *td, const char *name);
uint16_t field_get_u16(const void *row, TypeDesc *td, const char *name);
int32_t  field_get_i32(const void *row, TypeDesc *td, const char *name);
float    field_get_f32(const void *row, TypeDesc *td, const char *name);

void field_set_u8 (void *row, TypeDesc *td, const char *name, uint8_t  v);
void field_set_u16(void *row, TypeDesc *td, const char *name, uint16_t v);
void field_set_i32(void *row, TypeDesc *td, const char *name, int32_t  v);
void field_set_f32(void *row, TypeDesc *td, const char *name, float    v);

void type_dump(TypeDesc *td);

#endif
