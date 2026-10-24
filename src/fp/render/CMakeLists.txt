add_files(
    fp_gl.cpp
    fp_gl.h
    fp_gl_state.h
    fp_renderer.cpp
    fp_renderer.h
    fp_shaders.h
    CONDITION OPTION_FP AND OPENGL_FOUND AND NOT OPTION_DEDICATED
)
