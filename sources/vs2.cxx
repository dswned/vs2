#include "config.h"

#ifdef _WIN32
#include <fcntl.h> // _O_BINARY
#include <io.h> // setmode
#endif

#if _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <csignal>
#ifndef SIGBREAK
#define SIGBREAK SIGTSTP
#endif
#include <cstring> // strerror

#include <string>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>
#include <chrono>

#include <vapoursynth4.h>
#include <vsscript4.h>
#include <vshelper4.h>

#ifdef _WIN32
#define AVSC_NO_DECLSPEC
#include <avisynth_c.h>
#endif

#include <getopt.h>

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

volatile sig_atomic_t b_ctrl_c;
std::mutex mutex;
std::condition_variable v;
FILE* ofh = 0;
XXH64_state_t state0;
std::vector<XXH64_state_t> xxhs;
std::vector<uint8_t> buf;

typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_point_t;
time_point_t t0;
std::deque<std::pair<int, time_point_t>> t;

float f0, f1, f2;
/*                <        <          <    */
int u = 0, left = 0, out = 0, right = 0, end;
int y4m = 0, verbose = 0, hash = 1;

void sigint_handler(int)
{
	b_ctrl_c |= 2;
}

void sigbreak_handler(int)
{
	std::unique_lock lock(mutex);
	b_ctrl_c ^= 1;
	v.notify_one();
	if (signal(SIGBREAK, sigbreak_handler) == SIG_ERR)
		;
}

inline uint64_t xxh_value(const void* input, size_t length)
{
	XXH64_state_t state = state0;
	(void)XXH64_update(&state, input, length);
	return XXH64_digest(&state);
}

const char* yuv_ssx_to_str(int ssh, int ssw, const char* def = 0)
{
	if (ssh == 0 && ssw == 0)
		return "444";
	else if (ssh == 1 && ssw == 1)
		return "420";
	else if (ssh == 2 && ssw == 2)
		return "410";
	else if (ssh == 0 && ssw == 1)
		return "422";
	else if (ssh == 0 && ssw == 2)
		return "411";
	else if (ssh == 1 && ssw == 0)
		return "440";
	return def;
}

int write_y4m_header(int cf, int ssh, int ssw, int st, int depth, int height, int width, int fps_num, int fps_den, int nf)
{
	if (height <= 0 || width <= 0)
		return 1;
	char header[200], * p = header;
	const char* a, * b;
	if (st != stInteger)
		return 1;
	if (cf == cfGray)
		a = "mono", b = "";
	else if (cf != cfYUV)
		return 1;
	else if (a = yuv_ssx_to_str(ssh, ssw); a)
		b = (depth != 8) ? "p" : "";
	else
		return 1;
	p += sprintf(p, "YUV4MPEG2 C%s%s", a, b);
	if (depth != 8)
		p += sprintf(p, "%d", depth);
	p += sprintf(p, " W%d H%d F%d:%d Ip A0:0 XLENGTH=%d\n", width, height, fps_num, fps_den, nf);
	if (int bytes = p - header; fwrite(header, 1, bytes, ofh) != bytes || fflush(ofh))
		return -1;
	return 0;
}

inline int write_y4m_frame_header() { return fwrite("FRAME\n", 1, 6, ofh) != 6; }

void update_timer()
{
	t.emplace_front(out, std::chrono::high_resolution_clock::now());
	f1 = (t.front().first - t.back().first) /
		std::chrono::duration<float>(t.front().second - t.back().second).count();
	if (t.size() > 5)
		t.pop_back();
}

void print_status(int n0, int n1, float f0, float f1, bool last)
{
	static const int pn = (int)std::log10((float)n1) + 1;
	int p0 = (int)std::log10(f0), p1 = (int)std::log10(f1);
	int p01 = p0 < 2 ? 2 : p0 < 3 ? 1 : 0;
	int p00 = p0 < 2 ? 7 : p0 < 3 ? 6 : p0 + 1;
	int p02 = std::max(7 - p00, 0);
	int p11 = p1 < 2 ? 2 : p1 < 3 ? 1 : 0;
	int p10 = p1 < 2 ? 7 : p1 < 3 ? 6 : p1 + 1;
	int p12 = std::max(7 - p10, 0);
	static const char* dot = ".";
	if (last)
	{
		fprintf(stderr, "%*d/%*d, %7.3f / %*.*f%-*s fps",
			pn, n0, pn, n1, f1, p00, p01, f0, p02, dot + (p00 != 4));
		if (hash)
		{
			fprintf(stderr, ", xxh: ");
			for (int p = 0, end = xxhs.size(); p < end; p++)
				fprintf(stderr, "%016" PRIx64 "%s", (uint64_t)XXH64_digest(&xxhs[p]), p != end - 1 ? "-" : "");
		}
		fprintf(stderr, " \n");
	}
	else
	{
		fprintf(stderr, "%*d/%*d, %*.*f%-*s / %*.*f%-*s fps \r",
			pn, n0, pn, n1, p00, p01, f0, p02, dot + (p00 != 4), p10, p11, f1, p12, dot + (p10 != 4));
	}
	fflush(stderr);
}

