add_subdirectory(runner)

# Unit tests for include/illixr/* headers get registered here directly,
# via add_illixr_unit_test(include SOURCES ...), once they're migrated.

add_illixr_unit_test(include
                     SOURCES includes/test_phonebook.cpp
                     includes/test_cpu_timer.cpp
                     includes/test_dynamic_lib.cpp
                     includes/test_error_util.cpp
                     includes/test_global_module_defs.cpp
                     includes/test_managed_thread.cpp
                     includes/test_record_logger.cpp
                     includes/test_relative_clock.cpp
                     includes/test_math_util.cpp
                     includes/test_data_loading.cpp
                     includes/test_plugin.cpp
                     includes/test_runge-kutta.cpp
                     includes/test_stoplight.cpp
                     includes/test_string_utils.cpp
                     includes/test_switchboard.cpp
                     includes/test_threadloop.cpp
                     includes/iterators/test_data_iterator.cpp
                     includes/iterators/test_csv_iterator.cpp
                     includes/iterators/test_ssv_iterator.cpp
                     INCLUDES phonebook.hpp
                     cpu_timer.hpp
                     dynamic_lib.hpp
                     error_util.hpp
                     global_module_defs.hpp
                     managed_thread.hpp
                     record_logger.hpp
                     relative_clock.hpp
                     math_util.hpp
                     data_loading.hpp
                     plugin.hpp
                     runge-kutta.hpp
                     switchboard.hpp
                     threadloop.hpp
                     iterators/data_iterator.hpp
                     iterators/csv_iterator.hpp
                     iterators/ssv_iterator.hpp
)
