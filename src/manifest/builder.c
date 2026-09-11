#include "../../include/manifest.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  FixedString64 name;
  FixedString64 version;
  uint8_t commit_hash[41];
} VisitedPackage;

static int run_command(char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0)
    return -1;

  if (pid == 0) {
    execvp(argv[0], argv);
    exit(EXIT_FAILURE);
  }

  int status;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int resolve_recursive(Dependency *dep, const char *work_dir,
                             const Dependency *root_overrides,
                             int root_override_count,
                             const LockedEntry *locked_entries,
                             int locked_count, VisitedPackage *visited,
                             int *visited_count);

static int get_git_commit_hash(const char *repo_dir, uint8_t *out_hash,
                               size_t max_len);
static int write_lockfile(const VisitedPackage *visited, int visited_count);

int fetch_and_build_manifest(const Manifest *manifest, const char *work_dir) {
  LockedEntry locked_entries[128];
  int locked_count = parse_lockfile("excalibur.lock", locked_entries, 128);

  if (locked_count > 0) {
    printf("🔒 Loaded %d locked dependencies from excalibur.lock\n",
           locked_count);
  }

  VisitedPackage visited[128];
  int visited_count = 0;

  // Process all direct dependencies from the root manifest
  for (uint32_t i = 0; i < manifest->direct_count; i++) {
    Dependency dep = manifest->direct_deps[i];
    if (resolve_recursive(&dep, work_dir, manifest->overrides,
                          manifest->override_count, locked_entries,
                          locked_count, visited, &visited_count) != 0) {
      return -1;
    }
  }

  // Write the lockfile after all packages are successfully verified and built
  if (write_lockfile(visited, visited_count) != 0) {
    return -1;
  }

  // Atomically commit staged archives and headers to host lib/ and include/
  system("mkdir -p lib include");
  char batch_cmd[512];
  snprintf(batch_cmd, sizeof(batch_cmd), "cp %s/*.a lib/ 2>/dev/null || true",
           work_dir);
  system(batch_cmd);
  snprintf(batch_cmd, sizeof(batch_cmd),
           "cp %s/*.h include/ 2>/dev/null || true", work_dir);
  system(batch_cmd);

  return 0;
}

