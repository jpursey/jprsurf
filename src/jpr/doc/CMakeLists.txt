## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Documentation generated from the code, such as each device type's page. It
# may use any library, and nothing uses it, so it is outside the order of the
# libraries. The pages are checked in, and its test fails when one is out of
# date.

set(jpr_doc_SOURCE
  device_type_page.cc device_type_page.h
)

set(jpr_doc_DEPS
  absl::span
  absl::strings
  gb_base
  jpr_device
)

set(jpr_doc_TEST_SOURCE
  device_type_page_test.cc
)

gb_add_library(jpr_doc)
target_include_directories(jpr_doc PUBLIC "../..")

# Where the device type pages are checked in.
set(JPR_DEVICE_DOCS_DIR "${PROJECT_SOURCE_DIR}/docs/user/devices")
target_compile_definitions(jpr_doc_test PRIVATE
  "JPR_DEVICE_DOCS_DIR=\"${JPR_DEVICE_DOCS_DIR}\""
)

# Writes every device type's page to where it is checked in.

set(jpr_update_device_docs_DEPS
  absl::flat_hash_set
  jpr_doc
)

add_executable(jpr_update_device_docs update_device_docs.cc)
gb_set_target_properties(jpr_update_device_docs)

add_custom_target(update_device_docs
  COMMAND jpr_update_device_docs "${JPR_DEVICE_DOCS_DIR}"
  DEPENDS jpr_update_device_docs
  COMMENT "Writing the device type pages to docs/user/devices..."
)
