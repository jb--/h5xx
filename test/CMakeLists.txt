#
# Define dynamic Boost Test linking flag if needed.
#
# We support both the static and dynamic library variants.
#
# http://www.boost.org/doc/libs/release/libs/test/doc/html/utf/user-guide/usage-variants.html
#
get_filename_component(
  suffix "${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}" EXT
)
if(suffix STREQUAL "${CMAKE_SHARED_LIBRARY_SUFFIX}")
  get_directory_property(
    flags COMPILE_DEFINITIONS
  )
  list(APPEND flags
    "BOOST_TEST_DYN_LINK"
  )
  set_directory_properties(PROPERTIES
    COMPILE_DEFINITIONS "${flags}"
  )
endif(suffix STREQUAL "${CMAKE_SHARED_LIBRARY_SUFFIX}")

foreach(module
  attribute
  dataset
  chunked_dataset
  group
)
  add_executable(test_h5xx_${module}
    ${module}.cpp
  )
  target_link_libraries(test_h5xx_${module}
    ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
    ${HDF5_CPP_LIBRARY}
    ${HDF5_LIBRARY}
    dl
    pthread
    z
  )
  add_test(h5xx/${module}
    test_h5xx_${module} --log_level=test_suite
  )
endforeach()
