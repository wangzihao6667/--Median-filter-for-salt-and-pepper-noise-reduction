#include "benchmark.h"
#include "common.h"

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

int main(int, char **) {
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    std::vector<char *> argv;
    if (wargv) {
        args.reserve((size_t)argc);
        for (int i = 0; i < argc; ++i) args.push_back(wstring_to_utf8(wargv[i]));
        LocalFree(wargv);
    } else {
        args.push_back("median_bench.exe");
    }
    argv.reserve(args.size());
    for (auto &s : args) argv.push_back(s.data());
    return run_benchmark((int)argv.size(), argv.data());
}
