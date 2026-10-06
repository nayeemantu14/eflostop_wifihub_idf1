# The setup portal (the Wi-Fi manager carried in the tree since 2.1.4 WP-V). ESP-IDF 5.x only.
#
# LOCAL PATCH (2.1.4 C7): the page's text assets (index.html, code.js, style.css) are gzipped at
# build time by tools/gz_asset.py and embedded gzipped; http_app.c sends them with
# "Content-Encoding: gzip". The PNG is embedded as it is (already compressed). The .gz files are
# build outputs (in this component's build folder), never kept in the tree, so an edited asset is
# always gzipped again: each depends on its source and on the tool. The tool runs with the
# build's own Python (ESP-IDF's environment) and fails the build, leaving no output, if anything
# goes wrong: an image is never built with stale or missing assets. src/compress.bat is gone.
idf_component_register(SRC_DIRS src
    REQUIRES esp_wifi log nvs_flash mdns wpa_supplicant lwip esp_http_server
    INCLUDE_DIRS src
    EMBED_FILES src/Watts_Logo.png)

# Below the registration: the build's first pass (the components' requirements) returns at the
# call above, so these commands run only in the real configure.
idf_build_get_property(python PYTHON)
if(NOT python)
    message(FATAL_ERROR "wifi_portal: the build's PYTHON property is empty - the setup page cannot be gzipped")
endif()
set(gz_tool "${CMAKE_CURRENT_LIST_DIR}/tools/gz_asset.py")
if(NOT EXISTS "${gz_tool}")
    message(FATAL_ERROR "wifi_portal: ${gz_tool} is missing - the setup page cannot be gzipped")
endif()

# Each asset becomes <build>/esp-idf/wifi_portal/<name>.gz, embedded as _binary_<name>_gz_start
# and _binary_<name>_gz_end (for example _binary_index_html_gz_start).
foreach(asset index.html code.js style.css)
    set(asset_src "${CMAKE_CURRENT_LIST_DIR}/src/${asset}")
    set(asset_gz "${CMAKE_CURRENT_BINARY_DIR}/${asset}.gz")
    add_custom_command(OUTPUT "${asset_gz}"
        COMMAND "${python}" "${gz_tool}" "${asset_src}" "${asset_gz}"
        DEPENDS "${asset_src}" "${gz_tool}"
        COMMENT "Gzipping the setup page's ${asset}"
        VERBATIM)
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${asset_gz}")
    target_add_binary_data(${COMPONENT_LIB} "${asset_gz}" BINARY)
endforeach()
