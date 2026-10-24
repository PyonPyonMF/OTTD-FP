add_test_files(
    fp_test_math.cpp
    fp_test_metric.cpp
    fp_test_rect.cpp
    fp_test_snapshot_ring.cpp
    fp_test_spectator.cpp
    fp_test_sprite_atlas.cpp
    fp_test_terrain.cpp
    fp_test_tile_journal.cpp
    CONDITION OPTION_FP
)

add_test_files(
    fp_test_sprites.cpp
    CONDITION OPTION_FP AND NOT OPTION_DEDICATED
)
