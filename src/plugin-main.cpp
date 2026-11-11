/*
OBS CSS Stinger Forge for OBS
Copyright (C) 2026 Love Thy Nerd

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// Each preset saved by OBS CSS Stinger Forge (a small .json file in
//   <OBS config>/plugin_config/forge-transitions/presets/)
// becomes its own transition type, "Forge: <name>". When the transition runs it
//   1. plays the Forge page (CSS animation, transparent background) in a private
//      obs-browser source drawn over the scene change, and
//   2. cuts from scene A to scene B at the moment the animation fully covers the screen.
// No video files are involved.

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr int DEFAULT_SYNC_MS = 60;

struct Preset {
	std::string id;      // file-safe slug, also the source type id suffix
	std::string type_id; // "forge_tr_<id>" (kept alive: libobs stores the pointer)
	std::string name;
	std::string display; // "Forge: <name>"
	std::string page;    // file:/// or http://127.0.0.1 URL of transition-forge.html
	std::string query;   // "?t=iris&h=300&..." (no d / mode / ctl)
	uint32_t duration = 1400;
	bool removed = false;
};

struct ForgeTr;

std::mutex g_mx;
std::map<std::string, Preset *> g_presets;
std::vector<ForgeTr *> g_instances;
std::atomic<bool> g_alive{false};
std::atomic<bool> g_stop{false};
std::thread g_watcher;
std::string g_presets_dir;

struct ForgeTr {
	obs_source_t *self = nullptr;
	obs_source_t *browser = nullptr;
	Preset *preset = nullptr;
	int sync_ms = DEFAULT_SYNC_MS;
	bool running = false;
	uint32_t bw = 0, bh = 0;
	unsigned reload = 0;
	std::string applied; // url + size last sent to the browser source

	uint32_t total_ms() const { return preset ? std::max<uint32_t>(300, preset->duration) : 600; }
	uint32_t delay_ms() const
	{
		const uint32_t d = total_ms();
		return (uint32_t)std::clamp<int>(sync_ms, 0, (int)(d / 3));
	}
	// Fraction of the transition at which the overlay fully covers the screen.
	float cut_t() const
	{
		const float d = (float)total_ms(), dl = (float)delay_ms();
		return (dl + (d - dl) * 0.5f) / d;
	}

	std::string url() const
	{
		if (!preset)
			return "about:blank";
		std::string u = preset->page + preset->query;
		u += "&d=" + std::to_string(total_ms() - delay_ms());
		u += "&mode=full&ctl=obs";
		if (reload)
			u += "&_r=" + std::to_string(reload);
		return u;
	}

	void make_browser()
	{
		struct obs_video_info ovi = {};
		if (obs_get_video_info(&ovi)) {
			bw = ovi.base_width;
			bh = ovi.base_height;
		}
		if (!bw || !bh) {
			bw = 1920;
			bh = 1080;
		}
		applied = url() + "|" + std::to_string(bw) + "x" + std::to_string(bh);
		obs_data_t *s = obs_data_create();
		obs_data_set_string(s, "url", url().c_str());
		obs_data_set_int(s, "width", bw);
		obs_data_set_int(s, "height", bh);
		obs_data_set_bool(s, "is_local_file", false);
		obs_data_set_bool(s, "shutdown", false);
		obs_data_set_bool(s, "restart_when_active", false);
		obs_data_set_bool(s, "reroute_audio", false);
		obs_data_set_bool(s, "fps_custom", true);
		obs_data_set_int(s, "fps", 60);
		obs_data_set_string(s, "css", "body{background-color:rgba(0,0,0,0);margin:0;overflow:hidden;}");
		browser = obs_source_create_private("browser_source", "forge-overlay", s);
		obs_data_release(s);
		if (!browser)
			obs_log(LOG_WARNING, "obs-browser is not available: Forge transitions will cut without the overlay");
	}

	void apply_url()
	{
		if (!browser)
			return;
		const std::string key = url() + "|" + std::to_string(bw) + "x" + std::to_string(bh);
		if (key == applied)
			return;
		applied = key;
		obs_data_t *s = obs_data_create();
		obs_data_set_string(s, "url", url().c_str());
		obs_data_set_int(s, "width", bw);
		obs_data_set_int(s, "height", bh);
		obs_source_update(browser, s);
		obs_data_release(s);
	}
};

/* ------------------------------------------------------------------ source callbacks */

const char *forge_get_name(void *type_data)
{
	return type_data ? ((Preset *)type_data)->display.c_str() : "Forge";
}

void forge_get_defaults(void *type_data, obs_data_t *settings)
{
	UNUSED_PARAMETER(type_data);
	obs_data_set_default_int(settings, "sync_ms", DEFAULT_SYNC_MS);
}

