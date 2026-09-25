# Unit and integration tests (Qt Test). Run with: ctest --test-dir build --output-on-failure
function(classboard_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE classboard_lib Qt5::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Widgets tests run without a display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

classboard_test(tst_geometry)
classboard_test(tst_expression)
classboard_test(tst_document)
classboard_test(tst_serialization)
classboard_test(tst_objects)
classboard_test(tst_mathlayout)
classboard_test(tst_export)
classboard_test(tst_input)
classboard_test(tst_ui)
