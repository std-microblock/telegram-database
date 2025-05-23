-- thx to @star-hengxing
package("tdlib")
    set_homepage("https://core.telegram.org/tdlib")
    set_description("Cross-platform library for building Telegram clients")
    set_license("BSL-1.0")

    add_urls("https://github.com/tdlib/td.git")

    add_versions("2025.05.04", "34c390f9afe074071e01c623e42adfbd17e350ab")

    add_configs("jni", {description = "Enable JNI-compatible TDLib API.", default = false, type = "boolean"})

    add_deps("cmake", "gperf")
    add_deps("zlib", "openssl3")

    add_syslinks("psapi", "normaliz")

    on_install(function (package)
        if not package:config("shared") then
            package:add("defines", "TDJSON_STATIC_DEFINE")
        end

        io.replace("CMakeLists.txt", "add_subdirectory(benchmark)", "", {plain = true})
        io.replace("CMake/TdSetUpCompiler.cmake", "HAVE_STD17", "true", {plain = true})

        
        local openssl = package:dep("openssl3")
        if not openssl:is_system() then
            table.insert(configs, "-DOPENSSL_ROOT_DIR=" .. openssl:installdir())
        end
        
        local configs = {"-DBUILD_TESTING=OFF"}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DTD_INSTALL_SHARED_LIBRARIES=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DTD_INSTALL_STATIC_LIBRARIES=" .. (package:config("shared") and "OFF" or "ON"))
        table.insert(configs, "-DTD_ENABLE_LTO=" .. (package:config("lto") and "ON" or "OFF"))
        table.insert(configs, "-DTD_ENABLE_JNI=" .. (package:config("jni") and "ON" or "OFF"))
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("td_create_client_id", {includes = "td/telegram/td_json_client.h"}))
    end)