void *forge_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new ForgeTr();
	f->self = source;
	f->preset = (Preset *)obs_source_get_type_data(source);
	f->sync_ms = (int)obs_data_get_int(settings, "sync_ms");
	f->make_browser();
	std::lock_guard<std::mutex> lk(g_mx);
	g_instances.push_back(f);
	return f;
}

void forge_destroy(void *data)
{
	auto *f = (ForgeTr *)data;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), f), g_instances.end());
	}
	if (f->running && f->browser)
		obs_source_remove_active_child(f->self, f->browser);
	if (f->browser)
		obs_source_release(f->browser);
	delete f;
}

void forge_update(void *data, obs_data_t *settings)
{
	auto *f = (ForgeTr *)data;
	const int v = (int)obs_data_get_int(settings, "sync_ms");
	if (v != f->sync_ms) {
		f->sync_ms = v;
		f->apply_url();
	}
}

bool forge_reload_clicked(obs_properties_t *props, obs_property_t *prop, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(prop);
	auto *f = (ForgeTr *)data;
	f->reload++;
	f->apply_url();
	return false;
}

obs_properties_t *forge_properties(void *data, void *type_data)
{
	UNUSED_PARAMETER(type_data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_int_slider(p, "sync_ms", "Overlay sync offset (ms)", 0, 400, 5);
	if (data)
		obs_properties_add_button2(p, "reload", "Reload overlay page", forge_reload_clicked, data);
	obs_properties_add_text(p, "info",
				"Edit the look in OBS CSS Stinger Forge (Save as OBS transition). The animation length is set by the preset.",
				OBS_TEXT_INFO);
	return p;
}

void forge_transition_start(void *data)
{
	auto *f = (ForgeTr *)data;
	obs_transition_enable_fixed(f->self, true, f->total_ms());
	if (!f->browser || f->running)
		return;

	uint32_t cx = 0, cy = 0;
	obs_transition_get_size(f->self, &cx, &cy);
	if (cx && cy && (cx != f->bw || cy != f->bh)) {
		f->bw = cx;
		f->bh = cy;
		f->apply_url();
	}
	// Making the overlay an active child fires the page's "obsSourceActiveChanged" event,
	// which starts the animation.
	obs_source_add_active_child(f->self, f->browser);
	f->running = true;
}

void forge_transition_stop(void *data)
{
	auto *f = (ForgeTr *)data;
	if (f->running && f->browser)
		obs_source_remove_active_child(f->self, f->browser);
	f->running = false;
}

void forge_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *f = (ForgeTr *)data;
	const float t = obs_transition_get_time(f->self);
	const enum obs_transition_target target = t < f->cut_t() ? OBS_TRANSITION_SOURCE_A : OBS_TRANSITION_SOURCE_B;
	if (!obs_transition_video_render_direct(f->self, target))
		return;
	if (f->browser && f->running)
		obs_source_video_render(f->browser);
}

float forge_mix(ForgeTr *f, float t, bool a)
{
	const float w = 0.03f, c = f->cut_t();
	float k = (t - (c - w)) / (2.0f * w);
	k = std::clamp(k, 0.0f, 1.0f);
	return a ? 1.0f - k : k;
}
float forge_mix_a(void *data, float t)
{
	return forge_mix((ForgeTr *)data, t, true);
}
float forge_mix_b(void *data, float t)
{
	return forge_mix((ForgeTr *)data, t, false);
}

bool forge_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
			size_t channels, size_t sample_rate)
{
	auto *f = (ForgeTr *)data;
	return obs_transition_audio_render(f->self, ts_out, audio, mixers, channels, sample_rate, forge_mix_a,
					   forge_mix_b);
}

void forge_enum_active(void *data, obs_source_enum_proc_t cb, void *param)
{
	auto *f = (ForgeTr *)data;
	if (f->browser && f->running)
		cb(f->self, f->browser, param);
}

void forge_enum_all(void *data, obs_source_enum_proc_t cb, void *param)
{
	auto *f = (ForgeTr *)data;
	if (f->browser)
		cb(f->self, f->browser, param);
}

void register_preset_type(Preset *p)
{
	struct obs_source_info info = {};
	info.id = p->type_id.c_str();
	info.type = OBS_SOURCE_TYPE_TRANSITION;
	info.type_data = p;
	info.get_name = forge_get_name;
	info.create = forge_create;
	info.destroy = forge_destroy;
	info.update = forge_update;
	info.get_defaults2 = forge_get_defaults;
	info.get_properties2 = forge_properties;
	info.video_render = forge_video_render;
	info.audio_render = forge_audio_render;
	info.enum_active_sources = forge_enum_active;
	info.enum_all_sources = forge_enum_all;
	info.transition_start = forge_transition_start;
	info.transition_stop = forge_transition_stop;
	obs_register_source(&info);
}

