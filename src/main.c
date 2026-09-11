/*
 ============================================================================
 Name        : excalibur
 Version     : 1.0
 Description : An simple and opinionated package manager for C
 ============================================================================
*/

#include "../include/manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int remove_from_file(const char *filepath, const char *pkg_name);

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: excalibur <command> [args]\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "install") == 0) {
    Manifest manifest;
    // If excalibur.txt exists, parse it. If not, start with an empty manifest.
    FILE *check_file = fopen("excalibur.txt", "r");
    if (check_file) {
      fclose(check_file);
      if (parse_manifest("excalibur.txt", &manifest) != 0) {
        fprintf(stderr, "Error: Failed to parse excalibur.txt\n");
        return EXIT_FAILURE;
      }
    } else {
      manifest.direct_count = 0;
      manifest.override_count = 0;
    }

    // If an ad-hoc URL/spec is provided on the CLI (e.g., excalibur install
    // <url>)
    if (argc >= 3) {
      const char *pkg_spec = argv[2];

      // Check if it's already in the manifest in-memory
      int already_exists = 0;
      for (uint32_t i = 0; i < manifest.direct_count; i++) {
        char full_spec[512];
        snprintf(full_spec, sizeof(full_spec), "%s@%s",
                 manifest.direct_deps[i].raw_url.data,
                 manifest.direct_deps[i].version.data);
        if (strcmp(full_spec, pkg_spec) == 0 ||
            strcmp(manifest.direct_deps[i].raw_url.data, pkg_spec) == 0) {
          already_exists = 1;
          break;
        }
      }

      if (!already_exists && manifest.direct_count < MAX_DEPS) {
        Dependency *new_dep = &manifest.direct_deps[manifest.direct_count++];
        new_dep->raw_url = INIT_FSTR(FixedString256);
        new_dep->version = INIT_FSTR(FixedString64);
        new_dep->name = INIT_FSTR(FixedString64);

        char *at_symbol = strchr(pkg_spec, '@');
        if (at_symbol) {
          *at_symbol = '\0';
          fstr_assign(&new_dep->raw_url, pkg_spec);
          fstr_assign(&new_dep->version, at_symbol + 1);
          *at_symbol = '@'; // Restore string just in case
        } else {
          fstr_assign(&new_dep->raw_url, pkg_spec);
          fstr_assign(&new_dep->version, "");
        }

        char *last_slash = strrchr(new_dep->raw_url.data, '/');
        if (last_slash) {
          fstr_assign(&new_dep->name, last_slash + 1);
        } else {
          fstr_assign(&new_dep->name, new_dep->raw_url.data);
        }
      }
    }

    if (manifest.direct_count == 0 && manifest.override_count == 0) {
      printf("No dependencies found in excalibur.txt or command line.\n");
      return EXIT_SUCCESS;
    }

    const char *tmp_workdir = ".excal_tmp";
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "mkdir -p %s", tmp_workdir);
    system(cmd);

    if (fetch_and_build_manifest(&manifest, tmp_workdir) != 0) {
      fprintf(stderr, "\n❌ Failed to install dependencies.\n");
      snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_workdir);
      system(cmd);
      return EXIT_FAILURE;
    }

    // Build succeeded! Now safely persist the ad-hoc package to excalibur.txt
    // if provided
    if (argc >= 3) {
      const char *pkg_spec = argv[2];
      FILE *f = fopen("excalibur.txt", "a");
      if (f) {
        fprintf(f, "%s\n", pkg_spec);
        fclose(f);
        printf("➕ Added '%s' to excalibur.txt\n", pkg_spec);
      }
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_workdir);
    system(cmd);

    printf("\n✅ All dependencies and overrides installed successfully.\n");
    return EXIT_SUCCESS;
  } else if (strcmp(argv[1], "uninstall") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: excalibur uninstall <package-name>\n");
      return EXIT_FAILURE;
    }

    const char *pkg_name = argv[2];
    char path[512];

    // Remove compiled static library
    snprintf(path, sizeof(path), "lib/%s.a", pkg_name);
    if (remove(path) == 0) {
      printf("🗑️ Removed archive lib/%s.a\n", pkg_name);
    } else {
      printf("ℹ️ No static library found for '%s'\n", pkg_name);
    }

    // Remove library header
    snprintf(path, sizeof(path), "include/%s.h", pkg_name);
    if (remove(path) == 0) {
      printf("🗑️ Removed header include/%s.a\n", pkg_name);
    } else {
      printf("ℹ️ No header found for '%s'\n", pkg_name);
    }

    // Remove from excalibur.txt (handles both direct and override
    // declarations)
    remove_from_file("excalibur.txt", pkg_name);

    // Purge from excalibur.lock
    remove_from_file("excalibur.lock", pkg_name);

    printf("✅ Successfully uninstalled '%s'.\n", pkg_name);
    return EXIT_SUCCESS;
  }

  fprintf(stderr, "Unknown command: %s\n", argv[1]);
  return EXIT_FAILURE;
}

static int remove_from_file(const char *filepath, const char *pkg_name) {
  FILE *f = fopen(filepath, "r");
  if (!f)
    return 0; // File doesn't exist, nothing to remove

  char temp_path[512];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", filepath);
  FILE *temp = fopen(temp_path, "w");
  if (!temp) {
    fclose(f);
    return -1;
  }

  char line[512];
  int removed = 0;
  size_t pkg_len = strlen(pkg_name);

  while (fgets(line, sizeof(line), f)) {
    int should_remove = 0;
    char *match = line;

    // Search for the package name in the line
    while ((match = strstr(match, pkg_name)) != NULL) {
      // 1. Check what comes BEFORE the match (must be start of line or '/')
      int valid_start = (match == line) || (*(match - 1) == '/');

      // 2. Check what comes AFTER the match (must be '@', newline, or end of
      // string)
      char next_char = match[pkg_len];
      int valid_end = (next_char == '@' || next_char == '\n' ||
                       next_char == '\r' || next_char == '\0');

      // If both boundaries are valid, this is our exact package
      if (valid_start && valid_end) {
        should_remove = 1;
        break;
      }
      match++; // Advance to check the rest of the line just in case
    }

    if (should_remove) {
      removed = 1;
      continue; // Skip writing this line to the temp file
    }

    fputs(line, temp);
  }

  fclose(f);
  fclose(temp);

  if (removed) {
    rename(temp_path, filepath);
  } else {
    remove(temp_path);
  }
  return 0;
}
