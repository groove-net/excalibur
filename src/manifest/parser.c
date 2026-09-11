#include "../../include/manifest.h"
#include <stdio.h>
#include <string.h>

int parse_lockfile(const char *filepath, LockedEntry *entries,
                   int max_entries) {
  FILE *file = fopen(filepath, "r");
  if (!file)
    return 0; // Lockfile doesn't exist yet, which is normal on first run

  int count = 0;
  char line[512];

  while (fgets(line, sizeof(line), file) && count < max_entries) {
    line[strcspn(line, "\n")] = 0;

    char *ptr = line;
    while (*ptr == ' ' || *ptr == '\t')
      ptr++;
    if (*ptr == '\0' || *ptr == '#')
      continue; // Skip comments/empty lines

    char *at_symbol = strchr(ptr, '@');
    char *equals_sign = strchr(ptr, '=');
    if (!at_symbol || !equals_sign || equals_sign < at_symbol)
      continue;

    *at_symbol = '\0';
    *equals_sign = '\0';

    char *version_str = at_symbol + 1;
    char *hash_str = equals_sign + 1;

    // Trim whitespace from hash
    while (*hash_str == ' ' || *hash_str == '\t')
      hash_str++;

    entries[count].name = INIT_FSTR(FixedString64);
    entries[count].version = INIT_FSTR(FixedString64);

    fstr_assign(&entries[count].name, ptr);
    fstr_assign(&entries[count].version, version_str);

    memset(entries[count].commit_hash, 0, sizeof(entries[count].commit_hash));
    strncpy((char *)entries[count].commit_hash, hash_str,
            sizeof(entries[count].commit_hash) - 1);

    count++;
  }

  fclose(file);
  return count;
}
