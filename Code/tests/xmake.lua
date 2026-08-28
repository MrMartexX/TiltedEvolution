
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../immersive_launcher")
    add_headerfiles("**.h")
    add_files("*.cpp")
    if is_plat("windows") then
        add_files("TPTests.rc")
    end
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm")
