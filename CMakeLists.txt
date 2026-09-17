# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

cmake_minimum_required(VERSION 3.20)
project(Trepang2HeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
endif()

# MinHook (for the GetPlayerViewPoint trampoline) comes from the copy vendored
# in cameraunlock-core, which its hook manager picks up below when no `minhook`
# target has been defined. Fetching it here instead put a github.com clone on
# every clean configure - including the release job, which packages after the
# tag has already been pushed - and compiled the result straight into a DLL
# loaded in the player's process. Core's copy is v1.3.4 and needs no network;
# its one local change is recorded in its own LOCAL-CHANGES.md and in
# THIRD-PARTY-NOTICES.md.

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/tracking.cpp
    src/udp_link.cpp
    src/view_hook.cpp
    src/mod_hotkeys.cpp
    src/window_centering.cpp
    src/ads.cpp
    src/ads_pose.cpp
    src/reticle.cpp
    src/process_event_hook.cpp
    src/player_rig.cpp
    src/ue_call.cpp
    src/camera_boundary.cpp
    src/torch_aim.cpp
    src/camera_fov.cpp
    src/aim_trace.cpp
    src/kismet_trace.cpp
    src/game_state.cpp
    src/lean_trace.cpp
    src/dev_console.cpp
    src/ue_reflect.cpp
    src/ue_vm.cpp
    src/config.cpp
    src/crash_report.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
    src/builds/gdk_offsets.cpp
)

add_library(Trepang2HeadTracking SHARED ${MOD_SOURCES})

