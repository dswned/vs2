#if defined(_WIN32)
#include <io.h> // setmode
#include <fcntl.h> // _O_BINARY
#endif
#include "config.h"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <csignal>
#include <cerrno>

#include <vapoursynth.h>
#include <vsscript.h>
#include <vshelper.h>

#include <getopt.h>

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

volatile sig_atomic_t b_ctrl_c;

void sigint_handler(int)
{
	b_ctrl_c = 1;
}

#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>
#include <map>
#include <vector>
#include <algorithm>

typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_point_t;
time_point_t t0;
std::deque<std::pair<int, time_point_t>> t;

typedef struct
{
	const VSFrameRef* f;
} mapped_type;
std::map<int, mapped_type> of;

std::mutex mutex;
std::condition_variable v;
FILE* ofh = 0;
XXH64_state_t state0;
std::vector<XXH64_state_t> xxhs;
std::vector<uint64_t> xxhh;
std::vector<uint8_t> buf;
const VSAPI* vsapi = nullptr;
VSScript* se = nullptr;
VSNodeRef* node = nullptr;

float f0, f1, f2;
int u = 0, k = 0, req, out = 0, end;
int y4m = 0, verbose = 0, hash = 1;

inline uint64_t xxh_value(const void* input, size_t length)
{
	XXH64_state_t state = state0;
	XXH64_update(&state, input, length);
	return XXH64_digest(&state);
}

const char* ss_to_str(int ssh, int ssw)
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
	return 0;
}

int write_y4m_header(int cf, int ssh, int ssw, int st, int depth, int height, int width, int fps_num, int fps_den, int nf)
{
	char header[200], * p = header;
	const char* a, * b;
	if (st != stInteger)
		return 0;
	if (cf == cmGray)
		a = "mono", b = "";
	else if (cf != cmYUV)
		return 0;
	else if (a = ss_to_str(ssh, ssw); a)
		b = (depth != 8) ? "p" : "";
	else
		return 0;
	p += sprintf(p, "YUV4MPEG2 C%s%s", a, b);
	if (depth != 8)
		p += sprintf(p, "%d", depth);
	p += sprintf(p, " W%d H%d F%d:%d Ip A0:0 XLENGTH=%d\n", width, height, fps_num, fps_den, nf);
	if (int bytes = p - header; fwrite(header, 1, bytes, ofh) != bytes || fflush(ofh))
		return -1;
	return 1;
}

inline int write_y4m_frame_header() { return fwrite("FRAME\n", 1, 6, ofh) == 6; }

int write_frame(const VSFrameRef* f)
{
	const VSFormat* ff = vsapi->getFrameFormat(f);
	const int gbrp[] = { 1, 2, 0 };
	for (int rp = 0, np = ff->numPlanes; rp < np; rp++)
	{
		int p = ff->colorFamily == cmRGB ? gbrp[rp] : rp;
		int stride = vsapi->getStride(f, p), height = vsapi->getFrameHeight(f, p),
			rowsize = vsapi->getFrameWidth(f, p) * ff->bytesPerSample;
		const uint8_t* ptr = vsapi->getReadPtr(f, p);
		int bytes = rowsize * height;
		if (stride != rowsize)
			buf.resize(bytes), vs_bitblt(buf.data(), rowsize, ptr, stride, rowsize, height), ptr = buf.data();
		if (ofh && fwrite(ptr, 1, bytes, ofh) != bytes)
			return 0;
		if (hash)
			xxhh.push_back(xxh_value(ptr, bytes));
	}
	return 1;
}

void VS_CC callback(void* userData, const VSFrameRef* f, int n, VSNodeRef* node, const char* errorMsg)
{
	if (b_ctrl_c)
		end = req;
	if (f)
	{
		of[n].f = f, out++;
		if (u)
		{
			t.emplace_front(out, std::chrono::high_resolution_clock::now()), u = 0;
			f1 = (t.front().first - t.back().first) /
				std::chrono::duration<float>(t.front().second - t.back().second).count();
			if (t.size() > 5)
				t.pop_back();
		}
		if (req < end)
			vsapi->getFrameAsync(req++, node, callback, nullptr);
		while (of.count(k))
		{
			xxhh.clear();
			auto f = of[k].f;
			if (b_ctrl_c >= 0 && (y4m && !write_y4m_frame_header() || !write_frame(f)))
				fprintf(stderr, "write_frame() failed when writing frame #%d: %s\n", k, std::strerror(errno)), b_ctrl_c = -1;
			if (hash)
			{
				for (int p = 0, end = xxhh.size(); p < end; p++)
					XXH64_update(&xxhs[p], &xxhh[p], 8);
			}
			vsapi->freeFrame(f);
			of[k++].f = nullptr;
		}
	}
	else
	{
		fprintf(stderr, "failed to retrieve frame #%d:\n%s\n", n, errorMsg);
		if (!b_ctrl_c)
			vsapi->getFrameAsync(n, node, callback, nullptr);
	}
	if (k == end)
	{
		std::lock_guard<std::mutex> lock(mutex);
		v.notify_one();
	}
}

int cleanup(int ec, const char* f = 0, ...)
{
	switch (ec)
	{
	case 2:
		break;
	default:
		if (node)
			vsapi->freeNode(node);
		if (se)
			vsscript_freeScript(se);
		vsscript_finalize();
		if (ofh)
			fclose(ofh);
	}
	if (f)
	{
		va_list arg;
		va_start(arg, f);
		vfprintf(stderr, f, arg);
		va_end(arg);
	}
	return !!ec;
}

