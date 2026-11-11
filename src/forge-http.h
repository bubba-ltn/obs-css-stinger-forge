#pragma once
#include <string>

// Starts the built-in local server (designer page + preset saving) on 127.0.0.1.
void forge_http_start(const std::string &designer_dir, const std::string &presets_dir);
void forge_http_stop();
int forge_http_port();            // 0 if the server is not running
std::string forge_page_url();     // file:/// URL of the designer page, stored in presets
