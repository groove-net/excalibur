#include "../../include/manifest.h"
#include "builder.c"
#include "parser.c"
#include <stdio.h>
#include <string.h>

int parse_manifest(const char *filepath, Manifest *out_manifest) {
  FILE *file = fopen(filepath, "r");
  if (!file)
    return -1;

  out_manifest->direct_count = 0;
  out_manifest->override_count = 0;

  char line[512];
  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\n")] = 0;

    char *ptr = line;
    while (*ptr == ' ' || *ptr == '\t')
      ptr++;
    if (*ptr == '\0' || *ptr == '#')
      continue; // Skip comments and empty lines

    int is_override = 0;
    if (strncmp(ptr, "override ", 9) == 0) {
      is_override = 1;
      ptr += 9;
      while (*ptr == ' ' || *ptr == '\t')
        ptr++;
    }

    char *at_symbol = strchr(ptr, '@');

    Dependency *target_dep;
    if (is_override) {
      if (out_manifest->override_count >= MAX_DEPS)
        continue;
      target_dep = &out_manifest->overrides[out_manifest->override_count++];
    } else {
      if (out_manifest->direct_count >= MAX_DEPS)
        continue;
      target_dep = &out_manifest->direct_deps[out_manifest->direct_count++];
    }

    target_dep->raw_url = INIT_FSTR(FixedString256);
    target_dep->version = INIT_FSTR(FixedString64);
    target_dep->name = INIT_FSTR(FixedString64);

    if (at_symbol) {
      *at_symbol = '\0';
      fstr_assign(&target_dep->raw_url, ptr);
      fstr_assign(&target_dep->version, at_symbol + 1);
    } else {
      fstr_assign(&target_dep->raw_url, ptr);
      fstr_assign(&target_dep->version,
                  ""); // Default to empty (repo's default branch)
    }

    char *last_slash = strrchr(target_dep->raw_url.data, '/');
    if (last_slash) {
      fstr_assign(&target_dep->name, last_slash + 1);
    } else {
      fstr_assign(&target_dep->name, target_dep->raw_url.data);
    }
  }

  fclose(file);
  return 0;
}
