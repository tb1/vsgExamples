set(SOURCES
    vsgimgui.cpp
)

add_executable(vsgimgui ${SOURCES})

target_link_libraries(vsgimgui vsgImGui::vsgImGui vsg::vsg)

if (TARGET vsgXchange::vsgXchange)
    target_compile_definitions(vsgimgui PRIVATE vsgXchange_FOUND)
    target_link_libraries(vsgimgui vsgXchange::vsgXchange)
endif()

install(TARGETS vsgimgui RUNTIME DESTINATION bin)
