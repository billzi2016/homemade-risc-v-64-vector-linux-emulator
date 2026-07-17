# 文件职责：集中维护项目目标的严格编译告警，避免各模块复制或私自降低质量门槛。
# 边界：仅设置本项目目标的编译选项，不修改第三方依赖或宿主机全局编译器配置。

function(rvemu_enable_strict_warnings target_name)
    if(MSVC)
        set(rvemu_warning_flags /W4 /permissive-)
        if(RVEMU_WARNINGS_AS_ERRORS)
            list(APPEND rvemu_warning_flags /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(rvemu_warning_flags
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wnull-dereference
        )
        if(RVEMU_WARNINGS_AS_ERRORS)
            list(APPEND rvemu_warning_flags -Werror)
        endif()
    else()
        message(WARNING "当前编译器没有预设的严格告警集合：${CMAKE_CXX_COMPILER_ID}")
    endif()

    target_compile_options(${target_name} PRIVATE ${rvemu_warning_flags})
endfunction()
