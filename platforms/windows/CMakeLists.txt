# Общие исходники подключаются напрямую; отдельной копии клиента нет.
add_subdirectory(${CMAKE_SOURCE_DIR}/shared/client ${CMAKE_BINARY_DIR}/client)
add_subdirectory(${CMAKE_SOURCE_DIR}/shared/service ${CMAKE_BINARY_DIR}/service)