std::string format(const char* f, ...)
{
	va_list arg;
	va_start(arg, f);
	int n = std::vsnprintf(nullptr, 0, f, arg);
	if (n < 0)
	{
		va_end(arg);
		throw std::runtime_error("vsnprintf() failed");
	}
	std::string str(++n, 0);
	std::vsnprintf(str.data(), str.size(), f, arg);
	va_end(arg);
	return str;
}

struct synth
{
	int nt = 1, h = 0, w = 0, nf = 0, np = 0, ssh = -1, ssw = -1, cf = 0, st = 0, ss = 0;
	int64_t fps_num, fps_den;
	virtual void start() = 0;
	virtual void free() = 0;
	// unchecked
	virtual void evaluate() = 0;
	virtual ~synth() = default;
};

#ifdef _WIN32
struct asynth : synth
{
	AVS_ScriptEnvironment* env = 0;
	AVS_Clip* clip = 0;
	const AVS_VideoInfo* vi = 0;
	AVS_Library* avs = 0;
	std::string ifname;
	bool is_planar;
	asynth(const std::string& ifname) try
		: ifname(ifname)
	{
		avs = avs_load_library();
		if (!avs)
			throw std::string("!avs_load_library()");
		evaluate();
	}
	catch (...)
	{
		if (clip)
			avs->avs_release_clip(clip);
		if (env)
			avs->avs_delete_script_environment(env);
		if (avs)
			avs_free_library(avs);
	}
	void evaluate()
	{
		env = avs->avs_create_script_environment(2);
		if (const char* err = avs->avs_get_error(env); err)
			throw format("%s", err);
		AVS_Value arg = avs_new_value_string(ifname.c_str());
		t0 = std::chrono::high_resolution_clock::now();
		AVS_Value res = avs->avs_invoke(env, "import", arg, 0);
		f2 = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
		if (avs_is_error(res))
			throw format("%s", avs_as_string(res));
		if (!avs_is_clip(res))
			throw std::string("!avs_is_clip()");
		clip = avs->avs_take_clip(res, env);
		vi = avs->avs_get_video_info(clip);
		if (!avs_has_video(vi))
			throw std::string("!avs_has_video()");
		avs->avs_release_value(res);

		bool is_rgb = vi->pixel_type & AVS_CS_BGR;
		is_planar = vi->pixel_type & AVS_CS_PLANAR;
		bool is_y = (vi->pixel_type & AVS_CS_GENERIC_Y) == AVS_CS_GENERIC_Y;
		bool is_yuv = vi->pixel_type & AVS_CS_YUV + AVS_CS_YUVA;

		nf = vi->num_frames;
		h = vi->height;
		w = vi->width;
		fps_num = vi->fps_numerator;
		fps_den = vi->fps_denominator;
		ss = avs->avs_bits_per_component(vi);
		int nc = avs->avs_num_components(vi);
		np = is_planar ? nc : 1;
		st = ss < 32 ? stInteger : stFloat;
		if (is_y)
			cf = cfGray;
		else if (is_yuv)
		{
			cf = cfYUV;
			switch (vi->pixel_type & AVS_CS_SUB_HEIGHT_MASK)
			{
			case AVS_CS_SUB_HEIGHT_1:
				ssh = 0;
				break;
			case AVS_CS_SUB_HEIGHT_2:
				ssh = 1;
				break;
			case AVS_CS_SUB_HEIGHT_4:
				ssh = 2;
				break;
			}
			switch (vi->pixel_type & AVS_CS_SUB_WIDTH_MASK)
			{
			case AVS_CS_SUB_WIDTH_1:
				ssw = 0;
				break;
			case AVS_CS_SUB_WIDTH_2:
				ssw = 1;
				break;
			case AVS_CS_SUB_WIDTH_4:
				ssw = 2;
				break;
			}
		}
		else if (is_rgb)
			cf = cfRGB;
		else
			cf = cfUndefined;
	}
	int write_frame(const AVS_VideoFrame* f)
	{
		const int yuva[] = { AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V, AVS_PLANAR_A };
		const int rgba[] = { AVS_PLANAR_R, AVS_PLANAR_G, AVS_PLANAR_B, AVS_PLANAR_A };
		for (int rp = 0; rp < np; rp++)
		{
			int p = is_planar ? cf == cfYUV ? yuva[rp] : cf == cfRGB ? rgba[rp] : 0 : 0;
			const uint8_t* ptr = avs->avs_get_read_ptr_p(f, p);
			int height = avs->avs_get_height_p(f, p);
			int rowsize = avs->avs_get_row_size_p(f, p);
			int stride = avs->avs_get_pitch_p(f, p);
			int bytes = rowsize * height;
			if (stride != rowsize)
				buf.resize(bytes), vsh::bitblt(buf.data(), rowsize, ptr, stride, rowsize, height), ptr = buf.data();
			if (ofh && fwrite(ptr, 1, bytes, ofh) != bytes)
				return -1;
			if (hash)
			{
				uint64_t x = xxh_value(ptr, bytes);
				XXH64_update(&xxhs[rp], &x, 8);
			}
		}
		return 0;
	}
	void free()
	{
		if (clip)
			avs->avs_release_clip(clip), clip = 0;
		if (env)
			avs->avs_delete_script_environment(env), env = 0;
	}
	~asynth()
	{
		if (clip)
			avs->avs_release_clip(clip);
		if (env)
			avs->avs_delete_script_environment(env);
		avs_free_library(avs);
	}
	void start_thread()
	{
		for (; left < end; left++)
		{
			AVS_VideoFrame* f = avs->avs_get_frame(clip, right++);
			out = right;
			if (u)
				update_timer(), u = 0;
			if (const char* err = avs->avs_clip_get_error(clip); err)
				fprintf(stderr, "failed to retrieve frame #%d:\n%s\n", left, err), b_ctrl_c = -1;
			if (b_ctrl_c >= 0 && (y4m && write_y4m_frame_header() || write_frame(f)))
				fprintf(stderr, "write_frame() failed when writing frame #%d: %s\n", left, std::strerror(errno)), b_ctrl_c = -1;
			avs->avs_release_video_frame(f);
			if (b_ctrl_c)
				end = right;
		}
		v.notify_one();
	}
	void start()
	{
		end = nf;
		std::thread t(std::bind(&asynth::start_thread, this));
		t.detach();
	}
};
#endif