/* ------------------------------------------------------------------ preset loading */

bool safe_id(const std::string &s)
{
	if (s.empty() || s.size() > 60)
		return false;
	return std::all_of(s.begin(), s.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
	});
}

bool safe_page(const std::string &s)
{
	return s.rfind("file:///", 0) == 0 || s.rfind("http://127.0.0.1", 0) == 0 || s.rfind("http://localhost", 0) == 0;
}

// Runs on the UI thread. Registers new presets and refreshes changed ones.
void scan_presets()
{
	if (g_presets_dir.empty())
		return;
	os_mkdirs(g_presets_dir.c_str());
	const std::string pattern = g_presets_dir + "/*.json";
	os_glob_t *glob = nullptr;
	if (os_glob(pattern.c_str(), 0, &glob) != 0 || !glob)
		return;

	std::vector<std::string> seen;
	for (size_t i = 0; i < glob->gl_pathc; i++) {
		if (glob->gl_pathv[i].directory)
			continue;
		obs_data_t *d = obs_data_create_from_json_file(glob->gl_pathv[i].path);
		if (!d)
			continue;
		const std::string id = obs_data_get_string(d, "id");
		const std::string name = obs_data_get_string(d, "name");
		const std::string page = obs_data_get_string(d, "page");
		const std::string query = obs_data_get_string(d, "query");
		const uint32_t dur = (uint32_t)std::clamp<long long>(obs_data_get_int(d, "duration"), 300, 10000);
		obs_data_release(d);
		if (!safe_id(id) || name.empty() || !safe_page(page) || query.empty() || query[0] != '?') {
			obs_log(LOG_WARNING, "Skipping invalid preset file %s", glob->gl_pathv[i].path);
			continue;
		}
		seen.push_back(id);

		Preset *p = nullptr;
		bool is_new = false;
		{
			std::lock_guard<std::mutex> lk(g_mx);
			auto it = g_presets.find(id);
			if (it == g_presets.end()) {
				p = new Preset();
				p->id = id;
				p->type_id = "forge_tr_" + id;
				g_presets[id] = p;
				is_new = true;
			} else {
				p = it->second;
			}
			p->name = name;
			p->display = "Forge: " + name;
			p->page = page;
			p->query = query;
			p->duration = dur;
			p->removed = false;
		}
		if (is_new) {
			register_preset_type(p);
			obs_log(LOG_INFO, "Registered transition '%s'", p->display.c_str());
		}
	}
	os_globfree(glob);

	std::vector<ForgeTr *> refresh;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		for (auto &kv : g_presets)
			if (std::find(seen.begin(), seen.end(), kv.first) == seen.end())
				kv.second->removed = true;
		refresh = g_instances;
	}
	for (ForgeTr *f : refresh)
		f->apply_url();
}

void ui_rescan(void *)
{
	if (g_alive)
		scan_presets();
}

std::string dir_signature()
{
	std::string sig;
	std::error_code ec;
	for (auto &e : std::filesystem::directory_iterator(g_presets_dir, ec)) {
		if (e.path().extension() != ".json")
			continue;
		std::error_code ec2;
		const auto t = std::filesystem::last_write_time(e.path(), ec2).time_since_epoch().count();
		sig += e.path().filename().string() + ":" + std::to_string((long long)t) + ";";
	}
	return sig;
}

void watcher_main()
{
	std::string last = dir_signature();
	while (!g_stop) {
		for (int i = 0; i < 20 && !g_stop; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (g_stop)
			break;
		const std::string now = dir_signature();
		if (now != last) {
			last = now;
			obs_queue_task(OBS_TASK_UI, ui_rescan, nullptr, false);
		}
	}
}

} // namespace

bool obs_module_load(void)
{
	char *dir = obs_module_config_path("presets");
	if (dir) {
		g_presets_dir = dir;
		bfree(dir);
	}
	g_alive = true;
	scan_presets();
	g_stop = false;
	g_watcher = std::thread(watcher_main);
	obs_log(LOG_INFO, "OBS CSS Stinger Forge loaded (version %s), presets folder: %s", PLUGIN_VERSION,
		g_presets_dir.c_str());
	return true;
}

void obs_module_unload(void)
{
	g_alive = false;
	g_stop = true;
	if (g_watcher.joinable())
		g_watcher.join();
	obs_log(LOG_INFO, "OBS CSS Stinger Forge unloaded");
}