void print_status(int n, int nf, float f0, float f1)
{
	static const int p = (int)std::log10((float)nf) + 1;
	int p00 = (int)std::log10(f0);
	int p01 = p00 < 2 ? 2 : p00 < 3 ? 1 : 0;
	p00 = p00 < 2 ? 7 : p00 < 3 ? 6 : p00 + 1;
	int p02 = std::max(7 - p00, 0);
	const char* dot = ".";
	if (n == nf)
	{
		fprintf(stderr, "%*d/%*d, %4d ms / %*.*f%-*s fps",
			p, n, p, nf, int(f1 * 1000), p00, p01, f0, p02, dot + (p00 != 4));
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
		int p10 = (int)std::log10(f1);
		int p11 = p10 < 2 ? 2 : p10 < 3 ? 1 : 0;
		p10 = p10 < 2 ? 7 : p10 < 3 ? 6 : p10 + 1;
		int p12 = std::max(7 - p10, 0);
		fprintf(stderr, "%*d/%*d, %*.*f%-*s / %*.*f%-*s fps \r",
			p, n, p, nf, p10, p11, f1, p12, dot + (p10 != 4), p00, p01, f0, p02, dot + (p00 != 4));
	}
	fflush(stderr);
}

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
	std::vector<std::pair<std::string, std::string>> args;
	const char* ifname = 0, * ofname = 0;
	for (;;)
	{
		int longind = -1;
		int c = getopt_long(argc, argv, "-hi:yva:", longopts, &longind);
		if (c == -1)
			break;
		switch (c)
		{
		case 1:
			if (!ifname)
				ifname = optarg;
			else if (!ofname)
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
	if (!ifname)
		return 1;
	if (!vsscript_init())
		return cleanup(2, "vsscript_init() failed\n");
	vsapi = vsscript_getVSApi();
	if (!vsapi)
		return cleanup(1, "vsscript_getVSApi() failed\n");
	if (!args.empty())
	{
		if (vsscript_createScript(&se))
			return cleanup(1, "vsscript_createScript() failed:\n%s\n", vsscript_getError(se));
		VSMap* foldedArgs = vsapi->createMap();
		for (const auto& it : args)
			vsapi->propSetData(foldedArgs, it.first.c_str(), it.second.c_str(), it.second.size(), paAppend);
		vsscript_setVariable(se, foldedArgs);
		vsapi->freeMap(foldedArgs);
	}
	t0 = std::chrono::high_resolution_clock::now();
	if (vsscript_evaluateFile(&se, ifname, efSetWorkingDir))
		return cleanup(1, "vsscript_evaluateFile() failed:\n%s\n", vsscript_getError(se));
	f2 = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
	node = vsscript_getOutput(se, index);
	if (!node)
		return cleanup(1, "vsscript_getOutput() failed\n");
	const VSVideoInfo* vi = vsapi->getVideoInfo(node);
	if (!vi->format)
		return cleanup(1, "clips with varying format are not supported\n");
	VSCoreInfo ci;
	vsapi->getCoreInfo2(vsscript_getCore(se), &ci);
	int threads = ci.numThreads;
	int nf = end = vi->numFrames, height = vi->height, width = vi->width;
	int ssh = vi->format->subSamplingH, ssw = vi->format->subSamplingW, np = vi->format->numPlanes,
		cf = vi->format->colorFamily, st = vi->format->sampleType, depth = vi->format->bitsPerSample;
	const char* cf_str = cf == cmGray ? "gray" : cf == cmRGB ? "rgb" : ss_to_str(ssh, ssw);
	fprintf(stderr, "w = %d, h = %d, csp = %s, depth = %d%s, fps = %" PRId64 "/%" PRId64 ", fn = %d\n", width, height,
		cf_str ? cf_str : "?", depth, st != stFloat ? "" : "f", vi->fpsNum, vi->fpsDen, nf);
	if (hash)
	{
		XXH64_reset(&state0, 0);
		xxhs.resize(np, state0);
	}
	if (!ofname)
		y4m = 0, verbose = 1;
	else if (!strcmp(ofname, "-"))
	{
#ifdef _WIN32
		if (setmode(fileno(stdout), _O_BINARY) == -1)
			return cleanup(1, "setmode() failed\n");
#endif
		ofh = stdout;
	}
	else
	{
		if (ofh = fopen(ofname, "wb"); !ofh)
			return cleanup(1, "fopen() failed: %s\n", std::strerror(errno));
		verbose = 1;
	}
	if (y4m)
	{
		if (int ret = write_y4m_header(cf, ssh, ssw, st, depth, height, width, vi->fpsNum, vi->fpsDen, nf); !ret)
			return cleanup(1, "no y4m identifier exists for current format\n");
		else if (ret != 1)
			cleanup(1, "write_y4m_header() failed: %s\n", std::strerror(errno));
	}
	//
	std::unique_lock<std::mutex> lock(mutex);
	t0 = std::chrono::high_resolution_clock::now();
	t.emplace_front(0, t0);
	req = std::min(threads, nf);
	for (int n = 0, end = req; n < end; n++)
		vsapi->getFrameAsync(n, node, callback, nullptr);
	if (verbose)
	{
		while (k != end)
		{
			f0 = out / std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
			print_status(out, nf, f0, f1), u = 1;
			v.wait_for(lock, std::chrono::milliseconds(400));
		}
	}
	else
		v.wait(lock, [&]() { return k == end; });
	f0 = k / std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();
	if (verbose)
		print_status(k, end, f0, f2);
	return cleanup(b_ctrl_c);
}