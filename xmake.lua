set_project("telegram-database")
set_policy("compatibility.version", "3.0")

includes("dependencies/tdlib.lua")
includes("dependencies/yalantinglibs.lua")

add_requires("tdlib", "faiss", "rocksdb", "yalantinglibs", {
    configs = { ssl = true }
}, "gtest", "benchmark", "reflect-cpp", "utfcpp")
set_languages("c++2b")
set_warnings("all") 
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})
add_rules("mode.releasedbg")
-- set_toolset("ld","lld-link.exe")


target("tgdb")
    set_kind("binary")
    set_encodings("utf-8")
    add_defines("NOMINMAX")
    add_packages("tdlib", "faiss", "rocksdb", "yalantinglibs", "reflect-cpp", "utfcpp")
    add_files("src/*.cc", "src/*/**.cc") 
    set_languages("c++2b")

target("database_test")
    set_kind("binary")
    add_defines("NOMINMAX")
    add_files("test/database_test.cc")
    add_packages("gtest", "rocksdb", "yalantinglibs", "benchmark")
    set_languages("c++2b")

target("vector_db_test")
    set_kind("binary")
    add_defines("NOMINMAX")
    add_files("test/vector_db_test.cc", "src/database/faiss_vector_db.cc") 
    add_includedirs("src/database") 
    add_packages("gtest", "faiss", "yalantinglibs") 
    set_languages("c++2b")