target_compile_definitions(Trepang2HeadTracking PRIVATE
    T2_HT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(Trepang2HeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(Trepang2HeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# A linker map alongside the .asi. A fault offset in a Windows Error Reporting
# entry is the only forensic this mod gets from a player's machine - there is no
# symbol server and no debugger on the far end - and without a map it names
# nothing. Costs a text file next to the binary and is not shipped.
target_link_options(Trepang2HeadTracking PRIVATE /MAP)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(Trepang2HeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Behaviour locks for the pure-function parts: the caller gate, what the sights
# do to the head pose, and the UDP link walk. Separate executables that link
# none of the hook or Windows machinery, so `pixi run test` runs with no game
# installed.
option(T2_BUILD_TESTS "Build the behaviour tests" ON)
if(T2_BUILD_TESTS)
    enable_testing()

    # The aim-down-sights layer: the verdict walk, and what the sights do to the
    # head pose. Both run with no game and no graphics device.
    add_executable(t2_ads_gate_tests
        tests/ads_gate_tests.cpp
    )
    target_include_directories(t2_ads_gate_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME t2_ads_gate_tests COMMAND t2_ads_gate_tests)

    # The UDP link: how the receiver's flags read from outside, and the walk a
    # port held by another game takes on its way to receiving. Pure - no socket,
    # so it runs the same on a machine with the port already busy.
    add_executable(t2_udp_link_tests
        tests/udp_link_tests.cpp
        src/udp_link.cpp
    )
    target_include_directories(t2_udp_link_tests PRIVATE
        src
        tests
    )
    add_test(NAME t2_udp_link_tests COMMAND t2_udp_link_tests)

    # The other half of the same scenario, against a real socket: a held port,
    # the wait, and how long after it comes free the receiver binds it and
    # delivers a pose. Holds the port with a socket of its own on an OS-assigned
    # port number, so it needs no second process and never fights whatever is on
    # 4242 on the machine running it.
    add_executable(t2_udp_recovery_tests
        tests/udp_recovery_tests.cpp
    )
    target_include_directories(t2_udp_recovery_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(t2_udp_recovery_tests PRIVATE cameraunlock ws2_32)
    add_test(NAME t2_udp_recovery_tests COMMAND t2_udp_recovery_tests)

    # The bounds guard every reflection-derived parameter frame is written
    # behind. Header-only, so it links nothing: the point is that it can be run
    # anywhere, including on a machine with no game and no graphics device.
    add_executable(t2_ue_reflect_bounds_tests
        tests/ue_reflect_bounds_tests.cpp
    )
    target_include_directories(t2_ue_reflect_bounds_tests PRIVATE
        src
        tests
    )
    add_test(NAME t2_ue_reflect_bounds_tests COMMAND t2_ue_reflect_bounds_tests)

    # Where the mark goes, and what the field of view does to it. Pure: the
    # tangent maths and the aim direction are header-only, so this runs with no
    # game, no window and no graphics device.
    add_executable(t2_projection_tests
        tests/projection_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(t2_projection_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME t2_projection_tests COMMAND t2_projection_tests)

    # The tracker-to-Unreal boundary and the caller gate, locked at the numbers
    # the UE siblings verified in game.
    add_executable(t2_boundary_tests
        tests/boundary_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(t2_boundary_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME t2_boundary_tests COMMAND t2_boundary_tests)

    # The torch's relative rotation: the same composition the camera uses,
    # multiplied, turned back out of the parent it hangs off.
    add_executable(t2_torch_aim_tests
        tests/torch_aim_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(t2_torch_aim_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME t2_torch_aim_tests COMMAND t2_torch_aim_tests)

    add_executable(t2_ads_pose_tests
        tests/ads_pose_tests.cpp
        src/ads.cpp
        src/ads_pose.cpp
    )
    target_include_directories(t2_ads_pose_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(t2_ads_pose_tests PRIVATE cameraunlock)
    add_test(NAME t2_ads_pose_tests COMMAND t2_ads_pose_tests)

    # The parameter frame both traces are cast through: what the fill puts
    # where, given a layout resolved out of the engine's reflection data. Pure
    # over a byte buffer, so it runs with no game and no physics.
    add_executable(t2_kismet_trace_tests
        tests/kismet_trace_tests.cpp
        src/kismet_trace.cpp
        src/ue_reflect.cpp
        src/builds/build_registry.cpp
        src/builds/steam_offsets.cpp
        src/builds/gdk_offsets.cpp
    )
    target_include_directories(t2_kismet_trace_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(t2_kismet_trace_tests PRIVATE cameraunlock cameraunlock_unreal)
    add_test(NAME t2_kismet_trace_tests COMMAND t2_kismet_trace_tests)

    # What HeadTracking.ini is allowed to put into the camera. Writes real INI
    # files into a temp directory and reads them back through the same
    # GetPrivateProfile* path the game uses, because that reader's parsing rules
    # are the thing being pinned.
    add_executable(t2_config_tests
        tests/config_tests.cpp
        src/config.cpp
    )
    target_include_directories(t2_config_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(t2_config_tests PRIVATE cameraunlock)
    add_test(NAME t2_config_tests COMMAND t2_config_tests)

    # The walk from an engine-supplied object index to its table slot, against a
    # synthetic object table in the test process's own memory - so the bounds
    # and the zero-divisor cases run with no game.
    add_executable(t2_object_table_tests
        tests/object_table_tests.cpp
        src/ue_call.cpp
        src/ue_reflect.cpp
        src/ue_vm.cpp
        src/builds/build_registry.cpp
        src/builds/steam_offsets.cpp
        src/builds/gdk_offsets.cpp
    )
    target_include_directories(t2_object_table_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(t2_object_table_tests PRIVATE cameraunlock cameraunlock_unreal)
    add_test(NAME t2_object_table_tests COMMAND t2_object_table_tests)

    # Whether a player's game window gets moved, and where to. Header-only
    # geometry over rects, so it runs with no game and no window of its own -
    # which is the point: the cases it covers are monitor and taskbar layouts
    # this machine does not have.
    add_executable(t2_window_placement_tests
        tests/window_placement_tests.cpp
    )
    target_include_directories(t2_window_placement_tests PRIVATE
        src
        tests
    )
    add_test(NAME t2_window_placement_tests COMMAND t2_window_placement_tests)
endif()