struct vsynth : synth
{
	const VSAPI* vsapi = 0;
	VSScript* se = 0;
	VSNode* node = 0;
	const VSSCRIPTAPI* vssapi = 0;
	std::string ifname;
	std::vector<std::pair<std::string, std::string>> args;
	std::map<int, const VSFrame*> of;
	vsynth(const std::string& ifname, std::vector<std::pair<std::string, std::string>>& args) try
		: ifname(ifname)
		, args(args)
	{
		vssapi = getVSScriptAPI(VSSCRIPT_API_VERSION);
		if (!vssapi)
			throw std::string("!vssapi");
		vsapi = vssapi->getVSAPI(VAPOURSYNTH_API_VERSION);
		if (!vsapi)
			throw std::string("!vsapi");
		evaluate();
	}
	catch (...)
	{
		if (node)
			vsapi->freeNode(node);
		if (se)
			vssapi->freeScript(se);
	}
	void evaluate()
	{
		se = vssapi->createScript(0);
		if (!args.empty())
		{
			vssapi->evalSetWorkingDir(se, 1);
			VSMap* map = vsapi->createMap();
			for (auto& it : args)
			{
				if (it.second.find_first_not_of("0123456789.") == std::string::npos)
				{
					if (it.second.find_first_of('.') == std::string::npos)
						vsapi->mapSetInt(map, it.first.c_str(), std::stoll(it.second), maAppend);
					else
						vsapi->mapSetFloat(map, it.first.c_str(), std::stod(it.second), maAppend);
				}
				else
				{
					vsapi->mapSetData(map, it.first.c_str(), it.second.c_str(), it.second.size(), dtUtf8, maAppend);
				}
			}
			vssapi->setVariables(se, map);
			vsapi->freeMap(map);
		}
		t0 = std::chrono::high_resolution_clock::now();
		if (vssapi->evaluateFile(se, ifname.c_str()))
			throw format("evaluateFile() failed:\n%s", vssapi->getError(se));
		f2 = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
		node = vssapi->getOutputNode(se, 0);
		if (!node)
			throw std::string("!node");
		if (vsapi->getNodeType(node) != mtVideo)
			throw std::string("!mtVideo");
		//vsapi->setCacheMode(node, 0);
		const VSVideoInfo* vi = vsapi->getVideoInfo(node);
		VSCoreInfo ci;
		vsapi->getCoreInfo(vssapi->getCore(se), &ci);
		nt = ci.numThreads;
		nf = vi->numFrames;
		h = vi->height;
		w = vi->width;
		fps_num = vi->fpsNum;
		fps_den = vi->fpsDen;
		if (vsh::isConstantVideoFormat(vi))
		{
			ssh = vi->format.subSamplingH;
			ssw = vi->format.subSamplingW;
			np = vi->format.numPlanes;
			cf = vi->format.colorFamily;
			st = vi->format.sampleType;
			ss = vi->format.bitsPerSample;
		}
	}
	void free()
	{
		if (node)
			vsapi->freeNode(node), node = 0;
		if (se)
			vssapi->freeScript(se), se = 0;
	}
	~vsynth()
	{
		if (node)
			vsapi->freeNode(node);
		if (se)
			vssapi->freeScript(se);
	}
	int write_frame(const VSFrame* f)
	{
		const VSVideoFormat* ff = vsapi->getVideoFrameFormat(f);
		const int gbr[] = { 1, 2, 0 };
		for (int rp = 0, np = ff->numPlanes; rp < np; rp++)
		{
			int p = ff->colorFamily == cfRGB ? gbr[rp] : rp;
			const uint8_t* ptr = vsapi->getReadPtr(f, p);
			int height = vsapi->getFrameHeight(f, p);
			int rowsize = vsapi->getFrameWidth(f, p) * ff->bytesPerSample;
			int stride = vsapi->getStride(f, p);
			int bytes = rowsize * height;
			if (stride != rowsize)
				buf.resize(bytes), vsh::bitblt(buf.data(), rowsize, ptr, stride, rowsize, height), ptr = buf.data();
			if (ofh && fwrite(ptr, 1, bytes, ofh) != bytes)
				return -1;
			if (hash)
			{
				uint64_t x = xxh_value(ptr, bytes);
				XXH64_update(&xxhs[rp], &x, 8);
			}
		}
		return 0;
	}
	static void callback(void* userData, const VSFrame* f, int n, VSNode* node, const char* errorMsg)
	{
		vsynth* d = static_cast<vsynth*>(userData);
		const VSAPI* vsapi = d->vsapi;
		if (b_ctrl_c > 0)
			end = right;
		if (f)
		{
			d->of[n] = f, out++;
			if (u)
				update_timer(), u = 0;
			if (right < end)
				vsapi->getFrameAsync(right++, node, callback, userData);
			while (d->of.count(left))
			{
				auto f = d->of[left];
				if (b_ctrl_c >= 0 && (y4m && write_y4m_frame_header() || d->write_frame(f)))
				{
					fprintf(stderr, "write_frame() failed when writing frame #%d: %s\n", left, std::strerror(errno));
					b_ctrl_c = -1, end = left;
				}
				vsapi->freeFrame(f);
				d->of[left++] = 0;
			}
		}
		else
		{//\nifu
			static int error_count = 0;
			fprintf(stderr, "failed to retrieve frame #%d:\n%s\n", n, errorMsg);
			if (++error_count > 88)
				b_ctrl_c = -1, end = left;
			if (!b_ctrl_c)
				vsapi->getFrameAsync(n, node, callback, userData);
		}
		if (left == end)
			v.notify_one(); // ++wait if fail
	}
	void start()
	{
		end = nf;
		right = std::min(left + nt, nf);
		for (int n = left, end = right; n < end; n++)
			vsapi->getFrameAsync(n, node, callback, this);
	}
};

