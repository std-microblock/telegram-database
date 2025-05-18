set_project("telegram-database")
set_policy("compatibility.version", "3.0")

includes("dependencies/tdlib.lua")
includes("dependencies/yalantinglibs.lua")

add_requires("tdlib", "faiss", "rocksdb", "yalantinglibs", {
    configs = { ssl = true }
}, "gtest", "benchmark", "reflect-cpp")
set_languages("c++2b")
set_warnings("all") 
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})
add_rules("mode.releasedbg")
set_toolset("ld","lld-link.exe")
-- set_policy("build.sanitizer.address", true)

target("tgdb")
    set_encodings("utf-8")

    add_defines("NOMINMAX")
    add_packages("tdlib", "faiss", "rocksdb", "yalantinglibs", "reflect-cpp")
    add_files("src/*/**.cc", "src/*.cc")

target("database_test")
    set_kind("binary")
    add_defines("NOMINMAX")
    add_files("test/database_test.cc")
    add_packages("gtest", "rocksdb", "yalantinglibs", "benchmark")
    set_languages("c++2b")