
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../client", "../immersive_launcher")
    add_headerfiles("**.h")
    add_files("*.cpp")
    if is_plat("windows") then
        add_files(
            "TPTests.rc",
            "../client/Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeModuleLease.cpp",
            "../client/Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeOwner.cpp",
            "../client/Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeResolver.cpp")
        add_syslinks("kernel32", "bcrypt")
    end
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm",
        "entt")