static const struct option longopts[] = {
	{ "y4m",     no_argument, 0, 'y' },
	{ "no-hash", no_argument, 0,   0 },
	{ 0 }
};

int main(int argc, char** argv)
{
	int index = 0;
	if (signal(SIGINT, sigint_handler) == SIG_ERR)
		;
	if (signal(SIGBREAK, sigbreak_handler) == SIG_ERR)
		;
	std::vector<std::pair<std::string, std::string>> args;
	std::string ifname, ofname;
	for (;;)
	{
		int longind = -1;
		int c = getopt_long(argc, argv, "-hi:yva:l", longopts, &longind);
		if (c == -1)
			break;
		switch (c)
		{
		case 1:
			if (ifname.empty())
				ifname = optarg;
			else if (ofname.empty())
				ofname = optarg;
			else
				return 1;
			break;
		case '?':
			return 1;
		case 'h':
			fprintf(stderr, "vs2 version " PROJECT_VERSION "\nsyntax: vs2 [options] infile [outfile]\n");
			return 0;
		case 'v':
			verbose = 1;
			break;
		case 'i':
			index = atoi(optarg);
			break;
		case 'y':
			y4m = 1;
			break;
		case 'a':
		{
			std::string pair = optarg;
			size_t n = pair.find_first_of('=');
			args.emplace_back(pair.substr(0, n), pair.substr(std::min(n + 1, std::string::npos)));
		}
			break;
		default:
			if (!strcmp(longopts[longind].name, "no-hash")) hash = 0;
		}
	}
	if (ifname.empty())
		return -1;
	std::unique_ptr<synth> synther;
	try
	{
#ifdef _WIN32
		if (ifname.ends_with(".avs"))
			synther = std::make_unique<asynth>(ifname);
		else
#endif
			synther = std::make_unique<vsynth>(ifname, args);
	}
	catch (const std::string& e)
	{
		fprintf(stderr, "%s\n", e.c_str());
		return -1;
	}
	int nf = synther->nf;
	int height = synther->h;
	int width = synther->w;
	int np = synther->np;
	int st = synther->st;
	int ss = synther->ss;
	int cf = synther->cf;
	int ssh = synther->ssh;
	int ssw = synther->ssw;
	int64_t fps_num = synther->fps_num;
	int64_t fps_den = synther->fps_den;
	const char* csp_string =
		cf == cfGray ? "gray" : cf == cfRGB ? "rgb" : cf == cfYUV ? yuv_ssx_to_str(ssh, ssw, "unknown yuv") : "varying format";
	fprintf(stderr, "w = %d, h = %d, csp = %s, depth = %d%s, fps = %" PRId64 "/%" PRId64 ", nf = %d\n",
		width, height, csp_string, ss, st == stFloat ? "f" : "", fps_num, fps_den, nf);
	if (hash)
	{
		XXH64_reset(&state0, 0);
		xxhs.resize(np > 0 ? np : 4, state0);
	}
	if (ofname.empty())
		y4m = 0, verbose = 1;
	else if (ofname == "-")
	{
#ifdef _WIN32
		if (setmode(fileno(stdout), _O_BINARY) == -1)
		{
			fprintf(stderr, "setmode() failed\n");
			return -1;
		}
#endif
		ofh = stdout;
	}
	else
	{
		if (ofh = fopen(ofname.c_str(), "wb"); !ofh)
		{
			fprintf(stderr, "fopen() failed: %s\n", std::strerror(errno));
			return -1;
		}
		verbose = 1;
	}
	if (y4m)
	{
		if (int ret = write_y4m_header(cf, ssh, ssw, st, ss, height, width, fps_num, fps_den, nf); ret)
		{
			if (ret < 0)
				fprintf(stderr, "write_y4m_header() failed: %s\n", std::strerror(errno));
			else
				fprintf(stderr, "no y4m identifier exists for current format\n");
			fclose(ofh);
			return -1;
		}
	}
	std::unique_lock lock(mutex);
	t0 = std::chrono::high_resolution_clock::now();
	t.emplace_front(0, t0);
	for (;;)
	{
		synther->start();
		if (verbose)
		{
			while (left != end)
			{
				if (v.wait_for(lock, std::chrono::milliseconds(400), [&]() { return left == end; }))
					break;
				f0 = out / std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
				print_status(out, nf, f0, f1, 0), u = 1;
			}
		}
		else
			v.wait(lock, [&]() { return left == end; });
		synther->free();
		if (b_ctrl_c == 1)
			v.wait(lock);
		if (b_ctrl_c || out == nf)
			break;
		try
		{
			synther->evaluate(); // re
		}
		catch (...)
		{
			break;
		}
	}
	if (ofh)
		fclose(ofh);
	f0 = out / std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
	if (verbose)
		print_status(out, nf, f0, f2, 1);
	return 0;
}
