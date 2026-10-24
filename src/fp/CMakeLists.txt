add_subdirectory(core)
add_subdirectory(input)
add_subdirectory(render)
add_subdirectory(roles)
add_subdirectory(runtime)
add_subdirectory(sim)
add_subdirectory(sprites)
add_subdirectory(tests)
add_subdirectory(ui)
add_subdirectory(world)

add_files(
    fp_hooks.h
    CONDITION OPTION_FP
)
