set_project("telegram-database")
set_policy("compatibility.version", "3.0")

includes("dependencies/tdlib.lua")
includes("dependencies/yalantinglibs.lua")

option("test", {default = false})

add_requires("tdlib", "faiss", "rocksdb", "yalantinglibs", "reflect-cpp", "utfcpp")
if has_config("test") then
    add_requires("gtest", "benchmark")
end

set_languages("c++2b")
set_warnings("all")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})
add_rules("mode.releasedbg")

if is_plat("windows") then
    add_defines("NOMINMAX")
end

target("tgdb")
    set_kind("binary")
    set_encodings("utf-8")
    add_packages("tdlib", "faiss", "rocksdb", "yalantinglibs", "reflect-cpp", "utfcpp")
    add_files("src/*.cc", "src/*/**.cc") 

target("database_test")
    set_default(false)
    set_kind("binary")
    add_files("test/database_test.cc")
    add_packages("gtest", "rocksdb", "yalantinglibs", "benchmark")
    add_tests("default")

target("vector_db_test")
    set_default(false)
    set_kind("binary")
    add_files("test/vector_db_test.cc", "src/database/faiss_vector_db.cc") 
    add_includedirs("src/database") 
    add_packages("gtest", "faiss", "yalantinglibs") 
    add_tests("default")
