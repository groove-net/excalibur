#ifndef LIBMANIFEST_H
#define LIBMANIFEST_H

#include "custring.h"
#include <stdint.h>

#define MAX_DEPS 64

typedef struct {
  FixedString256 raw_url;
  FixedString64 version;
  FixedString64 name;
} Dependency;

typedef struct {
  Dependency direct_deps[MAX_DEPS];
  uint32_t direct_count;
  Dependency overrides[MAX_DEPS];
  uint32_t override_count;
} Manifest;

typedef struct {
  FixedString64 name;
  FixedString64 version;
  uint8_t commit_hash[41];
} LockedEntry;

int parse_manifest(const char *filepath, Manifest *out_manifest);
int fetch_and_build_manifest(const Manifest *manifest, const char *work_dir);
int parse_lockfile(const char *filepath, LockedEntry *entries, int max_entries);

#endif
