edge_add_app(heart_rate SOURCES src/heart_rate.c DEPS edge_module)
if(NOT WIN32)
    target_link_libraries(app_heart_rate PUBLIC m)
endif()