static int resolve_recursive(Dependency *dep, const char *work_dir,
                             const Dependency *root_overrides,
                             int root_override_count,
                             const LockedEntry *locked_entries,
                             int locked_count, VisitedPackage *visited,
                             int *visited_count) {
  // 1. Check Root Overrides First (Supremacy Rule)
  for (int i = 0; i < root_override_count; i++) {
    if (strcmp(root_overrides[i].name.data, dep->name.data) == 0) {
      if (strcmp(dep->version.data, root_overrides[i].version.data) != 0) {
        printf("ℹ️ Overriding transitive version for '%s': using root-pinned %s "
               "instead of %s\n",
               dep->name.data, root_overrides[i].version.data,
               dep->version.data);
        fstr_assign(&dep->version, root_overrides[i].version.data);
        fstr_assign(&dep->raw_url, root_overrides[i].raw_url.data);
      }
      break;
    }
  }

  // 2. Check Visited Registry for Conflicts
  for (int i = 0; i < *visited_count; i++) {
    if (strcmp(visited[i].name.data, dep->name.data) == 0) {
      if (strcmp(visited[i].version.data, dep->version.data) == 0) {
        return 0; // Already resolved with matching version
      } else {
        fprintf(stderr, "\n❌ Unresolved Dependency Conflict for '%s':\n",
                dep->name.data);
        fprintf(stderr, "   - Already resolved version: %s\n",
                visited[i].version.data);
        fprintf(stderr, "   - Conflicting version: %s\n", dep->version.data);
        fprintf(stderr,
                "   -> Action: Add 'override %s@<version>' to your root "
                "excalibur.txt.\n\n",
                dep->raw_url.data);
        return -1;
      }
    }
  }

  // 3. Register Package (capture index for later commit hash update)
  int reg_index = -1;
  if (*visited_count < 128) {
    reg_index = *visited_count;
    visited[reg_index].name = INIT_FSTR(FixedString64);
    visited[reg_index].version = INIT_FSTR(FixedString64);
    fstr_assign(&visited[reg_index].name, dep->name.data);
    fstr_assign(&visited[reg_index].version, dep->version.data);
    memset(visited[reg_index].commit_hash, 0,
           sizeof(visited[reg_index].commit_hash));
    (*visited_count)++;
  }

  // Check if a locked commit hash exists for this package
  const char *target_commit = NULL;
  for (int i = 0; i < locked_count; i++) {
    if (strcmp(locked_entries[i].name.data, dep->name.data) == 0) {
      target_commit = (char *)locked_entries[i].commit_hash;
      break;
    }
  }

  // 4. Fetch via Git
  char cmd[512];
  FixedString256 dep_dir = INIT_FSTR(FixedString256);
  snprintf(cmd, sizeof(cmd), "%s/%s", work_dir, dep->name.data);
  fstr_assign(&dep_dir, cmd);

  if (target_commit) {
    printf("Fetching %s@%s (locked to %.7s)...\n", dep->name.data,
           dep->version.data, target_commit);
  } else {
    printf("Fetching %s@%s...\n", dep->name.data, dep->version.data);
  }

  FixedString256 full_url = INIT_FSTR(FixedString256);
  fstr_assign(&full_url, "https://");
  fstr_append(&full_url, dep->raw_url.data);

  // Dynamic clone arguments to support untagged repos / default branches
  char *clone_args[10];
  int arg_idx = 0;
  clone_args[arg_idx++] = "git";
  clone_args[arg_idx++] = "clone";
  clone_args[arg_idx++] = "--quiet";
  clone_args[arg_idx++] = "--depth";
  clone_args[arg_idx++] = "1";

  if (dep->version.data[0] != '\0') {
    clone_args[arg_idx++] = "--branch";
    clone_args[arg_idx++] = dep->version.data;
  }

  clone_args[arg_idx++] = full_url.data;
  clone_args[arg_idx++] = dep_dir.data;
  clone_args[arg_idx] = NULL;

  if (run_command(clone_args) != 0) {
    fprintf(stderr, "Failed to fetch %s\n", dep->name.data);
    return -1;
  }

  // If locked, force checkout to the exact commit hash for absolute determinism
  if (target_commit) {
    char *const checkout_args[] = {"git",      "-C",      dep_dir.data,
                                   "checkout", "--quiet", (char *)target_commit,
                                   NULL};
    if (run_command(checkout_args) != 0) {
      fprintf(stderr,
              "Warning: Failed to check out locked commit %.7s for %s. Falling "
              "back to branch head.\n",
              target_commit, dep->name.data);
    }
  }

  // Capture commit hash and update the registered entry
  uint8_t commit_hash[41] = {0};
  if (target_commit) {
    strncpy((char *)commit_hash, target_commit, sizeof(commit_hash) - 1);
  } else {
    get_git_commit_hash(dep_dir.data, commit_hash, sizeof(commit_hash));
  }

  if (reg_index >= 0) {
    memcpy(visited[reg_index].commit_hash, commit_hash,
           sizeof(visited[reg_index].commit_hash));
  }

  // 5. Check for Nested Manifest
  char nested_manifest[512];
  snprintf(nested_manifest, sizeof(nested_manifest), "%s/excalibur.txt",
           dep_dir.data);

  FILE *mf = fopen(nested_manifest, "r");
  if (mf) {
    fclose(mf);
    printf("-> Found nested manifest in %s. Resolving transitive "
           "dependencies...\n",
           dep->name.data);

    Manifest nested_m;
    if (parse_manifest(nested_manifest, &nested_m) == 0) {
      for (uint32_t i = 0; i < nested_m.direct_count; i++) {
        if (resolve_recursive(&nested_m.direct_deps[i], work_dir,
                              root_overrides, root_override_count,
                              locked_entries, locked_count, visited,
                              visited_count) != 0) {
          return -1;
        }
      }
    }
  }

  // 6. Strict Excalibur Package Contract Validation
  FixedString256 src_dir = INIT_FSTR(FixedString256);
  FixedString256 inc_dir = INIT_FSTR(FixedString256);
  snprintf(cmd, sizeof(cmd), "%s/src", dep_dir.data);
  fstr_assign(&src_dir, cmd);
  snprintf(cmd, sizeof(cmd), "%s/include", dep_dir.data);
  fstr_assign(&inc_dir, cmd);

  // Rule 1: Must have a src/ directory
  if (access(src_dir.data, F_OK) != 0) {
    fprintf(stderr,
            "\n❌ Contract Violation: '%s' is missing a 'src/' directory.\n",
            dep->name.data);
    return -1;
  }

  // Rule 2: Must have an include/ directory
  if (access(inc_dir.data, F_OK) != 0) {
    fprintf(
        stderr,
        "\n❌ Contract Violation: '%s' is missing an 'include/' directory.\n",
        dep->name.data);
    return -1;
  }

  // Rule 3: Must have include/<library_name>.h
  char expected_header[512];
  snprintf(expected_header, sizeof(expected_header), "%s/%s.h", inc_dir.data,
           dep->name.data);
  if (access(expected_header, F_OK) != 0) {
    fprintf(stderr,
            "\n❌ Contract Violation: '%s' is missing its primary header "
            "'%s.h' in include/.\n",
            dep->name.data, dep->name.data);
    fprintf(stderr, "   Expected: %s\n", expected_header);
    return -1;
  }

  // 7. Native Compilation Paths & Archiving
  DIR *d = opendir(src_dir.data);
  if (!d) {
    fprintf(stderr, "Error: Failed to open %s.\n", src_dir.data);
    return -1;
  }

  struct dirent *dir;
  char obj_list[4096] = {0};
  int compiled_files = 0;

  printf("Compiling %s...\n", dep->name.data);
  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, ".c")) {
      compiled_files++;
      char obj_path[512];
      snprintf(obj_path, sizeof(obj_path), "%s/%s.o", dep_dir.data,
               dir->d_name);

      char src_file[512];
      snprintf(src_file, sizeof(src_file), "%s/%s", src_dir.data, dir->d_name);

      char inc_flag[512];
      snprintf(inc_flag, sizeof(inc_flag), "-I%s", inc_dir.data);

      // Excalibur controls the compilation flags natively
      char *const cc_args[] = {"clang", "-c",     src_file, inc_flag,
                               "-o",    obj_path, NULL};
      if (run_command(cc_args) != 0) {
        closedir(d);
        return -1;
      }

      char append_buf[512];
      snprintf(append_buf, sizeof(append_buf), "%s ", obj_path);
      strcat(obj_list, append_buf);
    }
  }
  closedir(d);

  // Rule 4: Must have at least one .c file in src/
  if (compiled_files == 0) {
    fprintf(stderr,
            "\n❌ Contract Violation: '%s' has no .c files in its src/ "
            "directory.\n",
            dep->name.data);
    return -1;
  }

  printf("Archiving %s.a...\n", dep->name.data);
  char archive_path[512];
  snprintf(archive_path, sizeof(archive_path), "%s/%s.a", dep_dir.data,
           dep->name.data);

  snprintf(cmd, sizeof(cmd), "ar rcs %s %s", archive_path, obj_list);
  if (system(cmd) != 0)
    return -1;

  printf("Staging artifacts for %s...\n", dep->name.data);
  snprintf(cmd, sizeof(cmd), "cp %s %s/", archive_path, work_dir);
  system(cmd);

  snprintf(cmd, sizeof(cmd), "cp -r %s/*.h %s/ 2>/dev/null || true",
           inc_dir.data, work_dir);
  system(cmd);

  return 0;
}

static int get_git_commit_hash(const char *repo_dir, uint8_t *out_hash,
                               size_t max_len) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "git -C %s rev-parse HEAD", repo_dir);

  FILE *fp = popen(cmd, "r");
  if (!fp)
    return -1;

  if (fgets((char *)out_hash, max_len, fp) != NULL) {
    out_hash[strcspn((char *)out_hash, "\n")] = 0; // Strip trailing newline
    pclose(fp);
    return 0;
  }

  pclose(fp);
  return -1;
}

static int write_lockfile(const VisitedPackage *visited, int visited_count) {
  FILE *f = fopen("excalibur.lock", "w");
  if (!f) {
    fprintf(stderr, "Warning: Failed to create excalibur.lock\n");
    return -1;
  }

  fprintf(
      f, "# This file is automatically generated by Excalibur. Do not edit.\n");
  for (int i = 0; i < visited_count; i++) {
    fprintf(f, "%s@%s = %s\n", visited[i].name.data, visited[i].version.data,
            (char *)visited[i].commit_hash);
  }

  fclose(f);
  printf("🔒 Generated excalibur.lock successfully.\n");
  return 0;
}
