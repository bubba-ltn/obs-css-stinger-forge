// Built-in local server for the OBS CSS Stinger Forge designer page.
// Replaces the old Node.js helper: serves the page, saves logos and saves/lists/removes presets.
// Listens on 127.0.0.1 only; API calls need the X-Forge header and a local Host/Origin.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include "../third_party/httplib.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "forge-http.h"

namespace fs = std::filesystem;

namespace {

httplib::Server *g_srv = nullptr;
std::thread g_thread;
std::atomic<int> g_port{0};
std::string g_designer, g_presets, g_page_url, g_palettes;

std::string json_escape(const std::string &s)
{
	std::string o;
	for (unsigned char c : s) {
		switch (c) {
		case '"': o += "\\\""; break;
		case '\\': o += "\\\\"; break;
		case '\n': o += "\\n"; break;
		case '\r': o += "\\r"; break;
		case '\t': o += "\\t"; break;
		default:
			if (c < 0x20) {
				char b[8];
				snprintf(b, sizeof b, "\\u%04x", c);
				o += b;
			} else
				o += (char)c;
		}
	}
	return o;
}

std::string slug(const std::string &n)
{
	std::string o;
	bool dash = false;
	for (unsigned char c : n) {
		if (std::isalnum(c) && c < 128) {
			o += (char)std::tolower(c);
			dash = false;
		} else if (!dash && !o.empty()) {
			o += '-';
			dash = true;
		}
	}
	while (!o.empty() && o.back() == '-')
		o.pop_back();
	if (o.size() > 50)
		o.resize(50);
	while (!o.empty() && o.back() == '-')
		o.pop_back();
	return o;
}

void reply(httplib::Response &res, int code, const std::string &body)
{
	res.status = code;
	res.set_content(body, "application/json");
}
void err(httplib::Response &res, int code, const std::string &msg)
{
	reply(res, code, "{\"error\":\"" + json_escape(msg) + "\"}");
}

bool local_host(const std::string &h)
{
	return h.rfind("127.0.0.1", 0) == 0 || h.rfind("localhost", 0) == 0;
}
bool origin_ok(const httplib::Request &req)
{
	if (!local_host(req.get_header_value("Host")))
		return false;
	const std::string o = req.get_header_value("Origin");
	if (o.empty())
		return true;
	return o.rfind("http://127.0.0.1", 0) == 0 || o.rfind("http://localhost", 0) == 0;
}
bool api_ok(const httplib::Request &req, httplib::Response &res)
{
	if (req.get_header_value("X-Forge") != "1" || !origin_ok(req)) {
		err(res, 403, "Forbidden");
		return false;
	}
	return true;
}

std::string write_sidecar(const std::string &name)
{
	std::ifstream in(fs::u8path(g_designer) / fs::u8path(name), std::ios::binary);
	if (!in)
		return "";
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string js = "(window.__svgLogos=window.__svgLogos||{})[\"" + json_escape(name) + "\"]=\"" +
			       json_escape(ss.str()) + "\";\n";
	std::ofstream out(fs::u8path(g_designer) / fs::u8path(name + ".js"), std::ios::binary);
	out << js;
	return name;
}

std::string lower_ext(const std::string &n)
{
	std::string e = fs::u8path(n).extension().u8string();
	std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return e;
}

const char *mime_for(const std::string &path)
{
	if (path.size() > 7 && path.compare(path.size() - 7, 7, ".svg.js") == 0)
		return "text/javascript; charset=utf-8";
	const std::string e = lower_ext(path);
	if (e == ".html") return "text/html; charset=utf-8";
	if (e == ".png") return "image/png";
	if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
	if (e == ".gif") return "image/gif";
	if (e == ".webp") return "image/webp";
	if (e == ".svg") return "image/svg+xml";
	if (e == ".webm") return "video/webm";
	return nullptr;
}

void handle_save_logo(const httplib::Request &req, httplib::Response &res)
{
	std::string base = fs::u8path(req.get_param_value("name")).filename().u8string();
	if (base.empty())
		base = "logo.png";
	for (char &c : base)
		if (!(std::isalnum((unsigned char)c) || c == ' ' || c == '.' || c == '(' || c == ')' || c == '-' ||
		      c == '_'))
			c = '_';
	const std::string ext = lower_ext(base);
	if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif" && ext != ".webp" && ext != ".svg")
		return err(res, 400, "Not an image file");
	if (req.body.size() > 20u * 1000 * 1000)
		return err(res, 413, "File is too large");
	const std::string stem = base.substr(0, base.size() - ext.size());
	std::string name = base;
	for (int i = 2;; i++) {
		const fs::path fp = fs::u8path(g_designer) / fs::u8path(name);
		std::ifstream ex(fp, std::ios::binary);
		if (!ex)
			break;
		std::stringstream ss;
		ss << ex.rdbuf();
		if (ss.str() == req.body)
			goto saved; // identical file already there
		name = stem + "-" + std::to_string(i) + ext;
	}
	{
		std::ofstream out(fs::u8path(g_designer) / fs::u8path(name), std::ios::binary);
		if (!out)
			return err(res, 500, "Could not write the logo into the plugin folder");
		out.write(req.body.data(), (std::streamsize)req.body.size());
	}
saved:
	if (ext == ".svg")
		write_sidecar(name);
	reply(res, 200, "{\"name\":\"" + json_escape(name) + "\"}");
}

void handle_list_presets(httplib::Response &res)
{
	std::string out = "{\"presets\":[";
	bool first = true;
	std::error_code ec;
	for (auto &e : fs::directory_iterator(fs::u8path(g_presets), ec)) {
		if (e.path().extension() != ".json")
			continue;
		obs_data_t *d = obs_data_create_from_json_file(e.path().u8string().c_str());
		if (!d)
			continue;
		if (!first)
			out += ",";
		first = false;
		out += "{\"id\":\"" + json_escape(obs_data_get_string(d, "id")) + "\",\"name\":\"" +
		       json_escape(obs_data_get_string(d, "name")) + "\",\"duration\":" +
		       std::to_string(obs_data_get_int(d, "duration")) + "}";
		obs_data_release(d);
	}
	out += "]}";
	reply(res, 200, out);
}

void handle_save_preset(const httplib::Request &req, httplib::Response &res)
{
	obs_data_t *b = obs_data_create_from_json(req.body.c_str());
	if (!b)
		return err(res, 400, "Bad request");
	std::string name = obs_data_get_string(b, "name");
	std::string q = obs_data_get_string(b, "query");
	obs_data_release(b);

	while (!name.empty() && name.front() == ' ')
		name.erase(name.begin());
	while (!name.empty() && name.back() == ' ')
		name.pop_back();
	if (name.size() > 40)
		name.resize(40);
	const std::string id = slug(name);
	if (id.empty())
		return err(res, 400, "Give the transition a name (letters or numbers)");
	if (q.empty() || q[0] != '?')
		return err(res, 400, "Missing settings");

	// Split the query, take the duration, drop the playback-only keys.
	int duration = 1400;
	bool has_t = false;
	std::string kept;
	std::stringstream ss(q.substr(1));
	std::string kv;
	while (std::getline(ss, kv, '&')) {
		const std::string k = kv.substr(0, kv.find('='));
		if (k == "d") {
			const size_t eq = kv.find('=');
			if (eq != std::string::npos)
				duration = std::atoi(kv.c_str() + eq + 1);
			continue;
		}
		if (k == "mode" || k == "loop" || k == "render" || k == "ctl" || k == "_r")
			continue;
		if (k == "t" && kv.size() > 2)
			has_t = true;
		kept += (kept.empty() ? "" : "&") + kv;
	}
	if (!has_t)
		return err(res, 400, "Missing transition type");
	duration = std::clamp(duration, 300, 10000);

	std::error_code ec;
	fs::create_directories(fs::u8path(g_presets), ec);
	const fs::path fp = fs::u8path(g_presets) / fs::u8path(id + ".json");
	const fs::path tmp = fs::u8path(g_presets) / fs::u8path(id + ".json.tmp");
	{
		std::ofstream out(tmp, std::ios::binary);
		if (!out)
			return err(res, 500, "Could not write the preset");
		out << "{\n  \"id\": \"" << id << "\",\n  \"name\": \"" << json_escape(name) << "\",\n  \"page\": \""
		    << json_escape(g_page_url) << "\",\n  \"query\": \"?" << json_escape(kept)
		    << "\",\n  \"duration\": " << duration << "\n}\n";
	}
	fs::rename(tmp, fp, ec);
	if (ec)
		return err(res, 500, "Could not save the preset");
	obs_log(LOG_INFO, "Saved preset \"%s\"", name.c_str());
	reply(res, 200, "{\"id\":\"" + id + "\",\"name\":\"" + json_escape(name) + "\",\"file\":\"" +
				json_escape(fp.u8string()) + "\"}");
}

void handle_delete_preset(const httplib::Request &req, httplib::Response &res)
{
	const std::string id = slug(req.get_param_value("id"));
	if (id.empty())
		return err(res, 400, "Bad id");
	std::error_code ec;
	fs::remove(fs::u8path(g_presets) / fs::u8path(id + ".json"), ec);
	reply(res, 200, "{\"ok\":true}");
}

void handle_get_palettes(httplib::Response &res)
{
	std::ifstream in(fs::u8path(g_palettes), std::ios::binary);
	if (!in)
		return reply(res, 200, "{\"palettes\":{}}");
	std::stringstream ss;
	ss << in.rdbuf();
	reply(res, 200, ss.str());
}

void handle_save_palettes(const httplib::Request &req, httplib::Response &res)
{
	if (req.body.size() > 200000)
		return err(res, 413, "Too many palettes");
	obs_data_t *d = obs_data_create_from_json(req.body.c_str());
	if (!d)
		return err(res, 400, "Bad request");
	obs_data_release(d);
	std::error_code ec;
	fs::create_directories(fs::u8path(g_palettes).parent_path(), ec);
	const fs::path tmp = fs::u8path(g_palettes + ".tmp");
	{
		std::ofstream out(tmp, std::ios::binary);
		if (!out)
			return err(res, 500, "Could not write the palettes");
		out << req.body;
	}
	fs::rename(tmp, fs::u8path(g_palettes), ec);
	if (ec)
		return err(res, 500, "Could not save the palettes");
	reply(res, 200, "{\"ok\":true}");
}

void handle_static(const httplib::Request &req, httplib::Response &res)
{
	std::string p = httplib::detail::decode_url(req.path, false);
	if (p == "/" || p.empty())
		p = "/transition-forge.html";
	p.erase(0, 1);
	if (p.find("..") != std::string::npos || p.find('\\') != std::string::npos || p.find(':') != std::string::npos ||
	    p.find('/') != std::string::npos) {
		res.status = 404;
		return;
	}
	const char *type = mime_for(p);
	std::ifstream in(fs::u8path(g_designer) / fs::u8path(p), std::ios::binary);
	if (!type || !in) {
		res.status = 404;
		res.set_content("Not found", "text/plain");
		return;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	res.set_header("Cache-Control", "no-cache");
	res.set_content(ss.str(), type);
}

std::string file_url(const std::string &path)
{
	std::string p = path;
	std::replace(p.begin(), p.end(), '\\', '/');
	std::string o = "file:///";
	for (unsigned char c : p) {
		if (std::isalnum(c) || strchr("/:-_.~()", c))
			o += (char)c;
		else {
			char b[4];
			snprintf(b, sizeof b, "%%%02X", c);
			o += b;
		}
	}
	return o;
}

} // namespace

std::string forge_page_url()
{
	return g_page_url;
}

int forge_http_port()
{
	return g_port;
}

void forge_http_start(const std::string &designer_dir, const std::string &presets_dir)
{
	g_designer = designer_dir;
	g_presets = presets_dir;
	while (!g_designer.empty() && (g_designer.back() == '/' || g_designer.back() == '\\'))
		g_designer.pop_back();
	g_page_url = file_url(g_designer + "/transition-forge.html");
	g_palettes = (fs::u8path(g_presets).parent_path() / "palettes.json").u8string();

	g_srv = new httplib::Server();
	g_srv->set_payload_max_length(25u * 1000 * 1000);

	g_srv->Get("/api/ping", [](const httplib::Request &req, httplib::Response &res) {
		if (!api_ok(req, res))
			return;
		reply(res, 200,
		      "{\"ok\":true,\"presetsDir\":\"" + json_escape(g_presets) + "\",\"root\":\"" +
			      json_escape(g_designer) + "\",\"fileUrl\":\"" + json_escape(g_page_url) + "\"}");
	});
	g_srv->Get("/api/presets", [](const httplib::Request &req, httplib::Response &res) {
		if (api_ok(req, res))
			handle_list_presets(res);
	});
	g_srv->Post("/api/preset", [](const httplib::Request &req, httplib::Response &res) {
		if (api_ok(req, res))
			handle_save_preset(req, res);
	});
	g_srv->Delete("/api/preset", [](const httplib::Request &req, httplib::Response &res) {
		if (api_ok(req, res))
			handle_delete_preset(req, res);
	});
	g_srv->Post("/api/logo", [](const httplib::Request &req, httplib::Response &res) {
		if (api_ok(req, res))
			handle_save_logo(req, res);
	});
	g_srv->Get("/api/palettes", [](const httplib::Request &req, httplib::Response &res) {
		if (api_ok(req, res))
			handle_get_palettes(res);
	});
	g_srv->Post("/api/palettes", [](const httplib::Request &req, httplib::Response &res) {
		if (api_ok(req, res))
			handle_save_palettes(req, res);
	});
	g_srv->Get(R"(/.*)", [](const httplib::Request &req, httplib::Response &res) {
		if (!origin_ok(req)) {
			res.status = 403;
			return;
		}
		if (req.path.rfind("/api/", 0) == 0) {
			err(res, 404, "Not found");
			return;
		}
		handle_static(req, res);
	});

	int port = 0;
	for (int p = 5177; p < 5187; p++) {
		if (g_srv->bind_to_port("127.0.0.1", p)) {
			port = p;
			break;
		}
	}
	if (!port) {
		obs_log(LOG_WARNING, "Could not open a local port for the designer page");
		delete g_srv;
		g_srv = nullptr;
		return;
	}
	g_port = port;
	g_thread = std::thread([] { g_srv->listen_after_bind(); });
	obs_log(LOG_INFO, "Designer page at http://127.0.0.1:%d/", port);
}

void forge_http_stop()
{
	if (g_srv)
		g_srv->stop();
	if (g_thread.joinable())
		g_thread.join();
	delete g_srv;
	g_srv = nullptr;
	g_port = 0;
}
