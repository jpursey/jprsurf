## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Documentation generated from the code, such as each device type's page, and
# the scene's pages. It may use any library, and nothing uses it, so it is
# outside the order of the libraries. The pages are checked in, and its test
# fails when one is out of date.

set(jpr_doc_SOURCE
  device_type_page.cc device_type_page.h
  markdown.cc markdown.h
  scene_pages.cc scene_pages.h
  user_docs.cc user_docs.h
)

set(jpr_doc_DEPS
  absl::algorithm_container
  absl::flat_hash_set
  absl::span
  absl::strings
  gb_base
  jpr_device
  jpr_scene
)

set(jpr_doc_TEST_SOURCE
  device_type_page_test.cc
  scene_pages_test.cc
  user_docs_test.cc
)

gb_add_library(jpr_doc)
target_include_directories(jpr_doc PUBLIC "../..")

# Where the user docs are checked in.
set(JPR_USER_DOCS_DIR "${PROJECT_SOURCE_DIR}/docs/user")
target_compile_definitions(jpr_doc_test PRIVATE
  "JPR_USER_DOCS_DIR=\"${JPR_USER_DOCS_DIR}\""
)

# Writes every generated page of the user docs to where it is checked in.

set(jpr_update_user_docs_DEPS
  absl::flat_hash_set
  jpr_doc
)

add_executable(jpr_update_user_docs update_user_docs.cc)
gb_set_target_properties(jpr_update_user_docs)

add_custom_target(update_user_docs
  COMMAND jpr_update_user_docs "${JPR_USER_DOCS_DIR}"
  DEPENDS jpr_update_user_docs
  COMMENT "Writing the generated user docs to docs/user..."
)
