#ifndef EXCALIBUR_H
#define EXCALIBUR_H

#include "custring.h"

#define MAX_DEPS 64

typedef struct {
  FixedString256 raw_url; // e.g., github.com/groove-net/liblog
  FixedString64 version;  // e.g., v1.2.0
  FixedString64 name;     // e.g., liblog
} Dependency;

int parse_manifest(const char *filepath, Dependency *deps, int max_deps);
int fetch_and_build(Dependency *dep, const char *work_dir);

#endif // EXCALIBUR_H
